#pragma once

#include "../../core/orderbook/Order.h"
#include "../../core/orderbook/OrderBook.h"
#include "../config/StrategyConfig.h"

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
 * @struct OrderFlowEvent
 * @brief Represents a single order flow event in the order book
 */
struct OrderFlowEvent {
  enum class Type : uint8_t {
    ADD_ORDER,
    CANCEL_ORDER,
    FILL_ORDER,
    PRICE_LEVEL_CHANGE
  };

  Type type;
  uint64_t timestamp;
  std::string orderId;
  OrderSide side;
  double price;
  double quantity;
  double newTotalQuantity; // For price level changes

  OrderFlowEvent(Type t, uint64_t ts, const std::string& id, OrderSide s,
                 double p, double q, double total = 0.0)
      : type(t), timestamp(ts), orderId(id), side(s), price(p), quantity(q),
        newTotalQuantity(total) {}
};

/**
 * @struct FlowMetrics
 * @brief Aggregated flow metrics for analysis
 */
struct FlowMetrics {
  // Order flow rates (orders per second)
  double bidOrderRate{0.0};
  double askOrderRate{0.0};
  double bidCancelRate{0.0};
  double askCancelRate{0.0};

  // Volume flow rates (volume per second)
  double bidVolumeRate{0.0};
  double askVolumeRate{0.0};

  // Liquidity metrics
  double liquidityImbalance{
      0.0}; // (bid_volume - ask_volume) / (bid_volume + ask_volume)
  double orderSizeImbalance{
      0.0}; // (avg_bid_size - avg_ask_size) / (avg_bid_size + avg_ask_size)

  // Market impact indicators
  double aggressiveOrderRatio{0.0}; // Ratio of market orders to total orders
  double largeOrderRatio{0.0};      // Ratio of large orders (>95th percentile)

  // Persistence metrics
  double orderPersistence{
      0.0}; // Average time orders stay in book before cancel/fill
  double levelPersistence{0.0}; // Average time price levels persist

  // Flow velocity
  double bidFlowVelocity{0.0}; // Rate of change in bid flow
  double askFlowVelocity{0.0}; // Rate of change in ask flow

  // Toxicity indicators
  double adverseSelectionRatio{0.0}; // Ratio of immediately filled orders
  double informationContent{0.0};    // Price impact per unit volume

  uint64_t timestamp{0};
  uint64_t windowStartTime{0};
  uint64_t windowEndTime{0};
};

/**
 * @struct LiquidityPrediction
 * @brief Predicted liquidity conditions
 */
struct LiquidityPrediction {
  double predictedBidLiquidity{0.0};
  double predictedAskLiquidity{0.0};
  double liquidityScore{0.0};    // 0-1 score, higher = more liquid
  double confidence{0.0};        // Prediction confidence 0-1
  uint64_t predictionHorizon{0}; // Microseconds into future
  uint64_t timestamp{0};
};

/**
 * @class OrderBookAnalyzer
 * @brief Analyzes order book flow patterns for enhanced market making decisions
 */
class OrderBookAnalyzer {
public:
  /**
   * @brief Constructor
   * @param symbol Trading symbol
   * @param windowSizeMs Analysis window size in milliseconds
   * @param maxEvents Maximum number of events to keep in history
   */
  explicit OrderBookAnalyzer(const std::string& symbol,
                             uint64_t windowSizeMs = 1000,
                             size_t maxEvents = 10000);

  /**
   * @brief Destructor
   */
  ~OrderBookAnalyzer() = default;

  // Delete copy and move operations
  OrderBookAnalyzer(const OrderBookAnalyzer&) = delete;
  OrderBookAnalyzer& operator=(const OrderBookAnalyzer&) = delete;
  OrderBookAnalyzer(OrderBookAnalyzer&&) = delete;
  OrderBookAnalyzer& operator=(OrderBookAnalyzer&&) = delete;

  /**
   * @brief Initialize the analyzer
   * @param orderBook Shared pointer to the order book to analyze
   * @return true if initialization succeeded
   */
  bool initialize(std::shared_ptr<OrderBook> orderBook);

  /**
   * @brief Start flow analysis
   * @return true if started successfully
   */
  bool start();

  /**
   * @brief Stop flow analysis
   * @return true if stopped successfully
   */
  bool stop();

  /**
   * @brief Check if analyzer is running
   * @return true if running
   */
  bool isRunning() const { return m_isRunning.load(); }

  /**
   * @brief Record an order flow event
   * @param event The flow event to record
   */
  void recordEvent(const OrderFlowEvent& event);

