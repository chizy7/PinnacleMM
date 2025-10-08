#pragma once

#include "../core/utils/DomainTypes.h"
#include "../core/utils/TimeUtils.h"
#include "../strategies/backtesting/BacktestEngine.h"
#include "../strategies/basic/MLEnhancedMarketMaker.h"

#include <atomic>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <queue>
#include <set>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace pinnacle {
namespace visualization {

using json = nlohmann::json;
using tcp = boost::asio::ip::tcp;
namespace websocket = boost::beast::websocket;
namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;

// Simple data structures for visualization
struct PerformanceData {
  uint64_t timestamp{0};
  double pnl{0.0};
  double position{0.0};
  double totalReturn{0.0};
  double sharpeRatio{0.0};
  double maxDrawdown{0.0};
  double winRate{0.0};
  int totalTrades{0};
  double mlAccuracy{0.0};
  double avgPredictionTime{0.0};
  bool mlModelReady{false};
  int mlPredictions{0};
  int currentRegime{0};
  double regimeConfidence{0.0};
};

struct MarketData {
  uint64_t timestamp{0};
  std::string symbol;
  double price{0.0};
  double volume{0.0};
  double bid{0.0};
  double ask{0.0};
  double midPrice{0.0};
  double bidPrice{0.0};
  double askPrice{0.0};
  double spread{0.0};
};

struct ChartDataPoint {
  uint64_t timestamp{0};
  double value{0.0};
  std::string label;
  std::string color;
};

// Simple performance collector
class PerformanceCollector {
public:
  PerformanceCollector();
  ~PerformanceCollector();

  void registerStrategy(const std::string& strategyId,
                        std::shared_ptr<void> strategy);
  void unregisterStrategy(const std::string& strategyId);
  void startCollection(uint64_t intervalMs = 1000);
  void stopCollection();

  PerformanceData getLatestPerformance(const std::string& strategyId) const;
  std::vector<ChartDataPoint> getChartData(const std::string& strategyId,
                                           const std::string& metric,
                                           uint64_t timeRange) const;
  size_t getRegisteredStrategiesCount() const;
  void setMaxHistorySize(size_t maxSize);
  void updateMarketData(const std::string& symbol, const MarketData& data);

private:
  mutable std::mutex m_mutex;
  std::unordered_map<std::string, PerformanceData> m_performanceData;
  std::atomic<bool> m_collecting{false};
  std::thread m_collectionThread;
  size_t m_maxHistorySize{10000};
  std::unordered_map<std::string, MarketData> m_marketData;
};

// Forward declarations
class WebSocketSession;
class WebSocketHandler;

/**
 * @class WebSocketSession
 * @brief Individual WebSocket session using Boost.Beast
 */
class WebSocketSession : public std::enable_shared_from_this<WebSocketSession> {
public:
  explicit WebSocketSession(tcp::socket&& socket,
                            std::shared_ptr<PerformanceCollector> collector,
                            std::shared_ptr<WebSocketHandler> handler);
  ~WebSocketSession();

  void start();
  bool send(const json& data);
  void close();

private:
  websocket::stream<beast::tcp_stream> m_ws;
  std::shared_ptr<PerformanceCollector> m_collector;
  std::shared_ptr<WebSocketHandler> m_handler;
  beast::flat_buffer m_buffer;
  std::mutex m_writeMutex;
  std::queue<std::string> m_writeQueue;
  bool m_writing{false};

  void onAccept(beast::error_code ec);
  void doRead();
  void onRead(beast::error_code ec, std::size_t bytes_transferred);
  void doWrite();
  void onWrite(beast::error_code ec, std::size_t bytes_transferred);

  // Message handlers
  void handleMessage(const std::string& message);
  void handleSubscribe(const json& request);
  void handleGetHistory(const json& request);
  void handleGetStrategies(const json& request);
};

/**
 * @class WebSocketHandler
 * @brief WebSocket server handler using Boost.Beast
 */
class WebSocketHandler : public std::enable_shared_from_this<WebSocketHandler> {
public:
  explicit WebSocketHandler(std::shared_ptr<PerformanceCollector> collector);
  ~WebSocketHandler();

  bool initialize(int port = 8080);
  void start();
  void stop();

  // Client management
  void broadcastUpdate(const json& data);
  void addSession(std::shared_ptr<WebSocketSession> session);
  void removeSession(std::shared_ptr<WebSocketSession> session);
  size_t getConnectedClients() const;

private:
  std::shared_ptr<PerformanceCollector> m_collector;
  std::shared_ptr<net::io_context> m_ioc;
  std::shared_ptr<tcp::acceptor> m_acceptor;
  std::thread m_serverThread;
  std::atomic<bool> m_running{false};
  int m_port{8080};

  // Session management with better safety
  std::vector<std::shared_ptr<WebSocketSession>> m_sessions;
  mutable std::mutex m_sessionsMutex;

