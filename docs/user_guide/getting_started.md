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

The simplest way to start is with the simulation mode:

```bash
./pinnaclemm --mode simulation --symbol BTC-USD
```

### Command Line Options

PinnacleMM supports the following command line options:

- `--help`: Show help message.
- `--symbol <symbol>`: Trading symbol (default: BTC-USD).
- `--mode <mode>`: Trading mode (simulation/live) (default: simulation).
- `--config <file>`: Configuration file (default: config/default_config.json).
- `--logfile <file>`: Log file (default: pinnaclemm.log).
- `--verbose`: Enable verbose output.

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

- **Simulation Not Starting**: Check the log file for detailed error messages
- **Performance Problems**: Try running with `--verbose` for more detailed output
- **Segmentation Faults**: These often indicate memory issues, run with a debugger

## Next Steps

After getting PinnacleMM up and running, you might want to:

1. **Explore the Code**: Understand how the different components work together
2. **Modify Strategy Parameters**: Experiment with different market making parameters
3. **Run Simulations**: Test the system with different market conditions
4. **Implement Custom Strategies**: Create your own market making strategies

## Support

If you encounter any issues or have questions, please file an issue on the GitHub repository.