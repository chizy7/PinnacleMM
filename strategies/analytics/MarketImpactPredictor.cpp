#include "MarketImpactPredictor.h"
#include "../../core/utils/TimeUtils.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>

namespace pinnacle {
namespace analytics {

MarketImpactPredictor::MarketImpactPredictor(const std::string& symbol,
                                             size_t maxHistorySize,
                                             uint64_t modelUpdateInterval)
    : m_symbol(symbol), m_maxHistorySize(maxHistorySize),
      m_modelUpdateInterval(modelUpdateInterval) {

  // Initialize default model parameters
  m_currentModel.alpha = 0.1;
  m_currentModel.beta = 0.05;
  m_currentModel.gamma = 0.02;
  m_currentModel.lastUpdate = getCurrentTimestamp();
}

bool MarketImpactPredictor::initialize(
    std::shared_ptr<OrderBook> orderBook,
    std::shared_ptr<OrderBookAnalyzer> flowAnalyzer) {
  if (!orderBook) {
    return false;
  }

  m_orderBook = orderBook;
  m_flowAnalyzer = flowAnalyzer;

  // Register callback for order book updates to track price changes
  m_orderBook->registerUpdateCallback(
      [this](const OrderBook& book) { updatePriceHistory(); });

  return true;
}

bool MarketImpactPredictor::start() {
  if (m_isRunning.load()) {
    return false; // Already running
  }

  if (!m_orderBook) {
    return false; // Not initialized
  }

  m_isRunning.store(true);
  return true;
}

bool MarketImpactPredictor::stop() {
  if (!m_isRunning.load()) {
    return false; // Not running
  }

  m_isRunning.store(false);
  return true;
}

void MarketImpactPredictor::recordImpactEvent(const MarketImpactEvent& event) {
  if (!m_isRunning.load() || !isValidImpactEvent(event)) {
    return;
  }

  {
    std::lock_guard<std::mutex> lock(m_historyMutex);
    m_impactHistory.push_back(event);

    // Limit history size
    if (m_impactHistory.size() > m_maxHistorySize) {
      m_impactHistory.pop_front();
    }
  }

  // Update model if enough time has passed
  uint64_t currentTime = getCurrentTimestamp();
  if (currentTime - m_currentModel.lastUpdate > m_modelUpdateInterval) {
    updateImpactModel();
  }
}

ImpactPrediction MarketImpactPredictor::predictImpact(OrderSide side,
                                                      double orderSize,
                                                      double urgency) const {
  ImpactPrediction prediction;
  prediction.timestamp = getCurrentTimestamp();
  prediction.validUntil =
      prediction.timestamp + 5000000000; // Valid for 5 seconds

  if (!m_orderBook || orderSize <= 0) {
    return prediction;
  }

  // Get current market conditions
  double midPrice = m_orderBook->getMidPrice();
  double spread = m_orderBook->getSpread();

  if (midPrice <= 0) {
    return prediction;
  }

  // Calculate different impact components
  double linearImpact = calculateLinearImpact(side, orderSize);
  double sqrtImpact = calculateSqrtImpact(side, orderSize);
  double liquidityImpact = calculateLiquidityBasedImpact(side, orderSize);
  double temporaryImpact = calculateTemporaryImpact(side, orderSize);

  // Combine impact estimates based on market conditions
  {
    std::lock_guard<std::mutex> lock(m_modelMutex);

    // Base impact prediction using current model
    prediction.predictedImpact = m_currentModel.alpha * sqrtImpact +
                                 m_currentModel.beta * linearImpact +
                                 liquidityImpact;

    // Add temporary impact component
    prediction.predictedImpact += temporaryImpact * m_currentModel.gamma;

    // Apply market regime adjustments
    prediction.predictedImpact *= m_currentModel.volatilityFactor;
    prediction.predictedImpact *=
        (2.0 - m_currentModel.liquidityFactor); // Less liquid = more impact

    // Urgency adjustment (higher urgency = higher impact)
    prediction.predictedImpact *= (1.0 + urgency * 0.5);

    // Calculate relative impact
    prediction.predictedRelativeImpact = prediction.predictedImpact / midPrice;

    // Estimate confidence based on model quality and market conditions
    prediction.confidence = std::max(
        0.1, std::min(0.95, m_currentModel.rsquared * (1.0 - urgency * 0.3)));
  }

  // Calculate execution cost
  prediction.costOfExecution =
      calculateExecutionCost(side, orderSize, midPrice);

  // Estimate optimal order size (size that minimizes total cost)
  prediction.optimimalOrderSize =
      estimateOptimalSize(0.1, // 10 bps max impact
                          midPrice,
                          calculateAverageVolume(300000), // 5 minute average
                          m_currentModel);

  // Recommend execution time based on order size and urgency
  double sizeRatio = orderSize / std::max(1.0, prediction.optimimalOrderSize);
  prediction.executionTime =
      std::max(1000.0,            // Minimum 1 second
               std::min(300000.0, // Maximum 5 minutes
                        sizeRatio * 60000.0 *
                            (1.0 - urgency))); // Scale with size and urgency

  // Generate slicing recommendation
  if (orderSize > prediction.optimimalOrderSize * 1.5) {
    size_t numSlices = std::min(
        10, static_cast<int>(orderSize / prediction.optimimalOrderSize) + 1);
    prediction.sliceSizes.resize(numSlices, orderSize / numSlices);
  } else {
    prediction.sliceSizes = {orderSize};
  }

  prediction.urgencyFactor = urgency;

  return prediction;
}

OrderSizingRecommendation
MarketImpactPredictor::getOptimalSizing(OrderSide side, double totalQuantity,
                                        double maxImpact,
                                        uint64_t timeHorizon) const {
  OrderSizingRecommendation recommendation;
  recommendation.timestamp = getCurrentTimestamp();
  recommendation.targetQuantity = totalQuantity;

  if (!m_orderBook || totalQuantity <= 0) {
    return recommendation;
  }

  // Optimize order slicing
  recommendation.sliceSizes =
      optimizeOrderSlicing(side, totalQuantity, maxImpact, timeHorizon);

  // Calculate timing between slices
  if (recommendation.sliceSizes.size() > 1) {
    uint64_t intervalMs = timeHorizon / recommendation.sliceSizes.size();
    recommendation.sliceTiming.resize(recommendation.sliceSizes.size(),
                                      intervalMs);
  } else {
    recommendation.sliceTiming = {0}; // Immediate execution
  }

  // Calculate total expected impact and cost
  recommendation.totalExpectedImpact =
      calculateSlicingCost(recommendation.sliceSizes, side);
  recommendation.executionCost =
      recommendation.totalExpectedImpact * m_orderBook->getMidPrice();
  recommendation.timeToComplete =
      std::accumulate(recommendation.sliceTiming.begin(),
                      recommendation.sliceTiming.end(), 0ULL);

  // Calculate risk score based on market conditions and execution parameters
  double marketCondition = assessMarketCondition();
  double sizeRisk =
      totalQuantity / calculateAverageVolume(300000); // Risk relative to volume
  double timeRisk =
      static_cast<double>(timeHorizon) / 3600000.0; // Risk relative to 1 hour

  recommendation.riskScore = std::min(1.0, (1.0 - marketCondition) * 0.4 +
                                               sizeRisk * 0.4 + timeRisk * 0.2);

  // Recommend execution strategy
  if (recommendation.riskScore < 0.3) {
    recommendation.strategy = "PATIENT";
  } else if (recommendation.riskScore < 0.7) {
    recommendation.strategy = "BALANCED";
  } else {
    recommendation.strategy = "AGGRESSIVE";
  }

  return recommendation;
}

double
MarketImpactPredictor::calculateExecutionCost(OrderSide side, double quantity,
                                              double currentMidPrice) const {
  if (!m_orderBook || quantity <= 0 || currentMidPrice <= 0) {
    return 0.0;
  }

  // Calculate spread cost
  double spread = m_orderBook->getSpread();
  double spreadCost = spread * 0.5; // Half-spread cost

  // Calculate impact cost
  auto prediction = predictImpact(side, quantity, 0.5);
  double impactCost = prediction.predictedImpact;

  // Calculate opportunity cost based on timing
  double opportunityCost =
      0.0; // Could be enhanced with volatility-based estimates

  return spreadCost + impactCost + opportunityCost;
}

MarketImpactPredictor::ImpactAnalysis
MarketImpactPredictor::analyzeHistoricalImpact(uint64_t lookbackPeriod) const {
  ImpactAnalysis analysis;

  uint64_t currentTime = getCurrentTimestamp();
  uint64_t startTime = currentTime - lookbackPeriod;

  auto events = getEventsInPeriod(startTime, currentTime);

  if (events.empty()) {
    return analysis;
  }

  analysis.sampleCount = events.size();

  // Calculate basic statistics
  std::vector<double> impacts;
  std::vector<double> recoveryTimes;
  uint64_t temporaryCount = 0;

  for (const auto& event : events) {
    impacts.push_back(event.relativeImpact);
    if (event.timeToRecover > 0) {
      recoveryTimes.push_back(event.timeToRecover);
    }
    if (event.isTemporary) {
      temporaryCount++;
    }
  }

  // Sort for percentile calculations
  std::sort(impacts.begin(), impacts.end());

  // Calculate average and median
  analysis.averageImpact =
      std::accumulate(impacts.begin(), impacts.end(), 0.0) / impacts.size();
  analysis.medianImpact = impacts[impacts.size() / 2];

  // Calculate volatility (standard deviation)
  double variance = 0.0;
  for (double impact : impacts) {
    variance +=
        (impact - analysis.averageImpact) * (impact - analysis.averageImpact);
  }
  analysis.impactVolatility = std::sqrt(variance / impacts.size());

  // Calculate recovery time statistics
  if (!recoveryTimes.empty()) {
    analysis.averageRecoveryTime =
        std::accumulate(recoveryTimes.begin(), recoveryTimes.end(), 0.0) /
        recoveryTimes.size();
  }

  // Calculate temporary impact ratio
  analysis.temporaryImpactRatio =
      static_cast<double>(temporaryCount) / events.size();

  // Calculate percentiles
  analysis.impactPercentiles.resize(5);
  analysis.impactPercentiles[0] =
      impacts[static_cast<size_t>(impacts.size() * 0.05)]; // 5th
  analysis.impactPercentiles[1] =
      impacts[static_cast<size_t>(impacts.size() * 0.25)]; // 25th
  analysis.impactPercentiles[2] =
      impacts[static_cast<size_t>(impacts.size() * 0.50)]; // 50th (median)
  analysis.impactPercentiles[3] =
      impacts[static_cast<size_t>(impacts.size() * 0.75)]; // 75th
  analysis.impactPercentiles[4] =
      impacts[static_cast<size_t>(impacts.size() * 0.95)]; // 95th

  return analysis;
}

ImpactModel MarketImpactPredictor::getCurrentModel() const {
  std::lock_guard<std::mutex> lock(m_modelMutex);
  return m_currentModel;
}

bool MarketImpactPredictor::retrainModel() {
  if (!m_isRunning.load()) {
    return false;
  }

  updateImpactModel();
  return true;
}

std::string MarketImpactPredictor::getImpactStatistics() const {
  std::ostringstream oss;
  oss << "=== Market Impact Prediction Statistics ===\n";
  oss << "Symbol: " << m_symbol << "\n";
  oss << "Running: " << (m_isRunning.load() ? "Yes" : "No") << "\n\n";

  {
    std::lock_guard<std::mutex> lock(m_historyMutex);
    oss << "Impact History:\n";
    oss << "  Total Events: " << m_impactHistory.size() << "\n";
    oss << "  Max History Size: " << m_maxHistorySize << "\n";
  }

  {
    std::lock_guard<std::mutex> lock(m_modelMutex);
    oss << "\nCurrent Model:\n";
    oss << "  Alpha (sqrt): " << m_currentModel.alpha << "\n";
    oss << "  Beta (linear): " << m_currentModel.beta << "\n";
    oss << "  Gamma (temporary): " << m_currentModel.gamma << "\n";
    oss << "  R-squared: " << m_currentModel.rsquared << "\n";
    oss << "  Mean Absolute Error: " << m_currentModel.meanAbsoluteError
        << "\n";
    oss << "  Observations: " << m_currentModel.observationCount << "\n";
    oss << "  Volatility Factor: " << m_currentModel.volatilityFactor << "\n";
    oss << "  Liquidity Factor: " << m_currentModel.liquidityFactor << "\n";
    oss << "  Momentum Factor: " << m_currentModel.momentumFactor << "\n";
  }

  // Recent impact analysis
  auto recentAnalysis = analyzeHistoricalImpact(3600000); // Last hour
  oss << "\nRecent Impact Analysis (1 hour):\n";
  oss << "  Sample Count: " << recentAnalysis.sampleCount << "\n";
  oss << "  Average Impact: " << (recentAnalysis.averageImpact * 10000)
      << " bps\n";
  oss << "  Median Impact: " << (recentAnalysis.medianImpact * 10000)
      << " bps\n";
  oss << "  Impact Volatility: " << (recentAnalysis.impactVolatility * 10000)
      << " bps\n";
  oss << "  Average Recovery Time: " << recentAnalysis.averageRecoveryTime
      << " ms\n";
  oss << "  Temporary Impact Ratio: "
      << (recentAnalysis.temporaryImpactRatio * 100) << "%\n";

  if (!recentAnalysis.impactPercentiles.empty()) {
    oss << "  Impact Percentiles (bps): ";
    oss << "5%=" << (recentAnalysis.impactPercentiles[0] * 10000) << ", ";
    oss << "25%=" << (recentAnalysis.impactPercentiles[1] * 10000) << ", ";
    oss << "50%=" << (recentAnalysis.impactPercentiles[2] * 10000) << ", ";
    oss << "75%=" << (recentAnalysis.impactPercentiles[3] * 10000) << ", ";
    oss << "95%=" << (recentAnalysis.impactPercentiles[4] * 10000) << "\n";
  }

  // Market condition assessment
  oss << "\nMarket Conditions:\n";
  oss << "  Overall Condition Score: " << assessMarketCondition() << "\n";
  oss << "  Average Volume (5min): " << calculateAverageVolume(300000) << "\n";
  oss << "  Average Spread (5min): " << calculateAverageSpread(300000) << "\n";

  return oss.str();
}

void MarketImpactPredictor::reset() {
  {
    std::lock_guard<std::mutex> lock(m_historyMutex);
    m_impactHistory.clear();
  }

  {
    std::lock_guard<std::mutex> lock(m_modelMutex);
    m_currentModel = ImpactModel{};
    m_currentModel.lastUpdate = getCurrentTimestamp();
  }

  {
    std::lock_guard<std::mutex> lock(m_ordersMutex);
    m_activeOrders.clear();
  }

  {
    std::lock_guard<std::mutex> lock(m_priceMutex);
    m_priceHistory.clear();
  }
}

void MarketImpactPredictor::updateMarketRegime(double volatility,
                                               double liquidity,
                                               double momentum) {
  std::lock_guard<std::mutex> lock(m_modelMutex);

  m_currentModel.volatilityFactor = std::max(0.5, std::min(2.0, volatility));
  m_currentModel.liquidityFactor = std::max(0.5, std::min(2.0, liquidity));
  m_currentModel.momentumFactor = std::max(0.5, std::min(2.0, momentum));
}

// Private implementation methods

void MarketImpactPredictor::updateImpactModel() {
  std::lock_guard<std::mutex> lock(m_modelMutex);

  std::vector<MarketImpactEvent> recentEvents;
  {
    std::lock_guard<std::mutex> historyLock(m_historyMutex);

    // Use events from last 24 hours for model fitting
    uint64_t cutoffTime =
        getCurrentTimestamp() - 86400000000000ULL; // 24 hours in nanoseconds

    for (const auto& event : m_impactHistory) {
      if (event.timestamp >= cutoffTime) {
        recentEvents.push_back(event);
      }
    }
  }

  if (recentEvents.size() < 10) {
    return; // Need at least 10 observations
  }

  // Simple linear regression to fit alpha and beta parameters
  // Impact = alpha * sqrt(OrderSize/AvgVolume) + beta *
  // (OrderSize/LiquidityDepth)

  double avgVolume = calculateAverageVolume(3600000); // 1 hour average
  if (avgVolume <= 0) {
    return;
  }

  std::vector<double> x1, x2, y;
  for (const auto& event : recentEvents) {
    if (event.orderSize > 0 && event.volumeAtImpact > 0) {
      x1.push_back(std::sqrt(event.orderSize / avgVolume));
      x2.push_back(event.orderSize / std::max(1.0, event.volumeAtImpact));
      y.push_back(event.relativeImpact);
    }
  }

  if (x1.size() < 5) {
    return;
  }

  // Simplified multiple linear regression (could be enhanced with proper matrix
  // operations)
  double sumX1 = std::accumulate(x1.begin(), x1.end(), 0.0);
  double sumX2 = std::accumulate(x2.begin(), x2.end(), 0.0);
  double sumY = std::accumulate(y.begin(), y.end(), 0.0);
  double n = static_cast<double>(x1.size());

  // Simple estimation (could be improved with proper regression)
  double meanX1 = sumX1 / n;
  double meanX2 = sumX2 / n;
  double meanY = sumY / n;

  double num1 = 0, num2 = 0, den1 = 0, den2 = 0;
  for (size_t i = 0; i < x1.size(); ++i) {
    num1 += (x1[i] - meanX1) * (y[i] - meanY);
    den1 += (x1[i] - meanX1) * (x1[i] - meanX1);
    num2 += (x2[i] - meanX2) * (y[i] - meanY);
    den2 += (x2[i] - meanX2) * (x2[i] - meanX2);
  }

  if (den1 > 0) {
    m_currentModel.alpha = std::max(0.01, std::min(1.0, num1 / den1));
  }
  if (den2 > 0) {
    m_currentModel.beta = std::max(0.01, std::min(1.0, num2 / den2));
  }

  // Calculate model quality metrics
  double totalSumSquares = 0, residualSumSquares = 0;
  for (size_t i = 0; i < x1.size(); ++i) {
    double predicted =
        m_currentModel.alpha * x1[i] + m_currentModel.beta * x2[i];
    residualSumSquares += (y[i] - predicted) * (y[i] - predicted);
    totalSumSquares += (y[i] - meanY) * (y[i] - meanY);
  }

  if (totalSumSquares > 0) {
    m_currentModel.rsquared = 1.0 - (residualSumSquares / totalSumSquares);
    m_currentModel.meanSquaredError = residualSumSquares / n;
    m_currentModel.meanAbsoluteError =
        std::sqrt(m_currentModel.meanSquaredError);
  }

  m_currentModel.observationCount = recentEvents.size();
  m_currentModel.lastUpdate = getCurrentTimestamp();
}

double MarketImpactPredictor::calculateLinearImpact(OrderSide side,
                                                    double orderSize) const {
  if (!m_orderBook)
    return 0.0;

  double liquidityDepth = getCurrentLiquidityDepth(side);
  if (liquidityDepth <= 0)
    return 0.0;

  return orderSize / liquidityDepth *
         0.001; // 0.1% impact per unit of depth consumed
}

double MarketImpactPredictor::calculateSqrtImpact(OrderSide side,
                                                  double orderSize) const {
  double avgVolume = calculateAverageVolume(300000); // 5 minute average
  if (avgVolume <= 0)
    return 0.0;

  return std::sqrt(orderSize / avgVolume) * 0.0005; // 5 bps base impact
}

double MarketImpactPredictor::calculateTemporaryImpact(OrderSide side,
                                                       double orderSize) const {
  if (!m_orderBook)
    return 0.0;

  double spread = m_orderBook->getSpread();
  double midPrice = m_orderBook->getMidPrice();

  if (midPrice <= 0)
    return 0.0;

  // Temporary impact as fraction of spread, proportional to order size
  double avgVolume = calculateAverageVolume(60000); // 1 minute average
  if (avgVolume <= 0)
    return spread * 0.25; // Quarter spread if no volume data

  double sizeRatio = orderSize / avgVolume;
  return spread * 0.1 * std::min(1.0, sizeRatio); // Up to 10% of spread
}

double
MarketImpactPredictor::calculateLiquidityBasedImpact(OrderSide side,
                                                     double orderSize) const {
  if (!m_orderBook)
    return 0.0;

  // Enhanced impact calculation using flow analysis if available
  if (m_flowAnalyzer && m_flowAnalyzer->isRunning()) {
    auto flowMetrics = m_flowAnalyzer->getCurrentMetrics();
    auto liquidityPrediction =
        m_flowAnalyzer->predictLiquidity(100); // 100ms ahead

    // Adjust impact based on predicted liquidity conditions
    double liquidityAdjustment =
        1.0 + (1.0 - liquidityPrediction.liquidityScore);
    double imbalanceAdjustment =
        1.0 + std::abs(flowMetrics.liquidityImbalance) * 0.5;

    double baseImpact = calculateLinearImpact(side, orderSize);
    return baseImpact * liquidityAdjustment * imbalanceAdjustment;
  }

  return calculateLinearImpact(side, orderSize);
}

std::vector<double> MarketImpactPredictor::optimizeOrderSlicing(
    OrderSide side, double totalQuantity, double maxImpact,
    uint64_t timeHorizon) const {
  std::vector<double> slices;

  // Start with single order and check if impact is acceptable
  auto singleOrderPrediction = predictImpact(side, totalQuantity, 0.5);

  if (singleOrderPrediction.predictedRelativeImpact <= maxImpact) {
    return {totalQuantity}; // Single order is optimal
  }

  // Binary search for optimal slice size
  double minSliceSize = totalQuantity / 20.0; // Maximum 20 slices
  double maxSliceSize = totalQuantity;
  double optimalSliceSize = totalQuantity / 2.0;

  for (int iterations = 0; iterations < 10; ++iterations) {
    auto prediction = predictImpact(side, optimalSliceSize, 0.5);

    if (prediction.predictedRelativeImpact > maxImpact) {
      maxSliceSize = optimalSliceSize;
      optimalSliceSize = (minSliceSize + optimalSliceSize) / 2.0;
    } else {
      minSliceSize = optimalSliceSize;
      optimalSliceSize = (optimalSliceSize + maxSliceSize) / 2.0;
    }
  }

  // Create slices
  double remaining = totalQuantity;
  while (remaining >
         optimalSliceSize * 0.1) { // Stop when remainder is < 10% of slice size
    double sliceSize = std::min(optimalSliceSize, remaining);
    slices.push_back(sliceSize);
    remaining -= sliceSize;
  }

  // Add remainder to last slice if any
  if (remaining > 0 && !slices.empty()) {
    slices.back() += remaining;
  } else if (remaining > 0) {
    slices.push_back(remaining);
  }

  return slices;
}

double
MarketImpactPredictor::calculateSlicingCost(const std::vector<double>& slices,
                                            OrderSide side) const {
  double totalCost = 0.0;

  for (double slice : slices) {
    auto prediction = predictImpact(side, slice, 0.5);
    totalCost += prediction.predictedImpact;
  }

  return totalCost;
}

std::vector<MarketImpactEvent>
MarketImpactPredictor::getEventsInPeriod(uint64_t startTime,
                                         uint64_t endTime) const {
  std::vector<MarketImpactEvent> events;

  std::lock_guard<std::mutex> lock(m_historyMutex);

  for (const auto& event : m_impactHistory) {
    if (event.timestamp >= startTime && event.timestamp <= endTime) {
      events.push_back(event);
    }
  }

  return events;
}

double
MarketImpactPredictor::calculateAverageVolume(uint64_t lookbackPeriod) const {
  if (!m_orderBook)
    return 0.0;

  // Simplified calculation - could be enhanced with actual volume tracking
  auto bidLevels = m_orderBook->getBidLevels(10);
  auto askLevels = m_orderBook->getAskLevels(10);

  double totalVolume = 0.0;
  for (const auto& level : bidLevels) {
    totalVolume += level.totalQuantity;
  }
  for (const auto& level : askLevels) {
    totalVolume += level.totalQuantity;
  }

  return totalVolume; // This could be enhanced with historical volume tracking
}

double
MarketImpactPredictor::calculateAverageSpread(uint64_t lookbackPeriod) const {
  if (!m_orderBook)
    return 0.0;

  // For now, return current spread - could be enhanced with historical tracking
  return m_orderBook->getSpread();
}

double MarketImpactPredictor::getCurrentLiquidityDepth(OrderSide side) const {
  if (!m_orderBook)
    return 0.0;

  auto levels = (side == OrderSide::BUY) ? m_orderBook->getBidLevels(5)
                                         : m_orderBook->getAskLevels(5);

  double totalDepth = 0.0;
  for (const auto& level : levels) {
    totalDepth += level.totalQuantity;
  }

  return totalDepth;
}

uint64_t MarketImpactPredictor::getCurrentTimestamp() const {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::high_resolution_clock::now().time_since_epoch())
      .count();
}

void MarketImpactPredictor::updatePriceHistory() {
  if (!m_orderBook)
    return;

  PriceSnapshot snapshot;
  snapshot.timestamp = getCurrentTimestamp();
  snapshot.midPrice = m_orderBook->getMidPrice();
  snapshot.bidPrice = m_orderBook->getBestBidPrice();
  snapshot.askPrice = m_orderBook->getBestAskPrice();
  snapshot.spread = m_orderBook->getSpread();
  snapshot.totalVolume = calculateAverageVolume(0); // Current volume

  {
    std::lock_guard<std::mutex> lock(m_priceMutex);
    m_priceHistory.push_back(snapshot);

    // Keep only recent history (last 1 hour)
    uint64_t cutoffTime =
        snapshot.timestamp - 3600000000000ULL; // 1 hour in nanoseconds
    while (!m_priceHistory.empty() &&
           m_priceHistory.front().timestamp < cutoffTime) {
      m_priceHistory.pop_front();
    }
  }
}

void MarketImpactPredictor::cleanupOldData() {
  uint64_t cutoffTime = getCurrentTimestamp() - 86400000000000ULL; // 24 hours

  {
    std::lock_guard<std::mutex> lock(m_historyMutex);
    auto it = std::remove_if(m_impactHistory.begin(), m_impactHistory.end(),
                             [cutoffTime](const MarketImpactEvent& event) {
                               return event.timestamp < cutoffTime;
                             });
    m_impactHistory.erase(it, m_impactHistory.end());
  }
}

bool MarketImpactPredictor::isValidImpactEvent(
    const MarketImpactEvent& event) const {
  return event.orderSize > 0 && event.priceImpact >= 0 &&
         event.relativeImpact >= 0 &&
         event.relativeImpact < 0.1; // Reject unrealistic impacts > 10%
}

double MarketImpactPredictor::assessMarketCondition() const {
  // Composite score: 1.0 = excellent conditions, 0.0 = poor conditions
  double liquidityScore =
      std::min(1.0, calculateAverageVolume(300000) / 1000.0);
  double spreadScore =
      std::max(0.0, 1.0 - calculateAverageSpread(300000) / 10.0);

  if (m_flowAnalyzer && m_flowAnalyzer->isRunning()) {
    auto flowMetrics = m_flowAnalyzer->getCurrentMetrics();
    double flowScore = 1.0 - std::abs(flowMetrics.liquidityImbalance);
    return (liquidityScore + spreadScore + flowScore) / 3.0;
  }

  return (liquidityScore + spreadScore) / 2.0;
}

double MarketImpactPredictor::calculateVolatilityFactor() const {
  // Simplified volatility calculation from price history
  std::lock_guard<std::mutex> lock(m_priceMutex);

  if (m_priceHistory.size() < 10) {
    return 1.0; // Default factor
  }

  std::vector<double> returns;
  for (size_t i = 1; i < m_priceHistory.size(); ++i) {
    if (m_priceHistory[i - 1].midPrice > 0) {
      double ret =
          (m_priceHistory[i].midPrice - m_priceHistory[i - 1].midPrice) /
          m_priceHistory[i - 1].midPrice;
      returns.push_back(ret);
    }
  }

  if (returns.empty())
    return 1.0;

  double mean =
      std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
  double variance = 0.0;
  for (double ret : returns) {
    variance += (ret - mean) * (ret - mean);
  }

  double volatility = std::sqrt(variance / returns.size());
  return std::max(
      0.5,
      std::min(2.0, 1.0 + volatility * 100.0)); // Scale to reasonable range
}

double MarketImpactPredictor::calculateLiquidityFactor() const {
  double currentVolume = calculateAverageVolume(60000);      // 1 minute
  double historicalVolume = calculateAverageVolume(3600000); // 1 hour

  if (historicalVolume <= 0)
    return 1.0;

  return std::max(0.5, std::min(2.0, currentVolume / historicalVolume));
}

double MarketImpactPredictor::calculateMomentumFactor() const {
  std::lock_guard<std::mutex> lock(m_priceMutex);

  if (m_priceHistory.size() < 20) {
    return 1.0; // Default factor
  }

  // Calculate short vs long term momentum
  double shortTerm = m_priceHistory.back().midPrice /
                     m_priceHistory[m_priceHistory.size() - 10].midPrice;
  double longTerm = m_priceHistory.back().midPrice / m_priceHistory[0].midPrice;

  double momentum = shortTerm / longTerm;
  return std::max(0.5, std::min(2.0, momentum));
}

// Helper functions implementation

MarketImpactEvent createImpactEvent(const std::string& orderId, OrderSide side,
                                    double orderSize, double priceBefore,
                                    double priceAfter, uint64_t timestamp) {
  return MarketImpactEvent(timestamp, orderId, side, orderSize, priceBefore,
                           priceAfter);
}

double calculateImpactCostBps(double priceImpact, double midPrice) {
  if (midPrice <= 0)
    return 0.0;
  return (priceImpact / midPrice) * 10000.0; // Convert to basis points
}

double estimateOptimalSize(double maxImpactBps, double midPrice,
                           double avgVolume, const ImpactModel& model) {
  if (midPrice <= 0 || avgVolume <= 0)
    return 0.0;

  double maxImpact = maxImpactBps / 10000.0; // Convert from bps

  // Solve: maxImpact = alpha * sqrt(size/avgVolume) + beta *
  // size/liquidityDepth Simplified assumption: liquidityDepth = avgVolume
  // maxImpact = alpha * sqrt(size/avgVolume) + beta * size/avgVolume
  // This is a quadratic equation in sqrt(size/avgVolume)

  double x = maxImpact / (model.alpha + model.beta);
  return x * x * avgVolume; // size = x^2 * avgVolume
}

} // namespace analytics
} // namespace pinnacle
