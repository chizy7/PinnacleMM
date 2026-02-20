<div align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset=".images/pinnaclemm-high-resolution-logo-transparent.svg">
    <source media="(prefers-color-scheme: light)" srcset=".images/pinnaclemm-high-resolution-logo-grayscale-transparent.svg">
    <img alt="PinnacleMM Logo" src=".images/pinnaclemm-high-resolution-logo-transparent.svg" width="750" height=100">
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
    <a href="docs/exchange/connector_guide.md">Exchange Connectors</a>
  </p>
</div>

PinnacleMM is a high-performance, production-grade market making system designed for high-frequency trading in cryptocurrency markets. Built primarily in C++ with a focus on ultra-low latency, this system achieves microsecond-level execution speeds while maintaining robust risk management capabilities.

## Key Features

- **Ultra-Low Latency Core**: Optimized C++ engine with lock-free data structures
- **Nanosecond Precision**: High-resolution timing for accurate execution
- **Crash Recovery**: Memory-mapped persistence system for reliable operation
- **Dynamic Market Making**: Adaptive bid-ask spread based on market conditions
- **Position Management**: Intelligent inventory management with customizable risk parameters
- **Exchange Simulation**: Realistic market simulation for strategy development and testing
- **Live Exchange Connectivity**: Real-time connection to Coinbase Pro WebSocket feeds
- **FIX Protocol Support**: Professional-grade FIX connectivity for institutional exchanges
- **Advanced Order Routing**: Smart order routing with 4 algorithms (BEST_PRICE, TWAP, VWAP, MARKET_IMPACT)
- **Multi-Venue Execution**: Intelligent order distribution across multiple exchanges
- **ML-Enhanced Trading**: Neural network-based spread optimization with sub-microsecond predictions
- **Market Regime Detection**: Real-time detection of 8 market regimes using Hidden Markov Models
- **Predictive Analytics**: Market impact prediction and reinforcement learning parameter adaptation
- **Advanced Backtesting**: Historical data replay with Monte Carlo analysis and A/B testing
- **Real-Time Visualization**: Professional web dashboard with live performance monitoring (access at `visualization/static/index.html` when running with `--enable-visualization`)
- **Structured Data Export**: JSON Lines (JSONL) logging for market data, strategy metrics, and trading events with `--json-log` flag
- **Risk Management**: Pre-trade risk checks (~750ns), position/exposure limits, drawdown tracking, daily loss limits, and auto-hedging
- **Circuit Breaker**: Automatic market halt on rapid price moves, spread widening, volume spikes, latency degradation, or crisis regime detection
- **Real-Time VaR**: Value at Risk using historical, parametric, and Monte Carlo (10K simulations) methods with lock-free double-buffered reads
- **Alerting System**: 16 alert types with throttling, severity levels, and WebSocket delivery to the dashboard
- **Disaster Recovery**: Atomic risk state persistence, position reconciliation, and labeled backup management
- **Kubernetes Deployment**: Production-ready StatefulSet with health probes, PVC, network policies, and pod disruption budget
- **Enterprise Security**: AES-256-CBC encryption with unique salts, 100,000 PBKDF2 iterations, secure password input, comprehensive input validation, audit logging, rate limiting, and certificate pinning
- **Comprehensive Testing**: Extensive test suite ensuring reliability and performance

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
- spdlog library (for structured logging)
- fmt library (for formatting, spdlog dependency)
- OpenSSL library (for secure credential handling)
- nlohmann_json library (for configuration handling)

### Quick Start with Scripts

PinnacleMM includes convenient bash scripts for easy execution:

#### **Native Execution** (Recommended for Development)
```bash
# Clone and setup
git clone https://github.com/chizy7/PinnacleMM.git
cd PinnacleMM

# One-command setup and run
scripts/run-native.sh                    # Simulation mode (auto-builds if needed)
scripts/run-native.sh -m live -v         # Live trading with verbose logs
scripts/run-native.sh --enable-ml        # ML-enhanced simulation mode
scripts/run-native.sh --enable-visualization # With real-time dashboard
scripts/run-native.sh --setup-credentials # Configure API keys
```

