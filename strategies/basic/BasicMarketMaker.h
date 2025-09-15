#pragma once

#include "../../core/orderbook/OrderBook.h"
#include "../../core/utils/LockFreeQueue.h"
#include "../config/StrategyConfig.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace pinnacle {
namespace strategy {

/**
 * @class BasicMarketMaker
 * @brief Implements a basic market making strategy with dynamic spread
 * adjustment
 */
class BasicMarketMaker {
public:
  /**
   * @brief Constructor
   *
   * @param symbol Trading symbol
   * @param config Strategy configuration
   */
  BasicMarketMaker(const std::string& symbol, const StrategyConfig& config);

  /**
   * @brief Destructor
   */
  ~BasicMarketMaker();

  /**
   * @brief Initialize the strategy
   *
   * @param orderBook Reference to the order book
   * @return true if initialization was successful, false otherwise
   */
  bool initialize(std::shared_ptr<OrderBook> orderBook);

  /**
   * @brief Start the strategy
   *
   * @return true if the strategy was started successfully, false otherwise
   */
  bool start();

  /**
   * @brief Stop the strategy
   *
   * @return true if the strategy was stopped successfully, false otherwise
   */
  bool stop();

  /**
   * @brief Check if the strategy is running
   *
   * @return true if the strategy is running, false otherwise
   */
  bool isRunning() const;

  /**
   * @brief Handle order book updates
   *
   * @param orderBook Reference to the updated order book
   */
  void onOrderBookUpdate(const OrderBook& orderBook);

  /**
   * @brief Handle trade notifications
   *
   * @param symbol Trading symbol
   * @param price Trade price
   * @param quantity Trade quantity
   * @param side Trade side (buy or sell)
   * @param timestamp Trade timestamp
   */
  void onTrade(const std::string& symbol, double price, double quantity,
               OrderSide side, uint64_t timestamp);

  /**
   * @brief Handle order status updates
   *
   * @param orderId Order ID
   * @param status New order status
   * @param filledQuantity Filled quantity
   * @param timestamp Update timestamp
   */
  void onOrderUpdate(const std::string& orderId, OrderStatus status,
                     double filledQuantity, uint64_t timestamp);

  /**
   * @brief Get the current strategy statistics
   *
   * @return String representation of the strategy statistics
   */
  std::string getStatistics() const;

  /**
   * @brief Get the current position
   *
   * @return Current position (positive for long, negative for short)
   */
  double getPosition() const;

  /**
   * @brief Get the current profit and loss
   *
   * @return Current profit and loss
   */
  double getPnL() const;

  /**
   * @brief Update the strategy configuration
   *
   * @param config New strategy configuration
   * @return true if the configuration was updated successfully, false otherwise
   */
  bool updateConfig(const StrategyConfig& config);

private:
  // Strategy identification
  std::string m_symbol;
  StrategyConfig m_config;

  // Order book reference
  std::shared_ptr<OrderBook> m_orderBook;

  // Strategy state
  std::atomic<bool> m_isRunning{false};
  std::atomic<bool> m_shouldStop{false};
  std::thread m_strategyThread;

  // Position and PnL tracking
  std::atomic<double> m_position{0.0};
  std::atomic<double> m_pnl{0.0};

  // Order tracking
  struct OrderInfo {
    std::string orderId;
    OrderSide side;
    double price;
    double quantity;
    double filledQuantity;
    OrderStatus status;
    uint64_t timestamp;
  };

  // Trade and order update structs
  struct TradeInfo {
    std::string symbol;
    double price;
    double quantity;
    OrderSide side;
    uint64_t timestamp;
  };

  struct OrderUpdateInfo {
    std::string orderId;
    OrderStatus status;
    double filledQuantity;
    uint64_t timestamp;
  };

  std::unordered_map<std::string, OrderInfo> m_activeOrders;
  mutable std::mutex m_ordersMutex;

  // Statistics
  struct Statistics {
    uint64_t quoteUpdateCount{0};
    uint64_t orderPlacedCount{0};
    uint64_t orderFilledCount{0};
    uint64_t orderCanceledCount{0};
    double totalVolumeTraded{0.0};
    double maxPosition{0.0};
    double minPosition{0.0};
    double maxPnL{0.0};
    double minPnL{0.0};
  };

  Statistics m_stats;
  mutable std::mutex m_statsMutex;

  // Internal event queue
  enum class EventType {
    ORDER_BOOK_UPDATE,
    TRADE,
    ORDER_UPDATE,
    CONFIG_UPDATE
  };

  struct Event {
    EventType type;
    uint64_t timestamp;
    std::shared_ptr<void> data;
  };

  utils::LockFreeMPMCQueue<Event, 1024> m_eventQueue;
  std::mutex m_eventMutex;
  std::condition_variable m_eventCondition;

  // Internal implementation methods
  void strategyMainLoop();
  void processEvents();
  void updateQuotes();
  void cancelAllOrders();
  void placeOrder(OrderSide side, double price, double quantity);
  void updateStatistics();
  double calculateTargetSpread() const;
  double calculateOrderQuantity(OrderSide side) const;
  double calculateInventorySkewFactor() const;
};

} // namespace strategy
} // namespace pinnacle
