# Performance Optimization Guide

## Overview

Phase 5 introduced several performance optimizations to PinnacleMM:

1. **Lock-Free OrderBook Fix**: Eliminated 56x regression, now 4.5x faster than mutex
2. **Object Pool**: Thread-safe allocation recycling for hot-path objects
3. **CPU Affinity & Thread Pinning**: Platform-specific core assignment
4. **Link-Time Optimization (LTO)**: Whole-program optimization at link time
5. **Dynamic Resource Allocation**: Automatic CPU core distribution across instruments

## Lock-Free OrderBook Optimization

### Problem

The lock-free order book was 56x slower than the mutex-based implementation at 1000 orders due to:

- **O(n) quantity recalculation**: `updateTotalQuantity()` walked the entire linked list on every add/remove
- **Redundant CAS loops**: Michael-Scott queue CAS retries inside an already-held exclusive lock
- **Logical deletion without physical unlinking**: Removed nodes stayed in the list, growing walk times
- **Duplicate shard lock acquisitions**: Separate `contains()` + `insert()` calls

### Fix

| Issue | Fix | Complexity Change |
|-------|-----|-------------------|
| `updateTotalQuantity()` full scan | O(1) atomic add/subtract on add/remove | O(n) -> O(1) per op |
| CAS loop under exclusive lock | Direct pointer append | Eliminates retries |
| Logical deletion (null pointer) | Physical node unlinking + delete | Prevents list growth |
| `contains()` + `insert()` | Single `insert()` check | 2 locks -> 1 lock |
| Spinlock busy-wait | Platform-specific yield hint | Reduces CPU waste |

### Results

| Metric | Before | After | Improvement |
|--------|--------|-------|-------------|
| LockFree 100 orders | N/A | 0.17ms | - |
| LockFree 1000 orders | 75.2ms | 1.79ms | **42x faster** |
| vs Mutex 1000 orders | 56x slower | 4.5x faster | Regression eliminated |
| LockFree 10000 orders | N/A | 20.2ms | - |
| vs Mutex 10000 orders | - | 3.3x faster | - |

## Object Pool

### `core/utils/ObjectPool.h`

Header-only, thread-safe object pool template for hot-path allocation:

```cpp
#include "core/utils/ObjectPool.h"

// Create pool with 1000 pre-allocated Order objects
pinnacle::utils::ObjectPool<Order> pool(1000);

// Acquire returns shared_ptr with custom deleter that recycles
auto order = pool.acquire("id", "BTC-USD", OrderSide::BUY, OrderType::LIMIT, 100.0, 1.0, ts);

// When shared_ptr ref count reaches 0, object is recycled back to pool
// No heap allocation on acquire if pool has available objects
```

### Design

- Pre-allocates objects in constructor
- `acquire()` returns `std::shared_ptr` with a custom deleter
- Custom deleter resets and recycles the object back to the pool
- Falls back to `new` allocation if pool is exhausted
- Thread-safe via `std::mutex` on the free list

### When to Use

- Order allocation in `ExchangeSimulator` and `BasicMarketMaker`
- Any hot-path object that is frequently created and destroyed
- Objects with non-trivial construction cost

## CPU Affinity & Thread Pinning

### `core/utils/ThreadAffinity.h`

Platform-specific thread pinning for latency-sensitive threads:

```cpp
#include "core/utils/ThreadAffinity.h"

// Pin current thread to core 2
pinnacle::utils::ThreadAffinity::pinToCore(2);

// Set thread name (visible in profilers)
pinnacle::utils::ThreadAffinity::setThreadName("strategy-btc");

// Query available cores
int cores = pinnacle::utils::ThreadAffinity::getNumCores();

// Pin a specific thread
pinnacle::utils::ThreadAffinity::pinThreadToCore(myThread, 3);
```

### Platform Support

| Platform | Thread Pinning | Thread Naming |
|----------|---------------|---------------|
| macOS (ARM/x86) | `thread_affinity_policy` | `pthread_setname_np` |
| Linux | `pthread_setaffinity_np` | `pthread_setname_np` |
| Other | No-op (returns false) | No-op |

## Link-Time Optimization (LTO)

### CMake Configuration

LTO is enabled via the `ENABLE_LTO` option:

```bash
cmake -DENABLE_LTO=ON ..
make -j$(nproc)
```

When enabled, the compiler performs whole-program optimization across translation units, enabling:
- Cross-file inlining
- Dead code elimination
- Interprocedural constant propagation

### Impact

LTO typically provides 5-15% throughput improvement for compute-bound workloads. The overhead is increased link time.

## Dynamic Resource Allocation

### `core/instrument/ResourceAllocator.h`

Automatically distributes CPU cores across instruments:

```cpp
ResourceAllocator allocator;
auto assignments = allocator.allocate(instrumentCount);

for (const auto& assignment : assignments) {
    // assignment.instrumentIndex - which instrument
    // assignment.coreId - which CPU core to pin to
    // assignment.priority - relative priority (0 = highest)
}
```

### Allocation Strategy

1. Core 0 is reserved for OS/kernel work
2. Remaining cores are distributed round-robin across instruments
3. Priority is assigned based on order (lower index = higher priority)

## Benchmarks

```bash
cd build

# Order book comparison (mutex vs lock-free)
./orderbook_benchmark

# Multi-instrument scaling
./multi_instrument_benchmark

# Key benchmarks in multi_instrument_benchmark:
#   SingleInstrumentStartup      — Baseline startup time
#   MultiInstrumentStartup/1..8  — Scaling with 1-8 instruments
#   ObjectPoolAcquireRelease     — Pool vs raw allocation
#   ObjectPoolContended/1..4     — Multi-threaded pool performance
```

## Profiling Tips

1. **CPU profiling**: Use `perf record` (Linux) or Instruments (macOS) to identify hotspots
2. **Cache analysis**: `perf stat -e cache-misses` to check cache behavior
3. **Lock contention**: Monitor spinlock spin counts in `LockFreeOrderMap::ShardGuard`
4. **Memory allocation**: Use `jemalloc` or `tcmalloc` for production builds
5. **NUMA awareness**: On multi-socket systems, pin threads to cores near their memory
