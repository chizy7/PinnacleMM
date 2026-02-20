#include "AlertManager.h"
#include "../utils/AuditLogger.h"

#include <algorithm>
#include <spdlog/spdlog.h>

namespace pinnacle {
namespace risk {

using pinnacle::utils::AuditLogger;

AlertManager& AlertManager::getInstance() {
  static AlertManager instance;
  return instance;
}

void AlertManager::initialize(const AlertConfig& config) {
  m_config = config;

  // Clear previous state so re-initialization starts clean
  {
    std::lock_guard<std::mutex> lock(m_alertsMutex);
    m_alerts.clear();
    m_lastAlertTime.clear();
  }
  {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_callbacks.clear();
  }

  spdlog::info("AlertManager initialized: minIntervalMs={}, maxHistory={}, "
               "warningPct={:.1f}, criticalPct={:.1f}",
               m_config.minAlertIntervalMs, m_config.maxAlertHistory,
               m_config.warningThresholdPct, m_config.criticalThresholdPct);

  AUDIT_SYSTEM_EVENT("AlertManager initialized", true);
}

uint64_t AlertManager::raiseAlert(AlertType type, AlertSeverity severity,
                                  const std::string& message,
                                  const std::string& source,
                                  const nlohmann::json& metadata) {
  Alert alert;
  alert.id = m_nextAlertId.fetch_add(1);
  alert.type = type;
  alert.severity = severity;
  alert.message = message;
  alert.source = source;
  alert.metadata = metadata;
  alert.timestamp = utils::TimeUtils::getCurrentMillis();

  // Throttle check + store under a single lock to avoid UB on the
  // unordered_map and prevent double-locking
  {
    std::lock_guard<std::mutex> lock(m_alertsMutex);

    // Check throttling
    auto it = m_lastAlertTime.find(static_cast<int>(type));
    if (it != m_lastAlertTime.end()) {
      uint64_t elapsed = alert.timestamp - it->second;
      if (elapsed < m_config.minAlertIntervalMs) {
        return 0; // Throttled
      }
    }

    m_alerts.push_back(alert);
    m_lastAlertTime[static_cast<int>(type)] = alert.timestamp;
    pruneHistory();
  }

  // Log based on severity
  switch (severity) {
  case AlertSeverity::INFO:
    spdlog::info("[ALERT] [{}] [{}] {}", typeToString(type),
                 severityToString(severity), message);
    break;
  case AlertSeverity::WARNING:
    spdlog::warn("[ALERT] [{}] [{}] {}", typeToString(type),
                 severityToString(severity), message);
    break;
  case AlertSeverity::CRITICAL:
    spdlog::error("[ALERT] [{}] [{}] {}", typeToString(type),
                  severityToString(severity), message);
    break;
  case AlertSeverity::EMERGENCY:
    spdlog::critical("[ALERT] [{}] [{}] {}", typeToString(type),
                     severityToString(severity), message);
    break;
  }

  // Audit log for critical and emergency alerts
  if (severity == AlertSeverity::CRITICAL ||
      severity == AlertSeverity::EMERGENCY) {
    AUDIT_SYSTEM_EVENT(
        "Risk alert: " + severityToString(severity) + " - " + message, false);
  }

  // Deliver to registered callbacks
  deliverAlert(alert);

  return alert.id;
}

bool AlertManager::acknowledgeAlert(uint64_t alertId) {
  std::lock_guard<std::mutex> lock(m_alertsMutex);

  for (auto& alert : m_alerts) {
    if (alert.id == alertId) {
      if (alert.acknowledged) {
        return false; // Already acknowledged
      }
      alert.acknowledged = true;
      alert.acknowledgedAt = utils::TimeUtils::getCurrentMillis();

      spdlog::info("Alert {} acknowledged: [{}] {}", alertId,
                   typeToString(alert.type), alert.message);
      return true;
    }
  }

  return false; // Alert not found
}

std::vector<Alert> AlertManager::getRecentAlerts(size_t count) const {
  std::lock_guard<std::mutex> lock(m_alertsMutex);

  std::vector<Alert> result;
  size_t start = (m_alerts.size() > count) ? m_alerts.size() - count : 0;
  for (size_t i = start; i < m_alerts.size(); ++i) {
    result.push_back(m_alerts[i]);
  }

  // Return in reverse chronological order (most recent first)
  std::reverse(result.begin(), result.end());
  return result;
}

std::vector<Alert> AlertManager::getUnacknowledgedAlerts() const {
  std::lock_guard<std::mutex> lock(m_alertsMutex);

  std::vector<Alert> result;
  for (const auto& alert : m_alerts) {
    if (!alert.acknowledged) {
      result.push_back(alert);
    }
  }

  // Return in reverse chronological order
  std::reverse(result.begin(), result.end());
  return result;
}

std::vector<Alert> AlertManager::getAlertsByType(AlertType type,
                                                 size_t count) const {
  std::lock_guard<std::mutex> lock(m_alertsMutex);

  std::vector<Alert> result;
  // Iterate in reverse to get the most recent first
  for (auto it = m_alerts.rbegin(); it != m_alerts.rend(); ++it) {
    if (it->type == type) {
      result.push_back(*it);
      if (result.size() >= count) {
        break;
      }
    }
  }

  return result;
}

std::vector<Alert> AlertManager::getAlertsBySeverity(AlertSeverity severity,
                                                     size_t count) const {
  std::lock_guard<std::mutex> lock(m_alertsMutex);

  std::vector<Alert> result;
  // Iterate in reverse to get the most recent first
  for (auto it = m_alerts.rbegin(); it != m_alerts.rend(); ++it) {
    if (it->severity == severity) {
      result.push_back(*it);
      if (result.size() >= count) {
        break;
      }
    }
  }

  return result;
}

void AlertManager::registerCallback(AlertCallback callback) {
  std::lock_guard<std::mutex> lock(m_callbackMutex);
  m_callbacks.push_back(std::move(callback));
}

size_t AlertManager::getTotalAlertCount() const {
  std::lock_guard<std::mutex> lock(m_alertsMutex);
  return m_alerts.size();
}

size_t AlertManager::getUnacknowledgedCount() const {
  std::lock_guard<std::mutex> lock(m_alertsMutex);

  size_t count = 0;
  for (const auto& alert : m_alerts) {
    if (!alert.acknowledged) {
      ++count;
    }
  }
  return count;
}

nlohmann::json AlertManager::alertToJson(const Alert& alert) const {
  return {{"id", alert.id},
          {"type", typeToString(alert.type)},
          {"severity", severityToString(alert.severity)},
          {"message", alert.message},
          {"source", alert.source},
          {"metadata", alert.metadata},
          {"timestamp", alert.timestamp},
          {"acknowledged", alert.acknowledged},
          {"acknowledged_at", alert.acknowledgedAt}};
}

nlohmann::json AlertManager::toJson() const {
  std::lock_guard<std::mutex> lock(m_alertsMutex);

  nlohmann::json alertsArray = nlohmann::json::array();

  // Serialize the most recent alerts (up to 50)
  size_t start = (m_alerts.size() > 50) ? m_alerts.size() - 50 : 0;
  for (size_t i = start; i < m_alerts.size(); ++i) {
    alertsArray.push_back(alertToJson(m_alerts[i]));
  }

  // Count unacknowledged
  size_t unackedCount = 0;
  for (const auto& alert : m_alerts) {
    if (!alert.acknowledged) {
      ++unackedCount;
    }
  }

  return {{"total_alerts", m_alerts.size()},
          {"unacknowledged_count", unackedCount},
          {"recent_alerts", alertsArray}};
}

std::string AlertManager::typeToString(AlertType type) {
  switch (type) {
  case AlertType::POSITION_WARNING:
    return "POSITION_WARNING";
  case AlertType::POSITION_BREACH:
    return "POSITION_BREACH";
  case AlertType::DRAWDOWN_WARNING:
    return "DRAWDOWN_WARNING";
  case AlertType::DRAWDOWN_BREACH:
    return "DRAWDOWN_BREACH";
  case AlertType::DAILY_LOSS_WARNING:
    return "DAILY_LOSS_WARNING";
  case AlertType::DAILY_LOSS_BREACH:
    return "DAILY_LOSS_BREACH";
  case AlertType::VAR_BREACH:
    return "VAR_BREACH";
  case AlertType::CIRCUIT_BREAKER_OPEN:
    return "CIRCUIT_BREAKER_OPEN";
  case AlertType::CIRCUIT_BREAKER_HALF_OPEN:
    return "CIRCUIT_BREAKER_HALF_OPEN";
  case AlertType::CIRCUIT_BREAKER_CLOSED:
    return "CIRCUIT_BREAKER_CLOSED";
  case AlertType::SPREAD_ANOMALY:
    return "SPREAD_ANOMALY";
  case AlertType::VOLUME_ANOMALY:
    return "VOLUME_ANOMALY";
  case AlertType::LATENCY_WARNING:
    return "LATENCY_WARNING";
  case AlertType::CONNECTIVITY_ISSUE:
    return "CONNECTIVITY_ISSUE";
  case AlertType::REGIME_CHANGE:
    return "REGIME_CHANGE";
  case AlertType::SYSTEM_ERROR:
    return "SYSTEM_ERROR";
  default:
    return "UNKNOWN";
  }
}

std::string AlertManager::severityToString(AlertSeverity severity) {
  switch (severity) {
  case AlertSeverity::INFO:
    return "INFO";
  case AlertSeverity::WARNING:
    return "WARNING";
  case AlertSeverity::CRITICAL:
    return "CRITICAL";
  case AlertSeverity::EMERGENCY:
    return "EMERGENCY";
  default:
    return "UNKNOWN";
  }
}

bool AlertManager::isThrottled(AlertType type) const {
  // Must hold m_alertsMutex because m_lastAlertTime is a std::unordered_map
  // and concurrent read/write is undefined behavior (rehashing can corrupt
  // iterators).
  std::lock_guard<std::mutex> lock(m_alertsMutex);

  auto it = m_lastAlertTime.find(static_cast<int>(type));
  if (it == m_lastAlertTime.end()) {
    return false;
  }

  uint64_t now = utils::TimeUtils::getCurrentMillis();
  uint64_t elapsed = now - it->second;
  return elapsed < m_config.minAlertIntervalMs;
}

void AlertManager::deliverAlert(const Alert& alert) {
  std::lock_guard<std::mutex> lock(m_callbackMutex);

  for (const auto& callback : m_callbacks) {
    try {
      callback(alert);
    } catch (const std::exception& e) {
      spdlog::error("Alert callback exception for alert {}: {}", alert.id,
                    e.what());
    } catch (...) {
      spdlog::error("Alert callback unknown exception for alert {}", alert.id);
    }
  }
}

void AlertManager::pruneHistory() {
  // Caller must hold m_alertsMutex
  while (m_alerts.size() > m_config.maxAlertHistory) {
    m_alerts.pop_front();
  }
}

} // namespace risk
} // namespace pinnacle
