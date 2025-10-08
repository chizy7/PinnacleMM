#include "WebServer.h"
#include "../strategies/analytics/MarketRegimeDetector.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <spdlog/spdlog.h>
#include <sstream>

namespace pinnacle {
namespace visualization {

// ============================================================================
// PerformanceCollector Implementation
// ============================================================================

PerformanceCollector::PerformanceCollector() = default;

PerformanceCollector::~PerformanceCollector() { stopCollection(); }

void PerformanceCollector::registerStrategy(const std::string& strategyId,
                                            std::shared_ptr<void> strategy) {
  std::lock_guard<std::mutex> lock(m_mutex);
  PerformanceData data;
  data.timestamp = 0; // Will be set during collection
  m_performanceData[strategyId] = data;
}

void PerformanceCollector::unregisterStrategy(const std::string& strategyId) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_performanceData.erase(strategyId);
}

void PerformanceCollector::startCollection(uint64_t intervalMs) {
  if (m_collecting.load()) {
    return;
  }
  m_collecting.store(true);
  spdlog::info("Started performance data collection (interval: {}ms)",
               intervalMs);
}

void PerformanceCollector::stopCollection() {
  if (!m_collecting.load()) {
    return;
  }
  m_collecting.store(false);
  if (m_collectionThread.joinable()) {
    m_collectionThread.join();
  }
  spdlog::info("Stopped performance data collection");
}

PerformanceData PerformanceCollector::getLatestPerformance(
    const std::string& strategyId) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_performanceData.find(strategyId);
  if (it != m_performanceData.end()) {
    return it->second;
  }
  return PerformanceData{};
}

std::vector<ChartDataPoint>
PerformanceCollector::getChartData(const std::string& strategyId,
                                   const std::string& metric,
                                   uint64_t timeRange) const {
  std::vector<ChartDataPoint> result;
  // Return empty for now - can be implemented later
  return result;
}

size_t PerformanceCollector::getRegisteredStrategiesCount() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_performanceData.size();
}

void PerformanceCollector::setMaxHistorySize(size_t maxSize) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_maxHistorySize = maxSize;
}

void PerformanceCollector::updateMarketData(const std::string& symbol,
                                            const MarketData& data) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_marketData[symbol] = data;
}

// ============================================================================
// WebSocketSession Implementation
// ============================================================================

WebSocketSession::WebSocketSession(
    tcp::socket&& socket, std::shared_ptr<PerformanceCollector> collector,
    std::shared_ptr<WebSocketHandler> handler)
    : m_ws(std::move(socket)), m_collector(collector), m_handler(handler) {
  spdlog::debug("WebSocketSession constructor called");
}

WebSocketSession::~WebSocketSession() {
  spdlog::debug("WebSocketSession destructor called");
  // Don't call shared_from_this() in destructor - the object is being destroyed
  // Session cleanup is handled by the handler when the session is closed
}

void WebSocketSession::start() {
  spdlog::debug("WebSocketSession::start() called");

  // Set WebSocket options first
  m_ws.set_option(
      websocket::stream_base::timeout::suggested(beast::role_type::server));
  m_ws.set_option(
      websocket::stream_base::decorator([](websocket::response_type& res) {
        res.set(http::field::server, "PinnacleMM-Visualization/1.0");
      }));

  // Accept the WebSocket handshake
  spdlog::debug("About to call shared_from_this() for async_accept");
  try {
    auto self = shared_from_this();
    spdlog::debug("shared_from_this() successful for async_accept");
    spdlog::debug("About to call m_ws.async_accept()");

    // Accept the WebSocket connection
    m_ws.async_accept([self](beast::error_code ec) {
      spdlog::debug("async_accept callback invoked with ec: {}", ec.message());
      if (!ec) {
        spdlog::debug("WebSocket accept successful, calling onAccept");
        self->onAccept(ec);

        // Register session with handler AFTER onAccept completes
        if (self->m_handler) {
          try {
            spdlog::debug("About to add session to handler");
            self->m_handler->addSession(self);
            spdlog::debug("Session added to handler successfully");
          } catch (const std::exception& e) {
            spdlog::error("Exception adding session to handler: {}", e.what());
          }
        }
      } else {
        spdlog::error("async_accept failed: {}", ec.message());
      }
    });
    spdlog::debug("async_accept call completed");
  } catch (const std::exception& e) {
    spdlog::error("Exception calling shared_from_this() for async_accept: {}",
                  e.what());
    return;
  }
}

