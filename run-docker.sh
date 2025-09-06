#!/bin/bash

# PinnacleMM Docker Runner Script
# Usage: ./run-docker.sh [simulation|live] [symbol] [exchange]

set -e

# Default values
MODE="simulation"
SYMBOL="BTC-USD"
EXCHANGE="coinbase"
VERBOSE=""

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
    echo "PinnacleMM Docker Runner"
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
    echo "Examples:"
    echo "  $0                                    # Run in simulation mode"
    echo "  $0 -m live -v                        # Run in live mode with verbose logging"
    echo "  $0 -m live -s ETH-USD -e coinbase    # Live trading ETH-USD on Coinbase"
    echo ""
    echo "Docker Commands:"
    echo "  $0 build                              # Build Docker image"
    echo "  $0 logs                               # View container logs"
    echo "  $0 stop                               # Stop running container"
    echo "  $0 clean                              # Remove containers and image"
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
        -h|--help)
            show_usage
            exit 0
            ;;
        build)
            print_info "Building Docker image..."
            docker build -t pinnaclemm .
            print_success "Docker image built successfully!"
            exit 0
            ;;
        logs)
            print_info "Showing container logs..."
            docker logs -f pinnaclemm 2>/dev/null || docker logs -f pinnaclemm-live 2>/dev/null || {
                print_error "No running PinnacleMM containers found"
                exit 1
            }
            exit 0
            ;;
        stop)
            print_info "Stopping PinnacleMM containers..."
            docker stop pinnaclemm pinnaclemm-live 2>/dev/null || true
            docker rm pinnaclemm pinnaclemm-live 2>/dev/null || true
            print_success "Containers stopped and removed"
            exit 0
            ;;
        clean)
            print_info "Cleaning up Docker containers and image..."
            docker stop pinnaclemm pinnaclemm-live 2>/dev/null || true
            docker rm pinnaclemm pinnaclemm-live 2>/dev/null || true
            docker rmi pinnaclemm 2>/dev/null || true
            print_success "Cleanup complete"
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

# Check if Docker is running
if ! docker info >/dev/null 2>&1; then
    print_error "Docker is not running. Please start Docker and try again."
    exit 1
fi

# Check if image exists, build if not
if ! docker image inspect pinnaclemm >/dev/null 2>&1; then
    print_warning "PinnacleMM Docker image not found. Building..."
    docker build -t pinnaclemm .
    print_success "Docker image built successfully!"
fi

# Stop any existing containers
print_info "Stopping any existing PinnacleMM containers..."
docker stop pinnaclemm pinnaclemm-live 2>/dev/null || true
docker rm pinnaclemm pinnaclemm-live 2>/dev/null || true

# Prepare container name and arguments
CONTAINER_NAME="pinnaclemm"
DOCKER_ARGS="--mode $MODE --symbol $SYMBOL"

if [[ "$MODE" == "live" ]]; then
    CONTAINER_NAME="pinnaclemm-live"
    DOCKER_ARGS="$DOCKER_ARGS --exchange $EXCHANGE"
    
    # Check if config directory exists for live mode
    if [[ ! -d "config" ]]; then
        print_warning "Config directory not found. Creating..."
        mkdir -p config
    fi
    
    # Check if credentials are configured
    if [[ ! -f "config/secure_config.json" ]]; then
        print_warning "No API credentials found. You'll need to set them up first."
        print_info "Run: ./run-native.sh --setup-credentials"
        print_info "Or run the native binary to configure credentials first."
    fi
fi

# Add verbose flag if specified
if [[ -n "$VERBOSE" ]]; then
    DOCKER_ARGS="$DOCKER_ARGS $VERBOSE"
fi

print_info "Starting PinnacleMM in Docker..."
print_info "Mode: $MODE"
print_info "Symbol: $SYMBOL"
if [[ "$MODE" == "live" ]]; then
    print_info "Exchange: $EXCHANGE"
fi

# Run the container
if [[ "$MODE" == "live" ]]; then
    # Live mode - interactive with volume mount for credentials
    print_info "Running in live mode (interactive for password input)..."
    docker run -it --name "$CONTAINER_NAME" \
        -v "$(pwd)/config:/app/config" \
        pinnaclemm $DOCKER_ARGS
else
    # Simulation mode - detached
    print_info "Running in simulation mode (detached)..."
    docker run -d --name "$CONTAINER_NAME" \
        pinnaclemm $DOCKER_ARGS
    
    print_success "Container started successfully!"
    print_info "Container ID: $(docker ps -q -f name=$CONTAINER_NAME)"
    print_info "View logs with: $0 logs"
    print_info "Stop with: $0 stop"
fi
