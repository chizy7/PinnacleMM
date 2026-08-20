<div align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset=".images/pinnaclemm-high-resolution-logo-transparent.svg">
    <source media="(prefers-color-scheme: light)" srcset=".images/pinnaclemm-high-resolution-logo-grayscale-transparent.svg">
    <img alt="PinnacleMM Logo" src=".images/pinnaclemm-high-resolution-logo-transparent.svg" width="750" height="100">
  </picture>

  <h1>Ultra-Low Latency Market Making System</h1>

  <p>
    <a href="https://github.com/chizy7/PinnacleMM/blob/main/LICENSE"><img src="https://img.shields.io/badge/license-MIT-blue.svg" alt="License"></a>
    <a href="https://github.com/chizy7/PinnacleMM"><img src="https://img.shields.io/badge/C%2B%2B-20-blue.svg" alt="C++"></a>
    <a href="https://github.com/chizy7/PinnacleMM/releases"><img src="https://img.shields.io/github/v/release/chizy7/PinnacleMM?include_prereleases" alt="Release"></a>
    <a href="https://github.com/chizy7/PinnacleMM/actions/workflows/ci.yml"><img src="https://github.com/chizy7/PinnacleMM/workflows/Build/badge.svg" alt="Build Status"></a>
    <a href="https://github.com/chizy7/PinnacleMM"><img src="https://img.shields.io/badge/latency-microsecond-blue.svg" alt="Performance"></a>
  </p>

  <p>
    <a href="docs/user_guide/getting_started.md">Getting Started</a>&nbsp;&nbsp;•&nbsp;&nbsp;
    <a href="docs/architecture/system_overview.md">Architecture</a>&nbsp;&nbsp;•&nbsp;&nbsp;
    <a href="docs/ORDER_ROUTING.md">Order Routing</a>&nbsp;&nbsp;•&nbsp;&nbsp;
    <a href="docs/PERFORMANCE_BENCHMARKS.md">Performance Benchmarks</a>&nbsp;&nbsp;•&nbsp;&nbsp;
    <a href="docs/api/reference.md">API Reference</a>&nbsp;&nbsp;•&nbsp;&nbsp;
    <a href="docs/exchange/connector_guide.md">Exchange Connectors</a>&nbsp;&nbsp;•&nbsp;&nbsp;
    <a href="docs/MULTI_INSTRUMENT_GUIDE.md">Multi-Instrument</a>
  </p>
</div>

PinnacleMM is a high-performance, production-grade market making system designed for high-frequency trading in cryptocurrency markets. Built primarily in C++ with a focus on ultra-low latency, this system achieves microsecond-level execution speeds while maintaining robust risk management capabilities.

## Key Features

- **Ultra-Low Latency Core**: C++20 engine with lock-free data structures and nanosecond-precision timing
- **Dynamic Market Making**: Adaptive bid-ask spreads with intelligent inventory and position management
- **Live Exchange Connectivity**: Coinbase Pro WebSocket feeds and institutional FIX protocol support (Interactive Brokers, Coinbase, Kraken, Binance)
- **Smart Order Routing**: Multi-venue execution with BEST_PRICE, TWAP, VWAP, and MARKET_IMPACT algorithms
- **ML-Enhanced Trading**: Neural network spread optimization, Hidden Markov Model regime detection, market impact prediction, and reinforcement learning parameter adaptation
- **Risk Management**: Lock-free pre-trade checks, position/exposure limits, circuit breaker, real-time VaR, and a 16-type alerting system
- **Multi-Instrument Trading**: Simultaneous multi-symbol trading with cross-exchange arbitrage detection and cross-market correlation analysis
- **Advanced Backtesting**: Historical data replay with Monte Carlo analysis and A/B testing
- **Observability**: Real-time web dashboard (`--enable-visualization`) and structured JSONL data export (`--json-log`)
- **Crash Recovery**: Memory-mapped persistence with disaster recovery and backup tooling
- **Enterprise Security**: AES-256 encrypted credentials, input validation, audit logging, rate limiting, and certificate pinning
- **Production Deployment**: Docker images and a production-ready Kubernetes StatefulSet

## System Architecture

PinnacleMM follows a modular, layered architecture:

- **Core Engine Layer**: Ultra-low latency components handling order book and execution
- **Risk Layer**: Pre-trade checks, circuit breaker, VaR engine, alerting, and disaster recovery
- **Strategy Layer**: Pluggable strategies for different market making approaches
- **Exchange Layer**: Multi-protocol connectivity (WebSocket, FIX) with simulation capabilities
- **Persistence Layer**: Memory-mapped file system for crash recovery

Read more about the [system architecture](docs/architecture/system_overview.md).

## Getting Started

### Prerequisites

- C++20 compatible compiler (GCC 10+, Clang 10+, or MSVC 2019+)
- CMake 3.14+
- Boost libraries 1.72+
- OpenSSL (secure credential handling)
- nlohmann_json (configuration handling)
- spdlog and fmt (auto-downloaded at pinned versions if not installed)

### Quick Start

```bash
# Clone and setup
git clone https://github.com/chizy7/PinnacleMM.git
cd PinnacleMM

# Native execution (recommended for development) — auto-builds if needed
scripts/run-native.sh                     # Simulation mode
scripts/run-native.sh test                # Run test suite
scripts/run-native.sh benchmark           # Run performance benchmarks

# Docker execution (recommended for production)
scripts/run-docker.sh                     # Simulation mode
```

Pre-built Docker images are available at `ghcr.io/chizy7/pinnaclemm`. See the [Scripts Reference](docs/SCRIPTS.md) for all script commands, options, and container usage.

