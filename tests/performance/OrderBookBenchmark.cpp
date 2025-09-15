#include "../../core/orderbook/LockFreeOrderBook.h"
#include "../../core/orderbook/OrderBook.h"
#include "../../core/persistence/PersistenceManager.h"
#include "../../core/utils/TimeUtils.h"

#include <benchmark/benchmark.h>
#include <chrono>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace pinnacle;

// Helper functions to create orders
std::shared_ptr<Order> createOrder(const std::string& id,
                                   const std::string& symbol, OrderSide side,
                                   double price, double quantity) {
  return std::make_shared<Order>(id, symbol, side, OrderType::LIMIT, price,
                                 quantity, utils::TimeUtils::getCurrentNanos());
}

// Benchmark for adding orders to mutex-based OrderBook
static void BM_OrderBook_AddOrder(benchmark::State& state) {
  // Set up the order book
  auto orderBook = std::make_shared<OrderBook>("BTC-USD");

  // Random number generator
  std::mt19937 rng(42); // Fixed seed for reproducibility
  std::uniform_real_distribution<double> priceDist(9000.0, 11000.0);
  std::uniform_real_distribution<double> quantityDist(0.1, 1.0);
  std::bernoulli_distribution sideDist(0.5);

  for (auto _ : state) {
    state.PauseTiming();

    // Generate a batch of orders
    std::vector<std::shared_ptr<Order>> orders;
    for (int i = 0; i < state.range(0); ++i) {
      OrderSide side = sideDist(rng) ? OrderSide::BUY : OrderSide::SELL;
      double price = priceDist(rng);
      double quantity = quantityDist(rng);
      std::string orderId = "order-" + std::to_string(i);
      orders.push_back(createOrder(orderId, "BTC-USD", side, price, quantity));
    }

    state.ResumeTiming();

    // Add orders to the order book
    for (const auto& order : orders) {
      orderBook->addOrder(order);
    }

    state.PauseTiming();
    orderBook->clear();
    state.ResumeTiming();
  }
}

// Benchmark for adding orders to lock-free OrderBook
static void BM_LockFreeOrderBook_AddOrder(benchmark::State& state) {
  // Set up the order book
  auto orderBook = std::make_shared<LockFreeOrderBook>("BTC-USD");

  // Random number generator
  std::mt19937 rng(42); // Fixed seed for reproducibility
  std::uniform_real_distribution<double> priceDist(9000.0, 11000.0);
  std::uniform_real_distribution<double> quantityDist(0.1, 1.0);
  std::bernoulli_distribution sideDist(0.5);

  for (auto _ : state) {
    state.PauseTiming();

    // Generate a batch of orders
    std::vector<std::shared_ptr<Order>> orders;
    for (int i = 0; i < state.range(0); ++i) {
      OrderSide side = sideDist(rng) ? OrderSide::BUY : OrderSide::SELL;
      double price = priceDist(rng);
      double quantity = quantityDist(rng);
      std::string orderId = "order-" + std::to_string(i);
      orders.push_back(createOrder(orderId, "BTC-USD", side, price, quantity));
    }

    state.ResumeTiming();

    // Add orders to the order book
    for (const auto& order : orders) {
      orderBook->addOrder(order);
    }

    state.PauseTiming();
    orderBook->clear();
    state.ResumeTiming();
  }
}

// Benchmark for executing market orders on mutex-based OrderBook
static void BM_OrderBook_ExecuteMarketOrder(benchmark::State& state) {
  // Set up the order book
  auto orderBook = std::make_shared<OrderBook>("BTC-USD");

  // Random number generator
  std::mt19937 rng(42); // Fixed seed for reproducibility
  std::uniform_real_distribution<double> bidPriceDist(9000.0, 9900.0);
  std::uniform_real_distribution<double> askPriceDist(10100.0, 11000.0);
  std::uniform_real_distribution<double> quantityDist(0.1, 1.0);

  // Pre-populate the order book with limit orders
  for (int i = 0; i < 1000; ++i) {
    // Add buy orders
    double bidPrice = bidPriceDist(rng);
    double bidQuantity = quantityDist(rng);
    std::string bidOrderId = "bid-" + std::to_string(i);
    orderBook->addOrder(createOrder(bidOrderId, "BTC-USD", OrderSide::BUY,
                                    bidPrice, bidQuantity));

    // Add sell orders
    double askPrice = askPriceDist(rng);
    double askQuantity = quantityDist(rng);
    std::string askOrderId = "ask-" + std::to_string(i);
    orderBook->addOrder(createOrder(askOrderId, "BTC-USD", OrderSide::SELL,
                                    askPrice, askQuantity));
  }

  for (auto _ : state) {
    // Execute a market order
    std::vector<std::pair<std::string, double>> fills;
    orderBook->executeMarketOrder(OrderSide::BUY, 0.5, fills);
  }
}

