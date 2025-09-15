#pragma once

#include "../orderbook/Order.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>

namespace pinnacle {
namespace utils {

/**
 * @class LockFreePriceLevel
 * @brief A lock-free implementation of a price level in the order book
 *
 * This class maintains a list of orders at a specific price point,
 * with lock-free operations for adding, removing, and modifying orders.
 */
class LockFreePriceLevel {
private:
  // Price for this level
  double m_price;

  // Total quantity at this price level
  std::atomic<double> m_totalQuantity;

  // Node structure for linked list of orders
  struct OrderNode {
    std::shared_ptr<Order> order;
    std::atomic<OrderNode *> next;

    explicit OrderNode(std::shared_ptr<Order> o)
        : order(std::move(o)), next(nullptr) {}
  };

  // Head and tail pointers for the order list
  std::atomic<OrderNode *> m_head;
  std::atomic<OrderNode *> m_tail;

  // Number of orders at this level
  std::atomic<size_t> m_orderCount;

  // Helper methods
  void updateTotalQuantity();

public:
  // Constructor
  explicit LockFreePriceLevel(double price);

  // Destructor
  ~LockFreePriceLevel();

  // Deleted copy and move operations
  LockFreePriceLevel(const LockFreePriceLevel &) = delete;
  LockFreePriceLevel &operator=(const LockFreePriceLevel &) = delete;
  LockFreePriceLevel(LockFreePriceLevel &&) = delete;
  LockFreePriceLevel &operator=(LockFreePriceLevel &&) = delete;

  // Get price
  double getPrice() const { return m_price; }

  // Get total quantity
  double getTotalQuantity() const {
    return m_totalQuantity.load(std::memory_order_acquire);
  }

  // Get order count
  size_t getOrderCount() const {
    return m_orderCount.load(std::memory_order_acquire);
  }

  // Add an order to this level
  bool addOrder(std::shared_ptr<Order> order);

  // Remove an order from this level
  bool removeOrder(const std::string &orderId);

  // Get all orders at this level
  std::vector<std::shared_ptr<Order>> getOrders() const;

  // Find order by ID
  std::shared_ptr<Order> findOrder(const std::string &orderId) const;

  // Apply a function to each order
  void
  forEachOrder(const std::function<void(std::shared_ptr<Order>)> &func) const;
};

/**
 * @class LockFreeOrderMap
 * @brief A lock-free hash map for fast order lookups
 *
 * This class provides fast lock-free lookups of orders by ID.
 * It uses a striped locking approach with a fixed number of shards
 * to minimize contention.
 */
class LockFreeOrderMap {
private:
  // Number of shards (must be a power of 2)
  static constexpr size_t SHARD_COUNT = 64;

  // Mask for computing shard index
  static constexpr size_t SHARD_MASK = SHARD_COUNT - 1;

  // Shard structure
  struct Shard {
    std::unordered_map<std::string, std::shared_ptr<Order>> orders;
    std::atomic_flag lock = ATOMIC_FLAG_INIT;
  };

  // Array of shards
  std::array<Shard, SHARD_COUNT> m_shards;

  // Total order count
  std::atomic<size_t> m_orderCount{0};

  // Helper methods
  size_t getShardIndex(const std::string &orderId) const;

  // RAII helper for locking a shard
  class ShardGuard {
  private:
    std::atomic_flag &m_lock;

  public:
    explicit ShardGuard(std::atomic_flag &lock);
    ~ShardGuard();
    ShardGuard(const ShardGuard &) = delete;
    ShardGuard &operator=(const ShardGuard &) = delete;
  };

public:
  // Constructor
  LockFreeOrderMap() = default;

  // Destructor
  ~LockFreeOrderMap() = default;

  // Deleted copy and move operations
  LockFreeOrderMap(const LockFreeOrderMap &) = delete;
  LockFreeOrderMap &operator=(const LockFreeOrderMap &) = delete;
  LockFreeOrderMap(LockFreeOrderMap &&) = delete;
  LockFreeOrderMap &operator=(LockFreeOrderMap &&) = delete;

