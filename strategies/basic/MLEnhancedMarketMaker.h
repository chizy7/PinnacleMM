#pragma once

#include "../../core/utils/TimeUtils.h"
#include "../analytics/MarketImpactPredictor.h"
#include "../analytics/MarketRegimeDetector.h"
#include "../analytics/OrderBookAnalyzer.h"
#include "../ml/MLSpreadOptimizer.h"
#include "../rl/RLParameterAdapter.h"
#include "BasicMarketMaker.h"

#include <memory>
#include <string>

namespace pinnacle {
namespace strategy {

/**
 * @class MLEnhancedMarketMaker
 * @brief Enhanced market maker using ML-based spread optimization
 *
 * This class extends BasicMarketMaker with machine learning capabilities
 * for dynamic spread optimization. It maintains backward compatibility
 * while providing advanced spread prediction and optimization features.
 */
class MLEnhancedMarketMaker : public BasicMarketMaker {
public:
  /**
   * @brief Configuration for ML enhancements
   */
  struct MLConfig {
    bool enableMLSpreadOptimization{true};
    bool enableOnlineLearning{true};
    bool fallbackToHeuristics{true};
    double mlConfidenceThreshold{0.5};

    // Flow analysis configuration
    bool enableFlowAnalysis{true};
    uint64_t flowAnalysisWindowMs{1000}; // 1 second window
    size_t maxFlowEvents{10000};
    double flowSpreadAdjustmentWeight{0.3}; // Weight for flow-based adjustments

    // Market impact prediction configuration
    bool enableImpactPrediction{true};
    size_t maxImpactHistorySize{10000};
    uint64_t impactModelUpdateInterval{60000}; // 1 minute
    double maxOrderSizeImpactRatio{0.001};     // Max 0.1% impact
    double impactSpreadAdjustmentWeight{
        0.2}; // Weight for impact-based adjustments

    // ML model configuration
    ml::MLSpreadOptimizer::Config optimizerConfig;

    // Performance monitoring
    bool enablePerformanceTracking{true};
    uint64_t performanceReportIntervalMs{60000}; // 1 minute

    // Risk management for ML predictions
    double maxSpreadDeviationRatio{2.0}; // Max deviation from base spread
    double minConfidenceForExecution{0.3};

    // RL parameter adaptation configuration
    bool enableRLParameterAdaptation{true};
    rl::RLParameterAdapter::Config rlConfig;

    // Market regime detection configuration
    bool enableRegimeDetection{true};
    pinnacle::analytics::RegimeConfiguration regimeConfig;
    double regimeSpreadAdjustmentWeight{
        0.4}; // Weight for regime-based adjustments
    bool enableRegimeAwareParameterAdaptation{true};

    MLConfig() {}
  };

  /**
   * @brief Constructor
   */
  MLEnhancedMarketMaker(const std::string& symbol, const StrategyConfig& config,
                        const MLConfig& mlConfig);

  /**
   * @brief Destructor
   */
  ~MLEnhancedMarketMaker();

  /**
   * @brief Initialize the enhanced strategy
   */
  bool initialize(std::shared_ptr<OrderBook> orderBook);

  /**
   * @brief Start the strategy with ML components
   */
  bool start();

  /**
   * @brief Stop the strategy and ML components
   */
  bool stop();

  /**
   * @brief Handle order book updates with ML feature extraction
   */
  void onOrderBookUpdate(const OrderBook& orderBook);

  /**
   * @brief Handle trades with ML outcome tracking
   */
  void onTrade(const std::string& symbol, double price, double quantity,
               OrderSide side, uint64_t timestamp);

  /**
   * @brief Handle order updates with ML performance tracking
   */
  void onOrderUpdate(const std::string& orderId, OrderStatus status,
                     double filledQuantity, uint64_t timestamp);

  /**
   * @brief Get enhanced statistics including ML metrics
   */
  std::string getStatistics() const;

  /**
   * @brief Update configuration including ML parameters
   */
  bool updateConfig(const StrategyConfig& config);

