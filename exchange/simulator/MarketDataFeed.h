#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace pinnacle {
namespace exchange {

/**
 * @struct MarketUpdate
 * @brief Structure containing market update information
 */
struct MarketUpdate {
  std::string symbol;
  double price;
  double volume;
  uint64_t timestamp;
  bool isBuy;
};

/**
 * @struct OrderBookUpdate
 * @brief Structure containing order book update information
 */
struct OrderBookUpdate {
  std::string symbol;
  std::vector<std::pair<double, double>> bids; // (price, quantity)
  std::vector<std::pair<double, double>> asks; // (price, quantity)
  uint64_t timestamp;
};

/**
 * @class MarketDataFeed
 * @brief Interface for market data feeds (real or simulated)
 */
class MarketDataFeed {
public:
  /**
   * @brief Destructor
   */
  virtual ~MarketDataFeed() = default;

  /**
   * @brief Start the market data feed
   *
   * @return true if the feed was started successfully, false otherwise
   */
  virtual bool start() = 0;

  /**
   * @brief Stop the market data feed
   *
   * @return true if the feed was stopped successfully, false otherwise
   */
  virtual bool stop() = 0;

  /**
   * @brief Check if the feed is running
   *
   * @return true if the feed is running, false otherwise
   */
  virtual bool isRunning() const = 0;

  /**
   * @brief Subscribe to market updates for a symbol
   *
   * @param symbol Trading symbol
   * @param callback Function to call when a market update is received
   * @return true if the subscription was successful, false otherwise
   */
  virtual bool subscribeToMarketUpdates(
      const std::string& symbol,
      std::function<void(const MarketUpdate&)> callback) = 0;

  /**
   * @brief Subscribe to order book updates for a symbol
   *
   * @param symbol Trading symbol
   * @param callback Function to call when an order book update is received
   * @return true if the subscription was successful, false otherwise
   */
  virtual bool subscribeToOrderBookUpdates(
      const std::string& symbol,
      std::function<void(const OrderBookUpdate&)> callback) = 0;

  /**
   * @brief Unsubscribe from market updates for a symbol
   *
   * @param symbol Trading symbol
   * @return true if the unsubscription was successful, false otherwise
   */
  virtual bool unsubscribeFromMarketUpdates(const std::string& symbol) = 0;

  /**
   * @brief Unsubscribe from order book updates for a symbol
   *
   * @param symbol Trading symbol
   * @return true if the unsubscription was successful, false otherwise
   */
  virtual bool unsubscribeFromOrderBookUpdates(const std::string& symbol) = 0;

  /**
   * @brief Publish a market update to subscribers
   *
   * @param update Market update to publish
   */
  virtual void publishMarketUpdate(const MarketUpdate& update) = 0;

  /**
   * @brief Publish an order book update to subscribers
   *
   * @param update Order book update to publish
   */
  virtual void publishOrderBookUpdate(const OrderBookUpdate& update) = 0;
};

/**
 * @class SimulatedMarketDataFeed
 * @brief Simulated market data feed for testing
 */
class SimulatedMarketDataFeed : public MarketDataFeed {
public:
  /**
   * @brief Constructor
   */
  SimulatedMarketDataFeed();

  /**
   * @brief Destructor
   */
  ~SimulatedMarketDataFeed() override;

  // Implementation of MarketDataFeed interface
  bool start() override;
  bool stop() override;
  bool isRunning() const override;

  bool subscribeToMarketUpdates(
      const std::string& symbol,
      std::function<void(const MarketUpdate&)> callback) override;

  bool subscribeToOrderBookUpdates(
      const std::string& symbol,
      std::function<void(const OrderBookUpdate&)> callback) override;

  bool unsubscribeFromMarketUpdates(const std::string& symbol) override;
  bool unsubscribeFromOrderBookUpdates(const std::string& symbol) override;

  void publishMarketUpdate(const MarketUpdate& update) override;
  void publishOrderBookUpdate(const OrderBookUpdate& update) override;

  /**
   * @brief Set the update frequency
   *
   * @param frequencyHz Updates per second
   */
  void setUpdateFrequency(double frequencyHz);

private:
  // Feed state
  std::atomic<bool> m_isRunning{false};
  std::atomic<bool> m_shouldStop{false};
  double m_updateFrequencyHz{1.0};

  // Subscription management
  std::unordered_map<std::string,
                     std::vector<std::function<void(const MarketUpdate&)>>>
      m_marketUpdateCallbacks;
  std::unordered_map<std::string,
                     std::vector<std::function<void(const OrderBookUpdate&)>>>
      m_orderBookUpdateCallbacks;
  std::mutex m_callbacksMutex;

  // Simulator thread
  std::thread m_simulationThread;

  // Internal implementation
  void simulationLoop();
  void generateSimulatedData();
};

} // namespace exchange
} // namespace pinnacle
