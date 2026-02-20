#include "../../core/risk/AlertManager.h"
#include "../../core/risk/RiskConfig.h"

#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <vector>

using namespace pinnacle::risk;

// ---------------------------------------------------------------------------
// Fixture: re-initializes the singleton with a clean config each time
// ---------------------------------------------------------------------------
class AlertManagerTest : public ::testing::Test {
protected:
  void SetUp() override {
    auto& am = AlertManager::getInstance();
    am.initialize(defaultConfig());
  }

  static AlertConfig defaultConfig() {
    AlertConfig config;
    config.minAlertIntervalMs = 0; // no throttling by default
    config.maxAlertHistory = 100;
    config.warningThresholdPct = 80.0;
    config.criticalThresholdPct = 100.0;
    return config;
  }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_F(AlertManagerTest, RaiseAlert) {
  auto& am = AlertManager::getInstance();

  uint64_t id = am.raiseAlert(AlertType::POSITION_WARNING, AlertSeverity::INFO,
                              "test alert", "unit_test");

  EXPECT_GT(id, 0u);
  EXPECT_GE(am.getTotalAlertCount(), 1u);

  auto recent = am.getRecentAlerts(10);
  EXPECT_GE(recent.size(), 1u);

  // The most recent alert should match what we raised
  bool found = false;
  for (const auto& a : recent) {
    if (a.id == id) {
      EXPECT_EQ(a.type, AlertType::POSITION_WARNING);
      EXPECT_EQ(a.severity, AlertSeverity::INFO);
      EXPECT_EQ(a.message, "test alert");
      found = true;
    }
  }
  EXPECT_TRUE(found);
}

TEST_F(AlertManagerTest, AlertThrottling) {
  auto& am = AlertManager::getInstance();

  // Re-initialize with throttling enabled
  AlertConfig config = defaultConfig();
  config.minAlertIntervalMs = 60000; // 60 s throttle window
  am.initialize(config);

  uint64_t id1 = am.raiseAlert(AlertType::POSITION_BREACH,
                               AlertSeverity::CRITICAL, "first", "test");
  EXPECT_GT(id1, 0u);

  // Raise the same type immediately -> should be throttled (returns 0)
  uint64_t id2 = am.raiseAlert(AlertType::POSITION_BREACH,
                               AlertSeverity::CRITICAL, "second", "test");
  EXPECT_EQ(id2, 0u);
}

TEST_F(AlertManagerTest, AcknowledgeAlert) {
  auto& am = AlertManager::getInstance();

  uint64_t id = am.raiseAlert(AlertType::DRAWDOWN_WARNING,
                              AlertSeverity::WARNING, "ack test", "test");

  auto unacked = am.getUnacknowledgedAlerts();
  bool foundBefore = false;
  for (const auto& a : unacked) {
    if (a.id == id) {
      foundBefore = true;
      EXPECT_FALSE(a.acknowledged);
    }
  }
  EXPECT_TRUE(foundBefore);

  bool ackResult = am.acknowledgeAlert(id);
  EXPECT_TRUE(ackResult);

  // After acknowledgment it should no longer appear in unacknowledged list
  unacked = am.getUnacknowledgedAlerts();
  for (const auto& a : unacked) {
    EXPECT_NE(a.id, id);
  }
}

TEST_F(AlertManagerTest, MultipleSeverities) {
  auto& am = AlertManager::getInstance();

  am.raiseAlert(AlertType::POSITION_WARNING, AlertSeverity::INFO, "info alert",
                "test");
  am.raiseAlert(AlertType::DRAWDOWN_WARNING, AlertSeverity::WARNING,
                "warning alert", "test");
  am.raiseAlert(AlertType::DAILY_LOSS_BREACH, AlertSeverity::CRITICAL,
                "critical alert", "test");

  auto infos = am.getAlertsBySeverity(AlertSeverity::INFO);
  auto warnings = am.getAlertsBySeverity(AlertSeverity::WARNING);
  auto criticals = am.getAlertsBySeverity(AlertSeverity::CRITICAL);

  EXPECT_GE(infos.size(), 1u);
  EXPECT_GE(warnings.size(), 1u);
  EXPECT_GE(criticals.size(), 1u);

  for (const auto& a : infos) {
    EXPECT_EQ(a.severity, AlertSeverity::INFO);
  }
  for (const auto& a : warnings) {
    EXPECT_EQ(a.severity, AlertSeverity::WARNING);
  }
  for (const auto& a : criticals) {
    EXPECT_EQ(a.severity, AlertSeverity::CRITICAL);
  }
}

TEST_F(AlertManagerTest, AlertCallback) {
  auto& am = AlertManager::getInstance();

  std::atomic<bool> callbackFired{false};
  AlertType capturedType = AlertType::SYSTEM_ERROR;
  AlertSeverity capturedSeverity = AlertSeverity::INFO;

  am.registerCallback([&](const Alert& alert) {
    capturedType = alert.type;
    capturedSeverity = alert.severity;
    callbackFired.store(true);
  });

  am.raiseAlert(AlertType::VAR_BREACH, AlertSeverity::CRITICAL, "callback test",
                "test");

  EXPECT_TRUE(callbackFired.load());
  EXPECT_EQ(capturedType, AlertType::VAR_BREACH);
  EXPECT_EQ(capturedSeverity, AlertSeverity::CRITICAL);
}

TEST_F(AlertManagerTest, MaxHistory) {
  auto& am = AlertManager::getInstance();

  // Re-initialize with very small history limit
  AlertConfig config = defaultConfig();
  config.maxAlertHistory = 5;
  config.minAlertIntervalMs = 0;
  am.initialize(config);

  // Raise more than the max
  for (int i = 0; i < 10; ++i) {
    am.raiseAlert(AlertType::SYSTEM_ERROR, AlertSeverity::INFO,
                  "alert " + std::to_string(i), "test");
  }

  auto recent = am.getRecentAlerts(100);
  // Should be pruned to at most maxAlertHistory
  EXPECT_LE(recent.size(), 5u);
}

TEST_F(AlertManagerTest, TypeToString) {
  EXPECT_FALSE(AlertManager::typeToString(AlertType::POSITION_WARNING).empty());
  EXPECT_FALSE(AlertManager::typeToString(AlertType::POSITION_BREACH).empty());
  EXPECT_FALSE(AlertManager::typeToString(AlertType::DRAWDOWN_WARNING).empty());
  EXPECT_FALSE(AlertManager::typeToString(AlertType::DRAWDOWN_BREACH).empty());
  EXPECT_FALSE(
      AlertManager::typeToString(AlertType::DAILY_LOSS_WARNING).empty());
  EXPECT_FALSE(
      AlertManager::typeToString(AlertType::DAILY_LOSS_BREACH).empty());
  EXPECT_FALSE(AlertManager::typeToString(AlertType::VAR_BREACH).empty());
  EXPECT_FALSE(
      AlertManager::typeToString(AlertType::CIRCUIT_BREAKER_OPEN).empty());
  EXPECT_FALSE(
      AlertManager::typeToString(AlertType::CIRCUIT_BREAKER_HALF_OPEN).empty());
  EXPECT_FALSE(
      AlertManager::typeToString(AlertType::CIRCUIT_BREAKER_CLOSED).empty());
  EXPECT_FALSE(AlertManager::typeToString(AlertType::SPREAD_ANOMALY).empty());
  EXPECT_FALSE(AlertManager::typeToString(AlertType::VOLUME_ANOMALY).empty());
  EXPECT_FALSE(AlertManager::typeToString(AlertType::LATENCY_WARNING).empty());
  EXPECT_FALSE(
      AlertManager::typeToString(AlertType::CONNECTIVITY_ISSUE).empty());
  EXPECT_FALSE(AlertManager::typeToString(AlertType::REGIME_CHANGE).empty());
  EXPECT_FALSE(AlertManager::typeToString(AlertType::SYSTEM_ERROR).empty());

  EXPECT_NE(AlertManager::typeToString(AlertType::POSITION_WARNING),
            AlertManager::typeToString(AlertType::VAR_BREACH));
}

TEST_F(AlertManagerTest, SeverityToString) {
  EXPECT_FALSE(AlertManager::severityToString(AlertSeverity::INFO).empty());
  EXPECT_FALSE(AlertManager::severityToString(AlertSeverity::WARNING).empty());
  EXPECT_FALSE(AlertManager::severityToString(AlertSeverity::CRITICAL).empty());
  EXPECT_FALSE(
      AlertManager::severityToString(AlertSeverity::EMERGENCY).empty());

  EXPECT_NE(AlertManager::severityToString(AlertSeverity::INFO),
            AlertManager::severityToString(AlertSeverity::EMERGENCY));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
