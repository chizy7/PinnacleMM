# Exchange Connector Implementation Guide

This guide provides detailed information about PinnacleMM's exchange connector system, including architecture, implementation details, and usage instructions.

## Overview

The exchange connector system allows PinnacleMM to connect to real cryptocurrency exchanges for market data and trading. It consists of modular components that handle:

1. **Market Data**: Real-time price feeds and order book updates
2. **Order Execution**: Placing, modifying, and canceling orders
3. **Authentication**: Securely connecting to exchange APIs
4. **Error Handling**: Robust handling of connectivity issues

## Architecture

The exchange connector system follows a layered design:

```mermaid
flowchart TD
    A[Strategy Layer] --> B[Exchange Connector Layer]
    
    subgraph ExchangeLayer["Exchange Connector Layer"]
        C[Market Data Feed] --- D[Order Executor]
    end
    
    B --> ExchangeLayer
    ExchangeLayer --> E[Network/Transport Layer]
    
    subgraph NetworkLayer["Network/Transport Layer"]
        F[WebSocket] --- G[REST API]
        G --- H[FIX Protocol]
    end
    
    E --> NetworkLayer
    
    style A fill:#f6ffed,stroke:#52c41a,stroke-width:2px
    style ExchangeLayer fill:#e6f7ff,stroke:#1890ff,stroke-width:2px
    style NetworkLayer fill:#fff2e8,stroke:#fa8c16,stroke-width:2px
```

### Key Components

1. **ExchangeConnector**: The main interface for interacting with exchanges
2. **WebSocketMarketDataFeed**: Real-time market data via WebSockets
3. **ExchangeConnectorFactory**: Creates appropriate connector types
4. **SecureConfig**: Handles API credentials securely

## Supported Exchanges

The connector system currently supports (or plans to support) these exchanges:

| Exchange   | Status      | Market Data | Order Execution |
|------------|-------------|-------------|-----------------|
| Coinbase   | Implemented | Yes         | Partial         |
| Kraken     | Planned     | Planned     | Planned         |
| Gemini     | Planned     | Planned     | Planned         |
| Binance    | Planned     | Planned     | Planned         |
| Bitstamp   | Planned     | Planned     | Planned         |

## Implementation

The following class diagram shows the relationships between the key components:

```mermaid
classDiagram
    class ExchangeConnector {
        -Exchange m_exchange
        -ApiCredentials* m_credentials
        -MarketDataFeed* m_marketDataFeed
        -OrderExecutor* m_orderExecutor
        +start() bool
        +stop() bool
        +isRunning() bool
        +getStatus() string
        +subscribeToMarketUpdates()
        +subscribeToOrderBookUpdates()
    }
    
    class ExchangeConnectorFactory {
        +createConnector(Exchange, ApiCredentials) ExchangeConnector
        -createCoinbaseConnector() ExchangeConnector
        -createKrakenConnector() ExchangeConnector
        -createGeminiConnector() ExchangeConnector
        -createBinanceConnector() ExchangeConnector
        -createBitstampConnector() ExchangeConnector
    }
    
    class MarketDataFeed {
        <<interface>>
        +start() bool
        +stop() bool
        +isRunning() bool
        +subscribeToMarketUpdates()
        +subscribeToOrderBookUpdates()
        +publishMarketUpdate()
        +publishOrderBookUpdate()
    }
    
    class WebSocketMarketDataFeed {
        -Exchange m_exchange
        -ApiCredentials* m_credentials
        -WebSocketClient m_client
        -string m_endpoint
        +start() bool
        +stop() bool
        +onMessage()
        +parseMessage()
    }
    
    class ApiCredentials {
        -map<string, string> m_apiKeys
        -map<string, string> m_apiSecrets
        -map<string, string> m_passphrases
        +loadFromFile(filename, password) bool
        +saveToFile(filename, password) bool
        +getApiKey(exchange) string
        +getApiSecret(exchange) string
        +getPassphrase(exchange) string
    }
    
    class OrderExecutor {
        <<interface>>
        +submitOrder() string
        +cancelOrder() bool
        +modifyOrder() bool
        +getOrderStatus() OrderStatus
    }
    
    ExchangeConnectorFactory ..> ExchangeConnector : creates
    ExchangeConnector --> MarketDataFeed : uses
    ExchangeConnector --> OrderExecutor : uses
    ExchangeConnector --> ApiCredentials : uses
    WebSocketMarketDataFeed ..|> MarketDataFeed : implements
    WebSocketMarketDataFeed --> ApiCredentials : uses
```

### ExchangeConnector

The `ExchangeConnector` class provides a unified interface for exchange operations:

