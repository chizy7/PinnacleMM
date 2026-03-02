#pragma once

#include "../../exchange/simulator/ExchangeSimulator.h"
#include "../../strategies/basic/BasicMarketMaker.h"
#include "../../strategies/config/StrategyConfig.h"
#include "../orderbook/LockFreeOrderBook.h"
#include "../orderbook/OrderBook.h"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace pinnacle {
namespace instrument {

/**
 * @struct InstrumentConfig
 * @brief Configuration for a single instrument
 */
struct InstrumentConfig {
  std::string symbol{"BTC-USD"};
  bool useLockFree{true};
  bool enableML{false};
  double baseSpreadBps{10.0};
  double orderQuantity{0.01};
  double maxPosition{10.0};
};

/**
 * @struct InstrumentContext
 * @brief Holds all per-instrument components
 */
struct InstrumentContext {
  std::string symbol;
  std::shared_ptr<OrderBook> orderBook;
  std::shared_ptr<strategy::BasicMarketMaker> strategy;
  std::shared_ptr<exchange::ExchangeSimulator> simulator; // null in live mode
  InstrumentConfig config;
  bool running{false};
};

/**
 * @class InstrumentManager
 * @brief Central class to manage multiple {symbol, orderbook, strategy,
 * simulator} tuples
 *
 * Manages the lifecycle of per-instrument components. Supports adding/removing
 * instruments at runtime. Keeps single-symbol mode working for backward
 * compatibility.
 *
 * Contexts are stored as shared_ptr so that getContext() returns a safe handle
 * that remains valid even if the map is modified concurrently.
 */
class InstrumentManager {
public:
  InstrumentManager() = default;
  ~InstrumentManager();

  InstrumentManager(const InstrumentManager&) = delete;
  InstrumentManager& operator=(const InstrumentManager&) = delete;

  /**
   * @brief Add an instrument with its own orderbook, strategy, and simulator
   * @param config Instrument configuration
   * @param mode Operating mode ("simulation", "live", "backtest")
   * @return true if the instrument was added successfully
   */
  bool addInstrument(const InstrumentConfig& config, const std::string& mode);

  /**
   * @brief Remove an instrument by symbol (stops it first)
   * @param symbol Trading symbol to remove
   * @return true if the instrument was removed successfully
   */
  bool removeInstrument(const std::string& symbol);

  /**
   * @brief Start all registered instruments
   * @return true if all instruments started successfully
   */
  bool startAll();

  /**
   * @brief Stop all registered instruments
   * @return true if all instruments stopped successfully
   */
  bool stopAll();

  /**
   * @brief Start a specific instrument
   * @param symbol Trading symbol
   * @return true if started successfully
   */
  bool startInstrument(const std::string& symbol);

  /**
   * @brief Stop a specific instrument
   * @param symbol Trading symbol
   * @return true if stopped successfully
   */
  bool stopInstrument(const std::string& symbol);

  /**
   * @brief Get the context for a specific instrument
   * @param symbol Trading symbol
   * @return shared_ptr to InstrumentContext, or nullptr if not found
   */
  std::shared_ptr<InstrumentContext> getContext(const std::string& symbol);

  /**
   * @brief Get the context for a specific instrument (const version)
   * @param symbol Trading symbol
   * @return shared_ptr to const InstrumentContext, or nullptr if not found
   */
  std::shared_ptr<const InstrumentContext>
  getContext(const std::string& symbol) const;

  /**
   * @brief Get all registered symbols
   * @return Vector of symbol strings
   */
  std::vector<std::string> getSymbols() const;

  /**
   * @brief Get number of registered instruments
   * @return Count of instruments
   */
  size_t getInstrumentCount() const;

  /**
   * @brief Check if a symbol is registered
   * @param symbol Trading symbol
   * @return true if the symbol is registered
   */
  bool hasInstrument(const std::string& symbol) const;

  /**
   * @brief Get aggregate statistics across all instruments
   * @return Formatted statistics string
   */
  std::string getAggregateStatistics() const;

  /**
   * @brief Create checkpoints for all order books
   */
  void createCheckpoints();

private:
  mutable std::mutex m_mutex;
  std::unordered_map<std::string, std::shared_ptr<InstrumentContext>>
      m_instruments;
};

} // namespace instrument
} // namespace pinnacle
