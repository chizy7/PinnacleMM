#include "InstrumentManager.h"
#include "../../strategies/basic/MLEnhancedMarketMaker.h"
#include "../persistence/PersistenceManager.h"

#include <spdlog/spdlog.h>
#include <sstream>

namespace pinnacle {
namespace instrument {

InstrumentManager::~InstrumentManager() { stopAll(); }

bool InstrumentManager::addInstrument(const InstrumentConfig& config,
                                      const std::string& mode) {
  std::lock_guard<std::mutex> lock(m_mutex);

  if (m_instruments.count(config.symbol)) {
    spdlog::warn("Instrument {} already registered", config.symbol);
    return false;
  }

  InstrumentContext ctx;
  ctx.symbol = config.symbol;
  ctx.config = config;

  // Try to recover order book from persistence
  auto& persistenceManager = persistence::PersistenceManager::getInstance();
  ctx.orderBook = persistenceManager.getRecoveredOrderBook(config.symbol);

  if (ctx.orderBook) {
    spdlog::info("Using recovered order book for {} with {} existing orders",
                 config.symbol, ctx.orderBook->getOrderCount());
  } else {
    if (config.useLockFree) {
      ctx.orderBook = std::make_shared<LockFreeOrderBook>(config.symbol);
      spdlog::info("[{}] Using lock-free order book", config.symbol);
    } else {
      ctx.orderBook = std::make_shared<OrderBook>(config.symbol);
      spdlog::info("[{}] Using mutex-based order book", config.symbol);
    }
  }

  // Create strategy
  strategy::StrategyConfig stratConfig;
  stratConfig.symbol = config.symbol;
  stratConfig.baseSpreadBps = config.baseSpreadBps;
  stratConfig.orderQuantity = config.orderQuantity;
  stratConfig.maxPosition = config.maxPosition;

  if (config.enableML) {
    strategy::MLEnhancedMarketMaker::MLConfig mlConfig{};
    mlConfig.enableMLSpreadOptimization = true;
    mlConfig.enableOnlineLearning = true;
    mlConfig.fallbackToHeuristics = true;
    mlConfig.mlConfidenceThreshold = 0.5;

    ctx.strategy = std::make_shared<strategy::MLEnhancedMarketMaker>(
        config.symbol, stratConfig, mlConfig);
    spdlog::info("[{}] Using ML-enhanced market maker", config.symbol);
  } else {
    ctx.strategy = std::make_shared<strategy::BasicMarketMaker>(config.symbol,
                                                                stratConfig);
    spdlog::info("[{}] Using basic market maker", config.symbol);
  }

  // Create simulator for non-live modes
  if (mode != "live") {
    ctx.simulator =
        std::make_shared<exchange::ExchangeSimulator>(ctx.orderBook);
  }

  m_instruments.emplace(config.symbol, std::move(ctx));
  spdlog::info("Instrument {} added (mode={})", config.symbol, mode);
  return true;
}

bool InstrumentManager::removeInstrument(const std::string& symbol) {
  std::lock_guard<std::mutex> lock(m_mutex);

  auto it = m_instruments.find(symbol);
  if (it == m_instruments.end()) {
    spdlog::warn("Instrument {} not found for removal", symbol);
    return false;
  }

  auto& ctx = it->second;

  // Stop components
  if (ctx.strategy && ctx.strategy->isRunning()) {
    ctx.strategy->stop();
  }
  if (ctx.simulator && ctx.simulator->isRunning()) {
    ctx.simulator->stop();
  }

  m_instruments.erase(it);
  spdlog::info("Instrument {} removed", symbol);
  return true;
}

bool InstrumentManager::startAll() {
  std::lock_guard<std::mutex> lock(m_mutex);

  bool allOk = true;
  for (auto& [symbol, ctx] : m_instruments) {
    if (ctx.running) {
      continue;
    }

    if (!ctx.strategy) {
      spdlog::error("[{}] Strategy is null", symbol);
      allOk = false;
      continue;
    }

    // Initialize strategy
    if (!ctx.strategy->initialize(ctx.orderBook)) {
      spdlog::error("[{}] Failed to initialize strategy", symbol);
      allOk = false;
      continue;
    }

    // Start strategy
    if (!ctx.strategy->start()) {
      spdlog::error("[{}] Failed to start strategy", symbol);
      allOk = false;
      continue;
    }

    // Start simulator if present
    if (ctx.simulator) {
      if (!ctx.simulator->start()) {
        spdlog::error("[{}] Failed to start simulator", symbol);
        ctx.strategy->stop();
        allOk = false;
        continue;
      }
    }

    ctx.running = true;
    spdlog::info("[{}] Instrument started", symbol);
  }

  return allOk;
}

bool InstrumentManager::stopAll() {
  std::lock_guard<std::mutex> lock(m_mutex);

  bool allOk = true;
  for (auto& [symbol, ctx] : m_instruments) {
    if (!ctx.running) {
      continue;
    }

    if (ctx.strategy && ctx.strategy->isRunning()) {
      if (!ctx.strategy->stop()) {
        spdlog::error("[{}] Failed to stop strategy", symbol);
        allOk = false;
      }
    }

    if (ctx.simulator && ctx.simulator->isRunning()) {
      if (!ctx.simulator->stop()) {
        spdlog::error("[{}] Failed to stop simulator", symbol);
        allOk = false;
      }
    }

    ctx.running = false;
    spdlog::info("[{}] Instrument stopped", symbol);
  }

  return allOk;
}

bool InstrumentManager::startInstrument(const std::string& symbol) {
  std::lock_guard<std::mutex> lock(m_mutex);

  auto it = m_instruments.find(symbol);
  if (it == m_instruments.end()) {
    return false;
  }

  auto& ctx = it->second;
  if (ctx.running) {
    return true;
  }

  if (!ctx.strategy) {
    spdlog::error("[{}] Strategy is null", symbol);
    return false;
  }
  if (!ctx.strategy->initialize(ctx.orderBook)) {
    spdlog::error("[{}] Failed to initialize strategy", symbol);
    return false;
  }
  if (!ctx.strategy->start()) {
    spdlog::error("[{}] Failed to start strategy", symbol);
    return false;
  }
  if (ctx.simulator && !ctx.simulator->start()) {
    spdlog::error("[{}] Failed to start simulator", symbol);
    ctx.strategy->stop();
    return false;
  }

  ctx.running = true;
  return true;
}

bool InstrumentManager::stopInstrument(const std::string& symbol) {
  std::lock_guard<std::mutex> lock(m_mutex);

  auto it = m_instruments.find(symbol);
  if (it == m_instruments.end()) {
    return false;
  }

  auto& ctx = it->second;
  if (!ctx.running) {
    return true;
  }

  if (ctx.strategy && ctx.strategy->isRunning()) {
    ctx.strategy->stop();
  }
  if (ctx.simulator && ctx.simulator->isRunning()) {
    ctx.simulator->stop();
  }

  ctx.running = false;
  return true;
}

InstrumentContext* InstrumentManager::getContext(const std::string& symbol) {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_instruments.find(symbol);
  if (it == m_instruments.end()) {
    return nullptr;
  }
  return &it->second;
}

const InstrumentContext*
InstrumentManager::getContext(const std::string& symbol) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_instruments.find(symbol);
  if (it == m_instruments.end()) {
    return nullptr;
  }
  return &it->second;
}

std::vector<std::string> InstrumentManager::getSymbols() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::vector<std::string> symbols;
  symbols.reserve(m_instruments.size());
  for (const auto& [sym, _] : m_instruments) {
    symbols.push_back(sym);
  }
  return symbols;
}

