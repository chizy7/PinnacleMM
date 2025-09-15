#pragma once

#include "../../core/orderbook/Order.h"
#include "../simulator/MarketDataFeed.h"
#include "hffix.hpp"

namespace pinnacle {
namespace core {
using ::pinnacle::Order;
using ::pinnacle::OrderSide;
using ::pinnacle::OrderType;
} // namespace core
} // namespace pinnacle
#include "../../core/utils/LockFreeQueue.h"
#include "../connector/SecureConfig.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace pinnacle {
namespace exchange {
namespace fix {

/**
 * @class FixConnector
 * @brief Base class for FIX protocol exchange connectors
 *
 * Provides common FIX protocol functionality for connecting to exchanges
 * that support FIX protocol for market data and order execution.
 */
class FixConnector : public MarketDataFeed {
public:
  /**
   * @brief FIX session configuration
   */
  struct FixConfig {
    std::string senderCompId;
    std::string targetCompId;
    std::string fixVersion{"FIX.4.4"};
    std::string host;
    int port{0};
    bool useSSL{true};
    int heartbeatInterval{30};
    int logonTimeout{30};
    std::string resetSeqNumsOnLogon{"Y"};
  };

  /**
   * @brief Constructor
   *
   * @param config FIX session configuration
   * @param credentials API credentials
   */
  FixConnector(const FixConfig &config,
               std::shared_ptr<utils::ApiCredentials> credentials);

  /**
   * @brief Destructor
   */
  virtual ~FixConnector();

  // MarketDataFeed interface implementation
  bool start() override;
  bool stop() override;
  bool isRunning() const override;

  bool subscribeToMarketUpdates(
      const std::string &symbol,
      std::function<void(const MarketUpdate &)> callback) override;

  bool subscribeToOrderBookUpdates(
      const std::string &symbol,
      std::function<void(const OrderBookUpdate &)> callback) override;

  bool unsubscribeFromMarketUpdates(const std::string &symbol) override;
  bool unsubscribeFromOrderBookUpdates(const std::string &symbol) override;

  void publishMarketUpdate(const MarketUpdate &update) override;
  void publishOrderBookUpdate(const OrderBookUpdate &update) override;

  /**
   * @brief Order execution interface
   */
  virtual bool sendNewOrderSingle(const pinnacle::Order &order) = 0;
  virtual bool cancelOrder(const std::string &orderId) = 0;
  virtual bool replaceOrder(const std::string &orderId,
                            const pinnacle::Order &newOrder) = 0;

  /**
   * @brief Get connection status
   */
  bool isLoggedOn() const { return m_isLoggedOn.load(); }
  std::string getSessionStatus() const;

protected:
  /**
   * @brief Exchange-specific methods to be implemented by derived classes
   */
  virtual void onLogon() = 0;
  virtual void onLogout() = 0;
  virtual void onMarketDataMessage(const hffix::message_reader &msg) = 0;
  virtual void onExecutionReport(const hffix::message_reader &msg) = 0;
  virtual void onOrderCancelReject(const hffix::message_reader &msg) = 0;

  /**
   * @brief Send FIX message
   */
  bool sendMessage(const hffix::message_writer &msg);

  /**
   * @brief Create market data request
   */
  hffix::message_writer createMarketDataRequest(const std::string &symbol,
                                                char subscriptionRequestType);

  /**
   * @brief Create new order single message
   */
  hffix::message_writer createNewOrderSingle(const pinnacle::Order &order);

  /**
   * @brief Create order cancel request
   */
  hffix::message_writer createOrderCancelRequest(const std::string &orderId);

  /**
   * @brief Get next message sequence number
   */
  int getNextSeqNum() { return ++m_msgSeqNum; }

  /**
   * @brief Configuration and credentials
   */
  FixConfig m_config;
  std::shared_ptr<utils::ApiCredentials> m_credentials;

private:
  /**
   * @brief Connection management
   */
  void connectToExchange();
  void disconnectFromExchange();
  void networkThread();
  void processIncomingMessages();

  /**
   * @brief FIX protocol handlers
   */
  void sendLogon();
  void sendLogout();
  void sendHeartbeat();
  void sendTestRequest();
  void handleLogon(const hffix::message_reader &msg);
  void handleLogout(const hffix::message_reader &msg);
  void handleHeartbeat(const hffix::message_reader &msg);
  void handleTestRequest(const hffix::message_reader &msg);
  void handleResendRequest(const hffix::message_reader &msg);

  /**
   * @brief Message processing
   */
  void processMessage(const std::string &rawMessage);
  bool validateMessage(const hffix::message_reader &msg);

  /**
   * @brief Connection state
   */
  std::atomic<bool> m_isRunning{false};
  std::atomic<bool> m_isLoggedOn{false};
  std::atomic<bool> m_shouldStop{false};
  std::string m_sessionStatus;
  mutable std::mutex m_statusMutex;

  /**
   * @brief Networking
   */
  std::thread m_networkThread;
  int m_socket{-1};
  std::string m_receiveBuffer;
  static constexpr size_t RECEIVE_BUFFER_SIZE = 65536;

  /**
   * @brief Message sequence numbers
   */
  std::atomic<int> m_msgSeqNum{1};
  std::atomic<int> m_expectedSeqNum{1};

  /**
   * @brief Heartbeat management
   */
  std::chrono::steady_clock::time_point m_lastHeartbeatSent;
  std::chrono::steady_clock::time_point m_lastMessageReceived;

  /**
   * @brief Message queues
   */
  utils::LockFreeMPMCQueue<std::string, 1024> m_incomingMessages;
  utils::LockFreeMPMCQueue<std::string, 1024> m_outgoingMessages;

  /**
   * @brief Subscription management
   */
  std::unordered_map<std::string,
                     std::vector<std::function<void(const MarketUpdate &)>>>
      m_marketUpdateCallbacks;
  std::unordered_map<std::string,
                     std::vector<std::function<void(const OrderBookUpdate &)>>>
      m_orderBookUpdateCallbacks;
  std::mutex m_callbacksMutex;

  /**
   * @brief Message processing thread
   */
  std::thread m_messageProcessingThread;
  void messageProcessingLoop();
};

} // namespace fix
} // namespace exchange
} // namespace pinnacle