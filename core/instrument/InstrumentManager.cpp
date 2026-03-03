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

  auto ctx = std::make_shared<InstrumentContext>();
  ctx->symbol = config.symbol;
  ctx->config = config;

  // Try to recover order book from persistence
  auto& persistenceManager = persistence::PersistenceManager::getInstance();
  ctx->orderBook = persistenceManager.getRecoveredOrderBook(config.symbol);

  if (ctx->orderBook) {
    spdlog::info("Using recovered order book for {} with {} existing orders",
                 config.symbol, ctx->orderBook->getOrderCount());
  } else {
    if (config.useLockFree) {
      ctx->orderBook = std::make_shared<LockFreeOrderBook>(config.symbol);
      spdlog::info("[{}] Using lock-free order book", config.symbol);
    } else {
      ctx->orderBook = std::make_shared<OrderBook>(config.symbol);
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

    ctx->strategy = std::make_shared<strategy::MLEnhancedMarketMaker>(
        config.symbol, stratConfig, mlConfig);
    spdlog::info("[{}] Using ML-enhanced market maker", config.symbol);
  } else {
    ctx->strategy = std::make_shared<strategy::BasicMarketMaker>(config.symbol,
                                                                 stratConfig);
    spdlog::info("[{}] Using basic market maker", config.symbol);
  }

  // Create simulator for non-live modes
  if (mode != "live") {
    ctx->simulator =
        std::make_shared<exchange::ExchangeSimulator>(ctx->orderBook);
  }

  m_instruments.emplace(config.symbol, std::move(ctx));
  spdlog::info("Instrument {} added (mode={})", config.symbol, mode);
  return true;
}

bool InstrumentManager::removeInstrument(const std::string& symbol) {
  // Extract context under lock, then stop outside lock
  std::shared_ptr<InstrumentContext> ctx;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_instruments.find(symbol);
    if (it == m_instruments.end()) {
      spdlog::warn("Instrument {} not found for removal", symbol);
      return false;
    }
    ctx = it->second;
    m_instruments.erase(it);
  }

  // Stop components outside the lock (these may block)
  if (ctx->strategy && ctx->strategy->isRunning()) {
    ctx->strategy->stop();
  }
  if (ctx->simulator && ctx->simulator->isRunning()) {
    ctx->simulator->stop();
  }

  spdlog::info("Instrument {} removed", symbol);
  return true;
}

bool InstrumentManager::startAll() {
  // Collect symbols to start under lock
  std::vector<std::string> toStart;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [symbol, ctx] : m_instruments) {
      if (!ctx->running) {
        toStart.push_back(symbol);
      }
    }
  }

  bool allOk = true;
  for (const auto& symbol : toStart) {
    // Get components under lock
    std::shared_ptr<strategy::BasicMarketMaker> strategy;
    std::shared_ptr<exchange::ExchangeSimulator> simulator;
    std::shared_ptr<OrderBook> orderBook;
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      auto it = m_instruments.find(symbol);
      if (it == m_instruments.end() || it->second->running) {
        continue;
      }
      strategy = it->second->strategy;
      simulator = it->second->simulator;
      orderBook = it->second->orderBook;
    }

    if (!strategy) {
      spdlog::error("[{}] Strategy is null", symbol);
      allOk = false;
      continue;
    }

    // Blocking calls outside lock
    if (!strategy->initialize(orderBook)) {
      spdlog::error("[{}] Failed to initialize strategy", symbol);
      allOk = false;
      continue;
    }

    if (!strategy->start()) {
      spdlog::error("[{}] Failed to start strategy", symbol);
      allOk = false;
      continue;
    }

    if (simulator) {
      if (!simulator->start()) {
        spdlog::error("[{}] Failed to start simulator", symbol);
        strategy->stop();
        allOk = false;
        continue;
      }
    }

    // Mark running under lock
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      auto it = m_instruments.find(symbol);
      if (it != m_instruments.end()) {
        it->second->running = true;
        spdlog::info("[{}] Instrument started", symbol);
      }
    }
  }

  return allOk;
}

bool InstrumentManager::stopAll() {
  // Collect contexts to stop under lock
  std::vector<std::pair<std::string, std::shared_ptr<InstrumentContext>>>
      toStop;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [symbol, ctx] : m_instruments) {
      if (ctx->running) {
        toStop.emplace_back(symbol, ctx);
      }
    }
  }

  bool allOk = true;
  for (auto& [symbol, ctx] : toStop) {
    // Blocking calls outside lock
    if (ctx->strategy && ctx->strategy->isRunning()) {
      if (!ctx->strategy->stop()) {
        spdlog::error("[{}] Failed to stop strategy", symbol);
        allOk = false;
      }
    }

    if (ctx->simulator && ctx->simulator->isRunning()) {
      if (!ctx->simulator->stop()) {
        spdlog::error("[{}] Failed to stop simulator", symbol);
        allOk = false;
      }
    }

    // Mark stopped under lock
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      auto it = m_instruments.find(symbol);
      if (it != m_instruments.end()) {
        it->second->running = false;
        spdlog::info("[{}] Instrument stopped", symbol);
      }
    }
  }

  return allOk;
}

