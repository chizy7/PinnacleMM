#include "BasicMarketMaker.h"
#include "../../core/utils/TimeUtils.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>

namespace pinnacle {
namespace strategy {

BasicMarketMaker::BasicMarketMaker(const std::string& symbol,
                                   const StrategyConfig& config)
    : m_symbol(symbol), m_config(config) {
  // Validate configuration
  std::string errorReason;
  if (!config.validate(errorReason)) {
    throw std::invalid_argument("Invalid strategy configuration: " +
                                errorReason);
  }
}

BasicMarketMaker::~BasicMarketMaker() {
  // Ensure strategy is stopped before destruction
  stop();
}

bool BasicMarketMaker::initialize(std::shared_ptr<OrderBook> orderBook) {
  if (!orderBook) {
    return false;
  }

  m_orderBook = orderBook;

  // Register for order book updates
  m_orderBook->registerUpdateCallback([this](const OrderBook& orderBook) {
    this->onOrderBookUpdate(orderBook);
  });

  return true;
}

bool BasicMarketMaker::start() {
  // Check if already running
  if (m_isRunning.load(std::memory_order_acquire)) {
    return false;
  }

  // Reset stop flag
  m_shouldStop.store(false, std::memory_order_release);

  // Start the strategy thread
  m_strategyThread = std::thread(&BasicMarketMaker::strategyMainLoop, this);

  // Mark as running
  m_isRunning.store(true, std::memory_order_release);

  return true;
}

bool BasicMarketMaker::stop() {
  // Check if already stopped
  if (!m_isRunning.load(std::memory_order_acquire)) {
    return false;
  }

  // Set stop flag
  m_shouldStop.store(true, std::memory_order_release);

  // Notify the strategy thread
  m_eventCondition.notify_all();

  // Wait for the strategy thread to exit
  if (m_strategyThread.joinable()) {
    m_strategyThread.join();
  }

  // Cancel all outstanding orders
  cancelAllOrders();

  // Mark as stopped
  m_isRunning.store(false, std::memory_order_release);

  return true;
}

bool BasicMarketMaker::isRunning() const {
  return m_isRunning.load(std::memory_order_acquire);
}

void BasicMarketMaker::onOrderBookUpdate(const OrderBook& orderBook) {
  // Create an order book update event
  Event event;
  event.type = EventType::ORDER_BOOK_UPDATE;
  event.timestamp = utils::TimeUtils::getCurrentNanos();
  // event.data = std::make_shared<OrderBook>(orderBook);
  // Store a pointer to the existing OrderBook instead of copying it
  event.data = std::shared_ptr<void>(std::shared_ptr<OrderBook>{},
                                     const_cast<OrderBook*>(&orderBook));

  // Add to event queue
  if (!m_eventQueue.tryEnqueue(event)) {
    // Queue is full, this shouldn't happen in normal operation
    // In a production system, we would log this and possibly alert
  }

  // Notify the strategy thread
  m_eventCondition.notify_one();
}

void BasicMarketMaker::onTrade(const std::string& symbol, double price,
                               double quantity, OrderSide side,
                               uint64_t timestamp) {
  // Ignore trades for other symbols
  if (symbol != m_symbol) {
    return;
  }

  // Create a trade event
  auto tradeInfo = std::make_shared<TradeInfo>();
  tradeInfo->symbol = symbol;
  tradeInfo->price = price;
  tradeInfo->quantity = quantity;
  tradeInfo->side = side;
  tradeInfo->timestamp = timestamp;

  Event event;
  event.type = EventType::TRADE;
  event.timestamp = utils::TimeUtils::getCurrentNanos();
  event.data = tradeInfo;

  // Add to event queue
  if (!m_eventQueue.tryEnqueue(event)) {
    // Queue is full, this shouldn't happen in normal operation
  }

  // Notify the strategy thread
  m_eventCondition.notify_one();
}

void BasicMarketMaker::onOrderUpdate(const std::string& orderId,
                                     OrderStatus status, double filledQuantity,
                                     uint64_t timestamp) {
  // Create an order update event
  auto updateInfo = std::make_shared<OrderUpdateInfo>();
  updateInfo->orderId = orderId;
  updateInfo->status = status;
  updateInfo->filledQuantity = filledQuantity;
  updateInfo->timestamp = timestamp;

  Event event;
  event.type = EventType::ORDER_UPDATE;
  event.timestamp = utils::TimeUtils::getCurrentNanos();
  event.data = updateInfo;

  // Add to event queue
  if (!m_eventQueue.tryEnqueue(event)) {
    // Queue is full, this shouldn't happen in normal operation
  }

  // Notify the strategy thread
  m_eventCondition.notify_one();
}

void BasicMarketMaker::onMarketUpdate(
    const pinnacle::exchange::MarketUpdate& update) {
  if (update.symbol != m_symbol) {
    return; // Ignore updates for other symbols
  }

  spdlog::debug("Strategy received ticker update for {}: price={:.2f}, "
                "volume={:.6f}, bid={:.2f}, ask={:.2f}",
                update.symbol, update.price, update.volume, update.bidPrice,
                update.askPrice);

  // Update strategy statistics for ticker processing
  {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    m_stats.quoteUpdateCount++;
  }

  // Log strategy metrics to JSON if enabled
  if (m_jsonLogger) {
    nlohmann::json metrics = {{"strategy_name", "BasicMarketMaker"},
                              {"quote_updates", m_stats.quoteUpdateCount},
                              {"position", m_position.load()},
                              {"pnl", m_pnl.load()},
                              {"market_price", update.price},
                              {"bid_price", update.bidPrice},
                              {"ask_price", update.askPrice},
                              {"volume", update.volume}};
    m_jsonLogger->logStrategyMetrics("BasicMarketMaker", m_symbol, metrics);
  }

  // Here I could add logic to:
  // - Update internal market state tracking
  // - Adjust spread parameters based on market volatility
  // - Trigger rebalancing based on price movements
  // - Update ML features if using enhanced strategies

  // For now, I'll just log the ticker data reception
  // This confirms the callback registration is working
}

std::string BasicMarketMaker::getStatistics() const {
  // Lock for thread safety
  std::lock_guard<std::mutex> lock(m_statsMutex);

  std::ostringstream oss;
  oss << "BasicMarketMaker Statistics:" << std::endl;
  oss << "  Symbol: " << m_symbol << std::endl;
  oss << "  Running: " << (m_isRunning.load() ? "Yes" : "No") << std::endl;
  oss << "  Position: " << std::fixed << std::setprecision(6)
      << m_position.load() << std::endl;
  oss << "  PnL: $" << std::fixed << std::setprecision(2) << m_pnl.load()
      << std::endl;
  oss << "  Quote Updates: " << m_stats.quoteUpdateCount << std::endl;
  oss << "  Orders Placed: " << m_stats.orderPlacedCount << std::endl;
  oss << "  Orders Filled: " << m_stats.orderFilledCount << std::endl;
  oss << "  Orders Canceled: " << m_stats.orderCanceledCount << std::endl;
  oss << "  Total Volume Traded: " << std::fixed << std::setprecision(6)
      << m_stats.totalVolumeTraded << std::endl;
  oss << "  Max Position: " << std::fixed << std::setprecision(6)
      << m_stats.maxPosition << std::endl;
  oss << "  Min Position: " << std::fixed << std::setprecision(6)
      << m_stats.minPosition << std::endl;
  oss << "  Max PnL: $" << std::fixed << std::setprecision(2) << m_stats.maxPnL
      << std::endl;
  oss << "  Min PnL: $" << std::fixed << std::setprecision(2) << m_stats.minPnL
      << std::endl;

  return oss.str();
}

double BasicMarketMaker::getPosition() const {
  return m_position.load(std::memory_order_relaxed);
}

double BasicMarketMaker::getPnL() const {
  return m_pnl.load(std::memory_order_relaxed);
}

bool BasicMarketMaker::updateConfig(const StrategyConfig& config) {
  // Validate the new configuration
  std::string errorReason;
  if (!config.validate(errorReason)) {
    // Log the error reason for debugging
    // In a production system, this would use a proper logging framework
    // such as the spdlog I've integrated in main.cpp
    std::cerr << "Invalid configuration update: " << errorReason << std::endl;
    return false;
  }

  // Create a config update event
  Event event;
  event.type = EventType::CONFIG_UPDATE;
  event.timestamp = utils::TimeUtils::getCurrentNanos();
  event.data = std::make_shared<StrategyConfig>(config);

  // Add to event queue
  if (!m_eventQueue.tryEnqueue(event)) {
    // Queue is full, this shouldn't happen in normal operation
    return false;
  }

  // Notify the strategy thread
  m_eventCondition.notify_one();

  return true;
}

void BasicMarketMaker::strategyMainLoop() {
  uint64_t lastQuoteUpdateTime = 0;

  while (!m_shouldStop.load(std::memory_order_acquire)) {
    // Process all pending events
    processEvents();

    // Current time
    uint64_t currentTime = utils::TimeUtils::getCurrentNanos();

    // Check if it's time to update quotes
    if (currentTime - lastQuoteUpdateTime >
        m_config.quoteUpdateIntervalMs * 1000000) {
      updateQuotes();
      lastQuoteUpdateTime = currentTime;
    }

    // Update statistics
    updateStatistics();

    // Wait for events or timeout
    {
      std::unique_lock<std::mutex> lock(m_eventMutex);
      m_eventCondition.wait_for(
          lock, std::chrono::milliseconds(m_config.quoteUpdateIntervalMs / 2),
          [this] { return !m_eventQueue.isEmpty() || m_shouldStop.load(); });
    }
  }
}

void BasicMarketMaker::processEvents() {
  // Process up to 100 events per call to avoid blocking for too long
  for (int i = 0; i < 100; ++i) {
    // Try to dequeue an event
    Event event;
    bool success = m_eventQueue.tryDequeue(event);
    if (!success) {
      break;
    }

    // Process based on event type
    switch (event.type) {
    case EventType::ORDER_BOOK_UPDATE:
      // Order book update handled separately - just triggers quote updates
      break;

    case EventType::TRADE: {
      // Market trade notification
      auto tradeInfo = std::static_pointer_cast<TradeInfo>(event.data);

      // Update market state based on trade
      // This could adjust our spread or order sizes based on market activity
      break;
    }

    case EventType::ORDER_UPDATE: {
      // Order status update
      auto updateInfo = std::static_pointer_cast<OrderUpdateInfo>(event.data);

      // Lock for thread safety
      std::lock_guard<std::mutex> lock(m_ordersMutex);

      auto it = m_activeOrders.find(updateInfo->orderId);
      if (it != m_activeOrders.end()) {
        OrderInfo& orderInfo = it->second;

        // Calculate fill delta
        double fillDelta =
            updateInfo->filledQuantity - orderInfo.filledQuantity;

        // Update order info
        orderInfo.status = updateInfo->status;
        orderInfo.filledQuantity = updateInfo->filledQuantity;

        // Update position and P&L if there was a fill
        if (fillDelta > 0) {
          // Update position
          double positionDelta =
              (orderInfo.side == OrderSide::BUY) ? fillDelta : -fillDelta;
          double currentPosition = m_position.load(std::memory_order_relaxed);
          double newPosition = currentPosition + positionDelta;
          m_position.store(newPosition, std::memory_order_relaxed);

          // Update statistics
          {
            std::lock_guard<std::mutex> statsLock(m_statsMutex);
            m_stats.orderFilledCount++;
            m_stats.totalVolumeTraded += fillDelta;
            m_stats.maxPosition = std::max(m_stats.maxPosition, newPosition);
            m_stats.minPosition = std::min(m_stats.minPosition, newPosition);
          }
        }

        // Remove completed orders
        if (orderInfo.status == OrderStatus::FILLED ||
            orderInfo.status == OrderStatus::CANCELED ||
            orderInfo.status == OrderStatus::REJECTED ||
            orderInfo.status == OrderStatus::EXPIRED) {

          // Update statistics for cancellations
          if (orderInfo.status == OrderStatus::CANCELED) {
            std::lock_guard<std::mutex> statsLock(m_statsMutex);
            m_stats.orderCanceledCount++;
          }

          // Remove the order
          m_activeOrders.erase(it);
        }
      }
      break;
    }

    case EventType::CONFIG_UPDATE: {
      // Configuration update
      auto newConfig = std::static_pointer_cast<StrategyConfig>(event.data);

      // Update configuration
      m_config = *newConfig;

      // Immediately update quotes with new config
      updateQuotes();
      break;
    }
    }
  }
}

void BasicMarketMaker::updateQuotes() {
  // Ensure we have a valid order book
  if (!m_orderBook) {
    return;
  }

  // Cancel existing orders
  cancelAllOrders();

  // Get current market prices
  double bestBid = m_orderBook->getBestBidPrice();
  double bestAsk = m_orderBook->getBestAskPrice();
  double midPrice = m_orderBook->getMidPrice();

  // Skip if market is not two-sided
  if (bestBid <= 0 || bestAsk >= std::numeric_limits<double>::max()) {
    return;
  }

  // Calculate target spread
  double targetSpread = calculateTargetSpread();

  // Calculate prices for our quotes
  double bidPrice = midPrice - (targetSpread / 2.0);
  double askPrice = midPrice + (targetSpread / 2.0);

  // Apply inventory skew
  double skewFactor = calculateInventorySkewFactor();
  bidPrice -= skewFactor * midPrice * 0.0001; // 1 bps adjustment per 0.01 skew
  askPrice -= skewFactor * midPrice * 0.0001; // 1 bps adjustment per 0.01 skew

  // Calculate order quantities
  double bidQuantity = calculateOrderQuantity(OrderSide::BUY);
  double askQuantity = calculateOrderQuantity(OrderSide::SELL);

  // Place orders (ensure minimum sizes)
  if (bidQuantity >= m_config.minOrderQuantity) {
    placeOrder(OrderSide::BUY, bidPrice, bidQuantity);
  }

  if (askQuantity >= m_config.minOrderQuantity) {
    placeOrder(OrderSide::SELL, askPrice, askQuantity);
  }

  // Update quote statistics
  {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    m_stats.quoteUpdateCount++;
  }
}

void BasicMarketMaker::cancelAllOrders() {
  std::lock_guard<std::mutex> lock(m_ordersMutex);

  // Copy order IDs to avoid iterator invalidation
  std::vector<std::string> orderIds;
  for (const auto& pair : m_activeOrders) {
    orderIds.push_back(pair.first);
  }

  // Cancel each order
  for (const auto& orderId : orderIds) {
    // In a real system, we would call the exchange API here
    m_orderBook->cancelOrder(orderId);
  }
}

void BasicMarketMaker::placeOrder(OrderSide side, double price,
                                  double quantity) {
  // In a real system, we would call the exchange API here
  // For now, just create an order and add it to our order book

  // Generate a unique order ID
  std::string orderId = m_symbol + "-" +
                        (side == OrderSide::BUY ? "BUY-" : "SELL-") +
                        std::to_string(utils::TimeUtils::getCurrentNanos());

  // Create the order
  auto order =
      std::make_shared<Order>(orderId, m_symbol, side, OrderType::LIMIT, price,
                              quantity, utils::TimeUtils::getCurrentNanos());

  // Add to order book
  if (m_orderBook->addOrder(order)) {
    // Track the order
    std::lock_guard<std::mutex> lock(m_ordersMutex);

    OrderInfo orderInfo;
    orderInfo.orderId = orderId;
    orderInfo.side = side;
    orderInfo.price = price;
    orderInfo.quantity = quantity;
    orderInfo.filledQuantity = 0.0;
    orderInfo.status = OrderStatus::NEW;
    orderInfo.timestamp = utils::TimeUtils::getCurrentNanos();

    m_activeOrders[orderId] = orderInfo;

    // Update statistics
    {
      std::lock_guard<std::mutex> statsLock(m_statsMutex);
      m_stats.orderPlacedCount++;
    }
  }
}

void BasicMarketMaker::updateStatistics() {
  // Update PnL
  double currentPosition = m_position.load(std::memory_order_relaxed);
  double midPrice = m_orderBook->getMidPrice();
  double unrealizedPnL = currentPosition * midPrice;

  // In a real system, we would track realized P&L from fills
  double estimatedPnL = unrealizedPnL;
  m_pnl.store(estimatedPnL, std::memory_order_relaxed);

  // Update P&L statistics
  {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    m_stats.maxPnL = std::max(m_stats.maxPnL, estimatedPnL);
    m_stats.minPnL = std::min(m_stats.minPnL, estimatedPnL);
  }
}

double BasicMarketMaker::calculateTargetSpread() const {
  // Start with base spread
  double targetSpread =
      m_config.baseSpreadBps * 0.0001 * m_orderBook->getMidPrice();

  // Adjust for order book imbalance
  double imbalance = std::abs(m_orderBook->calculateOrderBookImbalance(5));
  if (imbalance > m_config.imbalanceThreshold) {
    targetSpread *= (1.0 + imbalance);
  }

  // Adjust for market volatility (simplified)
  // In a real system, we would calculate actual volatility

  // Ensure spread is within min/max bounds
  double minSpread =
      m_config.minSpreadBps * 0.0001 * m_orderBook->getMidPrice();
  double maxSpread =
      m_config.maxSpreadBps * 0.0001 * m_orderBook->getMidPrice();

  return std::max(minSpread, std::min(targetSpread, maxSpread));
}

double BasicMarketMaker::calculateOrderQuantity(OrderSide side) const {
  // Start with base quantity
  double quantity = m_config.orderQuantity;

  // Adjust for inventory
  double currentPosition = m_position.load(std::memory_order_relaxed);
  double positionRatio = currentPosition / m_config.maxPosition;

  // Reduce buy quantity when long, reduce sell quantity when short
  if (side == OrderSide::BUY && positionRatio > 0) {
    quantity *= (1.0 - positionRatio);
  } else if (side == OrderSide::SELL && positionRatio < 0) {
    quantity *= (1.0 + positionRatio);
  }

  // Ensure within min/max bounds
  return std::max(m_config.minOrderQuantity,
                  std::min(quantity, m_config.maxOrderQuantity));
}

double BasicMarketMaker::calculateInventorySkewFactor() const {
  double currentPosition = m_position.load(std::memory_order_relaxed);
  double positionRatio = currentPosition / m_config.maxPosition;

  // Apply inventory skew factor (0-1)
  return m_config.inventorySkewFactor * positionRatio;
}

void BasicMarketMaker::setJsonLogger(
    std::shared_ptr<utils::JsonLogger> jsonLogger) {
  m_jsonLogger = jsonLogger;
}

} // namespace strategy
} // namespace pinnacle