```cpp
class ExchangeConnector {
public:
    // Constructor
    ExchangeConnector(
        Exchange exchange,
        std::shared_ptr<utils::ApiCredentials> credentials,
        std::shared_ptr<MarketDataFeed> marketDataFeed
    );
    
    // Core functionality
    bool start();
    bool stop();
    bool isRunning() const;
    std::string getStatus() const;
    
    // Market data subscription
    bool subscribeToMarketUpdates(
        const std::string& symbol,
        std::function<void(const MarketUpdate&)> callback
    );
    
    bool subscribeToOrderBookUpdates(
        const std::string& symbol,
        std::function<void(const OrderBookUpdate&)> callback
    );
    
    // Configuration
    void setRateLimits(int perSecond, int perMinute);
    void setRetryPolicy(int maxRetries, int retryDelayMs);
    
    // Accessors
    std::shared_ptr<MarketDataFeed> getMarketDataFeed() const;
    std::shared_ptr<OrderExecutor> getOrderExecutor() const;
    Exchange getExchangeType() const;
};
```

### WebSocketMarketDataFeed

The `WebSocketMarketDataFeed` class handles real-time market data:

```cpp
class WebSocketMarketDataFeed : public MarketDataFeed {
public:
    // Constructor
    WebSocketMarketDataFeed(
        Exchange exchange,
        std::shared_ptr<utils::ApiCredentials> credentials
    );
    
    // MarketDataFeed interface implementation
    bool start() override;
    bool stop() override;
    bool isRunning() const override;
    
    bool subscribeToMarketUpdates(
        const std::string& symbol,
        std::function<void(const MarketUpdate&)> callback
    ) override;
    
    bool subscribeToOrderBookUpdates(
        const std::string& symbol,
        std::function<void(const OrderBookUpdate&)> callback
    ) override;
    
    // Configuration
    void setConnectionParams(const std::string& endpoint, bool useSSL = true);
    void setAuthParams(const std::string& exchangeName);
    
    // Status
    std::string getStatusMessage() const;
};
```

### ExchangeConnectorFactory

The factory class creates the appropriate connector for each exchange:

```cpp
class ExchangeConnectorFactory {
public:
    static std::shared_ptr<ExchangeConnector> createConnector(
        Exchange exchange,
        std::shared_ptr<utils::ApiCredentials> credentials,
        const ConnectorConfig& config = ConnectorConfig()
    );
};
```

## WebSocket Implementation

The WebSocket implementation handles real-time data streams from exchanges:

1. **Core Connection**: Uses WebSocket++ for connection management
2. **Message Processing**: Processes JSON messages from exchanges
3. **Reconnection Logic**: Automatically reconnects on disconnection
4. **Rate Limiting**: Respects exchange rate limits

### WebSocket Stub for Testing

For development and testing, we provide a stub implementation that simulates exchange connectivity:

```cpp
// WebSocketStub.h provides a mock implementation
namespace websocketpp_stub {
    // Mock WebSocket client interface
    template<typename T>
    class client {
        // Stub implementations
    };
}

// WebSocketMarketDataFeedStub.cpp uses the stub for testing
bool WebSocketMarketDataFeed::start() {
    std::cout << "Mock WebSocket connection started for " << getExchangeName() << std::endl;
    m_isRunning.store(true, std::memory_order_release);
    return true;
}
```

## Usage Examples

### Connecting to an Exchange

#### Connection Sequence

The following sequence diagram illustrates the exchange connection process:

```mermaid
sequenceDiagram
    participant User
    participant Main as PinnacleMM Main
    participant Factory as ExchangeConnectorFactory
    participant Connector as ExchangeConnector
    participant APICredentials
    participant WebSocket as WebSocketMarketDataFeed
    participant Exchange as Remote Exchange

    User->>Main: Start with --mode live
    
    Main->>APICredentials: loadFromFile()
    APICredentials-->>Main: Prompt for master password
    User->>APICredentials: Enter password
    APICredentials->>APICredentials: Decrypt credentials
    APICredentials-->>Main: Credentials loaded
    
    Main->>Factory: createConnector(Exchange::COINBASE, credentials)
    Factory->>Connector: Create ExchangeConnector
    Connector->>WebSocket: Create WebSocketMarketDataFeed
    Factory-->>Main: Return connector
    
    Main->>Connector: start()
    Connector->>WebSocket: start()
    WebSocket->>APICredentials: getApiKey()
    WebSocket->>APICredentials: getApiSecret()
    APICredentials-->>WebSocket: Return credentials
    WebSocket->>WebSocket: Create authentication message
    WebSocket->>Exchange: Connect WebSocket
    WebSocket->>Exchange: Send authentication
    Exchange-->>WebSocket: Authentication confirmed
    WebSocket->>Exchange: Subscribe to market data
    Exchange-->>WebSocket: Subscription confirmed
    WebSocket-->>Connector: WebSocket started
    Connector-->>Main: Connector started
    
    Exchange->>WebSocket: Market update message
    WebSocket->>WebSocket: Parse message
    WebSocket->>Connector: Notify update
    Connector->>Main: Market update callback
    Main->>User: Display update
```

