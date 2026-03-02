# Multi-Instrument Trading Guide

## Overview

PinnacleMM supports simultaneous trading across multiple instruments, each with its own order book, strategy instance, and optional exchange simulator. The `InstrumentManager` class orchestrates per-instrument lifecycles while sharing global risk management and order routing infrastructure.

## Quick Start

```bash
# Trade two instruments in simulation mode
cd build && ./pinnaclemm --mode simulation --symbols BTC-USD,ETH-USD

# Trade with ML enabled on all instruments
cd build && ./pinnaclemm --mode simulation --symbols BTC-USD,ETH-USD --enable-ml

# Single-symbol mode still works (backward compatible)
cd build && ./pinnaclemm --mode simulation --symbol BTC-USD
```

## CLI Flags

| Flag | Description | Default |
|------|-------------|---------|
| `--symbols` | Comma-separated list of instruments | (uses `--symbol`) |
| `--symbol` | Single instrument (backward compat) | `BTC-USD` |

When `--symbols` is provided, it takes precedence over `--symbol`.

## Configuration

### `config/default_config.json`

The `instruments` array defines per-instrument settings:

```json
{
  "instruments": [
    {
      "symbol": "BTC-USD",
      "enabled": true,
      "useLockFree": true,
      "enableML": false,
      "baseSpreadBps": 10.0,
      "orderQuantity": 0.01,
      "maxPosition": 10.0
    },
    {
      "symbol": "ETH-USD",
      "enabled": true,
      "useLockFree": false,
      "enableML": true,
      "baseSpreadBps": 15.0,
      "orderQuantity": 0.1,
      "maxPosition": 50.0
    }
  ]
}
```

### Per-Instrument Fields

| Field | Type | Description |
|-------|------|-------------|
| `symbol` | string | Trading pair identifier |
| `enabled` | bool | Whether this instrument is active |
| `useLockFree` | bool | Use lock-free order book implementation |
| `enableML` | bool | Enable ML-enhanced strategy for this instrument |
| `baseSpreadBps` | double | Base spread in basis points |
| `orderQuantity` | double | Default order size |
| `maxPosition` | double | Maximum position for this instrument |

## Architecture

### InstrumentManager

The `InstrumentManager` owns a map of `InstrumentContext` objects, each containing:

```
InstrumentContext
  ├── symbol: std::string
  ├── orderBook: std::shared_ptr<OrderBook>
  ├── strategy: std::shared_ptr<BasicMarketMaker>
  └── simulator: std::shared_ptr<ExchangeSimulator>  (null in live mode)
```

### Lifecycle

1. `addInstrument(config, mode)` — Creates order book, strategy, and simulator for the given symbol
2. `startAll()` — Starts all registered instruments
3. Main loop — Each instrument runs independently; stats are aggregated
4. `stopAll()` — Gracefully stops all instruments

### Per-Symbol Risk Tracking

When multiple instruments are active, the `RiskManager` tracks position, PnL, and volume per-symbol using atomic state. See the risk management section for details.

```cpp
// Register symbols for per-symbol tracking
rm.registerSymbol("BTC-USD");
rm.registerSymbol("ETH-USD");

// Set per-symbol limits (optional — falls back to global)
PerSymbolLimits btcLimits;
btcLimits.symbol = "BTC-USD";
btcLimits.maxPositionSize = 5.0;
rm.setSymbolLimits(btcLimits);

// Query per-symbol state
auto* btcState = rm.getSymbolState("BTC-USD");
double btcPosition = btcState->position.load();
```

## Performance Considerations

- Each instrument runs its own strategy thread and simulator
- The `ResourceAllocator` distributes CPU cores across instruments based on available hardware
- Lock-free order books are recommended for high-throughput instruments
- Global risk checks remain lock-free regardless of instrument count
- Object pooling reduces allocation overhead on hot paths

## Testing

```bash
cd build
./instrument_manager_tests    # 9 tests covering lifecycle management
./multi_instrument_benchmark  # Startup scaling and throughput benchmarks
```