size_t InstrumentManager::getInstrumentCount() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_instruments.size();
}

bool InstrumentManager::hasInstrument(const std::string& symbol) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_instruments.count(symbol) > 0;
}

std::string InstrumentManager::getAggregateStatistics() const {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::ostringstream oss;
  double totalPnL = 0.0;
  double totalPosition = 0.0;
  size_t totalOrders = 0;

  for (const auto& [symbol, ctx] : m_instruments) {
    oss << "--- " << symbol << " ---\n";

    if (ctx.orderBook) {
      oss << "  Best bid: " << ctx.orderBook->getBestBidPrice() << "\n";
      oss << "  Best ask: " << ctx.orderBook->getBestAskPrice() << "\n";
      oss << "  Mid price: " << ctx.orderBook->getMidPrice() << "\n";
      oss << "  Spread: " << ctx.orderBook->getSpread() << "\n";
      oss << "  Order count: " << ctx.orderBook->getOrderCount() << "\n";
      totalOrders += ctx.orderBook->getOrderCount();
    }

    if (ctx.strategy) {
      oss << ctx.strategy->getStatistics() << "\n";
      totalPnL += ctx.strategy->getPnL();
      totalPosition += ctx.strategy->getPosition();
    }
  }

  oss << "--- AGGREGATE ---\n";
  oss << "  Instruments: " << m_instruments.size() << "\n";
  oss << "  Total PnL: " << totalPnL << "\n";
  oss << "  Total Position: " << totalPosition << "\n";
  oss << "  Total Orders: " << totalOrders << "\n";

  return oss.str();
}

void InstrumentManager::createCheckpoints() {
  std::lock_guard<std::mutex> lock(m_mutex);
  for (auto& [symbol, ctx] : m_instruments) {
    if (ctx.orderBook) {
      ctx.orderBook->createCheckpoint();
    }
  }
}

} // namespace instrument
} // namespace pinnacle
