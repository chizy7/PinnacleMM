#include "../../core/utils/TimeUtils.h"
#include "../../strategies/config/StrategyConfig.h"
#include "../../strategies/ml/MLSpreadOptimizer.h"

#include <benchmark/benchmark.h>
#include <random>
#include <vector>

using namespace pinnacle::strategy;
using namespace pinnacle::strategy::ml;

class MLSpreadOptimizerBenchmark : public benchmark::Fixture {
public:
  void SetUp(const ::benchmark::State& state) override {
    // Setup optimizer with performance-oriented configuration
    config_ = MLSpreadOptimizer::Config{};
    config_.maxTrainingDataPoints = 1000;
    config_.minTrainingDataPoints = 100;
    config_.learningRate = 0.01;
    config_.batchSize = 16;
    config_.epochs = 20; // Reduced for benchmarking
    config_.enableCache = true;

    optimizer_ = std::make_unique<MLSpreadOptimizer>(config_);
    optimizer_->initialize();

    // Setup strategy config
    strategyConfig_ = StrategyConfig{};
    strategyConfig_.baseSpreadBps = 10.0;
    strategyConfig_.minSpreadBps = 5.0;
    strategyConfig_.maxSpreadBps = 50.0;

    // Pre-populate with training data
    populateTrainingData(500);
    optimizer_->trainModel();
  }

  void TearDown(const ::benchmark::State& state) override {
    optimizer_.reset();
  }

protected:
  void populateTrainingData(size_t count) {
    std::mt19937 gen(42); // Fixed seed for reproducible benchmarks
    std::uniform_real_distribution<double> priceDist(45000.0, 55000.0);
    std::uniform_real_distribution<double> volDist(0.001, 0.1);
    std::uniform_real_distribution<double> imbalanceDist(-0.5, 0.5);
    std::uniform_real_distribution<double> positionDist(-5.0, 5.0);

    uint64_t baseTimestamp = pinnacle::utils::TimeUtils::getCurrentNanos();

    for (size_t i = 0; i < count; ++i) {
      MarketFeatures features;
      features.midPrice = priceDist(gen);
      features.bidAskSpread = features.midPrice * 0.0001;
      features.priceVolatility = volDist(gen);
      features.orderBookImbalance = imbalanceDist(gen);
      features.bidBookDepth = 1000.0 + (i % 100) * 10;
      features.askBookDepth = 1200.0 + (i % 100) * 12;
      features.totalBookDepth = features.bidBookDepth + features.askBookDepth;
      features.weightedMidPrice = features.midPrice;
      features.recentVolume = 10.0 + (i % 50) * 2;
      features.timeOfDay = (i % 24) / 24.0;
      features.dayOfWeek = (i % 7) / 7.0;
      features.isMarketOpen = true;
      features.currentPosition = positionDist(gen);
      features.positionRatio = features.currentPosition / 10.0;
      features.inventoryRisk = std::abs(features.positionRatio);
      features.trendStrength = imbalanceDist(gen) * 0.1;
      features.meanReversion = 1.0 - std::abs(features.trendStrength);
      features.marketStress = volDist(gen);
      features.timestamp = baseTimestamp + i * 1000000;

      // Calculate target spread based on features
      double targetSpread = strategyConfig_.baseSpreadBps * 0.0001 *
                            features.midPrice *
                            (1.0 + features.priceVolatility) *
                            (1.0 + std::abs(features.orderBookImbalance));

      optimizer_->updateWithOutcome(features, targetSpread,
                                    targetSpread * 0.1, // Mock P&L
                                    0.8,                // Mock fill rate
                                    features.timestamp);
    }
  }

  MarketFeatures createRandomFeatures(std::mt19937& gen) {
    std::uniform_real_distribution<double> priceDist(48000.0, 52000.0);
    std::uniform_real_distribution<double> volDist(0.01, 0.05);
    std::uniform_real_distribution<double> imbalanceDist(-0.3, 0.3);

    MarketFeatures features;
    features.midPrice = priceDist(gen);
    features.bidAskSpread = features.midPrice * 0.0001;
    features.priceVolatility = volDist(gen);
    features.orderBookImbalance = imbalanceDist(gen);
    features.bidBookDepth = 1000.0;
    features.askBookDepth = 1200.0;
    features.totalBookDepth = 2200.0;
    features.weightedMidPrice = features.midPrice;
    features.recentVolume = 50.0;
    features.timeOfDay = 0.5;
    features.dayOfWeek = 0.5;
    features.isMarketOpen = true;
    features.currentPosition = 0.0;
    features.positionRatio = 0.0;
    features.inventoryRisk = 0.0;
    features.timestamp = pinnacle::utils::TimeUtils::getCurrentNanos();
    return features;
  }

  MLSpreadOptimizer::Config config_;
  std::unique_ptr<MLSpreadOptimizer> optimizer_;
  StrategyConfig strategyConfig_;
};

// Benchmark single prediction latency
BENCHMARK_DEFINE_F(MLSpreadOptimizerBenchmark, SinglePrediction)
(benchmark::State& state) {
  std::mt19937 gen(42);

  for (auto _ : state) {
    auto features = createRandomFeatures(gen);
    auto prediction =
        optimizer_->predictOptimalSpread(features, strategyConfig_);
    benchmark::DoNotOptimize(prediction);
  }

  state.SetItemsProcessed(state.iterations());
  state.counters["predictions_per_second"] =
      benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate);
}