  /**
   * @brief Update ML-specific configuration
   */
  bool updateMLConfig(const MLConfig& mlConfig);

  /**
   * @brief Get current ML performance metrics
   */
  ml::ModelMetrics getMLMetrics() const;

  /**
   * @brief Get feature importance from ML model
   */
  std::vector<std::pair<std::string, double>> getFeatureImportance() const;

  /**
   * @brief Force ML model retraining
   */
  void forceMLRetraining();

  /**
   * @brief Save ML model to file
   */
  bool saveMLModel(const std::string& filename) const;

  /**
   * @brief Load ML model from file
   */
  bool loadMLModel(const std::string& filename);

  /**
   * @brief Get current ML model status
   */
  bool isMLModelReady() const;

  /**
   * @brief Get current flow analysis metrics
   */
  pinnacle::analytics::FlowMetrics getFlowMetrics() const;

  /**
   * @brief Get order book imbalance analysis
   */
  pinnacle::analytics::OrderBookAnalyzer::ImbalanceAnalysis
  getImbalanceAnalysis(size_t depth = 5) const;

  /**
   * @brief Get liquidity prediction
   */
  pinnacle::analytics::LiquidityPrediction
  getLiquidityPrediction(uint64_t horizonMs = 100) const;

  /**
   * @brief Get flow analysis statistics
   */
  std::string getFlowStatistics() const;

  /**
   * @brief Check if flow analysis is enabled and running
   */
  bool isFlowAnalysisEnabled() const;

  /**
   * @brief Predict market impact for a potential order
   */
  pinnacle::analytics::ImpactPrediction
  predictOrderImpact(OrderSide side, double orderSize,
                     double urgency = 0.5) const;

  /**
   * @brief Get optimal order sizing recommendation
   */
  pinnacle::analytics::OrderSizingRecommendation
  getOptimalOrderSizing(OrderSide side, double totalQuantity,
                        double maxImpact = 0.001,
                        uint64_t timeHorizon = 60000) const;

  /**
   * @brief Calculate execution cost including market impact
   */
  double calculateExecutionCost(OrderSide side, double quantity,
                                double currentMidPrice) const;

  /**
   * @brief Get current impact model statistics
   */
  std::string getImpactStatistics() const;

  /**
   * @brief Check if impact prediction is enabled and running
   */
  bool isImpactPredictionEnabled() const;

  /**
   * @brief Get current RL parameter values
   */
  std::unordered_map<rl::ParameterType, double> getRLParameters() const;

  /**
   * @brief Set RL parameter value manually
   */
  void setRLParameter(rl::ParameterType parameter, double value);

  /**
   * @brief Enable/disable RL adaptation for specific parameter
   */
  void enableRLParameterAdaptation(rl::ParameterType parameter, bool enable);

  /**
   * @brief Get RL performance statistics
   */
  std::string getRLStatistics() const;

  /**
   * @brief Get current market state for RL
   */
  rl::MarketState getCurrentMarketState() const;

  /**
   * @brief Force RL episode end (for testing/debugging)
   */
  void forceRLEpisodeEnd();

  /**
   * @brief Save RL model to file
   */
  bool saveRLModel(const std::string& filename) const;

  /**
   * @brief Load RL model from file
   */
  bool loadRLModel(const std::string& filename);

  /**
   * @brief Check if RL parameter adaptation is enabled and running
   */
  bool isRLAdaptationEnabled() const;

  // ============================================================================
  // Market Regime Detection API
  // ============================================================================

  /**
   * @brief Get current market regime
   */
  pinnacle::analytics::MarketRegime getCurrentRegime() const;

  /**
   * @brief Get current regime metrics
   */
  pinnacle::analytics::RegimeMetrics getCurrentRegimeMetrics() const;

  /**
   * @brief Get regime confidence level
   */
  double getRegimeConfidence() const;

  /**
   * @brief Get recent regime transitions
   */
  std::vector<pinnacle::analytics::RegimeTransition>
  getRecentRegimeTransitions(size_t count = 10) const;

  /**
   * @brief Get regime detection statistics
   */
  std::string getRegimeStatistics() const;

