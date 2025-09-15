#pragma once

#include "../../core/utils/LockFreeQueue.h"
#include "../../exchange/simulator/MarketDataFeed.h"
#include "SecureConfig.h"
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace pinnacle {
namespace exchange {

/**
 * @class WebSocketMarketDataFeed
 * @brief Real-time market data feed using WebSockets
 *
 * This class provides a concrete implementation of the MarketDataFeed interface
 * using WebSockets for real-time market data from cryptocurrency exchanges.
 */
class WebSocketMarketDataFeed : public MarketDataFeed {
public:
  /**
   * @brief Supported exchanges
   */
  enum class Exchange { COINBASE, KRAKEN, GEMINI, BINANCE, BITSTAMP };

  /**
   * @brief Constructor
   *
   * @param exchange Exchange to connect to
   * @param credentials API credentials for authentication
   */
  WebSocketMarketDataFeed(Exchange exchange,
                          std::shared_ptr<utils::ApiCredentials> credentials);

  /**
   * @brief Factory method to create WebSocketMarketDataFeed
   */
  static std::shared_ptr<WebSocketMarketDataFeed>
  create(Exchange exchange, std::shared_ptr<utils::ApiCredentials> credentials);

  /**
   * @brief Destructor
   */
  ~WebSocketMarketDataFeed() override;

  // Implementation of MarketDataFeed interface
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
   * @brief Set connection parameters
   *
   * @param endpoint WebSocket endpoint URL
   * @param useSSL Whether to use SSL/TLS
   */
  void setConnectionParams(const std::string &endpoint, bool useSSL = true);

  /**
   * @brief Set authentication parameters
   *
   * @param exchangeName Name of the exchange
   */
  void setAuthParams(const std::string &exchangeName);

  /**
   * @brief Get connection status message
   *
   * @return Current connection status message
   */
  std::string getStatusMessage() const;

private:
  // Exchange information
  Exchange m_exchange;
  std::string m_exchangeName;
  std::string m_endpoint;
  bool m_useSSL;

  // API credentials
  std::shared_ptr<utils::ApiCredentials> m_credentials;

  // WebSocket client using Boost.Beast
  using tcp = boost::asio::ip::tcp;
  using ssl_stream = boost::asio::ssl::stream<tcp::socket>;
  using websocket_stream = boost::beast::websocket::stream<ssl_stream>;
  using context_ptr = std::shared_ptr<boost::asio::ssl::context>;

  // Add these new variables
  std::shared_ptr<boost::asio::io_context> m_io_context;

  std::unique_ptr<websocket_stream> m_websocket;
  context_ptr m_ssl_context;

  // Connection state
  std::atomic<bool> m_isRunning{false};
  std::atomic<bool> m_shouldStop{false};
  std::string m_statusMessage;
  mutable std::mutex m_statusMutex;

  // Subscription management
  std::unordered_map<std::string,
                     std::vector<std::function<void(const MarketUpdate &)>>>
      m_marketUpdateCallbacks;
  std::unordered_map<std::string,
                     std::vector<std::function<void(const OrderBookUpdate &)>>>
      m_orderBookUpdateCallbacks;
  std::vector<std::string> m_pendingSubscriptions;
  std::mutex m_callbacksMutex;

  // Message processing
  std::thread m_processingThread;
  utils::LockFreeMPMCQueue<std::string, 1024> m_messageQueue;

  // Internal methods
  void initialize();
  void connectWebSocket();
  void disconnectWebSocket();
  void processMessages();

  // Event handlers
  void onConnect();
  void onDisconnect();
  void onError(const std::string &error);
  void onMessage(const std::string &message);

  // Message parsing
  void parseMessage(const std::string &message);
  MarketUpdate parseMarketUpdate(const std::string &message);
  OrderBookUpdate parseOrderBookUpdate(const std::string &message);

  // Subscription methods
  bool sendSubscription(const std::string &symbol);
  bool sendSubscriptionInternal(const std::string &symbol);
  std::string createSubscriptionMessage(const std::string &symbol);
  std::string createAuthenticationMessage();

  // Timer creation helper method
  void createTimer(long duration);

  // Add this helper method
  template <typename Callback> auto wrapWithStrand(Callback &&cb) {
    return std::forward<Callback>(cb);
  }

  // Exchange-specific methods
  void initExchangeSpecifics();
  std::string getExchangeName() const;
  std::string getDefaultEndpoint() const;
};

} // namespace exchange
} // namespace pinnacle