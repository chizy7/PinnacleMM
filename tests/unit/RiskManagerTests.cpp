#include "../../core/orderbook/Order.h"
#include "../../core/risk/RiskConfig.h"
#include "../../core/risk/RiskManager.h"

#include <cmath>
#include <gtest/gtest.h>
#include <string>

using namespace pinnacle;
using namespace pinnacle::risk;

// ---------------------------------------------------------------------------
// Fixture: resets singleton state before each test
// ---------------------------------------------------------------------------
class RiskManagerTest : public ::testing::Test {
protected:
  void SetUp() override {
    auto& rm = RiskManager::getInstance();
    // Resume in case a previous test left us halted
    rm.resume();
    // Reset daily counters
    rm.resetDaily();
  }

  void TearDown() override {
    auto& rm = RiskManager::getInstance();
    rm.resume();
    rm.resetDaily();
  }

  static RiskLimits defaultLimits() {
    RiskLimits limits;
    limits.maxPositionSize = 100.0;
    limits.maxOrderSize = 10.0;
    limits.dailyLossLimit = 10000.0;
    limits.maxDrawdownPct = 5.0;
    limits.maxDailyVolume = 1000.0;
    limits.maxOrderValue = 500000.0;
    limits.maxOrdersPerSecond = 1000;
    return limits;
  }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_F(RiskManagerTest, CheckOrderApproved) {
  auto& rm = RiskManager::getInstance();
  rm.initialize(defaultLimits());

  auto result = rm.checkOrder(OrderSide::BUY, 100.0, 1.0, "BTC-USD");
  EXPECT_EQ(result, RiskCheckResult::APPROVED);
}

TEST_F(RiskManagerTest, CheckOrderPositionLimit) {
  auto& rm = RiskManager::getInstance();

  RiskLimits limits = defaultLimits();
  limits.maxPositionSize = 2.0; // very small position cap
  rm.initialize(limits);

  // Fill up position to the limit first
  rm.onFill(OrderSide::BUY, 100.0, 2.0, "BTC-USD");

  // Now a new buy should be rejected due to position limit
  auto result = rm.checkOrder(OrderSide::BUY, 100.0, 1.0, "BTC-USD");
  EXPECT_EQ(result, RiskCheckResult::REJECTED_POSITION_LIMIT);
}

TEST_F(RiskManagerTest, CheckOrderSizeLimit) {
  auto& rm = RiskManager::getInstance();

  RiskLimits limits = defaultLimits();
  limits.maxOrderSize = 0.5; // tiny per-order limit
  rm.initialize(limits);

  auto result = rm.checkOrder(OrderSide::BUY, 100.0, 1.0, "BTC-USD");
  EXPECT_EQ(result, RiskCheckResult::REJECTED_ORDER_SIZE_LIMIT);
}

TEST_F(RiskManagerTest, DailyLossLimit) {
  auto& rm = RiskManager::getInstance();

  RiskLimits limits = defaultLimits();
  limits.dailyLossLimit = 100.0;
  rm.initialize(limits);

  // Simulate a heavy loss exceeding the daily limit
  rm.onPnLUpdate(-150.0);

  EXPECT_TRUE(rm.isHalted());
}

TEST_F(RiskManagerTest, DrawdownLimit) {
  auto& rm = RiskManager::getInstance();

  RiskLimits limits = defaultLimits();
  limits.maxDrawdownPct = 5.0;
  rm.initialize(limits);

  // Push PnL up to establish a peak
  rm.onPnLUpdate(1000.0);
  EXPECT_FALSE(rm.isHalted());

  // Drop PnL well below peak => drawdown exceeds 5 %
  rm.onPnLUpdate(900.0); // 10 % drawdown from 1000
  EXPECT_TRUE(rm.isHalted());
}

TEST_F(RiskManagerTest, OnFill) {
  auto& rm = RiskManager::getInstance();
  rm.initialize(defaultLimits());

  rm.onFill(OrderSide::BUY, 100.0, 5.0, "BTC-USD");
  EXPECT_DOUBLE_EQ(rm.getPosition(), 5.0);

  rm.onFill(OrderSide::SELL, 100.0, 3.0, "BTC-USD");
  EXPECT_DOUBLE_EQ(rm.getPosition(), 2.0);
}

TEST_F(RiskManagerTest, HaltResume) {
  auto& rm = RiskManager::getInstance();
  rm.initialize(defaultLimits());

  EXPECT_FALSE(rm.isHalted());

  rm.halt("manual test halt");
  EXPECT_TRUE(rm.isHalted());

  // While halted, orders should be rejected
  auto result = rm.checkOrder(OrderSide::BUY, 100.0, 1.0, "BTC-USD");
  EXPECT_EQ(result, RiskCheckResult::REJECTED_HALTED);

  rm.resume();
  EXPECT_FALSE(rm.isHalted());

  // After resume, orders should be accepted again
  result = rm.checkOrder(OrderSide::BUY, 100.0, 1.0, "BTC-USD");
  EXPECT_EQ(result, RiskCheckResult::APPROVED);
}

TEST_F(RiskManagerTest, DailyReset) {
  auto& rm = RiskManager::getInstance();
  rm.initialize(defaultLimits());

  rm.onFill(OrderSide::BUY, 100.0, 5.0, "BTC-USD");
  rm.onPnLUpdate(500.0);

  EXPECT_NE(rm.getDailyPnL(), 0.0);

  rm.resetDaily();

  EXPECT_DOUBLE_EQ(rm.getDailyPnL(), 0.0);
}

TEST_F(RiskManagerTest, ResultToString) {
  EXPECT_FALSE(RiskManager::resultToString(RiskCheckResult::APPROVED).empty());
  EXPECT_FALSE(
      RiskManager::resultToString(RiskCheckResult::REJECTED_POSITION_LIMIT)
          .empty());
  EXPECT_FALSE(
      RiskManager::resultToString(RiskCheckResult::REJECTED_EXPOSURE_LIMIT)
          .empty());
  EXPECT_FALSE(
      RiskManager::resultToString(RiskCheckResult::REJECTED_DRAWDOWN_LIMIT)
          .empty());
  EXPECT_FALSE(
      RiskManager::resultToString(RiskCheckResult::REJECTED_DAILY_LOSS_LIMIT)
          .empty());
  EXPECT_FALSE(
      RiskManager::resultToString(RiskCheckResult::REJECTED_ORDER_SIZE_LIMIT)
          .empty());
  EXPECT_FALSE(RiskManager::resultToString(RiskCheckResult::REJECTED_RATE_LIMIT)
                   .empty());
  EXPECT_FALSE(
      RiskManager::resultToString(RiskCheckResult::REJECTED_CIRCUIT_BREAKER)
          .empty());
  EXPECT_FALSE(
      RiskManager::resultToString(RiskCheckResult::REJECTED_VOLUME_LIMIT)
          .empty());
  EXPECT_FALSE(
      RiskManager::resultToString(RiskCheckResult::REJECTED_HALTED).empty());

  // Sanity: the strings should differ between distinct results
  EXPECT_NE(RiskManager::resultToString(RiskCheckResult::APPROVED),
            RiskManager::resultToString(RiskCheckResult::REJECTED_HALTED));
}

TEST_F(RiskManagerTest, PositionUtilization) {
  auto& rm = RiskManager::getInstance();

  RiskLimits limits = defaultLimits();
  limits.maxPositionSize = 10.0;
  rm.initialize(limits);

  rm.onFill(OrderSide::BUY, 100.0, 5.0, "BTC-USD");

  double util = rm.getPositionUtilization();
  EXPECT_NEAR(util, 50.0, 1.0); // 5/10 = 50 %
}

TEST_F(RiskManagerTest, DailyLossUtilization) {
  auto& rm = RiskManager::getInstance();

  RiskLimits limits = defaultLimits();
  limits.dailyLossLimit = 1000.0;
  rm.initialize(limits);

  rm.onPnLUpdate(-500.0);

  double util = rm.getDailyLossUtilization();
  EXPECT_NEAR(util, 50.0, 1.0); // 500/1000 = 50 %
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
