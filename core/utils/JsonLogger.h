#pragma once

#include "../../exchange/simulator/MarketDataFeed.h"
#include <fstream>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>

namespace pinnacle {
namespace utils {

/**
 * @class JsonLogger
 * @brief Structured JSON logging utility for market data and trading events
 *
 * This class provides structured JSON logging capabilities for market data,
 * trading events, and strategy performance metrics. It operates alongside
 * the existing console logging without interfering with the user experience.
 */
class JsonLogger {
public:
  /**
   * @brief Constructor
   *
   * @param filePath Path to the JSON log file
   * @param enableLogging Whether to enable JSON logging (default: false)
   */
  explicit JsonLogger(const std::string& filePath = "pinnaclemm_data.jsonl",
                      bool enableLogging = false);

  /**
   * @brief Destructor
   */
  ~JsonLogger();

  /**
   * @brief Enable or disable JSON logging
   *
   * @param enabled Whether to enable logging
   */
  void setEnabled(bool enabled);

  /**
   * @brief Check if JSON logging is enabled
   *
   * @return true if logging is enabled, false otherwise
   */
  bool isEnabled() const;

  /**
   * @brief Log a market update (ticker data)
   *
   * @param update Market update to log
   */
  void logMarketUpdate(const exchange::MarketUpdate& update);

  /**
   * @brief Log an order book update
   *
   * @param update Order book update to log
   */
  void logOrderBookUpdate(const exchange::OrderBookUpdate& update);

  /**
   * @brief Log a trading event
   *
   * @param eventType Type of trading event (e.g., "order_placed",
   * "order_filled")
   * @param symbol Trading symbol
   * @param data Additional event data as JSON
   */
  void logTradingEvent(const std::string& eventType, const std::string& symbol,
                       const nlohmann::json& data);

  /**
   * @brief Log strategy performance metrics
   *
   * @param strategyName Name of the strategy
   * @param symbol Trading symbol
   * @param metrics Performance metrics as JSON
   */
  void logStrategyMetrics(const std::string& strategyName,
                          const std::string& symbol,
                          const nlohmann::json& metrics);

  /**
   * @brief Log WebSocket connection events
   *
   * @param eventType Connection event type (e.g., "connected", "disconnected",
   * "error")
   * @param exchange Exchange name
   * @param message Additional message or error details
   */
  void logConnectionEvent(const std::string& eventType,
                          const std::string& exchange,
                          const std::string& message = "");

  /**
   * @brief Flush all pending writes to disk
   */
  void flush();

private:
  std::string m_filePath;
  std::atomic<bool> m_enabled{false};
  std::unique_ptr<std::ofstream> m_fileStream;
  mutable std::mutex m_mutex;

  /**
   * @brief Write a JSON log entry
   *
   * @param entry JSON entry to write
   */
  void writeLogEntry(const nlohmann::json& entry);

  /**
   * @brief Get current timestamp in ISO 8601 format
   *
   * @return Current timestamp string
   */
  std::string getCurrentTimestamp() const;

  /**
   * @brief Initialize the log file
   *
   * @return true if initialization was successful, false otherwise
   */
  bool initializeLogFile();
};

} // namespace utils
} // namespace pinnacle