void WebSocketSession::onAccept(beast::error_code ec) {
  spdlog::debug("WebSocketSession::onAccept() called");
  if (ec) {
    spdlog::error("WebSocket accept error: {}", ec.message());
    // Clean up session on accept failure
    if (m_handler) {
      try {
        auto self = shared_from_this();
        m_handler->removeSession(self);
      } catch (...) {
        // Ignore errors during cleanup
      }
    }
    return;
  }

  spdlog::info("WebSocket client connected");

  try {
    spdlog::debug("WebSocketSession::onAccept() completed successfully - "
                  "starting doRead()");
    doRead();
    spdlog::debug("doRead() started successfully");
  } catch (const std::exception& e) {
    spdlog::error("Exception in WebSocketSession::onAccept(): {}", e.what());
    return;
  }
}

void WebSocketSession::doRead() {
  spdlog::debug("WebSocketSession::doRead() called");
  try {
    auto self = shared_from_this();
    spdlog::debug("shared_from_this() successful in doRead()");
    m_ws.async_read(m_buffer,
                    beast::bind_front_handler(&WebSocketSession::onRead, self));
  } catch (const std::exception& e) {
    spdlog::error("Exception in WebSocketSession::doRead(): {}", e.what());
    return;
  }
}

void WebSocketSession::onRead(beast::error_code ec,
                              std::size_t bytes_transferred) {
  boost::ignore_unused(bytes_transferred);

  if (ec == websocket::error::closed) {
    spdlog::info("WebSocket client disconnected");
    return;
  }

  if (ec) {
    spdlog::error("WebSocket read error: {}", ec.message());
    return;
  }

  // Process the message
  auto message = beast::buffers_to_string(m_buffer.data());
  m_buffer.consume(m_buffer.size());

  try {
    handleMessage(message);
  } catch (const std::exception& e) {
    spdlog::error("Error processing WebSocket message: {}", e.what());
  }

  // Continue reading
  doRead();
}

void WebSocketSession::handleMessage(const std::string& message) {
  try {
    json request = json::parse(message);
    std::string type = request.value("type", "");

    if (type == "subscribe") {
      handleSubscribe(request);
    } else if (type == "get_history") {
      handleGetHistory(request);
    } else if (type == "get_strategies") {
      handleGetStrategies(request);
    } else {
      json errorMsg = {{"type", "error"},
                       {"message", "Unknown message type: " + type},
                       {"timestamp", utils::TimeUtils::getCurrentNanos()}};
      send(errorMsg);
    }
  } catch (const std::exception& e) {
    json errorMsg = {
        {"type", "error"},
        {"message", "Failed to parse message: " + std::string(e.what())},
        {"timestamp", utils::TimeUtils::getCurrentNanos()}};
    send(errorMsg);
  }
}

void WebSocketSession::handleSubscribe(const json& request) {
  try {
    std::string strategyId = request.value("strategy_id", "");
    std::string dataType = request.value("data_type", "performance");

    spdlog::debug(
        "Processing subscribe request for strategy: '{}', data_type: '{}'",
        strategyId, dataType);

    json response = {{"type", "subscribe_ack"},
                     {"strategy_id", strategyId},
                     {"data_type", dataType},
                     {"status", "subscribed"},
                     {"timestamp", utils::TimeUtils::getCurrentNanos()}};

    spdlog::debug("About to send subscribe response");
    send(response);
    spdlog::debug("Subscribe response sent successfully");
    spdlog::info("Client subscribed to {} data for strategy {}", dataType,
                 strategyId);
  } catch (const std::exception& e) {
    spdlog::error("Exception in handleSubscribe: {}", e.what());
  }
}

void WebSocketSession::handleGetHistory(const json& request) {
  std::string strategyId = request.value("strategy_id", "");
  std::string metric = request.value("metric", "pnl");
  uint64_t timeRange =
      request.value("time_range", 3600000000000ULL); // 1 hour default

  auto chartData = m_collector->getChartData(strategyId, metric, timeRange);

  json chartJson = json::array();
  for (const auto& point : chartData) {
    chartJson.push_back({{"timestamp", point.timestamp},
                         {"value", point.value},
                         {"label", point.label},
                         {"color", point.color}});
  }

  json response = {{"type", "history_data"},
                   {"strategy_id", strategyId},
                   {"metric", metric},
                   {"data", chartJson},
                   {"timestamp", utils::TimeUtils::getCurrentNanos()}};

  send(response);
}