```cpp
#include "exchange/connector/ExchangeConnectorFactory.h"
#include "exchange/connector/SecureConfig.h"

int main() {
    // Load credentials
    auto credentials = std::make_shared<pinnacle::utils::ApiCredentials>();
    credentials->loadFromFile("credentials.dat", "masterPassword");
    
    // Create connector configuration
    pinnacle::exchange::ConnectorConfig config;
    config.rateLimitPerSecond = 10;
    config.maxRetries = 3;
    
    // Create exchange connector
    auto connector = pinnacle::exchange::ExchangeConnectorFactory::createConnector(
        pinnacle::exchange::Exchange::COINBASE,
        credentials,
        config
    );
    
    // Start the connector
    if (connector->start()) {
        std::cout << "Connected to exchange" << std::endl;
    } else {
        std::cerr << "Failed to connect" << std::endl;
        return 1;
    }
    
    // Subscribe to market updates
    connector->subscribeToMarketUpdates("BTC-USD", [](const auto& update) {
        std::cout << "Price: " << update.price << ", Volume: " << update.volume << std::endl;
    });
    
    // Main loop
    while (true) {
        // Do something
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Stop the connector
    connector->stop();
    
    return 0;
}
```

### Using the WebSocket Stub for Testing

To use the WebSocket stub implementation for testing:

1. Build with the `USE_WEBSOCKET_STUB` flag enabled:
   ```bash
   cmake .. -DUSE_WEBSOCKET_STUB=ON
   ```

2. The application will use the stub implementation automatically:
   ```cpp
   // This will use the stub implementation when USE_WEBSOCKET_STUB is defined
   auto marketDataFeed = std::make_shared<WebSocketMarketDataFeed>(
       WebSocketMarketDataFeed::Exchange::COINBASE, 
       credentials
   );
   ```

## Rate Limiting

The connector system implements exchange rate limiting to avoid being blocked:

```cpp
bool ExchangeConnector::checkRateLimit() {
    std::lock_guard<std::mutex> lock(m_rateLimitMutex);
    
    auto now = std::chrono::steady_clock::now();
    auto sinceLast = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - m_lastRequestTime).count();
    
    // Reset counters if a second has passed
    if (sinceLast >= 1000) {
        m_requestsThisSecond = 0;
        m_lastRequestTime = now;
    }
    
    // Check rate limits
    if (m_requestsThisSecond >= m_rateLimitPerSecond) {
        return false;
    }
    
    // Increment counters
    m_requestsThisSecond++;
    m_requestsThisMinute++;
    
    return true;
}
```

## Error Handling and Retry Logic

The connector implements robust error handling and automatic retries:

```cpp
// Example of retry logic (simplified)
bool sendRequest(const std::string& request) {
    for (int attempt = 0; attempt <= m_maxRetries; attempt++) {
        try {
            // Attempt the request
            bool success = doSendRequest(request);
            if (success) {
                return true;
            }
        } catch (const std::exception& e) {
            // Log the error
            spdlog::error("Request failed (attempt {}/{}): {}", 
                attempt + 1, m_maxRetries + 1, e.what());
        }
        
        // Wait before retrying
        if (attempt < m_maxRetries) {
            utils::TimeUtils::sleepForMillis(m_retryDelayMs);
        }
    }
    
    return false;
}
```

## Future Enhancements

Planned improvements to the exchange connector system:

1. **FIX Protocol Support**: Add support for FIX protocol connections
2. **Connection Pooling**: Manage multiple connections for high throughput
3. **Smart Order Routing**: Intelligently route orders across exchanges
4. **Cross-Exchange Arbitrage**: Leverage price differences between exchanges
5. **Performance Monitoring**: Detailed performance metrics and monitoring

## Troubleshooting

### Common Issues

1. **Connection Failures**
   - Check network connectivity
   - Verify API credentials
   - Ensure rate limits aren't exceeded

2. **Missing Market Data**
   - Verify subscription parameters
   - Check exchange status
   - Ensure the symbol format is correct for the exchange

3. **Authentication Issues**
   - Verify API key permissions
   - Check system clock synchronization
   - Ensure credentials are not expired