  /**
   * @brief Update regime detection configuration
   */
  bool
  updateRegimeConfig(const pinnacle::analytics::RegimeConfiguration& config);

  /**
   * @brief Check if regime detection is enabled and running
   */
  bool isRegimeDetectionEnabled() const;

  /**
   * @brief Save regime detection model to file
   */
  bool saveRegimeModel(const std::string& filename) const;

  /**
   * @brief Load regime detection model from file
   */
  bool loadRegimeModel(const std::string& filename);

protected:
  /**
   * @brief Enhanced spread calculation using ML
   */
  double calculateTargetSpread() const override;

private:
  MLConfig m_mlConfig;
  std::unique_ptr<ml::MLSpreadOptimizer> m_mlOptimizer;
  std::unique_ptr<pinnacle::analytics::OrderBookAnalyzer> m_flowAnalyzer;
  std::unique_ptr<pinnacle::analytics::MarketImpactPredictor> m_impactPredictor;
  std::unique_ptr<pinnacle::analytics::MarketRegimeDetector> m_regimeDetector;
  std::unique_ptr<rl::RLParameterAdapter> m_rlAdapter;

  // Performance tracking for ML predictions
  struct MLPerformanceTracker {
    struct PredictionOutcome {
      ml::SpreadPrediction prediction;
      double actualSpread;
      double realizedPnL;
      double fillRate;
      uint64_t timestamp;
      bool wasMLUsed;
    };

    std::deque<PredictionOutcome> outcomes;
    mutable std::mutex outcomesMutex;

    // Aggregate metrics
    double totalMLPnL{0.0};
    double totalHeuristicPnL{0.0};
    uint64_t mlPredictionCount{0};
    uint64_t heuristicPredictionCount{0};
    uint64_t correctMLPredictions{0};
    uint64_t correctHeuristicPredictions{0};

    void addOutcome(const PredictionOutcome& outcome);
    void updateMetrics();
    std::string getReport() const;
  };

  MLPerformanceTracker m_performanceTracker;

  // Market data collection for ML
  struct MarketSnapshot {
    double midPrice;
    double bidPrice;
    double askPrice;
    double bidVolume;
    double askVolume;
    double tradeVolume;
    uint64_t timestamp;
  };

  MarketSnapshot m_lastSnapshot;
  mutable std::mutex m_snapshotMutex;

  // Current spread prediction state
  struct CurrentPrediction {
    ml::SpreadPrediction prediction;
    bool isValid{false};
    uint64_t timestamp{0};
    double baseFallbackSpread{0.0};
  };

  mutable CurrentPrediction m_currentPrediction;
  mutable std::mutex m_predictionMutex;

  // Performance reporting
  uint64_t m_lastPerformanceReport{0};
  mutable std::mutex m_reportMutex;

  // Helper methods
  ml::MarketFeatures extractMarketFeatures() const;
  ml::MarketFeatures extractFlowEnhancedFeatures() const;
  double calculateHeuristicSpread() const;
  double calculateFlowEnhancedSpread() const;
  double calculateImpactAwareSpread() const;
  bool shouldUseMLPrediction(const ml::SpreadPrediction& prediction) const;
  void updateMLWithOutcome(double actualSpread, double pnl, double fillRate);
  void generatePerformanceReport();
  void collectMarketData();
  void recordFlowEvent(const pinnacle::analytics::OrderFlowEvent& event);
  void recordImpactEvent(const pinnacle::analytics::MarketImpactEvent& event);

  // Risk management for ML predictions
  double validateAndAdjustMLSpread(double mlSpread, double baseSpread) const;
  bool isMLSpreadSafe(double mlSpread, double baseSpread) const;

  // RL integration methods
  void updateRLMarketState();
  void recordRLPerformance();
  void applyRLParameterAdaptation() const;
  rl::MarketState extractRLMarketState() const;

  // Regime detection integration methods
  void updateRegimeDetector(const OrderBook& orderBook);
  double calculateRegimeAwareSpread() const;
};

} // namespace strategy
} // namespace pinnacle
