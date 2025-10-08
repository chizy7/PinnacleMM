#include "../../core/orderbook/OrderBook.h"
#include "../../core/utils/TimeUtils.h"
#include "../../strategies/analytics/MarketImpactPredictor.h"
#include "../../strategies/analytics/OrderBookAnalyzer.h"

#include <benchmark/benchmark.h>
#include <memory>
#include <random>
#include <vector>

using namespace pinnacle;
using namespace pinnacle::analytics;
using namespace pinnacle::utils;

class MarketImpactPredictionBenchmark : public benchmark::Fixture {
public:
  void SetUp(const ::benchmark::State& state) override {
    // Setup predictor with performance-oriented configuration
    symbol_ = "BTC-USD";
    maxHistorySize_ = 10000;
    modelUpdateInterval_ = 60000; // 1 minute

    predictor_ = std::make_unique<MarketImpactPredictor>(
        symbol_, maxHistorySize_, modelUpdateInterval_);

    // Create test order book with realistic data
    orderBook_ = std::make_shared<OrderBook>(symbol_);
    flowAnalyzer_ = std::make_shared<OrderBookAnalyzer>(symbol_, 1000, 1000);

    // Initialize predictor
    predictor_->initialize(orderBook_, flowAnalyzer_);
    predictor_->start();

    // Populate with realistic market data
    populateOrderBook();
    populateImpactHistory(1000);

    // Force model training for consistent benchmarks
    predictor_->retrainModel();
  }

  void TearDown(const ::benchmark::State& state) override {
    if (predictor_) {
      predictor_->stop();
    }
    predictor_.reset();
    flowAnalyzer_.reset();
    orderBook_.reset();
  }

protected:
  void populateOrderBook() {
    std::mt19937 gen(42); // Fixed seed for reproducible benchmarks
    std::uniform_real_distribution<double> priceDist(49900.0, 50100.0);
    std::uniform_real_distribution<double> sizeDist(0.1, 5.0);

    uint64_t timestamp = TimeUtils::getCurrentNanos();

    // Add 10 levels of bids and asks
    for (int i = 0; i < 10; ++i) {
      double bidPrice = 50000.0 - (i + 1) * 10.0;
      double askPrice = 50000.0 + (i + 1) * 10.0;
      double bidSize = sizeDist(gen);
      double askSize = sizeDist(gen);

      orderBook_->addOrder(std::make_shared<Order>(
          "bid_" + std::to_string(i), symbol_, OrderSide::BUY, OrderType::LIMIT,
          bidPrice, bidSize, timestamp));
      orderBook_->addOrder(std::make_shared<Order>(
          "ask_" + std::to_string(i), symbol_, OrderSide::SELL,
          OrderType::LIMIT, askPrice, askSize, timestamp));
    }
  }

  void populateImpactHistory(size_t count) {
    std::mt19937 gen(123); // Different seed for impact data
    std::uniform_real_distribution<double> sizeDist(0.1, 3.0);
    std::uniform_real_distribution<double> impactDist(0.5, 5.0);
    std::uniform_int_distribution<int> sideDist(0, 1);

    uint64_t baseTimestamp =
        TimeUtils::getCurrentNanos() - 3600000000000ULL; // 1 hour ago

    for (size_t i = 0; i < count; ++i) {
      double orderSize = sizeDist(gen);
      double priceImpact =
          impactDist(gen) * (orderSize / 2.0); // Impact correlated with size
      OrderSide side = sideDist(gen) == 0 ? OrderSide::BUY : OrderSide::SELL;

      double priceBefore = 50000.0 + (i % 100 - 50) * 0.1; // Slight price drift
      double priceAfter =
          priceBefore + (side == OrderSide::BUY ? priceImpact : -priceImpact);

      MarketImpactEvent event(baseTimestamp + i * 1000000ULL, // 1ms intervals
                              "order_" + std::to_string(i), side, orderSize,
                              priceBefore, priceAfter);

      predictor_->recordImpactEvent(event);
    }
  }

  MarketImpactEvent generateRandomImpactEvent() {
    static std::mt19937 gen(456);
    static std::uniform_real_distribution<double> sizeDist(0.1, 2.0);
    static std::uniform_real_distribution<double> impactDist(0.1, 3.0);
    static std::uniform_int_distribution<int> sideDist(0, 1);

    double orderSize = sizeDist(gen);
    double priceImpact = impactDist(gen);
    OrderSide side = sideDist(gen) == 0 ? OrderSide::BUY : OrderSide::SELL;

    double priceBefore = 50000.0;
    double priceAfter =
        priceBefore + (side == OrderSide::BUY ? priceImpact : -priceImpact);

    return MarketImpactEvent(TimeUtils::getCurrentNanos(), "bench_order", side,
                             orderSize, priceBefore, priceAfter);
  }