// Benchmark for executing market orders on lock-free OrderBook
static void BM_LockFreeOrderBook_ExecuteMarketOrder(benchmark::State& state) {
  // Set up the order book
  auto orderBook = std::make_shared<LockFreeOrderBook>("BTC-USD");

  // Random number generator
  std::mt19937 rng(42); // Fixed seed for reproducibility
  std::uniform_real_distribution<double> bidPriceDist(9000.0, 9900.0);
  std::uniform_real_distribution<double> askPriceDist(10100.0, 11000.0);
  std::uniform_real_distribution<double> quantityDist(0.1, 1.0);

  // Pre-populate the order book with limit orders
  for (int i = 0; i < 1000; ++i) {
    // Add buy orders
    double bidPrice = bidPriceDist(rng);
    double bidQuantity = quantityDist(rng);
    std::string bidOrderId = "bid-" + std::to_string(i);
    orderBook->addOrder(createOrder(bidOrderId, "BTC-USD", OrderSide::BUY,
                                    bidPrice, bidQuantity));

    // Add sell orders
    double askPrice = askPriceDist(rng);
    double askQuantity = quantityDist(rng);
    std::string askOrderId = "ask-" + std::to_string(i);
    orderBook->addOrder(createOrder(askOrderId, "BTC-USD", OrderSide::SELL,
                                    askPrice, askQuantity));
  }

  for (auto _ : state) {
    // Execute a market order
    std::vector<std::pair<std::string, double>> fills;
    orderBook->executeMarketOrder(OrderSide::BUY, 0.5, fills);
  }
}

// Benchmark for concurrent operations on mutex-based OrderBook
static void BM_OrderBook_ConcurrentOperations(benchmark::State& state) {
  // Set up the order book
  auto orderBook = std::make_shared<OrderBook>("BTC-USD");

  // Random number generator for setup only
  std::mt19937 rng(42); // Fixed seed for reproducibility
  std::uniform_real_distribution<double> priceDist(9000.0, 11000.0);
  std::uniform_real_distribution<double> quantityDist(0.1, 1.0);
  std::bernoulli_distribution sideDist(0.5);

  // Pre-populate the order book with some orders
  for (int i = 0; i < 1000; ++i) {
    OrderSide side = sideDist(rng) ? OrderSide::BUY : OrderSide::SELL;
    double price = priceDist(rng);
    double quantity = quantityDist(rng);
    std::string orderId = "order-" + std::to_string(i);
    orderBook->addOrder(createOrder(orderId, "BTC-USD", side, price, quantity));
  }

  for (auto _ : state) {
    state.PauseTiming();

    // Number of threads to use
    int numThreads = state.range(0);
    std::vector<std::thread> threads;
    std::atomic<int> threadCounter(0);

    state.ResumeTiming();

    // Launch threads
    for (int t = 0; t < numThreads; ++t) {
      threads.emplace_back([&orderBook, &threadCounter, t]() {
        // Each thread needs its own RNG to avoid race conditions
        std::mt19937 localRng(42 + t); // Different seed per thread
        std::uniform_real_distribution<double> localPriceDist(9000.0, 11000.0);
        std::uniform_real_distribution<double> localQuantityDist(0.1, 1.0);
        std::bernoulli_distribution localSideDist(0.5);

        int localCounter =
            threadCounter.fetch_add(1, std::memory_order_relaxed);

        // Each thread performs a mix of operations
        for (int i = 0; i < 100; ++i) {
          int op = i % 3; // Cycle through different operations

          if (op == 0) {
            // Add a new order
            OrderSide side =
                localSideDist(localRng) ? OrderSide::BUY : OrderSide::SELL;
            double price = localPriceDist(localRng);
            double quantity = localQuantityDist(localRng);
            std::string orderId =
                "t" + std::to_string(t) + "-order-" + std::to_string(i);
            orderBook->addOrder(
                createOrder(orderId, "BTC-USD", side, price, quantity));
          } else if (op == 1) {
            // Query the order book
            orderBook->getBestBidPrice();
            orderBook->getBestAskPrice();
            orderBook->getMidPrice();
            orderBook->getSpread();
          } else {
            // Execute a market order
            std::vector<std::pair<std::string, double>> fills;
            orderBook->executeMarketOrder(
                localSideDist(localRng) ? OrderSide::BUY : OrderSide::SELL,
                localQuantityDist(localRng), fills);
          }
        }
      });
    }

    // Wait for all threads to finish
    for (auto& thread : threads) {
      thread.join();
    }

    state.PauseTiming();
    orderBook->clear();
    state.ResumeTiming();
  }
}

