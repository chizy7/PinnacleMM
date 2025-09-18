#include "../../core/orderbook/OrderBook.h"
#include "../../core/persistence/PersistenceManager.h"
#include "../../core/utils/TimeUtils.h"

#include <chrono>
#include <filesystem>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace pinnacle;

// Test fixture for OrderBook tests
class OrderBookTest : public ::testing::Test {
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

  // Helper to create a buy order
  std::shared_ptr<Order> createBuyOrder(double price, double quantity) {
    std::string orderId = "buy-" + std::to_string(orderIdCounter++);
    return std::make_shared<Order>(orderId, "BTC-USD", OrderSide::BUY,
                                   OrderType::LIMIT, price, quantity,
                                   utils::TimeUtils::getCurrentNanos());
  }

  // Helper to create a sell order
  std::shared_ptr<Order> createSellOrder(double price, double quantity) {
    std::string orderId = "sell-" + std::to_string(orderIdCounter++);
    return std::make_shared<Order>(orderId, "BTC-USD", OrderSide::SELL,
                                   OrderType::LIMIT, price, quantity,
                                   utils::TimeUtils::getCurrentNanos());
  }

  std::shared_ptr<OrderBook> orderBook;
  int orderIdCounter = 1;
  std::filesystem::path tempDir;
};

// Test adding orders
TEST_F(OrderBookTest, AddOrder) {
  auto buyOrder = createBuyOrder(10000.0, 1.0);
  auto sellOrder = createSellOrder(10100.0, 1.0);

  // Add orders
  EXPECT_TRUE(orderBook->addOrder(buyOrder));
  EXPECT_TRUE(orderBook->addOrder(sellOrder));

  // Verify order count
  EXPECT_EQ(orderBook->getOrderCount(), 2);

  // Verify best prices
  EXPECT_EQ(orderBook->getBestBidPrice(), 10000.0);
  EXPECT_EQ(orderBook->getBestAskPrice(), 10100.0);

  // Verify spread and mid price
  EXPECT_EQ(orderBook->getSpread(), 100.0);
  EXPECT_EQ(orderBook->getMidPrice(), 10050.0);
}

// Test canceling orders
TEST_F(OrderBookTest, CancelOrder) {
  auto buyOrder = createBuyOrder(10000.0, 1.0);
  auto sellOrder = createSellOrder(10100.0, 1.0);

  // Add orders
  EXPECT_TRUE(orderBook->addOrder(buyOrder));
  EXPECT_TRUE(orderBook->addOrder(sellOrder));

  // Cancel buy order
  EXPECT_TRUE(orderBook->cancelOrder(buyOrder->getOrderId()));

  // Verify order count
  EXPECT_EQ(orderBook->getOrderCount(), 1);

  // Verify best prices
  EXPECT_EQ(orderBook->getBestBidPrice(), 0.0);
  EXPECT_EQ(orderBook->getBestAskPrice(), 10100.0);
}

// Test executing orders
TEST_F(OrderBookTest, ExecuteOrder) {
  auto buyOrder = createBuyOrder(10000.0, 1.0);
  auto sellOrder = createSellOrder(10100.0, 1.0);

  // Add orders
  EXPECT_TRUE(orderBook->addOrder(buyOrder));
  EXPECT_TRUE(orderBook->addOrder(sellOrder));

  // Partially execute buy order
  EXPECT_TRUE(orderBook->executeOrder(buyOrder->getOrderId(), 0.5));

  // Verify order status
  auto updatedBuyOrder = orderBook->getOrder(buyOrder->getOrderId());
  EXPECT_EQ(updatedBuyOrder->getStatus(), OrderStatus::PARTIALLY_FILLED);
  EXPECT_EQ(updatedBuyOrder->getFilledQuantity(), 0.5);

  // Fully execute buy order
  EXPECT_TRUE(orderBook->executeOrder(buyOrder->getOrderId(), 0.5));

  // Verify order count (buy order should be removed)
  EXPECT_EQ(orderBook->getOrderCount(), 1);

  // Verify best prices
  EXPECT_EQ(orderBook->getBestBidPrice(), 0.0);
  EXPECT_EQ(orderBook->getBestAskPrice(), 10100.0);
}

