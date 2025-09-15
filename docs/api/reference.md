# PinnacleMM API Reference

This document provides a reference for the key classes and interfaces in PinnacleMM.

## Core Components

### OrderBook

The central component for maintaining and managing the limit order book.

```cpp
class OrderBook {
public:
    // Constructor
    explicit OrderBook(const std::string& symbol);

    // Order management
    bool addOrder(std::shared_ptr<Order> order);
    bool cancelOrder(const std::string& orderId);
    bool executeOrder(const std::string& orderId, double quantity);
    double executeMarketOrder(OrderSide side, double quantity,
                             std::vector<std::pair<std::string, double>>& fills);

    // Order book queries
    std::shared_ptr<Order> getOrder(const std::string& orderId) const;
    double getBestBidPrice() const;
    double getBestAskPrice() const;
    double getMidPrice() const;
    double getSpread() const;
    size_t getOrderCount() const;

    // Level queries
    size_t getBidLevels() const;
    size_t getAskLevels() const;
    double getVolumeAtPrice(double price) const;
    std::vector<PriceLevel> getBidLevels(size_t depth) const;
    std::vector<PriceLevel> getAskLevels(size_t depth) const;

    // Market depth calculations
    double calculateMarketImpact(OrderSide side, double quantity) const;
    double calculateVolumeWeightedAveragePrice(OrderSide side, double quantity) const;
    double calculateOrderBookImbalance(size_t depth) const;

    // Take a snapshot of the current order book state
    std::shared_ptr<OrderBookSnapshot> getSnapshot() const;

    // Clear the order book
    void clear();

    // Get the symbol for this order book
    const std::string& getSymbol() const;

    // Callback registration for order book events
    using OrderBookUpdateCallback = std::function<void(const OrderBook&)>;
    void registerUpdateCallback(OrderBookUpdateCallback callback);
};
```

### Order

Represents a single order in the system.

```cpp
class Order {
public:
    // Constructor
    Order(
        const std::string& orderId,
        const std::string& symbol,
        OrderSide side,
        OrderType type,
        double price,
        double quantity,
        uint64_t timestamp
    );

    // Getters
    const std::string& getOrderId() const;
    const std::string& getSymbol() const;
    OrderSide getSide() const;
    OrderType getType() const;
    OrderStatus getStatus() const;
    double getPrice() const;
    double getQuantity() const;
    double getFilledQuantity() const;
    double getRemainingQuantity() const;
    uint64_t getTimestamp() const;
    uint64_t getLastUpdateTime() const;

    // Modifiers
    void updateStatus(OrderStatus newStatus);
    bool fill(double fillQuantity, uint64_t timestamp);
    bool cancel(uint64_t timestamp);
    bool reject(uint64_t timestamp);
    bool expire(uint64_t timestamp);

    // Utility methods
    bool isBuy() const;
    bool isSell() const;
    bool isActive() const;
    bool isCompleted() const;
};
```

### Order Types and Enums

```cpp
enum class OrderSide : uint8_t {
    BUY = 0,
    SELL = 1
};

enum class OrderType : uint8_t {
    LIMIT = 0,
    MARKET = 1,
    STOP = 2,
    STOP_LIMIT = 3,
    IOC = 4, // Immediate or Cancel
    FOK = 5  // Fill or Kill
};

enum class OrderStatus : uint8_t {
    NEW = 0,
    PARTIALLY_FILLED = 1,
    FILLED = 2,
    CANCELED = 3,
    REJECTED = 4,
    EXPIRED = 5
};
```

## Strategy Components

### BasicMarketMaker

The basic market making strategy implementation.

```cpp
class BasicMarketMaker {
public:
    // Constructor
    BasicMarketMaker(const std::string& symbol, const StrategyConfig& config);

    // Strategy lifecycle
    bool initialize(std::shared_ptr<OrderBook> orderBook);
    bool start();
    bool stop();
    bool isRunning() const;

    // Event handlers
    void onOrderBookUpdate(const OrderBook& orderBook);
    void onTrade(const std::string& symbol, double price, double quantity,
                OrderSide side, uint64_t timestamp);
    void onOrderUpdate(const std::string& orderId, OrderStatus status,
                      double filledQuantity, uint64_t timestamp);

    // Statistics and state
    std::string getStatistics() const;
    double getPosition() const;
    double getPnL() const;

    // Configuration
    bool updateConfig(const StrategyConfig& config);
};
```

