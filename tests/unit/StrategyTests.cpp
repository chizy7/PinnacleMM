#include "../../core/orderbook/OrderBook.h"
#include "../../core/persistence/PersistenceManager.h"
#include "../../core/utils/TimeUtils.h"
#include "../../strategies/basic/BasicMarketMaker.h"
#include "../../strategies/config/StrategyConfig.h"
#include <filesystem>
#include <gtest/gtest.h>
#include <memory>

using namespace pinnacle;
using namespace pinnacle::strategy;

// Test fixture for strategy tests
class StrategyTestFixture : public ::testing::Test {
protected:
  void SetUp() override {
    // Initialize persistence manager with a temporary directory
    tempDir = std::filesystem::temp_directory_path() / "pinnaclemm_test";
    std::filesystem::create_directories(tempDir);
    auto& persistenceManager = persistence::PersistenceManager::getInstance();
    persistenceManager.initialize(tempDir.string());

    // Create order book
    orderBook = std::make_shared<OrderBook>("BTC-USD");

    // Create default config
    config = StrategyConfig();
    config.symbol = "BTC-USD";
    config.baseSpreadBps = 10.0;
    config.orderQuantity = 0.01;

    // Initialize strategy
    strategy = std::make_shared<BasicMarketMaker>("BTC-USD", config);
    strategy->initialize(orderBook);
  }

  void TearDown() override {
    if (strategy->isRunning()) {
      strategy->stop();
    }
    orderBook->clear();
    // Clean up temporary directory
    if (std::filesystem::exists(tempDir)) {
      std::filesystem::remove_all(tempDir);
    }
  }

  std::shared_ptr<OrderBook> orderBook;
  StrategyConfig config;
  std::shared_ptr<BasicMarketMaker> strategy;
  std::filesystem::path tempDir;
};

// Test strategy initialization
TEST_F(StrategyTestFixture, StrategyInitialization) {
  ASSERT_FALSE(strategy->isRunning());

  // Start the strategy
  ASSERT_TRUE(strategy->start());
  ASSERT_TRUE(strategy->isRunning());

  // Stop the strategy
  ASSERT_TRUE(strategy->stop());
  ASSERT_FALSE(strategy->isRunning());
}

// Test config validation
TEST_F(StrategyTestFixture, ConfigValidation) {
  std::string errorReason;
  ASSERT_TRUE(config.validate(errorReason));

  // Test invalid config
  StrategyConfig invalidConfig = config;
  invalidConfig.baseSpreadBps = -1.0; // Invalid value
  ASSERT_FALSE(invalidConfig.validate(errorReason));
  ASSERT_FALSE(errorReason.empty());
}

// Test strategy behavior with market data
TEST_F(StrategyTestFixture, MarketDataResponse) {
  // Start the strategy
  ASSERT_TRUE(strategy->start());

  // Add some orders to create a market
  auto buyOrder = std::make_shared<Order>(
      "buy-1", "BTC-USD", OrderSide::BUY, OrderType::LIMIT, 9900.0, 1.0,
      pinnacle::utils::TimeUtils::getCurrentNanos() // Fix namespace
  );

  auto sellOrder = std::make_shared<Order>(
      "sell-1", "BTC-USD", OrderSide::SELL, OrderType::LIMIT, 10100.0, 1.0,
      pinnacle::utils::TimeUtils::getCurrentNanos() // Fix namespace
  );

  orderBook->addOrder(buyOrder);
  orderBook->addOrder(sellOrder);

  // Let the strategy run for a short time
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Stop the strategy
  ASSERT_TRUE(strategy->stop());

  // Check strategy statistics
  std::string stats = strategy->getStatistics();
  ASSERT_FALSE(stats.empty());
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