#### **Docker Execution** (Recommended for Production)
```bash
# Build and run in one command
scripts/run-docker.sh                    # Simulation mode
scripts/run-docker.sh -m live -v         # Live trading mode
scripts/run-docker.sh build              # Build Docker image
scripts/run-docker.sh logs               # View container logs
```

### Manual Building from Source

```bash
# Clone the repository
git clone https://github.com/chizy7/PinnacleMM.git
cd PinnacleMM

# Build with native script (recommended)
scripts/run-native.sh build

# Or build manually
mkdir build && cd build
cmake ..
make -j$(sysctl -n hw.ncpu)  # macOS
# make -j$(nproc)            # Linux
```

### Script Features Comparison

| Feature | Native Script (`scripts/run-native.sh`) | Docker Script (`scripts/run-docker.sh`) |
|---------|-----------------------------------|-----------------------------------|
| **Simulation Mode** | Perfect | Perfect |
| **Live Trading** | Real WebSocket data | WebSocket config issue |
| **Auto-Build** | Builds if needed | Auto Docker build |
| **Test Runner** | `scripts/run-native.sh test` | Not included |
| **Benchmarks** | `scripts/run-native.sh benchmark` | Not included |
| **Credential Setup** | Interactive setup | Volume mounting |
| **Dependency Check** | cmake, make, g++ | Docker only |
| **Best For** | Development & Live Trading | Production & Simulation |

### Running PinnacleMM

#### Simulation Mode
```bash
# Using scripts (recommended)
scripts/run-native.sh                    # Native execution
scripts/run-docker.sh                    # Docker execution

# Manual execution
cd build && ./pinnaclemm --mode simulation --symbol BTC-USD

# ML-enhanced simulation with visualization
cd build && ./pinnaclemm --mode simulation --enable-ml --enable-visualization

# Custom ports for visualization (useful for running multiple instances)
cd build && ./pinnaclemm --mode simulation --enable-ml --enable-visualization --viz-ws-port 8089 --viz-api-port 8090

# Debug mode with enhanced WebSocket logging
cd build && SPDLOG_LEVEL=debug ./pinnaclemm --mode simulation --enable-ml --enable-visualization --verbose

# Enable JSON data export (logs market data and strategy metrics to JSONL file)
cd build && ./pinnaclemm --mode simulation --enable-ml --json-log --json-log-file simulation_data.jsonl

# Combined: ML + visualization + JSON logging
cd build && ./pinnaclemm --mode simulation --enable-ml --enable-visualization --json-log --json-log-file sim_ml_data.jsonl

# The visualization dashboard will be available at:
# - WebSocket: ws://localhost:8080 (or custom port with --viz-ws-port)
# - REST API: http://localhost:8081 (or custom port with --viz-api-port)
# - Dashboard: Open visualization/static/index.html in your browser
# - Test Dashboard: Open build/test_dashboard.html for WebSocket connection testing
```

#### Live Exchange Mode
```bash
# Setup credentials first
scripts/run-native.sh --setup-credentials

# Live trading with scripts
scripts/run-native.sh -m live -v         # Native (recommended for live)
scripts/run-docker.sh -m live -v         # Docker

# Manual execution
cd build && ./pinnaclemm --mode live --exchange coinbase --symbol BTC-USD --verbose

# Live trading with ML and visualization (custom ports)
cd build && ./pinnaclemm --mode live --exchange coinbase --symbol BTC-USD --enable-ml --enable-visualization --viz-ws-port 8085 --viz-api-port 8086 --verbose

# Debug mode for live trading (with enhanced logging)
cd build && SPDLOG_LEVEL=debug ./pinnaclemm --mode live --exchange coinbase --symbol BTC-USD --enable-ml --enable-visualization --viz-ws-port 8085 --viz-api-port 8086 --verbose

# Live trading with JSON data export (captures real market data)
cd build && ./pinnaclemm --mode live --exchange coinbase --symbol BTC-USD --enable-ml --json-log --json-log-file live_btc_data.jsonl

# Full-featured live trading: ML + visualization + JSON logging
cd build && ./pinnaclemm --mode live --exchange coinbase --symbol BTC-USD --enable-ml --enable-visualization --viz-ws-port 8085 --viz-api-port 8086 --json-log --json-log-file live_trading_session.jsonl --verbose

# Access the live dashboard:
# - Main Dashboard: visualization/static/live_dashboard.html
# - Test Dashboard: build/test_dashboard.html (for connection testing)
# - WebSocket: ws://localhost:8085 (or your custom port)
# - REST API: http://localhost:8086 (or your custom port)
```

