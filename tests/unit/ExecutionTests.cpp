#include "../../core/orderbook/OrderBook.h"
#include "../../core/persistence/PersistenceManager.h"
#include "../../core/utils/TimeUtils.h"
#include <filesystem>
#include <gtest/gtest.h>
#include <memory>

using namespace pinnacle;

// A simple placeholder test to ensure the file compiles
// TODO: This test will be updated once ExecutionEngine is implemented
TEST(ExecutionTest, PlaceholderTest) {
  // Simple assertion to make the test pass
  ASSERT_TRUE(true);
}

// Setup for future execution tests
class ExecutionTestFixture : public ::testing::Test {
protected:
  void SetUp() override {
    // Initialize persistence manager with a temporary directory
    tempDir = std::filesystem::temp_directory_path() / "pinnaclemm_test";
    std::filesystem::create_directories(tempDir);
    auto& persistenceManager = persistence::PersistenceManager::getInstance();
    persistenceManager.initialize(tempDir.string());

    orderBook = std::make_shared<OrderBook>("BTC-USD");
  }

  void TearDown() override {
    orderBook->clear();
    // Clean up temporary directory
    if (std::filesystem::exists(tempDir)) {
      std::filesystem::remove_all(tempDir);
    }
  }

  std::shared_ptr<OrderBook> orderBook;
  std::filesystem::path tempDir;
};

// Test order execution via the order book directly
// TODO: This will be updated once ExecutionEngine is implemented
TEST_F(ExecutionTestFixture, BasicOrderExecution) {
  // Create a buy and sell order
  auto buyOrder = std::make_shared<Order>("buy-1", "BTC-USD", OrderSide::BUY,
                                          OrderType::LIMIT, 10000.0, 1.0,
                                          utils::TimeUtils::getCurrentNanos());

  auto sellOrder = std::make_shared<Order>(
      "sell-1", "BTC-USD", OrderSide::SELL, OrderType::LIMIT,
      10000.0, // Same price to ensure matching
      0.5,     // Partial fill
      utils::TimeUtils::getCurrentNanos());

  // Add orders
  ASSERT_TRUE(orderBook->addOrder(buyOrder));
  ASSERT_TRUE(orderBook->addOrder(sellOrder));

  // Execute the buy order partially
  ASSERT_TRUE(orderBook->executeOrder(buyOrder->getOrderId(), 0.5));

  // Verify order status
  auto updatedBuyOrder = orderBook->getOrder(buyOrder->getOrderId());
  ASSERT_EQ(updatedBuyOrder->getStatus(), OrderStatus::PARTIALLY_FILLED);
  ASSERT_EQ(updatedBuyOrder->getFilledQuantity(), 0.5);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
