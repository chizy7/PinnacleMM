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

// Create Coinbase JWT
std::string createCoinbaseJWT(const std::string& apiKey,
                              const std::string& apiSecret) {
  (void)apiSecret; // Suppress unused parameter warning
  // JWT Header
  nlohmann::json header = {
      {"alg", "ES256"}, {"kid", apiKey}, {"nonce", generateNonce()}};

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

  // Create signature (simplified - in production use proper ES256)
  std::string signature = base64UrlEncode("placeholder_signature");

  return encodedHeader + "." + encodedPayload + "." + signature;
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

  try {
    connectWebSocket();
    m_processingThread =
        std::thread(&WebSocketMarketDataFeed::processMessages, this);
    m_isRunning.store(true, std::memory_order_release);

    std::lock_guard<std::mutex> lock(m_statusMutex);
    m_statusMessage = "Connected to " + getExchangeName();

    spdlog::info("WebSocket started successfully");
    return true;
  } catch (const std::exception& e) {
    spdlog::error("WebSocket start failed: {}", e.what());
    std::lock_guard<std::mutex> lock(m_statusMutex);
    m_statusMessage = "Failed to connect: " + std::string(e.what());
    return false;
  }
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

void WebSocketMarketDataFeed::connectWebSocket() {
  try {
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

    spdlog::info("Connecting to host: {}, port: {}, path: {}", host, port,
                 path);

    // Resolve host
    auto const results = resolver.resolve(host, port);
    spdlog::info("DNS resolution successful");

    // Create SSL stream and WebSocket
    auto ssl_stream = std::make_unique<boost::asio::ssl::stream<tcp::socket>>(
        *m_io_context, *m_ssl_context);

    // Connect TCP
    boost::asio::connect(ssl_stream->lowest_layer(), results);
    spdlog::info("TCP connection established");

    // Set SNI hostname
    if (!SSL_set_tlsext_host_name(ssl_stream->native_handle(), host.c_str())) {
      throw std::runtime_error("Failed to set SNI hostname");
    }

    // Perform SSL handshake
    ssl_stream->handshake(boost::asio::ssl::stream_base::client);
    spdlog::info("SSL handshake completed");

    // Create WebSocket stream
    m_websocket = std::make_unique<websocket_stream>(std::move(*ssl_stream));

    // Set WebSocket options
    m_websocket->set_option(boost::beast::websocket::stream_base::decorator(
        [](boost::beast::websocket::request_type& req) {
          req.set(boost::beast::http::field::user_agent, "PinnacleMM/1.0");
        }));

    // Perform WebSocket handshake
    m_websocket->handshake(host, path);
    spdlog::info("WebSocket handshake completed");

    spdlog::info("WebSocket connected to {}", m_endpoint);

    // Send pending subscriptions
    for (const auto& symbol : m_pendingSubscriptions) {
      sendSubscriptionInternal(symbol);
    }
    m_pendingSubscriptions.clear();

  } catch (const std::exception& e) {
    spdlog::error("WebSocket connection error: {}", e.what());
    throw std::runtime_error("WebSocket connection failed: " +
                             std::string(e.what()));
  }
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
}

void WebSocketMarketDataFeed::onDisconnect() {
  spdlog::info("WebSocket disconnected from {}", getExchangeName());
}

void WebSocketMarketDataFeed::onError(const std::string& error) {
  spdlog::error("WebSocket error: {}", error);
}

void WebSocketMarketDataFeed::onMessage(const std::string& message) {
  try {
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
    std::string subscriptionMessage = createSubscriptionMessage(symbol);
    spdlog::info("Sending subscription for {}: {}", symbol,
                 subscriptionMessage);

    boost::beast::error_code ec;
    m_websocket->write(boost::asio::buffer(subscriptionMessage), ec);

    if (ec) {
      throw std::runtime_error("Failed to send subscription message: " +
                               ec.message());
    }

    spdlog::info("Subscription sent successfully for {}", symbol);
    return true;
  } catch (const std::exception& e) {
    spdlog::error("Error sending subscription for {}: {}", symbol, e.what());
    std::lock_guard<std::mutex> lock(m_statusMutex);
    m_statusMessage = "Error sending subscription: " + std::string(e.what());
    return false;
  }
}

std::string
WebSocketMarketDataFeed::createSubscriptionMessage(const std::string& symbol) {
  nlohmann::json message;

  switch (m_exchange) {
  case Exchange::COINBASE: {
    message = {{"type", "subscribe"},
               {"product_ids", {symbol}},
               {"channels", {"ticker"}}};
    break;
  }

  default:
    throw std::runtime_error("Unsupported exchange for subscription");
  }

  return message.dump();
}

// Implement remaining methods with simplified logic for now
void WebSocketMarketDataFeed::initExchangeSpecifics() {
  switch (m_exchange) {
  case Exchange::COINBASE:
    m_exchangeName = "coinbase";
    m_endpoint = "wss://ws-feed.exchange.coinbase.com";
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
  auto it = m_marketUpdateCallbacks.find(update.symbol);
  if (it != m_marketUpdateCallbacks.end()) {
    for (const auto& callback : it->second) {
      callback(update);
    }
  }
}

void WebSocketMarketDataFeed::publishOrderBookUpdate(
    const OrderBookUpdate& update) {
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

    // Handle Coinbase messages
    if (json.contains("type")) {
      std::string type = json["type"];

      if (type == "l2update" && json.contains("product_id")) {
        // Order book update
        OrderBookUpdate update = parseOrderBookUpdate(message);

        auto it = m_orderBookUpdateCallbacks.find(update.symbol);
        if (it != m_orderBookUpdateCallbacks.end()) {
          for (const auto& callback : it->second) {
            callback(update);
          }
        }
      } else if (type == "ticker" && json.contains("product_id")) {
        // Market data update
        MarketUpdate update = parseMarketUpdate(message);

        auto it = m_marketUpdateCallbacks.find(update.symbol);
        if (it != m_marketUpdateCallbacks.end()) {
          for (const auto& callback : it->second) {
            callback(update);
          }
        }
      }
    }
  } catch (const std::exception& e) {
    spdlog::error("Error parsing message: {}", e.what());
  }
}

MarketUpdate
WebSocketMarketDataFeed::parseMarketUpdate(const std::string& message) {
  MarketUpdate update;

  try {
    auto json = nlohmann::json::parse(message);

    if (json.contains("product_id")) {
      update.symbol = json["product_id"];
    }

    if (json.contains("price")) {
      update.price = std::stod(json["price"].get<std::string>());
    }

    if (json.contains("last_size")) {
      update.volume = std::stod(json["last_size"].get<std::string>());
    }

    update.timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();

  } catch (const std::exception& e) {
    spdlog::error("Error parsing market update: {}", e.what());
  }

  return update;
}

OrderBookUpdate
WebSocketMarketDataFeed::parseOrderBookUpdate(const std::string& message) {
  OrderBookUpdate update;

  try {
    auto json = nlohmann::json::parse(message);

    if (json.contains("product_id")) {
      update.symbol = json["product_id"];
    }

    if (json.contains("changes")) {
      for (const auto& change : json["changes"]) {
        if (change.is_array() && change.size() >= 3) {
          std::string side = change[0];
          double price = std::stod(change[1].get<std::string>());
          double size = std::stod(change[2].get<std::string>());

          if (side == "buy") {
            update.bids.emplace_back(price, size);
          } else if (side == "sell") {
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
  }

  return update;
}

} // namespace exchange
} // namespace pinnacle