When running in live mode, you'll be prompted for your master password to decrypt API credentials.

#### Backtest Mode
```bash
# Run backtest with synthetic data (no API keys needed, auto-generates data if no CSV found)
cd build && ./pinnaclemm --mode backtest --symbol BTC-USD

# Backtest with ML-enhanced strategy
cd build && ./pinnaclemm --mode backtest --symbol BTC-USD --enable-ml

# Custom parameters
cd build && ./pinnaclemm --mode backtest --symbol BTC-USD \
  --initial-balance 50000 --trading-fee 0.002 --slippage-bps 5.0

# Custom output directory
cd build && ./pinnaclemm --mode backtest --symbol BTC-USD --backtest-output my_results

# Using your own historical data: place CSV at <backtest-output>/data/<SYMBOL>.csv
# e.g. backtest_results/data/BTC-USD.csv with format:
# timestamp,symbol,price,bid,ask,volume
# 1640995200000000000,BTC-USD,47892.50,47890.00,47895.00,1250.75
```

Backtest mode runs the strategy against historical (or synthetic) data, prints a detailed performance report (Sharpe ratio, drawdown, win rate, etc.), saves JSON results to the output directory, and exits cleanly.

## JSON Data Export

PinnacleMM provides comprehensive structured data export capabilities through JSON Lines (JSONL) format logging. This feature enables detailed analysis, backtesting, monitoring, and debugging of trading strategies and market data.

### Features

- **Market Data Logging**: Real-time price, volume, bid/ask data with timestamps
- **Strategy Metrics**: Position, P&L, quote counts, and performance statistics
- **Order Book Updates**: Complete order book state with bid and ask arrays
- **Connection Events**: WebSocket connections, disconnections, and errors
- **Trading Events**: Order placements, fills, cancellations, and status updates
- **Thread-Safe**: Concurrent logging without performance impact
- **JSONL Format**: One JSON object per line for easy parsing and streaming

### Usage

Enable JSON logging with the `--json-log` flag and optionally specify a custom file path:

```bash
# Basic JSON logging (default file: pinnaclemm_data.jsonl)
./pinnaclemm --mode simulation --symbol BTC-USD --json-log

# Custom file path
./pinnaclemm --mode live --exchange coinbase --symbol BTC-USD --json-log --json-log-file my_trading_data.jsonl

# Combined with other features
./pinnaclemm --mode live --enable-ml --enable-visualization --json-log --json-log-file full_session.jsonl
```

### Sample Output

```json
{"format":"jsonl","timestamp":"2025-09-29T16:27:06.770Z","type":"session_start","version":"1.0.0"}
{"metrics":{"ask_price":67252.80,"bid_price":67248.20,"market_price":67250.45,"pnl":0.0,"position":0.0,"quote_updates":1,"strategy_name":"BasicMarketMaker","volume":1234.56},"strategy_name":"BasicMarketMaker","symbol":"BTC-USD","timestamp":"2025-09-29T16:27:12.011Z","type":"strategy_metrics"}
{"ask_price":67253.00,"bid_price":67249.50,"event_timestamp":1695736032610,"is_buy":true,"price":67250.75,"symbol":"BTC-USD","timestamp":"2025-09-29T16:27:12.610Z","type":"market_update","volume":0.5}
```

### Data Types

- **`session_start`**: Session initialization marker with format and version
- **`strategy_metrics`**: Trading strategy performance and position data
- **`market_update`**: Real-time market data from exchange feeds
- **`order_book_update`**: Complete order book snapshots with bid/ask arrays
- **`trading_event`**: Order lifecycle events and trading actions
- **`connection_event`**: Exchange connectivity status and errors

