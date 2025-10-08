#include "JsonLogger.h"
#include "TimeUtils.h"
#include <chrono>
#include <iomanip>
#include <spdlog/spdlog.h>
#include <sstream>

namespace pinnacle {
namespace utils {

JsonLogger::JsonLogger(const std::string& filePath, bool enableLogging)
    : m_filePath(filePath), m_enabled(enableLogging) {
  if (m_enabled) {
    initializeLogFile();
  }
}

JsonLogger::~JsonLogger() {
  if (m_fileStream && m_fileStream->is_open()) {
    flush();
    m_fileStream->close();
  }
}

void JsonLogger::setEnabled(bool enabled) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (enabled && !m_enabled) {
    // Enabling logging
    m_enabled = true;
    if (!initializeLogFile()) {
      m_enabled = false;
      spdlog::error("Failed to initialize JSON log file: {}", m_filePath);
    } else {
      spdlog::info("JSON logging enabled, writing to: {}", m_filePath);
    }
  } else if (!enabled && m_enabled) {
    // Disabling logging
    m_enabled = false;
    if (m_fileStream && m_fileStream->is_open()) {
      flush();
      m_fileStream->close();
    }
    spdlog::info("JSON logging disabled");
  }
}

bool JsonLogger::isEnabled() const { return m_enabled.load(); }

void JsonLogger::logMarketUpdate(const exchange::MarketUpdate& update) {
  if (!m_enabled)
    return;

  nlohmann::json entry = {{"timestamp", getCurrentTimestamp()},
                          {"type", "market_update"},
                          {"symbol", update.symbol},
                          {"price", update.price},
                          {"volume", update.volume},
                          {"is_buy", update.isBuy},
                          {"bid_price", update.bidPrice},
                          {"ask_price", update.askPrice},
                          {"event_timestamp", update.timestamp}};

  writeLogEntry(entry);
}

void JsonLogger::logOrderBookUpdate(const exchange::OrderBookUpdate& update) {
  if (!m_enabled)
    return;

  nlohmann::json bidsArray = nlohmann::json::array();
  for (const auto& bid : update.bids) {
    bidsArray.push_back({{"price", bid.first}, {"quantity", bid.second}});
  }

  nlohmann::json asksArray = nlohmann::json::array();
  for (const auto& ask : update.asks) {
    asksArray.push_back({{"price", ask.first}, {"quantity", ask.second}});
  }

  nlohmann::json entry = {{"timestamp", getCurrentTimestamp()},
                          {"type", "order_book_update"},
                          {"symbol", update.symbol},
                          {"bids", bidsArray},
                          {"asks", asksArray},
                          {"event_timestamp", update.timestamp}};

  writeLogEntry(entry);
}

void JsonLogger::logTradingEvent(const std::string& eventType,
                                 const std::string& symbol,
                                 const nlohmann::json& data) {
  if (!m_enabled)
    return;

  nlohmann::json entry = {{"timestamp", getCurrentTimestamp()},
                          {"type", "trading_event"},
                          {"event_type", eventType},
                          {"symbol", symbol},
                          {"data", data}};

  writeLogEntry(entry);
}

void JsonLogger::logStrategyMetrics(const std::string& strategyName,
                                    const std::string& symbol,
                                    const nlohmann::json& metrics) {
  if (!m_enabled)
    return;

  nlohmann::json entry = {{"timestamp", getCurrentTimestamp()},
                          {"type", "strategy_metrics"},
                          {"strategy_name", strategyName},
                          {"symbol", symbol},
                          {"metrics", metrics}};

  writeLogEntry(entry);
}

void JsonLogger::logConnectionEvent(const std::string& eventType,
                                    const std::string& exchange,
                                    const std::string& message) {
  if (!m_enabled)
    return;

  nlohmann::json entry = {{"timestamp", getCurrentTimestamp()},
                          {"type", "connection_event"},
                          {"event_type", eventType},
                          {"exchange", exchange},
                          {"message", message}};

  writeLogEntry(entry);
}

void JsonLogger::flush() {
  if (m_fileStream && m_fileStream->is_open()) {
    m_fileStream->flush();
  }
}

void JsonLogger::writeLogEntry(const nlohmann::json& entry) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_enabled || !m_fileStream || !m_fileStream->is_open()) {
    return;
  }

  try {
    *m_fileStream << entry.dump() << std::endl;
  } catch (const std::exception& e) {
    spdlog::error("Failed to write JSON log entry: {}", e.what());
  }
}

std::string JsonLogger::getCurrentTimestamp() const {
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()) %
            1000;

  std::stringstream ss;
  ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S");
  ss << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
  return ss.str();
}

bool JsonLogger::initializeLogFile() {
  try {
    m_fileStream = std::make_unique<std::ofstream>(m_filePath, std::ios::app);
    if (!m_fileStream->is_open()) {
      spdlog::error("Failed to open JSON log file: {}", m_filePath);
      return false;
    }

    // Write a session start marker
    nlohmann::json sessionStart = {{"timestamp", getCurrentTimestamp()},
                                   {"type", "session_start"},
                                   {"version", "1.0.0"},
                                   {"format", "jsonl"}};

    *m_fileStream << sessionStart.dump() << std::endl;
    return true;
  } catch (const std::exception& e) {
    spdlog::error("Failed to initialize JSON log file: {}", e.what());
    return false;
  }
}

} // namespace utils
} // namespace pinnacle
