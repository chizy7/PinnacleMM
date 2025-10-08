#include "MLEnhancedMarketMaker.h"
#include "../../core/utils/TimeUtils.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace pinnacle {
namespace strategy {

MLEnhancedMarketMaker::MLEnhancedMarketMaker(const std::string& symbol,
                                             const StrategyConfig& config,
                                             const MLConfig& mlConfig)
    : BasicMarketMaker(symbol, config), m_mlConfig(mlConfig) {

  // Initialize ML optimizer if enabled
  if (m_mlConfig.enableMLSpreadOptimization) {
    m_mlOptimizer =
        std::make_unique<ml::MLSpreadOptimizer>(m_mlConfig.optimizerConfig);
  }

  // Initialize flow analyzer if enabled
  if (m_mlConfig.enableFlowAnalysis) {
    m_flowAnalyzer = std::make_unique<pinnacle::analytics::OrderBookAnalyzer>(
        symbol, m_mlConfig.flowAnalysisWindowMs, m_mlConfig.maxFlowEvents);
  }

  // Initialize impact predictor if enabled
  if (m_mlConfig.enableImpactPrediction) {
    m_impactPredictor =
        std::make_unique<pinnacle::analytics::MarketImpactPredictor>(
            symbol, m_mlConfig.maxImpactHistorySize,
            m_mlConfig.impactModelUpdateInterval);
  }

  // Initialize market regime detector if enabled
  if (m_mlConfig.enableRegimeDetection) {
    m_regimeDetector =
        std::make_unique<pinnacle::analytics::MarketRegimeDetector>(
            m_mlConfig.regimeConfig);
  }

  // Initialize RL parameter adapter if enabled
  if (m_mlConfig.enableRLParameterAdaptation) {
    m_rlAdapter =
        std::make_unique<rl::RLParameterAdapter>(symbol, m_mlConfig.rlConfig);
  }
}

MLEnhancedMarketMaker::~MLEnhancedMarketMaker() {
  // Ensure strategy is stopped before destruction
  stop();
}

bool MLEnhancedMarketMaker::initialize(std::shared_ptr<OrderBook> orderBook) {
  // Initialize base strategy
  if (!BasicMarketMaker::initialize(orderBook)) {
    return false;
  }

  // Initialize ML components
  if (m_mlOptimizer && !m_mlOptimizer->initialize()) {
    if (!m_mlConfig.fallbackToHeuristics) {
      return false;
    }
    // Continue with heuristics if fallback is enabled
  }

  // Initialize flow analyzer
  if (m_flowAnalyzer && !m_flowAnalyzer->initialize(orderBook)) {
    if (!m_mlConfig.fallbackToHeuristics) {
      return false;
    }
    // Continue without flow analysis if fallback is enabled
  }

  // Initialize impact predictor
  std::shared_ptr<pinnacle::analytics::OrderBookAnalyzer> sharedFlowAnalyzer =
      m_flowAnalyzer ? std::shared_ptr<pinnacle::analytics::OrderBookAnalyzer>(
                           m_flowAnalyzer.get(),
                           [](pinnacle::analytics::OrderBookAnalyzer*) {})
                     : nullptr;
  if (m_impactPredictor &&
      !m_impactPredictor->initialize(orderBook, sharedFlowAnalyzer)) {
    if (!m_mlConfig.fallbackToHeuristics) {
      return false;
    }
    // Continue without impact prediction if fallback is enabled
  }

  // Initialize market regime detector
  if (m_regimeDetector && !m_regimeDetector->initialize()) {
    if (!m_mlConfig.fallbackToHeuristics) {
      return false;
    }
    // Continue without regime detection if fallback is enabled
  }

  // Initialize RL parameter adapter
  if (m_rlAdapter && !m_rlAdapter->initialize()) {
    if (!m_mlConfig.fallbackToHeuristics) {
      return false;
    }
    // Continue without RL adaptation if fallback is enabled
  }

  return true;
}

bool MLEnhancedMarketMaker::start() {
  // Start base strategy
  if (!BasicMarketMaker::start()) {
    return false;
  }

  // Start flow analyzer
  if (m_flowAnalyzer && !m_flowAnalyzer->start()) {
    // Log warning but continue without flow analysis
  }

  // Start impact predictor
  if (m_impactPredictor && !m_impactPredictor->start()) {
    // Log warning but continue without impact prediction
  }

  // Start RL parameter adapter
  if (m_rlAdapter && !m_rlAdapter->start()) {
    // Log warning but continue without RL adaptation
  }

  // Initialize performance tracking
  m_lastPerformanceReport = utils::TimeUtils::getCurrentNanos();

  return true;
}

bool MLEnhancedMarketMaker::stop() {
  // Generate final performance report
  if (m_mlConfig.enablePerformanceTracking) {
    generatePerformanceReport();
  }

  // Stop flow analyzer
  if (m_flowAnalyzer) {
    m_flowAnalyzer->stop();
  }

  // Stop impact predictor
  if (m_impactPredictor) {
    m_impactPredictor->stop();
  }

  // Stop RL parameter adapter
  if (m_rlAdapter) {
    m_rlAdapter->stop();
  }

  // Stop base strategy
  return BasicMarketMaker::stop();
}

void MLEnhancedMarketMaker::onOrderBookUpdate(const OrderBook& orderBook) {
  // Collect market data for ML
  collectMarketData();

  // Update regime detector with market data
  updateRegimeDetector(orderBook);

  // Update RL market state
  updateRLMarketState();

  // Call base implementation
  BasicMarketMaker::onOrderBookUpdate(orderBook);
}

void MLEnhancedMarketMaker::onTrade(const std::string& symbol, double price,
                                    double quantity, OrderSide side,
                                    uint64_t timestamp) {
  // Update ML optimizer with trade data
  if (m_mlOptimizer && symbol == m_symbol) {
    std::lock_guard<std::mutex> lock(m_snapshotMutex);
    m_mlOptimizer->addMarketData(
        m_lastSnapshot.midPrice, m_lastSnapshot.bidPrice,
        m_lastSnapshot.askPrice, m_lastSnapshot.bidVolume,
        m_lastSnapshot.askVolume, quantity, getPosition(), timestamp);
  }

  // Call base implementation
  BasicMarketMaker::onTrade(symbol, price, quantity, side, timestamp);
}

void MLEnhancedMarketMaker::onOrderUpdate(const std::string& orderId,
                                          OrderStatus status,
                                          double filledQuantity,
                                          uint64_t timestamp) {
  // Track order outcome for ML performance
  if (status == OrderStatus::FILLED ||
      status == OrderStatus::PARTIALLY_FILLED) {
    // Calculate realized P&L and fill rate for this update
    double fillRate =
        filledQuantity; // Simplified - would need total order quantity

    // Update ML with outcome if we have a recent prediction
    {
      std::lock_guard<std::mutex> lock(m_predictionMutex);
      if (m_currentPrediction.isValid &&
          (timestamp - m_currentPrediction.timestamp) <
              10000000000ULL) { // 10 seconds
        updateMLWithOutcome(m_currentPrediction.prediction.optimalSpread,
                            getPnL(), fillRate);
      }
    }

    // Record RL performance
    recordRLPerformance();
  }

  // Call base implementation
  BasicMarketMaker::onOrderUpdate(orderId, status, filledQuantity, timestamp);
}

std::string MLEnhancedMarketMaker::getStatistics() const {
  std::ostringstream oss;

  // Get base statistics
  oss << BasicMarketMaker::getStatistics();

  // Add ML-specific statistics
  if (m_mlOptimizer) {
    oss << "\n=== ML Enhancement Statistics ===" << std::endl;

    auto mlMetrics = m_mlOptimizer->getMetrics();
    oss << "  ML Model Ready: " << (isMLModelReady() ? "Yes" : "No")
        << std::endl;
    oss << "  Total ML Predictions: " << mlMetrics.totalPredictions
        << std::endl;
    oss << "  ML Model Accuracy: " << std::fixed << std::setprecision(2)
        << (mlMetrics.accuracy * 100) << "%" << std::endl;
    oss << "  Avg Prediction Time: " << std::fixed << std::setprecision(1)
        << mlMetrics.avgPredictionTime << " μs" << std::endl;
    oss << "  Model Retrain Count: " << mlMetrics.retrainCount << std::endl;

    // Performance comparison
    oss << m_performanceTracker.getReport();
  }

  // Add flow analysis statistics
  if (m_flowAnalyzer && isFlowAnalysisEnabled()) {
    oss << "\n=== Flow Analysis Statistics ===" << std::endl;
    oss << getFlowStatistics();
  }

  // Add impact prediction statistics
  if (m_impactPredictor && isImpactPredictionEnabled()) {
    oss << "\n=== Impact Prediction Statistics ===" << std::endl;
    oss << getImpactStatistics();
  }

  // Add RL parameter adaptation statistics
  if (m_rlAdapter && isRLAdaptationEnabled()) {
    oss << "\n=== RL Parameter Adaptation Statistics ===" << std::endl;
    oss << getRLStatistics();
  }

  // Generate performance report if needed
  if (m_mlConfig.enablePerformanceTracking) {
    uint64_t currentTime = utils::TimeUtils::getCurrentNanos();
    if (currentTime - m_lastPerformanceReport >
        (m_mlConfig.performanceReportIntervalMs * 1000000ULL)) {
      const_cast<MLEnhancedMarketMaker*>(this)->generatePerformanceReport();
    }
  }

  return oss.str();
}

bool MLEnhancedMarketMaker::updateConfig(const StrategyConfig& config) {
  // Update base configuration
  return BasicMarketMaker::updateConfig(config);
}

bool MLEnhancedMarketMaker::updateMLConfig(const MLConfig& mlConfig) {
  m_mlConfig = mlConfig;

  // Recreate ML optimizer if configuration changed significantly
  if (m_mlConfig.enableMLSpreadOptimization && !m_mlOptimizer) {
    m_mlOptimizer =
        std::make_unique<ml::MLSpreadOptimizer>(m_mlConfig.optimizerConfig);
    if (isRunning()) {
      return m_mlOptimizer->initialize();
    }
  }

  return true;
}

ml::ModelMetrics MLEnhancedMarketMaker::getMLMetrics() const {
  if (m_mlOptimizer) {
    return m_mlOptimizer->getMetrics();
  }
  return ml::ModelMetrics{};
}

std::vector<std::pair<std::string, double>>
MLEnhancedMarketMaker::getFeatureImportance() const {
  if (m_mlOptimizer) {
    return m_mlOptimizer->getFeatureImportance();
  }
  return {};
}

void MLEnhancedMarketMaker::forceMLRetraining() {
  if (m_mlOptimizer) {
    m_mlOptimizer->forceRetrain();
  }
}

bool MLEnhancedMarketMaker::saveMLModel(const std::string& filename) const {
  if (m_mlOptimizer) {
    return m_mlOptimizer->saveModel(filename);
  }
  return false;
}

bool MLEnhancedMarketMaker::loadMLModel(const std::string& filename) {
  if (m_mlOptimizer) {
    return m_mlOptimizer->loadModel(filename);
  }
  return false;
}

bool MLEnhancedMarketMaker::isMLModelReady() const {
  if (m_mlOptimizer) {
    return m_mlOptimizer->isModelReady();
  }
  return false;
}

double MLEnhancedMarketMaker::calculateTargetSpread() const {
  // Calculate base heuristic spread
  double heuristicSpread = BasicMarketMaker::calculateTargetSpread();

  // If ML is not enabled, return flow-enhanced spread or heuristic
  if (!m_mlConfig.enableMLSpreadOptimization || !m_mlOptimizer) {
    return m_mlConfig.enableFlowAnalysis ? calculateFlowEnhancedSpread()
                                         : heuristicSpread;
  }

  // Extract market features (enhanced with flow analysis if available)
  auto features = m_mlConfig.enableFlowAnalysis ? extractFlowEnhancedFeatures()
                                                : extractMarketFeatures();

  // Get ML prediction
  auto prediction = m_mlOptimizer->predictOptimalSpread(features, m_config);

  // Store prediction for outcome tracking
  {
    std::lock_guard<std::mutex> lock(m_predictionMutex);
    m_currentPrediction.prediction = prediction;
    m_currentPrediction.isValid = true;
    m_currentPrediction.timestamp = utils::TimeUtils::getCurrentNanos();
    m_currentPrediction.baseFallbackSpread = heuristicSpread;
  }

  // Decide whether to use ML prediction or fallback
  if (shouldUseMLPrediction(prediction)) {
    double mlSpread =
        validateAndAdjustMLSpread(prediction.optimalSpread, heuristicSpread);

    // Apply flow-based adjustment if enabled
    if (m_mlConfig.enableFlowAnalysis && m_flowAnalyzer) {
      auto flowMetrics = m_flowAnalyzer->getCurrentMetrics();
      double flowAdjustment =
          m_flowAnalyzer->calculateFlowBasedSpreadAdjustment(mlSpread,
                                                             flowMetrics);
      double weight = m_mlConfig.flowSpreadAdjustmentWeight;
      mlSpread =
          mlSpread * (1.0 - weight) + (mlSpread * flowAdjustment) * weight;
    }

    // Apply impact-based adjustment if enabled
    if (m_mlConfig.enableImpactPrediction && m_impactPredictor) {
      double targetOrderSize = m_config.orderQuantity;
      auto impactPrediction =
          m_impactPredictor->predictImpact(OrderSide::BUY, targetOrderSize);
      if (impactPrediction.confidence > 0.3) {
        double impactAdjustment =
            1.0 + (impactPrediction.predictedRelativeImpact * 2.0);
        double weight = m_mlConfig.impactSpreadAdjustmentWeight;
        mlSpread =
            mlSpread * (1.0 - weight) + (mlSpread * impactAdjustment) * weight;
      }
    }

    // Apply regime-based adjustment if enabled
    if (m_mlConfig.enableRegimeDetection && m_regimeDetector) {
      double regimeSpread = calculateRegimeAwareSpread();
      if (regimeSpread > 0.0) {
        double weight = m_mlConfig.regimeSpreadAdjustmentWeight;
        mlSpread = mlSpread * (1.0 - weight) + regimeSpread * weight;
      }
    }

    // Apply RL parameter adaptation if enabled
    if (m_mlConfig.enableRLParameterAdaptation && m_rlAdapter) {
      applyRLParameterAdaptation();
    }

    return mlSpread;
  } else {
    return m_mlConfig.enableFlowAnalysis ? calculateFlowEnhancedSpread()
                                         : heuristicSpread;
  }
}

ml::MarketFeatures MLEnhancedMarketMaker::extractMarketFeatures() const {
  if (!m_orderBook) {
    return ml::MarketFeatures{};
  }

  ml::MarketFeatures features;
  features.timestamp = utils::TimeUtils::getCurrentNanos();

  // Basic price features
  features.midPrice = m_orderBook->getMidPrice();
  features.bidAskSpread =
      m_orderBook->getBestAskPrice() - m_orderBook->getBestBidPrice();

  // Order book features
  features.orderBookImbalance = m_orderBook->calculateOrderBookImbalance(5);
  features.bidBookDepth =
      1000.0; // Simplified - could implement proper depth calculation
  features.askBookDepth = 1200.0; // Simplified
  features.totalBookDepth = features.bidBookDepth + features.askBookDepth;

  // Position features
  features.currentPosition = getPosition();
  if (m_config.maxPosition > 0) {
    features.positionRatio = features.currentPosition / m_config.maxPosition;
  }
  features.inventoryRisk = std::abs(features.positionRatio);

  // Market data from last snapshot
  {
    std::lock_guard<std::mutex> lock(m_snapshotMutex);
    if (m_lastSnapshot.timestamp > 0) {
      double timeDiff =
          (features.timestamp - m_lastSnapshot.timestamp) / 1e9; // seconds
      if (timeDiff > 0 && timeDiff < 60) { // Within last minute
        features.priceMovement = (features.midPrice - m_lastSnapshot.midPrice) /
                                 m_lastSnapshot.midPrice;
        features.priceVelocity = features.priceMovement / timeDiff;
      }
    }
  }

  // Time-based features
  auto now = std::chrono::system_clock::now();
  auto timeT = std::chrono::system_clock::to_time_t(now);
  auto tm = *std::localtime(&timeT);

  features.timeOfDay = (tm.tm_hour * 60 + tm.tm_min) / (24.0 * 60.0);
  features.dayOfWeek = tm.tm_wday / 7.0;
  features.isMarketOpen = true; // Simplified

  // Simplified weighted mid price calculation
  features.weightedMidPrice =
      features.midPrice; // Could be enhanced with proper depth analysis

  return features;
}

double MLEnhancedMarketMaker::calculateHeuristicSpread() const {
  return BasicMarketMaker::calculateTargetSpread();
}

bool MLEnhancedMarketMaker::shouldUseMLPrediction(
    const ml::SpreadPrediction& prediction) const {
  // Check if prediction is valid
  if (!prediction.isValid()) {
    return false;
  }

  // Check confidence threshold
  if (prediction.confidence < m_mlConfig.mlConfidenceThreshold) {
    return false;
  }

  // Check minimum confidence for execution
  if (prediction.confidence < m_mlConfig.minConfidenceForExecution) {
    return false;
  }

  // Check if ML model is ready
  if (!isMLModelReady()) {
    return false;
  }

  return true;
}

void MLEnhancedMarketMaker::updateMLWithOutcome(double actualSpread, double pnL,
                                                double fillRate) {
  if (!m_mlOptimizer) {
    return;
  }

  // Extract features for the outcome
  auto features = extractMarketFeatures();

  // Update ML model with the outcome
  m_mlOptimizer->updateWithOutcome(features, actualSpread, pnL, fillRate,
                                   utils::TimeUtils::getCurrentNanos());

  // Track performance
  if (m_mlConfig.enablePerformanceTracking) {
    MLPerformanceTracker::PredictionOutcome outcome;
    {
      std::lock_guard<std::mutex> lock(m_predictionMutex);
      outcome.prediction = m_currentPrediction.prediction;
      outcome.wasMLUsed = m_currentPrediction.isValid &&
                          shouldUseMLPrediction(m_currentPrediction.prediction);
    }
    outcome.actualSpread = actualSpread;
    outcome.realizedPnL = pnL;
    outcome.fillRate = fillRate;
    outcome.timestamp = utils::TimeUtils::getCurrentNanos();

    m_performanceTracker.addOutcome(outcome);
  }
}

void MLEnhancedMarketMaker::generatePerformanceReport() {
  std::lock_guard<std::mutex> lock(m_reportMutex);

  // Update timestamp
  m_lastPerformanceReport = utils::TimeUtils::getCurrentNanos();

  // Trigger metrics update
  m_performanceTracker.updateMetrics();

  // Log performance report (in production, would use proper logging)
  // For now, just update internal metrics
}

void MLEnhancedMarketMaker::collectMarketData() {
  if (!m_orderBook) {
    return;
  }

  std::lock_guard<std::mutex> lock(m_snapshotMutex);

  m_lastSnapshot.midPrice = m_orderBook->getMidPrice();
  m_lastSnapshot.bidPrice = m_orderBook->getBestBidPrice();
  m_lastSnapshot.askPrice = m_orderBook->getBestAskPrice();
  m_lastSnapshot.bidVolume = 1000.0; // Simplified
  m_lastSnapshot.askVolume = 1200.0; // Simplified
  m_lastSnapshot.tradeVolume = 0.0; // Would be updated from trade notifications
  m_lastSnapshot.timestamp = utils::TimeUtils::getCurrentNanos();
}

double
MLEnhancedMarketMaker::validateAndAdjustMLSpread(double mlSpread,
                                                 double baseSpread) const {
  // Check if ML spread is within safe bounds
  if (!isMLSpreadSafe(mlSpread, baseSpread)) {
    return baseSpread; // Fall back to heuristic
  }

  // Ensure spread is within configured deviation limits
  double maxSpread = baseSpread * m_mlConfig.maxSpreadDeviationRatio;
  double minSpread = baseSpread / m_mlConfig.maxSpreadDeviationRatio;

  return std::max(minSpread, std::min(maxSpread, mlSpread));
}

bool MLEnhancedMarketMaker::isMLSpreadSafe(double mlSpread,
                                           double baseSpread) const {
  // Check for reasonable values
  if (mlSpread <= 0 || !std::isfinite(mlSpread)) {
    return false;
  }

  // Check if deviation is too extreme
  double ratio = mlSpread / baseSpread;
  if (ratio > m_mlConfig.maxSpreadDeviationRatio ||
      ratio < (1.0 / m_mlConfig.maxSpreadDeviationRatio)) {
    return false;
  }

  return true;
}

// MLPerformanceTracker implementation
void MLEnhancedMarketMaker::MLPerformanceTracker::addOutcome(
    const PredictionOutcome& outcome) {
  std::lock_guard<std::mutex> lock(outcomesMutex);

  outcomes.push_back(outcome);

  // Keep only recent outcomes (last 1000)
  while (outcomes.size() > 1000) {
    outcomes.pop_front();
  }
}

void MLEnhancedMarketMaker::MLPerformanceTracker::updateMetrics() {
  std::lock_guard<std::mutex> lock(outcomesMutex);

  totalMLPnL = 0.0;
  totalHeuristicPnL = 0.0;
  mlPredictionCount = 0;
  heuristicPredictionCount = 0;
  correctMLPredictions = 0;
  correctHeuristicPredictions = 0;

  for (const auto& outcome : outcomes) {
    if (outcome.wasMLUsed) {
      totalMLPnL += outcome.realizedPnL;
      mlPredictionCount++;

      // Check if prediction was "correct" (within 10% of optimal)
      if (std::abs(outcome.prediction.optimalSpread - outcome.actualSpread) /
              outcome.actualSpread <
          0.1) {
        correctMLPredictions++;
      }
    } else {
      totalHeuristicPnL += outcome.realizedPnL;
      heuristicPredictionCount++;
      // Simplified correctness check for heuristics
      correctHeuristicPredictions++;
    }
  }
}

std::string MLEnhancedMarketMaker::MLPerformanceTracker::getReport() const {
  std::ostringstream oss;

  oss << "\n=== ML vs Heuristic Performance ===" << std::endl;
  oss << "  ML Predictions: " << mlPredictionCount << std::endl;
  oss << "  Heuristic Predictions: " << heuristicPredictionCount << std::endl;

  if (mlPredictionCount > 0) {
    oss << "  ML Avg P&L per Prediction: $" << std::fixed
        << std::setprecision(4) << (totalMLPnL / mlPredictionCount)
        << std::endl;
    oss << "  ML Accuracy: " << std::fixed << std::setprecision(2)
        << (static_cast<double>(correctMLPredictions) / mlPredictionCount * 100)
        << "%" << std::endl;
  }

  if (heuristicPredictionCount > 0) {
    oss << "  Heuristic Avg P&L per Prediction: $" << std::fixed
        << std::setprecision(4)
        << (totalHeuristicPnL / heuristicPredictionCount) << std::endl;
  }

  if (mlPredictionCount > 0 && heuristicPredictionCount > 0) {
    double mlAvgPnL = totalMLPnL / mlPredictionCount;
    double heuristicAvgPnL = totalHeuristicPnL / heuristicPredictionCount;
    double improvement =
        ((mlAvgPnL - heuristicAvgPnL) / std::abs(heuristicAvgPnL)) * 100;

    oss << "  ML Improvement: " << std::fixed << std::setprecision(2)
        << improvement << "%" << std::endl;
  }

  return oss.str();
}

// Flow analysis methods implementation
ml::MarketFeatures MLEnhancedMarketMaker::extractFlowEnhancedFeatures() const {
  // Start with base market features
  auto features = extractMarketFeatures();

  // Enhance with flow analysis data if available
  if (m_flowAnalyzer && m_flowAnalyzer->isRunning()) {
    auto flowMetrics = m_flowAnalyzer->getCurrentMetrics();
    auto imbalance = m_flowAnalyzer->analyzeImbalance();
    (void)imbalance; // Avoid unused variable warning

    // Add flow-specific features (I can expand the MarketFeatures struct later)
    // For now, I'll influence existing features with flow data

    // Adjust volatility based on flow velocity
    double flowVelocityFactor = 1.0 + (std::abs(flowMetrics.bidFlowVelocity) +
                                       std::abs(flowMetrics.askFlowVelocity)) /
                                          1000.0;
    features.priceVolatility *= flowVelocityFactor;

    // Adjust order book imbalance with flow imbalance
    features.orderBookImbalance =
        (features.orderBookImbalance + flowMetrics.liquidityImbalance) / 2.0;

    // Adjust volume profile with flow rates
    double totalFlowRate =
        flowMetrics.bidVolumeRate + flowMetrics.askVolumeRate;
    if (totalFlowRate > 0) {
      features.volumeProfile =
          (features.volumeProfile + totalFlowRate / 1000.0) / 2.0;
    }

    // Influence trade intensity with order rates
    double totalOrderRate = flowMetrics.bidOrderRate + flowMetrics.askOrderRate;
    features.tradeIntensity =
        std::max(features.tradeIntensity, totalOrderRate / 100.0);
  }

  return features;
}

double MLEnhancedMarketMaker::calculateFlowEnhancedSpread() const {
  double baseSpread = BasicMarketMaker::calculateTargetSpread();

  if (!m_flowAnalyzer || !m_flowAnalyzer->isRunning()) {
    return baseSpread;
  }

  // Get current flow metrics
  auto flowMetrics = m_flowAnalyzer->getCurrentMetrics();

  // Calculate flow-based spread adjustment
  double flowAdjustment = m_flowAnalyzer->calculateFlowBasedSpreadAdjustment(
      baseSpread, flowMetrics);

  // Apply weighted adjustment
  double weight = m_mlConfig.flowSpreadAdjustmentWeight;
  return baseSpread * (1.0 - weight) + (baseSpread * flowAdjustment) * weight;
}

void MLEnhancedMarketMaker::recordFlowEvent(
    const pinnacle::analytics::OrderFlowEvent& event) {
  if (m_flowAnalyzer && m_flowAnalyzer->isRunning()) {
    m_flowAnalyzer->recordEvent(event);
  }
}

pinnacle::analytics::FlowMetrics MLEnhancedMarketMaker::getFlowMetrics() const {
  if (m_flowAnalyzer) {
    return m_flowAnalyzer->getCurrentMetrics();
  }
  return pinnacle::analytics::FlowMetrics{};
}

pinnacle::analytics::OrderBookAnalyzer::ImbalanceAnalysis
MLEnhancedMarketMaker::getImbalanceAnalysis(size_t depth) const {
  if (m_flowAnalyzer) {
    return m_flowAnalyzer->analyzeImbalance(depth);
  }
  return pinnacle::analytics::OrderBookAnalyzer::ImbalanceAnalysis{};
}

pinnacle::analytics::LiquidityPrediction
MLEnhancedMarketMaker::getLiquidityPrediction(uint64_t horizonMs) const {
  if (m_flowAnalyzer) {
    return m_flowAnalyzer->predictLiquidity(horizonMs);
  }
  return pinnacle::analytics::LiquidityPrediction{};
}

std::string MLEnhancedMarketMaker::getFlowStatistics() const {
  if (m_flowAnalyzer) {
    return m_flowAnalyzer->getFlowStatistics();
  }
  return "Flow analysis not available";
}

bool MLEnhancedMarketMaker::isFlowAnalysisEnabled() const {
  return m_mlConfig.enableFlowAnalysis && m_flowAnalyzer &&
         m_flowAnalyzer->isRunning();
}

pinnacle::analytics::ImpactPrediction
MLEnhancedMarketMaker::predictOrderImpact(OrderSide side, double orderSize,
                                          double urgency) const {
  if (!m_impactPredictor || !m_impactPredictor->isRunning()) {
    return pinnacle::analytics::ImpactPrediction{};
  }
  return m_impactPredictor->predictImpact(side, orderSize, urgency);
}

pinnacle::analytics::OrderSizingRecommendation
MLEnhancedMarketMaker::getOptimalOrderSizing(OrderSide side,
                                             double totalQuantity,
                                             double maxImpact,
                                             uint64_t timeHorizon) const {
  if (!m_impactPredictor || !m_impactPredictor->isRunning()) {
    return pinnacle::analytics::OrderSizingRecommendation{};
  }
  return m_impactPredictor->getOptimalSizing(side, totalQuantity, maxImpact,
                                             timeHorizon);
}

double
MLEnhancedMarketMaker::calculateExecutionCost(OrderSide side, double quantity,
                                              double currentMidPrice) const {
  if (!m_impactPredictor || !m_impactPredictor->isRunning()) {
    return currentMidPrice; // Return mid price if no impact prediction
                            // available
  }
  return m_impactPredictor->calculateExecutionCost(side, quantity,
                                                   currentMidPrice);
}

std::string MLEnhancedMarketMaker::getImpactStatistics() const {
  if (!m_impactPredictor || !m_impactPredictor->isRunning()) {
    return "Impact Prediction: Disabled";
  }
  return m_impactPredictor->getImpactStatistics();
}

bool MLEnhancedMarketMaker::isImpactPredictionEnabled() const {
  return m_mlConfig.enableImpactPrediction && m_impactPredictor &&
         m_impactPredictor->isRunning();
}

double MLEnhancedMarketMaker::calculateImpactAwareSpread() const {
  double baseSpread = calculateHeuristicSpread();

  if (!m_impactPredictor || !m_impactPredictor->isRunning()) {
    return baseSpread;
  }

  // Predict impact for typical order size
  double targetOrderSize = m_config.orderQuantity;
  auto buyImpact =
      m_impactPredictor->predictImpact(OrderSide::BUY, targetOrderSize);
  auto sellImpact =
      m_impactPredictor->predictImpact(OrderSide::SELL, targetOrderSize);

  // Use the higher impact prediction to adjust spread
  double maxRelativeImpact = std::max(buyImpact.predictedRelativeImpact,
                                      sellImpact.predictedRelativeImpact);

  if (maxRelativeImpact > 0 && buyImpact.confidence > 0.3 &&
      sellImpact.confidence > 0.3) {
    // Increase spread to account for expected impact
    double impactAdjustment = 1.0 + (maxRelativeImpact * 2.0);
    return baseSpread * impactAdjustment;
  }

  return baseSpread;
}

void MLEnhancedMarketMaker::recordImpactEvent(
    const pinnacle::analytics::MarketImpactEvent& event) {
  if (m_impactPredictor && m_impactPredictor->isRunning()) {
    m_impactPredictor->recordImpactEvent(event);
  }
}

// RL Parameter Adaptation Methods

std::unordered_map<rl::ParameterType, double>
MLEnhancedMarketMaker::getRLParameters() const {
  if (!m_rlAdapter) {
    return {};
  }

  std::unordered_map<rl::ParameterType, double> parameters;
  parameters[rl::ParameterType::BASE_SPREAD_BPS] =
      m_rlAdapter->getParameterValue(rl::ParameterType::BASE_SPREAD_BPS);
  parameters[rl::ParameterType::ORDER_QUANTITY] =
      m_rlAdapter->getParameterValue(rl::ParameterType::ORDER_QUANTITY);
  parameters[rl::ParameterType::MAX_POSITION] =
      m_rlAdapter->getParameterValue(rl::ParameterType::MAX_POSITION);
  parameters[rl::ParameterType::INVENTORY_SKEW_FACTOR] =
      m_rlAdapter->getParameterValue(rl::ParameterType::INVENTORY_SKEW_FACTOR);
  parameters[rl::ParameterType::ML_CONFIDENCE_THRESHOLD] =
      m_rlAdapter->getParameterValue(
          rl::ParameterType::ML_CONFIDENCE_THRESHOLD);
  parameters[rl::ParameterType::FLOW_ADJUSTMENT_WEIGHT] =
      m_rlAdapter->getParameterValue(rl::ParameterType::FLOW_ADJUSTMENT_WEIGHT);
  parameters[rl::ParameterType::IMPACT_ADJUSTMENT_WEIGHT] =
      m_rlAdapter->getParameterValue(
          rl::ParameterType::IMPACT_ADJUSTMENT_WEIGHT);

  return parameters;
}

void MLEnhancedMarketMaker::setRLParameter(rl::ParameterType parameter,
                                           double value) {
  if (m_rlAdapter) {
    m_rlAdapter->setParameterValue(parameter, value);
  }
}

void MLEnhancedMarketMaker::enableRLParameterAdaptation(
    rl::ParameterType parameter, bool enable) {
  if (m_rlAdapter) {
    m_rlAdapter->enableParameterAdaptation(parameter, enable);
  }
}

std::string MLEnhancedMarketMaker::getRLStatistics() const {
  if (!m_rlAdapter) {
    return "RL Parameter Adaptation: Disabled";
  }

  std::ostringstream oss;
  oss << m_rlAdapter->getPerformanceStatistics() << "\n";
  oss << m_rlAdapter->getQLearningStatistics() << "\n";
  oss << m_rlAdapter->getBanditStatistics();

  return oss.str();
}

rl::MarketState MLEnhancedMarketMaker::getCurrentMarketState() const {
  return extractRLMarketState();
}

void MLEnhancedMarketMaker::forceRLEpisodeEnd() {
  if (m_rlAdapter) {
    m_rlAdapter->forceEpisodeEnd();
  }
}

bool MLEnhancedMarketMaker::saveRLModel(const std::string& filename) const {
  if (!m_rlAdapter) {
    return false;
  }
  return m_rlAdapter->saveModel(filename);
}

bool MLEnhancedMarketMaker::loadRLModel(const std::string& filename) {
  if (!m_rlAdapter) {
    return false;
  }
  return m_rlAdapter->loadModel(filename);
}

bool MLEnhancedMarketMaker::isRLAdaptationEnabled() const {
  return m_mlConfig.enableRLParameterAdaptation && m_rlAdapter &&
         m_rlAdapter->isRunning();
}

void MLEnhancedMarketMaker::updateRLMarketState() {
  if (!m_rlAdapter || !m_rlAdapter->isRunning()) {
    return;
  }

  rl::MarketState marketState = extractRLMarketState();
  m_rlAdapter->updateMarketState(marketState);
}

void MLEnhancedMarketMaker::recordRLPerformance() {
  if (!m_rlAdapter || !m_rlAdapter->isRunning()) {
    return;
  }

  uint64_t timestamp = utils::TimeUtils::getCurrentNanos();
  double currentPnL = getPnL();
  double fillRate = 0.8; // Simplified - would calculate actual fill rate
  double riskMetric =
      std::abs(getPosition()) / m_config.maxPosition; // Position-based risk

  m_rlAdapter->recordPerformance(currentPnL, fillRate, riskMetric, timestamp);
}

void MLEnhancedMarketMaker::applyRLParameterAdaptation() const {
  if (!m_rlAdapter || !m_rlAdapter->isRunning()) {
    return;
  }

  // Create a mutable copy of the config for RL adaptation
  strategy::StrategyConfig adaptedConfig = m_config;
  m_rlAdapter->adaptParameters(adaptedConfig);

  // Apply adapted parameters to internal config
  // Note: In a production system, I may want to validate these changes
  const_cast<strategy::StrategyConfig&>(m_config).baseSpreadBps =
      adaptedConfig.baseSpreadBps;
  const_cast<strategy::StrategyConfig&>(m_config).orderQuantity =
      adaptedConfig.orderQuantity;
  const_cast<strategy::StrategyConfig&>(m_config).maxPosition =
      adaptedConfig.maxPosition;
  const_cast<strategy::StrategyConfig&>(m_config).inventorySkewFactor =
      adaptedConfig.inventorySkewFactor;
}

rl::MarketState MLEnhancedMarketMaker::extractRLMarketState() const {
  rl::MarketState state;

  // Get order book data
  if (m_orderBook) {
    double bidPrice = m_orderBook->getBestBidPrice();
    double askPrice = m_orderBook->getBestAskPrice();
    state.spread = askPrice - bidPrice;

    // Get order book depth
    auto bidLevels = m_orderBook->getBidLevels(5);
    auto askLevels = m_orderBook->getAskLevels(5);

    double bidDepth = 0.0;
    double askDepth = 0.0;
    for (const auto& level : bidLevels) {
      bidDepth += level.totalQuantity;
    }
    for (const auto& level : askLevels) {
      askDepth += level.totalQuantity;
    }

    if (bidDepth + askDepth > 0) {
      state.imbalance = (bidDepth - askDepth) / (bidDepth + askDepth);
    }
    state.liquidity = bidDepth + askDepth; // Total liquidity
  }

  // Get flow analysis data
  if (m_flowAnalyzer && m_flowAnalyzer->isRunning()) {
    auto flowMetrics = m_flowAnalyzer->getCurrentMetrics();
    state.volume = flowMetrics.bidVolumeRate + flowMetrics.askVolumeRate;
    // Use existing liquidity from order book, but could enhance with flow
    // metrics
    state.volatility =
        0.01; // Would need historical price data for proper calculation
  } else {
    // Calculate basic volatility from price snapshots
    std::lock_guard<std::mutex> lock(m_snapshotMutex);
    if (m_lastSnapshot.timestamp > 0) {
      double midPrice =
          (m_orderBook->getBestBidPrice() + m_orderBook->getBestAskPrice()) /
          2.0;
      state.volatility = std::abs(midPrice - m_lastSnapshot.midPrice) /
                         m_lastSnapshot.midPrice;
    }
  }

  // Time features
  auto now = utils::TimeUtils::getCurrentNanos();
  auto timeOfDay = (now % (24ULL * 3600ULL * 1000000000ULL)) /
                   (24.0 * 3600.0 * 1000000000.0);
  state.timeOfDay = timeOfDay;

  // Day of week (simplified - would need proper date calculation)
  auto dayOfWeek = ((now / (24ULL * 3600ULL * 1000000000ULL)) % 7) / 7.0;
  state.dayOfWeek = dayOfWeek;

  // Position and P&L
  state.currentPosition =
      getPosition() / m_config.maxPosition; // Normalized position
  state.unrealizedPnL = getPnL();

  // Market momentum (simplified)
  std::lock_guard<std::mutex> lock(m_snapshotMutex);
  if (m_lastSnapshot.timestamp > 0) {
    double midPrice =
        (m_orderBook->getBestBidPrice() + m_orderBook->getBestAskPrice()) / 2.0;
    state.momentum =
        (midPrice - m_lastSnapshot.midPrice) / m_lastSnapshot.midPrice;
  }

  return state;
}

// ============================================================================
// Market Regime Detection Implementation
// ============================================================================

void MLEnhancedMarketMaker::updateRegimeDetector(const OrderBook& orderBook) {
  if (!m_regimeDetector)
    return;

  // Create market data point for regime analysis
  pinnacle::analytics::MarketDataPoint dataPoint;
  dataPoint.price =
      (orderBook.getBestBidPrice() + orderBook.getBestAskPrice()) / 2.0;
  dataPoint.volume = static_cast<double>(
      orderBook.getOrderCount()); // Use order count as volume proxy
  dataPoint.bid = orderBook.getBestBidPrice();
  dataPoint.ask = orderBook.getBestAskPrice();
  dataPoint.spread = dataPoint.ask - dataPoint.bid;
  dataPoint.timestamp = utils::TimeUtils::getCurrentNanos();

  // Update regime detector
  m_regimeDetector->updateMarketData(dataPoint);
}

pinnacle::analytics::MarketRegime
MLEnhancedMarketMaker::getCurrentRegime() const {
  if (!m_regimeDetector)
    return pinnacle::analytics::MarketRegime::UNKNOWN;
  return m_regimeDetector->getCurrentRegime();
}

pinnacle::analytics::RegimeMetrics
MLEnhancedMarketMaker::getCurrentRegimeMetrics() const {
  if (!m_regimeDetector)
    return pinnacle::analytics::RegimeMetrics{};
  return m_regimeDetector->getCurrentMetrics();
}

double MLEnhancedMarketMaker::getRegimeConfidence() const {
  if (!m_regimeDetector)
    return 0.0;
  return m_regimeDetector->getRegimeConfidence();
}

std::vector<pinnacle::analytics::RegimeTransition>
MLEnhancedMarketMaker::getRecentRegimeTransitions(size_t count) const {
  if (!m_regimeDetector)
    return {};
  return m_regimeDetector->getRecentTransitions(count);
}

std::string MLEnhancedMarketMaker::getRegimeStatistics() const {
  if (!m_regimeDetector)
    return "Regime detection not enabled.";
  return m_regimeDetector->getRegimeStatistics();
}

bool MLEnhancedMarketMaker::updateRegimeConfig(
    const pinnacle::analytics::RegimeConfiguration& config) {
  if (!m_regimeDetector)
    return false;
  m_regimeDetector->updateConfiguration(config);
  return true;
}

bool MLEnhancedMarketMaker::isRegimeDetectionEnabled() const {
  return m_regimeDetector != nullptr && m_mlConfig.enableRegimeDetection;
}

bool MLEnhancedMarketMaker::saveRegimeModel(const std::string& filename) const {
  if (!m_regimeDetector)
    return false;
  return m_regimeDetector->saveModel(filename);
}

bool MLEnhancedMarketMaker::loadRegimeModel(const std::string& filename) {
  if (!m_regimeDetector)
    return false;
  return m_regimeDetector->loadModel(filename);
}

double MLEnhancedMarketMaker::calculateRegimeAwareSpread() const {
  if (!m_regimeDetector)
    return 0.0;

  auto currentRegime = m_regimeDetector->getCurrentRegime();
  auto regimeMetrics = m_regimeDetector->getCurrentMetrics();
  double regimeConfidence = m_regimeDetector->getRegimeConfidence();

  // Base adjustment factor
  double regimeAdjustment = 1.0;

  // Adjust spread based on regime type
  switch (currentRegime) {
  case pinnacle::analytics::MarketRegime::HIGH_VOLATILITY:
    // Widen spreads during high volatility
    regimeAdjustment = 1.0 + (regimeMetrics.volatility * 2.0);
    break;

  case pinnacle::analytics::MarketRegime::LOW_VOLATILITY:
    // Narrow spreads during low volatility
    regimeAdjustment = 1.0 - (0.3 * regimeConfidence);
    break;

  case pinnacle::analytics::MarketRegime::TRENDING_UP:
  case pinnacle::analytics::MarketRegime::TRENDING_DOWN:
    // Adjust for trend strength
    regimeAdjustment = 1.0 + (std::abs(regimeMetrics.trendStrength) * 0.5);
    break;

  case pinnacle::analytics::MarketRegime::MEAN_REVERTING:
    // Tighter spreads for mean reverting markets
    regimeAdjustment = 1.0 - (regimeMetrics.meanReversion * 0.4);
    break;

  case pinnacle::analytics::MarketRegime::CRISIS:
    // Significantly widen spreads during crisis
    regimeAdjustment = 1.0 + (regimeMetrics.stress * 3.0);
    break;

  case pinnacle::analytics::MarketRegime::CONSOLIDATION:
    // Moderate adjustment for consolidation
    regimeAdjustment = 1.0 + (0.1 * regimeConfidence);
    break;

  case pinnacle::analytics::MarketRegime::UNKNOWN:
  default:
    regimeAdjustment = 1.0;
    break;
  }

  // Weight the adjustment by confidence
  regimeAdjustment = 1.0 + ((regimeAdjustment - 1.0) * regimeConfidence);

  // Clamp to reasonable bounds
  regimeAdjustment = std::max(0.5, std::min(3.0, regimeAdjustment));

  // Calculate base spread
  double baseSpread = m_config.baseSpreadBps * 0.0001;
  if (m_orderBook) {
    double midPrice =
        (m_orderBook->getBestBidPrice() + m_orderBook->getBestAskPrice()) / 2.0;
    baseSpread *= midPrice;
  }

  return baseSpread * regimeAdjustment;
}

} // namespace strategy
} // namespace pinnacle