### File Management

JSON log files are created in the current working directory by default. For production use, consider:

```bash
# Save to logs directory (create and add to .gitignore)
mkdir -p logs
./pinnaclemm --json-log --json-log-file logs/trading_$(date +%Y%m%d_%H%M%S).jsonl

# Save to data directory (existing, likely gitignored)
./pinnaclemm --json-log --json-log-file data/market_data_$(date +%Y%m%d).jsonl

# Save outside project directory
./pinnaclemm --json-log --json-log-file ~/trading_logs/pinnaclemm_session.jsonl
```

## API Credential Management

PinnacleMM securely stores and manages exchange API credentials:

- **Encryption**: AES-256-CBC encryption with PBKDF2 key derivation
- **Master Password**: Single password to unlock all exchange credentials
- **Secure Storage**: Credentials encrypted in `config/secure_config.json`
- **Interactive Setup**: User-friendly credential configuration interface

### Setting Up API Credentials

1. **Run credential setup**:
```bash
scripts/run-native.sh --setup-credentials
# or manually:
./pinnaclemm --setup-credentials
```

2. **Enter master password** (secure input with hidden characters - this encrypts all API keys with AES-256-CBC + unique salt + 100,000 PBKDF2 iterations)

3. **Configure exchange credentials**:
   - **Coinbase Pro**: API Key + API Secret + Passphrase
   - **Other exchanges**: API Key + API Secret (+ optional passphrase)
   - All inputs are validated and sanitized before encryption

4. **Verify setup**:
```bash
scripts/run-native.sh -m live -v
# or manually:
./pinnaclemm --mode live --exchange coinbase --symbol BTC-USD --verbose
```

### Supported Exchanges

#### WebSocket Connectivity
- **Coinbase Pro**: Live market data via WebSocket

#### FIX Protocol Connectivity
- **Interactive Brokers**: FIX 4.2 support (requires IB FIX API agreement)
- **Coinbase Pro**: FIX 4.4 institutional connectivity (framework ready)
- **Kraken**: FIX 4.4 institutional connectivity (framework ready)
- **Binance**: FIX 4.4 institutional connectivity (framework ready)

#### In Development
- **Kraken, Gemini, Binance, Bitstamp**: WebSocket connectors in development

For more detailed instructions, see the [Getting Started Guide](docs/user_guide/getting_started.md).

## Script Documentation

### Native Script (`scripts/run-native.sh`)

**Available Commands:**
```bash
# Execution modes
scripts/run-native.sh                     # Simulation mode (default)
scripts/run-native.sh -m live -v          # Live mode with verbose logging
scripts/run-native.sh -s ETH-USD          # Custom trading symbol
scripts/run-native.sh -e coinbase         # Specify exchange

# Build commands
scripts/run-native.sh build               # Build project
scripts/run-native.sh clean               # Clean build directory
scripts/run-native.sh rebuild             # Clean and rebuild

# Testing and benchmarks
scripts/run-native.sh test                # Run all tests
scripts/run-native.sh benchmark           # Run performance benchmarks

# Setup
scripts/run-native.sh --setup-credentials # Configure API credentials (secure input)
scripts/run-native.sh --help              # Show help

# Cleanup
scripts/cleanup.sh                        # Interactive cleanup utility
```

**Features:**
- **Auto-build**: Builds project if executable not found
- **Dependency checking**: Validates cmake, make, g++/clang++
- **Cross-platform**: Works on macOS and Linux
- **Test runner**: Comprehensive test suite execution
- **Live trading**: Real WebSocket connections to exchanges

### Docker Script (`scripts/run-docker.sh`)

**Available Commands:**
```bash
# Execution modes
scripts/run-docker.sh                     # Simulation mode (detached)
scripts/run-docker.sh -m live -v          # Live mode (interactive)
scripts/run-docker.sh -s ETH-USD          # Custom trading symbol

# Container management
scripts/run-docker.sh build               # Build Docker image
scripts/run-docker.sh logs                # View container logs
scripts/run-docker.sh stop                # Stop and remove containers
scripts/run-docker.sh clean               # Remove containers and image
scripts/run-docker.sh --help              # Show help
```

