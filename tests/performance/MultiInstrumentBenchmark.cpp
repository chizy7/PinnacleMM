#include "../../core/instrument/InstrumentManager.h"
#include "../../core/orderbook/Order.h"
#include "../../core/utils/ObjectPool.h"
#include "../../core/utils/ThreadAffinity.h"

#include <benchmark/benchmark.h>
#include <memory>
#include <string>

using namespace pinnacle;

// Benchmark: Single instrument startup
static void BM_SingleInstrumentStartup(benchmark::State& state) {
  for (auto _ : state) {
    instrument::InstrumentManager manager;
    instrument::InstrumentConfig cfg;
    cfg.symbol = "BTC-USD";
    cfg.useLockFree = false;
    cfg.enableML = false;
    manager.addInstrument(cfg, "simulation");
    manager.startAll();
    manager.stopAll();
  }
}
BENCHMARK(BM_SingleInstrumentStartup);

// Benchmark: Multiple instrument startup
static void BM_MultiInstrumentStartup(benchmark::State& state) {
  int numInstruments = state.range(0);

  for (auto _ : state) {
    instrument::InstrumentManager manager;
    for (int i = 0; i < numInstruments; ++i) {
      instrument::InstrumentConfig cfg;
      cfg.symbol = "INST-" + std::to_string(i);
      cfg.useLockFree = false;
      cfg.enableML = false;
      manager.addInstrument(cfg, "simulation");
    }
    manager.startAll();
    manager.stopAll();
  }

  state.SetItemsProcessed(state.iterations() * numInstruments);
}
BENCHMARK(BM_MultiInstrumentStartup)->Arg(1)->Arg(2)->Arg(4)->Arg(8);

// Benchmark: Object pool acquire/release
static void BM_ObjectPoolAcquireRelease(benchmark::State& state) {
  utils::ObjectPool<Order> pool(128);

  for (auto _ : state) {
    auto obj = pool.acquire();
    benchmark::DoNotOptimize(obj.get());
  }
}
BENCHMARK(BM_ObjectPoolAcquireRelease);

// Benchmark: Raw new/delete vs pool
static void BM_RawNewDelete(benchmark::State& state) {
  for (auto _ : state) {
    auto* obj = new Order();
    benchmark::DoNotOptimize(obj);
    delete obj;
  }
}
BENCHMARK(BM_RawNewDelete);

// Benchmark: Object pool under contention
static void BM_ObjectPoolContended(benchmark::State& state) {
  static utils::ObjectPool<Order> pool(256);

  for (auto _ : state) {
    auto obj = pool.acquire();
    benchmark::DoNotOptimize(obj.get());
  }
}
BENCHMARK(BM_ObjectPoolContended)->Threads(1)->Threads(2)->Threads(4);

// Benchmark: Get aggregate statistics
static void BM_AggregateStatistics(benchmark::State& state) {
  instrument::InstrumentManager manager;

  int numInstruments = state.range(0);
  for (int i = 0; i < numInstruments; ++i) {
    instrument::InstrumentConfig cfg;
    cfg.symbol = "INST-" + std::to_string(i);
    cfg.useLockFree = false;
    cfg.enableML = false;
    manager.addInstrument(cfg, "simulation");
  }
  manager.startAll();

  for (auto _ : state) {
    auto stats = manager.getAggregateStatistics();
    benchmark::DoNotOptimize(stats.data());
  }

  manager.stopAll();
}
BENCHMARK(BM_AggregateStatistics)->Arg(1)->Arg(4)->Arg(8);

// Benchmark: Thread affinity info
static void BM_GetNumCores(benchmark::State& state) {
  for (auto _ : state) {
    int cores = utils::ThreadAffinity::getNumCores();
    benchmark::DoNotOptimize(cores);
  }
}
BENCHMARK(BM_GetNumCores);

BENCHMARK_MAIN();