void WebSocketSession::handleGetStrategies(const json& request) {
  boost::ignore_unused(request);

  json strategies = json::array();
  strategies.push_back({{"id", "primary_strategy"},
                        {"name", "ML-Enhanced Market Maker"},
                        {"symbol", "BTC-USD"},
                        {"status", "running"},
                        {"ml_enabled", true}});

  json response = {{"type", "strategies_list"},
                   {"strategies", strategies},
                   {"count", strategies.size()},
                   {"timestamp", utils::TimeUtils::getCurrentNanos()}};

  send(response);
}

bool WebSocketSession::send(const json& data) {
  try {
    // Check if WebSocket is still valid before sending
    if (!m_ws.is_open()) {
      spdlog::debug("WebSocket is closed, skipping send");
      return false;
    }

    bool shouldStartWrite = false;
    {
      std::lock_guard<std::mutex> lock(m_writeMutex);
      m_writeQueue.push(data.dump());

      if (!m_writing) {
        shouldStartWrite = true;
      }
    }

    if (shouldStartWrite) {
      doWrite();
    }
    return true;
  } catch (const std::exception& e) {
    spdlog::error("Exception in WebSocketSession::send(): {}", e.what());
    return false;
  }
}

void WebSocketSession::doWrite() {
  std::string message;
  {
    std::lock_guard<std::mutex> lock(m_writeMutex);
    if (m_writeQueue.empty()) {
      m_writing = false;
      return;
    }

    m_writing = true;
    message = m_writeQueue.front();
    m_writeQueue.pop();
  }

  spdlog::debug("WebSocketSession::doWrite() called");
  try {
    // Check if WebSocket is still open before writing
    if (!m_ws.is_open()) {
      spdlog::debug("WebSocket is closed, skipping write");
      std::lock_guard<std::mutex> lock(m_writeMutex);
      m_writing = false;
      return;
    }

    auto self = shared_from_this();
    spdlog::debug("shared_from_this() successful in doWrite()");
    m_ws.async_write(
        net::buffer(message),
        beast::bind_front_handler(&WebSocketSession::onWrite, self));
  } catch (const std::exception& e) {
    spdlog::error("Exception in WebSocketSession::doWrite(): {}", e.what());
    std::lock_guard<std::mutex> lock(m_writeMutex);
    m_writing = false;
    return;
  }
}

void WebSocketSession::onWrite(beast::error_code ec,
                               std::size_t bytes_transferred) {
  boost::ignore_unused(bytes_transferred);

  if (ec) {
    spdlog::error("WebSocket write error: {}", ec.message());
    return;
  }

  std::lock_guard<std::mutex> lock(m_writeMutex);
  if (!m_writeQueue.empty()) {
    doWrite();
  } else {
    m_writing = false;
  }
}

void WebSocketSession::close() {
  // Remove this session from the handler before closing
  if (m_handler) {
    try {
      auto self = shared_from_this();
      m_handler->removeSession(self);
    } catch (const std::exception& e) {
      spdlog::error("Exception in WebSocketSession::close() calling "
                    "shared_from_this(): {}",
                    e.what());
    }
  }

  m_ws.async_close(websocket::close_code::normal, [](beast::error_code ec) {
    if (ec) {
      spdlog::error("WebSocket close error: {}", ec.message());
    }
  });
}

// ============================================================================
// WebSocketHandler Implementation
// ============================================================================

WebSocketHandler::WebSocketHandler(
    std::shared_ptr<PerformanceCollector> collector)
    : m_collector(collector), m_ioc(std::make_shared<net::io_context>()) {}

WebSocketHandler::~WebSocketHandler() { stop(); }

bool WebSocketHandler::initialize(int port) {
  try {
    m_port = port;
    m_acceptor =
        std::make_shared<tcp::acceptor>(*m_ioc, tcp::endpoint(tcp::v4(), port));

    spdlog::info("WebSocket server initialized on port {}", port);
    return true;
  } catch (const std::exception& e) {
    spdlog::error("Failed to initialize WebSocket server: {}", e.what());
    return false;
  }
}