  std::string symbol_;
  size_t maxHistorySize_;
  uint64_t modelUpdateInterval_;
  std::unique_ptr<MarketImpactPredictor> predictor_;
  std::shared_ptr<OrderBook> orderBook_;
  std::shared_ptr<OrderBookAnalyzer> flowAnalyzer_;
};

// Core prediction performance benchmarks
BENCHMARK_F(MarketImpactPredictionBenchmark, BasicImpactPrediction)
(benchmark::State& state) {
  std::mt19937 gen(789);
  std::uniform_real_distribution<double> sizeDist(0.1, 3.0);
  std::uniform_real_distribution<double> urgencyDist(0.0, 1.0);
  std::uniform_int_distribution<int> sideDist(0, 1);

  for (auto _ : state) {
    double orderSize = sizeDist(gen);
    double urgency = urgencyDist(gen);
    OrderSide side = sideDist(gen) == 0 ? OrderSide::BUY : OrderSide::SELL;

    auto prediction = predictor_->predictImpact(side, orderSize, urgency);
    benchmark::DoNotOptimize(prediction);
  }

  state.SetItemsProcessed(state.iterations());
}

BENCHMARK_F(MarketImpactPredictionBenchmark, OptimalOrderSizing)
(benchmark::State& state) {
  std::mt19937 gen(101112);
  std::uniform_real_distribution<double> quantityDist(5.0, 50.0);
  std::uniform_real_distribution<double> impactDist(0.0005,
                                                    0.002); // 0.05% to 0.2%
  std::uniform_int_distribution<int> sideDist(0, 1);

  for (auto _ : state) {
    double totalQuantity = quantityDist(gen);
    double maxImpact = impactDist(gen);
    OrderSide side = sideDist(gen) == 0 ? OrderSide::BUY : OrderSide::SELL;

    auto recommendation =
        predictor_->getOptimalSizing(side, totalQuantity, maxImpact);
    benchmark::DoNotOptimize(recommendation);
  }

  state.SetItemsProcessed(state.iterations());
}

BENCHMARK_F(MarketImpactPredictionBenchmark, ExecutionCostCalculation)
(benchmark::State& state) {
  std::mt19937 gen(131415);
  std::uniform_real_distribution<double> quantityDist(0.5, 10.0);
  std::uniform_real_distribution<double> priceDist(49000.0, 51000.0);
  std::uniform_int_distribution<int> sideDist(0, 1);

  for (auto _ : state) {
    double quantity = quantityDist(gen);
    double midPrice = priceDist(gen);
    OrderSide side = sideDist(gen) == 0 ? OrderSide::BUY : OrderSide::SELL;

    auto cost = predictor_->calculateExecutionCost(side, quantity, midPrice);
    benchmark::DoNotOptimize(cost);
  }

  state.SetItemsProcessed(state.iterations());
}

BENCHMARK_F(MarketImpactPredictionBenchmark, HistoricalImpactAnalysis)
(benchmark::State& state) {
  std::vector<uint64_t> lookbackPeriods = {
      300000000000ULL,  // 5 minutes
      1800000000000ULL, // 30 minutes
      3600000000000ULL, // 1 hour
      7200000000000ULL  // 2 hours
  };

  size_t periodIndex = 0;

  for (auto _ : state) {
    uint64_t lookback = lookbackPeriods[periodIndex % lookbackPeriods.size()];
    auto analysis = predictor_->analyzeHistoricalImpact(lookback);
    benchmark::DoNotOptimize(analysis);
    periodIndex++;
  }

  state.SetItemsProcessed(state.iterations());
}

BENCHMARK_F(MarketImpactPredictionBenchmark, ImpactEventRecording)
(benchmark::State& state) {
  for (auto _ : state) {
    auto event = generateRandomImpactEvent();
    predictor_->recordImpactEvent(event);
    benchmark::DoNotOptimize(event);
  }

  state.SetItemsProcessed(state.iterations());
}

BENCHMARK_F(MarketImpactPredictionBenchmark, ModelRetraining)
(benchmark::State& state) {
  // Add more data between retraining runs
  for (auto _ : state) {
    // Add some new impact events
    for (int i = 0; i < 50; ++i) {
      auto event = generateRandomImpactEvent();
      predictor_->recordImpactEvent(event);
    }

    // Retrain model
    bool success = predictor_->retrainModel();
    benchmark::DoNotOptimize(success);
  }

  state.SetItemsProcessed(state.iterations());
}

BENCHMARK_F(MarketImpactPredictionBenchmark, MarketRegimeUpdate)
(benchmark::State& state) {
  std::mt19937 gen(161718);
  std::uniform_real_distribution<double> regimeDist(0.1, 2.0);

  for (auto _ : state) {
    double volatility = regimeDist(gen);
    double liquidity = regimeDist(gen);
    double momentum = regimeDist(gen);

    predictor_->updateMarketRegime(volatility, liquidity, momentum);
    benchmark::DoNotOptimize(volatility);
  }

  state.SetItemsProcessed(state.iterations());
}

