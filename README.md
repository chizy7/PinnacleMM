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
    <a href="https://github.com/chizy7/PinnacleMM"><img src="https://img.shields.io/badge/status-Phase%203%20In%20Progress-yellow.svg" alt="Status"></a>
    <a href="https://github.com/chizy7/PinnacleMM"><img src="https://tokei.rs/b1/github/chizy7/PinnacleMM?category=code" alt="Lines of Code"></a>
    <a href="https://github.com/chizy7/PinnacleMM"><img src="https://img.shields.io/badge/build-passing-brightgreen.svg" alt="Build Status"></a>
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
- **Enterprise Security**: AES-256-CBC encryption with unique salts, 100,000 PBKDF2 iterations, secure password input, comprehensive input validation, audit logging, rate limiting, and certificate pinning
- **Comprehensive Testing**: Extensive test suite ensuring reliability and performance

## System Architecture

PinnacleMM follows a modular, layered architecture:

- **Core Engine Layer**: Ultra-low latency components handling order book and execution
- **Strategy Layer**: Pluggable strategies for different market making approaches
- **Exchange Layer**: Multi-protocol connectivity (WebSocket, FIX) with simulation capabilities
- **Persistence Layer**: Memory-mapped file system for crash recovery

Read more about the [system architecture](docs/architecture/system_overview.md).

## Development Roadmap

PinnacleMM is being developed in phases:

- ✅ **Phase 1 (Completed)**: Core engine, basic strategy, and simulation
- ✅ **Phase 2 (Completed)**: Latency optimization, exchange connectivity, and smart order routing
  - ✅ Live WebSocket integration (Coinbase Pro)
  - ✅ FIX protocol support (Interactive Brokers)
  - ✅ Advanced order routing (BEST_PRICE, TWAP, VWAP, MARKET_IMPACT)
  - ℹ️ **DPDK Integration**: Ultra-low latency networking (Deferred - requires specialized hardware)
- 🔄 **Phase 3**: Advanced strategies and machine learning integration
- 🔲 **Phase 4**: Risk management and production deployment

See the detailed [project roadmap](docs/ROADMAP.md) for more information.

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
./run-native.sh                    # Simulation mode (auto-builds if needed)
./run-native.sh -m live -v         # Live trading with verbose logs
./run-native.sh --setup-credentials # Configure API keys
```

#### **Docker Execution** (Recommended for Production)
```bash
# Build and run in one command
./run-docker.sh                    # Simulation mode
./run-docker.sh -m live -v         # Live trading mode
./run-docker.sh build              # Build Docker image
./run-docker.sh logs               # View container logs
```

### Manual Building from Source

```bash
# Clone the repository
git clone https://github.com/chizy7/PinnacleMM.git
cd PinnacleMM

# Build with native script (recommended)
./run-native.sh build

# Or build manually
mkdir build && cd build
cmake ..
make -j$(sysctl -n hw.ncpu)  # macOS
# make -j$(nproc)            # Linux
```

### Script Features Comparison
> **Note**: I will update later on after completing phase 4 and 5, cleaning up the code and getting PinnacleMM ready for optimization and production deployment.

| Feature | Native Script (`./run-native.sh`) | Docker Script (`./run-docker.sh`) |
|---------|-----------------------------------|-----------------------------------|
| **Simulation Mode** | ✅ Perfect | ✅ Perfect |
| **Live Trading** | ✅ Real WebSocket data | ⚠️ WebSocket config issue |
| **Auto-Build** | ✅ Builds if needed | ✅ Auto Docker build |
| **Test Runner** | ✅ `./run-native.sh test` | ❌ Not included |
| **Benchmarks** | ✅ `./run-native.sh benchmark` | ❌ Not included |
| **Credential Setup** | ✅ Interactive setup | ✅ Volume mounting |
| **Dependency Check** | ✅ cmake, make, g++ | ✅ Docker only |
| **Best For** | Development & Live Trading | Production & Simulation |

### Running PinnacleMM

#### Simulation Mode
```bash
# Using scripts (recommended)
./run-native.sh                    # Native execution
./run-docker.sh                    # Docker execution

# Manual execution
cd build && ./pinnaclemm --mode simulation --symbol BTC-USD
```

#### Live Exchange Mode
```bash
# Setup credentials first
./run-native.sh --setup-credentials

# Live trading with scripts
./run-native.sh -m live -v         # Native (recommended for live)
./run-docker.sh -m live -v         # Docker

# Manual execution
cd build && ./pinnaclemm --mode live --exchange coinbase --symbol BTC-USD --verbose
```

When running in live mode, you'll be prompted for your master password to decrypt API credentials.

## API Credential Management

PinnacleMM securely stores and manages exchange API credentials:

- **Encryption**: AES-256-CBC encryption with PBKDF2 key derivation
- **Master Password**: Single password to unlock all exchange credentials
- **Secure Storage**: Credentials encrypted in `config/secure_config.json`
- **Interactive Setup**: User-friendly credential configuration interface

### Setting Up API Credentials

1. **Run credential setup**:
```bash
./run-native.sh --setup-credentials
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
./run-native.sh -m live -v
# or manually:
./pinnaclemm --mode live --exchange coinbase --symbol BTC-USD --verbose
```

### Supported Exchanges

#### WebSocket Connectivity
- ✅ **Coinbase Pro**: Live market data via WebSocket

#### FIX Protocol Connectivity
- ✅ **Interactive Brokers**: FIX 4.2 support (requires IB FIX API agreement)
- 🔄 **Coinbase Pro**: FIX 4.4 institutional connectivity (framework ready)
- 🔄 **Kraken**: FIX 4.4 institutional connectivity (framework ready)
- 🔄 **Binance**: FIX 4.4 institutional connectivity (framework ready)

#### In Development
- 🔄 **Kraken, Gemini, Binance, Bitstamp**: WebSocket connectors in development

For more detailed instructions, see the [Getting Started Guide](docs/user_guide/getting_started.md).

## Script Documentation

### Native Script (`./run-native.sh`)

**Available Commands:**
```bash
# Execution modes
./run-native.sh                     # Simulation mode (default)
./run-native.sh -m live -v          # Live mode with verbose logging
./run-native.sh -s ETH-USD          # Custom trading symbol
./run-native.sh -e coinbase         # Specify exchange