**Features:**
- **Containerized**: Isolated execution environment
- **Auto-build**: Builds Docker image if not found
- **Container lifecycle**: Complete start/stop/clean management
- **Volume mounting**: Credential persistence for live mode
- **Production ready**: Optimized for deployment

## Docker Deployment

### Using Docker Script (Recommended)
```bash
# Quick start
scripts/run-docker.sh                     # Simulation mode
scripts/run-docker.sh -m live -v          # Live trading

# Container management
scripts/run-docker.sh logs                # Monitor logs
scripts/run-docker.sh stop                # Stop trading
```

### Using Pre-built Images (GitHub Container Registry)
```bash
# Pull the latest image
docker pull ghcr.io/chizy7/pinnaclemm:latest

# Run simulation mode
docker run --rm ghcr.io/chizy7/pinnaclemm:latest

# Setup credentials for live trading
docker run -it --rm -v $(pwd)/config:/app/config \
  ghcr.io/chizy7/pinnaclemm:latest --setup-credentials

# Run live mode with credentials
docker run -it --rm -v $(pwd)/config:/app/config \
  ghcr.io/chizy7/pinnaclemm:latest --mode live --exchange coinbase --symbol BTC-USD --verbose
```

### Manual Docker Commands
```bash
# Build the Docker image locally
docker build -t pinnaclemm .

# Run simulation mode
docker run -d --name pinnaclemm pinnaclemm

# Run live mode with credentials
docker run -it --name pinnaclemm-live \
  -v $(pwd)/config:/app/config \
  pinnaclemm --mode live --exchange coinbase --symbol BTC-USD --verbose
```

## Key Components

- **Order Book Engine**: Ultra-fast matching engine with lock-free operations
- **Market Making Strategy**: Adaptive pricing based on market conditions
- **Risk Manager**: Lock-free pre-trade risk checks with position, exposure, and loss limits
- **Circuit Breaker**: Market circuit breaker with 8 triggers and automatic recovery
- **VaR Engine**: Real-time Value at Risk with Monte Carlo simulations on a background thread
- **Alert Manager**: Alerting system with throttling and real-time WebSocket delivery
- **Disaster Recovery**: Atomic state persistence, position reconciliation, and backup management
- **ML Spread Optimization**: Neural network-based spread prediction with ~1-2μs latency
- **Order Book Flow Analyzer**: Real-time analysis of order flow patterns and market microstructure
- **Market Impact Predictor**: Advanced models for predicting price impact of trades
- **Market Regime Detector**: Hidden Markov Model-based detection of 8 market regimes
- **RL Parameter Adapter**: Reinforcement learning for dynamic strategy parameter optimization
- **Advanced Backtesting Engine**: Historical replay with Monte Carlo analysis and statistical testing
- **Real-Time Visualization**: Web-based dashboard with Chart.js and D3.js visualization
- **FIX Protocol Engine**: Professional-grade FIX connectivity for institutional trading
- **Persistence System**: Crash recovery with memory-mapped files
- **Exchange Simulator**: Realistic market simulation for testing

## Documentation

### Core System
- [System Architecture](docs/architecture/system_overview.md)
- [Getting Started Guide](docs/user_guide/getting_started.md)
- [API Reference](docs/api/reference.md)
- [Project Roadmap](docs/ROADMAP.md)

### Risk Management & Production
- [Risk Management](docs/RISK_MANAGEMENT.md) - **Pre-trade checks, VaR, circuit breaker, alerting**
- [Disaster Recovery](docs/DISASTER_RECOVERY.md) - **Operational runbook for crash recovery and backups**
- [Kubernetes Deployment](docs/KUBERNETES_DEPLOYMENT.md) - **Production K8s deployment guide**