  /**
   * @brief Get current flow metrics
   * @return Current aggregated flow metrics
   */
  FlowMetrics getCurrentMetrics() const;

  /**
   * @brief Predict future liquidity conditions
   * @param horizonMs Prediction horizon in milliseconds
   * @return Liquidity prediction
   */
  LiquidityPrediction predictLiquidity(uint64_t horizonMs = 100) const;

  /**
   * @brief Calculate optimal spread adjustment based on flow analysis
   * @param baseSpread Base spread from other strategies
   * @param currentMetrics Current flow metrics
   * @return Suggested spread adjustment factor (multiply base spread by this)
   */
  double
  calculateFlowBasedSpreadAdjustment(double baseSpread,
                                     const FlowMetrics& currentMetrics) const;

  /**
   * @brief Get order book imbalance analysis
   * @param depth Number of levels to analyze
   * @return Imbalance metrics
   */
  struct ImbalanceAnalysis {
    double volumeImbalance{0.0};
    double orderCountImbalance{0.0};
    double weightedImbalance{0.0};
    double topLevelImbalance{0.0};
    uint64_t timestamp{0};
  };

  ImbalanceAnalysis analyzeImbalance(size_t depth = 5) const;

  /**
   * @brief Detect flow regime changes
   * @return true if significant regime change detected
   */
  bool detectRegimeChange() const;

  /**
   * @brief Get flow statistics for the current window
   * @return String representation of flow statistics
   */
  std::string getFlowStatistics() const;

  /**
   * @brief Reset analyzer state
   */
  void reset();

  /**
   * @brief Get the symbol being analyzed
   * @return Trading symbol
   */
  const std::string& getSymbol() const { return m_symbol; }

private:
  // Configuration
  std::string m_symbol;
  uint64_t m_windowSizeMs;
  size_t m_maxEvents;

  // Order book reference
  std::shared_ptr<OrderBook> m_orderBook;

  // Running state
  std::atomic<bool> m_isRunning{false};

  // Event history
  std::deque<OrderFlowEvent> m_eventHistory;
  mutable std::mutex m_eventMutex;

  // Cached metrics
  mutable FlowMetrics m_cachedMetrics;
  mutable uint64_t m_lastMetricsUpdate{0};
  mutable std::mutex m_metricsMutex;

  // Price level tracking
  struct PriceLevelInfo {
    double quantity;
    uint64_t firstSeenTime;
    uint64_t lastUpdateTime;
    size_t orderCount;
  };

  std::unordered_map<double, PriceLevelInfo> m_bidLevels;
  std::unordered_map<double, PriceLevelInfo> m_askLevels;
  mutable std::mutex m_levelsMutex;

  // Order tracking for persistence analysis
  struct OrderInfo {
    uint64_t addTime;
    uint64_t removeTime{0}; // 0 if still active
    double quantity;
    OrderSide side;
    bool wasFilled{false};
  };

  std::unordered_map<std::string, OrderInfo> m_orderTracking;
  mutable std::mutex m_orderMutex;

  // Flow velocity tracking
  struct FlowVelocityData {
    double bidVolume{0.0};
    double askVolume{0.0};
    uint64_t timestamp{0};
  };

  std::deque<FlowVelocityData> m_velocityHistory;
  mutable std::mutex m_velocityMutex;

  // Internal calculation methods
  void updateMetrics() const;
  void cleanupOldEvents();
  void updatePriceLevelInfo(double price, double newQuantity, OrderSide side);
  void updateOrderTracking(const OrderFlowEvent& event);
  void updateFlowVelocity();

  // Analysis helper methods
  double calculateOrderRate(OrderSide side, OrderFlowEvent::Type eventType,
                            uint64_t windowMs) const;
  double calculateVolumeRate(OrderSide side, uint64_t windowMs) const;
  double calculateAverageOrderSize(OrderSide side, uint64_t windowMs) const;
  double calculateOrderPersistence() const;
  double calculateLevelPersistence() const;
  double calculateFlowVelocity(OrderSide side) const;
  double calculateAdverseSelectionRatio() const;
  double calculateInformationContent() const;

  // Prediction methods
  LiquidityPrediction predictLiquidityInternal(uint64_t horizonMs) const;
  double calculateLiquidityScore(const FlowMetrics& metrics) const;

  // Utility methods
  uint64_t getCurrentTimestamp() const;
  bool isEventInWindow(const OrderFlowEvent& event, uint64_t windowMs) const;
};

} // namespace analytics
} // namespace pinnacle
