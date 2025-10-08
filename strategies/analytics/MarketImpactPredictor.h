#pragma once

#include "../../core/orderbook/Order.h"
#include "../../core/orderbook/OrderBook.h"
#include "../config/StrategyConfig.h"
#include "OrderBookAnalyzer.h"

#include <atomic>
#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace pinnacle {
namespace analytics {

/**
 * @struct MarketImpactEvent
 * @brief Represents a historical market impact observation
 */
struct MarketImpactEvent {
  uint64_t timestamp;
  std::string orderId;
  OrderSide side;
  double orderSize;
  double priceBeforeOrder;
  double priceAfterOrder;
  double priceImpact;       // Absolute price change
  double relativeImpact;    // Impact as percentage of mid price
  double normalizedImpact;  // Impact per unit volume
  double liquidityConsumed; // Amount of liquidity consumed
  double timeToRecover;     // Time for price to return to pre-impact level (ms)
  double volumeAtImpact;    // Total order book volume at time of impact
  double spreadAtImpact;    // Spread at time of impact
  bool isTemporary;         // Whether impact was temporary (< 1s)

  MarketImpactEvent() = default;
  MarketImpactEvent(uint64_t ts, const std::string& id, OrderSide s,
                    double size, double priceBefore, double priceAfter)
      : timestamp(ts), orderId(id), side(s), orderSize(size),
        priceBeforeOrder(priceBefore), priceAfterOrder(priceAfter),
        priceImpact(std::abs(priceAfter - priceBefore)),
        relativeImpact(priceBefore > 0 ? priceImpact / priceBefore : 0.0),
        normalizedImpact(size > 0 ? priceImpact / size : 0.0),
        liquidityConsumed(0.0), timeToRecover(0.0), volumeAtImpact(0.0),
        spreadAtImpact(0.0), isTemporary(false) {}
};

/**
 * @struct ImpactPrediction
 * @brief Predicted market impact for a potential order
 */
struct ImpactPrediction {
  double predictedImpact;         // Predicted absolute price impact
  double predictedRelativeImpact; // Predicted relative impact (%)
  double confidence;              // Prediction confidence [0-1]
  double optimimalOrderSize;      // Recommended order size to minimize impact
  double costOfExecution;         // Estimated execution cost including impact
  double executionTime;           // Recommended execution timeframe (ms)
  std::vector<double> sliceSizes; // Recommended order slicing strategy
  double urgencyFactor;           // Market urgency factor [0-1]
  uint64_t timestamp;
  uint64_t validUntil; // Prediction validity time

  ImpactPrediction()
      : predictedImpact(0.0), predictedRelativeImpact(0.0), confidence(0.0),
        optimimalOrderSize(0.0), costOfExecution(0.0), executionTime(0.0),
        urgencyFactor(0.0), timestamp(0), validUntil(0) {}
};

/**
 * @struct ImpactModel
 * @brief Market impact model parameters and statistics
 */
struct ImpactModel {
  // Linear impact model: Impact = alpha * sqrt(OrderSize/AverageVolume) + beta
  // * (OrderSize/LiquidityDepth)
  double alpha{0.1};  // Square root impact coefficient
  double beta{0.05};  // Linear impact coefficient
  double gamma{0.02}; // Temporary impact decay factor

  // Model statistics
  double rsquared{0.0};          // Model fit quality
  double meanAbsoluteError{0.0}; // Mean absolute prediction error
  double meanSquaredError{0.0};  // Mean squared prediction error
  uint64_t observationCount{0};  // Number of observations used for fitting
  uint64_t lastUpdate{0};        // Last model update timestamp