### Advanced Features (ML)
- [ML Spread Optimization](docs/ML_SPREAD_OPTIMIZATION.md) - **Neural network-based spread prediction**
- [Order Book Flow Analysis](docs/ORDER_BOOK_FLOW_ANALYSIS.md) - **Real-time market microstructure analysis**
- [Market Impact Prediction](docs/MARKET_IMPACT_PREDICTION.md) - **Advanced trade impact modeling**
- [RL Parameter Adaptation](docs/RL_PARAMETER_ADAPTATION.md) - **Reinforcement learning optimization**
- [Market Regime Detection](docs/MARKET_REGIME_DETECTION.md) - **Hidden Markov Model regime detection**
- [Advanced Backtesting](docs/ADVANCED_BACKTESTING.md) - **Historical data replay and Monte Carlo analysis**
- [Strategy Performance Visualization](docs/STRATEGY_PERFORMANCE_VISUALIZATION.md) - **Real-time web dashboard**
- [WebSocket Testing Guide](docs/WEBSOCKET_TESTING.md) - **WebSocket connection testing and troubleshooting**

## Real-Time Visualization Dashboard

The PinnacleMM system includes a professional web-based dashboard for real-time performance monitoring and analysis:

### Features
- **Live Performance Metrics**: Real-time P&L, position, Sharpe ratio, and win rate tracking
- **Interactive Charts**: Time-series visualization with Chart.js for performance trends
- **Market Data Visualization**: Live order book, spread analysis, and trade flow
- **ML Model Metrics**: Neural network accuracy, prediction times, and regime detection
- **WebSocket Real-Time Updates**: Sub-100ms latency for live data streaming
- **Multiple Dashboard Types**: Main dashboard, live trading dashboard, and test dashboard
- **Fixed WebSocket Issues**: Resolved segmentation faults and connection stability issues

### Access Dashboard

#### Simulation Mode Dashboard
1. **Start the system with visualization enabled:**
   ```bash
   cd build && ./pinnaclemm --mode simulation --enable-ml --enable-visualization --viz-ws-port 8089 --viz-api-port 8090
   ```

2. **Open the dashboard:**
   - **Main Dashboard**: `visualization/static/index.html` (full-featured dashboard)
   - **Test Dashboard**: `build/test_dashboard.html` (WebSocket connection testing)
   - **WebSocket**: `ws://localhost:8089` (or your custom port)
   - **REST API**: `http://localhost:8090` (or your custom port)

#### Live Trading Dashboard
1. **Start live trading with visualization:**
   ```bash
   cd build && ./pinnaclemm --mode live --exchange coinbase --enable-ml --enable-visualization --viz-ws-port 8085 --viz-api-port 8086 --verbose
   ```

2. **Access live dashboard:**
   - **Live Dashboard**: `visualization/static/live_dashboard.html` (optimized for live trading)
   - **Test Dashboard**: `build/test_dashboard.html` (connection diagnostics)
   - **WebSocket**: `ws://localhost:8085`
   - **REST API**: `http://localhost:8086`

### Dashboard Components
- **Performance Cards**: Key metrics at-a-glance with real-time updates
- **Interactive Charts**: P&L, positions, spreads, ML accuracy with Chart.js
- **Real-Time Status**: Connection status indicators and live data feed monitoring
- **Strategy Controls**: Multiple strategy monitoring and comparison
- **Market Regime Visualization**: Real-time regime detection with confidence indicators
- **ML Metrics Panel**: Model accuracy, prediction latency, and retrain statistics
- **Risk Analysis**: Real-time VaR (historical, parametric, Monte Carlo), circuit breaker status, position/exposure limits, drawdown tracking, and alerting

### Technical Details
- **Frontend**: HTML5/CSS3/JavaScript with Chart.js and D3.js
- **Backend**: Boost.Beast WebSocket and HTTP servers (migrated from websocketpp for compatibility)
- **Data Format**: JSON with efficient real-time streaming
- **Update Frequency**: 1-second intervals with configurable rates
- **Connection Stability**: Fixed shared_ptr lifecycle management for stable WebSocket connections
- **Debug Support**: Enhanced logging with `SPDLOG_LEVEL=debug` for troubleshooting

