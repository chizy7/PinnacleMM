# Interactive Brokers FIX Testing Guide

## Prerequisites

### 1. Interactive Brokers Account Setup
- Sign up for IB paper trading account (free)
- Download TWS (Trader Workstation) or IB Gateway
- Enable API access in account management

### 2. FIX API Configuration
- In TWS/Gateway: Configure → API → Settings
- Enable "Enable ActiveX and Socket Clients"
- Set Socket Port to 4101 (paper) or 4001 (live)
- **Important**: For FIX, you need a special FIX API agreement with IB

## Testing Steps

### Step 1: Start IB Gateway
```bash
# Download IB Gateway from:
# https://www.interactivebrokers.com/en/trading/ib-api.php

# Run IB Gateway (not TWS for API testing)
# Login with paper trading credentials
# Verify API is enabled and listening on port 4101
```

### Step 2: Configure Credentials
```bash
# In PinnacleMM, set your IB credentials:
# API Key = Your IB username/client ID
# API Secret = Your password (if required)
# Host = localhost
# Port = 4101 (paper) or 4001 (live)
```

### Step 3: Run Connection Test
```bash
cd build
./fix_basic_test

# Expected output should show:
# ✓ Interactive Brokers FIX support: Yes
# ✓ IB Config - Host: localhost:4101
```

### Step 4: Live Market Data Test
```cpp
// Once hffix API is fixed, you can test:
auto connector = factory.createConnector(
    FixConnectorFactory::Exchange::INTERACTIVE_BROKERS,
    credentials
);

connector->subscribeToMarketUpdates("AAPL", [](const MarketUpdate& update) {
    std::cout << "AAPL: " << update.bid << "/" << update.ask << std::endl;
});

connector->start(); // This will attempt actual FIX connection
```

### Step 5: Paper Trading Test
```cpp
// Create a small test order
pinnacle::Order testOrder(
    "TEST001",
    "AAPL",
    pinnacle::OrderSide::BUY,
    pinnacle::OrderType::LIMIT,
    145.00, // price
    1.0,    // quantity
    utils::TimeUtils::getCurrentNanos()
);

// Send to IB (paper trading only!)
bool success = connector->sendNewOrderSingle(testOrder);
```

## Important Notes

### FIX API Requirements
- Interactive Brokers requires a **separate FIX API agreement**
- Standard API uses proprietary protocol, not FIX
- FIX access typically requires larger account minimums
- Contact IB support to enable FIX API access

### Alternative Testing
If FIX API is not available, you can:
1. Use the existing WebSocket API (already working in PinnacleMM)
2. Test the FIX message construction offline
3. Use FIX protocol simulators

### Security
- **Never test with live trading initially**
- Always start with paper trading
- Verify all orders before execution
- Use small position sizes during testing

## Troubleshooting

### Connection Issues
```bash
# Check if IB Gateway is running:
netstat -an | grep 4101

# Verify API is enabled in TWS/Gateway
# Check firewall settings
# Ensure correct client ID in credentials
```

### FIX Protocol Specific
- FIX protocol is more complex than REST/WebSocket
- Requires proper sequence number management
- Heartbeat timing is critical
- Message format must be exact

### Error Handling
- Connection timeouts: Check network/firewall
- Authentication failures: Verify credentials
- Message format errors: Check FIX message construction
- Sequence number issues: Implement proper recovery
