#pragma once

#include "../../exchange/connector/ExchangeConnectorFactory.h"
#include "../../exchange/fix/FixConnectorFactory.h"
#include "../orderbook/Order.h"
#include "../utils/LockFreeQueue.h"
#include "../utils/TimeUtils.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace pinnacle {
namespace core {
namespace routing {

/**
 * @brief Market data snapshot for routing decisions
 */
struct MarketData {
  std::string venue;
  double bidPrice{0.0};
  double askPrice{0.0};
  double bidSize{0.0};
  double askSize{0.0};
  uint64_t timestamp{0};
  double averageDailyVolume{0.0};
  double recentVolume{0.0};
  double impactCost{0.0}; // Estimated market impact
  double fees{0.0};       // Trading fees for this venue
};

/**
 * @brief Order execution request
 */
struct ExecutionRequest {
  std::string requestId;
  pinnacle::Order order;
  std::string targetVenue;                    // Empty for auto-selection
  std::chrono::seconds maxExecutionTime{300}; // 5 minutes default
  double maxSlippage{0.001};                  // 0.1% default
  bool allowPartialFills{true};
  std::string routingStrategy{
      "BEST_PRICE"}; // BEST_PRICE, TWAP, VWAP, MARKET_IMPACT

  // Default constructor
  ExecutionRequest() = default;

  // Move constructor and assignment
  ExecutionRequest(ExecutionRequest&& other) noexcept = default;
  ExecutionRequest& operator=(ExecutionRequest&& other) noexcept = default;

  // Delete copy constructor and assignment to prevent accidental copies
  ExecutionRequest(const ExecutionRequest&) = delete;
  ExecutionRequest& operator=(const ExecutionRequest&) = delete;
};

/**
 * @brief Order execution result
 */
struct ExecutionResult {
  std::string requestId;
  std::string orderId;
  std::string venue;
  pinnacle::OrderStatus status;
  double filledQuantity{0.0};
  double avgFillPrice{0.0};
  double totalFees{0.0};
  uint64_t executionTime{0}; // Microseconds
  std::string errorMessage;
};

/**
 * @brief Routing strategy interface
 */
class RoutingStrategy {
public:
  virtual ~RoutingStrategy() = default;

  /**
   * @brief Plan order execution across venues and time
   */
  virtual std::vector<ExecutionRequest>
  planExecution(const ExecutionRequest& originalRequest,
                const std::vector<MarketData>& marketData) = 0;

  /**
   * @brief Get strategy name
   */
  virtual std::string getName() const = 0;
};

/**
 * @brief Best price routing strategy
 */
class BestPriceStrategy : public RoutingStrategy {
public:
  std::vector<ExecutionRequest>
  planExecution(const ExecutionRequest& originalRequest,
                const std::vector<MarketData>& marketData) override;

  std::string getName() const override { return "BEST_PRICE"; }
};

/**
 * @brief Time Weighted Average Price (TWAP) strategy
 */
class TWAPStrategy : public RoutingStrategy {
private:
  int m_numSlices;
  std::chrono::seconds m_sliceInterval;

public:
  explicit TWAPStrategy(int numSlices = 10, std::chrono::seconds sliceInterval =
                                                std::chrono::seconds(30))
      : m_numSlices(numSlices), m_sliceInterval(sliceInterval) {}

  std::vector<ExecutionRequest>
  planExecution(const ExecutionRequest& originalRequest,
                const std::vector<MarketData>& marketData) override;

  std::string getName() const override { return "TWAP"; }
};

/**
 * @brief Volume Weighted Average Price (VWAP) strategy
 */
class VWAPStrategy : public RoutingStrategy {
private:
  double m_participationRate;

public:
  explicit VWAPStrategy(double participationRate = 0.1)
      : m_participationRate(participationRate) {}

  std::vector<ExecutionRequest>
  planExecution(const ExecutionRequest& originalRequest,
                const std::vector<MarketData>& marketData) override;

  std::string getName() const override { return "VWAP"; }
};

/**
 * @brief Market impact minimization strategy
 */
class MarketImpactStrategy : public RoutingStrategy {
private:
  double m_maxImpactThreshold;

public:
  explicit MarketImpactStrategy(double maxImpactThreshold = 0.005)
      : m_maxImpactThreshold(maxImpactThreshold) {}

  std::vector<ExecutionRequest>
  planExecution(const ExecutionRequest& originalRequest,
                const std::vector<MarketData>& marketData) override;

  std::string getName() const override { return "MARKET_IMPACT"; }
};

/**
 * @brief Main order routing engine
 */
class OrderRouter {
public:
  /**
   * @brief Constructor
   */
  OrderRouter();

  /**
   * @brief Destructor
   */
  ~OrderRouter();

  /**
   * @brief Initialize the router with exchange connections
   */
  bool initialize();

  /**
   * @brief Start the routing engine
   */
  bool start();

  /**
   * @brief Stop the routing engine
   */
  bool stop();

  /**
   * @brief Check if router is running
   */
  bool isRunning() const { return m_isRunning.load(); }

