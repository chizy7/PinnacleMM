# PinnacleMM System Architecture

## Overview

PinnacleMM is designed with a modular, layered architecture to achieve ultra-low latency performance while maintaining flexibility and extensibility. The system is built around a high-performance core that handles critical functions with minimal overhead, surrounded by layers of increasingly higher-level functionality.

## System Architecture Diagram

![PinnacleMM Architecture Diagram](../architecture/images/phase1.svg)


## Architectural Principles

1. **Performance-First Design**: Critical paths are optimized for minimum latency
2. **Modularity**: Clear separation of concerns between components
3. **Testability**: Components are designed to be testable in isolation
4. **Thread Safety**: Concurrent operations are supported with minimal locking
5. **Minimal Dependencies**: External dependencies are carefully managed

## System Layers

The architecture is divided into three main layers, each with specific responsibilities:

### 1. Core Engine Layer

The foundation of PinnacleMM, responsible for the most performance-critical operations.

**Components:**
- **Order Book**: Maintains the full limit order book with nanosecond precision
- **Time Utilities**: High-precision timing functions and measurements
- **Lock-Free Queues**: Thread-safe communication without locks
- **Order Management**: Order creation, modification, and cancellation

**Key Characteristics:**
- Written in optimized C++ with cache-friendly data structures
- Uses lock-free algorithms where possible
- Minimal external dependencies
- Designed for microsecond-level latency

### 2. Strategy Layer

Implements market making strategies and decision logic.

**Components:**
- **BasicMarketMaker**: Implements the core market making algorithm
- **StrategyConfig**: Configuration parameters for strategies
- **Position Management**: Tracking and managing trading positions
- **P&L Calculation**: Real-time profit and loss monitoring

**Key Characteristics:**
- Event-driven architecture
- Configurable parameters for different market conditions
- Thread-safe interaction with the core layer

### 3. Exchange Layer

Handles interaction with exchanges and market data.

**Components:**
- **Exchange Simulator**: Simulates an exchange for testing and development
- **Market Data Feed**: Processes and distributes market data
- **Order Execution**: Handles order submission and confirmation

**Key Characteristics:**
- Abstracts exchange-specific differences
- Handles network I/O efficiently
- Manages market data flow

## Data Flow

1. **Market Data Flow**:
   - Market data arrives from exchanges or the simulator
   - Data is normalized and processed by the Market Data Feed
   - Updates are pushed to the Order Book
   - Strategy components are notified of order book changes

2. **Strategy Decision Flow**:
   - Market Maker evaluates the current market state
   - Decisions are made to place, modify, or cancel orders
   - Commands are sent to the Order Book
   - Orders are executed or simulated

3. **Order Execution Flow**:
   - Orders are validated by the Core Engine
   - Valid orders are added to the Order Book
   - Orders may match against existing orders
   - Execution results are reported back to the Strategy Layer

## Thread Model

PinnacleMM uses a multithreaded design for optimal performance:

1. **Market Data Thread**: Processes incoming market data
2. **Strategy Thread**: Runs the market making algorithm
3. **Execution Thread**: Handles order execution
4. **Monitoring Thread**: Tracks system performance

Threads communicate using lock-free queues to minimize contention.

## Performance Considerations

- **Memory Management**: Custom memory allocation for critical components
- **Cache Optimization**: Data structures designed to minimize cache misses
- **Lock-Free Algorithms**: Used extensively to avoid mutex contention
- **Batched Processing**: Operations are batched where possible to reduce overhead

## Current Implementation Status (Phase 1)

In Phase 1, I have implemented:

- Complete Order Book implementation with price level management
- Basic Market Making strategy with dynamic spread adjustment
- Exchange Simulator for testing
- Thread-safe data structures and utilities
- Comprehensive unit tests

Future phases will build on this foundation to add exchange connectivity, advanced strategies, and production optimization.