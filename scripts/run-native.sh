#!/bin/bash

# PinnacleMM Native Runner Script
# Usage: ./run-native.sh [OPTIONS]

set -e

# Default values
MODE="simulation"
SYMBOL="BTC-USD"
EXCHANGE="coinbase"
VERBOSE=""
BUILD_DIR="build"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored output
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Function to show usage
show_usage() {
    echo "PinnacleMM Native Runner"
    echo ""
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  -m, --mode MODE        Trading mode: simulation (default) or live"
    echo "  -s, --symbol SYMBOL    Trading symbol (default: BTC-USD)"
    echo "  -e, --exchange EXCHANGE Exchange name (default: coinbase)"
    echo "  -v, --verbose          Enable verbose logging"
    echo "  -h, --help             Show this help message"
    echo ""
    echo "Build Commands:"
    echo "  $0 build               # Build the project"
    echo "  $0 clean               # Clean build directory"
    echo "  $0 rebuild             # Clean and rebuild"
    echo "  $0 test                # Run tests"
    echo "  $0 benchmark           # Run benchmarks"
    echo ""
    echo "Setup Commands:"
    echo "  $0 --setup-credentials # Configure API credentials"
    echo ""
    echo "Examples:"
    echo "  $0                                    # Run in simulation mode"
    echo "  $0 -m live -v                        # Run in live mode with verbose logging"
    echo "  $0 -m live -s ETH-USD -e coinbase    # Live trading ETH-USD on Coinbase"
    echo "  $0 build                              # Build the project"
    echo "  $0 --setup-credentials                # Setup API credentials"
}

