#pragma once

#include "../utils/TimeUtils.h"
#include "RiskConfig.h"

#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace pinnacle {
namespace risk {

enum class AlertType {
  POSITION_WARNING,
  POSITION_BREACH,
  DRAWDOWN_WARNING,
  DRAWDOWN_BREACH,
  DAILY_LOSS_WARNING,
  DAILY_LOSS_BREACH,
  VAR_BREACH,
  CIRCUIT_BREAKER_OPEN,
  CIRCUIT_BREAKER_HALF_OPEN,
  CIRCUIT_BREAKER_CLOSED,
  SPREAD_ANOMALY,
  VOLUME_ANOMALY,
  LATENCY_WARNING,
  CONNECTIVITY_ISSUE,
  REGIME_CHANGE,
  SYSTEM_ERROR
};

enum class AlertSeverity { INFO, WARNING, CRITICAL, EMERGENCY };

struct Alert {
  uint64_t id{0};
  AlertType type;
  AlertSeverity severity;
  std::string message;
  std::string source;
  nlohmann::json metadata;
  uint64_t timestamp{0};
  bool acknowledged{false};
  uint64_t acknowledgedAt{0};
};

class AlertManager {
public:
  static AlertManager& getInstance();

  void initialize(const AlertConfig& config);

  // Raise an alert
  uint64_t raiseAlert(AlertType type, AlertSeverity severity,
                      const std::string& message,
                      const std::string& source = "",
                      const nlohmann::json& metadata = {});

  // Acknowledge an alert
  bool acknowledgeAlert(uint64_t alertId);

  // Get alerts
  std::vector<Alert> getRecentAlerts(size_t count = 50) const;
  std::vector<Alert> getUnacknowledgedAlerts() const;
  std::vector<Alert> getAlertsByType(AlertType type, size_t count = 50) const;
  std::vector<Alert> getAlertsBySeverity(AlertSeverity severity,
                                         size_t count = 50) const;

  // Callback registration for real-time delivery
  using AlertCallback = std::function<void(const Alert&)>;
  void registerCallback(AlertCallback callback);

  // Statistics
  size_t getTotalAlertCount() const;
  size_t getUnacknowledgedCount() const;

  // Serialization
  nlohmann::json toJson() const;
  nlohmann::json alertToJson(const Alert& alert) const;

  // String conversions
  static std::string typeToString(AlertType type);
  static std::string severityToString(AlertSeverity severity);

private:
  AlertManager() = default;
  ~AlertManager() = default;

  AlertManager(const AlertManager&) = delete;
  AlertManager& operator=(const AlertManager&) = delete;

  AlertConfig m_config;

  // Alert storage
  mutable std::mutex m_alertsMutex;
  std::deque<Alert> m_alerts;
  std::atomic<uint64_t> m_nextAlertId{1};

  // Throttling: last alert time per type
  std::unordered_map<int, uint64_t> m_lastAlertTime;

  // Callbacks
  std::mutex m_callbackMutex;
  std::vector<AlertCallback> m_callbacks;

  // Internal
  bool isThrottled(AlertType type) const;
  void deliverAlert(const Alert& alert);
  void pruneHistory();
};

} // namespace risk
} // namespace pinnacle