  // Server operations
  void acceptConnections();
  void runServer();
  void startBroadcastLoop();
  void broadcastLoop();
  std::thread m_broadcastThread;

  // Data formatting
  json formatPerformanceData(const PerformanceData& data);
  json formatMarketData(const MarketData& data);
  json formatChartData(const std::vector<ChartDataPoint>& data);
};

// Forward declaration for HTTP session
class HttpSession;

/**
 * @class RestAPIServer
 * @brief REST API server for data access and configuration using Boost.Beast
 */
class RestAPIServer : public std::enable_shared_from_this<RestAPIServer> {
public:
  explicit RestAPIServer(std::shared_ptr<PerformanceCollector> collector);
  ~RestAPIServer();

  bool initialize(int port = 8081);
  void start();
  void stop();

private:
  std::shared_ptr<PerformanceCollector> m_collector;
  std::shared_ptr<net::io_context> m_ioc;
  std::shared_ptr<tcp::acceptor> m_acceptor;
  std::thread m_serverThread;
  std::atomic<bool> m_running{false};
  int m_port{8081};

  void acceptConnections();
  void runServer();

  // Route handlers (to be called by HttpSession)
  http::response<http::string_body>
  handleRequest(http::request<http::string_body>&& req);
  http::response<http::string_body> handleGetStrategies();
  http::response<http::string_body>
  handleGetPerformance(const std::string& strategyId, const std::string& query);
  http::response<http::string_body>
  handleGetChartData(const std::string& strategyId, const std::string& metric,
                     const std::string& query);
  http::response<http::string_body> handleGetBacktestResults();
  http::response<http::string_body> handleStaticFile(const std::string& path);

  // Utility methods
  json createErrorResponse(const std::string& error, int code = 400);
  json createSuccessResponse(const json& data);
  std::unordered_map<std::string, std::string>
  parseQueryString(const std::string& query);
  std::string extractPath(const std::string& target);
  std::string getContentType(const std::string& path);

  friend class HttpSession;
};

/**
 * @class HttpSession
 * @brief Individual HTTP session using Boost.Beast
 */
class HttpSession : public std::enable_shared_from_this<HttpSession> {
public:
  explicit HttpSession(tcp::socket&& socket,
                       std::shared_ptr<RestAPIServer> server);
  ~HttpSession();

  void start();

private:
  beast::tcp_stream m_stream;
  std::shared_ptr<RestAPIServer> m_server;
  beast::flat_buffer m_buffer;
  http::request<http::string_body> m_req;

  void doRead();
  void onRead(beast::error_code ec, std::size_t bytes_transferred);
  void sendResponse(http::response<http::string_body>&& res);
  void onWrite(beast::error_code ec, std::size_t bytes_transferred, bool close);
};

/**
 * @class VisualizationServer
 * @brief Main visualization server coordinating all components
 */
class VisualizationServer {
public:
  struct Config {
    int webSocketPort{8080};
    int restApiPort{8081};
    std::string staticFilesPath{"visualization/static"};
    uint64_t dataCollectionIntervalMs{1000};
    size_t maxHistorySize{10000};
    bool enableRealTimeUpdates{true};
    bool enableBacktestVisualization{true};
    bool enablePerformanceAnalytics{true};
    bool enableMarketRegimeVisualization{true};
    bool enableMLMetricsVisualization{true};
  };

  explicit VisualizationServer(const Config& config);
  ~VisualizationServer();

  // Lifecycle
  bool initialize();
  bool start();
  void stop();

  // Strategy management
  void
  registerStrategy(const std::string& strategyId,
                   std::shared_ptr<strategy::MLEnhancedMarketMaker> strategy);
  void unregisterStrategy(const std::string& strategyId);

  // Market data updates
  void updateMarketData(const std::string& symbol, const MarketData& data);

  // Backtest integration
  void addBacktestResults(const std::string& backtestId,
                          const backtesting::TradingStatistics& results);
  void updateBacktestProgress(const std::string& backtestId, double progress);

  // Status
  bool isRunning() const { return m_running.load(); }
  size_t getConnectedClients() const;
  std::string getStatus() const;

private:
  Config m_config;
  std::atomic<bool> m_running{false};

  std::shared_ptr<PerformanceCollector> m_collector;
  std::shared_ptr<WebSocketHandler> m_webSocketHandler;
  std::shared_ptr<RestAPIServer> m_restApiServer;

  // Backtest data storage
  struct BacktestData {
    backtesting::TradingStatistics results;
    double progress{0.0};
    uint64_t timestamp{0};
    std::string status{"running"};
  };

  std::unordered_map<std::string, BacktestData> m_backtestResults;
  mutable std::mutex m_backtestMutex;

  // Static file serving
  void setupStaticFileServer();
  std::string getContentType(const std::string& filename);
  std::string loadStaticFile(const std::string& path);

  // Logging and monitoring
  void logServerEvent(const std::string& event,
                      const std::string& details = "");
};

} // namespace visualization
} // namespace pinnacle