void WebSocketHandler::start() {
  if (m_running.load()) {
    return;
  }

  m_running.store(true);

  // Start the server thread
  m_serverThread = std::thread(&WebSocketHandler::runServer, this);

  // Start the broadcast thread
  m_broadcastThread = std::thread(&WebSocketHandler::broadcastLoop, this);

  spdlog::info("WebSocket server started on port {}", m_port);
}

void WebSocketHandler::stop() {
  if (!m_running.load()) {
    return;
  }

  m_running.store(false);

  // Close all sessions
  {
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    for (auto& session : m_sessions) {
      session->close();
    }
    m_sessions.clear();
  }

  // Stop the acceptor
  if (m_acceptor) {
    m_acceptor->close();
  }

  // Stop the io_context
  m_ioc->stop();

  // Join threads
  if (m_serverThread.joinable()) {
    m_serverThread.join();
  }

  if (m_broadcastThread.joinable()) {
    m_broadcastThread.join();
  }

  spdlog::info("WebSocket server stopped");
}

void WebSocketHandler::runServer() {
  acceptConnections();
  m_ioc->run();
}

void WebSocketHandler::acceptConnections() {
  if (!m_running.load()) {
    return;
  }

  m_acceptor->async_accept(
      [self = shared_from_this()](beast::error_code ec, tcp::socket socket) {
        if (!ec) {
          // Create a new session
          spdlog::debug("Creating new WebSocketSession");
          auto session = std::make_shared<WebSocketSession>(
              std::move(socket), self->m_collector, self);
          spdlog::debug("WebSocketSession created, about to call start()");
          session->start();
          spdlog::debug("WebSocketSession start() completed");
        } else {
          spdlog::error("Accept error: {}", ec.message());
        }

        // Continue accepting connections
        if (self->m_running.load()) {
          self->acceptConnections();
        }
      });
}

void WebSocketHandler::addSession(std::shared_ptr<WebSocketSession> session) {
  if (!session) {
    spdlog::error("Attempted to add null session");
    return;
  }

  // Use a simple approach with better error handling
  try {
    std::lock_guard<std::mutex> lock(m_sessionsMutex);

    // Check if session already exists
    auto it = std::find(m_sessions.begin(), m_sessions.end(), session);
    if (it != m_sessions.end()) {
      spdlog::warn("Session already exists in container");
      return;
    }

    // Add session safely
    m_sessions.push_back(session);
    spdlog::info("WebSocket client connected. Total clients: {}",
                 m_sessions.size());

  } catch (const std::exception& e) {
    spdlog::error("Exception in addSession: {}", e.what());
  }
}

void WebSocketHandler::removeSession(
    std::shared_ptr<WebSocketSession> session) {
  if (!session) {
    return;
  }

  try {
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    auto it = std::find(m_sessions.begin(), m_sessions.end(), session);
    if (it != m_sessions.end()) {
      m_sessions.erase(it);
      spdlog::info("WebSocket client disconnected. Total clients: {}",
                   m_sessions.size());
    }
  } catch (const std::exception& e) {
    spdlog::error("Exception in removeSession: {}", e.what());
  }
}

size_t WebSocketHandler::getConnectedClients() const {
  std::lock_guard<std::mutex> lock(m_sessionsMutex);
  return m_sessions.size();
}

void WebSocketHandler::broadcastUpdate(const json& data) {
  std::vector<std::shared_ptr<WebSocketSession>> sessionsCopy;

  // Copy sessions under lock
  {
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    sessionsCopy = m_sessions;
  }

  // Send to sessions without holding lock
  for (auto& session : sessionsCopy) {
    try {
      if (session) {
        session->send(data);
      }
    } catch (const std::exception& e) {
      spdlog::error("Failed to send update to client: {}", e.what());
      // Remove failed session
      removeSession(session);
    }
  }
}