// Stress tests and concurrent performance
BENCHMARK_F(MarketImpactPredictionBenchmark, HighFrequencyPredictions)
(benchmark::State& state) {
  std::mt19937 gen(192021);
  std::uniform_real_distribution<double> sizeDist(0.1, 1.0);

  for (auto _ : state) {
    // Simulate HFT scenario with rapid predictions
    for (int i = 0; i < 10; ++i) {
      auto prediction = predictor_->predictImpact(
          i % 2 == 0 ? OrderSide::BUY : OrderSide::SELL, sizeDist(gen));
      benchmark::DoNotOptimize(prediction);
    }
  }

  state.SetItemsProcessed(state.iterations() * 10);
}

BENCHMARK_F(MarketImpactPredictionBenchmark, StatisticsGeneration)
(benchmark::State& state) {
  for (auto _ : state) {
    auto stats = predictor_->getImpactStatistics();
    benchmark::DoNotOptimize(stats);
  }

  state.SetItemsProcessed(state.iterations());
}

// Latency-focused benchmarks (single operation timing)
BENCHMARK_F(MarketImpactPredictionBenchmark, PredictionLatency_SingleOp)
(benchmark::State& state) {
  for (auto _ : state) {
    auto start = std::chrono::high_resolution_clock::now();
    auto prediction = predictor_->predictImpact(OrderSide::BUY, 1.0, 0.5);
    auto end = std::chrono::high_resolution_clock::now();

    auto latency =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    state.SetIterationTime(latency.count() / 1e9); // Convert to seconds

    benchmark::DoNotOptimize(prediction);
  }

  state.SetLabel("ns per prediction");
}

BENCHMARK_F(MarketImpactPredictionBenchmark, OrderSizingLatency_SingleOp)
(benchmark::State& state) {
  for (auto _ : state) {
    auto start = std::chrono::high_resolution_clock::now();
    auto recommendation =
        predictor_->getOptimalSizing(OrderSide::BUY, 10.0, 0.001);
    auto end = std::chrono::high_resolution_clock::now();

    auto latency =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    state.SetIterationTime(latency.count() / 1e9); // Convert to seconds

    benchmark::DoNotOptimize(recommendation);
  }

  state.SetLabel("ns per sizing calculation");
}

// Memory and scalability benchmarks
BENCHMARK_F(MarketImpactPredictionBenchmark, MemoryScaling_ImpactEvents)
(benchmark::State& state) {
  size_t eventCount = state.range(0);

  for (auto _ : state) {
    state.PauseTiming();
    // Clear existing data
    predictor_->reset();

    // Add specified number of events
    for (size_t i = 0; i < eventCount; ++i) {
      auto event = generateRandomImpactEvent();
      predictor_->recordImpactEvent(event);
    }
    state.ResumeTiming();

    // Measure prediction performance with this amount of data
    auto prediction = predictor_->predictImpact(OrderSide::BUY, 1.0);
    benchmark::DoNotOptimize(prediction);
  }

  state.SetItemsProcessed(state.iterations());
  state.SetComplexityN(state.range(0));
}

// Register scaling benchmark with different data sizes
BENCHMARK_REGISTER_F(MarketImpactPredictionBenchmark,
                     MemoryScaling_ImpactEvents)
    ->Range(100, 10000)
    ->Complexity(benchmark::oN);

// Register other benchmarks
BENCHMARK_REGISTER_F(MarketImpactPredictionBenchmark, BasicImpactPrediction);
BENCHMARK_REGISTER_F(MarketImpactPredictionBenchmark, OptimalOrderSizing);
BENCHMARK_REGISTER_F(MarketImpactPredictionBenchmark, ExecutionCostCalculation);
BENCHMARK_REGISTER_F(MarketImpactPredictionBenchmark, HistoricalImpactAnalysis);
BENCHMARK_REGISTER_F(MarketImpactPredictionBenchmark, ImpactEventRecording);
BENCHMARK_REGISTER_F(MarketImpactPredictionBenchmark, ModelRetraining);
BENCHMARK_REGISTER_F(MarketImpactPredictionBenchmark, MarketRegimeUpdate);
BENCHMARK_REGISTER_F(MarketImpactPredictionBenchmark, HighFrequencyPredictions);
BENCHMARK_REGISTER_F(MarketImpactPredictionBenchmark, StatisticsGeneration);
BENCHMARK_REGISTER_F(MarketImpactPredictionBenchmark,
                     PredictionLatency_SingleOp)
    ->UseManualTime();
BENCHMARK_REGISTER_F(MarketImpactPredictionBenchmark,
                     OrderSizingLatency_SingleOp)
    ->UseManualTime();

BENCHMARK_MAIN();