  // Add an order
  bool insert(const std::string &orderId, std::shared_ptr<Order> order);

  // Remove an order
  bool erase(const std::string &orderId);

  // Find an order
  std::shared_ptr<Order> find(const std::string &orderId) const;

  // Check if an order exists
  bool contains(const std::string &orderId) const;

  // Get order count
  size_t size() const { return m_orderCount.load(std::memory_order_acquire); }

  // Clear all orders
  void clear();
};

/**
 * @class LockFreePriceMap
 * @brief A lock-free price level map for order book
 *
 * This class maintains a map of price levels for either the bid or ask side
 * of the order book. It uses RCU (Read-Copy-Update) principles to ensure
 * lock-free read operations.
 *
 * @tparam Comparator The comparator for ordering price levels
 */
template <typename Comparator> class LockFreePriceMap {
private:
  // The map structure
  struct PriceMap {
    std::map<double, std::shared_ptr<LockFreePriceLevel>, Comparator> levels;
    std::atomic<size_t> readers{0};
  };

  // Current and new map pointers
  std::atomic<PriceMap *> m_current;
  PriceMap *m_new;

  // Lock for write operations
  std::atomic_flag m_writeLock = ATOMIC_FLAG_INIT;

  // RAII helper for read operations
  class ReadGuard {
  private:
    PriceMap &m_map;

  public:
    explicit ReadGuard(PriceMap &map);
    ~ReadGuard();
    ReadGuard(const ReadGuard &) = delete;
    ReadGuard &operator=(const ReadGuard &) = delete;
  };

  // RAII helper for write operations
  class WriteGuard {
  private:
    std::atomic_flag &m_lock;

  public:
    explicit WriteGuard(std::atomic_flag &lock);
    ~WriteGuard();
    WriteGuard(const WriteGuard &) = delete;
    WriteGuard &operator=(const WriteGuard &) = delete;
  };

public:
  // Constructor
  LockFreePriceMap();

  // Destructor
  ~LockFreePriceMap();

  // Deleted copy and move operations
  LockFreePriceMap(const LockFreePriceMap &) = delete;
  LockFreePriceMap &operator=(const LockFreePriceMap &) = delete;
  LockFreePriceMap(LockFreePriceMap &&) = delete;
  LockFreePriceMap &operator=(LockFreePriceMap &&) = delete;

  // Add a price level
  bool insertLevel(double price, std::shared_ptr<LockFreePriceLevel> level);

  // Remove a price level
  bool removeLevel(double price);

  // Find a price level
  std::shared_ptr<LockFreePriceLevel> findLevel(double price) const;

  // Get the best price level (first in map)
  std::optional<std::pair<double, std::shared_ptr<LockFreePriceLevel>>>
  getBestLevel() const;

  // Get all price levels up to a certain depth
  std::vector<std::shared_ptr<LockFreePriceLevel>>
  getLevels(size_t depth) const;

  // Get the number of price levels
  size_t size() const;

  // Check if empty
  bool empty() const;

  // Clear all levels
  void clear();

  // Apply a function to each level
  void forEachLevel(
      const std::function<void(double, std::shared_ptr<LockFreePriceLevel>)>
          &func) const;
};

// Template implementation for LockFreePriceMap

template <typename Comparator>
LockFreePriceMap<Comparator>::ReadGuard::ReadGuard(PriceMap &map) : m_map(map) {
  m_map.readers.fetch_add(1, std::memory_order_acquire);
}

template <typename Comparator>
LockFreePriceMap<Comparator>::ReadGuard::~ReadGuard() {
  m_map.readers.fetch_sub(1, std::memory_order_release);
}

template <typename Comparator>
LockFreePriceMap<Comparator>::WriteGuard::WriteGuard(std::atomic_flag &lock)
    : m_lock(lock) {
  while (m_lock.test_and_set(std::memory_order_acquire)) {
    // Spin until we acquire the lock
    // Use a busy-wait instead of std::this_thread::yield() for broader
    // compatibility
  }
}

template <typename Comparator>
LockFreePriceMap<Comparator>::WriteGuard::~WriteGuard() {
  m_lock.clear(std::memory_order_release);
}

template <typename Comparator>
LockFreePriceMap<Comparator>::LockFreePriceMap() {
  m_current.store(new PriceMap(), std::memory_order_release);
  m_new = nullptr;
}

template <typename Comparator>
LockFreePriceMap<Comparator>::~LockFreePriceMap() {
  delete m_current.load(std::memory_order_acquire);
  delete m_new;
}

template <typename Comparator>
bool LockFreePriceMap<Comparator>::insertLevel(
    double price, std::shared_ptr<LockFreePriceLevel> level) {
  WriteGuard guard(m_writeLock);

  PriceMap *current = m_current.load(std::memory_order_acquire);

  // Create a new map as a copy of the current one
  m_new = new PriceMap();
  m_new->levels = current->levels;

  // Insert or update the level
  m_new->levels[price] = level;

  // Publish the new map
  m_current.store(m_new, std::memory_order_release);

  // Wait for any ongoing readers to finish
  while (current->readers.load(std::memory_order_acquire) > 0) {
    // Busy-wait instead of std::this_thread::yield()
  }

  // Delete the old map
  delete current;
  m_new = nullptr;

  return true;
}

template <typename Comparator>
bool LockFreePriceMap<Comparator>::removeLevel(double price) {
  WriteGuard guard(m_writeLock);

  PriceMap *current = m_current.load(std::memory_order_acquire);

  // Check if the level exists
  if (current->levels.find(price) == current->levels.end()) {
    return false;
  }

  // Create a new map as a copy of the current one
  m_new = new PriceMap();
  m_new->levels = current->levels;

  // Remove the level
  m_new->levels.erase(price);

  // Publish the new map
  m_current.store(m_new, std::memory_order_release);

  // Wait for any ongoing readers to finish
  while (current->readers.load(std::memory_order_acquire) > 0) {
    // Busy-wait instead of std::this_thread::yield()
  }

  // Delete the old map
  delete current;
  m_new = nullptr;

  return true;
}

template <typename Comparator>
std::shared_ptr<LockFreePriceLevel>
LockFreePriceMap<Comparator>::findLevel(double price) const {
  PriceMap *current = m_current.load(std::memory_order_acquire);
  ReadGuard guard(*current);

  auto it = current->levels.find(price);
  if (it != current->levels.end()) {
    return it->second;
  }

  return nullptr;
}

template <typename Comparator>
std::optional<std::pair<double, std::shared_ptr<LockFreePriceLevel>>>
LockFreePriceMap<Comparator>::getBestLevel() const {
  PriceMap *current = m_current.load(std::memory_order_acquire);
  ReadGuard guard(*current);

  if (current->levels.empty()) {
    return std::nullopt;
  }

  auto it = current->levels.begin();
  return std::make_optional(std::make_pair(it->first, it->second));
}

template <typename Comparator>
std::vector<std::shared_ptr<LockFreePriceLevel>>
LockFreePriceMap<Comparator>::getLevels(size_t depth) const {
  PriceMap *current = m_current.load(std::memory_order_acquire);
  ReadGuard guard(*current);

  std::vector<std::shared_ptr<LockFreePriceLevel>> result;
  result.reserve(std::min(depth, current->levels.size()));

  size_t count = 0;
  for (const auto &pair : current->levels) {
    result.push_back(pair.second);
    count++;

    if (count >= depth) {
      break;
    }
  }

  return result;
}

template <typename Comparator>
size_t LockFreePriceMap<Comparator>::size() const {
  PriceMap *current = m_current.load(std::memory_order_acquire);
  ReadGuard guard(*current);

  return current->levels.size();
}

template <typename Comparator>
bool LockFreePriceMap<Comparator>::empty() const {
  PriceMap *current = m_current.load(std::memory_order_acquire);
  ReadGuard guard(*current);

  return current->levels.empty();
}

template <typename Comparator> void LockFreePriceMap<Comparator>::clear() {
  WriteGuard guard(m_writeLock);

  PriceMap *current = m_current.load(std::memory_order_acquire);

  // Create a new empty map
  m_new = new PriceMap();

  // Publish the new map
  m_current.store(m_new, std::memory_order_release);

  // Wait for any ongoing readers to finish
  while (current->readers.load(std::memory_order_acquire) > 0) {
    // Busy-wait instead of std::this_thread::yield()
  }

  // Delete the old map
  delete current;
  m_new = nullptr;
}

template <typename Comparator>
void LockFreePriceMap<Comparator>::forEachLevel(
    const std::function<void(double, std::shared_ptr<LockFreePriceLevel>)>
        &func) const {

  PriceMap *current = m_current.load(std::memory_order_acquire);
  ReadGuard guard(*current);

  for (const auto &pair : current->levels) {
    func(pair.first, pair.second);
  }
}

/**
 * @class LockFreeOrderBook
 * @brief A lock-free implementation of an order book
 *
 * This class uses lock-free data structures to maintain a high-performance
 * order book with minimal contention.
 */
class LockFreeOrderBook {
private:
  // Symbol for this order book
  std::string m_symbol;