// Test market orders
TEST_F(OrderBookTest, MarketOrder) {
  // Add multiple buy and sell orders at different price levels
  orderBook->addOrder(createBuyOrder(10000.0, 1.0));
  orderBook->addOrder(createBuyOrder(9900.0, 2.0));
  orderBook->addOrder(createBuyOrder(9800.0, 3.0));

  orderBook->addOrder(createSellOrder(10100.0, 1.0));
  orderBook->addOrder(createSellOrder(10200.0, 2.0));
  orderBook->addOrder(createSellOrder(10300.0, 3.0));

  // Execute a buy market order
  std::vector<std::pair<std::string, double>> fills;
  double executedQuantity =
      orderBook->executeMarketOrder(OrderSide::BUY, 2.0, fills);

  // Verify execution
  EXPECT_EQ(executedQuantity, 2.0);
  EXPECT_EQ(fills.size(), 2); // Fills against 2 sell orders

  // Verify order book state
  EXPECT_EQ(orderBook->getOrderCount(), 5); // 3 buys + 2 remaining sells
  EXPECT_EQ(orderBook->getBestAskPrice(), 10200.0);

  // Execute a sell market order
  fills.clear();
  executedQuantity = orderBook->executeMarketOrder(OrderSide::SELL, 4.0, fills);

  // Verify execution
  EXPECT_EQ(executedQuantity, 4.0);
  EXPECT_EQ(fills.size(), 3); // Fills against 3 buy orders

  // Verify order book state
  EXPECT_EQ(orderBook->getOrderCount(), 3); // Remaining: 0 buys, 3 sells
  EXPECT_EQ(orderBook->getBestBidPrice(), 9800.0);
}

// Test price level management
TEST_F(OrderBookTest, PriceLevels) {
  // Add multiple orders at the same price level
  auto buyOrder1 = createBuyOrder(10000.0, 1.0);
  auto buyOrder2 = createBuyOrder(10000.0, 2.0);
  auto buyOrder3 = createBuyOrder(9900.0, 3.0);

  orderBook->addOrder(buyOrder1);
  orderBook->addOrder(buyOrder2);
  orderBook->addOrder(buyOrder3);

  // Verify bid levels
  auto bidLevels = orderBook->getBidLevels(10);
  EXPECT_EQ(bidLevels.size(), 2);
  EXPECT_EQ(bidLevels[0].price, 10000.0);
  EXPECT_EQ(bidLevels[0].totalQuantity, 3.0);
  EXPECT_EQ(bidLevels[1].price, 9900.0);
  EXPECT_EQ(bidLevels[1].totalQuantity, 3.0);

  // Cancel one order at the best price level
  orderBook->cancelOrder(buyOrder1->getOrderId());

  // Verify updated bid levels
  bidLevels = orderBook->getBidLevels(10);
  EXPECT_EQ(bidLevels.size(), 2);
  EXPECT_EQ(bidLevels[0].price, 10000.0);
  EXPECT_EQ(bidLevels[0].totalQuantity, 2.0);

  // Cancel the last order at the best price level
  orderBook->cancelOrder(buyOrder2->getOrderId());

  // Verify that the price level is removed
  bidLevels = orderBook->getBidLevels(10);
  EXPECT_EQ(bidLevels.size(), 1);
  EXPECT_EQ(bidLevels[0].price, 9900.0);
}

// Test order book imbalance calculation
TEST_F(OrderBookTest, OrderBookImbalance) {
  // Add orders with more volume on the bid side
  orderBook->addOrder(createBuyOrder(10000.0, 5.0));
  orderBook->addOrder(createBuyOrder(9900.0, 5.0));

  orderBook->addOrder(createSellOrder(10100.0, 2.0));
  orderBook->addOrder(createSellOrder(10200.0, 2.0));

  // Calculate imbalance
  double imbalance = orderBook->calculateOrderBookImbalance(10);

  // Verify positive imbalance (more bids than asks)
  EXPECT_GT(imbalance, 0.0);

  // Clear order book
  orderBook->clear();

  // Add orders with more volume on the ask side
  orderBook->addOrder(createBuyOrder(10000.0, 2.0));
  orderBook->addOrder(createBuyOrder(9900.0, 2.0));

  orderBook->addOrder(createSellOrder(10100.0, 5.0));
  orderBook->addOrder(createSellOrder(10200.0, 5.0));

  // Calculate imbalance
  imbalance = orderBook->calculateOrderBookImbalance(10);

  // Verify negative imbalance (more asks than bids)
  EXPECT_LT(imbalance, 0.0);
}

