#include "../../core/routing/OrderRouter.h"
#include "../../core/utils/TimeUtils.h"

#include <benchmark/benchmark.h>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace pinnacle::core::routing;
using namespace pinnacle;

// Helper function to create market data
std::vector<MarketData> createTestMarketData() {
  return {{"Coinbase", 50000.0, 50100.0, 10.0, 8.0,
           utils::TimeUtils::getCurrentNanos(), 1000.0, 500.0, 0.001, 0.005},
          {"Binance", 49990.0, 50090.0, 15.0, 12.0,
           utils::TimeUtils::getCurrentNanos(), 1200.0, 600.0, 0.0008, 0.003},
          {"Kraken", 50010.0, 50110.0, 8.0, 6.0,
           utils::TimeUtils::getCurrentNanos(), 800.0, 400.0, 0.0015, 0.004},
          {"Gemini", 50005.0, 50105.0, 12.0, 9.0,
           utils::TimeUtils::getCurrentNanos(), 900.0, 450.0, 0.0012, 0.0035}};
}

// Helper function to create execution request
ExecutionRequest createTestExecutionRequest(const std::string& strategy,
                                            double quantity) {
  ExecutionRequest request;
  request.requestId =
      "BENCH_REQ_" + std::to_string(utils::TimeUtils::getCurrentNanos());
  request.order = Order("BENCH_ORDER_" +
                            std::to_string(utils::TimeUtils::getCurrentNanos()),
                        "BTC-USD", OrderSide::BUY, OrderType::LIMIT, 50000.0,
                        quantity, utils::TimeUtils::getCurrentNanos());
  request.routingStrategy = strategy;
  request.maxSlippage = 0.001;
  request.allowPartialFills = true;
  return request;
}

// =============================================================================
// Strategy Planning Benchmarks
// =============================================================================

static void BM_BestPriceStrategy_Planning(benchmark::State& state) {
  BestPriceStrategy strategy;
  auto marketData = createTestMarketData();
  auto request = createTestExecutionRequest("BEST_PRICE", 1.0);

  for (auto _ : state) {
    auto results = strategy.planExecution(request, marketData);
    benchmark::DoNotOptimize(results);
  }
}

static void BM_TWAPStrategy_Planning(benchmark::State& state) {
  TWAPStrategy strategy(static_cast<int>(state.range(0)),
                        std::chrono::seconds(30));
  auto marketData = createTestMarketData();
  auto request = createTestExecutionRequest("TWAP", 10.0);

  for (auto _ : state) {
    auto results = strategy.planExecution(request, marketData);
    benchmark::DoNotOptimize(results);
  }
}

static void BM_VWAPStrategy_Planning(benchmark::State& state) {
  VWAPStrategy strategy(0.15); // 15% participation rate
  auto marketData = createTestMarketData();
  auto request = createTestExecutionRequest("VWAP", 5.0);

  for (auto _ : state) {
    auto results = strategy.planExecution(request, marketData);
    benchmark::DoNotOptimize(results);
  }
}

static void BM_MarketImpactStrategy_Planning(benchmark::State& state) {
  MarketImpactStrategy strategy(0.003); // 0.3% threshold
  auto marketData = createTestMarketData();
  auto request = createTestExecutionRequest("MARKET_IMPACT", 20.0);

  for (auto _ : state) {
    auto results = strategy.planExecution(request, marketData);
    benchmark::DoNotOptimize(results);
  }
}

// =============================================================================
// OrderRouter End-to-End Benchmarks
// =============================================================================

