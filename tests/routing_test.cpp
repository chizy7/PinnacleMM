#include "../core/routing/OrderRouter.h"
#include "../core/utils/TimeUtils.h"

#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

using namespace pinnacle::core::routing;
using namespace pinnacle;

void testBestPriceStrategy() {
  std::cout << "Testing BestPriceStrategy..." << std::endl;

  BestPriceStrategy strategy;

  // Create test execution request
  Order testOrder("TEST_001", "BTC-USD", OrderSide::BUY, OrderType::LIMIT,
                  50000.0, 1.0, utils::TimeUtils::getCurrentNanos());
  ExecutionRequest request;
  request.requestId = "REQ_001";
  request.order = std::move(testOrder);

  // Create market data for different venues
  std::vector<MarketData> marketData = {
      {"Coinbase", 50100.0, 50200.0, 5.0, 3.0,
       utils::TimeUtils::getCurrentNanos(), 1000.0, 500.0, 0.002, 0.005},
      {"Binance", 50050.0, 50150.0, 8.0, 6.0,
       utils::TimeUtils::getCurrentNanos(), 1200.0, 600.0, 0.001, 0.003},
      {"Kraken", 50080.0, 50180.0, 4.0, 2.0,
       utils::TimeUtils::getCurrentNanos(), 800.0, 400.0, 0.0015, 0.004}};

  auto results = strategy.planExecution(request, marketData);

  assert(results.size() == 1);
  assert(results[0].targetVenue ==
         "Binance"); // Should select Binance (lowest ask + fees)

  std::cout << "✓ BestPriceStrategy selected venue: " << results[0].targetVenue
            << std::endl;
}

void testTWAPStrategy() {
  std::cout << "Testing TWAPStrategy..." << std::endl;

  TWAPStrategy strategy(5,
                        std::chrono::seconds(10)); // 5 slices, 10 seconds apart

  Order testOrder("TEST_002", "ETH-USD", OrderSide::SELL, OrderType::LIMIT,
                  3000.0, 10.0, utils::TimeUtils::getCurrentNanos());
  ExecutionRequest request;
  request.requestId = "REQ_002";
  request.order = std::move(testOrder);

  std::vector<MarketData> marketData = {{"Coinbase", 2990.0, 3010.0, 20.0, 15.0,
                                         utils::TimeUtils::getCurrentNanos(),
                                         500.0, 250.0, 0.001, 0.005}};

  auto results = strategy.planExecution(request, marketData);

  assert(results.size() == 5); // Should create 5 slices

  double totalQuantity = 0;
  for (const auto &result : results) {
    totalQuantity += result.order.getQuantity();
    assert(result.targetVenue == "Coinbase");
  }

  assert(std::abs(totalQuantity - 10.0) <
         0.001); // Total should equal original quantity

  std::cout << "✓ TWAPStrategy created " << results.size()
            << " slices, total quantity: " << totalQuantity << std::endl;
}

void testVWAPStrategy() {
  std::cout << "Testing VWAPStrategy..." << std::endl;

  VWAPStrategy strategy(0.2); // 20% participation rate

  Order testOrder("TEST_003", "BTC-USD", OrderSide::BUY, OrderType::LIMIT,
                  50000.0, 5.0, utils::TimeUtils::getCurrentNanos());
  ExecutionRequest request;
  request.requestId = "REQ_003";
  request.order = std::move(testOrder);

  std::vector<MarketData> marketData = {
      {"Venue1", 50000.0, 50100.0, 10.0, 8.0,
       utils::TimeUtils::getCurrentNanos(), 1000.0, 500.0, 0.001, 0.003},
      {"Venue2", 50010.0, 50110.0, 12.0, 10.0,
       utils::TimeUtils::getCurrentNanos(), 2000.0, 1000.0, 0.0015, 0.004},
      {"Venue3", 50020.0, 50120.0, 8.0, 6.0,
       utils::TimeUtils::getCurrentNanos(), 500.0, 250.0, 0.002, 0.005}};

  auto results = strategy.planExecution(request, marketData);

  assert(!results.empty());

  double totalAllocated = 0;
  for (const auto &result : results) {
    totalAllocated += result.order.getQuantity();
    std::cout << "  Venue: " << result.targetVenue
              << ", Quantity: " << result.order.getQuantity() << std::endl;
  }

  std::cout << "✓ VWAPStrategy allocated total quantity: " << totalAllocated
            << std::endl;
}

void testMarketImpactStrategy() {
  std::cout << "Testing MarketImpactStrategy..." << std::endl;

  MarketImpactStrategy strategy(0.003); // 0.3% max impact threshold

  Order testOrder("TEST_004", "ETH-USD", OrderSide::SELL, OrderType::LIMIT,
                  3000.0, 20.0, utils::TimeUtils::getCurrentNanos());
  ExecutionRequest request;
  request.requestId = "REQ_004";
  request.order = std::move(testOrder);

  std::vector<MarketData> marketData = {
      {"LowImpact", 2990.0, 3010.0, 100.0, 80.0,
       utils::TimeUtils::getCurrentNanos(), 1000.0, 500.0, 0.002,
       0.003}, // Low impact
      {"HighImpact", 2995.0, 3015.0, 50.0, 30.0,
       utils::TimeUtils::getCurrentNanos(), 800.0, 400.0, 0.005,
       0.004}, // High impact
      {"MediumImpact", 2992.0, 3012.0, 60.0, 40.0,
       utils::TimeUtils::getCurrentNanos(), 600.0, 300.0, 0.0025, 0.0035}
      // Medium impact
  };

  auto results = strategy.planExecution(request, marketData);

  assert(!results.empty());

  for (const auto &result : results) {
    std::cout << "  Venue: " << result.targetVenue
              << ", Quantity: " << result.order.getQuantity() << std::endl;
  }

  std::cout << "✓ MarketImpactStrategy created " << results.size() << " orders"
            << std::endl;
}

