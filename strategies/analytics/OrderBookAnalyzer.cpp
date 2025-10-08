#include "OrderBookAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>

namespace pinnacle {
namespace analytics {

OrderBookAnalyzer::OrderBookAnalyzer(const std::string& symbol,
                                     uint64_t windowSizeMs, size_t maxEvents)
    : m_symbol(symbol), m_windowSizeMs(windowSizeMs), m_maxEvents(maxEvents) {}

bool OrderBookAnalyzer::initialize(std::shared_ptr<OrderBook> orderBook) {
  if (!orderBook) {
    return false;
  }

  m_orderBook = orderBook;

  // Register callback for order book updates
  m_orderBook->registerUpdateCallback([this](const OrderBook& book) {
    // This will be called on every order book update
    // We'll track changes and generate flow events
    updateFlowVelocity();
  });

  return true;
}

bool OrderBookAnalyzer::start() {
  if (m_isRunning.load()) {
    return false; // Already running
  }

  if (!m_orderBook) {
    return false; // Not initialized
  }

  m_isRunning.store(true);
  return true;
}

bool OrderBookAnalyzer::stop() {
  if (!m_isRunning.load()) {
    return false; // Not running
  }

  m_isRunning.store(false);
  return true;
}

void OrderBookAnalyzer::recordEvent(const OrderFlowEvent& event) {
  if (!m_isRunning.load()) {
    return;
  }

  {
    std::lock_guard<std::mutex> lock(m_eventMutex);
    m_eventHistory.push_back(event);

    // Limit history size
    if (m_eventHistory.size() > m_maxEvents) {
      m_eventHistory.pop_front();
    }
  }

  // Update tracking structures
  updateOrderTracking(event);
  updatePriceLevelInfo(event.price, event.newTotalQuantity, event.side);

  // Cleanup old events periodically
  if (m_eventHistory.size() % 100 == 0) {
    cleanupOldEvents();
  }

  // Invalidate cached metrics
  {
    std::lock_guard<std::mutex> lock(m_metricsMutex);
    m_lastMetricsUpdate = 0;
  }
}

FlowMetrics OrderBookAnalyzer::getCurrentMetrics() const {
  std::lock_guard<std::mutex> lock(m_metricsMutex);

  uint64_t currentTime = getCurrentTimestamp();

  // Check if cached metrics are still valid (cache for 10ms)
  if (currentTime - m_lastMetricsUpdate < 10000000) { // 10ms in nanoseconds
    return m_cachedMetrics;
  }

  updateMetrics();
  m_lastMetricsUpdate = currentTime;

  return m_cachedMetrics;
}

void OrderBookAnalyzer::updateMetrics() const {
  uint64_t currentTime = getCurrentTimestamp();
  uint64_t windowStart =
      currentTime - (m_windowSizeMs * 1000000); // Convert to nanoseconds

  m_cachedMetrics = FlowMetrics{};
  m_cachedMetrics.timestamp = currentTime;
  m_cachedMetrics.windowStartTime = windowStart;
  m_cachedMetrics.windowEndTime = currentTime;

  // Calculate order and volume rates
  m_cachedMetrics.bidOrderRate = calculateOrderRate(
      OrderSide::BUY, OrderFlowEvent::Type::ADD_ORDER, m_windowSizeMs);
  m_cachedMetrics.askOrderRate = calculateOrderRate(
      OrderSide::SELL, OrderFlowEvent::Type::ADD_ORDER, m_windowSizeMs);
  m_cachedMetrics.bidCancelRate = calculateOrderRate(
      OrderSide::BUY, OrderFlowEvent::Type::CANCEL_ORDER, m_windowSizeMs);
  m_cachedMetrics.askCancelRate = calculateOrderRate(
      OrderSide::SELL, OrderFlowEvent::Type::CANCEL_ORDER, m_windowSizeMs);

  m_cachedMetrics.bidVolumeRate =
      calculateVolumeRate(OrderSide::BUY, m_windowSizeMs);
  m_cachedMetrics.askVolumeRate =
      calculateVolumeRate(OrderSide::SELL, m_windowSizeMs);

  // Calculate imbalances
  double totalVolume =
      m_cachedMetrics.bidVolumeRate + m_cachedMetrics.askVolumeRate;
  if (totalVolume > 0) {
    m_cachedMetrics.liquidityImbalance =
        (m_cachedMetrics.bidVolumeRate - m_cachedMetrics.askVolumeRate) /
        totalVolume;
  }

  double avgBidSize = calculateAverageOrderSize(OrderSide::BUY, m_windowSizeMs);
  double avgAskSize =
      calculateAverageOrderSize(OrderSide::SELL, m_windowSizeMs);
  double totalAvgSize = avgBidSize + avgAskSize;
  if (totalAvgSize > 0) {
    m_cachedMetrics.orderSizeImbalance =
        (avgBidSize - avgAskSize) / totalAvgSize;
  }

  // Calculate persistence metrics
  m_cachedMetrics.orderPersistence = calculateOrderPersistence();
  m_cachedMetrics.levelPersistence = calculateLevelPersistence();

  // Calculate flow velocity
  m_cachedMetrics.bidFlowVelocity = calculateFlowVelocity(OrderSide::BUY);
  m_cachedMetrics.askFlowVelocity = calculateFlowVelocity(OrderSide::SELL);

  // Calculate market impact indicators
  m_cachedMetrics.adverseSelectionRatio = calculateAdverseSelectionRatio();
  m_cachedMetrics.informationContent = calculateInformationContent();

  // Calculate aggressive order ratio (simplified)
  uint64_t totalOrders = 0;
  uint64_t aggressiveOrders = 0;

  {
    std::lock_guard<std::mutex> lock(m_eventMutex);
    for (const auto& event : m_eventHistory) {
      if (isEventInWindow(event, m_windowSizeMs) &&
          event.type == OrderFlowEvent::Type::ADD_ORDER) {
        totalOrders++;
        // Simplified: consider orders at best price as aggressive
        if (m_orderBook) {
          double bestPrice = (event.side == OrderSide::BUY)
                                 ? m_orderBook->getBestBidPrice()
                                 : m_orderBook->getBestAskPrice();
          if (std::abs(event.price - bestPrice) < 1e-8) {
            aggressiveOrders++;
          }
        }
      }
    }
  }

  if (totalOrders > 0) {
    m_cachedMetrics.aggressiveOrderRatio =
        static_cast<double>(aggressiveOrders) / totalOrders;
  }

  // Calculate large order ratio (orders > 95th percentile)
  std::vector<double> orderSizes;
  {
    std::lock_guard<std::mutex> lock(m_eventMutex);
    for (const auto& event : m_eventHistory) {
      if (isEventInWindow(event, m_windowSizeMs) &&
          event.type == OrderFlowEvent::Type::ADD_ORDER) {
        orderSizes.push_back(event.quantity);
      }
    }
  }

  if (orderSizes.size() > 10) {
    std::sort(orderSizes.begin(), orderSizes.end());
    double p95 = orderSizes[static_cast<size_t>(orderSizes.size() * 0.95)];
    size_t largeOrders =
        std::count_if(orderSizes.begin(), orderSizes.end(),
                      [p95](double size) { return size >= p95; });
    m_cachedMetrics.largeOrderRatio =
        static_cast<double>(largeOrders) / orderSizes.size();
  }
}

double OrderBookAnalyzer::calculateOrderRate(OrderSide side,
                                             OrderFlowEvent::Type eventType,
                                             uint64_t windowMs) const {
  uint64_t count = 0;

  std::lock_guard<std::mutex> lock(m_eventMutex);
  for (const auto& event : m_eventHistory) {
    if (isEventInWindow(event, windowMs) && event.side == side &&
        event.type == eventType) {
      count++;
    }
  }

  return static_cast<double>(count) * 1000.0 / windowMs; // Events per second
}

double OrderBookAnalyzer::calculateVolumeRate(OrderSide side,
                                              uint64_t windowMs) const {
  double totalVolume = 0.0;

  std::lock_guard<std::mutex> lock(m_eventMutex);
  for (const auto& event : m_eventHistory) {
    if (isEventInWindow(event, windowMs) && event.side == side &&
        event.type == OrderFlowEvent::Type::ADD_ORDER) {
      totalVolume += event.quantity;
    }
  }

  return totalVolume * 1000.0 / windowMs; // Volume per second
}

double OrderBookAnalyzer::calculateAverageOrderSize(OrderSide side,
                                                    uint64_t windowMs) const {
  double totalVolume = 0.0;
  uint64_t orderCount = 0;

  std::lock_guard<std::mutex> lock(m_eventMutex);
  for (const auto& event : m_eventHistory) {
    if (isEventInWindow(event, windowMs) && event.side == side &&
        event.type == OrderFlowEvent::Type::ADD_ORDER) {
      totalVolume += event.quantity;
      orderCount++;
    }
  }

  return orderCount > 0 ? totalVolume / orderCount : 0.0;
}

double OrderBookAnalyzer::calculateOrderPersistence() const {
  std::vector<uint64_t> persistenceTimes;

  std::lock_guard<std::mutex> lock(m_orderMutex);
  for (const auto& [orderId, info] : m_orderTracking) {
    if (info.removeTime > 0) { // Order was removed
      persistenceTimes.push_back(info.removeTime - info.addTime);
    }
  }

  if (persistenceTimes.empty()) {
    return 0.0;
  }

  uint64_t avgPersistence =
      std::accumulate(persistenceTimes.begin(), persistenceTimes.end(), 0ULL) /
      persistenceTimes.size();

  return static_cast<double>(avgPersistence) /
         1000000.0; // Convert to milliseconds
}

double OrderBookAnalyzer::calculateLevelPersistence() const {
  std::vector<uint64_t> levelTimes;

  {
    std::lock_guard<std::mutex> lock(m_levelsMutex);
    uint64_t currentTime = getCurrentTimestamp();

    for (const auto& [price, info] : m_bidLevels) {
      if (info.quantity > 0) {
        levelTimes.push_back(currentTime - info.firstSeenTime);
      }
    }

    for (const auto& [price, info] : m_askLevels) {
      if (info.quantity > 0) {
        levelTimes.push_back(currentTime - info.firstSeenTime);
      }
    }
  }

  if (levelTimes.empty()) {
    return 0.0;
  }

  uint64_t avgPersistence =
      std::accumulate(levelTimes.begin(), levelTimes.end(), 0ULL) /
      levelTimes.size();

  return static_cast<double>(avgPersistence) /
         1000000.0; // Convert to milliseconds
}

double OrderBookAnalyzer::calculateFlowVelocity(OrderSide side) const {
  std::lock_guard<std::mutex> lock(m_velocityMutex);

  if (m_velocityHistory.size() < 2) {
    return 0.0;
  }

  const auto& latest = m_velocityHistory.back();
  const auto& previous = m_velocityHistory[m_velocityHistory.size() - 2];

  double volumeChange = (side == OrderSide::BUY)
                            ? (latest.bidVolume - previous.bidVolume)
                            : (latest.askVolume - previous.askVolume);

  uint64_t timeDiff = latest.timestamp - previous.timestamp;

  if (timeDiff == 0) {
    return 0.0;
  }

  return volumeChange * 1000000000.0 / timeDiff; // Volume change per second
}

double OrderBookAnalyzer::calculateAdverseSelectionRatio() const {
  uint64_t totalFills = 0;
  uint64_t immediateFills = 0;

  std::lock_guard<std::mutex> lock(m_orderMutex);
  for (const auto& [orderId, info] : m_orderTracking) {
    if (info.wasFilled) {
      totalFills++;
      // Consider fills within 100ms as immediate
      if (info.removeTime > 0 && (info.removeTime - info.addTime) < 100000000) {
        immediateFills++;
      }
    }
  }

  return totalFills > 0 ? static_cast<double>(immediateFills) / totalFills
                        : 0.0;
}

double OrderBookAnalyzer::calculateInformationContent() const {
  // Simplified calculation of price impact per unit volume
  if (!m_orderBook) {
    return 0.0;
  }

  double totalVolumeImpact = 0.0;
  uint64_t impactCount = 0;

  std::lock_guard<std::mutex> lock(m_eventMutex);
  double lastMidPrice = 0.0;

  for (const auto& event : m_eventHistory) {
    if (isEventInWindow(event, m_windowSizeMs) &&
        event.type == OrderFlowEvent::Type::FILL_ORDER) {

      double currentMidPrice = m_orderBook->getMidPrice();
      if (lastMidPrice > 0) {
        double priceImpact = std::abs(currentMidPrice - lastMidPrice);
        if (event.quantity > 0) {
          totalVolumeImpact += priceImpact / event.quantity;
          impactCount++;
        }
      }
      lastMidPrice = currentMidPrice;
    }
  }

  return impactCount > 0 ? totalVolumeImpact / impactCount : 0.0;
}

void OrderBookAnalyzer::updatePriceLevelInfo(double price, double newQuantity,
                                             OrderSide side) {
  std::lock_guard<std::mutex> lock(m_levelsMutex);

  auto& levels = (side == OrderSide::BUY) ? m_bidLevels : m_askLevels;
  auto it = levels.find(price);

  uint64_t currentTime = getCurrentTimestamp();

  if (it == levels.end()) {
    // New price level
    if (newQuantity > 0) {
      levels[price] = {newQuantity, currentTime, currentTime, 1};
    }
  } else {
    // Existing price level
    if (newQuantity <= 0) {
      // Level removed
      levels.erase(it);
    } else {
      // Level updated
      it->second.quantity = newQuantity;
      it->second.lastUpdateTime = currentTime;
      it->second.orderCount++;
    }
  }
}

void OrderBookAnalyzer::updateOrderTracking(const OrderFlowEvent& event) {
  std::lock_guard<std::mutex> lock(m_orderMutex);

  switch (event.type) {
  case OrderFlowEvent::Type::ADD_ORDER:
    m_orderTracking[event.orderId] = {event.timestamp, 0, event.quantity,
                                      event.side, false};
    break;

  case OrderFlowEvent::Type::CANCEL_ORDER:
    if (auto it = m_orderTracking.find(event.orderId);
        it != m_orderTracking.end()) {
      it->second.removeTime = event.timestamp;
    }
    break;

  case OrderFlowEvent::Type::FILL_ORDER:
    if (auto it = m_orderTracking.find(event.orderId);
        it != m_orderTracking.end()) {
      it->second.removeTime = event.timestamp;
      it->second.wasFilled = true;
    }
    break;

  default:
    break;
  }
}

void OrderBookAnalyzer::updateFlowVelocity() {
  if (!m_orderBook) {
    return;
  }

  std::lock_guard<std::mutex> lock(m_velocityMutex);

  uint64_t currentTime = getCurrentTimestamp();

  // Get current bid/ask volumes (simplified)
  auto bidLevels = m_orderBook->getBidLevels(5);
  auto askLevels = m_orderBook->getAskLevels(5);

  double bidVolume = 0.0;
  double askVolume = 0.0;

  for (const auto& level : bidLevels) {
    bidVolume += level.totalQuantity;
  }

  for (const auto& level : askLevels) {
    askVolume += level.totalQuantity;
  }

  m_velocityHistory.push_back({bidVolume, askVolume, currentTime});

  // Keep only recent data (last 10 updates)
  if (m_velocityHistory.size() > 10) {
    m_velocityHistory.pop_front();
  }
}

LiquidityPrediction
OrderBookAnalyzer::predictLiquidity(uint64_t horizonMs) const {
  return predictLiquidityInternal(horizonMs);
}

LiquidityPrediction
OrderBookAnalyzer::predictLiquidityInternal(uint64_t horizonMs) const {
  LiquidityPrediction prediction{};
  prediction.timestamp = getCurrentTimestamp();
  prediction.predictionHorizon = horizonMs * 1000000; // Convert to nanoseconds

  FlowMetrics currentMetrics = getCurrentMetrics();

  // Simple linear prediction based on current flow rates
  double timeFactor =
      static_cast<double>(horizonMs) / 1000.0; // Convert to seconds

  prediction.predictedBidLiquidity = currentMetrics.bidVolumeRate * timeFactor;
  prediction.predictedAskLiquidity = currentMetrics.askVolumeRate * timeFactor;

  // Calculate liquidity score
  prediction.liquidityScore = calculateLiquidityScore(currentMetrics);

  // Calculate confidence based on flow consistency
  double flowVariance = std::abs(currentMetrics.bidFlowVelocity) +
                        std::abs(currentMetrics.askFlowVelocity);
  prediction.confidence =
      std::max(0.0, std::min(1.0, 1.0 - flowVariance / 1000.0));

  return prediction;
}

double
OrderBookAnalyzer::calculateLiquidityScore(const FlowMetrics& metrics) const {
  // Composite liquidity score based on multiple factors
  double volumeScore =
      std::min(1.0, (metrics.bidVolumeRate + metrics.askVolumeRate) / 1000.0);
  double balanceScore = 1.0 - std::abs(metrics.liquidityImbalance);
  double persistenceScore =
      std::min(1.0, metrics.orderPersistence / 1000.0); // Normalize to [0,1]
  double flowScore = 1.0 - std::min(1.0, (std::abs(metrics.bidFlowVelocity) +
                                          std::abs(metrics.askFlowVelocity)) /
                                             100.0);

  return (volumeScore + balanceScore + persistenceScore + flowScore) / 4.0;
}

double OrderBookAnalyzer::calculateFlowBasedSpreadAdjustment(
    double baseSpread, const FlowMetrics& currentMetrics) const {
  // Adjust spread based on flow conditions
  double adjustment = 1.0;

  // Increase spread during high volatility (high flow velocity)
  double velocityFactor = std::abs(currentMetrics.bidFlowVelocity) +
                          std::abs(currentMetrics.askFlowVelocity);
  adjustment += velocityFactor / 1000.0; // Scale factor

  // Increase spread during imbalanced flow
  double imbalanceFactor = std::abs(currentMetrics.liquidityImbalance);
  adjustment += imbalanceFactor * 0.5;

  // Increase spread during high adverse selection
  adjustment += currentMetrics.adverseSelectionRatio * 0.3;

  // Decrease spread during high liquidity
  double liquidityScore = calculateLiquidityScore(currentMetrics);
  adjustment *= (2.0 - liquidityScore); // Range [1.0, 2.0]

  // Clamp adjustment to reasonable bounds
  return std::max(0.5, std::min(3.0, adjustment));
}

OrderBookAnalyzer::ImbalanceAnalysis
OrderBookAnalyzer::analyzeImbalance(size_t depth) const {
  ImbalanceAnalysis analysis{};
  analysis.timestamp = getCurrentTimestamp();

  if (!m_orderBook) {
    return analysis;
  }

  auto bidLevels = m_orderBook->getBidLevels(depth);
  auto askLevels = m_orderBook->getAskLevels(depth);

  double bidVolume = 0.0;
  double askVolume = 0.0;
  size_t bidOrderCount = 0;
  size_t askOrderCount = 0;
  double weightedBidVolume = 0.0;
  double weightedAskVolume = 0.0;

  // Calculate volumes and weighted volumes
  for (size_t i = 0; i < bidLevels.size(); ++i) {
    bidVolume += bidLevels[i].totalQuantity;
    bidOrderCount += bidLevels[i].orders.size();
    double weight = 1.0 / (i + 1); // Weight decreases with distance from top
    weightedBidVolume += bidLevels[i].totalQuantity * weight;
  }

  for (size_t i = 0; i < askLevels.size(); ++i) {
    askVolume += askLevels[i].totalQuantity;
    askOrderCount += askLevels[i].orders.size();
    double weight = 1.0 / (i + 1);
    weightedAskVolume += askLevels[i].totalQuantity * weight;
  }

  // Calculate imbalances
  double totalVolume = bidVolume + askVolume;
  if (totalVolume > 0) {
    analysis.volumeImbalance = (bidVolume - askVolume) / totalVolume;
  }

  size_t totalOrders = bidOrderCount + askOrderCount;
  if (totalOrders > 0) {
    analysis.orderCountImbalance =
        (static_cast<double>(bidOrderCount) - askOrderCount) / totalOrders;
  }

  double totalWeightedVolume = weightedBidVolume + weightedAskVolume;
  if (totalWeightedVolume > 0) {
    analysis.weightedImbalance =
        (weightedBidVolume - weightedAskVolume) / totalWeightedVolume;
  }

  // Top level imbalance
  if (!bidLevels.empty() && !askLevels.empty()) {
    double topLevelVolume =
        bidLevels[0].totalQuantity + askLevels[0].totalQuantity;
    if (topLevelVolume > 0) {
      analysis.topLevelImbalance =
          (bidLevels[0].totalQuantity - askLevels[0].totalQuantity) /
          topLevelVolume;
    }
  }

  return analysis;
}

bool OrderBookAnalyzer::detectRegimeChange() const {
  // Simple regime change detection based on flow metrics
  FlowMetrics currentMetrics = getCurrentMetrics();

  // Detect significant changes in flow patterns
  static FlowMetrics lastMetrics{};
  static bool initialized = false;

  if (!initialized) {
    lastMetrics = currentMetrics;
    initialized = true;
    return false;
  }

  // Calculate changes in key metrics
  double orderRateChange =
      std::abs(currentMetrics.bidOrderRate - lastMetrics.bidOrderRate) +
      std::abs(currentMetrics.askOrderRate - lastMetrics.askOrderRate);

  double volumeRateChange =
      std::abs(currentMetrics.bidVolumeRate - lastMetrics.bidVolumeRate) +
      std::abs(currentMetrics.askVolumeRate - lastMetrics.askVolumeRate);

  double imbalanceChange = std::abs(currentMetrics.liquidityImbalance -
                                    lastMetrics.liquidityImbalance);

  // Thresholds for regime change detection
  const double ORDER_RATE_THRESHOLD = 10.0;   // orders per second
  const double VOLUME_RATE_THRESHOLD = 100.0; // volume per second
  const double IMBALANCE_THRESHOLD = 0.3;     // imbalance ratio

  bool regimeChange = (orderRateChange > ORDER_RATE_THRESHOLD) ||
                      (volumeRateChange > VOLUME_RATE_THRESHOLD) ||
                      (imbalanceChange > IMBALANCE_THRESHOLD);

  lastMetrics = currentMetrics;

  return regimeChange;
}

std::string OrderBookAnalyzer::getFlowStatistics() const {
  FlowMetrics metrics = getCurrentMetrics();
  ImbalanceAnalysis imbalance = analyzeImbalance();
  LiquidityPrediction prediction = predictLiquidity();

  std::ostringstream oss;
  oss << "=== Order Flow Analysis Statistics ===\n";
  oss << "Symbol: " << m_symbol << "\n";
  oss << "Window: " << m_windowSizeMs << "ms\n\n";

  oss << "Flow Rates:\n";
  oss << "  Bid Order Rate: " << metrics.bidOrderRate << " orders/sec\n";
  oss << "  Ask Order Rate: " << metrics.askOrderRate << " orders/sec\n";
  oss << "  Bid Cancel Rate: " << metrics.bidCancelRate << " cancels/sec\n";
  oss << "  Ask Cancel Rate: " << metrics.askCancelRate << " cancels/sec\n";
  oss << "  Bid Volume Rate: " << metrics.bidVolumeRate << " volume/sec\n";
  oss << "  Ask Volume Rate: " << metrics.askVolumeRate << " volume/sec\n\n";

  oss << "Imbalances:\n";
  oss << "  Liquidity Imbalance: " << metrics.liquidityImbalance << "\n";
  oss << "  Order Size Imbalance: " << metrics.orderSizeImbalance << "\n";
  oss << "  Volume Imbalance: " << imbalance.volumeImbalance << "\n";
  oss << "  Order Count Imbalance: " << imbalance.orderCountImbalance << "\n\n";

  oss << "Market Impact:\n";
  oss << "  Aggressive Order Ratio: " << metrics.aggressiveOrderRatio << "\n";
  oss << "  Large Order Ratio: " << metrics.largeOrderRatio << "\n";
  oss << "  Adverse Selection Ratio: " << metrics.adverseSelectionRatio << "\n";
  oss << "  Information Content: " << metrics.informationContent << "\n\n";

  oss << "Persistence:\n";
  oss << "  Order Persistence: " << metrics.orderPersistence << " ms\n";
  oss << "  Level Persistence: " << metrics.levelPersistence << " ms\n\n";

  oss << "Flow Velocity:\n";
  oss << "  Bid Flow Velocity: " << metrics.bidFlowVelocity << "\n";
  oss << "  Ask Flow Velocity: " << metrics.askFlowVelocity << "\n\n";

  oss << "Liquidity Prediction:\n";
  oss << "  Predicted Bid Liquidity: " << prediction.predictedBidLiquidity
      << "\n";
  oss << "  Predicted Ask Liquidity: " << prediction.predictedAskLiquidity
      << "\n";
  oss << "  Liquidity Score: " << prediction.liquidityScore << "\n";
  oss << "  Confidence: " << prediction.confidence << "\n\n";

  oss << "Regime Detection: "
      << (detectRegimeChange() ? "CHANGE DETECTED" : "STABLE") << "\n";

  return oss.str();
}

void OrderBookAnalyzer::cleanupOldEvents() {
  std::lock_guard<std::mutex> lock(m_eventMutex);

  uint64_t cutoffTime = getCurrentTimestamp() -
                        (m_windowSizeMs * 10 * 1000000); // Keep 10x window size

  auto it = std::remove_if(m_eventHistory.begin(), m_eventHistory.end(),
                           [cutoffTime](const OrderFlowEvent& event) {
                             return event.timestamp < cutoffTime;
                           });

  m_eventHistory.erase(it, m_eventHistory.end());
}

void OrderBookAnalyzer::reset() {
  {
    std::lock_guard<std::mutex> lock(m_eventMutex);
    m_eventHistory.clear();
  }

  {
    std::lock_guard<std::mutex> lock(m_levelsMutex);
    m_bidLevels.clear();
    m_askLevels.clear();
  }

  {
    std::lock_guard<std::mutex> lock(m_orderMutex);
    m_orderTracking.clear();
  }

  {
    std::lock_guard<std::mutex> lock(m_velocityMutex);
    m_velocityHistory.clear();
  }

  {
    std::lock_guard<std::mutex> lock(m_metricsMutex);
    m_cachedMetrics = FlowMetrics{};
    m_lastMetricsUpdate = 0;
  }
}

uint64_t OrderBookAnalyzer::getCurrentTimestamp() const {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::high_resolution_clock::now().time_since_epoch())
      .count();
}

bool OrderBookAnalyzer::isEventInWindow(const OrderFlowEvent& event,
                                        uint64_t windowMs) const {
  uint64_t currentTime = getCurrentTimestamp();
  uint64_t windowNs = windowMs * 1000000; // Convert to nanoseconds
  return (currentTime - event.timestamp) <= windowNs;
}

} // namespace analytics
} // namespace pinnacle