// Benchmark for concurrent operations on lock-free OrderBook
static void BM_LockFreeOrderBook_ConcurrentOperations(benchmark::State& state) {
  // Set up the order book
  auto orderBook = std::make_shared<LockFreeOrderBook>("BTC-USD");

  // Random number generator
  std::mt19937 rng(42); // Fixed seed for reproducibility
  std::uniform_real_distribution<double> priceDist(9000.0, 11000.0);
  std::uniform_real_distribution<double> quantityDist(0.1, 1.0);
  std::bernoulli_distribution sideDist(0.5);

  // Pre-populate the order book with some orders
  for (int i = 0; i < 1000; ++i) {
    OrderSide side = sideDist(rng) ? OrderSide::BUY : OrderSide::SELL;
    double price = priceDist(rng);
    double quantity = quantityDist(rng);
    std::string orderId = "order-" + std::to_string(i);
    orderBook->addOrder(createOrder(orderId, "BTC-USD", side, price, quantity));
  }

  for (auto _ : state) {
    state.PauseTiming();

    // Number of threads to use
    int numThreads = state.range(0);
    std::vector<std::thread> threads;
    std::atomic<int> threadCounter(0);

    state.ResumeTiming();

    // Launch threads
    for (int t = 0; t < numThreads; ++t) {
      threads.emplace_back([&orderBook, &threadCounter, t]() {
        // Each thread needs its own RNG to avoid race conditions
        std::mt19937 localRng(42 + t); // Different seed per thread
        std::uniform_real_distribution<double> localPriceDist(9000.0, 11000.0);
        std::uniform_real_distribution<double> localQuantityDist(0.1, 1.0);
        std::bernoulli_distribution localSideDist(0.5);

        int localCounter =
            threadCounter.fetch_add(1, std::memory_order_relaxed);

        // Each thread performs a mix of operations
        for (int i = 0; i < 100; ++i) {
          int op = i % 3; // Cycle through different operations

          if (op == 0) {
            // Add a new order
            OrderSide side =
                localSideDist(localRng) ? OrderSide::BUY : OrderSide::SELL;
            double price = localPriceDist(localRng);
            double quantity = localQuantityDist(localRng);
            std::string orderId =
                "t" + std::to_string(t) + "-order-" + std::to_string(i);
            orderBook->addOrder(
                createOrder(orderId, "BTC-USD", side, price, quantity));
          } else if (op == 1) {
            // Query the order book
            orderBook->getBestBidPrice();
            orderBook->getBestAskPrice();
            orderBook->getMidPrice();
            orderBook->getSpread();
          } else {
            // Execute a market order
            std::vector<std::pair<std::string, double>> fills;
            orderBook->executeMarketOrder(
                localSideDist(localRng) ? OrderSide::BUY : OrderSide::SELL,
                localQuantityDist(localRng), fills);
          }
        }
      });
    }

    // Wait for all threads to finish
    for (auto& thread : threads) {
      thread.join();
    }

    state.PauseTiming();
    orderBook->clear();
    state.ResumeTiming();
  }
}

// Register benchmarks
BENCHMARK(BM_OrderBook_AddOrder)->Arg(100)->Arg(1000)->Arg(10000);
BENCHMARK(BM_LockFreeOrderBook_AddOrder)->Arg(100)->Arg(1000)->Arg(10000);
BENCHMARK(BM_OrderBook_ExecuteMarketOrder);
BENCHMARK(BM_LockFreeOrderBook_ExecuteMarketOrder);
BENCHMARK(BM_OrderBook_ConcurrentOperations)->Arg(2)->Arg(4)->Arg(8)->Arg(16);
BENCHMARK(BM_LockFreeOrderBook_ConcurrentOperations)
    ->Arg(2)
    ->Arg(4)
    ->Arg(8)
    ->Arg(16);

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

  // Force garbage collection - any remaining order book references should be
  // released This is important because benchmark objects may still hold
  // references
  if (std::getenv("JOURNAL_PATH")) {
    // In CI environment, being extra careful about cleanup
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

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