static void BM_OrderRouter_SubmitOrder(benchmark::State& state) {
  OrderRouter router;
  router.initialize();
  router.start();

  // Add test venues
  router.addVenue("TestExchange1", "websocket");
  router.addVenue("TestExchange2", "websocket");
  router.addVenue("TestExchange3", "fix");

  // Update market data
  auto marketData = createTestMarketData();
  for (const auto& data : marketData) {
    router.updateMarketData(data.venue, data);
  }

  router.setRoutingStrategy("BEST_PRICE");

  for (auto _ : state) {
    auto request = createTestExecutionRequest("BEST_PRICE", 1.0);

    state.PauseTiming();
    // Create new request outside timing to avoid ID conflicts
    request.requestId = "BENCH_REQ_" +
                        std::to_string(utils::TimeUtils::getCurrentNanos()) +
                        "_" + std::to_string(state.iterations());
    request.order =
        Order(request.requestId, "BTC-USD", OrderSide::BUY, OrderType::LIMIT,
              50000.0, 1.0, utils::TimeUtils::getCurrentNanos());
    state.ResumeTiming();

    std::string requestId = router.submitOrder(std::move(request));
    benchmark::DoNotOptimize(requestId);
  }

  router.stop();
}

static void BM_OrderRouter_MultipleStrategies(benchmark::State& state) {
  OrderRouter router;
  router.initialize();
  router.start();

  // Add test venues
  router.addVenue("Exchange1", "websocket");
  router.addVenue("Exchange2", "fix");

  // Update market data
  auto marketData = createTestMarketData();
  for (const auto& data : marketData) {
    router.updateMarketData(data.venue, data);
  }

  std::vector<std::string> strategies = {"BEST_PRICE", "TWAP", "VWAP",
                                         "MARKET_IMPACT"};
  size_t strategyIndex = 0;

  for (auto _ : state) {
    std::string strategy = strategies[strategyIndex % strategies.size()];
    router.setRoutingStrategy(strategy);

    auto request = createTestExecutionRequest(strategy, 2.0);
    request.requestId = "MULTI_BENCH_" +
                        std::to_string(utils::TimeUtils::getCurrentNanos()) +
                        "_" + std::to_string(strategyIndex);

    std::string requestId = router.submitOrder(std::move(request));
    benchmark::DoNotOptimize(requestId);

    strategyIndex++;
  }

  router.stop();
}

// =============================================================================
// Market Data Processing Benchmarks
// =============================================================================

static void BM_OrderRouter_MarketDataUpdate(benchmark::State& state) {
  OrderRouter router;
  router.initialize();
  router.start();

  router.addVenue("TestVenue", "websocket");

  std::mt19937 rng(42);
  std::uniform_real_distribution<double> priceVariation(0.99, 1.01);
  std::uniform_real_distribution<double> sizeVariation(5.0, 15.0);

  MarketData baseData;
  baseData.venue = "TestVenue";
  baseData.bidPrice = 50000.0;
  baseData.askPrice = 50100.0;
  baseData.fees = 0.001;
  baseData.impactCost = 0.002;
  baseData.averageDailyVolume = 1000.0;
  baseData.recentVolume = 500.0;

  for (auto _ : state) {
    MarketData data = baseData;
    data.bidPrice *= priceVariation(rng);
    data.askPrice *= priceVariation(rng);
    data.bidSize = sizeVariation(rng);
    data.askSize = sizeVariation(rng);
    data.timestamp = utils::TimeUtils::getCurrentNanos();

    router.updateMarketData("TestVenue", data);
  }

  router.stop();
}

// =============================================================================
// Large Order Routing Benchmarks
// =============================================================================

static void BM_OrderRouter_LargeOrderTWAP(benchmark::State& state) {
  OrderRouter router;
  router.initialize();
  router.start();

  router.addVenue("LiquidVenue", "websocket");
  router.setRoutingStrategy("TWAP");

  // High liquidity market data
  MarketData liquidData;
  liquidData.venue = "LiquidVenue";
  liquidData.bidPrice = 50000.0;
  liquidData.askPrice = 50100.0;
  liquidData.bidSize = 100.0;
  liquidData.askSize = 100.0;
  liquidData.timestamp = utils::TimeUtils::getCurrentNanos();
  liquidData.averageDailyVolume = 10000.0;
  liquidData.recentVolume = 5000.0;
  liquidData.impactCost = 0.001;
  liquidData.fees = 0.003;

  router.updateMarketData("LiquidVenue", liquidData);

  for (auto _ : state) {
    // Large order that will be split by TWAP
    auto request =
        createTestExecutionRequest("TWAP", static_cast<double>(state.range(0)));
    request.requestId =
        "LARGE_TWAP_" + std::to_string(utils::TimeUtils::getCurrentNanos());

    std::string requestId = router.submitOrder(std::move(request));
    benchmark::DoNotOptimize(requestId);
  }

  router.stop();
}

