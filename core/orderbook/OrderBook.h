#pragma once

#include "../persistence/journal/Journal.h"
#include "../utils/LockFreeQueue.h"
#include "Order.h"

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace pinnacle {

// Forward declarations
class OrderBookSnapshot;

/**
 * @struct PriceLevel
 * @brief Represents a price level in the order book with all orders at that
 * price
 */
struct PriceLevel {
  double price;
  double totalQuantity;
  std::vector<std::shared_ptr<Order>> orders;

  // Constructors
  explicit PriceLevel(double price);
  PriceLevel(); // Default constructor

  // Add an order to this price level
  void addOrder(std::shared_ptr<Order> order);

  // Remove an order from this price level
  bool removeOrder(const std::string& orderId);

  // Update total quantity
  void updateTotalQuantity();
};

/**
 * @class OrderBook
 * @brief Maintains the full limit order book for a single instrument
 *
 * Optimized for ultra-low latency with lock-free structures where possible
 */
class OrderBook {
public:
  // Constructor
  explicit OrderBook(const std::string& symbol);

  // Destructor
  ~OrderBook() = default;

  // Deleted copy and move operations to prevent accidental copies
  OrderBook(const OrderBook&) = delete;
  OrderBook& operator=(const OrderBook&) = delete;
  OrderBook(OrderBook&&) = delete;
  OrderBook& operator=(OrderBook&&) = delete;

  // Core order book functionality
  bool addOrder(std::shared_ptr<Order> order);
  bool cancelOrder(const std::string& orderId);
  bool executeOrder(const std::string& orderId, double quantity);

  // Market order execution
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

  // Order book depth
  std::vector<PriceLevel> getBidLevels(size_t depth) const;
  std::vector<PriceLevel> getAskLevels(size_t depth) const;

  // Market depth calculations
  double calculateMarketImpact(OrderSide side, double quantity) const;
  double calculateVolumeWeightedAveragePrice(OrderSide side,
                                             double quantity) const;

  // Order book imbalance calculations
  double calculateOrderBookImbalance(size_t depth) const;

  // Take a snapshot of the current order book state
  std::shared_ptr<OrderBookSnapshot> getSnapshot() const;

  // Clear the order book
  void clear();

  // Get the symbol for this order book
  const std::string& getSymbol() const { return m_symbol; }

  // Callback registration for order book events
  using OrderBookUpdateCallback = std::function<void(const OrderBook&)>;
  void registerUpdateCallback(OrderBookUpdateCallback callback);

  // Recovery methods
  bool
  recoverFromJournal(std::shared_ptr<persistence::journal::Journal> journal);
  void createCheckpoint();

private:
  // Symbol for this order book
  std::string m_symbol;

  // Price level structures
  // Using maps for price levels (ordered by price)
  // For bids: higher price has higher priority (reverse order)
  // For asks: lower price has higher priority (natural order)
  std::map<double, PriceLevel, std::greater<double>> m_bids;
  std::map<double, PriceLevel, std::less<double>> m_asks;

  // Order lookup map (order id -> order pointer)
  std::unordered_map<std::string, std::shared_ptr<Order>> m_orders;

  // Reader-writer lock for thread safety
  // Allows multiple readers or single writer
  mutable std::shared_mutex m_mutex;

  // Atomic counters for order book statistics
  std::atomic<size_t> m_orderCount{0};
  std::atomic<uint64_t> m_updateCount{0};

  // Callbacks for order book updates
  std::vector<OrderBookUpdateCallback> m_updateCallbacks;

  // Notify all registered callbacks about an update
  void notifyUpdate();

  // Internal helper methods for order matching
  bool matchOrder(std::shared_ptr<Order> order);
  bool canMatch(const Order& takerOrder, const Order& makerOrder) const;

  // Persistence-related fields
  std::shared_ptr<persistence::journal::Journal> m_journal;
  uint64_t m_lastCheckpointSequence{0};

  // Initialize persistence
  void initializePersistence();

  // Journal operations
  void journalAddOrder(std::shared_ptr<Order> order);
  void journalCancelOrder(const std::string& orderId);
  void journalExecuteOrder(const std::string& orderId, double quantity);
  void
  journalMarketOrder(OrderSide side, double quantity,
                     const std::vector<std::pair<std::string, double>>& fills);

  // Internal non-journaling versions for recovery
  bool addOrderInternal(std::shared_ptr<Order> order);
  bool cancelOrderInternal(const std::string& orderId);
  bool executeOrderInternal(const std::string& orderId, double quantity);
  void executeMarketOrderInternal(
      OrderSide side, double quantity,
      const std::vector<std::pair<std::string, double>>& fills);
};

/**
 * @class OrderBookSnapshot
 * @brief Immutable snapshot of the order book at a point in time
 */
class OrderBookSnapshot {
public:
  OrderBookSnapshot(const std::string& symbol, uint64_t timestamp,
                    std::vector<PriceLevel> bids, std::vector<PriceLevel> asks);

  // Accessors
  const std::string& getSymbol() const { return m_symbol; }
  uint64_t getTimestamp() const { return m_timestamp; }
  const std::vector<PriceLevel>& getBids() const { return m_bids; }
  const std::vector<PriceLevel>& getAsks() const { return m_asks; }

private:
  std::string m_symbol;
  uint64_t m_timestamp;
  std::vector<PriceLevel> m_bids;
  std::vector<PriceLevel> m_asks;
};

} // namespace pinnacle
