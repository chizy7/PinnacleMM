#include "../../core/orderbook/OrderBook.h"
#include "../../core/utils/TimeUtils.h"
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
  void SetUp() override { orderBook = std::make_shared<OrderBook>("BTC-USD"); }

  void TearDown() override { orderBook->clear(); }

  std::shared_ptr<OrderBook> orderBook;
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
