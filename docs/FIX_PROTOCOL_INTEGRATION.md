# FIX Protocol Integration for PinnacleMM

## Overview

PinnacleMM now supports FIX (Financial Information eXchange) protocol for connecting to professional exchanges that offer FIX connectivity. This provides ultra-low latency market data and order execution capabilities for institutional-grade trading.

## Architecture

### Core Components

1. **FixConnector** (Base Class)
   - Abstract base class for all FIX protocol connectors
   - Implements FIX session management, heartbeats, and message parsing
   - Provides market data and order execution interfaces

2. **InteractiveBrokersFixConnector**
   - Concrete implementation for Interactive Brokers FIX API
   - Supports FIX 4.2 protocol
   - Handles IB-specific message formats and symbol mapping

3. **FixConnectorFactory**
   - Factory pattern for creating FIX connectors
   - Manages connector instances and configuration
   - Supports multiple exchange types

### Library Dependencies

- **hffix**: High-performance, header-only FIX protocol library
  - Ultra-low latency parsing and message construction
  - Designed specifically for high-frequency trading applications
  - Zero-copy message processing where possible

## Supported Exchanges

### Currently Implemented
- ✅ **Interactive Brokers** - Full implementation with FIX 4.2 support

### Planned Implementation
- 🔄 **Coinbase Pro** - FIX 4.4 support for institutional clients
- 🔄 **Kraken** - FIX 4.4 support for institutional clients
- 🔄 **Binance** - FIX 4.4 support for institutional clients

## Usage Examples

### Basic Market Data Subscription

```cpp
#include "exchange/fix/FixConnectorFactory.h"

using namespace pinnacle::exchange::fix;

// Create credentials
auto credentials = std::make_shared<utils::ApiCredentials>();
credentials->apiKey = "YOUR_CLIENT_ID";
credentials->apiSecret = "YOUR_PASSWORD";

// Get factory instance
auto& factory = FixConnectorFactory::getInstance();

// Create IB FIX connector
auto connector = factory.createConnector(
    FixConnectorFactory::Exchange::INTERACTIVE_BROKERS,
    credentials
);

// Subscribe to market data
connector->subscribeToMarketUpdates("AAPL",
    [](const MarketUpdate& update) {
        std::cout << "Price: " << update.lastPrice
                  << " Bid: " << update.bid
                  << " Ask: " << update.ask << std::endl;
    }
);

// Start the connection
connector->start();
```

### Order Execution

```cpp
#include "core/orderbook/Order.h"

// Create an order
Order buyOrder(1, "AAPL", OrderSide::BUY, OrderType::LIMIT, 100.0, 150.0);

// Send order via FIX
bool success = connector->sendNewOrderSingle(buyOrder);

// Cancel order
connector->cancelOrder("ORDER_ID_123");
```

## Configuration

### Interactive Brokers Setup

1. **Install IB Gateway or TWS**
   - Download from Interactive Brokers website
   - Configure for FIX API access

2. **FIX Configuration**
   - Host: `localhost` (for local IB Gateway)
   - Port: `4101` (paper trading) or `4001` (live trading)
   - Protocol: FIX 4.2
   - Authentication: Client ID and password

3. **API Permissions**
   - Enable FIX API in IB account settings
   - Configure IP restrictions if needed
   - Set appropriate trading permissions

## Performance Characteristics

- **Message Processing**: Sub-microsecond FIX message parsing
- **Memory Usage**: Zero-copy message processing where possible
- **Threading**: Lock-free queues for message passing
- **Latency**: Direct TCP connections for minimal latency

## Testing

Run the FIX protocol integration test:

```bash
cd build
make fix_test
./fix_test
```

The test verifies:
- Factory pattern functionality
- Connector creation and configuration
- Market data subscription interface
- Order execution interface
- hffix library integration

## Monitoring and Debugging

### Session Status

```cpp
std::string status = connector->getSessionStatus();
bool loggedOn = connector->isLoggedOn();
bool running = connector->isRunning();
```

### FIX Message Logging

The base FixConnector class provides built-in message logging for debugging:
- All incoming and outgoing FIX messages are logged
- Sequence number tracking and validation
- Heartbeat monitoring

## Security Considerations

- **Credentials**: Use secure credential storage (existing SecureConfig system)
- **Network**: Consider VPN or dedicated lines for production
- **Authentication**: Support for username/password and certificate-based auth
- **Session Management**: Automatic logon/logout and sequence number handling

## Production Deployment

### Requirements

1. **Network Connectivity**
   - Stable, low-latency connection to exchange
   - Consider co-location for best performance
   - Redundant connections for failover

2. **System Resources**
   - Dedicated CPU cores for FIX processing
   - Sufficient memory for message queues
   - SSD storage for logging and persistence

3. **Monitoring**
   - Real-time session monitoring
   - Latency measurements
   - Error handling and alerting

### Best Practices

- **Testing**: Thorough testing in paper trading environment
- **Failover**: Implement connection redundancy
- **Logging**: Comprehensive audit trail for regulatory compliance
- **Performance**: Regular latency benchmarking

## Integration with Existing System

The FIX protocol implementation seamlessly integrates with existing PinnacleMM components:

- **Order Book**: FIX market data updates the same order book structures
- **Strategy Engine**: Strategies can use FIX connectors through the same interfaces
- **Risk Management**: All FIX orders go through existing risk controls
- **Persistence**: FIX session data is persisted using existing systems

## Future Enhancements

- **Multi-venue Trading**: Support for simultaneous FIX connections
- **Smart Order Routing**: Intelligent order routing across FIX venues
- **Drop Copy**: Support for FIX drop copy sessions
- **Market Data Conflation**: Advanced market data aggregation
