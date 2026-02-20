#include "../../core/risk/DisasterRecovery.h"

#include <filesystem>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <string>

using namespace pinnacle::risk;

// ---------------------------------------------------------------------------
// Fixture: creates a temp directory for each test, cleans up afterwards
// ---------------------------------------------------------------------------
class DisasterRecoveryTest : public ::testing::Test {
protected:
  void SetUp() override {
    tempDir_ = std::filesystem::temp_directory_path() / "pinnaclemm_dr_test";
    std::filesystem::create_directories(tempDir_);

    auto& dr = DisasterRecovery::getInstance();
    dr.initialize(tempDir_.string());
  }

  void TearDown() override {
    if (std::filesystem::exists(tempDir_)) {
      std::filesystem::remove_all(tempDir_);
    }
  }

  std::filesystem::path tempDir_;

  static nlohmann::json sampleRiskState() {
    return {{"position", 5.0},
            {"daily_pnl", 250.0},
            {"drawdown", 1.2},
            {"halted", false}};
  }

  static nlohmann::json sampleStrategyState() {
    return {{"symbol", "BTC-USD"},
            {"spread_bps", 10.0},
            {"inventory", 2.5},
            {"active_orders", 4}};
  }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_F(DisasterRecoveryTest, Initialize) {
  // Initialization already happened in SetUp; verify no crash
  auto& dr = DisasterRecovery::getInstance();
  (void)dr;
  SUCCEED();
}

TEST_F(DisasterRecoveryTest, SaveLoadRiskState) {
  auto& dr = DisasterRecovery::getInstance();

  auto riskState = sampleRiskState();
  auto strategyState = sampleStrategyState();

  bool saved = dr.saveRiskState(riskState, strategyState);
  EXPECT_TRUE(saved);

  auto loaded = dr.loadRiskState();
  EXPECT_FALSE(loaded.empty());

  // Verify key fields survived the round-trip
  EXPECT_DOUBLE_EQ(loaded["position"].get<double>(), 5.0);
  EXPECT_DOUBLE_EQ(loaded["daily_pnl"].get<double>(), 250.0);
}

TEST_F(DisasterRecoveryTest, SaveLoadStrategyState) {
  auto& dr = DisasterRecovery::getInstance();

  auto riskState = sampleRiskState();
  auto strategyState = sampleStrategyState();

  bool saved = dr.saveRiskState(riskState, strategyState);
  EXPECT_TRUE(saved);

  auto loaded = dr.loadStrategyState();
  EXPECT_FALSE(loaded.empty());

  EXPECT_EQ(loaded["symbol"].get<std::string>(), "BTC-USD");
  EXPECT_DOUBLE_EQ(loaded["spread_bps"].get<double>(), 10.0);
}

TEST_F(DisasterRecoveryTest, CreateListDeleteBackup) {
  auto& dr = DisasterRecovery::getInstance();

  // Save some state first so there is something to back up
  dr.saveRiskState(sampleRiskState(), sampleStrategyState());

  bool created = dr.createBackup("test_backup_1");
  EXPECT_TRUE(created);

  auto backups = dr.listBackups();
  bool found = false;
  for (const auto& b : backups) {
    if (b.label == "test_backup_1") {
      found = true;
    }
  }
  EXPECT_TRUE(found);

  bool deleted = dr.deleteBackup("test_backup_1");
  EXPECT_TRUE(deleted);

  // After deletion the backup should be gone
  backups = dr.listBackups();
  found = false;
  for (const auto& b : backups) {
    if (b.label == "test_backup_1") {
      found = true;
    }
  }
  EXPECT_FALSE(found);
}

TEST_F(DisasterRecoveryTest, EmergencySave) {
  auto& dr = DisasterRecovery::getInstance();

  bool ok = dr.emergencySave(sampleRiskState(), sampleStrategyState());
  EXPECT_TRUE(ok);

  // Verify that files were written (load should succeed)
  auto riskLoaded = dr.loadRiskState();
  EXPECT_FALSE(riskLoaded.empty());
}

TEST_F(DisasterRecoveryTest, ReconcilePositionMatch) {
  auto& dr = DisasterRecovery::getInstance();

  auto result = dr.reconcilePosition("BTC-USD", 5.0, 5.0);
  EXPECT_TRUE(result.positionsMatch);
  EXPECT_DOUBLE_EQ(result.discrepancy, 0.0);
  EXPECT_EQ(result.symbol, "BTC-USD");
}

TEST_F(DisasterRecoveryTest, ReconcilePositionMismatch) {
  auto& dr = DisasterRecovery::getInstance();

  auto result = dr.reconcilePosition("BTC-USD", 5.0, 7.0);
  EXPECT_FALSE(result.positionsMatch);
  EXPECT_DOUBLE_EQ(result.discrepancy, -2.0);
  EXPECT_DOUBLE_EQ(result.localPosition, 5.0);
  EXPECT_DOUBLE_EQ(result.exchangePosition, 7.0);
}

TEST_F(DisasterRecoveryTest, RestoreBackup) {
  auto& dr = DisasterRecovery::getInstance();

  // Save initial state
  nlohmann::json initialRisk = {{"position", 1.0}, {"daily_pnl", 100.0}};
  nlohmann::json initialStrategy = {{"symbol", "ETH-USD"}, {"spread_bps", 5.0}};
  dr.saveRiskState(initialRisk, initialStrategy);

  // Create a backup of that state
  bool created = dr.createBackup("restore_test");
  EXPECT_TRUE(created);

  // Overwrite with different state
  nlohmann::json newRisk = {{"position", 99.0}, {"daily_pnl", -5000.0}};
  nlohmann::json newStrategy = {{"symbol", "SOL-USD"}, {"spread_bps", 50.0}};
  dr.saveRiskState(newRisk, newStrategy);

  // Restore the backup
  bool restored = dr.restoreBackup("restore_test");
  EXPECT_TRUE(restored);

  // Loaded state should match the original
  auto loadedRisk = dr.loadRiskState();
  EXPECT_DOUBLE_EQ(loadedRisk["position"].get<double>(), 1.0);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
