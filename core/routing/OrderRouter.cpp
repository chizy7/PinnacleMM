#include "OrderRouter.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>

namespace pinnacle {
namespace core {
namespace routing {

// =============================================================================
// BestPriceStrategy Implementation
// =============================================================================

std::vector<ExecutionRequest>
BestPriceStrategy::planExecution(const ExecutionRequest &originalRequest,
                                 const std::vector<MarketData> &marketData) {

  if (marketData.empty()) {
    return {}; // No venues available
  }

  // Find venue with best price
  const MarketData *bestVenue = nullptr;
  double bestPrice =
      (originalRequest.order.getSide() == pinnacle::OrderSide::BUY)
          ? std::numeric_limits<double>::max()
          : 0.0;

  for (const auto &venue : marketData) {
    double price = (originalRequest.order.getSide() == pinnacle::OrderSide::BUY)
                       ? venue.askPrice
                       : venue.bidPrice;
    double totalCost = price + venue.fees;

    bool isBetter =
        (originalRequest.order.getSide() == pinnacle::OrderSide::BUY)
            ? (totalCost < bestPrice)
            : (totalCost > bestPrice);

    if (isBetter && venue.askPrice > 0 && venue.bidPrice > 0) {
      bestPrice = totalCost;
      bestVenue = &venue;
    }
  }

  if (!bestVenue) {
    return {}; // No suitable venue found
  }

  // Create single execution request for best venue
  ExecutionRequest request;
  request.requestId = originalRequest.requestId;
  request.order = Order(
      originalRequest.order.getOrderId(), originalRequest.order.getSymbol(),
      originalRequest.order.getSide(), originalRequest.order.getType(),
      originalRequest.order.getPrice(), originalRequest.order.getQuantity(),
      originalRequest.order.getTimestamp());
  request.targetVenue = bestVenue->venue;
  request.maxExecutionTime = originalRequest.maxExecutionTime;
  request.maxSlippage = originalRequest.maxSlippage;
  request.allowPartialFills = originalRequest.allowPartialFills;
  request.routingStrategy = originalRequest.routingStrategy;

  std::vector<ExecutionRequest> result;
  result.push_back(std::move(request));
  return result;
}

// =============================================================================
// TWAPStrategy Implementation
// =============================================================================

std::vector<ExecutionRequest>
TWAPStrategy::planExecution(const ExecutionRequest &originalRequest,
                            const std::vector<MarketData> &marketData) {

  std::vector<ExecutionRequest> requests;

  if (marketData.empty()) {
    return requests;
  }

  // Find best venue for TWAP execution
  const MarketData *bestVenue = nullptr;
  double bestScore = -1.0;

  for (const auto &venue : marketData) {
    // Score based on liquidity and impact
    double liquidityScore = venue.bidSize + venue.askSize;
    double impactScore = 1.0 / (1.0 + venue.impactCost);
    double feeScore = 1.0 / (1.0 + venue.fees);
    double totalScore = liquidityScore * impactScore * feeScore;

    if (totalScore > bestScore && venue.askPrice > 0 && venue.bidPrice > 0) {
      bestScore = totalScore;
      bestVenue = &venue;
    }
  }

  if (!bestVenue) {
    return requests;
  }

  // Split order into time slices
  double totalQuantity = originalRequest.order.getQuantity();
  double sliceQuantity = totalQuantity / m_numSlices;

  // Split order into time slices

  for (int i = 0; i < m_numSlices; ++i) {
    ExecutionRequest sliceRequest;
    sliceRequest.requestId =
        originalRequest.requestId + "_SLICE_" + std::to_string(i);
    sliceRequest.targetVenue = bestVenue->venue;
    sliceRequest.maxExecutionTime = originalRequest.maxExecutionTime;
    sliceRequest.maxSlippage = originalRequest.maxSlippage;
    sliceRequest.allowPartialFills = originalRequest.allowPartialFills;
    sliceRequest.routingStrategy = originalRequest.routingStrategy;

    // Adjust quantity for last slice to handle rounding
    if (i == m_numSlices - 1) {
      sliceQuantity = totalQuantity - (sliceQuantity * (m_numSlices - 1));
    }

    // Create new order with slice quantity
    sliceRequest.order = pinnacle::Order(
        sliceRequest.requestId, originalRequest.order.getSymbol(),
        originalRequest.order.getSide(), originalRequest.order.getType(),
        originalRequest.order.getPrice(), sliceQuantity,
        utils::TimeUtils::getCurrentNanos());

    // Time-based execution scheduling handled by router
    sliceRequest.maxExecutionTime = originalRequest.maxExecutionTime;

    requests.emplace_back(std::move(sliceRequest));
  }

  return requests;
}

// =============================================================================
// VWAPStrategy Implementation
// =============================================================================

std::vector<ExecutionRequest>
VWAPStrategy::planExecution(const ExecutionRequest &originalRequest,
                            const std::vector<MarketData> &marketData) {

  std::vector<ExecutionRequest> requests;

  if (marketData.empty()) {
    return requests;
  }

  // Calculate total market volume
  double totalMarketVolume = 0.0;
  for (const auto &venue : marketData) {
    totalMarketVolume += venue.recentVolume;
  }

  if (totalMarketVolume <= 0) {
    // Fallback to equal distribution
    return TWAPStrategy(10, std::chrono::seconds(60))
        .planExecution(originalRequest, marketData);
  }

  double totalQuantity = originalRequest.order.getQuantity();
  double maxParticipation = totalMarketVolume * m_participationRate;

  // If our order is too large, split across time and venues
  if (totalQuantity > maxParticipation) {
    int numTimeSlices = std::ceil(totalQuantity / maxParticipation);
    double sliceQuantity = totalQuantity / numTimeSlices;

    for (int i = 0; i < numTimeSlices; ++i) {
      // For each time slice, distribute across venues by volume
      double remainingSliceQty =
          (i == numTimeSlices - 1)
              ? totalQuantity - (sliceQuantity * (numTimeSlices - 1))
              : sliceQuantity;

      for (const auto &venue : marketData) {
        if (venue.recentVolume <= 0)
          continue;

        double venueWeight = venue.recentVolume / totalMarketVolume;
        double venueQuantity = remainingSliceQty * venueWeight;

        if (venueQuantity < 1.0)
          continue; // Skip very small orders

        ExecutionRequest venueRequest;
        venueRequest.requestId = originalRequest.requestId + "_VWAP_" +
                                 std::to_string(i) + "_" + venue.venue;
        venueRequest.targetVenue = venue.venue;
        venueRequest.maxExecutionTime = originalRequest.maxExecutionTime;
        venueRequest.maxSlippage = originalRequest.maxSlippage;
        venueRequest.allowPartialFills = originalRequest.allowPartialFills;
        venueRequest.routingStrategy = originalRequest.routingStrategy;

        venueRequest.order = pinnacle::Order(
            venueRequest.requestId, originalRequest.order.getSymbol(),
            originalRequest.order.getSide(), originalRequest.order.getType(),
            originalRequest.order.getPrice(), venueQuantity,
            utils::TimeUtils::getCurrentNanos());
        requests.emplace_back(std::move(venueRequest));
      }
    }
  } else {
    // Single slice, distribute by volume across venues
    for (const auto &venue : marketData) {
      if (venue.recentVolume <= 0)
        continue;

      double venueWeight = venue.recentVolume / totalMarketVolume;
      double venueQuantity = totalQuantity * venueWeight;

      if (venueQuantity < 1.0)
        continue;

      ExecutionRequest venueRequest;
      venueRequest.requestId =
          originalRequest.requestId + "_VWAP_" + venue.venue;
      venueRequest.targetVenue = venue.venue;
      venueRequest.maxExecutionTime = originalRequest.maxExecutionTime;
      venueRequest.maxSlippage = originalRequest.maxSlippage;
      venueRequest.allowPartialFills = originalRequest.allowPartialFills;
      venueRequest.routingStrategy = originalRequest.routingStrategy;

      venueRequest.order = pinnacle::Order(
          venueRequest.requestId, originalRequest.order.getSymbol(),
          originalRequest.order.getSide(), originalRequest.order.getType(),
          originalRequest.order.getPrice(), venueQuantity,
          utils::TimeUtils::getCurrentNanos());
      requests.emplace_back(std::move(venueRequest));
    }
  }

  return requests;
}

// =============================================================================
// MarketImpactStrategy Implementation
// =============================================================================

std::vector<ExecutionRequest>
MarketImpactStrategy::planExecution(const ExecutionRequest &originalRequest,
                                    const std::vector<MarketData> &marketData) {

  std::vector<ExecutionRequest> requests;

  // Sort venues by market impact (ascending)
  std::vector<MarketData> sortedVenues = marketData;
  std::sort(sortedVenues.begin(), sortedVenues.end(),
            [](const MarketData &a, const MarketData &b) {
              return a.impactCost < b.impactCost;
            });

  double remainingQuantity = originalRequest.order.getQuantity();

  for (const auto &venue : sortedVenues) {
    if (remainingQuantity <= 0)
      break;
    if (venue.impactCost > m_maxImpactThreshold)
      break;

    // Calculate max quantity for this venue without exceeding impact threshold
    double availableSize =
        (originalRequest.order.getSide() == pinnacle::OrderSide::BUY)
            ? venue.askSize
            : venue.bidSize;

    // Use conservative fraction of available liquidity
    double maxVenueQty = std::min(availableSize * 0.3, remainingQuantity);

    if (maxVenueQty < 1.0)
      continue;

    ExecutionRequest venueRequest;
    venueRequest.requestId =
        originalRequest.requestId + "_IMPACT_" + venue.venue;
    venueRequest.targetVenue = venue.venue;
    venueRequest.maxExecutionTime = originalRequest.maxExecutionTime;
    venueRequest.maxSlippage = originalRequest.maxSlippage;
    venueRequest.allowPartialFills = originalRequest.allowPartialFills;
    venueRequest.routingStrategy = originalRequest.routingStrategy;

    venueRequest.order = pinnacle::Order(
        venueRequest.requestId, originalRequest.order.getSymbol(),
        originalRequest.order.getSide(), originalRequest.order.getType(),
        originalRequest.order.getPrice(), maxVenueQty,
        utils::TimeUtils::getCurrentNanos());
    requests.emplace_back(std::move(venueRequest));

    remainingQuantity -= maxVenueQty;
  }

  return requests;
}

// =============================================================================
// OrderRouter Implementation
// =============================================================================

OrderRouter::OrderRouter() {
  initializeStrategies();

  // Initialize exchange factories
  m_webSocketFactory = std::make_shared<exchange::ExchangeConnectorFactory>();
  // Note: FixConnectorFactory initialization deferred due to singleton pattern
  // m_fixFactory = std::make_shared<exchange::fix::FixConnectorFactory>();
}

OrderRouter::~OrderRouter() { stop(); }

bool OrderRouter::initialize() {
  // Initialize exchange connections would happen here
  // For now, we'll assume they're initialized elsewhere
  return true;
}

bool OrderRouter::start() {
  if (m_isRunning.load()) {
    return false; // Already running
  }

  m_shouldStop.store(false);

  // Start worker threads
  m_routingThread = std::thread(&OrderRouter::routingThreadLoop, this);
  m_executionThread = std::thread(&OrderRouter::executionThreadLoop, this);
  m_monitoringThread = std::thread(&OrderRouter::monitoringThreadLoop, this);

  m_isRunning.store(true);

  std::cout << "OrderRouter started successfully" << std::endl;
  return true;
}

bool OrderRouter::stop() {
  if (!m_isRunning.load()) {
    return true; // Already stopped
  }

  m_shouldStop.store(true);

  // Join worker threads
  if (m_routingThread.joinable()) {
    m_routingThread.join();
  }
  if (m_executionThread.joinable()) {
    m_executionThread.join();
  }
  if (m_monitoringThread.joinable()) {
    m_monitoringThread.join();
  }

  m_isRunning.store(false);

  std::cout << "OrderRouter stopped successfully" << std::endl;
  return true;
}

std::string OrderRouter::submitOrder(const ExecutionRequest &request) {
  std::string requestId = generateRequestId();

  ExecutionRequest modifiedRequest;
  modifiedRequest.requestId = requestId;
  modifiedRequest.order =
      Order(request.order.getOrderId(), request.order.getSymbol(),
            request.order.getSide(), request.order.getType(),
            request.order.getPrice(), request.order.getQuantity(),
            request.order.getTimestamp());
  modifiedRequest.targetVenue = request.targetVenue;
  modifiedRequest.maxExecutionTime = request.maxExecutionTime;
  modifiedRequest.maxSlippage = request.maxSlippage;
  modifiedRequest.allowPartialFills = request.allowPartialFills;
  modifiedRequest.routingStrategy = request.routingStrategy;

  // Store in active executions
  {
    std::lock_guard<std::mutex> lock(m_executionsMutex);
    ActiveExecution execution;
    execution.originalRequest = std::move(modifiedRequest);
    execution.startTime = utils::TimeUtils::getCurrentNanos();
    m_activeExecutions.emplace(requestId, std::move(execution));
  }

  // Queue for routing
  if (!m_executionQueue.tryEnqueue(std::move(modifiedRequest))) {
    std::cerr << "Failed to queue execution request: " << requestId
              << std::endl;
    return "";
  }

  {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    m_stats.totalRequests++;
  }

  return requestId;
}

bool OrderRouter::cancelOrder(const std::string &requestId) {
  return m_cancelQueue.tryEnqueue(requestId);
}

void OrderRouter::setExecutionCallback(
    std::function<void(const ExecutionResult &)> callback) {
  std::lock_guard<std::mutex> lock(m_callbackMutex);
  m_executionCallback = callback;
}

bool OrderRouter::addVenue(const std::string &venueName,
                           const std::string &connectionType) {
  std::lock_guard<std::mutex> lock(m_venuesMutex);

  VenueConnection venue;
  venue.name = venueName;
  venue.type = connectionType;
  venue.isActive = true; // Would actually test connection

  m_venues[venueName] = venue;

  std::cout << "Added venue: " << venueName << " (type: " << connectionType
            << ")" << std::endl;
  return true;
}

void OrderRouter::updateMarketData(const std::string &venue,
                                   const MarketData &data) {
  std::lock_guard<std::mutex> lock(m_venuesMutex);

  auto it = m_venues.find(venue);
  if (it != m_venues.end()) {
    it->second.lastMarketData = data;
    it->second.lastMarketData.timestamp = utils::TimeUtils::getCurrentNanos();
  }
}

void OrderRouter::setRoutingStrategy(const std::string &strategyName) {
  if (m_strategies.find(strategyName) != m_strategies.end()) {
    m_currentStrategy = strategyName;
    std::cout << "Switched to routing strategy: " << strategyName << std::endl;
  } else {
    std::cerr << "Unknown routing strategy: " << strategyName << std::endl;
  }
}

std::string OrderRouter::getStatistics() const {
  std::lock_guard<std::mutex> lock(m_statsMutex);

  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2);
  oss << "OrderRouter Statistics:\n";
  oss << "  Total Requests: " << m_stats.totalRequests << "\n";
  oss << "  Completed: " << m_stats.completedExecutions << "\n";
  oss << "  Canceled: " << m_stats.canceledExecutions << "\n";
  oss << "  Failed: " << m_stats.failedExecutions << "\n";
  oss << "  Total Volume: " << m_stats.totalVolume << "\n";
  oss << "  Avg Execution Time: " << m_stats.avgExecutionTime << "ms\n";
  oss << "  Best Fill Rate: " << (m_stats.bestFillRate * 100) << "%\n";
  oss << "  Current Strategy: " << m_currentStrategy << "\n";
  oss << "  Active Venues: ";

