#include "WebSocketMarketDataFeed.h"
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <random>
#include <spdlog/spdlog.h>
#include <sstream>

namespace pinnacle {
namespace exchange {

// Helper function
std::string toLower(const std::string& str) {
  std::string result = str;
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return result;
}

// Base64 URL encoding helper
std::string base64UrlEncode(const std::string& input) {
  BIO* bio = BIO_new(BIO_s_mem());
  BIO* b64 = BIO_new(BIO_f_base64());
  BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
  bio = BIO_push(b64, bio);

  BIO_write(bio, input.c_str(), input.length());
  BIO_flush(bio);

  BUF_MEM* bufferPtr;
  BIO_get_mem_ptr(bio, &bufferPtr);

  std::string result(bufferPtr->data, bufferPtr->length);
  BIO_free_all(bio);

  // Convert to URL-safe base64
  std::replace(result.begin(), result.end(), '+', '-');
  std::replace(result.begin(), result.end(), '/', '_');

  // Remove padding
  result.erase(std::find(result.begin(), result.end(), '='), result.end());

  return result;
}

// Generate random nonce
std::string generateNonce() {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(0, 15);

  std::stringstream ss;
  for (int i = 0; i < 32; ++i) {
    ss << std::hex << dis(gen);
  }
  return ss.str();
}

// HMAC-SHA256 helper for JWT signing
std::string hmacSha256(const std::string& data, const std::string& key) {
  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int hashLen;

  HMAC(EVP_sha256(), key.c_str(), key.length(),
       reinterpret_cast<const unsigned char*>(data.c_str()), data.length(),
       hash, &hashLen);

  return std::string(reinterpret_cast<char*>(hash), hashLen);
}

// Create Coinbase JWT with proper HMAC-SHA256 signing
std::string createCoinbaseJWT(const std::string& apiKey,
                              const std::string& apiSecret) {
  // JWT Header for HS256 (HMAC-SHA256) - Coinbase Advanced Trade uses HS256,
  // not ES256
  nlohmann::json header = {{"alg", "HS256"}, {"typ", "JWT"}};

  // JWT Payload
  auto now = std::chrono::duration_cast<std::chrono::seconds>(
                 std::chrono::system_clock::now().time_since_epoch())
                 .count();

  nlohmann::json payload = {{"sub", apiKey},
                            {"iss", "cdp"},
                            {"nbf", now},
                            {"exp", now + 120}, // 2 minutes expiration
                            {"aud", {"retail_rest_api_proxy"}}};

  // Encode header and payload
  std::string encodedHeader = base64UrlEncode(header.dump());
  std::string encodedPayload = base64UrlEncode(payload.dump());

  // Create signature data
  std::string signatureData = encodedHeader + "." + encodedPayload;

  // Sign with HMAC-SHA256
  std::string signature = hmacSha256(signatureData, apiSecret);
  std::string encodedSignature = base64UrlEncode(signature);

  return signatureData + "." + encodedSignature;
}

WebSocketMarketDataFeed::WebSocketMarketDataFeed(
    Exchange exchange, std::shared_ptr<utils::ApiCredentials> credentials)
    : m_exchange(exchange), m_credentials(credentials),
      m_io_context(std::make_shared<boost::asio::io_context>()) {

  // Initialize exchange-specific settings
  initExchangeSpecifics();

  // Initialize SSL context
  m_ssl_context = std::make_shared<boost::asio::ssl::context>(
      boost::asio::ssl::context::tlsv12_client);
  m_ssl_context->set_default_verify_paths();
  m_ssl_context->set_verify_mode(boost::asio::ssl::verify_peer);

  // Initialize WebSocket client
  initialize();
}

WebSocketMarketDataFeed::~WebSocketMarketDataFeed() { stop(); }

std::shared_ptr<WebSocketMarketDataFeed> WebSocketMarketDataFeed::create(
    Exchange exchange, std::shared_ptr<utils::ApiCredentials> credentials) {
  return std::shared_ptr<WebSocketMarketDataFeed>(
      new WebSocketMarketDataFeed(exchange, credentials));
}

bool WebSocketMarketDataFeed::start() {
  if (m_isRunning.load(std::memory_order_acquire)) {
    spdlog::warn("WebSocket already running");
    return false;
  }

  spdlog::info("Starting WebSocket connection to {}", m_endpoint);
  m_shouldStop.store(false, std::memory_order_release);
  m_reconnectAttempts = 0;

  // Start processing thread first
  m_processingThread =
      std::thread(&WebSocketMarketDataFeed::processMessages, this);
  m_isRunning.store(true, std::memory_order_release);

  // Start connection attempts in background
  std::thread([this]() { connectWithRetry(); }).detach();

  spdlog::info("WebSocket service started, attempting connection...");
  return true;
}

bool WebSocketMarketDataFeed::stop() {
  if (!m_isRunning.load(std::memory_order_acquire)) {
    return false;
  }

  m_shouldStop.store(true, std::memory_order_release);

  try {
    disconnectWebSocket();

    if (m_processingThread.joinable()) {
      m_processingThread.join();
    }

    m_isRunning.store(false, std::memory_order_release);

    std::lock_guard<std::mutex> lock(m_statusMutex);
    m_statusMessage = "Disconnected from " + getExchangeName();

    return true;
  } catch (const std::exception& e) {
    std::lock_guard<std::mutex> lock(m_statusMutex);
    m_statusMessage = "Error during disconnect: " + std::string(e.what());
    return false;
  }
}

void WebSocketMarketDataFeed::initialize() {
  // Basic initialization - connection will be established in connectWebSocket()
}

void WebSocketMarketDataFeed::connectWithRetry() {
  while (!m_shouldStop.load(std::memory_order_acquire) &&
         m_reconnectAttempts < m_maxReconnectAttempts) {
    try {
      connectWebSocket();

      // Connection successful
      m_reconnectAttempts = 0;
      {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        m_statusMessage = "Connected to " + getExchangeName();
      }
      spdlog::info("WebSocket connected successfully to {}", m_endpoint);
      return;

    } catch (const std::exception& e) {
      m_reconnectAttempts++;
      {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        m_statusMessage = "Connection failed (attempt " +
                          std::to_string(m_reconnectAttempts) + "/" +
                          std::to_string(m_maxReconnectAttempts) +
                          "): " + std::string(e.what());
      }

      spdlog::error("WebSocket connection attempt {} failed: {}",
                    m_reconnectAttempts, e.what());

      if (m_reconnectAttempts < m_maxReconnectAttempts) {
        spdlog::info("Retrying connection in {} seconds...",
                     m_reconnectDelay / 1000);
        std::this_thread::sleep_for(
            std::chrono::milliseconds(m_reconnectDelay));
        // Exponential backoff with jitter
        m_reconnectDelay =
            std::min(m_reconnectDelay * 2, 30000); // Max 30 seconds
      }
    }
  }

  if (m_reconnectAttempts >= m_maxReconnectAttempts) {
    std::lock_guard<std::mutex> lock(m_statusMutex);
    m_statusMessage = "Connection failed after " +
                      std::to_string(m_maxReconnectAttempts) + " attempts";
    spdlog::error("WebSocket connection failed after {} attempts",
                  m_maxReconnectAttempts);
  }
}

void WebSocketMarketDataFeed::connectWebSocket() {
  spdlog::info("Attempting to connect to WebSocket endpoint: {}", m_endpoint);

  // Create WebSocket stream
  tcp::resolver resolver(*m_io_context);

  // Parse endpoint
  std::string host, port = "443", path = "/";
  if (m_endpoint.find("wss://") == 0) {
    std::string temp = m_endpoint.substr(6); // Remove "wss://"
    auto pathPos = temp.find('/');
    if (pathPos != std::string::npos) {
      host = temp.substr(0, pathPos);
      path = temp.substr(pathPos);
    } else {
      host = temp;
    }
  }

  spdlog::info("Connecting to host: {}, port: {}, path: {}", host, port, path);

  // Resolve host
  boost::system::error_code resolve_ec;
  auto const results = resolver.resolve(host, port, resolve_ec);
  if (resolve_ec) {
    throw std::runtime_error("DNS resolution failed: " + resolve_ec.message());
  }
  spdlog::info("DNS resolution successful");

  // Create SSL stream and WebSocket
  auto ssl_stream = std::make_unique<boost::asio::ssl::stream<tcp::socket>>(
      *m_io_context, *m_ssl_context);

  // Connect TCP
  boost::system::error_code connect_ec;
  boost::asio::connect(ssl_stream->lowest_layer(), results, connect_ec);
  if (connect_ec) {
    throw std::runtime_error("TCP connection failed: " + connect_ec.message());
  }
  spdlog::info("TCP connection established");

  // Set SNI hostname
  if (!SSL_set_tlsext_host_name(ssl_stream->native_handle(), host.c_str())) {
    throw std::runtime_error("Failed to set SNI hostname");
  }

  // Perform SSL handshake
  boost::system::error_code ssl_ec;
  ssl_stream->handshake(boost::asio::ssl::stream_base::client, ssl_ec);
  if (ssl_ec) {
    throw std::runtime_error("SSL handshake failed: " + ssl_ec.message());
  }
  spdlog::info("SSL handshake completed");

  // Create WebSocket stream
  m_websocket = std::make_unique<websocket_stream>(std::move(*ssl_stream));

  // Set WebSocket options
  m_websocket->set_option(boost::beast::websocket::stream_base::decorator(
      [](boost::beast::websocket::request_type& req) {
        req.set(boost::beast::http::field::user_agent, "PinnacleMM/1.0");
      }));

  // Perform WebSocket handshake
  boost::beast::error_code ws_ec;
  m_websocket->handshake(host, path, ws_ec);
  if (ws_ec) {
    throw std::runtime_error("WebSocket handshake failed: " + ws_ec.message());
  }
  spdlog::info("WebSocket handshake completed");

  // Send pending subscriptions
  for (const auto& symbol : m_pendingSubscriptions) {
    sendSubscriptionInternal(symbol);
  }
  m_pendingSubscriptions.clear();
}

void WebSocketMarketDataFeed::disconnectWebSocket() {
  if (m_websocket) {
    try {
      m_websocket->close(boost::beast::websocket::close_code::normal);
    } catch (...) {
      // Ignore close errors
    }
    m_websocket.reset();
  }
}

void WebSocketMarketDataFeed::processMessages() {
  boost::beast::flat_buffer buffer;

  while (!m_shouldStop.load(std::memory_order_acquire)) {
    try {
      if (!m_websocket) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        continue;
      }

      // Read message
      boost::beast::error_code ec;
      auto bytes_transferred = m_websocket->read(buffer, ec);

      if (ec) {
        if (ec != boost::beast::websocket::error::closed) {
          spdlog::error("WebSocket read error: {}", ec.message());
        }
        break;
      }

      // Convert to string and process
      std::string message = boost::beast::buffers_to_string(buffer.data());
      buffer.consume(bytes_transferred);

      onMessage(message);

    } catch (const std::exception& e) {
      spdlog::error("Error in message processing: {}", e.what());
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
  }
}

void WebSocketMarketDataFeed::onConnect() {
  spdlog::info("WebSocket connected to {}", getExchangeName());
  if (m_jsonLogger) {
    m_jsonLogger->logConnectionEvent("connected", getExchangeName());
  }
}

void WebSocketMarketDataFeed::onDisconnect() {
  spdlog::info("WebSocket disconnected from {}", getExchangeName());
  if (m_jsonLogger) {
    m_jsonLogger->logConnectionEvent("disconnected", getExchangeName());
  }
}

void WebSocketMarketDataFeed::onError(const std::string& error) {
  spdlog::error("WebSocket error: {}", error);
  if (m_jsonLogger) {
    m_jsonLogger->logConnectionEvent("error", getExchangeName(), error);
  }
}

void WebSocketMarketDataFeed::onMessage(const std::string& message) {
  try {
    // Always log full message at info level for debugging Coinbase issue
    spdlog::info("WebSocket received message (length: {}): {}",
                 message.length(), message);

    spdlog::debug("Received message: {}",
                  message.substr(0, 200) +
                      (message.length() > 200 ? "..." : ""));
    parseMessage(message);
  } catch (const std::exception& e) {
    spdlog::error("Error parsing message: {}", e.what());
  }
}

bool WebSocketMarketDataFeed::sendSubscription(const std::string& symbol) {
  if (!m_isRunning.load(std::memory_order_acquire) || !m_websocket) {
    // Add to pending subscriptions if not connected yet
    m_pendingSubscriptions.push_back(symbol);
    return true;
  }

  return sendSubscriptionInternal(symbol);
}

bool WebSocketMarketDataFeed::sendSubscriptionInternal(
    const std::string& symbol) {
  try {
    // Coinbase Advanced Trade requires separate subscription for each channel
    // Try public channels first (no auth), then authenticated channels
    std::vector<std::pair<std::string, bool>> channels = {
        {"heartbeats", true}, // heartbeats requires authentication
        {"ticker", false},    // try ticker without auth first (public data)
        {"level2", false}     // try level2 without auth first (public data)
    };

    for (const auto& channelPair : channels) {
      const std::string& channel = channelPair.first;
      bool requiresAuth = channelPair.second;

      std::string subscriptionMessage =
          createSubscriptionMessage(symbol, channel, requiresAuth);
      spdlog::info("Sending {} subscription for {} (auth: {}): {}", channel,
                   symbol, requiresAuth, subscriptionMessage);

      boost::beast::error_code ec;
      m_websocket->write(boost::asio::buffer(subscriptionMessage), ec);

      if (ec) {
        throw std::runtime_error("Failed to send " + channel +
                                 " subscription message: " + ec.message());
      }

      spdlog::info("{} subscription sent successfully for {}", channel, symbol);

      // Small delay between subscriptions to avoid rate limiting
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return true;
  } catch (const std::exception& e) {
    spdlog::error("Error sending subscription for {}: {}", symbol, e.what());
    std::lock_guard<std::mutex> lock(m_statusMutex);
    m_statusMessage = "Error sending subscription: " + std::string(e.what());
    return false;
  }
}

std::string WebSocketMarketDataFeed::createSubscriptionMessage(
    const std::string& symbol, const std::string& channel, bool requiresAuth) {
  nlohmann::json message;

  switch (m_exchange) {
  case Exchange::COINBASE: {
    // Get JWT token for authentication (fresh JWT for each subscription)
    std::string jwt;
    if (requiresAuth && m_credentials) {
      auto apiKey = m_credentials->getApiKey("coinbase");
      auto apiSecret = m_credentials->getApiSecret("coinbase");

      if (apiKey && apiSecret) {
        jwt = createCoinbaseJWT(*apiKey, *apiSecret);
        spdlog::debug("Generated fresh JWT for {} channel subscription",
                      channel);
      } else {
        spdlog::warn(
            "No Coinbase credentials found for authenticated channel: {}",
            channel);
      }
    }

    // Coinbase Advanced Trade requires ONE channel per subscription
    message = {
        {"type", "subscribe"},
        {"product_ids", {symbol}},
        {"channel",
         channel} // Single channel per subscription (API requirement)
    };

    // Add JWT only if authentication is required and JWT is available
    if (requiresAuth && !jwt.empty()) {
      message["jwt"] = jwt;
    }

    break;
  }

  default:
    throw std::runtime_error("Unsupported exchange for subscription");
  }

  return message.dump();
}

// Overload for backward compatibility
std::string
WebSocketMarketDataFeed::createSubscriptionMessage(const std::string& symbol,
                                                   const std::string& channel) {
  // Default to requiring authentication for backward compatibility
  return createSubscriptionMessage(symbol, channel, true);
}

// Overload for backward compatibility
std::string
WebSocketMarketDataFeed::createSubscriptionMessage(const std::string& symbol) {
  // Default to level2 for backward compatibility
  return createSubscriptionMessage(symbol, "level2", true);
}

// Implement remaining methods with simplified logic for now
void WebSocketMarketDataFeed::initExchangeSpecifics() {
  switch (m_exchange) {
  case Exchange::COINBASE:
    m_exchangeName = "coinbase";
    m_endpoint = "wss://advanced-trade-ws.coinbase.com";
    m_useSSL = true;
    break;
  default:
    throw std::runtime_error("Unsupported exchange");
  }
}

std::string WebSocketMarketDataFeed::getExchangeName() const {
  return m_exchangeName;
}

std::string WebSocketMarketDataFeed::getStatusMessage() const {
  std::lock_guard<std::mutex> lock(m_statusMutex);
  return m_statusMessage;
}

void WebSocketMarketDataFeed::setConnectionParams(const std::string& endpoint,
                                                  bool useSSL) {
  if (!m_isRunning.load(std::memory_order_acquire)) {
    m_endpoint = endpoint;
    m_useSSL = useSSL;
  }
}

void WebSocketMarketDataFeed::setAuthParams(const std::string& exchangeName) {
  if (!m_isRunning.load(std::memory_order_acquire)) {
    m_exchangeName = exchangeName;
  }
}

void WebSocketMarketDataFeed::setJsonLogger(
    std::shared_ptr<utils::JsonLogger> jsonLogger) {
  m_jsonLogger = jsonLogger;
}

bool WebSocketMarketDataFeed::subscribeToMarketUpdates(
    const std::string& symbol,
    std::function<void(const MarketUpdate&)> callback) {
  m_marketUpdateCallbacks[symbol].push_back(callback);
  return sendSubscription(symbol);
}

bool WebSocketMarketDataFeed::subscribeToOrderBookUpdates(
    const std::string& symbol,
    std::function<void(const OrderBookUpdate&)> callback) {
  m_orderBookUpdateCallbacks[symbol].push_back(callback);
  return sendSubscription(symbol);
}

bool WebSocketMarketDataFeed::unsubscribeFromMarketUpdates(
    const std::string& symbol) {
  m_marketUpdateCallbacks.erase(symbol);
  return true;
}

bool WebSocketMarketDataFeed::unsubscribeFromOrderBookUpdates(
    const std::string& symbol) {
  m_orderBookUpdateCallbacks.erase(symbol);
  return true;
}

void WebSocketMarketDataFeed::publishMarketUpdate(const MarketUpdate& update) {
  // Log to JSON if enabled
  if (m_jsonLogger) {
    m_jsonLogger->logMarketUpdate(update);
  }

  auto it = m_marketUpdateCallbacks.find(update.symbol);
  if (it != m_marketUpdateCallbacks.end()) {
    for (const auto& callback : it->second) {
      callback(update);
    }
  }
}

void WebSocketMarketDataFeed::publishOrderBookUpdate(
    const OrderBookUpdate& update) {
  // Log to JSON if enabled
  if (m_jsonLogger) {
    m_jsonLogger->logOrderBookUpdate(update);
  }

  auto it = m_orderBookUpdateCallbacks.find(update.symbol);
  if (it != m_orderBookUpdateCallbacks.end()) {
    for (const auto& callback : it->second) {
      callback(update);
    }
  }
}

bool WebSocketMarketDataFeed::isRunning() const {
  return m_isRunning.load(std::memory_order_acquire);
}

void WebSocketMarketDataFeed::parseMessage(const std::string& message) {
  try {
    auto json = nlohmann::json::parse(message);

    // Debug: Log the message structure for analysis
    spdlog::info("Parsing JSON message with keys: [{}]", [&json]() {
      std::string keys;
      for (auto it = json.begin(); it != json.end(); ++it) {
        if (!keys.empty())
          keys += ", ";
        keys += "\"" + it.key() + "\"";
      }
      return keys;
    }());

    // Handle Coinbase Advanced Trade messages
    if (json.contains("channel")) {
      std::string channel = json["channel"];
      spdlog::info("Processing channel: {}", channel);

      if ((channel == "level2" || channel == "l2_data") &&
          json.contains("events")) {
        spdlog::info("Processing level2/l2_data events, count: {}",
                     json["events"].size());
        // Level 2 order book updates
        for (const auto& event : json["events"]) {
          if (event.contains("type") && event["type"] == "update") {
            spdlog::info("Processing level2 update event");
            OrderBookUpdate update = parseOrderBookUpdate(event.dump());

            auto it = m_orderBookUpdateCallbacks.find(update.symbol);
            if (it != m_orderBookUpdateCallbacks.end()) {
              spdlog::info("Calling {} order book callbacks for {}",
                           it->second.size(), update.symbol);
              for (const auto& callback : it->second) {
                callback(update);
              }
            } else {
              spdlog::warn("No order book callbacks registered for symbol: {}",
                           update.symbol);
            }
          } else {
            spdlog::info("Level2 event type: {}",
                         event.value("type", "unknown"));
          }
        }
      } else if (channel == "ticker" && json.contains("events")) {
        spdlog::info("Processing ticker events, count: {}",
                     json["events"].size());
        // Ticker updates
        for (const auto& event : json["events"]) {
          if (event.contains("tickers")) {
            for (const auto& ticker : event["tickers"]) {
              MarketUpdate update = parseMarketUpdate(ticker.dump());

              auto it = m_marketUpdateCallbacks.find(update.symbol);
              if (it != m_marketUpdateCallbacks.end()) {
                spdlog::info("Calling {} market update callbacks for {}",
                             it->second.size(), update.symbol);
                for (const auto& callback : it->second) {
                  callback(update);
                }
              } else {
                spdlog::warn(
                    "No market update callbacks registered for symbol: {}",
                    update.symbol);
              }
            }
          }
        }
      } else if (channel == "heartbeats" && json.contains("events")) {
        // Handle heartbeat messages - these keep the connection alive
        spdlog::debug("Received heartbeat message - connection is alive");
        for (const auto& event : json["events"]) {
          if (event.contains("heartbeat_counter")) {
            spdlog::debug("Heartbeat #{}: {}",
                          event.value("heartbeat_counter", 0),
                          event.value("current_time", "unknown"));
          }
        }
      } else if (channel == "subscriptions") {
        // Handle subscription confirmation messages
        spdlog::info("Subscription confirmation received: {}",
                     message.substr(0, 300));
      } else {
        spdlog::warn(
            "Unknown channel or missing events: channel={}, has_events={}",
            channel, json.contains("events"));
      }
    }
    // Handle subscription acknowledgments, errors, and original ticker messages
    else if (json.contains("type")) {
      std::string type = json["type"];
      spdlog::info("Processing message type: {}", type);

      if (type == "subscriptions") {
        spdlog::info("Subscription confirmed: {}", message.substr(0, 200));
      } else if (type == "error") {
        std::string errorMsg = json.value("message", "Unknown error");
        spdlog::error("WebSocket error from server: {}", errorMsg);
      } else if (type == "ticker" && json.contains("product_id")) {
        // Handle original Coinbase ticker format for immediate price updates
        spdlog::info("Processing ticker message for price update");
        MarketUpdate update = parseMarketUpdate(message);

        auto it = m_marketUpdateCallbacks.find(update.symbol);
        if (it != m_marketUpdateCallbacks.end()) {
          spdlog::info("Calling {} market update callbacks for ticker data: {}",
                       it->second.size(), update.symbol);
          for (const auto& callback : it->second) {
            callback(update);
          }
        } else {
          spdlog::warn(
              "No market update callbacks registered for ticker symbol: {}",
              update.symbol);
        }
      } else if (type == "heartbeat") {
        // Handle heartbeat messages to keep connection alive
        spdlog::debug("Received heartbeat message - connection is alive");
      } else {
        spdlog::info("Unhandled message type: {}", type);
      }
    } else {
      spdlog::warn("Message has no 'channel' or 'type' field - unknown format");
    }
  } catch (const std::exception& e) {
    spdlog::error("Error parsing message: {}", e.what());
    spdlog::debug("Problematic message: {}", message.substr(0, 500));
  }
}

MarketUpdate
WebSocketMarketDataFeed::parseMarketUpdate(const std::string& message) {
  MarketUpdate update;

  try {
    auto json = nlohmann::json::parse(message);

    // Coinbase Advanced Trade ticker format
    if (json.contains("product_id")) {
      update.symbol = json["product_id"];
    }

    if (json.contains("price")) {
      update.price = std::stod(json["price"].get<std::string>());
    }

    // Handle volume from various possible fields
    if (json.contains("last_size")) {
      update.volume = std::stod(json["last_size"].get<std::string>());
    } else if (json.contains("volume_24_h")) {
      update.volume = std::stod(json["volume_24_h"].get<std::string>());
    }

    // Parse additional ticker fields from Coinbase Advanced Trade
    if (json.contains("best_bid")) {
      update.bidPrice = std::stod(json["best_bid"].get<std::string>());
    }

    if (json.contains("best_ask")) {
      update.askPrice = std::stod(json["best_ask"].get<std::string>());
    }

    update.timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();

  } catch (const std::exception& e) {
    spdlog::error("Error parsing market update: {}", e.what());
    spdlog::debug("Problematic ticker message: {}", message.substr(0, 200));
  }

  return update;
}

OrderBookUpdate
WebSocketMarketDataFeed::parseOrderBookUpdate(const std::string& message) {
  OrderBookUpdate update;

  try {
    auto json = nlohmann::json::parse(message);

    // Advanced Trade format
    if (json.contains("product_id")) {
      update.symbol = json["product_id"];
    }

    // Parse updates array for Advanced Trade format
    if (json.contains("updates")) {
      for (const auto& updateItem : json["updates"]) {
        if (updateItem.contains("side") && updateItem.contains("price_level") &&
            updateItem.contains("new_quantity")) {
          std::string side = updateItem["side"];
          double price =
              std::stod(updateItem["price_level"].get<std::string>());
          double size =
              std::stod(updateItem["new_quantity"].get<std::string>());

          if (side == "bid") {
            update.bids.emplace_back(price, size);
          } else if (side == "offer") {
            update.asks.emplace_back(price, size);
          }
        }
      }
    }
    // Fallback for old format (legacy support)
    else if (json.contains("changes")) {
      for (const auto& change : json["changes"]) {
        if (change.is_array() && change.size() >= 3) {
          std::string side = change[0];
          double price = std::stod(change[1].get<std::string>());
          double size = std::stod(change[2].get<std::string>());

          if (side == "buy" || side == "bid") {
            update.bids.emplace_back(price, size);
          } else if (side == "sell" || side == "offer") {
            update.asks.emplace_back(price, size);
          }
        }
      }
    }

    update.timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();

  } catch (const std::exception& e) {
    spdlog::error("Error parsing order book update: {}", e.what());
    spdlog::debug("Problematic message: {}", message.substr(0, 500));
  }

  return update;
}

} // namespace exchange
} // namespace pinnacle