### Manual Build

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON -DBUILD_BENCHMARKS=ON ..
make -j$(nproc || sysctl -n hw.ncpu)
```

## Running PinnacleMM

### Simulation Mode

```bash
cd build && ./pinnaclemm --mode simulation --symbol BTC-USD

# With ML strategy and real-time dashboard
./pinnaclemm --mode simulation --enable-ml --enable-visualization
```

### Live Trading Mode

```bash
# One-time credential setup (AES-256 encrypted, master password protected)
scripts/run-native.sh --setup-credentials

# Start live trading
cd build && ./pinnaclemm --mode live --exchange coinbase --symbol BTC-USD --verbose
```

See [Security & API Key Management](docs/security/credentials.md) for credential details.

### Backtest Mode

```bash
# Runs against historical data (auto-generates synthetic data if no CSV found)
cd build && ./pinnaclemm --mode backtest --symbol BTC-USD
```

Backtest mode prints a detailed performance report (Sharpe ratio, drawdown, win rate) and saves JSON results. See the [Advanced Backtesting Guide](docs/ADVANCED_BACKTESTING.md) for custom data and parameters.

For all command-line options, run modes, multi-instrument setups, and the visualization dashboard, see the [Getting Started Guide](docs/user_guide/getting_started.md).

## Performance

- **Order Book Update Latency**: <1 μs
- **Order Execution Latency**: <50 μs end-to-end
- **Pre-Trade Risk Check**: ~750 ns (lock-free)
- **Circuit Breaker Check**: ~5 ns (single atomic load)
- **ML Prediction Latency**: 1–3 μs (neural network inference)
- **Throughput**: 100,000+ messages per second
- **Recovery Time**: <5 seconds for full system recovery
- **Memory Footprint**: <100 MB for core engine

See [Performance Benchmarks](docs/PERFORMANCE_BENCHMARKS.md) for methodology and full results.

## Documentation

### Core System
- [Getting Started Guide](docs/user_guide/getting_started.md)
- [System Architecture](docs/architecture/system_overview.md)
- [API Reference](docs/api/reference.md)
- [Scripts Reference](docs/SCRIPTS.md)
- [Project Roadmap](docs/ROADMAP.md)

### Risk Management & Production Operations
- [Risk Management](docs/RISK_MANAGEMENT.md) — pre-trade checks, VaR, circuit breaker, alerting
- [Disaster Recovery](docs/DISASTER_RECOVERY.md) — operational runbook for crash recovery and backups
- [Kubernetes Deployment](docs/KUBERNETES_DEPLOYMENT.md) — production K8s deployment guide
- [Persistence System](docs/architecture/persistence.md)
- [Security & API Key Management](docs/security/credentials.md)
- [Certificate Pinning](docs/security/CERTIFICATE_PINNING.md)

### Machine Learning & Analytics
- [ML Spread Optimization](docs/ML_SPREAD_OPTIMIZATION.md)
- [Market Regime Detection](docs/MARKET_REGIME_DETECTION.md)
- [Market Impact Prediction](docs/MARKET_IMPACT_PREDICTION.md)
- [RL Parameter Adaptation](docs/RL_PARAMETER_ADAPTATION.md)
- [Order Book Flow Analysis](docs/ORDER_BOOK_FLOW_ANALYSIS.md)
- [Advanced Backtesting](docs/ADVANCED_BACKTESTING.md)
- [Strategy Performance Visualization](docs/STRATEGY_PERFORMANCE_VISUALIZATION.md)
- [JSON Data Export](docs/JSON_DATA_EXPORT.md)

### Multi-Instrument & Optimization
- [Multi-Instrument Guide](docs/MULTI_INSTRUMENT_GUIDE.md)
- [Cross-Exchange Arbitrage](docs/CROSS_EXCHANGE_ARBITRAGE.md)
- [Cross-Market Correlation](docs/CROSS_MARKET_CORRELATION.md)
- [Performance Optimization Guide](docs/PERFORMANCE_OPTIMIZATION_GUIDE.md)

### Exchange Integration & Testing
- [FIX Protocol Integration](docs/FIX_PROTOCOL_INTEGRATION.md)
- [Interactive Brokers Setup](docs/IB_TESTING_GUIDE.md)
- [Testing Guide](docs/TESTING_GUIDE.md)
- [WebSocket Testing](docs/WEBSOCKET_TESTING.md)

## Technology Stack

- **Core Engine**: C++20, lock-free algorithms, `std::atomic`
- **Build System**: CMake
- **Testing**: Google Test, Google Benchmark
- **Networking**: Boost.Beast (WebSocket), hffix (FIX protocol)
- **Machine Learning**: Custom neural networks, Hidden Markov Models, reinforcement learning
- **Visualization**: HTML5/JavaScript frontend with Chart.js and D3.js
- **Security**: OpenSSL, AES-256-CBC encryption, PBKDF2 key derivation
- **Configuration**: nlohmann/json
- **Deployment**: Docker, Kubernetes

## Contributing

Contributions are welcome! Please read the [Contributing Guide](CONTRIBUTING.md) for development setup, coding standards, pre-commit hooks, and the pull request process. All participants are expected to follow our [Code of Conduct](CODE_OF_CONDUCT.md).

- **Bugs & feature requests**: use the [issue templates](https://github.com/chizy7/PinnacleMM/issues/new/choose)
- **Security vulnerabilities**: report privately via the [Security Policy](.github/SECURITY.md) — never in a public issue
- **Looking for ideas?** Check the [Roadmap](docs/ROADMAP.md)

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Contact

For questions, feedback, or collaboration opportunities:

- **Email**: [chizy@chizyhub.com](mailto:chizy@chizyhub.com)
- **X (Twitter)**: [![Twitter Follow](https://img.shields.io/twitter/follow/chizyization?style=social)](https://x.com/Chizyization)