  {
    std::lock_guard<std::mutex> venueLock(
        const_cast<std::mutex &>(m_venuesMutex));
    for (const auto &venue : m_venues) {
      if (venue.second.isActive) {
        oss << venue.first << " ";
      }
    }
  }

  return oss.str();
}

// =============================================================================
// Private Methods
// =============================================================================

void OrderRouter::routingThreadLoop() {
  while (!m_shouldStop.load()) {
    ExecutionRequest request;

    // Process execution requests
    while (m_executionQueue.tryDequeue(request)) {
      // Get market data for routing decision
      std::vector<MarketData> marketData =
          getAllMarketData(request.order.getSymbol());

      // Apply routing strategy
      auto strategy = m_strategies.find(m_currentStrategy);
      if (strategy == m_strategies.end()) {
        std::cerr << "Invalid routing strategy: " << m_currentStrategy
                  << std::endl;
        continue;
      }

      std::vector<ExecutionRequest> childRequests =
          strategy->second->planExecution(request, marketData);

      // Execute child requests
      for (auto &childRequest : childRequests) {
        executeOrder(childRequest);
      }

      // Store child requests for tracking
      {
        std::lock_guard<std::mutex> lock(m_executionsMutex);
        auto it = m_activeExecutions.find(request.requestId);
        if (it != m_activeExecutions.end()) {
          it->second.childRequests = std::move(childRequests);
        }
      }
    }

    // Process cancellation requests
    std::string cancelRequestId;
    while (m_cancelQueue.tryDequeue(cancelRequestId)) {
      std::lock_guard<std::mutex> lock(m_executionsMutex);
      auto it = m_activeExecutions.find(cancelRequestId);
      if (it != m_activeExecutions.end()) {
        // Mark as canceled - actual cancellation would happen in execution
        // thread
        it->second.isComplete.store(true);

        std::lock_guard<std::mutex> statsLock(m_statsMutex);
        m_stats.canceledExecutions++;
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

void OrderRouter::executionThreadLoop() {
  // This would handle actual order execution to exchanges
  // For now, simulate execution
  while (!m_shouldStop.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

void OrderRouter::monitoringThreadLoop() {
  while (!m_shouldStop.load()) {
    // Monitor execution timeouts and update statistics
    std::vector<std::string> completedExecutions;

    {
      std::lock_guard<std::mutex> lock(m_executionsMutex);
      auto now = utils::TimeUtils::getCurrentNanos();

      for (auto &pair : m_activeExecutions) {
        auto &execution = pair.second;

        // Check for timeouts
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::nanoseconds(now - execution.startTime));

        if (elapsed > execution.originalRequest.maxExecutionTime) {
          execution.isComplete.store(true);
          completedExecutions.push_back(pair.first);
        }
      }
    }

    // Process completed executions
    for (const auto &requestId : completedExecutions) {
      processCompletedExecution(requestId);
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
}

std::vector<MarketData>
OrderRouter::getAllMarketData(const std::string & /* symbol */) {
  std::lock_guard<std::mutex> lock(m_venuesMutex);

  std::vector<MarketData> marketData;
  auto now = utils::TimeUtils::getCurrentNanos();

  for (const auto &venue : m_venues) {
    if (!venue.second.isActive)
      continue;

    const auto &data = venue.second.lastMarketData;

    // Check if market data is recent (within last 5 seconds)
    if (now - data.timestamp < 5000000000ULL) { // 5 seconds in nanoseconds
      marketData.push_back(data);
    }
  }

  return marketData;
}

void OrderRouter::executeOrder(const ExecutionRequest &request) {
  // Simulate order execution - in reality this would connect to actual
  // exchanges
  ExecutionResult result;
  result.requestId = request.requestId;
  result.orderId = request.order.getOrderId();
  result.venue = request.targetVenue;
  result.status = pinnacle::OrderStatus::FILLED; // Simulate immediate fill
  result.filledQuantity = request.order.getQuantity();
  result.avgFillPrice = request.order.getPrice();
  result.totalFees = result.filledQuantity * 0.001; // 0.1% fee simulation
  result.executionTime = 1000;                      // 1ms simulation

  updateExecutionResult(result);
}

void OrderRouter::updateExecutionResult(const ExecutionResult &result) {
  {
    std::lock_guard<std::mutex> lock(m_executionsMutex);

    // Find parent execution by parsing request ID
    std::string parentId = result.requestId;
    size_t pos = parentId.find('_');
    if (pos != std::string::npos) {
      parentId = parentId.substr(0, pos);
    }

    auto it = m_activeExecutions.find(parentId);
    if (it != m_activeExecutions.end()) {
      it->second.results.push_back(result);

      // Check if all child requests are complete
      if (it->second.results.size() >= it->second.childRequests.size()) {
        it->second.isComplete.store(true);
      }
    }
  }

  // Call execution callback
  {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    if (m_executionCallback) {
      m_executionCallback(result);
    }
  }

  // Update statistics
  {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    m_stats.completedExecutions++;
    m_stats.totalVolume += result.filledQuantity;

    // Update average execution time
    double totalTime =
        m_stats.avgExecutionTime * (m_stats.completedExecutions - 1);
    m_stats.avgExecutionTime = (totalTime + result.executionTime / 1000.0) /
                               m_stats.completedExecutions;
  }
}

void OrderRouter::initializeStrategies() {
  m_strategies["BEST_PRICE"] = std::make_unique<BestPriceStrategy>();
  m_strategies["TWAP"] = std::make_unique<TWAPStrategy>();
  m_strategies["VWAP"] = std::make_unique<VWAPStrategy>();
  m_strategies["MARKET_IMPACT"] = std::make_unique<MarketImpactStrategy>();
}

void OrderRouter::processCompletedExecution(const std::string &requestId) {
  std::lock_guard<std::mutex> lock(m_executionsMutex);

  auto it = m_activeExecutions.find(requestId);
  if (it != m_activeExecutions.end()) {
    // Could aggregate results and send final callback here
    m_activeExecutions.erase(it);
  }
}

std::string OrderRouter::generateRequestId() {
  return "REQ_" + std::to_string(m_requestIdCounter.fetch_add(1)) + "_" +
         std::to_string(utils::TimeUtils::getCurrentNanos());
}

} // namespace routing
} // namespace core
} // namespace pinnacle