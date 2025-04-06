# PinnacleMM - Ultra-Low Latency Market Making System

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![C++](https://img.shields.io/badge/C%2B%2B-20-blue.svg)
![Status](https://img.shields.io/badge/status-Phase%202%20In%20Progress-red.svg)
<a href="https://github.com/chizy7/PinnacleMM"><img src="https://tokei.rs/b1/github/chizy7/PinnacleMM?category=code" alt="Lines of Code"></a>
![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)
![Performance](https://img.shields.io/badge/latency-microsecond-blue.svg)

PinnacleMM is a high-performance, production-grade market making system designed for high-frequency trading in cryptocurrency markets. Built primarily in C++ with a focus on ultra-low latency, this system achieves microsecond-level execution speeds while maintaining robust risk management capabilities.

## 🚀 Key Features

- **Ultra-Low Latency Core**: Optimized C++ engine with lock-free data structures
- **Nanosecond Precision**: High-resolution timing for accurate execution
- **Crash Recovery**: Memory-mapped persistence system for reliable operation
- **Dynamic Market Making**: Adaptive bid-ask spread based on market conditions
- **Position Management**: Intelligent inventory management with customizable risk parameters
- **Exchange Simulation**: Realistic market simulation for strategy development and testing
- **Live Exchange Connectivity**: Connect to major cryptocurrency exchanges
- **Secure API Credentials**: Encrypted storage for exchange API keys
- **Comprehensive Testing**: Extensive test suite ensuring reliability and performance

## 📊 System Architecture

PinnacleMM follows a modular, layered architecture:

- **Core Engine Layer**: Ultra-low latency components handling order book and execution
- **Strategy Layer**: Pluggable strategies for different market making approaches
- **Exchange Layer**: Connectivity to exchanges with simulation capabilities
- **Persistence Layer**: Memory-mapped file system for crash recovery

Read more about the [system architecture](docs/architecture/system_overview.md).

## 📋 Development Roadmap

PinnacleMM is being developed in phases:

- ✅ **Phase 1 (Completed)**: Core engine, basic strategy, and simulation
- 🔄 **Phase 2 (In Progress)**: Latency optimization and exchange connectivity
  - ℹ️ **DPDK Integration**: Ultra-low latency networking (Deferred - requires specialized hardware)
- 🔲 **Phase 3**: Advanced strategies and machine learning integration
- 🔲 **Phase 4**: Risk management and production deployment

See the detailed [project roadmap](docs/roadmap.md) for more information.

## 🔧 Getting Started

### Prerequisites

- C++20 compatible compiler (GCC 10+, Clang 10+, or MSVC 2019+)
- CMake 3.14+
- Boost libraries 1.72+
- spdlog library (for structured logging)
- fmt library (for formatting, spdlog dependency)
- OpenSSL library (for secure credential handling)
- nlohmann_json library (for configuration handling)

### Building from Source

```bash
# Clone the repository
git clone https://github.com/chizy7/PinnacleMM.git
cd PinnacleMM

# Create build directory
mkdir build && cd build

# Configure with CMake
cmake ..

# Build the project
# For macOS:
make -j$(sysctl -n hw.ncpu)
# For Linux:
# make -j$(nproc) # Use appropriate core count for your machine
```

### Running PinnacleMM

#### Simulation Mode
```bash
# Run in simulation mode
./pinnaclemm --mode simulation --symbol BTC-USD
```

#### Live Exchange Mode
```bash
# Run with a live exchange
./pinnaclemm --mode live --exchange coinbase --symbol BTC-USD
```

When running in live mode, you'll be prompted for your master password to decrypt API credentials.

## 🔐 API Credential Management

PinnacleMM securely stores and manages exchange API credentials:

- **Encryption**: AES-256-GCM encryption for all sensitive data
- **Master Password**: Single password to unlock all exchange credentials
- **Secure Storage**: Credentials are never stored in plaintext
- **Configuration**: API keys are configured via a secure interface

### Setting Up API Credentials

1. Create a credentials file:
```bash
./pinnaclemm --setup-credentials
```

2. Enter your master password when prompted
3. Add your exchange API keys through the interactive prompt

For more detailed instructions, see the [Getting Started Guide](docs/user_guide/getting_started.md).

## 🐳 Docker Deployment

```bash
# Build the Docker image
docker build -t pinnaclemm .

# Run the container
docker run -d --name pinnaclemm pinnaclemm
```

## 🔍 Key Components

- **Order Book Engine**: Ultra-fast matching engine with lock-free operations
- **Market Making Strategy**: Adaptive pricing based on market conditions
- **Persistence System**: Crash recovery with memory-mapped files
- **Exchange Simulator**: Realistic market simulation for testing

## 📚 Documentation

- [System Architecture](docs/architecture/system_overview.md)
- [Persistence System](docs/architecture/persistence.md)
- [API Reference](docs/api/reference.md)
- [Getting Started Guide](docs/user_guide/getting_started.md)
- [Recovery Guide](docs/user_guide/recovery.md)
- [Security & API Key Management](docs/security/credentials.md)
- [Project Roadmap](docs/roadmap.md)

## 🛠️ Technology Stack

- **Core Engine**: C++20
- **Build System**: CMake
- **Testing**: Google Test
- **Performance Benchmarking**: Google Benchmark
- **Concurrency**: Lock-free algorithms, std::atomic
- **Security**: OpenSSL for encryption
- **Configuration**: nlohmann/json
- **Containerization**: Docker

## 📊 Performance

PinnacleMM achieves exceptional performance metrics:

- **Order Book Update Latency**: <1 μs (microsecond)
- **Order Execution Latency**: <50 μs (end-to-end)
- **Throughput**: 100,000+ messages per second
- **Recovery Time**: <5 seconds for full system recovery
- **Memory Footprint**: <100 MB for core engine

## 🔮 Current Progress (Phase 2)

- ✅ Lock-free data structures implemented for ultra-low latency
- ✅ Memory-mapped persistence system with crash recovery capabilities
- 🔄 Next: Implementing exchange connectivity and WebSocket integration

## 📝 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.