static void BM_OrderRouter_MultiVenueVWAP(benchmark::State& state) {
  OrderRouter router;
  router.initialize();
  router.start();

  // Add multiple venues
  std::vector<std::string> venues = {"Venue1", "Venue2", "Venue3", "Venue4"};
  for (const auto& venue : venues) {
    router.addVenue(venue, "websocket");
  }

  router.setRoutingStrategy("VWAP");

  // Update market data for all venues
  auto marketDataList = createTestMarketData();
  for (size_t i = 0; i < venues.size() && i < marketDataList.size(); ++i) {
    auto data = marketDataList[i];
    data.venue = venues[i];
    router.updateMarketData(venues[i], data);
  }

  for (auto _ : state) {
    auto request = createTestExecutionRequest("VWAP", 10.0);
    request.requestId =
        "MULTI_VWAP_" + std::to_string(utils::TimeUtils::getCurrentNanos());

    std::string requestId = router.submitOrder(std::move(request));
    benchmark::DoNotOptimize(requestId);
  }

  router.stop();
}

// =============================================================================
// Concurrent Routing Benchmarks
// =============================================================================

static void BM_OrderRouter_ConcurrentSubmissions(benchmark::State& state) {
  OrderRouter router;
  router.initialize();
  router.start();

  router.addVenue("ConcurrentVenue", "websocket");
  router.setRoutingStrategy("BEST_PRICE");

  auto marketData = createTestMarketData()[0];
  marketData.venue = "ConcurrentVenue";
  router.updateMarketData("ConcurrentVenue", marketData);

  std::atomic<int> requestCounter{0};

  for (auto _ : state) {
    state.SetItemsProcessed(state.range(0));

    std::vector<std::thread> threads;

    for (int t = 0; t < state.range(0); ++t) {
      threads.emplace_back([&router, &requestCounter]() {
        int reqId = requestCounter.fetch_add(1);
        auto request = createTestExecutionRequest("BEST_PRICE", 0.1);
        request.requestId = "CONCURRENT_" + std::to_string(reqId);

        std::string submittedId = router.submitOrder(std::move(request));
        benchmark::DoNotOptimize(submittedId);
      });
    }

    for (auto& thread : threads) {
      thread.join();
    }
  }

  router.stop();
}

// =============================================================================
// Register Benchmarks
// =============================================================================

// Strategy planning benchmarks
BENCHMARK(BM_BestPriceStrategy_Planning);
BENCHMARK(BM_TWAPStrategy_Planning)->Arg(5)->Arg(10)->Arg(20);
BENCHMARK(BM_VWAPStrategy_Planning);
BENCHMARK(BM_MarketImpactStrategy_Planning);

// OrderRouter core benchmarks
BENCHMARK(BM_OrderRouter_SubmitOrder);
BENCHMARK(BM_OrderRouter_MultipleStrategies);
BENCHMARK(BM_OrderRouter_MarketDataUpdate);

// Large order benchmarks
BENCHMARK(BM_OrderRouter_LargeOrderTWAP)->Arg(10)->Arg(50)->Arg(100);
BENCHMARK(BM_OrderRouter_MultiVenueVWAP);

// Concurrent benchmarks
BENCHMARK(BM_OrderRouter_ConcurrentSubmissions)
    ->Arg(2)
    ->Arg(4)
    ->Arg(8)
    ->Arg(16);

BENCHMARK_MAIN();