### StrategyConfig

Configuration parameters for market making strategies.

```cpp
struct StrategyConfig {
    // General strategy parameters
    std::string strategyName = "BasicMarketMaker";
    std::string symbol = "BTC-USD";

    // Quote parameters
    double baseSpreadBps = 10.0;        // Base spread in basis points
    double minSpreadBps = 5.0;          // Minimum spread in basis points
    double maxSpreadBps = 50.0;         // Maximum spread in basis points
    double orderQuantity = 0.01;        // Base order quantity
    double minOrderQuantity = 0.001;    // Minimum order quantity
    double maxOrderQuantity = 1.0;      // Maximum order quantity

    // Market making parameters
    double targetPosition = 0.0;        // Target position (0 = neutral)
    double maxPosition = 10.0;          // Maximum absolute position
    double inventorySkewFactor = 0.5;   // How much to skew quotes based on inventory (0-1)

    // Risk parameters
    double maxDrawdownPct = 5.0;        // Maximum drawdown percentage before stopping
    double stopLossPct = 3.0;           // Stop loss percentage for individual position
    double takeProfitPct = 5.0;         // Take profit percentage for individual position

    // Timing parameters
    uint64_t quoteUpdateIntervalMs = 100;  // Quote update interval in milliseconds

    // Validate the configuration parameters
    // Returns false if validation fails and populates errorReason with descriptive message
    bool validate(std::string& errorReason) const;
};
```

## Exchange Components

### ExchangeSimulator

Simulates an exchange for testing strategies.

```cpp
class ExchangeSimulator {
public:
    // Constructor
    explicit ExchangeSimulator(std::shared_ptr<OrderBook> orderBook);

    // Simulator lifecycle
    bool start();
    bool stop();
    bool isRunning() const;

    // Configuration
    void setMarketDataFeed(std::shared_ptr<MarketDataFeed> marketDataFeed);
    void setVolatility(double volatility);
    void setDrift(double drift);
    void setTickSize(double tickSize);
    void addMarketParticipant(const std::string& type, double frequency, double volumeRatio);
};
```

### MarketDataFeed

Interface for market data feeds.

```cpp
class MarketDataFeed {
public:
    virtual ~MarketDataFeed() = default;

    virtual bool start() = 0;
    virtual bool stop() = 0;
    virtual bool isRunning() const = 0;

    virtual bool subscribeToMarketUpdates(
        const std::string& symbol,
        std::function<void(const MarketUpdate&)> callback) = 0;

    virtual bool subscribeToOrderBookUpdates(
        const std::string& symbol,
        std::function<void(const OrderBookUpdate&)> callback) = 0;

    virtual bool unsubscribeFromMarketUpdates(const std::string& symbol) = 0;
    virtual bool unsubscribeFromOrderBookUpdates(const std::string& symbol) = 0;
};
```

## Utility Classes

### TimeUtils

High-precision timing utilities.

```cpp
class TimeUtils {
public:
    static uint64_t getCurrentNanos();
    static uint64_t getCurrentMicros();
    static uint64_t getCurrentMillis();
    static uint64_t getCurrentSeconds();

    static std::string nanosToTimestamp(uint64_t nanos);
    static void sleepForNanos(uint64_t nanos);
    static void sleepForMicros(uint64_t micros);
    static void sleepForMillis(uint64_t millis);

    template<typename Func>
    static uint64_t measureExecutionTimeNanos(Func&& func);

    template<typename Func>
    static uint64_t measureExecutionTimeMicros(Func&& func);

    static uint64_t getDiffNanos(uint64_t start_nanos, uint64_t end_nanos);
    static std::string getCurrentISOTimestamp();
    static bool isNanosecondPrecisionAvailable();
};
```

### LockFreeQueue

Thread-safe lock-free queue implementation.