### Testing WebSocket Connections
If you encounter connection issues, use the test dashboard:
```bash
# Start with debug logging
SPDLOG_LEVEL=debug ./pinnaclemm --mode simulation --enable-visualization --viz-ws-port 8089 --viz-api-port 8090 --verbose

# Open test dashboard
open build/test_dashboard.html
# or manually: file:///path/to/PinnacleMM/build/test_dashboard.html
```

### Exchange Integration
- [FIX Protocol Integration Guide](docs/FIX_PROTOCOL_INTEGRATION.md)
- [FIX Testing Guide](docs/TESTING_GUIDE.md)
- [Interactive Brokers Setup](docs/IB_TESTING_GUIDE.md)

### System Administration
- [Kubernetes Deployment](docs/KUBERNETES_DEPLOYMENT.md)
- [Disaster Recovery Runbook](docs/DISASTER_RECOVERY.md)
- [Persistence System](docs/architecture/persistence.md)
- [Recovery Guide](docs/user_guide/recovery.md)
- [Security & API Key Management](docs/security/credentials.md)
- [Certificate Pinning Guide](docs/security/CERTIFICATE_PINNING.md)

## Technology Stack

- **Core Engine**: C++20
- **Build System**: CMake
- **Testing**: Google Test
- **Performance Benchmarking**: Google Benchmark
- **Concurrency**: Lock-free algorithms, std::atomic
- **Networking**: Boost.Beast WebSocket, hffix FIX protocol
- **Machine Learning**: Custom neural networks, Hidden Markov Models, reinforcement learning
- **Visualization**: HTML5/CSS3/JavaScript frontend, Chart.js, D3.js, WebSocket real-time updates
- **Security**: OpenSSL for encryption
- **Configuration**: nlohmann/json
- **Containerization**: Docker
- **Security**: AES-256-CBC encryption, PBKDF2 key derivation, input validation, audit logging, rate limiting

## Performance

PinnacleMM achieves exceptional performance metrics:

- **Order Book Update Latency**: <1 μs (microsecond)
- **Order Execution Latency**: <50 μs (end-to-end)
- **Pre-Trade Risk Check**: ~750ns (lock-free, 8 sequential checks)
- **Circuit Breaker Check**: ~5ns (single atomic load)
- **ML Prediction Latency**: 1-3 μs (neural network inference)
- **Throughput**: 100,000+ messages per second
- **Recovery Time**: <5 seconds for full system recovery
- **Memory Footprint**: <100 MB for core engine
- **Dashboard Update Latency**: <100ms (real-time visualization)
- **Regime Detection**: Real-time with 85%+ confidence accuracy

### Testing Integration

```bash
# Test FIX protocol components
cd build
./fix_basic_test

# Expected output:
# ✓ Factory instance created
# ✓ Interactive Brokers FIX support: Yes
# ✓ Configuration system working
# ✓ Order creation working

# Test advanced order routing system
./routing_test

# Expected output:
# All OrderRouter tests passed successfully!
# ✓ BestPriceStrategy, TWAP, VWAP, MarketImpact all working
# ✓ Multi-venue execution with 1ms latency
# ✓ 8 completed fills across multiple strategies

# Test risk management components (Phase 4)
./risk_manager_tests          # 11 tests - pre-trade checks, position limits
./circuit_breaker_tests       # 10 tests - state machine, triggers
./var_engine_tests            # 8 tests - VaR calculations
./alert_manager_tests         # 8 tests - alerting, throttling
./disaster_recovery_tests     # 8 tests - state persistence, backups
./risk_check_benchmark        # Risk check latency benchmarks

# Memory safety validation with Address Sanitizer (development builds)
cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON .. && make -j8
./pinnaclemm --mode simulation --symbol BTC-USD --verbose
# ASan will detect memory leaks, buffer overflows, and use-after-free errors
```

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Contact Me

For questions, feedback, or collaboration opportunities:

- **Email**: [chizy@chizyhub.com](mailto:chizy@chizyhub.com)
- **X(Twitter)**: [![Twitter Follow](https://img.shields.io/twitter/follow/chizyization?style=social)](https://x.com/Chizyization)