  // Market regime parameters
  double volatilityFactor{1.0}; // Current volatility adjustment
  double liquidityFactor{1.0};  // Current liquidity adjustment
  double momentumFactor{1.0};   // Current momentum adjustment
};

/**
 * @struct OrderSizingRecommendation
 * @brief Recommendation for optimal order sizing and timing
 */
struct OrderSizingRecommendation {
  double targetQuantity;             // Total quantity to execute
  std::vector<double> sliceSizes;    // Recommended slice sizes
  std::vector<uint64_t> sliceTiming; // Recommended timing between slices (ms)
  double totalExpectedImpact;        // Total expected market impact
  double executionCost;              // Total estimated execution cost
  double timeToComplete;             // Total estimated execution time (ms)
  double riskScore;                  // Execution risk score [0-1]
  std::string strategy;              // Recommended execution strategy
  uint64_t timestamp;
};

/**
 * @class MarketImpactPredictor
 * @brief Predicts market impact of orders and optimizes execution strategies
 */
class MarketImpactPredictor {
public:
  /**
   * @brief Constructor
   * @param symbol Trading symbol
   * @param maxHistorySize Maximum number of impact events to keep
   * @param modelUpdateInterval Model update interval in milliseconds
   */
  explicit MarketImpactPredictor(const std::string& symbol,
                                 size_t maxHistorySize = 10000,
                                 uint64_t modelUpdateInterval = 60000);

  /**
   * @brief Destructor
   */
  ~MarketImpactPredictor() = default;

  // Delete copy and move operations
  MarketImpactPredictor(const MarketImpactPredictor&) = delete;
  MarketImpactPredictor& operator=(const MarketImpactPredictor&) = delete;
  MarketImpactPredictor(MarketImpactPredictor&&) = delete;
  MarketImpactPredictor& operator=(MarketImpactPredictor&&) = delete;

  /**
   * @brief Initialize the predictor
   * @param orderBook Shared pointer to the order book
   * @param flowAnalyzer Shared pointer to flow analyzer for enhanced
   * predictions
   * @return true if initialization succeeded
   */
  bool initialize(std::shared_ptr<OrderBook> orderBook,
                  std::shared_ptr<OrderBookAnalyzer> flowAnalyzer = nullptr);

  /**
   * @brief Start impact prediction
   * @return true if started successfully
   */
  bool start();

  /**
   * @brief Stop impact prediction
   * @return true if stopped successfully
   */
  bool stop();

  /**
   * @brief Check if predictor is running
   * @return true if running
   */
  bool isRunning() const { return m_isRunning.load(); }

  /**
   * @brief Record a market impact event
   * @param event The impact event to record
   */
  void recordImpactEvent(const MarketImpactEvent& event);

  /**
   * @brief Predict market impact for a potential order
   * @param side Order side (buy/sell)
   * @param orderSize Order size
   * @param urgency Execution urgency [0-1], 0=patient, 1=immediate
   * @return Impact prediction
   */
  ImpactPrediction predictImpact(OrderSide side, double orderSize,
                                 double urgency = 0.5) const;

  /**
   * @brief Get optimal order sizing recommendation
   * @param side Order side
   * @param totalQuantity Total quantity to execute
   * @param maxImpact Maximum acceptable impact (as % of mid price)
   * @param timeHorizon Maximum execution time horizon (ms)
   * @return Order sizing recommendation
   */
  OrderSizingRecommendation
  getOptimalSizing(OrderSide side, double totalQuantity,
                   double maxImpact = 0.001,            // 0.1%
                   uint64_t timeHorizon = 60000) const; // 1 minute

  /**
   * @brief Calculate execution cost including market impact
   * @param side Order side
   * @param quantity Order quantity
   * @param currentMidPrice Current mid price
   * @return Total execution cost per unit
   */
  double calculateExecutionCost(OrderSide side, double quantity,
                                double currentMidPrice) const;

  /**
   * @brief Analyze historical impact patterns
   * @param lookbackPeriod Analysis period in milliseconds
   * @return Impact analysis statistics
   */
  struct ImpactAnalysis {
    double averageImpact{0.0};
    double medianImpact{0.0};
    double impactVolatility{0.0};
    double averageRecoveryTime{0.0};
    double temporaryImpactRatio{0.0};
    uint64_t sampleCount{0};
    std::vector<double> impactPercentiles; // 5%, 25%, 50%, 75%, 95%
  };

  ImpactAnalysis analyzeHistoricalImpact(
      uint64_t lookbackPeriod = 3600000) const; // 1 hour default