void testOrderRouterBasicFunctionality() {
  std::cout << "Testing OrderRouter basic functionality..." << std::endl;

  OrderRouter router;

  // Test initialization and startup
  assert(router.initialize());
  assert(router.start());
  assert(router.isRunning());

  // Add some test venues
  assert(router.addVenue("TestExchange1", "websocket"));
  assert(router.addVenue("TestExchange2", "fix"));

  // Update market data
  MarketData testData;
  testData.venue = "TestExchange1";
  testData.bidPrice = 50000.0;
  testData.askPrice = 50100.0;
  testData.bidSize = 10.0;
  testData.askSize = 8.0;
  testData.timestamp = utils::TimeUtils::getCurrentNanos();
  testData.fees = 0.001;
  testData.impactCost = 0.002;

  router.updateMarketData("TestExchange1", testData);

  // Test strategy setting
  router.setRoutingStrategy("TWAP");

  // Create and submit a test order
  Order testOrder("ORDER_001", "BTC-USD", OrderSide::BUY, OrderType::LIMIT,
                  50000.0, 2.0, utils::TimeUtils::getCurrentNanos());
  ExecutionRequest request;
  request.requestId = "REQ_ROUTER_001";
  request.order = std::move(testOrder);
  request.routingStrategy = "TWAP";

  std::string submittedId = router.submitOrder(request);
  assert(!submittedId.empty());

  std::cout << "  Submitted order with ID: " << submittedId << std::endl;

  // Let the router process for a short time
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Check statistics
  std::string stats = router.getStatistics();
  std::cout << "  Router Statistics:\n" << stats << std::endl;

  // Test cancellation
  assert(router.cancelOrder(submittedId));

  // Stop the router
  assert(router.stop());
  assert(!router.isRunning());

  std::cout << "✓ OrderRouter basic functionality test passed" << std::endl;
}

void testOrderRouterMultipleStrategies() {
  std::cout << "Testing OrderRouter with multiple strategies..." << std::endl;

  OrderRouter router;
  router.initialize();
  router.start();

  // Add venues with different characteristics
  router.addVenue("FastVenue", "websocket");
  router.addVenue("CheapVenue", "websocket");
  router.addVenue("LiquidVenue", "fix");

  // Update market data for all venues
  MarketData fastVenue = {"FastVenue", 50000.0,
                          50100.0,     5.0,
                          4.0,         utils::TimeUtils::getCurrentNanos(),
                          500.0,       250.0,
                          0.001,       0.005};
  MarketData cheapVenue = {"CheapVenue", 50010.0,
                           50090.0,      8.0,
                           6.0,          utils::TimeUtils::getCurrentNanos(),
                           600.0,        300.0,
                           0.002,        0.001};
  MarketData liquidVenue = {"LiquidVenue", 50005.0,
                            50095.0,       20.0,
                            18.0,          utils::TimeUtils::getCurrentNanos(),
                            1000.0,        800.0,
                            0.0015,        0.003};

  router.updateMarketData("FastVenue", fastVenue);
  router.updateMarketData("CheapVenue", cheapVenue);
  router.updateMarketData("LiquidVenue", liquidVenue);

  // Test different strategies
  std::vector<std::string> strategies = {"BEST_PRICE", "TWAP", "VWAP",
                                         "MARKET_IMPACT"};

  for (const auto &strategy : strategies) {
    router.setRoutingStrategy(strategy);

    Order testOrder("ORDER_" + strategy, "BTC-USD", OrderSide::BUY,
                    OrderType::LIMIT, 50000.0, 5.0,
                    utils::TimeUtils::getCurrentNanos());
    ExecutionRequest request;
    request.requestId = "REQ_" + strategy;
    request.order = std::move(testOrder);
    request.routingStrategy = strategy;

    std::string submittedId = router.submitOrder(request);
    assert(!submittedId.empty());

    std::cout << "  Submitted " << strategy << " order with ID: " << submittedId
              << std::endl;
  }

  // Let the router process
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  std::cout << "  Final Statistics:\n" << router.getStatistics() << std::endl;

  router.stop();
  std::cout << "✓ Multiple strategies test passed" << std::endl;
}

int main() {
  std::cout << "=== OrderRouter Test Suite ===" << std::endl;

  try {
    // Test individual strategies
    testBestPriceStrategy();
    testTWAPStrategy();
    testVWAPStrategy();
    testMarketImpactStrategy();

    // Test router functionality
    testOrderRouterBasicFunctionality();
    testOrderRouterMultipleStrategies();

    std::cout << "\n All OrderRouter tests passed successfully!" << std::endl;
    return 0;

  } catch (const std::exception &e) {
    std::cerr << "\n Test failed with exception: " << e.what() << std::endl;
    return 1;
  } catch (...) {
    std::cerr << "\n Test failed with unknown exception" << std::endl;
    return 1;
  }
}