# Build commands
./run-native.sh build               # Build project
./run-native.sh clean               # Clean build directory
./run-native.sh rebuild             # Clean and rebuild

# Testing and benchmarks
./run-native.sh test                # Run all tests
./run-native.sh benchmark           # Run performance benchmarks

# Setup
./run-native.sh --setup-credentials # Configure API credentials (secure input)
./run-native.sh --help              # Show help

# Cleanup
./cleanup.sh                        # Interactive cleanup utility
```

**Features:**
- **Auto-build**: Builds project if executable not found
- **Dependency checking**: Validates cmake, make, g++/clang++
- **Cross-platform**: Works on macOS and Linux
- **Test runner**: Comprehensive test suite execution
- **Live trading**: Real WebSocket connections to exchanges

### Docker Script (`./run-docker.sh`)

**Available Commands:**
```bash
# Execution modes
./run-docker.sh                     # Simulation mode (detached)
./run-docker.sh -m live -v          # Live mode (interactive)
./run-docker.sh -s ETH-USD          # Custom trading symbol

# Container management
./run-docker.sh build               # Build Docker image
./run-docker.sh logs                # View container logs
./run-docker.sh stop                # Stop and remove containers
./run-docker.sh clean               # Remove containers and image
./run-docker.sh --help              # Show help
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
./run-docker.sh                     # Simulation mode
./run-docker.sh -m live -v          # Live trading

# Container management
./run-docker.sh logs                # Monitor logs
./run-docker.sh stop                # Stop trading
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
- **FIX Protocol Engine**: Professional-grade FIX connectivity for institutional trading
- **Persistence System**: Crash recovery with memory-mapped files
- **Exchange Simulator**: Realistic market simulation for testing

## Documentation

- [System Architecture](docs/architecture/system_overview.md)
- [FIX Protocol Integration Guide](docs/FIX_PROTOCOL_INTEGRATION.md)
- [FIX Testing Guide](docs/TESTING_GUIDE.md)
- [Interactive Brokers Setup](docs/IB_TESTING_GUIDE.md)
- [Persistence System](docs/architecture/persistence.md)
- [API Reference](docs/api/reference.md)
- [Getting Started Guide](docs/user_guide/getting_started.md)
- [Recovery Guide](docs/user_guide/recovery.md)
- [Security & API Key Management](docs/security/credentials.md)
- [Project Roadmap](docs/ROADMAP.md)

## Technology Stack

- **Core Engine**: C++20
- **Build System**: CMake
- **Testing**: Google Test
- **Performance Benchmarking**: Google Benchmark
- **Concurrency**: Lock-free algorithms, std::atomic
- **Networking**: Boost.Beast WebSocket, hffix FIX protocol
- **Security**: OpenSSL for encryption
- **Configuration**: nlohmann/json
- **Containerization**: Docker
- **Security**: AES-256-CBC encryption, PBKDF2 key derivation, input validation, audit logging, rate limiting

## Performance

PinnacleMM achieves exceptional performance metrics:

- **Order Book Update Latency**: <1 μs (microsecond)
- **Order Execution Latency**: <50 μs (end-to-end)
- **Throughput**: 100,000+ messages per second
- **Recovery Time**: <5 seconds for full system recovery
- **Memory Footprint**: <100 MB for core engine

## Current Progress (Phase 2 - COMPLETED)

- Lock-free data structures implemented for ultra-low latency
- Memory-mapped persistence system with crash recovery capabilities
- **Live Exchange Connectivity**: Coinbase Pro WebSocket integration with real-time market data
- **FIX Protocol Integration**: Professional-grade FIX connectivity for institutional exchanges
  - Interactive Brokers FIX 4.2 support (requires IB FIX API agreement)
  - hffix library integration for ultra-low latency message processing
  - Factory pattern for multiple exchange support
- **Advanced Order Routing**: Institutional-grade smart order routing system
  - 4 routing algorithms: BEST_PRICE, TWAP, VWAP, MARKET_IMPACT
  - Multi-venue execution with intelligent order splitting
  - Real-time market data integration for dynamic venue selection
  - Ultra-low latency: 1ms average execution time
- **Comprehensive Performance Benchmarking**: Production-grade performance validation
  - Strategy planning: 83ns-2.3μs across all algorithms
  - End-to-end routing: 1.88μs average latency
  - System throughput: 640k+ operations/second
  - Nanosecond-precision performance metrics
- **Enhanced Security Infrastructure**:
  - AES-256-CBC encryption with unique random salts (replacing fixed salt vulnerability)
  - PBKDF2 key derivation increased from 10,000 to 100,000 iterations
  - Secure password input with terminal masking
  - Comprehensive input validation framework preventing injection attacks
  - Certificate pinning for WebSocket SSL connections
  - Audit logging system for security events
  - Rate limiting with configurable policies
  - Secure memory clearing to prevent credential leakage
- **Interactive credential setup utility**
- **Real-time ticker data processing** ($109K+ BTC prices)
- 🔄 **Next**: Full order book data (requires Coinbase authentication) and live FIX trading

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
```

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