  /**
   * @brief Submit order for smart routing
   */
  std::string submitOrder(const ExecutionRequest& request);

  /**
   * @brief Cancel a routing request
   */
  bool cancelOrder(const std::string& requestId);

  /**
   * @brief Get execution status
   */
  std::vector<ExecutionResult> getExecutionStatus(const std::string& requestId);

  /**
   * @brief Register execution callback
   */
  void
  setExecutionCallback(std::function<void(const ExecutionResult&)> callback);

  /**
   * @brief Add venue for routing (WebSocket or FIX)
   */
  bool addVenue(const std::string& venueName,
                const std::string& connectionType = "websocket");

  /**
   * @brief Remove venue from routing
   */
  bool removeVenue(const std::string& venueName);

  /**
   * @brief Update market data for a venue
   */
  void updateMarketData(const std::string& venue, const MarketData& data);

  /**
   * @brief Set routing strategy
   */
  void setRoutingStrategy(const std::string& strategyName);

  /**
   * @brief Get routing statistics
   */
  std::string getStatistics() const;

private:
  /**
   * @brief Routing engine state
   */
  std::atomic<bool> m_isRunning{false};
  std::atomic<bool> m_shouldStop{false};

  /**
   * @brief Available venues and their connections
   */
  struct VenueConnection {
    std::string name;
    std::string type;                // "websocket" or "fix"
    std::shared_ptr<void> connector; // WebSocket or FIX connector
    bool isActive{false};
    MarketData lastMarketData;
  };

  std::unordered_map<std::string, VenueConnection> m_venues;
  std::mutex m_venuesMutex;

  /**
   * @brief Active execution requests
   */
  struct ActiveExecution {
    ExecutionRequest originalRequest;
    std::vector<ExecutionRequest> childRequests;
    std::vector<ExecutionResult> results;
    uint64_t startTime;
    std::atomic<bool> isComplete{false};

    // Default constructor
    ActiveExecution() = default;

    // Move constructor and assignment
    ActiveExecution(ActiveExecution&& other) noexcept
        : originalRequest(std::move(other.originalRequest)),
          childRequests(std::move(other.childRequests)),
          results(std::move(other.results)), startTime(other.startTime),
          isComplete(other.isComplete.load()) {}

    ActiveExecution& operator=(ActiveExecution&& other) noexcept {
      if (this != &other) {
        originalRequest = std::move(other.originalRequest);
        childRequests = std::move(other.childRequests);
        results = std::move(other.results);
        startTime = other.startTime;
        isComplete = other.isComplete.load();
      }
      return *this;
    }

    // Delete copy constructor and assignment
    ActiveExecution(const ActiveExecution&) = delete;
    ActiveExecution& operator=(const ActiveExecution&) = delete;
  };

  std::unordered_map<std::string, ActiveExecution> m_activeExecutions;
  std::mutex m_executionsMutex;

  /**
   * @brief Routing strategies
   */
  std::unordered_map<std::string, std::unique_ptr<RoutingStrategy>>
      m_strategies;
  std::string m_currentStrategy{"BEST_PRICE"};

  /**
   * @brief Execution threads and queues
   */
  std::thread m_routingThread;
  std::thread m_executionThread;
  std::thread m_monitoringThread;

  utils::LockFreeMPMCQueue<ExecutionRequest, 1024> m_executionQueue;
  utils::LockFreeMPMCQueue<std::string, 256> m_cancelQueue;

  /**
   * @brief Execution callback
   */
  std::function<void(const ExecutionResult&)> m_executionCallback;
  std::mutex m_callbackMutex;

  /**
   * @brief Statistics
   */
  struct Statistics {
    uint64_t totalRequests{0};
    uint64_t completedExecutions{0};
    uint64_t canceledExecutions{0};
    uint64_t failedExecutions{0};
    double totalVolume{0.0};
    double avgExecutionTime{0.0}; // milliseconds
    double bestFillRate{
        0.0}; // percentage of orders getting best available price
  };

  Statistics m_stats;
  mutable std::mutex m_statsMutex;

  /**
   * @brief Internal methods
   */
  void routingThreadLoop();
  void executionThreadLoop();
  void monitoringThreadLoop();

  VenueConnection* selectBestVenue(const ExecutionRequest& request);
  std::vector<MarketData> getAllMarketData(const std::string& symbol);
  void executeOrder(const ExecutionRequest& request);
  void updateExecutionResult(const ExecutionResult& result);

  double calculateMarketImpact(const ExecutionRequest& request,
                               const MarketData& venue);
  double calculateTotalCost(const ExecutionRequest& request,
                            const MarketData& venue);

  void initializeStrategies();
  void processCompletedExecution(const std::string& requestId);

  /**
   * @brief Exchange connectors
   */
  std::shared_ptr<exchange::ExchangeConnectorFactory> m_webSocketFactory;
  std::shared_ptr<exchange::fix::FixConnectorFactory> m_fixFactory;

  /**
   * @brief Request ID generation
   */
  std::atomic<uint64_t> m_requestIdCounter{1};
  std::string generateRequestId();
};

} // namespace routing
} // namespace core
} // namespace pinnacle
