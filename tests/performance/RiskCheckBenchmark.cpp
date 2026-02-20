#include "../../core/orderbook/Order.h"
#include "../../core/risk/CircuitBreaker.h"
#include "../../core/risk/RiskConfig.h"
#include "../../core/risk/RiskManager.h"
#include "../../core/utils/TimeUtils.h"

#include <benchmark/benchmark.h>
#include <string>

using namespace pinnacle;
using namespace pinnacle::risk;

// ---------------------------------------------------------------------------
// One-time setup: initialize singletons before benchmarks run
// ---------------------------------------------------------------------------
static void setupRiskManager() {
  auto& rm = RiskManager::getInstance();
  RiskLimits limits;
  limits.maxPositionSize = 100.0;
  limits.maxOrderSize = 10.0;
  limits.dailyLossLimit = 100000.0;
  limits.maxDrawdownPct = 10.0;
  limits.maxDailyVolume = 10000.0;
  limits.maxOrderValue = 1000000.0;
  limits.maxOrdersPerSecond = 1'000'000; // effectively unlimited for bench
  rm.initialize(limits);
}

static void setupCircuitBreaker() {
  auto& cb = CircuitBreaker::getInstance();
  CircuitBreakerConfig config;
  config.priceMove1minPct = 100.0; // very wide -> won't trip during bench
  config.priceMove5minPct = 100.0;
  config.maxLatencyUs = 1'000'000;
  config.cooldownPeriodMs = 60000;
  cb.initialize(config);
}

// ---------------------------------------------------------------------------
// BM_RiskCheckOrder
// Measures the hot-path latency of a pre-trade risk check.
// Target: < 100 ns per call.
// ---------------------------------------------------------------------------
static void BM_RiskCheckOrder(benchmark::State& state) {
  setupRiskManager();
  auto& rm = RiskManager::getInstance();

  for (auto _ : state) {
    auto result = rm.checkOrder(OrderSide::BUY, 50000.0, 0.1, "BTC-USD");
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(BM_RiskCheckOrder);

// ---------------------------------------------------------------------------
// BM_CircuitBreakerCheck
// Measures the hot-path latency of isTradingAllowed() (single atomic load).
// ---------------------------------------------------------------------------
static void BM_CircuitBreakerCheck(benchmark::State& state) {
  setupCircuitBreaker();
  auto& cb = CircuitBreaker::getInstance();

  for (auto _ : state) {
    bool allowed = cb.isTradingAllowed();
    benchmark::DoNotOptimize(allowed);
  }
}
BENCHMARK(BM_CircuitBreakerCheck);

// ---------------------------------------------------------------------------
// BM_OnFill
// Measures the latency of processing a fill event (position + volume update).
// ---------------------------------------------------------------------------
static void BM_OnFill(benchmark::State& state) {
  setupRiskManager();
  auto& rm = RiskManager::getInstance();

  int iteration = 0;
  for (auto _ : state) {
    // Alternate between BUY and SELL to keep position near zero
    OrderSide side = (iteration++ % 2 == 0) ? OrderSide::BUY : OrderSide::SELL;
    rm.onFill(side, 50000.0, 0.01, "BTC-USD");
  }
}
BENCHMARK(BM_OnFill);

// ---------------------------------------------------------------------------
// BM_OnPnLUpdate
// Measures the latency of a PnL update (drawdown + daily loss evaluation).
// ---------------------------------------------------------------------------
static void BM_OnPnLUpdate(benchmark::State& state) {
  setupRiskManager();
  auto& rm = RiskManager::getInstance();
  rm.resume(); // ensure not halted

  double pnl = 0.0;
  for (auto _ : state) {
    // Small oscillating PnL to avoid triggering halt
    pnl = (pnl > 100.0) ? 0.0 : pnl + 0.01;
    rm.onPnLUpdate(pnl);
  }
}
BENCHMARK(BM_OnPnLUpdate);

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