# Function to check dependencies
check_dependencies() {
    local missing_deps=()

    # Check for required tools
    if ! command -v cmake >/dev/null 2>&1; then
        missing_deps+=("cmake")
    fi

    if ! command -v make >/dev/null 2>&1; then
        missing_deps+=("make")
    fi

    if ! command -v g++ >/dev/null 2>&1 && ! command -v clang++ >/dev/null 2>&1; then
        missing_deps+=("g++ or clang++")
    fi

    if [[ ${#missing_deps[@]} -gt 0 ]]; then
        print_error "Missing required dependencies: ${missing_deps[*]}"
        print_info "Please install the missing dependencies and try again."

        if [[ "$OSTYPE" == "darwin"* ]]; then
            print_info "On macOS, install with: brew install cmake"
        elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
            print_info "On Ubuntu/Debian: sudo apt-get install cmake build-essential"
            print_info "On CentOS/RHEL: sudo yum install cmake gcc-c++ make"
        fi

        exit 1
    fi
}

# Function to build the project
build_project() {
    print_info "Building PinnacleMM..."

    # Create build directory if it doesn't exist
    if [[ ! -d "$BUILD_DIR" ]]; then
        mkdir -p "$BUILD_DIR"
    fi

    cd "$BUILD_DIR"

    # Configure with CMake
    cmake .. || {
        print_error "CMake configuration failed"
        exit 1
    }

    # Build with appropriate number of cores
    local cores
    if [[ "$OSTYPE" == "darwin"* ]]; then
        cores=$(sysctl -n hw.ncpu)
    else
        cores=$(nproc)
    fi

    make -j"$cores" || {
        print_error "Build failed"
        exit 1
    }

    cd ..
    print_success "Build completed successfully!"
}

# Function to run tests
run_tests() {
    print_info "Running tests..."

    if [[ ! -f "$BUILD_DIR/pinnaclemm" ]]; then
        print_warning "Project not built. Building first..."
        build_project
    fi

    cd "$BUILD_DIR"

    # Run all tests
    local test_files=(
        "orderbook_tests"
        "lockfree_orderbook_tests"
        "execution_tests"
        "strategy_tests"
        "fix_basic_test"
    )

    local failed_tests=()

    for test in "${test_files[@]}"; do
        if [[ -f "$test" ]]; then
            print_info "Running $test..."
            if ./"$test"; then
                print_success "$test passed"
            else
                print_error "$test failed"
                failed_tests+=("$test")
            fi
        else
            print_warning "$test not found (may not be built)"
        fi
    done

    cd ..

    if [[ ${#failed_tests[@]} -eq 0 ]]; then
        print_success "All tests passed!"
    else
        print_error "Failed tests: ${failed_tests[*]}"
        exit 1
    fi
}

# Function to run benchmarks
run_benchmarks() {
    print_info "Running benchmarks..."

    if [[ ! -f "$BUILD_DIR/pinnaclemm" ]]; then
        print_warning "Project not built. Building first..."
        build_project
    fi

    cd "$BUILD_DIR"

    local benchmark_files=(
        "latency_benchmark"
        "throughput_benchmark"
        "orderbook_benchmark"
    )

    for benchmark in "${benchmark_files[@]}"; do
        if [[ -f "$benchmark" ]]; then
            print_info "Running $benchmark..."
            ./"$benchmark"
        else
            print_warning "$benchmark not found"
        fi
    done

    cd ..
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -m|--mode)
            MODE="$2"
            shift 2
            ;;
        -s|--symbol)
            SYMBOL="$2"
            shift 2
            ;;
        -e|--exchange)
            EXCHANGE="$2"
            shift 2
            ;;
        -v|--verbose)
            VERBOSE="--verbose"
            shift
            ;;
        --setup-credentials)
            check_dependencies
            if [[ ! -f "$BUILD_DIR/pinnaclemm" ]]; then
                build_project
            fi
            print_info "Setting up API credentials..."
            cd "$BUILD_DIR"
            ./pinnaclemm --setup-credentials
            cd ..
            exit 0
            ;;
        -h|--help)
            show_usage
            exit 0
            ;;
        build)
            check_dependencies
            build_project
            exit 0
            ;;
        clean)
            print_info "Cleaning build directory..."
            rm -rf "$BUILD_DIR"
            print_success "Build directory cleaned"
            exit 0
            ;;
        rebuild)
            check_dependencies
            print_info "Cleaning and rebuilding..."
            rm -rf "$BUILD_DIR"
            build_project
            exit 0
            ;;
        test)
            check_dependencies
            run_tests
            exit 0
            ;;
        benchmark)
            check_dependencies
            run_benchmarks
            exit 0
            ;;
        *)
            print_error "Unknown option: $1"
            show_usage
            exit 1
            ;;
    esac
done

# Validate mode
if [[ "$MODE" != "simulation" && "$MODE" != "live" ]]; then
    print_error "Invalid mode: $MODE. Must be 'simulation' or 'live'"
    exit 1
fi

# Check dependencies
check_dependencies

# Build if necessary
if [[ ! -f "$BUILD_DIR/pinnaclemm" ]]; then
    print_warning "Executable not found. Building project..."
    build_project
fi

# Prepare arguments as array
ARGS=("--mode" "$MODE" "--symbol" "$SYMBOL")

if [[ "$MODE" == "live" ]]; then
    ARGS+=("--exchange" "$EXCHANGE")

    # Check if credentials are configured
    if [[ ! -f "config/secure_config.json" ]]; then
        print_warning "No API credentials found."
        print_info "Run: $0 --setup-credentials"
        read -p "Would you like to set up credentials now? (y/n): " -n 1 -r
        echo
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            cd "$BUILD_DIR"
            ./pinnaclemm --setup-credentials
            cd ..
        else
            print_error "API credentials required for live mode"
            exit 1
        fi
    fi
fi

# Add verbose flag if specified
if [[ -n "$VERBOSE" ]]; then
    ARGS+=("$VERBOSE")
fi

print_info "Starting PinnacleMM..."
print_info "Mode: $MODE"
print_info "Symbol: $SYMBOL"
if [[ "$MODE" == "live" ]]; then
    print_info "Exchange: $EXCHANGE"
fi

# Run the application
cd "$BUILD_DIR"
./pinnaclemm "${ARGS[@]}"