```cpp
template<typename T, size_t Capacity>
class LockFreeQueue {
public:
    LockFreeQueue() = default;

    bool tryEnqueue(const T& item);
    bool tryEnqueue(T&& item);
    std::optional<T> tryDequeue();

    bool isEmpty() const;
    size_t size() const;
    constexpr size_t capacity() const;
};

template<typename T, size_t Capacity>
class LockFreeMPMCQueue {
public:
    LockFreeMPMCQueue();

    bool tryEnqueue(const T& data);
    bool tryEnqueue(T&& data);
    bool tryDequeue(T& result);

    bool isEmpty() const;
    size_t approximateSize() const;
    constexpr size_t capacity() const;
};
```

## Data Structures

### PriceLevel

Represents a price level in the order book.

```cpp
struct PriceLevel {
    double price;
    double totalQuantity;
    std::vector<std::shared_ptr<Order>> orders;

    explicit PriceLevel(double price);
    void addOrder(std::shared_ptr<Order> order);
    bool removeOrder(const std::string& orderId);
    void updateTotalQuantity();
};
```

### OrderBookSnapshot

Immutable snapshot of the order book at a point.

```cpp
class OrderBookSnapshot {
public:
    OrderBookSnapshot(
        const std::string& symbol,
        uint64_t timestamp,
        std::vector<PriceLevel> bids,
        std::vector<PriceLevel> asks
    );

    const std::string& getSymbol() const;
    uint64_t getTimestamp() const;
    const std::vector<PriceLevel>& getBids() const;
    const std::vector<PriceLevel>& getAsks() const;
};
```

### MarketUpdate

Structure containing market update information.

```cpp
struct MarketUpdate {
    std::string symbol;
    double price;
    double volume;
    uint64_t timestamp;
    bool isBuy;
};
```

### OrderBookUpdate

Structure containing order book update information.

```cpp
struct OrderBookUpdate {
    std::string symbol;
    std::vector<std::pair<double, double>> bids; // (price, quantity)
    std::vector<std::pair<double, double>> asks; // (price, quantity)
    uint64_t timestamp;
};
```

## Command-Line Interface

The main program entry point accepts the following command-line arguments:

```
Options:
  --help                      Show help message
  --setup-credentials         Interactive API credential setup
  --symbol arg (=BTC-USD)     Trading symbol
  --mode arg (=simulation)    Trading mode (simulation/live)
  --exchange arg              Exchange name (coinbase/kraken/gemini/binance/bitstamp)
  --config arg (=config/default_config.json)
                              Configuration file
  --logfile arg (=pinnaclemm.log)
                              Log file
  --verbose                   Verbose output with real-time market data
  --lock-free                 Use lock-free data structures (default: enabled)
```

## Live Trading Integration

### Setting Up API Credentials

PinnacleMM includes secure credential management for live exchange connectivity:

```cpp
// Setup credentials interactively
#include "exchange/connector/SecureConfig.h"

bool setupCredentials() {
    auto credentials = std::make_shared<pinnacle::utils::ApiCredentials>();

    // Interactive setup with encryption
    std::string masterPassword;
    std::cout << "Create master password: ";
    std::getline(std::cin, masterPassword);

    // Add exchange credentials
    std::string apiKey, apiSecret, passphrase;
    std::cout << "API Key: ";
    std::getline(std::cin, apiKey);
    std::cout << "API Secret: ";
    std::getline(std::cin, apiSecret);
    std::cout << "API Passphrase: ";
    std::getline(std::cin, passphrase);

    credentials->setCredentials("coinbase", apiKey, apiSecret, passphrase);
    return credentials->saveToFile("secure_config.json", masterPassword);
}
```

### Live Market Data Connection

```cpp
#include "exchange/connector/WebSocketMarketDataFeed.h"

// Connect to live Coinbase market data
auto credentials = std::make_shared<pinnacle::utils::ApiCredentials>();
if (!credentials->loadFromFile("secure_config.json", masterPassword)) {
    std::cerr << "Failed to load credentials" << std::endl;
    return false;
}

auto marketDataFeed = std::make_shared<pinnacle::exchange::WebSocketMarketDataFeed>(
    pinnacle::exchange::Exchange::COINBASE,
    credentials
);

// Subscribe to real-time ticker updates
marketDataFeed->subscribeToMarketUpdates("BTC-USD", [](const auto& update) {
    std::cout << "Live BTC Price: $" << update.price
              << ", Volume: " << update.volume << std::endl;
});

if (marketDataFeed->start()) {
    std::cout << "Connected to live Coinbase market data" << std::endl;
    // Receives real-time updates (e.g., BTC ~$109,200+)
}
```

