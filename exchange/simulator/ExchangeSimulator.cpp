#include "ExchangeSimulator.h"
#include "../../core/utils/TimeUtils.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>

namespace pinnacle {
namespace exchange {

ExchangeSimulator::ExchangeSimulator(std::shared_ptr<OrderBook> orderBook)
    : m_orderBook(orderBook), m_rng(std::random_device()()),
      m_priceDistribution(0.0, 1.0) {

  // Initialize market price
  if (m_orderBook) {
    double midPrice = m_orderBook->getMidPrice();
    if (midPrice > 0) {
      m_lastPrice = midPrice;
    }
  }

  // Add some default market participants
  addMarketParticipant("taker", 10.0,
                       0.3); // 10 trades per minute, 30% of volume
  addMarketParticipant("maker", 20.0,
                       0.4); // 20 trades per minute, 40% of volume
  addMarketParticipant("noise", 5.0, 0.1); // 5 trades per minute, 10% of volume
  addMarketParticipant("arbitrageur", 2.0,
                       0.2); // 2 trades per minute, 20% of volume
}

ExchangeSimulator::~ExchangeSimulator() {
  // Ensure simulator is stopped
  stop();
}

bool ExchangeSimulator::start() {
  // Check if already running
  if (m_isRunning.load(std::memory_order_acquire)) {
    return false;
  }

  // Reset stop flag
  m_shouldStop.store(false, std::memory_order_release);

  // Start threads
  m_mainThread = std::thread(&ExchangeSimulator::mainLoop, this);
  m_participantThread = std::thread(&ExchangeSimulator::participantLoop, this);

  // If we have a market data feed, start the market data thread
  if (m_marketDataFeed) {
    m_marketDataThread = std::thread(&ExchangeSimulator::marketDataLoop, this);
    m_marketDataFeed->start();
  }

  // Mark as running
  m_isRunning.store(true, std::memory_order_release);

  return true;
}

bool ExchangeSimulator::stop() {
  // Check if already stopped
  if (!m_isRunning.load(std::memory_order_acquire)) {
    return false;
  }

  // Set stop flag
  m_shouldStop.store(true, std::memory_order_release);

  // Join threads
  if (m_mainThread.joinable()) {
    m_mainThread.join();
  }

  if (m_participantThread.joinable()) {
    m_participantThread.join();
  }

  // Stop market data feed
  if (m_marketDataFeed && m_marketDataFeed->isRunning()) {
    m_marketDataFeed->stop();

    if (m_marketDataThread.joinable()) {
      m_marketDataThread.join();
    }
  }

  // Mark as stopped
  m_isRunning.store(false, std::memory_order_release);

  return true;
}

bool ExchangeSimulator::isRunning() const {
  return m_isRunning.load(std::memory_order_acquire);
}

void ExchangeSimulator::setMarketDataFeed(
    std::shared_ptr<MarketDataFeed> marketDataFeed) {
  // Only allow setting market data feed when not running
  if (!m_isRunning.load(std::memory_order_acquire)) {
    m_marketDataFeed = marketDataFeed;
  }
}

// These methods are now implemented inline in the header file
// void ExchangeSimulator::setVolatility(double volatility) - implemented inline
// void ExchangeSimulator::setDrift(double drift) - implemented inline
// void ExchangeSimulator::setTickSize(double tickSize) - implemented inline

void ExchangeSimulator::addMarketParticipant(const std::string &type,
                                             double frequency,
                                             double volumeRatio) {
  // Lock for thread safety
  std::lock_guard<std::mutex> lock(m_participantsMutex);

  // Create participant
  MarketParticipant participant;
  participant.type = type;
  participant.frequency = std::max(0.1, frequency);
  participant.volumeRatio = std::max(0.0, std::min(volumeRatio, 1.0));

  // Configure activity distribution (exponential with mean = 1/frequency
  // minutes)
  participant.activityDistribution =
      std::exponential_distribution<double>(frequency / 60.0);

  // Set initial activity time
  participant.nextActivityTime =
      utils::TimeUtils::getCurrentSeconds() +
      static_cast<uint64_t>(participant.activityDistribution(m_rng));

  // Add to participants list
  m_participants.push_back(participant);
}

void ExchangeSimulator::mainLoop() {
  // Main simulator loop
  while (!m_shouldStop.load(std::memory_order_acquire)) {
    // Update market price
    updateMarketPrice();

    // Sleep for a short interval
    utils::TimeUtils::sleepForMillis(100);
  }
}

void ExchangeSimulator::marketDataLoop() {
  // Market data loop
  while (!m_shouldStop.load(std::memory_order_acquire) && m_marketDataFeed) {
    // Get current order book state
    if (m_orderBook) {
      std::shared_ptr<OrderBookSnapshot> snapshot = m_orderBook->getSnapshot();

      if (snapshot) {
        // Create order book update
        OrderBookUpdate update;
        update.symbol = m_orderBook->getSymbol();
        update.timestamp = utils::TimeUtils::getCurrentNanos();

        // Extract bids and asks
        const auto &bids = snapshot->getBids();
        const auto &asks = snapshot->getAsks();

        // Add bids to update
        for (const auto &level : bids) {
          update.bids.emplace_back(level.price, level.totalQuantity);
        }

        // Add asks to update
        for (const auto &level : asks) {
          update.asks.emplace_back(level.price, level.totalQuantity);
        }

        // Publish update
        m_marketDataFeed->publishOrderBookUpdate(update);
      }
    }

    // Sleep for a short interval
    utils::TimeUtils::sleepForMillis(500);
  }
}

void ExchangeSimulator::participantLoop() {
  // Participant activity loop
  while (!m_shouldStop.load(std::memory_order_acquire)) {
    uint64_t currentTime = utils::TimeUtils::getCurrentSeconds();

    // Lock for thread safety
    std::lock_guard<std::mutex> lock(m_participantsMutex);

    // Check each participant
    for (auto &participant : m_participants) {
      // Check if it's time for activity
      if (currentTime >= participant.nextActivityTime) {
        // Perform activity based on participant type
        if (participant.type == "taker") {
          // Takers submit market orders
          double quantity =
              0.01 + (0.1 * participant.volumeRatio * (m_rng() % 10) / 10.0);
          OrderSide side =
              (m_rng() % 2 == 0) ? OrderSide::BUY : OrderSide::SELL;
          simulateMarketOrder(side, quantity);
        } else if (participant.type == "maker") {
          // Makers submit limit orders
          double quantity =
              0.01 + (0.2 * participant.volumeRatio * (m_rng() % 10) / 10.0);
          OrderSide side =
              (m_rng() % 2 == 0) ? OrderSide::BUY : OrderSide::SELL;
          double priceOffset = m_tickSize * (1 + m_rng() % 5);
          double price;

          if (side == OrderSide::BUY) {
            price = m_lastPrice - priceOffset;
          } else {
            price = m_lastPrice + priceOffset;
          }

          simulateLimitOrder(side, roundToTickSize(price), quantity);
        } else if (participant.type == "arbitrageur") {
          // Arbitrageurs look for imbalances
          double imbalance =
              m_orderBook ? m_orderBook->calculateOrderBookImbalance(5) : 0.0;

          if (std::abs(imbalance) > 0.3) {
            // Trade against the imbalance
            OrderSide side = (imbalance > 0) ? OrderSide::SELL : OrderSide::BUY;
            double quantity =
                0.05 + (0.2 * participant.volumeRatio * (m_rng() % 10) / 10.0);
            simulateMarketOrder(side, quantity);
          }
        } else if (participant.type == "noise") {
          // Noise traders randomly cancel orders or submit small orders
          if (m_rng() % 3 == 0) {
            simulateOrderCancellation();
          } else {
            double quantity = 0.001 + (0.01 * (m_rng() % 10) / 10.0);
            OrderSide side =
                (m_rng() % 2 == 0) ? OrderSide::BUY : OrderSide::SELL;

            if (m_rng() % 2 == 0) {
              simulateMarketOrder(side, quantity);
            } else {
              double priceOffset = m_tickSize * (1 + m_rng() % 10);
              double price;

              if (side == OrderSide::BUY) {
                price = m_lastPrice - priceOffset;
              } else {
                price = m_lastPrice + priceOffset;
              }

              simulateLimitOrder(side, roundToTickSize(price), quantity);
            }
          }
        }

        // Schedule next activity
        participant.nextActivityTime =
            currentTime +
            static_cast<uint64_t>(participant.activityDistribution(m_rng));
      }
    }

    // Sleep for a short interval
    utils::TimeUtils::sleepForMillis(100);
  }
}

void ExchangeSimulator::simulateMarketOrder(OrderSide side, double quantity) {
  if (!m_orderBook) {
    return;
  }

  // Execute market order
  std::vector<std::pair<std::string, double>> fills;
  double executedQuantity =
      m_orderBook->executeMarketOrder(side, quantity, fills);

  // Update last price if there was a fill
  if (!fills.empty() && executedQuantity > 0) {
    // Calculate volume-weighted price
    double volumeWeightedPrice = 0.0;
    double totalVolume = 0.0;

    for (const auto &fill : fills) {
      const std::string &orderId = fill.first;
      double fillQuantity = fill.second;

      auto order = m_orderBook->getOrder(orderId);
      if (order) {
        volumeWeightedPrice += order->getPrice() * fillQuantity;
        totalVolume += fillQuantity;
      }
    }

    if (totalVolume > 0) {
      m_lastPrice = volumeWeightedPrice / totalVolume;
    }

    // Publish market update if market data feed is available
    if (m_marketDataFeed) {
      MarketUpdate update;
      update.symbol = m_orderBook->getSymbol();
      update.price = m_lastPrice;
      update.volume = executedQuantity;
      update.timestamp = utils::TimeUtils::getCurrentNanos();
      update.isBuy = (side == OrderSide::BUY);

      m_marketDataFeed->publishMarketUpdate(update);
    }
  }
}

void ExchangeSimulator::simulateLimitOrder(OrderSide side, double price,
                                           double quantity) {
  if (!m_orderBook) {
    return;
  }

  // Generate order ID
  std::string orderId = generateOrderId();

  // Create and add order
  auto order = std::make_shared<Order>(orderId, m_orderBook->getSymbol(), side,
                                       OrderType::LIMIT, price, quantity,
                                       utils::TimeUtils::getCurrentNanos());

  m_orderBook->addOrder(order);
}

void ExchangeSimulator::simulateOrderCancellation() {
  if (!m_orderBook) {
    return;
  }

  // Get all orders
  auto snapshot = m_orderBook->getSnapshot();
  if (!snapshot) {
    return;
  }

  // Combine bids and asks
  std::vector<std::string> orderIds;

  // Add bid orders
  for (const auto &level : snapshot->getBids()) {
    for (const auto &order : level.orders) {
      orderIds.push_back(order->getOrderId());
    }
  }

  // Add ask orders
  for (const auto &level : snapshot->getAsks()) {
    for (const auto &order : level.orders) {
      orderIds.push_back(order->getOrderId());
    }
  }

  // Select random order to cancel
  if (!orderIds.empty()) {
    size_t index = m_rng() % orderIds.size();
    m_orderBook->cancelOrder(orderIds[index]);
  }
}

void ExchangeSimulator::updateMarketPrice() {
  // Get current mid price from order book if available
  double currentPrice = m_lastPrice;
  if (m_orderBook) {
    double midPrice = m_orderBook->getMidPrice();
    if (midPrice > 0) {
      currentPrice = midPrice;
    }
  }

  // Generate price change
  double priceChange = generateRandomPrice();

  // Apply price change
  m_lastPrice = roundToTickSize(currentPrice + priceChange);

  // Ensure price is positive
  m_lastPrice = std::max(m_tickSize, m_lastPrice);
}

double ExchangeSimulator::generateRandomPrice() {
  // Generate random price movement
  double noise = m_priceDistribution(m_rng);
  double drift = m_drift * m_volatility * 0.1;

  // Scale based on current price and volatility
  return m_lastPrice * (noise + drift) * 0.001;
}

double ExchangeSimulator::roundToTickSize(double price) const {
  return std::round(price / m_tickSize) * m_tickSize;
}

std::string ExchangeSimulator::generateOrderId() {
  // Generate a unique order ID
  std::ostringstream oss;
  oss << "sim-" << utils::TimeUtils::getCurrentNanos() << "-"
      << (m_rng() % 1000);
  return oss.str();
}

} // namespace exchange
} // namespace pinnacle