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
- **WebSocket Market Data Feed**: Real-time market data via WebSocket connections
- **Exchange Connectors**: Live connectivity to cryptocurrency exchanges (Coinbase Pro production-ready)
- **Market Data Feed**: Processes and distributes market data
- **Order Execution**: Handles order submission and confirmation
- **Secure Config**: Enhanced encrypted API credential management with unique salt generation
- **Security Infrastructure**: Comprehensive security utilities for enterprise-grade protection

**Key Characteristics:**
- Abstracts exchange-specific differences
- Handles network I/O efficiently with Boost.Beast WebSocket + SSL/TLS
- Manages live market data flow from real exchanges
- Enterprise-grade security with multiple protection layers
- Secure credential storage with AES-256-CBC encryption + unique salts + 100,000 PBKDF2 iterations
- Comprehensive input validation preventing injection attacks
- Certificate pinning for WebSocket SSL connections
- Audit logging and rate limiting for security monitoring

### 4. Security Layer (Added in Phase 2)

The security layer provides enterprise-grade protection for all system operations.

**Components:**
- **SecureInput**: Cross-platform secure password input with terminal masking
- **InputValidator**: Comprehensive validation framework preventing injection attacks
- **CertificatePinner**: SSL certificate pinning for WebSocket connections
- **AuditLogger**: Security event logging with structured JSON format
- **RateLimiter**: Multi-strategy rate limiting (sliding window, token bucket)
- **SecureConfig**: Enhanced credential encryption with unique salt generation

**Key Characteristics:**
- Defense-in-depth security architecture
- Zero-trust input validation for all user data
- Cryptographic protection using industry-standard algorithms
- Real-time security monitoring and alerting
- Cross-platform compatibility (macOS, Linux, Windows)
- Minimal performance impact on trading operations

### 5. Persistence Layer (Added in Phase 2)

The persistence layer provides data durability and crash recovery capabilities.

**Components:**
- **Journal**: Records all order book operations in a transaction log
- **Snapshot Manager**: Creates periodic snapshots of the system state
- **Recovery Manager**: Restores system state after crashes or restarts

**Key Characteristics:**
- Uses memory-mapped files for ultra-low latency I/O operations
- Minimizes performance impact on critical trading paths
- Enables instant restart without lengthy replay operations
- Maintains historical order and execution data

## Data Flow

1. **Live Market Data Flow**:
   - Real-time market data arrives from Coinbase Pro WebSocket feed
   - WebSocketMarketDataFeed processes JSON ticker messages
   - Price and volume data is normalized and distributed
   - Order Book receives live market updates
   - Strategy components are notified of real-time market changes

2. **Strategy Decision Flow**:
   - Market Maker evaluates live market conditions
   - Decisions are made based on real-time price spreads and volume
   - Commands are sent to the Order Book
   - Orders are executed or prepared for exchange submission

3. **Credential Security Flow**:
   - SecureInput prompts user authentication with terminal masking
   - InputValidator validates all user inputs before processing
   - PBKDF2 derives encryption key from password + unique random salt (100,000 iterations)
   - AES-256-CBC decrypts stored API credentials using salt-specific keys
   - CertificatePinner validates SSL connections before credential transmission
   - Credentials are provided to exchange connectors securely
   - AuditLogger records all security events for monitoring
   - Memory is cleared using volatile operations to prevent exposure

4. **Order Execution Flow**:
   - Orders are validated by the Core Engine
   - Valid orders are added to the Order Book
   - Orders may match against existing orders or be sent to exchange
   - Execution results are reported back to the Strategy Layer

## Thread Model

PinnacleMM uses a multithreaded design for optimal performance:

1. **Market Data Thread**: Processes incoming market data with rate limiting
2. **Strategy Thread**: Runs the market making algorithm
3. **Execution Thread**: Handles order execution with input validation
4. **Monitoring Thread**: Tracks system performance and security events
5. **Security Thread**: Manages audit logging and certificate validation

Threads communicate using lock-free queues to minimize contention.

## Performance Considerations

- **Memory Management**: Custom memory allocation for critical components
- **Cache Optimization**: Data structures designed to minimize cache misses
- **Lock-Free Algorithms**: Used extensively to avoid mutex contention
- **Batched Processing**: Operations are batched where possible to reduce overhead

## Current Implementation Status

### Completed

- Complete Order Book implementation with price level management
- Basic Market Making strategy with dynamic spread adjustment
- Exchange Simulator for testing
- Thread-safe data structures and utilities
- Comprehensive unit tests
- Lock-free data structures for all critical paths
- Memory-mapped file system for data persistence
- **Enhanced Security Infrastructure**: Enterprise-grade security system
  - AES-256-CBC encryption with unique salt generation (replacing fixed salt vulnerability)
  - PBKDF2 key derivation increased from 10,000 to 100,000 iterations
  - Secure password input with cross-platform terminal masking
  - Comprehensive input validation framework preventing injection attacks
  - Certificate pinning for WebSocket SSL connections
  - Audit logging system for security event monitoring
  - Rate limiting with sliding window and token bucket algorithms
  - Secure memory clearing to prevent credential leakage
- WebSocket integration for real-time market data using Boost.Beast
- **Live Exchange Connectivity**: Production-ready Coinbase Pro connector
  - Real-time ticker data processing (BTC-USD ~$109,200+)
  - Secure WebSocket connection with SSL/TLS and certificate pinning
  - Interactive credential setup utility with secure input
  - Multiple market updates per second with automatic reconnection

**System Status**: Production-ready for live cryptocurrency market making operations
