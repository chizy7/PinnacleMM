#include "Order.h"
#include "../utils/TimeUtils.h"

namespace pinnacle {

Order::Order(const std::string& orderId, const std::string& symbol,
             OrderSide side, OrderType type, double price, double quantity,
             uint64_t timestamp)
    : m_orderId(orderId), m_symbol(symbol), m_side(side), m_type(type),
      m_price(price), m_quantity(quantity), m_timestamp(timestamp) {

  // Validate inputs
  if (price < 0.0 || quantity < 0.0) {
    throw std::invalid_argument("price and quantity must be non-negative");
  }

  // Initialize atomic members
  m_status.store(OrderStatus::NEW, std::memory_order_relaxed);
  m_filledQuantity.store(0.0, std::memory_order_relaxed);
  m_lastUpdateTime.store(timestamp, std::memory_order_relaxed);
}

Order::Order(Order&& other) noexcept
    : m_orderId(std::move(other.m_orderId)),
      m_symbol(std::move(other.m_symbol)), m_side(other.m_side),
      m_type(other.m_type), m_price(other.m_price),
      m_quantity(other.m_quantity), m_timestamp(other.m_timestamp) {

  // Move atomic members
  m_status.store(other.m_status.load(std::memory_order_relaxed),
                 std::memory_order_relaxed);
  m_filledQuantity.store(other.m_filledQuantity.load(std::memory_order_relaxed),
                         std::memory_order_relaxed);
  m_lastUpdateTime.store(other.m_lastUpdateTime.load(std::memory_order_relaxed),
                         std::memory_order_relaxed);
}

Order& Order::operator=(Order&& other) noexcept {
  if (this != &other) {
    m_orderId = std::move(other.m_orderId);
    m_symbol = std::move(other.m_symbol);
    m_side = other.m_side;
    m_type = other.m_type;
    m_price = other.m_price;
    m_quantity = other.m_quantity;
    m_timestamp = other.m_timestamp;

    // Move atomic members
    m_status.store(other.m_status.load(std::memory_order_relaxed),
                   std::memory_order_relaxed);
    m_filledQuantity.store(
        other.m_filledQuantity.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
    m_lastUpdateTime.store(
        other.m_lastUpdateTime.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
  }
  return *this;
}

double Order::getRemainingQuantity() const {
  const double filled = m_filledQuantity.load(std::memory_order_relaxed);
  return m_quantity - filled;
}

void Order::updateStatus(OrderStatus newStatus) {
  m_status.store(newStatus, std::memory_order_release);
  updateLastUpdateTime(utils::TimeUtils::getCurrentNanos());
}

bool Order::fill(double fillQuantity, uint64_t timestamp) {
  if (fillQuantity <= 0) {
    return false;
  }

  double currentFilled = m_filledQuantity.load(std::memory_order_relaxed);
  double newFilled = currentFilled + fillQuantity;

  // Check if the fill would exceed the order quantity
  if (newFilled > m_quantity) {
    return false;
  }

  // Update filled quantity
  m_filledQuantity.store(newFilled, std::memory_order_release);

  // Update the status based on fill amount
  if (newFilled == m_quantity) {
    m_status.store(OrderStatus::FILLED, std::memory_order_release);
  } else {
    m_status.store(OrderStatus::PARTIALLY_FILLED, std::memory_order_release);
  }

  // Update the last update time
  updateLastUpdateTime(timestamp);

  return true;
}

bool Order::cancel(uint64_t timestamp) {
  OrderStatus currentStatus = m_status.load(std::memory_order_relaxed);

  // Can only cancel active orders
  if (currentStatus == OrderStatus::NEW ||
      currentStatus == OrderStatus::PARTIALLY_FILLED) {
    m_status.store(OrderStatus::CANCELED, std::memory_order_release);
    updateLastUpdateTime(timestamp);
    return true;
  }

  return false;
}

bool Order::reject(uint64_t timestamp) {
  OrderStatus currentStatus = m_status.load(std::memory_order_relaxed);

  // Can only reject new orders
  if (currentStatus == OrderStatus::NEW) {
    m_status.store(OrderStatus::REJECTED, std::memory_order_release);
    updateLastUpdateTime(timestamp);
    return true;
  }

  return false;
}

bool Order::expire(uint64_t timestamp) {
  OrderStatus currentStatus = m_status.load(std::memory_order_relaxed);

  // Can only expire active orders
  if (currentStatus == OrderStatus::NEW ||
      currentStatus == OrderStatus::PARTIALLY_FILLED) {
    m_status.store(OrderStatus::EXPIRED, std::memory_order_release);
    updateLastUpdateTime(timestamp);
    return true;
  }

  return false;
}

bool Order::isActive() const {
  OrderStatus currentStatus = m_status.load(std::memory_order_relaxed);
  return currentStatus == OrderStatus::NEW ||
         currentStatus == OrderStatus::PARTIALLY_FILLED;
}

bool Order::isCompleted() const {
  OrderStatus currentStatus = m_status.load(std::memory_order_relaxed);
  return currentStatus == OrderStatus::FILLED ||
         currentStatus == OrderStatus::CANCELED ||
         currentStatus == OrderStatus::REJECTED ||
         currentStatus == OrderStatus::EXPIRED;
}

bool Order::operator<(const Order& other) const {
  // Primary ordering by price (buy: higher price has priority, sell: lower
  // price has priority)
  if (m_side == OrderSide::BUY) {
    if (m_price != other.m_price) {
      return m_price < other.m_price;
    }
  } else { // SELL
    if (m_price != other.m_price) {
      return m_price > other.m_price;
    }
  }

  // Secondary ordering by time (earlier orders have priority)
  return m_timestamp > other.m_timestamp;
}

bool Order::operator>(const Order& other) const { return other < *this; }

void Order::updateLastUpdateTime(uint64_t timestamp) {
  m_lastUpdateTime.store(timestamp, std::memory_order_release);
}

} // namespace pinnacle
