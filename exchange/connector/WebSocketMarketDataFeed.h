#pragma once

#include "../../exchange/simulator/MarketDataFeed.h"
#include "../../core/utils/LockFreeQueue.h"
#include "SecureConfig.h"

#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <unordered_map>
#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/client.hpp>

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
    enum class Exchange {
        COINBASE,
        KRAKEN,
        GEMINI,
        BINANCE,
        BITSTAMP
    };
    
    /**
     * @brief Constructor
     * 
     * @param exchange Exchange to connect to
     * @param credentials API credentials for authentication
     */
    WebSocketMarketDataFeed(Exchange exchange, std::shared_ptr<utils::ApiCredentials> credentials);
    
    /**
     * @brief Destructor
     */
    ~WebSocketMarketDataFeed() override;
    
    // Implementation of MarketDataFeed interface
    bool start() override;
    bool stop() override;
    bool isRunning() const override;
    
    bool subscribeToMarketUpdates(
        const std::string& symbol,
        std::function<void(const MarketUpdate&)> callback) override;
    
    bool subscribeToOrderBookUpdates(
        const std::string& symbol,
        std::function<void(const OrderBookUpdate&)> callback) override;
    
    bool unsubscribeFromMarketUpdates(const std::string& symbol) override;
    bool unsubscribeFromOrderBookUpdates(const std::string& symbol) override;
    
    void publishMarketUpdate(const MarketUpdate& update) override;
    void publishOrderBookUpdate(const OrderBookUpdate& update) override;
    
    /**
     * @brief Set connection parameters
     * 
     * @param endpoint WebSocket endpoint URL
     * @param useSSL Whether to use SSL/TLS
     */
    void setConnectionParams(const std::string& endpoint, bool useSSL = true);
    
    /**
     * @brief Set authentication parameters
     * 
     * @param exchangeName Name of the exchange
     */
    void setAuthParams(const std::string& exchangeName);
    
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
    
    // WebSocket client
    using websocket_client = websocketpp::client<websocketpp::config::asio_tls_client>;
    using message_ptr = websocketpp::config::asio_client::message_type::ptr;
    using context_ptr = websocketpp::lib::shared_ptr<boost::asio::ssl::context>;
    
    websocket_client m_client;
    websocketpp::connection_hdl m_connection;
    
    // Connection state
    std::atomic<bool> m_isRunning{false};
    std::atomic<bool> m_shouldStop{false};
    std::string m_statusMessage;
    std::mutex m_statusMutex;
    
    // Subscription management
    std::unordered_map<std::string, std::vector<std::function<void(const MarketUpdate&)>>> m_marketUpdateCallbacks;
    std::unordered_map<std::string, std::vector<std::function<void(const OrderBookUpdate&)>>> m_orderBookUpdateCallbacks;
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
    void onOpen(websocketpp::connection_hdl hdl);
    void onClose(websocketpp::connection_hdl hdl);
    void onFail(websocketpp::connection_hdl hdl);
    void onMessage(websocketpp::connection_hdl hdl, message_ptr msg);
    
    // TLS handlers
    context_ptr onTlsInit(websocketpp::connection_hdl hdl);
    
    // Message parsing
    void parseMessage(const std::string& message);
    MarketUpdate parseMarketUpdate(const std::string& message);
    OrderBookUpdate parseOrderBookUpdate(const std::string& message);
    
    // Subscription methods
    bool sendSubscription(const std::string& symbol);
    std::string createSubscriptionMessage(const std::string& symbol);
    std::string createAuthenticationMessage();
    
    // Exchange-specific methods
    void initExchangeSpecifics();
    std::string getExchangeName() const;
    std::string getDefaultEndpoint() const;
};

} // namespace exchange
} // namespace pinnacle