## Example Usage

Here are some examples of how to use the PinnacleMM API:

### Creating and using an OrderBook

```cpp
// Create an order book for BTC-USD
auto orderBook = std::make_shared<pinnacle::OrderBook>("BTC-USD");

// Register for order book updates
orderBook->registerUpdateCallback([](const OrderBook& book) {
    std::cout << "Order book updated: " << book.getSymbol() << std::endl;
    std::cout << "Best bid: " << book.getBestBidPrice() << std::endl;
    std::cout << "Best ask: " << book.getBestAskPrice() << std::endl;
});

// Create and add a buy order
auto buyOrder = std::make_shared<Order>(
    "order-1",
    "BTC-USD",
    OrderSide::BUY,
    OrderType::LIMIT,
    10000.0,  // price
    1.0,      // quantity
    TimeUtils::getCurrentNanos()
);
orderBook->addOrder(buyOrder);

// Create and add a sell order
auto sellOrder = std::make_shared<Order>(
    "order-2",
    "BTC-USD",
    OrderSide::SELL,
    OrderType::LIMIT,
    10100.0,  // price
    1.0,      // quantity
    TimeUtils::getCurrentNanos()
);
orderBook->addOrder(sellOrder);

// Get the current spread
double spread = orderBook->getSpread();
std::cout << "Spread: " << spread << std::endl;

// Execute a market order
std::vector<std::pair<std::string, double>> fills;
double executed = orderBook->executeMarketOrder(OrderSide::BUY, 0.5, fills);
std::cout << "Executed: " << executed << " BTC" << std::endl;
```

### Setting up a market making strategy

```cpp
// Create strategy configuration
pinnacle::strategy::StrategyConfig config;
config.symbol = "BTC-USD";
config.baseSpreadBps = 10.0;
config.orderQuantity = 0.01;
config.maxPosition = 1.0;

// Create strategy
auto strategy = std::make_shared<pinnacle::strategy::BasicMarketMaker>("BTC-USD", config);

// Initialize with order book
strategy->initialize(orderBook);

// Start the strategy
strategy->start();

// Let it run for a while...
std::this_thread::sleep_for(std::chrono::minutes(5));

// Get statistics
std::string stats = strategy->getStatistics();
std::cout << stats << std::endl;

// Stop the strategy
strategy->stop();
```

### Running an exchange simulation

```cpp
// Create order book
auto orderBook = std::make_shared<pinnacle::OrderBook>("BTC-USD");

// Create exchange simulator
auto simulator = std::make_shared<pinnacle::exchange::ExchangeSimulator>(orderBook);

// Configure simulator
simulator->setVolatility(0.2);
simulator->setDrift(0.01);
simulator->setTickSize(0.01);

// Add market participants
simulator->addMarketParticipant("taker", 10.0, 0.3);
simulator->addMarketParticipant("maker", 20.0, 0.4);
simulator->addMarketParticipant("noise", 5.0, 0.1);

// Start the simulator
simulator->start();

// Let it run for a while...
std::this_thread::sleep_for(std::chrono::minutes(5));

// Stop the simulator
simulator->stop();
```

## Performance Considerations

When using the PinnacleMM API, keep the following performance considerations in mind:

1. **Thread Safety**: All public methods of OrderBook are thread-safe, but may block during high contention.
2. **Lock-Free Structures**: Use LockFreeQueue for inter-thread communication to avoid contention.
3. **Callback Performance**: OrderBook update callbacks should execute quickly to avoid blocking.
4. **Memory Management**: Use shared_ptr judiciously as they incur reference counting overhead.
5. **Time Precision**: Be consistent with time units (nanoseconds vs microseconds).

## Error Handling

Most API methods return a boolean indicator of success. Check return values and handle errors appropriately. For example:

```cpp
if (!orderBook->addOrder(order)) {
    // Handle order addition failure
    std::cerr << "Failed to add order: " << order->getOrderId() << std::endl;
}

if (!strategy->start()) {
    // Handle strategy start failure
    std::cerr << "Failed to start strategy" << std::endl;
}
```
