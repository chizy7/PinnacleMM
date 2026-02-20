# Getting Started with PinnacleMM

This guide will help you set up and run PinnacleMM in your local environment.

## Prerequisites

To build and run PinnacleMM, you'll need the following:

- **C++20 compatible compiler** (GCC 10+, Clang 10+, or MSVC 2019+)
- **CMake** (version 3.14 or higher)
- **Boost libraries** (version 1.72 or higher)
- **spdlog** (for structured logging)
- **fmt** (formatting library, required by spdlog)
- **Google Test** (for running unit tests)
- **Git** (for cloning the repository)

Optional dependencies for advanced features:
- **Intel TBB** (for parallel algorithms)
- **Google Benchmark** (for performance benchmarks)

## Building from Source

### 1. Clone the Repository

```bash
git clone https://github.com/chizy7/PinnacleMM.git
cd PinnacleMM
```

### 2. Create a Build Directory

```bash
mkdir build
cd build
```

### 3. Configure with CMake

```bash
cmake ..
```

For a release build with optimizations:
```bash
cmake .. -DCMAKE_BUILD_TYPE=Release
```

With optional components:
```bash
cmake .. -DBUILD_TESTS=ON -DBUILD_BENCHMARKS=ON -DUSE_TBB=ON
```

### 4. Build the Project

```bash
make -j$(nproc)  # Use the number of available CPU cores
```

### 5. Run Tests (Optional)

```bash
ctest
```

Or run a specific test:
```bash
./tests/orderbook_tests
```

## Running PinnacleMM

### Running in Simulation Mode

The simplest way to start is with the simulation mode (no API keys needed):

```bash
./pinnaclemm --mode simulation --symbol BTC-USD
```

### Running with Live Market Data

**First-time setup** - Configure your exchange API credentials:

```bash
# Using the native script (recommended)
scripts/run-native.sh --setup-credentials

# Or directly
./pinnaclemm --setup-credentials
```

Follow the interactive prompts to:
1. Set a master password (secure input with hidden characters, encrypts all API keys with AES-256-CBC + unique salt)
2. Enter your Coinbase Pro API credentials (input validation prevents common attacks)
3. Optionally configure other exchanges with comprehensive security validation

**Run with live Coinbase market data**:

```bash
./pinnaclemm --mode live --exchange coinbase --symbol BTC-USD
```

**With verbose logging** to see real-time market updates:

```bash
./pinnaclemm --mode live --exchange coinbase --symbol BTC-USD --verbose
```

You'll see live BTC prices (currently ~$109,200+) and real-time trading activity.

### Running in Backtest Mode

Backtest mode runs a strategy against historical (or synthetic) data, prints a detailed performance report, and exits. No API keys or exchange connection required.

**Basic backtest with synthetic data** (generates data automatically if no CSV is found):

```bash
./pinnaclemm --mode backtest --symbol BTC-USD
```

**With ML-enhanced strategy**:

```bash
./pinnaclemm --mode backtest --symbol BTC-USD --enable-ml
```

**Custom parameters**:

```bash
./pinnaclemm --mode backtest --symbol BTC-USD \
  --initial-balance 50000 \
  --trading-fee 0.002 \
  --slippage-bps 5.0
```

**Using your own historical data**: Place a CSV file at `backtest_results/data/BTC-USD.csv` with the format:

```csv
timestamp,symbol,price,bid,ask,volume
1640995200000000000,BTC-USD,47892.50,47890.00,47895.00,1250.75
```

Then run the backtest as usual. Results (JSON) are saved to the `backtest_results/` directory.

**Custom output directory**:

```bash
./pinnaclemm --mode backtest --symbol BTC-USD --backtest-output my_results
# Data CSVs should be at my_results/data/BTC-USD.csv
# Results are saved to my_results/
```

### Command Line Options

PinnacleMM supports the following command line options:

- `--help`: Show help message
- `--setup-credentials`: Interactive API credential setup
- `--symbol <symbol>`: Trading symbol (default: BTC-USD)
- `--mode <mode>`: Trading mode (simulation/live/backtest) (default: simulation)
- `--exchange <name>`: Exchange name (coinbase/kraken/gemini/binance/bitstamp)
- `--config <file>`: Configuration file (default: config/default_config.json)
- `--logfile <file>`: Log file (default: pinnaclemm.log)
- `--verbose`: Enable verbose output with real-time market data
- `--lock-free`: Use lock-free data structures (default: enabled)
- `--json-log`: Enable JSON structured data export (JSONL format)
- `--json-log-file <file>`: JSON log file path (default: pinnaclemm_data.jsonl)
- `--backtest-output <dir>`: Backtest output directory (default: backtest_results)
- `--initial-balance <val>`: Starting balance for backtest (default: 100000.0)
- `--trading-fee <val>`: Trading fee as decimal, e.g. 0.001 = 0.1% (default: 0.001)
- `--enable-slippage <bool>`: Enable slippage simulation (default: true)
- `--slippage-bps <val>`: Slippage in basis points (default: 2.0)