  // Price maps for bids and asks
  LockFreePriceMap<std::greater<double>> m_bids;
  LockFreePriceMap<std::less<double>> m_asks;

  // Order map for lookups
  LockFreeOrderMap m_orders;

  // Statistics
  std::atomic<uint64_t> m_updateCount{0};

  // Callbacks for updates
  using OrderBookUpdateCallback =
      std::function<void(const LockFreeOrderBook &)>;
  std::vector<OrderBookUpdateCallback> m_updateCallbacks;
  std::atomic_flag m_callbackLock = ATOMIC_FLAG_INIT;

public:
  // Constructor
  explicit LockFreeOrderBook(const std::string &symbol);

  // Destructor
  ~LockFreeOrderBook() = default;

  // Deleted copy and move operations
  LockFreeOrderBook(const LockFreeOrderBook &) = delete;
  LockFreeOrderBook &operator=(const LockFreeOrderBook &) = delete;
  LockFreeOrderBook(LockFreeOrderBook &&) = delete;
  LockFreeOrderBook &operator=(LockFreeOrderBook &&) = delete;

  // Core order book functionality
  bool addOrder(std::shared_ptr<Order> order);
  bool cancelOrder(const std::string &orderId);
  bool executeOrder(const std::string &orderId, double quantity);

  // Market order execution
  double executeMarketOrder(OrderSide side, double quantity,
                            std::vector<std::pair<std::string, double>> &fills);

  // Order book queries
  std::shared_ptr<Order> getOrder(const std::string &orderId) const;
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
  std::vector<std::shared_ptr<LockFreePriceLevel>>
  getBidLevels(size_t depth) const;
  std::vector<std::shared_ptr<LockFreePriceLevel>>
  getAskLevels(size_t depth) const;

  // Market depth calculations
  double calculateMarketImpact(OrderSide side, double quantity) const;
  double calculateVolumeWeightedAveragePrice(OrderSide side,
                                             double quantity) const;

  // Order book imbalance calculations
  double calculateOrderBookImbalance(size_t depth) const;

  // Clear the order book
  void clear();

  // Get the symbol for this order book
  const std::string &getSymbol() const { return m_symbol; }

  // Callback registration
  void registerUpdateCallback(OrderBookUpdateCallback callback);

  // Notify listeners about updates
  void notifyUpdate();
};

} // namespace utils
} // namespace pinnacle