#include "../../core/instrument/InstrumentManager.h"

#include <gtest/gtest.h>
#include <string>

using namespace pinnacle;
using namespace pinnacle::instrument;

class InstrumentManagerTest : public ::testing::Test {
protected:
  InstrumentManager manager;

  static InstrumentConfig makeConfig(const std::string& symbol) {
    InstrumentConfig cfg;
    cfg.symbol = symbol;
    cfg.useLockFree = false; // Use mutex-based for test simplicity
    cfg.enableML = false;
    cfg.baseSpreadBps = 10.0;
    cfg.orderQuantity = 0.01;
    cfg.maxPosition = 10.0;
    return cfg;
  }
};

TEST_F(InstrumentManagerTest, AddInstrument) {
  auto cfg = makeConfig("BTC-USD");
  EXPECT_TRUE(manager.addInstrument(cfg, "simulation"));
  EXPECT_EQ(manager.getInstrumentCount(), 1u);
  EXPECT_TRUE(manager.hasInstrument("BTC-USD"));
}

TEST_F(InstrumentManagerTest, AddDuplicateInstrument) {
  auto cfg = makeConfig("BTC-USD");
  EXPECT_TRUE(manager.addInstrument(cfg, "simulation"));
  EXPECT_FALSE(manager.addInstrument(cfg, "simulation"));
  EXPECT_EQ(manager.getInstrumentCount(), 1u);
}

TEST_F(InstrumentManagerTest, RemoveInstrument) {
  auto cfg = makeConfig("BTC-USD");
  EXPECT_TRUE(manager.addInstrument(cfg, "simulation"));
  EXPECT_TRUE(manager.removeInstrument("BTC-USD"));
  EXPECT_EQ(manager.getInstrumentCount(), 0u);
  EXPECT_FALSE(manager.hasInstrument("BTC-USD"));
}

TEST_F(InstrumentManagerTest, RemoveNonexistent) {
  EXPECT_FALSE(manager.removeInstrument("FAKE-USD"));
}

TEST_F(InstrumentManagerTest, GetContext) {
  auto cfg = makeConfig("ETH-USD");
  manager.addInstrument(cfg, "simulation");

  auto* ctx = manager.getContext("ETH-USD");
  ASSERT_NE(ctx, nullptr);
  EXPECT_EQ(ctx->symbol, "ETH-USD");
  EXPECT_NE(ctx->orderBook, nullptr);
  EXPECT_NE(ctx->strategy, nullptr);
  EXPECT_NE(ctx->simulator, nullptr); // simulation mode

  EXPECT_EQ(manager.getContext("FAKE"), nullptr);
}

TEST_F(InstrumentManagerTest, LiveModeNoSimulator) {
  auto cfg = makeConfig("BTC-USD");
  manager.addInstrument(cfg, "live");

  auto* ctx = manager.getContext("BTC-USD");
  ASSERT_NE(ctx, nullptr);
  EXPECT_EQ(ctx->simulator, nullptr);
}

TEST_F(InstrumentManagerTest, MultipleInstruments) {
  manager.addInstrument(makeConfig("BTC-USD"), "simulation");
  manager.addInstrument(makeConfig("ETH-USD"), "simulation");
  manager.addInstrument(makeConfig("SOL-USD"), "simulation");

  EXPECT_EQ(manager.getInstrumentCount(), 3u);

  auto symbols = manager.getSymbols();
  EXPECT_EQ(symbols.size(), 3u);
}

TEST_F(InstrumentManagerTest, StartAndStopAll) {
  manager.addInstrument(makeConfig("BTC-USD"), "simulation");
  manager.addInstrument(makeConfig("ETH-USD"), "simulation");

  EXPECT_TRUE(manager.startAll());

  // Verify both are running
  auto* btc = manager.getContext("BTC-USD");
  auto* eth = manager.getContext("ETH-USD");
  ASSERT_NE(btc, nullptr);
  ASSERT_NE(eth, nullptr);
  EXPECT_TRUE(btc->running);
  EXPECT_TRUE(eth->running);

  EXPECT_TRUE(manager.stopAll());
  EXPECT_FALSE(btc->running);
  EXPECT_FALSE(eth->running);
}

TEST_F(InstrumentManagerTest, GetAggregateStatistics) {
  manager.addInstrument(makeConfig("BTC-USD"), "simulation");
  manager.addInstrument(makeConfig("ETH-USD"), "simulation");
  manager.startAll();

  std::string stats = manager.getAggregateStatistics();
  EXPECT_FALSE(stats.empty());
  EXPECT_NE(stats.find("BTC-USD"), std::string::npos);
  EXPECT_NE(stats.find("ETH-USD"), std::string::npos);
  EXPECT_NE(stats.find("AGGREGATE"), std::string::npos);

  manager.stopAll();
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
