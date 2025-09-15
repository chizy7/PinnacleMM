#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

namespace pinnacle {

/**
 * @enum OrderSide
 * @brief Represents the side of an order (buy or sell)
 */
enum class OrderSide : uint8_t { BUY = 0, SELL = 1 };

/**
 * @enum OrderType
 * @brief Represents the type of order
 */
enum class OrderType : uint8_t {
  LIMIT = 0,
  MARKET = 1,
  STOP = 2,
  STOP_LIMIT = 3,
  IOC = 4, // Immediate or Cancel
  FOK = 5  // Fill or Kill
};

/**
 * @enum OrderStatus
 * @brief Represents the current status of an order
 */
enum class OrderStatus : uint8_t {
  NEW = 0,
  PARTIALLY_FILLED = 1,
  FILLED = 2,
  CANCELED = 3,
  REJECTED = 4,
  EXPIRED = 5
};

/**
 * @class Order
 * @brief Represents a single order in the trading system
 *
 * Optimized for memory layout and cache-friendliness
 */
class Order {
public:
  // Constructor for creating a new order
  Order(const std::string& orderId, const std::string& symbol, OrderSide side,
        OrderType type, double price, double quantity, uint64_t timestamp);

  // Default constructor and destructor
  Order() = default;
  ~Order() = default;

  // Move constructor and assignment operator
  Order(Order&& other) noexcept;
  Order& operator=(Order&& other) noexcept;

  // Copy constructor and assignment operator (deleted to prevent unexpected
  // copies)
  Order(const Order&) = delete;
  Order& operator=(const Order&) = delete;

  // Getters (const to ensure they don't modify state)
  const std::string& getOrderId() const { return m_orderId; }
  const std::string& getSymbol() const { return m_symbol; }
  OrderSide getSide() const { return m_side; }
  OrderType getType() const { return m_type; }
  OrderStatus getStatus() const {
    return m_status.load(std::memory_order_relaxed);
  }
  double getPrice() const { return m_price; }
  double getQuantity() const { return m_quantity; }
  double getFilledQuantity() const {
    return m_filledQuantity.load(std::memory_order_relaxed);
  }
  double getRemainingQuantity() const;
  uint64_t getTimestamp() const { return m_timestamp; }
  uint64_t getLastUpdateTime() const {
    return m_lastUpdateTime.load(std::memory_order_relaxed);
  }

  // Setters and modifiers
  void updateStatus(OrderStatus newStatus);
  bool fill(double fillQuantity, uint64_t timestamp);
  bool cancel(uint64_t timestamp);
  bool reject(uint64_t timestamp);
  bool expire(uint64_t timestamp);

  // Utility methods
  bool isBuy() const { return m_side == OrderSide::BUY; }
  bool isSell() const { return m_side == OrderSide::SELL; }
  bool isActive() const;
  bool isCompleted() const;

  // Price comparison operators for priority queue
  bool operator<(const Order& other) const;
  bool operator>(const Order& other) const;

private:
  std::string m_orderId; // Unique order identifier
  std::string m_symbol;  // Trading symbol (e.g., "BTC-USD")
  OrderSide m_side;      // Buy or Sell
  OrderType m_type;      // Order type
  std::atomic<OrderStatus>
      m_status;      // Current status (atomic for thread safety)
  double m_price;    // Price level
  double m_quantity; // Original quantity
  std::atomic<double>
      m_filledQuantity; // Filled quantity (atomic for thread safety)
  uint64_t m_timestamp; // Creation timestamp (nanoseconds)
  std::atomic<uint64_t> m_lastUpdateTime; // Last update timestamp (nanoseconds)

  // Update the last update time
  void updateLastUpdateTime(uint64_t timestamp);
};

} // namespace pinnacle
