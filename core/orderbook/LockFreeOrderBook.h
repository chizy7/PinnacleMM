#pragma once

#include "../utils/LockFreeOrderBook.h"
#include "OrderBook.h"
#include <limits>

namespace pinnacle {

/**
 * @class LockFreeOrderBook
 * @brief A wrapper around the original OrderBook class that uses lock-free data
 * structures
 *
 * This class maintains the same interface as the original OrderBook class
 * but uses lock-free data structures internally for improved performance.
 */
class LockFreeOrderBook : public OrderBook {
private:
  // Lock-free order book implementation
  std::unique_ptr<utils::LockFreeOrderBook> m_lockFreeOrderBook;

  // Store callbacks locally
  std::vector<OrderBookUpdateCallback> m_callbacks;

  // Adapter for update callbacks
  void onLockFreeOrderBookUpdate();

public:
  // Constructor
  explicit LockFreeOrderBook(const std::string &symbol);

  // Destructor
  virtual ~LockFreeOrderBook();

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

  // Callback registration for order book events
  void registerUpdateCallback(OrderBookUpdateCallback callback);
};

} // namespace pinnacle