void WebSocketHandler::broadcastLoop() {
  spdlog::debug("WebSocketHandler::broadcastLoop() started");
  while (m_running.load()) {
    try {
      spdlog::debug("Broadcast loop iteration starting");

      // Check if we have any connected clients before creating expensive
      // messages
      size_t clientCount = getConnectedClients();

      // Re-enabled broadcasting with production-ready session management

      if (clientCount == 0) {
        spdlog::debug("No connected clients, skipping broadcast");
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        continue;
      }

      spdlog::debug("About to create performance update message");
      // Create performance update message
      json updateMsg = {{"type", "performance_update"},
                        {"timestamp", utils::TimeUtils::getCurrentNanos()},
                        {"strategies", json::object()}};
      spdlog::debug("Performance update message created");

      // Get data for primary strategy if it exists
      if (m_collector && m_collector->getRegisteredStrategiesCount() > 0) {
        try {
          auto performanceData =
              m_collector->getLatestPerformance("primary_strategy");

          updateMsg["strategies"]["primary_strategy"] = {
              {"pnl", performanceData.pnl},
              {"position", performanceData.position},
              {"sharpe_ratio", performanceData.sharpeRatio},
              {"max_drawdown", performanceData.maxDrawdown},
              {"win_rate", performanceData.winRate},
              {"total_trades", performanceData.totalTrades},
              {"ml_accuracy", performanceData.mlAccuracy},
              {"prediction_time", performanceData.avgPredictionTime},
              {"ml_model_ready", performanceData.mlModelReady},
              {"ml_predictions", performanceData.mlPredictions},
              {"regime", static_cast<int>(performanceData.currentRegime)},
              {"regime_confidence", performanceData.regimeConfidence}};
        } catch (const std::exception& e) {
          spdlog::error("Error getting performance data: {}", e.what());
        }
      }

      spdlog::debug("About to call broadcastUpdate");
      broadcastUpdate(updateMsg);
      spdlog::debug("broadcastUpdate completed");

    } catch (const std::exception& e) {
      spdlog::error("Error in broadcast loop: {}", e.what());
    } catch (...) {
      spdlog::error("Unknown error in broadcast loop");
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }
}

json WebSocketHandler::formatPerformanceData(const PerformanceData& data) {
  return {{"timestamp", data.timestamp},
          {"pnl", data.pnl},
          {"position", data.position},
          {"total_return", data.totalReturn},
          {"sharpe_ratio", data.sharpeRatio},
          {"max_drawdown", data.maxDrawdown},
          {"win_rate", data.winRate},
          {"total_trades", data.totalTrades},
          {"ml_accuracy", data.mlAccuracy},
          {"prediction_time", data.avgPredictionTime},
          {"ml_model_ready", data.mlModelReady},
          {"ml_predictions", data.mlPredictions},
          {"regime", static_cast<int>(data.currentRegime)},
          {"regime_confidence", data.regimeConfidence}};
}

json WebSocketHandler::formatMarketData(const MarketData& data) {
  return {{"timestamp", data.timestamp}, {"symbol", data.symbol},
          {"mid_price", data.midPrice},  {"bid_price", data.bidPrice},
          {"ask_price", data.askPrice},  {"spread", data.spread}};
}

json WebSocketHandler::formatChartData(
    const std::vector<ChartDataPoint>& data) {
  json result = json::array();

  for (const auto& point : data) {
    result.push_back({{"timestamp", point.timestamp},
                      {"value", point.value},
                      {"label", point.label},
                      {"color", point.color}});
  }

  return result;
}

// ============================================================================
// HttpSession Implementation (Stub for now)
// ============================================================================

HttpSession::HttpSession(tcp::socket&& socket,
                         std::shared_ptr<RestAPIServer> server)
    : m_stream(std::move(socket)), m_server(server) {}

HttpSession::~HttpSession() = default;

void HttpSession::start() { doRead(); }

void HttpSession::doRead() {
  m_req = {};
  m_stream.expires_after(std::chrono::seconds(30));

  try {
    auto self = shared_from_this();
    http::async_read(m_stream, m_buffer, m_req,
                     beast::bind_front_handler(&HttpSession::onRead, self));
  } catch (const std::exception& e) {
    spdlog::error(
        "Exception in HttpSession::doRead() calling shared_from_this(): {}",
        e.what());
    return;
  }
}

void HttpSession::onRead(beast::error_code ec, std::size_t bytes_transferred) {
  boost::ignore_unused(bytes_transferred);

  if (ec == http::error::end_of_stream) {
    return;
  }

  if (ec) {
    spdlog::error("HTTP read error: {}", ec.message());
    return;
  }

  // Process the request
  auto response = m_server->handleRequest(std::move(m_req));
  sendResponse(std::move(response));
}

void HttpSession::sendResponse(http::response<http::string_body>&& res) {
  bool close = res.need_eof();

  auto sp = std::make_shared<http::response<http::string_body>>(std::move(res));

  http::async_write(m_stream, *sp,
                    [self = shared_from_this(), sp, close](
                        beast::error_code ec, std::size_t bytes_transferred) {
                      self->onWrite(ec, bytes_transferred, close);
                    });
}

void HttpSession::onWrite(beast::error_code ec, std::size_t bytes_transferred,
                          bool close) {
  boost::ignore_unused(bytes_transferred);

  if (ec) {
    spdlog::error("HTTP write error: {}", ec.message());
    return;
  }

  if (close) {
    m_stream.socket().shutdown(tcp::socket::shutdown_send, ec);
    return;
  }

  // Continue reading
  doRead();
}

// ============================================================================
// RestAPIServer Implementation (Simplified)
// ============================================================================

RestAPIServer::RestAPIServer(std::shared_ptr<PerformanceCollector> collector)
    : m_collector(collector), m_ioc(std::make_shared<net::io_context>()) {}

RestAPIServer::~RestAPIServer() { stop(); }

bool RestAPIServer::initialize(int port) {
  try {
    m_port = port;
    m_acceptor =
        std::make_shared<tcp::acceptor>(*m_ioc, tcp::endpoint(tcp::v4(), port));

    spdlog::info("REST API server initialized on port {}", port);
    return true;
  } catch (const std::exception& e) {
    spdlog::error("Failed to initialize REST API server: {}", e.what());
    return false;
  }
}

void RestAPIServer::start() {
  if (m_running.load()) {
    return;
  }

  m_running.store(true);
  m_serverThread = std::thread(&RestAPIServer::runServer, this);

  spdlog::info("REST API server started on port {}", m_port);
}

void RestAPIServer::stop() {
  if (!m_running.load()) {
    return;
  }

  m_running.store(false);

  if (m_acceptor) {
    m_acceptor->close();
  }

  m_ioc->stop();

  if (m_serverThread.joinable()) {
    m_serverThread.join();
  }

  spdlog::info("REST API server stopped");
}

void RestAPIServer::runServer() {
  acceptConnections();
  m_ioc->run();
}

void RestAPIServer::acceptConnections() {
  if (!m_running.load()) {
    return;
  }

  m_acceptor->async_accept(
      [self = shared_from_this()](beast::error_code ec, tcp::socket socket) {
        if (!ec) {
          auto session = std::make_shared<HttpSession>(std::move(socket), self);
          session->start();
        }

        if (self->m_running.load()) {
          self->acceptConnections();
        }
      });
}

http::response<http::string_body>
RestAPIServer::handleRequest(http::request<http::string_body>&& req) {
  // Simple routing
  auto target = std::string(req.target());

  if (target == "/api/v1/strategies") {
    return handleGetStrategies();
  } else if (target.starts_with("/api/v1/strategies/") &&
             target.ends_with("/performance")) {
    // Extract strategy ID
    auto start = target.find("/api/v1/strategies/") + 19;
    auto end = target.find("/performance");
    auto strategyId = target.substr(start, end - start);
    return handleGetPerformance(strategyId, "");
  } else if (target.starts_with("/")) {
    // Serve static files
    return handleStaticFile(target);
  }

  // Not found
  http::response<http::string_body> res{http::status::not_found, req.version()};
  res.set(http::field::server, "PinnacleMM-Visualization/1.0");
  res.set(http::field::content_type, "application/json");
  res.body() = R"({"error": "Not Found"})";
  res.prepare_payload();
  return res;
}

http::response<http::string_body> RestAPIServer::handleGetStrategies() {
  json strategies = json::array();
  strategies.push_back({{"id", "primary_strategy"},
                        {"name", "ML-Enhanced Market Maker"},
                        {"symbol", "BTC-USD"},
                        {"status", "running"},
                        {"ml_enabled", true}});

  auto response = createSuccessResponse(strategies);

  http::response<http::string_body> res{http::status::ok, 11};
  res.set(http::field::server, "PinnacleMM-Visualization/1.0");
  res.set(http::field::content_type, "application/json");
  res.body() = response.dump();
  res.prepare_payload();
  return res;
}

http::response<http::string_body>
RestAPIServer::handleGetPerformance(const std::string& strategyId,
                                    const std::string& query) {
  boost::ignore_unused(query);

  auto data = m_collector->getLatestPerformance(strategyId);
  json performance = {{"pnl", data.pnl},
                      {"position", data.position},
                      {"sharpe_ratio", data.sharpeRatio},
                      {"max_drawdown", data.maxDrawdown},
                      {"win_rate", data.winRate},
                      {"total_trades", data.totalTrades},
                      {"ml_accuracy", data.mlAccuracy},
                      {"prediction_time", data.avgPredictionTime}};

  auto response = createSuccessResponse(performance);

  http::response<http::string_body> res{http::status::ok, 11};
  res.set(http::field::server, "PinnacleMM-Visualization/1.0");
  res.set(http::field::content_type, "application/json");
  res.body() = response.dump();
  res.prepare_payload();
  return res;
}

http::response<http::string_body>
RestAPIServer::handleGetChartData(const std::string& strategyId,
                                  const std::string& metric,
                                  const std::string& query) {
  boost::ignore_unused(query);

  auto chartData =
      m_collector->getChartData(strategyId, metric, 3600000000000ULL); // 1 hour

  json data = json::array();
  for (const auto& point : chartData) {
    data.push_back({{"timestamp", point.timestamp}, {"value", point.value}});
  }

  auto response = createSuccessResponse(data);

  http::response<http::string_body> res{http::status::ok, 11};
  res.set(http::field::server, "PinnacleMM-Visualization/1.0");
  res.set(http::field::content_type, "application/json");
  res.body() = response.dump();
  res.prepare_payload();
  return res;
}

http::response<http::string_body> RestAPIServer::handleGetBacktestResults() {
  json backtests = json::array();

  auto response = createSuccessResponse(backtests);

  http::response<http::string_body> res{http::status::ok, 11};
  res.set(http::field::server, "PinnacleMM-Visualization/1.0");
  res.set(http::field::content_type, "application/json");
  res.body() = response.dump();
  res.prepare_payload();
  return res;
}

http::response<http::string_body>
RestAPIServer::handleStaticFile(const std::string& path) {
  // Simple static file serving
  std::string filePath =
      "visualization/static" + (path == "/" ? "/index.html" : path);

  std::ifstream file(filePath, std::ios::binary);
  if (!file) {
    http::response<http::string_body> res{http::status::not_found, 11};
    res.set(http::field::server, "PinnacleMM-Visualization/1.0");
    res.set(http::field::content_type, "text/plain");
    res.body() = "File not found";
    res.prepare_payload();
    return res;
  }

  std::string content((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());

  http::response<http::string_body> res{http::status::ok, 11};
  res.set(http::field::server, "PinnacleMM-Visualization/1.0");
  res.set(http::field::content_type, getContentType(path));
  res.body() = content;
  res.prepare_payload();
  return res;
}

json RestAPIServer::createErrorResponse(const std::string& error, int code) {
  return {{"success", false},
          {"error", error},
          {"code", code},
          {"timestamp", utils::TimeUtils::getCurrentNanos()}};
}

json RestAPIServer::createSuccessResponse(const json& data) {
  return {{"success", true},
          {"data", data},
          {"timestamp", utils::TimeUtils::getCurrentNanos()}};
}

std::unordered_map<std::string, std::string>
RestAPIServer::parseQueryString(const std::string& query) {
  std::unordered_map<std::string, std::string> params;
  // Simple query string parsing (not implemented for brevity)
  boost::ignore_unused(query);
  return params;
}

std::string RestAPIServer::extractPath(const std::string& target) {
  auto pos = target.find('?');
  return pos != std::string::npos ? target.substr(0, pos) : target;
}

std::string RestAPIServer::getContentType(const std::string& path) {
  if (path.ends_with(".html"))
    return "text/html";
  if (path.ends_with(".css"))
    return "text/css";
  if (path.ends_with(".js"))
    return "application/javascript";
  if (path.ends_with(".json"))
    return "application/json";
  return "text/plain";
}

// ============================================================================
// VisualizationServer Implementation (Simplified)
// ============================================================================

VisualizationServer::VisualizationServer(const Config& config)
    : m_config(config), m_collector(std::make_shared<PerformanceCollector>()) {

  m_collector->setMaxHistorySize(config.maxHistorySize);
}

VisualizationServer::~VisualizationServer() { stop(); }

bool VisualizationServer::initialize() {
  try {
    // Initialize WebSocket handler
    m_webSocketHandler = std::make_shared<WebSocketHandler>(m_collector);
    if (!m_webSocketHandler->initialize(m_config.webSocketPort)) {
      spdlog::error("Failed to initialize WebSocket handler");
      return false;
    }

    // Initialize REST API server
    m_restApiServer = std::make_shared<RestAPIServer>(m_collector);
    if (!m_restApiServer->initialize(m_config.restApiPort)) {
      spdlog::error("Failed to initialize REST API server");
      return false;
    }

    spdlog::info("Visualization server initialized successfully");
    spdlog::info("WebSocket server on port {}", m_config.webSocketPort);
    spdlog::info("REST API server on port {}", m_config.restApiPort);

    return true;

  } catch (const std::exception& e) {
    spdlog::error("Failed to initialize visualization server: {}", e.what());
    return false;
  }
}

bool VisualizationServer::start() {
  if (m_running.load()) {
    return true;
  }

  try {
    // Start data collection
    if (m_config.enableRealTimeUpdates) {
      m_collector->startCollection(m_config.dataCollectionIntervalMs);
    }

    // Start WebSocket server
    m_webSocketHandler->start();

    // Start REST API server
    m_restApiServer->start();

    m_running.store(true);

    spdlog::info("Visualization server started successfully");
    return true;

  } catch (const std::exception& e) {
    spdlog::error("Failed to start visualization server: {}", e.what());
    return false;
  }
}

void VisualizationServer::stop() {
  if (!m_running.load()) {
    return;
  }

  m_running.store(false);

  // Stop components in reverse order
  if (m_restApiServer) {
    m_restApiServer->stop();
  }

  if (m_webSocketHandler) {
    m_webSocketHandler->stop();
  }

  if (m_collector) {
    m_collector->stopCollection();
  }

  spdlog::info("Visualization server stopped");
}

void VisualizationServer::registerStrategy(
    const std::string& strategyId,
    std::shared_ptr<strategy::MLEnhancedMarketMaker> strategy) {
  if (m_collector) {
    m_collector->registerStrategy(strategyId, strategy);
  }
}

void VisualizationServer::unregisterStrategy(const std::string& strategyId) {
  if (m_collector) {
    m_collector->unregisterStrategy(strategyId);
  }
}

void VisualizationServer::updateMarketData(const std::string& symbol,
                                           const MarketData& data) {
  if (m_collector) {
    m_collector->updateMarketData(symbol, data);
  }
}

size_t VisualizationServer::getConnectedClients() const {
  if (m_webSocketHandler) {
    return m_webSocketHandler->getConnectedClients();
  }
  return 0;
}

std::string VisualizationServer::getStatus() const {
  json status = {
      {"running", m_running.load()},
      {"connected_clients", getConnectedClients()},
      {"registered_strategies",
       m_collector ? m_collector->getRegisteredStrategiesCount() : 0},
      {"websocket_port", m_config.webSocketPort},
      {"rest_api_port", m_config.restApiPort},
      {"data_collection_interval_ms", m_config.dataCollectionIntervalMs},
      {"timestamp", utils::TimeUtils::getCurrentNanos()}};

  return status.dump(2);
}

void VisualizationServer::addBacktestResults(
    const std::string& backtestId,
    const backtesting::TradingStatistics& results) {
  std::lock_guard<std::mutex> lock(m_backtestMutex);

  BacktestData data;
  data.results = results;
  data.progress = 100.0;
  data.timestamp = utils::TimeUtils::getCurrentNanos();
  data.status = "completed";

  m_backtestResults[backtestId] = data;

  spdlog::info("Added backtest results for: {}", backtestId);
}

void VisualizationServer::updateBacktestProgress(const std::string& backtestId,
                                                 double progress) {
  std::lock_guard<std::mutex> lock(m_backtestMutex);

  auto it = m_backtestResults.find(backtestId);
  if (it != m_backtestResults.end()) {
    it->second.progress = progress;
    it->second.timestamp = utils::TimeUtils::getCurrentNanos();

    if (progress >= 100.0) {
      it->second.status = "completed";
    } else {
      it->second.status = "running";
    }
  }
}

} // namespace visualization
} // namespace pinnacle