// Benchmark batch predictions
BENCHMARK_DEFINE_F(MLSpreadOptimizerBenchmark, BatchPredictions)
(benchmark::State& state) {
  std::mt19937 gen(42);
  const int batch_size = state.range(0);

  for (auto _ : state) {
    std::vector<SpreadPrediction> predictions;
    predictions.reserve(batch_size);

    for (int i = 0; i < batch_size; ++i) {
      auto features = createRandomFeatures(gen);
      predictions.push_back(
          optimizer_->predictOptimalSpread(features, strategyConfig_));
    }

    benchmark::DoNotOptimize(predictions);
  }

  state.SetItemsProcessed(state.iterations() * batch_size);
  state.counters["predictions_per_second"] = benchmark::Counter(
      state.iterations() * batch_size, benchmark::Counter::kIsRate);
}

// Benchmark feature extraction performance
BENCHMARK_DEFINE_F(MLSpreadOptimizerBenchmark, FeatureExtraction)
(benchmark::State& state) {
  std::mt19937 gen(42);

  for (auto _ : state) {
    auto features = createRandomFeatures(gen);
    auto featureVector = features.toVector();
    benchmark::DoNotOptimize(featureVector);
  }

  state.SetItemsProcessed(state.iterations());
  state.counters["extractions_per_second"] =
      benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate);
}

// Benchmark market data ingestion
BENCHMARK_DEFINE_F(MLSpreadOptimizerBenchmark, MarketDataIngestion)
(benchmark::State& state) {
  std::mt19937 gen(42);
  std::uniform_real_distribution<double> priceDist(48000.0, 52000.0);
  std::uniform_real_distribution<double> volumeDist(0.1, 10.0);

  uint64_t timestamp = pinnacle::utils::TimeUtils::getCurrentNanos();

  for (auto _ : state) {
    double midPrice = priceDist(gen);
    optimizer_->addMarketData(midPrice,               // midPrice
                              midPrice - 2.5,         // bidPrice
                              midPrice + 2.5,         // askPrice
                              volumeDist(gen) * 1000, // bidVolume
                              volumeDist(gen) * 1200, // askVolume
                              volumeDist(gen),        // tradeVolume
                              0.0,                    // currentPosition
                              timestamp++);
  }

  state.SetItemsProcessed(state.iterations());
  state.counters["ingestions_per_second"] =
      benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate);
}

// Benchmark online learning updates
BENCHMARK_DEFINE_F(MLSpreadOptimizerBenchmark, OnlineLearningUpdate)
(benchmark::State& state) {
  std::mt19937 gen(42);
  std::uniform_real_distribution<double> spreadDist(2.0, 10.0);
  std::uniform_real_distribution<double> pnlDist(-1.0, 2.0);
  std::uniform_real_distribution<double> fillRateDist(0.5, 1.0);

  for (auto _ : state) {
    auto features = createRandomFeatures(gen);
    optimizer_->updateWithOutcome(features, spreadDist(gen), pnlDist(gen),
                                  fillRateDist(gen), features.timestamp);
  }

  state.SetItemsProcessed(state.iterations());
  state.counters["updates_per_second"] =
      benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate);
}

// Benchmark model training
BENCHMARK_DEFINE_F(MLSpreadOptimizerBenchmark, ModelTraining)
(benchmark::State& state) {
  for (auto _ : state) {
    // Add some fresh training data
    populateTrainingData(100);

    auto start_time = std::chrono::high_resolution_clock::now();
    bool success = optimizer_->trainModel();
    auto end_time = std::chrono::high_resolution_clock::now();

    if (success) {
      auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                          end_time - start_time)
                          .count();
      state.counters["training_time_us"] = duration;
    }

    benchmark::DoNotOptimize(success);
  }

  state.SetItemsProcessed(state.iterations());
}

// Register benchmarks with different batch sizes
BENCHMARK_REGISTER_F(MLSpreadOptimizerBenchmark, SinglePrediction)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_REGISTER_F(MLSpreadOptimizerBenchmark, BatchPredictions)
    ->RangeMultiplier(2)
    ->Range(8, 256)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_REGISTER_F(MLSpreadOptimizerBenchmark, FeatureExtraction)
    ->Unit(benchmark::kNanosecond);

BENCHMARK_REGISTER_F(MLSpreadOptimizerBenchmark, MarketDataIngestion)
    ->Unit(benchmark::kNanosecond);

BENCHMARK_REGISTER_F(MLSpreadOptimizerBenchmark, OnlineLearningUpdate)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_REGISTER_F(MLSpreadOptimizerBenchmark, ModelTraining)
    ->Unit(benchmark::kMillisecond);

// Simple non-fixture benchmarks for comparison
static void BM_FeatureVectorCreation(benchmark::State& state) {
  MarketFeatures features;
  features.midPrice = 50000.0;
  features.bidAskSpread = 5.0;
  features.priceVolatility = 0.02;

  for (auto _ : state) {
    auto vector = features.toVector();
    benchmark::DoNotOptimize(vector);
  }
}
BENCHMARK(BM_FeatureVectorCreation)->Unit(benchmark::kNanosecond);

static void BM_FeatureNamesRetrieval(benchmark::State& state) {
  for (auto _ : state) {
    auto names = MarketFeatures::getFeatureNames();
    benchmark::DoNotOptimize(names);
  }
}
BENCHMARK(BM_FeatureNamesRetrieval)->Unit(benchmark::kNanosecond);

BENCHMARK_MAIN();
