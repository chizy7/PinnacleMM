#include "../../core/risk/CircuitBreaker.h"
#include "../../core/risk/RiskConfig.h"
#include "../../core/utils/TimeUtils.h"

#include <atomic>
#include <gtest/gtest.h>
#include <string>
#include <thread>

using namespace pinnacle;
using namespace pinnacle::risk;

// ---------------------------------------------------------------------------
// Fixture: resets circuit breaker state before each test
// ---------------------------------------------------------------------------
class CircuitBreakerTest : public ::testing::Test {
protected:
  void SetUp() override {
    auto& cb = CircuitBreaker::getInstance();
    cb.reset();
  }

  void TearDown() override {
    auto& cb = CircuitBreaker::getInstance();
    // Clear callback BEFORE reset to avoid invoking it with dangling references
    cb.setStateCallback(nullptr);
    cb.reset();
  }

  static CircuitBreakerConfig defaultConfig() {
    CircuitBreakerConfig config;
    config.priceMove1minPct = 2.0;
    config.priceMove5minPct = 5.0;
    config.spreadWidenMultiplier = 3.0;
    config.volumeSpikeMultiplier = 5.0;
    config.cooldownPeriodMs = 30000;
    config.halfOpenTestDurationMs = 10000;
    config.maxLatencyUs = 10000;
    config.priceHistorySize = 300;
    return config;
  }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_F(CircuitBreakerTest, InitialStateClosed) {
  auto& cb = CircuitBreaker::getInstance();
  cb.initialize(defaultConfig());

  EXPECT_EQ(cb.getState(), CircuitBreakerState::CLOSED);
  EXPECT_TRUE(cb.isTradingAllowed());
}

TEST_F(CircuitBreakerTest, ManualTrip) {
  auto& cb = CircuitBreaker::getInstance();
  cb.initialize(defaultConfig());

  cb.trip("manual test trip");

  EXPECT_EQ(cb.getState(), CircuitBreakerState::OPEN);
  EXPECT_FALSE(cb.isTradingAllowed());
}

TEST_F(CircuitBreakerTest, ManualReset) {
  auto& cb = CircuitBreaker::getInstance();
  cb.initialize(defaultConfig());

  cb.trip("trip for reset test");
  EXPECT_EQ(cb.getState(), CircuitBreakerState::OPEN);

  cb.reset();
  EXPECT_EQ(cb.getState(), CircuitBreakerState::CLOSED);
  EXPECT_TRUE(cb.isTradingAllowed());
}

TEST_F(CircuitBreakerTest, StateCallback) {
  auto& cb = CircuitBreaker::getInstance();
  cb.initialize(defaultConfig());

  std::atomic<bool> callbackInvoked{false};
  CircuitBreakerState capturedOldState = CircuitBreakerState::CLOSED;
  CircuitBreakerState capturedNewState = CircuitBreakerState::CLOSED;
  CircuitBreakerTrigger capturedTrigger = CircuitBreakerTrigger::NONE;

  cb.setStateCallback([&](CircuitBreakerState oldState,
                          CircuitBreakerState newState,
                          CircuitBreakerTrigger trigger) {
    capturedOldState = oldState;
    capturedNewState = newState;
    capturedTrigger = trigger;
    callbackInvoked.store(true);
  });

  cb.trip("callback test");

  EXPECT_TRUE(callbackInvoked.load());
  EXPECT_EQ(capturedOldState, CircuitBreakerState::CLOSED);
  EXPECT_EQ(capturedNewState, CircuitBreakerState::OPEN);
  EXPECT_EQ(capturedTrigger, CircuitBreakerTrigger::MANUAL);
}

TEST_F(CircuitBreakerTest, PriceMoveTrip) {
  auto& cb = CircuitBreaker::getInstance();

  // Use a very tight threshold so a small move trips
  CircuitBreakerConfig config = defaultConfig();
  config.priceMove1minPct = 0.5; // 0.5 % move in 1 min triggers
  cb.initialize(config);

  // Feed a baseline price
  uint64_t now = utils::TimeUtils::getCurrentNanos();
  uint64_t timestampNs = now;
  cb.onPrice(100.0, timestampNs);

  // Feed a sharply moved price within the same time window
  // Advance only a few milliseconds
  timestampNs += 500'000'000ULL;  // +0.5 s
  cb.onPrice(105.0, timestampNs); // 5 % move

  // The breaker should have tripped on rapid price move
  EXPECT_EQ(cb.getState(), CircuitBreakerState::OPEN);
  EXPECT_FALSE(cb.isTradingAllowed());
}

TEST_F(CircuitBreakerTest, LatencyTrip) {
  auto& cb = CircuitBreaker::getInstance();

  CircuitBreakerConfig config = defaultConfig();
  config.maxLatencyUs = 1000; // 1 ms max
  cb.initialize(config);

  EXPECT_TRUE(cb.isTradingAllowed());

  // Report extremely high latency
  cb.onLatency(50000); // 50 ms

  EXPECT_EQ(cb.getState(), CircuitBreakerState::OPEN);
  EXPECT_FALSE(cb.isTradingAllowed());
}

TEST_F(CircuitBreakerTest, RegimeCrisis) {
  auto& cb = CircuitBreaker::getInstance();
  cb.initialize(defaultConfig());

  // Regime value 5 typically corresponds to CRISIS
  cb.onRegimeChange(5);

  EXPECT_EQ(cb.getState(), CircuitBreakerState::OPEN);
  EXPECT_FALSE(cb.isTradingAllowed());
}

TEST_F(CircuitBreakerTest, StateToString) {
  EXPECT_FALSE(
      CircuitBreaker::stateToString(CircuitBreakerState::CLOSED).empty());
  EXPECT_FALSE(
      CircuitBreaker::stateToString(CircuitBreakerState::OPEN).empty());
  EXPECT_FALSE(
      CircuitBreaker::stateToString(CircuitBreakerState::HALF_OPEN).empty());

  // Distinct values should produce distinct strings
  EXPECT_NE(CircuitBreaker::stateToString(CircuitBreakerState::CLOSED),
            CircuitBreaker::stateToString(CircuitBreakerState::OPEN));
  EXPECT_NE(CircuitBreaker::stateToString(CircuitBreakerState::OPEN),
            CircuitBreaker::stateToString(CircuitBreakerState::HALF_OPEN));
}

TEST_F(CircuitBreakerTest, TriggerToString) {
  EXPECT_FALSE(
      CircuitBreaker::triggerToString(CircuitBreakerTrigger::NONE).empty());
  EXPECT_FALSE(CircuitBreaker::triggerToString(
                   CircuitBreakerTrigger::RAPID_PRICE_MOVE_1MIN)
                   .empty());
  EXPECT_FALSE(CircuitBreaker::triggerToString(
                   CircuitBreakerTrigger::RAPID_PRICE_MOVE_5MIN)
                   .empty());
  EXPECT_FALSE(
      CircuitBreaker::triggerToString(CircuitBreakerTrigger::SPREAD_WIDENING)
          .empty());
  EXPECT_FALSE(
      CircuitBreaker::triggerToString(CircuitBreakerTrigger::VOLUME_SPIKE)
          .empty());
  EXPECT_FALSE(
      CircuitBreaker::triggerToString(CircuitBreakerTrigger::MARKET_CRISIS)
          .empty());
  EXPECT_FALSE(CircuitBreaker::triggerToString(
                   CircuitBreakerTrigger::LATENCY_DEGRADATION)
                   .empty());
  EXPECT_FALSE(
      CircuitBreaker::triggerToString(CircuitBreakerTrigger::CONNECTIVITY_LOSS)
          .empty());
  EXPECT_FALSE(
      CircuitBreaker::triggerToString(CircuitBreakerTrigger::MANUAL).empty());

  EXPECT_NE(CircuitBreaker::triggerToString(CircuitBreakerTrigger::NONE),
            CircuitBreaker::triggerToString(CircuitBreakerTrigger::MANUAL));
}

TEST_F(CircuitBreakerTest, StatusSnapshot) {
  auto& cb = CircuitBreaker::getInstance();
  cb.initialize(defaultConfig());

  auto status = cb.getStatus();
  EXPECT_EQ(status.state, CircuitBreakerState::CLOSED);
  EXPECT_EQ(status.lastTrigger, CircuitBreakerTrigger::NONE);
  EXPECT_EQ(status.tripCount, 0u);

  cb.trip("status test");

  status = cb.getStatus();
  EXPECT_EQ(status.state, CircuitBreakerState::OPEN);
  EXPECT_EQ(status.lastTrigger, CircuitBreakerTrigger::MANUAL);
  EXPECT_GE(status.tripCount, 1u);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