### Example Configuration

A basic configuration file example:

```json
{
  "strategy": {
    "name": "BasicMarketMaker",
    "baseSpreadBps": 10.0,
    "minSpreadBps": 5.0,
    "maxSpreadBps": 50.0,
    "orderQuantity": 0.01,
    "targetPosition": 0.0,
    "maxPosition": 10.0,
    "inventorySkewFactor": 0.5
  },
  "execution": {
    "quoteUpdateIntervalMs": 100,
    "orderTimeoutMs": 5000
  },
  "risk": {
    "maxDrawdownPct": 5.0,
    "stopLossPct": 3.0,
    "takeProfitPct": 5.0
  }
}
```

## Docker Deployment

PinnacleMM can also be run inside a Docker container.

### Building the Docker Image

```bash
docker build -t pinnaclemm .
```

The Dockerfile automatically installs all required dependencies including Boost, spdlog, and fmt libraries.

### Running the Container

```bash
docker run -d --name pinnaclemm pinnaclemm
```

With custom options:
```bash
docker run -d --name pinnaclemm pinnaclemm --symbol ETH-USD --verbose
```

## Repository Structure

| Directory | Description |
|-----------|-------------|
| `core/` | Core engine components including orderbook, execution, risk management, and utilities |
| `strategies/` | Market making strategies and configuration |
| `exchange/` | Exchange connectivity and simulation for testing |
| `tests/` | Unit tests and performance benchmarks |
| `docs/` | Project documentation including architecture diagrams and user guides |
| `config/` | Configuration files and settings |
| `deployment/` | Docker and CI/CD deployment configurations **{currently local only but dockerfile works}** |
| `benchmarks/` | Performance benchmarking tools and micro-benchmarks |
| `research/` | Research and experimental features |

## Development Workflow

1. **Configure Environment**: Set up your development environment with the required dependencies
2. **Build in Debug Mode**: During development, build with debug mode for easier debugging
3. **Run Unit Tests**: Ensure all tests pass after making changes
4. **Run Performance Tests**: Check that your changes don't negatively impact performance
5. **Build Release Version**: For deployment, build in release mode with optimizations

## Troubleshooting

### Common Build Issues

- **Missing Dependencies**: Make sure all required libraries are installed
- **Compilation Errors**: Check that your compiler supports C++20
- **Link Errors**: Verify that library paths are correctly set

### Runtime Issues

- **"Failed to load secure config"**: Run `scripts/run-native.sh --setup-credentials` first
- **"Authentication failure"**: Check your API credentials and master password
- **"Key derivation failed"**: Your config file may be corrupted; delete and recreate credentials
- **WebSocket connection issues**: Verify internet connection and exchange endpoints
- **Certificate validation errors**: Certificate pinning may be blocking connection; check logs
- **Rate limit exceeded**: Wait for cooldown period or check rate limiting configuration
- **Simulation Not Starting**: Check the log file for detailed error messages
- **Performance Problems**: Try running with `--verbose` for more detailed output
- **Empty order book in live mode**: Normal for ticker-only data; order book requires level2 authentication

## Live Trading Setup

### Getting Coinbase Pro API Credentials

1. **Create Coinbase Pro Account**: Sign up at [pro.coinbase.com](https://pro.coinbase.com)
2. **Generate API Key**: Go to Settings → API → Create API Key
3. **Set Permissions**: Enable "View" permissions (no trading permissions needed for market data)
4. **Save Credentials**: Note your API Key, Secret, and Passphrase

### Security Best Practices

- **Strong Master Password**: Use a unique, strong password for credential encryption (minimum 8 characters with mixed case, numbers, and symbols)
- **API Key Permissions**: Limit to "View" only for market data
- **IP Restrictions**: Set IP restrictions on your Coinbase API keys
- **Regular Rotation**: Rotate API credentials periodically
- **Secure Environment**: Run PinnacleMM on secure systems with up-to-date software
- **Monitor Logs**: Check audit logs regularly for security events
- **Clean Shutdown**: Use Ctrl+C for graceful shutdown to clear sensitive memory

## Next Steps

After getting PinnacleMM up and running, you might want to:

1. **Test Live Market Data**: Verify real-time price feeds with `--verbose`
2. **Explore the Code**: Understand how the different components work together
3. **Modify Strategy Parameters**: Experiment with different market making parameters
4. **Run Performance Benchmarks**: Test the ultra-low latency capabilities
5. **Implement Custom Strategies**: Create your own market making strategies

## Support

If you encounter any issues or have questions, please file an issue on the GitHub repository.
