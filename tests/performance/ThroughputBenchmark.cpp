#include "../../core/orderbook/OrderBook.h"
#include "../../core/persistence/PersistenceManager.h"
#include "../../core/utils/TimeUtils.h"

#include <benchmark/benchmark.h>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace pinnacle;

// Global persistence initialization for benchmarks
class BenchmarkEnvironment : public benchmark::Environment {
public:
  void SetUp() override {
    auto tempDir = std::filesystem::temp_directory_path() / "pinnaclemm_bench";
    std::filesystem::create_directories(tempDir);
    auto& persistenceManager = persistence::PersistenceManager::getInstance();
    persistenceManager.initialize(tempDir.string());
  }

  void TearDown() override {
    auto tempDir = std::filesystem::temp_directory_path() / "pinnaclemm_bench";
    if (std::filesystem::exists(tempDir)) {
      std::filesystem::remove_all(tempDir);
    }
  }
};

// Benchmark for measuring order throughput (adds/cancels per second)
static void BM_OrderThroughput(benchmark::State& state) {
  // Setup order book
  auto orderBook = std::make_shared<OrderBook>("BTC-USD");
  std::vector<std::string> orderIds;
  std::random_device rd;
  std::mt19937 gen(rd());

  int orderId = 0;

  for (auto _ : state) {
    // Add a batch of orders
    for (int i = 0; i < state.range(0); ++i) {
      std::string id = "order-" + std::to_string(orderId++);
      auto order = std::make_shared<Order>(
          id, "BTC-USD", (i % 2 == 0) ? OrderSide::BUY : OrderSide::SELL,
          OrderType::LIMIT, 10000.0 + (i % 100), 1.0,
          utils::TimeUtils::getCurrentNanos());
      orderBook->addOrder(order);
      orderIds.push_back(id);
    }

    // Cancel a batch of orders
    if (!orderIds.empty()) {
      // Fix type mismatch with explicit template parameter
      for (int i = 0; i < std::min<int64_t>(state.range(0), orderIds.size());
           ++i) {
        std::uniform_int_distribution<> distrib(0, orderIds.size() - 1);
        int index = distrib(gen);
        orderBook->cancelOrder(orderIds[index]);
        orderIds.erase(orderIds.begin() + index);
      }
    }
  }

  // Calculate operations per second
  state.SetItemsProcessed(state.iterations() * state.range(0) *
                          2); // adds + cancels
}

// Benchmark for measuring market order execution throughput
static void BM_MarketOrderThroughput(benchmark::State& state) {
  // Setup order book with some limit orders
  auto orderBook = std::make_shared<OrderBook>("BTC-USD");

  // Add some limit orders to the book
  for (int i = 0; i < 1000; i++) {
    std::string id = "order-" + std::to_string(i);
    double price = 10000.0 + (i % 100);
    auto order = std::make_shared<Order>(
        id, "BTC-USD", (i % 2 == 0) ? OrderSide::BUY : OrderSide::SELL,
        OrderType::LIMIT, price, 1.0, utils::TimeUtils::getCurrentNanos());
    orderBook->addOrder(order);
  }

  for (auto _ : state) {
    // Execute market orders
    for (int i = 0; i < state.range(0); ++i) {
      std::vector<std::pair<std::string, double>> fills;
      OrderSide side = (i % 2 == 0) ? OrderSide::BUY : OrderSide::SELL;
      orderBook->executeMarketOrder(side, 0.1, fills);
    }
  }

  // Calculate operations per second
  state.SetItemsProcessed(state.iterations() * state.range(0));
}

// Register benchmarks
BENCHMARK(BM_OrderThroughput)->Arg(10)->Arg(100);
BENCHMARK(BM_MarketOrderThroughput)->Arg(10)->Arg(100);

int main(int argc, char** argv) {
  benchmark::RegisterGlobalEnvironment(new BenchmarkEnvironment);
  benchmark::Initialize(&argc, argv);
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