  /**
   * @brief Get current impact model parameters
   * @return Current impact model
   */
  ImpactModel getCurrentModel() const;

  /**
   * @brief Force model retraining with current data
   * @return true if retraining was successful
   */
  bool retrainModel();

  /**
   * @brief Get impact prediction statistics
   * @return String representation of statistics
   */
  std::string getImpactStatistics() const;

  /**
   * @brief Reset predictor state
   */
  void reset();

  /**
   * @brief Get the symbol being analyzed
   * @return Trading symbol
   */
  const std::string& getSymbol() const { return m_symbol; }

  /**
   * @brief Update market regime factors based on current conditions
   * @param volatility Current volatility measure
   * @param liquidity Current liquidity measure
   * @param momentum Current momentum measure
   */
  void updateMarketRegime(double volatility, double liquidity, double momentum);

private:
  // Configuration
  std::string m_symbol;
  size_t m_maxHistorySize;
  uint64_t m_modelUpdateInterval;

  // Order book and flow analyzer references
  std::shared_ptr<OrderBook> m_orderBook;
  std::shared_ptr<OrderBookAnalyzer> m_flowAnalyzer;

  // Running state
  std::atomic<bool> m_isRunning{false};

  // Impact event history
  std::deque<MarketImpactEvent> m_impactHistory;
  mutable std::mutex m_historyMutex;

  // Current impact model
  ImpactModel m_currentModel;
  mutable std::mutex m_modelMutex;

  // Ongoing order tracking for impact measurement
  struct OrderTracker {
    std::string orderId;
    OrderSide side;
    double size;
    double priceAtPlacement;
    uint64_t placementTime;
    bool isActive{true};
  };

  std::unordered_map<std::string, OrderTracker> m_activeOrders;
  mutable std::mutex m_ordersMutex;

  // Price history for impact calculation
  struct PriceSnapshot {
    double midPrice;
    double bidPrice;
    double askPrice;
    double totalVolume;
    double spread;
    uint64_t timestamp;
  };

  std::deque<PriceSnapshot> m_priceHistory;
  mutable std::mutex m_priceMutex;

  // Model fitting and prediction methods
  void updateImpactModel();
  double calculateLinearImpact(OrderSide side, double orderSize) const;
  double calculateSqrtImpact(OrderSide side, double orderSize) const;
  double calculateTemporaryImpact(OrderSide side, double orderSize) const;
  double calculateLiquidityBasedImpact(OrderSide side, double orderSize) const;

  // Order sizing optimization
  std::vector<double> optimizeOrderSlicing(OrderSide side, double totalQuantity,
                                           double maxImpact,
                                           uint64_t timeHorizon) const;
  double calculateSlicingCost(const std::vector<double>& slices,
                              OrderSide side) const;

  // Historical analysis methods
  std::vector<MarketImpactEvent> getEventsInPeriod(uint64_t startTime,
                                                   uint64_t endTime) const;
  double calculateAverageVolume(uint64_t lookbackPeriod) const;
  double calculateAverageSpread(uint64_t lookbackPeriod) const;
  double getCurrentLiquidityDepth(OrderSide side) const;

  // Utility methods
  uint64_t getCurrentTimestamp() const;
  void updatePriceHistory();
  void cleanupOldData();
  bool isValidImpactEvent(const MarketImpactEvent& event) const;

  // Market condition assessment
  double assessMarketCondition() const;
  double calculateVolatilityFactor() const;
  double calculateLiquidityFactor() const;
  double calculateMomentumFactor() const;
};

/**
 * @brief Helper function to create market impact event from order execution
 */
MarketImpactEvent createImpactEvent(const std::string& orderId, OrderSide side,
                                    double orderSize, double priceBefore,
                                    double priceAfter, uint64_t timestamp);

/**
 * @brief Helper function to calculate impact cost in basis points
 */
double calculateImpactCostBps(double priceImpact, double midPrice);

/**
 * @brief Helper function to estimate optimal order size for given impact
 * tolerance
 */
double estimateOptimalSize(double maxImpactBps, double midPrice,
                           double avgVolume, const ImpactModel& model);

} // namespace analytics
} // namespace pinnacle
