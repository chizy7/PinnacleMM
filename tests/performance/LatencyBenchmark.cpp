#include "../../core/orderbook/OrderBook.h"
#include "../../core/persistence/PersistenceManager.h"
#include "../../core/utils/TimeUtils.h"

#include <benchmark/benchmark.h>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace pinnacle;

// Benchmark for measuring order addition latency
static void BM_OrderAddLatency(benchmark::State& state) {
  // Setup order book
  auto orderBook = std::make_shared<OrderBook>("BTC-USD");
  int orderId = 0;

  for (auto _ : state) {
    state.PauseTiming();
    // Create a new order outside timing
    std::string id = "order-" + std::to_string(orderId++);
    auto order = std::make_shared<Order>(id, "BTC-USD", OrderSide::BUY,
                                         OrderType::LIMIT, 10000.0, 1.0,
                                         utils::TimeUtils::getCurrentNanos());
    state.ResumeTiming();

    // Measure the time to add an order
    orderBook->addOrder(order);
  }
}

// Benchmark for measuring order book lookup latency
static void BM_OrderBookQueryLatency(benchmark::State& state) {
  // Setup order book with some orders
  auto orderBook = std::make_shared<OrderBook>("BTC-USD");

  // Add some orders to the book
  for (int i = 0; i < 1000; i++) {
    std::string id = "order-" + std::to_string(i);
    double price = 10000.0 + (i % 100);
    auto order = std::make_shared<Order>(
        id, "BTC-USD", (i % 2 == 0) ? OrderSide::BUY : OrderSide::SELL,
        OrderType::LIMIT, price, 1.0, utils::TimeUtils::getCurrentNanos());
    orderBook->addOrder(order);
  }

  for (auto _ : state) {
    // Measure the time to query best bid/ask
    volatile double bestBid = orderBook->getBestBidPrice();
    volatile double bestAsk = orderBook->getBestAskPrice();
    benchmark::DoNotOptimize(bestBid);
    benchmark::DoNotOptimize(bestAsk);
  }
}

// Register benchmarks
BENCHMARK(BM_OrderAddLatency);
BENCHMARK(BM_OrderBookQueryLatency);

int main(int argc, char** argv) {
  // Use environment variable for journal path if available, otherwise use temp
  // dir
  const char* journal_path = std::getenv("JOURNAL_PATH");
  std::filesystem::path tempDir;
  if (journal_path) {
    tempDir = std::filesystem::path(journal_path);
  } else {
    tempDir = std::filesystem::temp_directory_path() / "pinnaclemm_bench";
  }

  std::filesystem::create_directories(tempDir);
  auto& persistenceManager = persistence::PersistenceManager::getInstance();
  persistenceManager.initialize(tempDir.string());

  benchmark::Initialize(&argc, argv);
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();

  // Give threads time to complete and ensure all destructors run
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Properly shutdown persistence before cleanup
  persistenceManager.shutdown();

  // Small delay before directory cleanup
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  if (std::filesystem::exists(tempDir) && !journal_path) {
    // Only remove temp dir if we created it (not if user specified
    // JOURNAL_PATH)
    std::filesystem::remove_all(tempDir);
  }
  return 0;
}