bool InstrumentManager::startInstrument(const std::string& symbol) {
  // Get components under lock
  std::shared_ptr<strategy::BasicMarketMaker> strategy;
  std::shared_ptr<exchange::ExchangeSimulator> simulator;
  std::shared_ptr<OrderBook> orderBook;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_instruments.find(symbol);
    if (it == m_instruments.end()) {
      return false;
    }
    if (it->second->running) {
      return true;
    }
    strategy = it->second->strategy;
    simulator = it->second->simulator;
    orderBook = it->second->orderBook;
  }

  if (!strategy) {
    spdlog::error("[{}] Strategy is null", symbol);
    return false;
  }

  // Blocking calls outside lock
  if (!strategy->initialize(orderBook)) {
    spdlog::error("[{}] Failed to initialize strategy", symbol);
    return false;
  }
  if (!strategy->start()) {
    spdlog::error("[{}] Failed to start strategy", symbol);
    return false;
  }
  if (simulator && !simulator->start()) {
    spdlog::error("[{}] Failed to start simulator", symbol);
    strategy->stop();
    return false;
  }

  // Mark running under lock
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_instruments.find(symbol);
    if (it != m_instruments.end()) {
      it->second->running = true;
    }
  }
  return true;
}

bool InstrumentManager::stopInstrument(const std::string& symbol) {
  // Get components under lock
  std::shared_ptr<strategy::BasicMarketMaker> strategy;
  std::shared_ptr<exchange::ExchangeSimulator> simulator;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_instruments.find(symbol);
    if (it == m_instruments.end()) {
      return false;
    }
    if (!it->second->running) {
      return true;
    }
    strategy = it->second->strategy;
    simulator = it->second->simulator;
  }

  // Blocking calls outside lock
  if (strategy && strategy->isRunning()) {
    strategy->stop();
  }
  if (simulator && simulator->isRunning()) {
    simulator->stop();
  }

  // Mark stopped under lock
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_instruments.find(symbol);
    if (it != m_instruments.end()) {
      it->second->running = false;
    }
  }
  return true;
}

std::shared_ptr<InstrumentContext>
InstrumentManager::getContext(const std::string& symbol) {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_instruments.find(symbol);
  if (it == m_instruments.end()) {
    return nullptr;
  }
  return it->second;
}

std::shared_ptr<const InstrumentContext>
InstrumentManager::getContext(const std::string& symbol) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_instruments.find(symbol);
  if (it == m_instruments.end()) {
    return nullptr;
  }
  return it->second;
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
  // Take a snapshot of contexts under lock, then format without lock
  std::vector<std::shared_ptr<InstrumentContext>> contexts;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    contexts.reserve(m_instruments.size());
    for (const auto& [symbol, ctx] : m_instruments) {
      contexts.push_back(ctx);
    }
  }

  std::ostringstream oss;
  double totalPnL = 0.0;
  double totalPosition = 0.0;
  size_t totalOrders = 0;

  for (const auto& ctx : contexts) {
    oss << "--- " << ctx->symbol << " ---\n";

    if (ctx->orderBook) {
      oss << "  Best bid: " << ctx->orderBook->getBestBidPrice() << "\n";
      oss << "  Best ask: " << ctx->orderBook->getBestAskPrice() << "\n";
      oss << "  Mid price: " << ctx->orderBook->getMidPrice() << "\n";
      oss << "  Spread: " << ctx->orderBook->getSpread() << "\n";
      oss << "  Order count: " << ctx->orderBook->getOrderCount() << "\n";
      totalOrders += ctx->orderBook->getOrderCount();
    }

    if (ctx->strategy) {
      oss << ctx->strategy->getStatistics() << "\n";
      totalPnL += ctx->strategy->getPnL();
      totalPosition += ctx->strategy->getPosition();
    }
  }

  oss << "--- AGGREGATE ---\n";
  oss << "  Instruments: " << contexts.size() << "\n";
  oss << "  Total PnL: " << totalPnL << "\n";
  oss << "  Total Position: " << totalPosition << "\n";
  oss << "  Total Orders: " << totalOrders << "\n";

  return oss.str();
}

void InstrumentManager::createCheckpoints() {
  std::lock_guard<std::mutex> lock(m_mutex);
  for (auto& [symbol, ctx] : m_instruments) {
    if (ctx->orderBook) {
      ctx->orderBook->createCheckpoint();
    }
  }
}

} // namespace instrument
} // namespace pinnacle