// Test market impact calculation
TEST_F(OrderBookTest, MarketImpact) {
  // Add orders at different price levels
  orderBook->addOrder(createBuyOrder(10000.0, 1.0));
  orderBook->addOrder(createBuyOrder(9900.0, 2.0));
  orderBook->addOrder(createBuyOrder(9800.0, 3.0));

  orderBook->addOrder(createSellOrder(10100.0, 1.0));
  orderBook->addOrder(createSellOrder(10200.0, 2.0));
  orderBook->addOrder(createSellOrder(10300.0, 3.0));

  // Calculate impact of a small buy order
  double impact1 = orderBook->calculateMarketImpact(OrderSide::BUY, 0.5);

  // Calculate impact of a larger buy order
  double impact2 = orderBook->calculateMarketImpact(OrderSide::BUY, 2.5);

  // Verify that larger order has higher price impact
  EXPECT_GT(impact2, impact1);

  // Calculate impact of a small sell order
  double impact3 = orderBook->calculateMarketImpact(OrderSide::SELL, 0.5);

  // Calculate impact of a larger sell order
  double impact4 = orderBook->calculateMarketImpact(OrderSide::SELL, 2.5);

  // Verify that larger order has lower price impact (for sells)
  EXPECT_LT(impact4, impact3);
}

// Test order book snapshot
TEST_F(OrderBookTest, OrderBookSnapshot) {
  // Add orders
  orderBook->addOrder(createBuyOrder(10000.0, 1.0));
  orderBook->addOrder(createBuyOrder(9900.0, 2.0));

  orderBook->addOrder(createSellOrder(10100.0, 1.0));
  orderBook->addOrder(createSellOrder(10200.0, 2.0));

  // Take snapshot
  auto snapshot = orderBook->getSnapshot();

  // Verify snapshot
  EXPECT_EQ(snapshot->getSymbol(), "BTC-USD");
  EXPECT_EQ(snapshot->getBids().size(), 2);
  EXPECT_EQ(snapshot->getAsks().size(), 2);
  EXPECT_EQ(snapshot->getBids()[0].price, 10000.0);
  EXPECT_EQ(snapshot->getBids()[1].price, 9900.0);
  EXPECT_EQ(snapshot->getAsks()[0].price, 10100.0);
  EXPECT_EQ(snapshot->getAsks()[1].price, 10200.0);

  // Modify order book after snapshot
  orderBook->cancelOrder(snapshot->getBids()[0].orders[0]->getOrderId());

  // Verify that snapshot remains unchanged
  EXPECT_EQ(snapshot->getBids().size(), 2);
  EXPECT_EQ(snapshot->getBids()[0].price, 10000.0);
}

// Test concurrent order operations
TEST_F(OrderBookTest, ConcurrentOperations) {
  const int numOrders = 1000;
  const int numThreads = 4;

  // Add a large number of orders
  for (int i = 0; i < numOrders; ++i) {
    if (i % 2 == 0) {
      orderBook->addOrder(createBuyOrder(10000.0 - i * 0.1, 1.0));
    } else {
      orderBook->addOrder(createSellOrder(10100.0 + i * 0.1, 1.0));
    }
  }

  // Launch threads to concurrently query the order book
  std::vector<std::thread> threads;
  std::atomic<int> snapshotCount(0);

  for (int t = 0; t < numThreads; ++t) {
    threads.emplace_back([&]() {
      for (int i = 0; i < 100; ++i) {
        auto snapshot = orderBook->getSnapshot();
        if (snapshot) {
          snapshotCount++;
        }

        orderBook->getBestBidPrice();
        orderBook->getBestAskPrice();
        orderBook->getMidPrice();
        orderBook->getSpread();

        // Small sleep to simulate workload
        std::this_thread::sleep_for(std::chrono::microseconds(10));
      }
    });
  }

  // Wait for all threads to finish
  for (auto& thread : threads) {
    thread.join();
  }

  // Verify that all snapshots were successfully taken
  EXPECT_EQ(snapshotCount, numThreads * 100);

  // Verify final order book state
  EXPECT_EQ(orderBook->getOrderCount(), numOrders);
}

// Test callback registration (uses separate OrderBook without persistence)
TEST_F(OrderBookTest, UpdateCallbacks) {
  // Create OrderBook without persistence to avoid deadlock in test
  OrderBook testOrderBook("BTC-USD", false);
  std::atomic<int> callbackCount(0);

  // Register update callback with atomic counter
  testOrderBook.registerUpdateCallback([&callbackCount](const OrderBook&) {
    callbackCount.fetch_add(1, std::memory_order_relaxed);
  });

  // Add orders
  testOrderBook.addOrder(createBuyOrder(10000.0, 1.0));
  testOrderBook.addOrder(createSellOrder(10100.0, 1.0));

  // Verify callback was called
  EXPECT_EQ(callbackCount.load(), 2);

  // Cancel order
  auto buyOrder = createBuyOrder(9900.0, 1.0);
  testOrderBook.addOrder(buyOrder);
  testOrderBook.cancelOrder(buyOrder->getOrderId());

  // Verify callback was called again
  EXPECT_EQ(callbackCount.load(), 4);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
