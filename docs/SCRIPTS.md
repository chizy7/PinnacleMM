# Helper Scripts Reference

PinnacleMM ships with bash scripts that handle building, running, testing, and container management. Both scripts print full usage with `--help`.

## Native Script (`scripts/run-native.sh`)

Recommended for development and live trading.

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

## Docker Script (`scripts/run-docker.sh`)

Recommended for production and simulation deployments.

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

## Pre-built Docker Images

Images are published to GitHub Container Registry:

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

## Choosing Between Them

| Feature | Native (`run-native.sh`) | Docker (`run-docker.sh`) |
|---------|--------------------------|--------------------------|
| Simulation mode | Yes | Yes |
| Live trading | Real WebSocket data | Known WebSocket config limitation |
| Auto-build | Builds if needed | Auto Docker build |
| Test runner | `test` command | Not included |
| Benchmarks | `benchmark` command | Not included |
| Credential setup | Interactive setup | Volume mounting |
| Dependencies | cmake, make, g++/clang++ | Docker only |
| Best for | Development and live trading | Production and simulation |
