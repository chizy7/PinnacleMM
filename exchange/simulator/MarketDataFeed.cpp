#include "MarketDataFeed.h"
#include "../../core/utils/TimeUtils.h"

#include <algorithm>
#include <iostream>
#include <random>

namespace pinnacle {
namespace exchange {

// SimulatedMarketDataFeed implementation

SimulatedMarketDataFeed::SimulatedMarketDataFeed() {}

SimulatedMarketDataFeed::~SimulatedMarketDataFeed() { stop(); }

bool SimulatedMarketDataFeed::start() {
  // Check if already running
  if (m_isRunning.load(std::memory_order_acquire)) {
    return false;
  }

  // Reset stop flag
  m_shouldStop.store(false, std::memory_order_release);

  // Start simulation thread
  m_simulationThread =
      std::thread(&SimulatedMarketDataFeed::simulationLoop, this);

  // Mark as running
  m_isRunning.store(true, std::memory_order_release);

  return true;
}

bool SimulatedMarketDataFeed::stop() {
  // Check if already stopped
  if (!m_isRunning.load(std::memory_order_acquire)) {
    return false;
  }

  // Set stop flag
  m_shouldStop.store(true, std::memory_order_release);

  // Wait for simulation thread to exit
  if (m_simulationThread.joinable()) {
    m_simulationThread.join();
  }

  // Mark as stopped
  m_isRunning.store(false, std::memory_order_release);

  return true;
}

bool SimulatedMarketDataFeed::isRunning() const {
  return m_isRunning.load(std::memory_order_acquire);
}

bool SimulatedMarketDataFeed::subscribeToMarketUpdates(
    const std::string &symbol,
    std::function<void(const MarketUpdate &)> callback) {

  // Acquire lock for callback registration
  std::lock_guard<std::mutex> lock(m_callbacksMutex);

  // Add callback to the list for this symbol
  m_marketUpdateCallbacks[symbol].push_back(std::move(callback));

  return true;
}

bool SimulatedMarketDataFeed::subscribeToOrderBookUpdates(
    const std::string &symbol,
    std::function<void(const OrderBookUpdate &)> callback) {

  // Acquire lock for callback registration
  std::lock_guard<std::mutex> lock(m_callbacksMutex);

  // Add callback to the list for this symbol
  m_orderBookUpdateCallbacks[symbol].push_back(std::move(callback));

  return true;
}

bool SimulatedMarketDataFeed::unsubscribeFromMarketUpdates(
    const std::string &symbol) {
  // Acquire lock for callback management
  std::lock_guard<std::mutex> lock(m_callbacksMutex);

  // Remove all callbacks for this symbol
  m_marketUpdateCallbacks.erase(symbol);

  return true;
}

bool SimulatedMarketDataFeed::unsubscribeFromOrderBookUpdates(
    const std::string &symbol) {
  // Acquire lock for callback management
  std::lock_guard<std::mutex> lock(m_callbacksMutex);

  // Remove all callbacks for this symbol
  m_orderBookUpdateCallbacks.erase(symbol);

  return true;
}

void SimulatedMarketDataFeed::publishMarketUpdate(const MarketUpdate &update) {
  // Acquire lock for callback access
  std::lock_guard<std::mutex> lock(m_callbacksMutex);

  // Find callbacks for this symbol
  auto it = m_marketUpdateCallbacks.find(update.symbol);
  if (it != m_marketUpdateCallbacks.end()) {
    // Call all registered callbacks
    for (const auto &callback : it->second) {
      callback(update);
    }
  }
}

void SimulatedMarketDataFeed::publishOrderBookUpdate(
    const OrderBookUpdate &update) {
  // Acquire lock for callback access
  std::lock_guard<std::mutex> lock(m_callbacksMutex);

  // Find callbacks for this symbol
  auto it = m_orderBookUpdateCallbacks.find(update.symbol);
  if (it != m_orderBookUpdateCallbacks.end()) {
    // Call all registered callbacks
    for (const auto &callback : it->second) {
      callback(update);
    }
  }
}

void SimulatedMarketDataFeed::setUpdateFrequency(double frequencyHz) {
  // Ensure frequency is positive
  if (frequencyHz > 0) {
    m_updateFrequencyHz = frequencyHz;
  }
}

void SimulatedMarketDataFeed::simulationLoop() {
  // Calculate sleep time based on update frequency
  const uint64_t sleepTimeMs =
      static_cast<uint64_t>(1000.0 / m_updateFrequencyHz);

  // Main simulation loop
  while (!m_shouldStop.load(std::memory_order_acquire)) {
    // Generate and publish simulated data
    generateSimulatedData();

    // Sleep for the configured interval
    utils::TimeUtils::sleepForMillis(sleepTimeMs);
  }
}

void SimulatedMarketDataFeed::generateSimulatedData() {
  // This is a simplified implementation for Phase 1
  // In a more sophisticated implementation, we would generate realistic market
  // data based on statistical models or historical data

  // For now, we'll just generate random market updates for all subscribed
  // symbols

  // Get current timestamp
  uint64_t timestamp = utils::TimeUtils::getCurrentNanos();

  // Random number generation
  static std::mt19937 rng(std::random_device{}());
  static std::uniform_real_distribution<double> priceDist(-0.1, 0.1);
  static std::uniform_real_distribution<double> volumeDist(0.01, 1.0);
  static std::bernoulli_distribution sideDist(0.5);

  // Acquire lock for callback access
  std::lock_guard<std::mutex> lock(m_callbacksMutex);

  // Generate market updates for all subscribed symbols
  for (const auto &pair : m_marketUpdateCallbacks) {
    const std::string &symbol = pair.first;

    // Generate random price and volume
    double price =
        10000.0 * (1.0 + priceDist(rng)); // Base price around 10000.0
    double volume = volumeDist(rng);
    bool isBuy = sideDist(rng);

    // Create and publish market update
    MarketUpdate update;
    update.symbol = symbol;
    update.price = price;
    update.volume = volume;
    update.timestamp = timestamp;
    update.isBuy = isBuy;

    // Publish to all subscribers
    publishMarketUpdate(update);
  }

  // Generate order book updates for all subscribed symbols
  for (const auto &pair : m_orderBookUpdateCallbacks) {
    const std::string &symbol = pair.first;

    // Create order book update
    OrderBookUpdate update;
    update.symbol = symbol;
    update.timestamp = timestamp;

    // Generate 5 random bid levels
    for (int i = 0; i < 5; ++i) {
      double price = 10000.0 * (1.0 - 0.001 * i + priceDist(rng) * 0.1);
      double quantity = volumeDist(rng) * 10.0;
      update.bids.emplace_back(price, quantity);
    }

    // Generate 5 random ask levels
    for (int i = 0; i < 5; ++i) {
      double price = 10000.0 * (1.0 + 0.001 * i + priceDist(rng) * 0.1);
      double quantity = volumeDist(rng) * 10.0;
      update.asks.emplace_back(price, quantity);
    }

    // Sort bid levels (descending by price)
    std::sort(update.bids.begin(), update.bids.end(),
              [](const auto &a, const auto &b) { return a.first > b.first; });

    // Sort ask levels (ascending by price)
    std::sort(update.asks.begin(), update.asks.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });

    // Publish to all subscribers
    publishOrderBookUpdate(update);
  }
}

} // namespace exchange
} // namespace pinnacle