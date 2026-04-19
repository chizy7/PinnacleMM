#include "BacktestEngine.h"
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <random>
#include <sstream>

namespace pinnacle::backtesting {

// HistoricalDataManager Implementation
HistoricalDataManager::HistoricalDataManager(const std::string& dataDirectory)
    : m_dataDirectory(dataDirectory), m_currentIndex(0) {}

bool HistoricalDataManager::loadData(const std::string& symbol,
                                     uint64_t startTime, uint64_t endTime) {
  std::lock_guard<std::mutex> lock(m_dataMutex);

  m_dataPoints.clear();
  m_currentIndex = 0;

  // Try to load from CSV first
  std::string csvFile = m_dataDirectory + "/" + symbol + ".csv";
  if (std::filesystem::exists(csvFile)) {
    spdlog::info("Loading historical data from CSV: {}", csvFile);
    if (loadFromCSV(csvFile)) {
      // Filter by time range
      auto it = std::remove_if(
          m_dataPoints.begin(), m_dataPoints.end(),
          [startTime, endTime](const MarketDataPoint& point) {
            return point.timestamp < startTime || point.timestamp > endTime;
          });
      m_dataPoints.erase(it, m_dataPoints.end());

      // Sort by timestamp
      std::sort(m_dataPoints.begin(), m_dataPoints.end(),
                [](const MarketDataPoint& a, const MarketDataPoint& b) {
                  return a.timestamp < b.timestamp;
                });

      spdlog::info("Loaded {} data points for symbol {}", m_dataPoints.size(),
                   symbol);
      return true;
    }
  }

  // Try binary format
  std::string binFile = m_dataDirectory + "/" + symbol + ".bin";
  if (std::filesystem::exists(binFile)) {
    spdlog::info("Loading historical data from binary: {}", binFile);
    return loadFromBinary(binFile);
  }

  // Generate synthetic data if no historical data available
  spdlog::warn("No historical data found for {}, generating synthetic data",
               symbol);

  // Generate realistic synthetic market data
  std::random_device rd;
  std::mt19937 gen(rd());
  std::normal_distribution<double> price_dist(0.0, 0.001); // 0.1% volatility
  std::normal_distribution<double> volume_dist(100.0, 20.0);

  double basePrice = 10000.0; // Starting price
  uint64_t currentTime = startTime;
  uint64_t timeStep = 1000000000ULL; // 1 second in nanoseconds

  while (currentTime <= endTime) {
    MarketDataPoint point;
    point.timestamp = currentTime;

    // Generate price with some drift and volatility
    basePrice *= (1.0 + price_dist(gen));
    point.price = basePrice;
    point.bid = basePrice - 0.1;
    point.ask = basePrice + 0.1;
    point.spread = point.ask - point.bid;
    point.volume = std::max(1.0, volume_dist(gen));

    m_dataPoints.push_back(point);
    currentTime += timeStep;
  }

  spdlog::info("Generated {} synthetic data points for symbol {}",
               m_dataPoints.size(), symbol);
  return true;
}

bool HistoricalDataManager::loadFromCSV(const std::string& filename) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    spdlog::error("Failed to open CSV file: {}", filename);
    return false;
  }

  std::string line;
  // Skip header if present
  if (std::getline(file, line) && line.find("timestamp") != std::string::npos) {
    // Header found, continue to data
  } else {
    // No header, parse first line as data
    auto point = parseCSVLine(line);
    if (point.timestamp > 0) {
      m_dataPoints.push_back(point);
    }
  }

  while (std::getline(file, line)) {
    auto point = parseCSVLine(line);
    if (point.timestamp > 0) {
      m_dataPoints.push_back(point);
    }
  }

  return !m_dataPoints.empty();
}

MarketDataPoint HistoricalDataManager::parseCSVLine(const std::string& line) {
  MarketDataPoint point;
  std::stringstream ss(line);
  std::string item;

  try {
    // Expected format: timestamp,symbol,price,bid,ask,volume
    if (std::getline(ss, item, ',')) {
      point.timestamp = std::stoull(item);
    }
    if (std::getline(ss, item, ',')) {
      // Skip symbol field as MarketDataPoint doesn't have it
    }
    if (std::getline(ss, item, ',')) {
      point.price = std::stod(item);
    }
    if (std::getline(ss, item, ',')) {
      point.bid = std::stod(item);
    }
    if (std::getline(ss, item, ',')) {
      point.ask = std::stod(item);
    }
    if (std::getline(ss, item, ',')) {
      point.volume = std::stod(item);
    }

    point.spread = point.ask - point.bid;
  } catch (const std::exception& e) {
    spdlog::warn("Failed to parse CSV line: {} - {}", line, e.what());
    point.timestamp = 0; // Mark as invalid
  }

  return point;
}

bool HistoricalDataManager::loadFromBinary(const std::string& filename) {
  std::ifstream file(filename, std::ios::binary);
  if (!file.is_open()) {
    spdlog::error("Failed to open binary file: {}", filename);
    return false;
  }

  // Read header
  uint64_t count;
  file.read(reinterpret_cast<char*>(&count), sizeof(count));

  m_dataPoints.reserve(count);

  // Read data points
  for (uint64_t i = 0; i < count; ++i) {
    MarketDataPoint point;
    file.read(reinterpret_cast<char*>(&point.timestamp),
              sizeof(point.timestamp));

    // Read symbol length and string (skip since MarketDataPoint doesn't have
    // symbol)
    uint32_t symbolLen;
    file.read(reinterpret_cast<char*>(&symbolLen), sizeof(symbolLen));
    file.seekg(symbolLen, std::ios::cur); // Skip symbol data

    file.read(reinterpret_cast<char*>(&point.price), sizeof(point.price));
    file.read(reinterpret_cast<char*>(&point.bid), sizeof(point.bid));
    file.read(reinterpret_cast<char*>(&point.ask), sizeof(point.ask));
    file.read(reinterpret_cast<char*>(&point.volume), sizeof(point.volume));

    point.spread = point.ask - point.bid;
    m_dataPoints.push_back(point);
  }

  return true;
}

bool HistoricalDataManager::hasMoreData() const {
  std::lock_guard<std::mutex> lock(m_dataMutex);
  return m_currentIndex < m_dataPoints.size();
}

MarketDataPoint HistoricalDataManager::getNextDataPoint() {
  std::lock_guard<std::mutex> lock(m_dataMutex);
  if (m_currentIndex < m_dataPoints.size()) {
    return m_dataPoints[m_currentIndex++];
  }
  return MarketDataPoint{}; // Return empty point if no more data
}

uint64_t HistoricalDataManager::getStartTime() const {
  std::lock_guard<std::mutex> lock(m_dataMutex);
  return m_dataPoints.empty() ? 0 : m_dataPoints.front().timestamp;
}

uint64_t HistoricalDataManager::getEndTime() const {
  std::lock_guard<std::mutex> lock(m_dataMutex);
  return m_dataPoints.empty() ? 0 : m_dataPoints.back().timestamp;
}

bool HistoricalDataManager::validateDataIntegrity() const {
  std::lock_guard<std::mutex> lock(m_dataMutex);

  if (m_dataPoints.empty())
    return false;

  // Check for time ordering
  for (size_t i = 1; i < m_dataPoints.size(); ++i) {
    if (m_dataPoints[i].timestamp <= m_dataPoints[i - 1].timestamp) {
      spdlog::warn("Data integrity issue: timestamp ordering at index {}", i);
      return false;
    }
  }

  // Check for reasonable price values
  for (const auto& point : m_dataPoints) {
    if (point.price <= 0 || point.bid <= 0 || point.ask <= 0 ||
        point.volume < 0) {
      spdlog::warn("Data integrity issue: invalid values");
      return false;
    }
    if (point.bid >= point.ask) {
      spdlog::warn("Data integrity issue: bid >= ask");
      return false;
    }
  }

  return true;
}

void HistoricalDataManager::printDataStatistics() const {
  std::lock_guard<std::mutex> lock(m_dataMutex);

  if (m_dataPoints.empty()) {
    spdlog::info("No data loaded");
    return;
  }

  // Calculate statistics
  double minPrice = std::numeric_limits<double>::max();
  double maxPrice = std::numeric_limits<double>::min();
  double totalVolume = 0.0;
  double totalSpread = 0.0;

  for (const auto& point : m_dataPoints) {
    minPrice = std::min(minPrice, point.price);
    maxPrice = std::max(maxPrice, point.price);
    totalVolume += point.volume;
    totalSpread += point.spread;
  }

  double avgPrice =
      std::accumulate(
          m_dataPoints.begin(), m_dataPoints.end(), 0.0,
          [](double sum, const MarketDataPoint& p) { return sum + p.price; }) /
      m_dataPoints.size();

  double avgVolume = totalVolume / m_dataPoints.size();
  double avgSpread = totalSpread / m_dataPoints.size();

  // Read timestamps directly - getStartTime()/getEndTime() would
  // re-enter m_dataMutex (non-recursive) and deadlock.
  uint64_t startTs = m_dataPoints.front().timestamp;
  uint64_t endTs = m_dataPoints.back().timestamp;

  spdlog::info("Data Statistics:");
  spdlog::info("  Data Points: {}", m_dataPoints.size());
  spdlog::info("  Time Range: {} to {}", startTs, endTs);
  spdlog::info("  Price Range: ${:.2f} - ${:.2f} (avg: ${:.2f})", minPrice,
               maxPrice, avgPrice);
  spdlog::info("  Average Volume: {:.2f}", avgVolume);
  spdlog::info("  Average Spread: ${:.4f}", avgSpread);
}

// PerformanceAnalyzer Implementation
void PerformanceAnalyzer::recordTrade(const BacktestTrade& trade) {
  std::lock_guard<std::mutex> lock(m_analysisMutex);
  m_trades.push_back(trade);
}

void PerformanceAnalyzer::recordMarketData(const MarketDataPoint& data) {
  std::lock_guard<std::mutex> lock(m_analysisMutex);
  m_marketData.push_back(data);
}

TradingStatistics PerformanceAnalyzer::calculateStatistics() const {
  std::lock_guard<std::mutex> lock(m_analysisMutex);

  TradingStatistics stats;

  if (m_trades.empty())
    return stats;

  // Basic metrics
  stats.totalTrades = m_trades.size();
  stats.totalPnL = std::accumulate(
      m_trades.begin(), m_trades.end(), 0.0,
      [](double sum, const BacktestTrade& trade) { return sum + trade.pnl; });
  stats.totalVolume =
      std::accumulate(m_trades.begin(), m_trades.end(), 0.0,
                      [](double sum, const BacktestTrade& trade) {
                        return sum + std::abs(trade.quantity);
                      });

  // Time metrics
  stats.startTime = m_trades.front().timestamp;
  stats.endTime = m_trades.back().timestamp;
  stats.duration = stats.endTime - stats.startTime;

  // Win/Loss analysis
  std::vector<double> profits, losses;
  for (const auto& trade : m_trades) {
    if (trade.pnl > 0) {
      profits.push_back(trade.pnl);
    } else if (trade.pnl < 0) {
      losses.push_back(std::abs(trade.pnl));
    }
  }

  stats.winRate = profits.empty()
                      ? 0.0
                      : static_cast<double>(profits.size()) / stats.totalTrades;
  stats.avgWin = profits.empty()
                     ? 0.0
                     : std::accumulate(profits.begin(), profits.end(), 0.0) /
                           profits.size();
  stats.avgLoss =
      losses.empty()
          ? 0.0
          : std::accumulate(losses.begin(), losses.end(), 0.0) / losses.size();
  stats.profitFactor =
      (losses.empty() || stats.avgLoss == 0.0)
          ? std::numeric_limits<double>::infinity()
          : (stats.avgWin * profits.size()) / (stats.avgLoss * losses.size());

  // Position metrics
  if (!m_trades.empty()) {
    auto minPos =
        std::min_element(m_trades.begin(), m_trades.end(),
                         [](const BacktestTrade& a, const BacktestTrade& b) {
                           return a.position < b.position;
                         });
    auto maxPos =
        std::max_element(m_trades.begin(), m_trades.end(),
                         [](const BacktestTrade& a, const BacktestTrade& b) {
                           return a.position < b.position;
                         });

    stats.minPosition = minPos->position;
    stats.maxPosition = maxPos->position;
    stats.avgPosition =
        std::accumulate(m_trades.begin(), m_trades.end(), 0.0,
                        [](double sum, const BacktestTrade& trade) {
                          return sum + trade.position;
                        }) /
        m_trades.size();
  }

  // Risk metrics
  stats.sharpeRatio = calculateSharpeRatio();
  stats.maxDrawdown = calculateMaxDrawdown();
  stats.valueAtRisk95 = calculateValueAtRisk(0.95);
  stats.valueAtRisk99 = calculateValueAtRisk(0.99);

  return stats;
}

double PerformanceAnalyzer::calculateSharpeRatio() const {
  if (m_trades.size() < 2)
    return 0.0;

  auto returns = calculateReturns();
  if (returns.empty())
    return 0.0;

  double meanReturn =
      std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();

  double variance = 0.0;
  for (double ret : returns) {
    variance += (ret - meanReturn) * (ret - meanReturn);
  }
  variance /= (returns.size() - 1);

  double stdDev = std::sqrt(variance);
  return (stdDev == 0.0) ? 0.0 : meanReturn / stdDev;
}

double PerformanceAnalyzer::calculateMaxDrawdown() const {
  if (m_trades.empty())
    return 0.0;

  double peak = m_trades[0].balance;
  double maxDrawdown = 0.0;

  for (const auto& trade : m_trades) {
    if (trade.balance > peak) {
      peak = trade.balance;
    } else {
      double drawdown = (peak - trade.balance) / peak;
      maxDrawdown = std::max(maxDrawdown, drawdown);
    }
  }

  return maxDrawdown;
}

double PerformanceAnalyzer::calculateValueAtRisk(double confidence) const {
  auto returns = calculateReturns();
  if (returns.empty())
    return 0.0;

  std::sort(returns.begin(), returns.end());
  size_t index = static_cast<size_t>((1.0 - confidence) * returns.size());
  index = std::min(index, returns.size() - 1);

  return -returns[index]; // VaR is typically expressed as a positive number
}

std::vector<double> PerformanceAnalyzer::calculateReturns() const {
  std::vector<double> returns;
  if (m_trades.size() < 2)
    return returns;

  returns.reserve(m_trades.size() - 1);
  for (size_t i = 1; i < m_trades.size(); ++i) {
    double prevBalance = m_trades[i - 1].balance;
    double currBalance = m_trades[i].balance;
    if (prevBalance > 0) {
      returns.push_back((currBalance - prevBalance) / prevBalance);
    }
  }

  return returns;
}

void PerformanceAnalyzer::reset() {
  std::lock_guard<std::mutex> lock(m_analysisMutex);
  m_trades.clear();
  m_marketData.clear();
  m_snapshots.clear();
}

// BacktestEngine Implementation
BacktestEngine::BacktestEngine(const BacktestConfiguration& config)
    : m_config(config), m_balance(config.initialBalance), m_position(0.0),
      m_unrealizedPnL(0.0), m_realizedPnL(0.0), m_currentTime(0) {

  m_dataManager = std::make_unique<HistoricalDataManager>(
      m_config.outputDirectory + "/data");
  m_analyzer = std::make_unique<PerformanceAnalyzer>();
}

void BacktestEngine::setStrategy(
    std::shared_ptr<pinnacle::strategy::MLEnhancedMarketMaker> strategy) {
  m_strategy = std::move(strategy);
}

void BacktestEngine::setJsonLogger(
    std::shared_ptr<pinnacle::utils::JsonLogger> jsonLogger) {
  m_jsonLogger = std::move(jsonLogger);
}

void BacktestEngine::emitFinalStrategyMetrics() {
  if (!m_jsonLogger || !m_jsonLogger->isEnabled()) {
    return;
  }

  const auto stats = m_analyzer->calculateStatistics();

  // nlohmann::json serializes NaN/Inf as `null` without warning, but the Go
  // runner rejects that — and profitFactor naturally goes to +Inf when there
  // are no losing trades. Replace non-finite values with null explicitly so
  // the JSON is at least well-defined.
  auto finite = [](double v) -> nlohmann::json {
    return std::isfinite(v) ? nlohmann::json(v) : nlohmann::json(nullptr);
  };

  nlohmann::json entry = {{"type", "strategy_metrics"},
                          {"sharpe_ratio", finite(stats.sharpeRatio)},
                          {"max_drawdown", finite(stats.maxDrawdown)},
                          {"win_rate", finite(stats.winRate)},
                          {"total_trades", stats.totalTrades},
                          {"total_pnl", finite(stats.totalPnL)},
                          {"total_volume", finite(stats.totalVolume)},
                          {"profit_factor", finite(stats.profitFactor)},
                          {"var_95", finite(stats.valueAtRisk95)},
                          {"var_99", finite(stats.valueAtRisk99)},
                          {"timestamp", m_currentTime}};
  m_jsonLogger->log(entry);
  m_jsonLogger->flush();
}

bool BacktestEngine::initialize() {
  spdlog::info("Initializing BacktestEngine");

  if (!setupOutputDirectory()) {
    spdlog::error("Failed to setup output directory");
    return false;
  }

  if (!setupDataManager()) {
    spdlog::error("Failed to setup data manager");
    return false;
  }

  spdlog::info("BacktestEngine initialized successfully");
  return true;
}

bool BacktestEngine::runBacktest(const std::string& symbol) {
  if (!m_strategy) {
    // Data-replay-only mode: produce zero-trade metrics. Useful for
    // validating data ingestion and for tests that don't need a strategy.
    spdlog::warn("Running backtest without a strategy (data replay only)");
  }
  return runBacktest(symbol, m_strategy);
}

bool BacktestEngine::runBacktest(
    const std::string& symbol,
    std::shared_ptr<pinnacle::strategy::MLEnhancedMarketMaker> strategy) {

  spdlog::info("Starting backtest for symbol: {}", symbol);

  m_isRunning.store(true);
  m_shouldStop.store(false);
  m_progress.store(0.0);
  m_backtestStartTime = std::chrono::steady_clock::now();

  // Load historical data
  if (!m_dataManager->loadData(symbol, m_config.startTimestamp,
                               m_config.endTimestamp)) {
    spdlog::error("Failed to load historical data for symbol: {}", symbol);
    m_isRunning.store(false);
    return false;
  }

  // Validate data integrity
  if (!m_dataManager->validateDataIntegrity()) {
    spdlog::warn("Data integrity validation failed, proceeding with caution");
  }

  m_dataManager->printDataStatistics();

  // Initialize strategy (if not already done)
  if (strategy) {
    spdlog::info("Using strategy for backtesting");
  }

  // Reset analyzer
  m_analyzer->reset();

  // Reset portfolio state
  m_balance = m_config.initialBalance;
  m_position = 0.0;
  m_unrealizedPnL = 0.0;
  m_realizedPnL = 0.0;
  m_avgCostBasis = 0.0;
  m_lastData = MarketDataPoint{};

  size_t totalDataPoints = m_dataManager->getDataPointCount();
  size_t processedPoints = 0;

  // Main backtesting loop
  while (m_dataManager->hasMoreData() && !m_shouldStop.load()) {
    auto dataPoint = m_dataManager->getNextDataPoint();
    if (dataPoint.timestamp == 0)
      break; // Invalid data point

    m_currentTime = dataPoint.timestamp;

    // Update the market-data snapshot the fill logic reads from. We do this
    // before processStrategyOrders so that any quotes carried over from the
    // previous tick are matched against the new bid/ask (i.e., they fill if
    // the market walked through them between ticks).
    m_lastData = dataPoint;
    m_analyzer->recordMarketData(dataPoint);

    // Match any previously-queued strategy orders against the new market.
    processStrategyOrders();

    // Feed the new tick to the strategy so it can regenerate quotes. Those
    // quotes become resting orders that the next iteration will try to fill.
    if (m_strategy) {
      m_strategy->updateMarketData(dataPoint);
    }

    // Update portfolio
    updatePortfolio(dataPoint);

    // Calculate performance periodically
    if (processedPoints % 1000 == 0) {
      calculatePerformance();
      if (m_config.saveIntermediateResults) {
        saveIntermediateResults();
      }
    }

    // Update progress
    processedPoints++;
    m_progress.store(static_cast<double>(processedPoints) / totalDataPoints);

    // Apply speed control
    if (m_config.speedMultiplier < 1.0) {
      auto sleepTime = std::chrono::microseconds(
          static_cast<int64_t>(1000.0 / m_config.speedMultiplier));
      std::this_thread::sleep_for(sleepTime);
    }
  }

  // Final performance calculation
  calculatePerformance();

  // Fast-path summary for the platform runner: one JSONL line with
  // pre-computed Sharpe / drawdown / win rate.
  emitFinalStrategyMetrics();

  m_isRunning.store(false);
  // Preserve partial progress if the run was interrupted via stop().
  if (!m_shouldStop.load()) {
    m_progress.store(1.0);
  }

  auto endTime = std::chrono::steady_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      endTime - m_backtestStartTime);

  spdlog::info("Backtest completed in {} ms", duration.count());
  spdlog::info("Processed {} data points", processedPoints);

  // Export final results
  if (!exportResults(m_config.outputDirectory + "/backtest_results.json")) {
    spdlog::warn("Failed to export backtest results");
  }

  return true;
}

void BacktestEngine::processMarketData(const MarketDataPoint& data) {
  // Kept for compatibility / external callers. The main backtest loop does
  // not call this — it inlines the equivalent steps so it can interleave
  // strategy updates with fill matching. Notably, this function does NOT
  // invoke processStrategyOrders, so calling it directly will record market
  // data and regenerate strategy quotes without attempting any fills.
  m_analyzer->recordMarketData(data);
  m_lastData = data;
  if (m_strategy) {
    m_strategy->updateMarketData(data);
  }
}

double BacktestEngine::applyFillToCostBasis(OrderSide side, double qty,
                                            double fillPrice) {
  const double signedQty = (side == OrderSide::BUY) ? qty : -qty;
  const double prev = m_position;
  const double next = prev + signedQty;
  double realized = 0.0;

  const bool sameSide = (prev == 0.0) || ((prev > 0) == (signedQty > 0));
  if (sameSide) {
    // Opening or increasing: recompute weighted-average cost basis.
    if (next != 0.0) {
      m_avgCostBasis = (prev * m_avgCostBasis + signedQty * fillPrice) / next;
    } else {
      m_avgCostBasis = 0.0;
    }
  } else {
    // Reducing (possibly flipping) position. P&L is realized on the portion
    // that closes existing exposure, valued against m_avgCostBasis.
    const double closeQty = std::min(std::abs(signedQty), std::abs(prev));
    if (prev > 0.0) {
      realized = (fillPrice - m_avgCostBasis) * closeQty;
    } else {
      realized = (m_avgCostBasis - fillPrice) * closeQty;
    }

    if (next == 0.0) {
      m_avgCostBasis = 0.0;
    } else if ((prev > 0.0) != (next > 0.0)) {
      // Flipped sides: remaining qty opens a fresh position at fillPrice.
      m_avgCostBasis = fillPrice;
    }
    // else: partial close, keep existing cost basis on the residual.
  }

  m_position = next;
  return realized;
}

void BacktestEngine::processStrategyOrders() {
  if (!m_strategy) {
    return;
  }

  auto pending = m_strategy->getPendingOrders();
  if (pending.empty()) {
    return;
  }

  const double currentBid = m_lastData.bid;
  const double currentAsk = m_lastData.ask;
  if (currentBid <= 0.0 || currentAsk <= 0.0 || currentAsk <= currentBid) {
    return;
  }

  for (const auto& order : pending) {
    if (!order) {
      continue;
    }

    const OrderSide side = order->getSide();
    const double limitPrice = order->getPrice();
    const double qty = order->getQuantity();
    if (qty <= 0.0) {
      continue;
    }

    // Marketable limit: buy fills if its limit >= best ask; sell fills
    // if its limit <= best bid. Non-marketable limits are dropped (a real
    // book would queue them, but in this synchronous replay we don't
    // simulate resting liquidity).
    double fillPrice = 0.0;
    bool filled = false;
    if (side == OrderSide::BUY && limitPrice >= currentAsk) {
      fillPrice = currentAsk;
      filled = true;
    } else if (side == OrderSide::SELL && limitPrice <= currentBid) {
      fillPrice = currentBid;
      filled = true;
    }

    if (!filled) {
      continue;
    }

    // Adverse slippage: push fill price against the taker. Clamp to a
    // positive minimum — pathological slippageBps on a low-priced asset
    // could otherwise drive sell-fills to zero or negative.
    if (m_config.enableSlippage && m_config.slippageBps > 0.0) {
      const double slip = fillPrice * m_config.slippageBps * 0.0001;
      fillPrice += (side == OrderSide::BUY) ? slip : -slip;
      if (fillPrice <= 0.0) {
        spdlog::warn("Slippage drove fill price non-positive; skipping order "
                     "{} (slippageBps={})",
                     order->getOrderId(), m_config.slippageBps);
        continue;
      }
    }

    const double notional = qty * fillPrice;
    const double fee = std::abs(notional) * m_config.tradingFee;

    // Enforce position limit: reject fills that would push |position| above
    // configured maxPosition. A real venue would reject these upstream; the
    // strategy's inventory skew should keep us away, but this is belt-and-
    // braces.
    const double signedQty = (side == OrderSide::BUY) ? qty : -qty;
    const double wouldBePosition = m_position + signedQty;
    if (m_config.maxPosition > 0.0 &&
        std::abs(wouldBePosition) > m_config.maxPosition) {
      continue;
    }

    // Buys can't overspend available balance (fee included). Sells always OK
    // here since we model long-or-short positions uniformly.
    if (side == OrderSide::BUY && m_balance < notional + fee) {
      continue;
    }

    double realized = 0.0;
    double tradePnL = 0.0;
    {
      // Mutate portfolio state under the same mutex createSnapshot reads
      // under, so external observers never see a torn update.
      std::lock_guard<std::mutex> stateLock(m_stateMutex);
      realized = applyFillToCostBasis(side, qty, fillPrice);
      tradePnL = realized - fee;

      // Cash: buying consumes balance (+fees), selling releases it (-fees).
      m_balance -= (side == OrderSide::BUY) ? notional : -notional;
      m_balance -= fee;
      m_realizedPnL += tradePnL;
    }

    BacktestTrade trade;
    trade.timestamp = m_currentTime;
    trade.orderId = order->getOrderId();
    trade.symbol = order->getSymbol();
    trade.side = side;
    trade.quantity = qty;
    trade.price = fillPrice;
    trade.fee = fee;
    trade.pnl = tradePnL;
    trade.position = m_position;
    trade.balance = m_balance;
    trade.strategy = "BasicMarketMaker";
    trade.regime = "";
    m_analyzer->recordTrade(trade);

    m_strategy->onBacktestFill(side, fillPrice, qty, m_currentTime);

    if (m_jsonLogger && m_jsonLogger->isEnabled()) {
      nlohmann::json entry = {{"type", "order_filled"},
                              {"side", side == OrderSide::BUY ? "buy" : "sell"},
                              {"price", fillPrice},
                              {"quantity", qty},
                              {"pnl", tradePnL},
                              {"timestamp", m_currentTime}};
      m_jsonLogger->log(entry);
    }
  }
}

void BacktestEngine::updatePortfolio(const MarketDataPoint& data) {
  // Update unrealized P&L based on current position and market price
  if (m_position != 0.0) {
    // Simplified P&L calculation
    // In practice, this would be more sophisticated
    double markToMarket = m_position * data.price;
    m_unrealizedPnL = markToMarket; // Simplified - would need cost basis
  }
}

void BacktestEngine::calculatePerformance() {
  // Create performance snapshot
  createSnapshot();

  // Add to analyzer
  // Note: PerformanceAnalyzer would need a method to add snapshots
  // m_analyzer->addSnapshot(snapshot);
}

PerformanceSnapshot BacktestEngine::createSnapshot() const {
  std::lock_guard<std::mutex> lock(m_stateMutex);

  PerformanceSnapshot snapshot;
  snapshot.timestamp = m_currentTime;
  snapshot.balance = m_balance;
  snapshot.position = m_position;
  snapshot.unrealizedPnL = m_unrealizedPnL;
  snapshot.realizedPnL = m_realizedPnL;
  snapshot.totalPnL = m_unrealizedPnL + m_realizedPnL;

  // Calculate drawdown (simplified)
  snapshot.drawdown = std::max(0.0, (m_config.initialBalance - m_balance) /
                                        m_config.initialBalance);

  // Get current statistics
  snapshot.statistics = m_analyzer->calculateStatistics();
  snapshot.sharpeRatio = snapshot.statistics.sharpeRatio;

  return snapshot;
}

bool BacktestEngine::setupOutputDirectory() {
  try {
    std::filesystem::create_directories(m_config.outputDirectory);
    std::filesystem::create_directories(m_config.outputDirectory + "/data");
    std::filesystem::create_directories(m_config.outputDirectory + "/results");
    return true;
  } catch (const std::exception& e) {
    spdlog::error("Failed to create output directories: {}", e.what());
    return false;
  }
}

bool BacktestEngine::setupDataManager() {
  // The data manager is already created in the constructor
  return true;
}

TradingStatistics BacktestEngine::getResults() const {
  return m_analyzer->calculateStatistics();
}

std::string BacktestEngine::getDetailedReport() const {
  auto stats = getResults();
  std::stringstream report;

  report << "=== BACKTEST RESULTS ===\n";
  report << "Total P&L: $" << std::fixed << std::setprecision(2)
         << stats.totalPnL << "\n";
  report << "Total Trades: " << stats.totalTrades << "\n";
  report << "Win Rate: " << std::fixed << std::setprecision(1)
         << (stats.winRate * 100.0) << "%\n";
  report << "Sharpe Ratio: " << std::fixed << std::setprecision(3)
         << stats.sharpeRatio << "\n";
  report << "Max Drawdown: " << std::fixed << std::setprecision(1)
         << (stats.maxDrawdown * 100.0) << "%\n";
  report << "Total Volume: " << std::fixed << std::setprecision(2)
         << stats.totalVolume << "\n";
  report << "Average Win: $" << std::fixed << std::setprecision(2)
         << stats.avgWin << "\n";
  report << "Average Loss: $" << std::fixed << std::setprecision(2)
         << stats.avgLoss << "\n";
  report << "Profit Factor: " << std::fixed << std::setprecision(2)
         << stats.profitFactor << "\n";
  report << "VaR (95%): $" << std::fixed << std::setprecision(2)
         << stats.valueAtRisk95 << "\n";
  report << "VaR (99%): $" << std::fixed << std::setprecision(2)
         << stats.valueAtRisk99 << "\n";

  return report.str();
}

bool BacktestEngine::exportResults(const std::string& filename) const {
  try {
    std::ofstream file(filename);
    if (!file.is_open()) {
      spdlog::error("Failed to open results file: {}", filename);
      return false;
    }

    auto stats = getResults();

    // Export as JSON format
    file << "{\n";
    file << "  \"backtest_config\": {\n";
    file << "    \"start_timestamp\": " << m_config.startTimestamp << ",\n";
    file << "    \"end_timestamp\": " << m_config.endTimestamp << ",\n";
    file << "    \"initial_balance\": " << m_config.initialBalance << ",\n";
    file << "    \"trading_fee\": " << m_config.tradingFee << "\n";
    file << "  },\n";
    file << "  \"results\": {\n";
    file << "    \"total_pnl\": " << stats.totalPnL << ",\n";
    file << "    \"total_trades\": " << stats.totalTrades << ",\n";
    file << "    \"win_rate\": " << stats.winRate << ",\n";
    file << "    \"sharpe_ratio\": " << stats.sharpeRatio << ",\n";
    file << "    \"max_drawdown\": " << stats.maxDrawdown << ",\n";
    file << "    \"total_volume\": " << stats.totalVolume << ",\n";
    file << "    \"profit_factor\": " << stats.profitFactor << ",\n";
    file << "    \"var_95\": " << stats.valueAtRisk95 << ",\n";
    file << "    \"var_99\": " << stats.valueAtRisk99 << "\n";
    file << "  }\n";
    file << "}\n";

    spdlog::info("Backtest results exported to: {}", filename);
    return true;
  } catch (const std::exception& e) {
    spdlog::error("Failed to export results: {}", e.what());
    return false;
  }
}

void BacktestEngine::saveIntermediateResults() {
  // Save intermediate results for monitoring progress
  std::string filename =
      m_config.outputDirectory + "/intermediate_results.json";
  exportResults(filename);
}

double BacktestEngine::getProgress() const { return m_progress.load(); }

void BacktestEngine::updateConfiguration(const BacktestConfiguration& config) {
  std::lock_guard<std::mutex> lock(m_stateMutex);
  m_config = config;
}

BacktestEngine::ComparisonResult BacktestEngine::compareStrategies(
    const TradingStatistics& statsA, const std::string& nameA,
    const TradingStatistics& statsB, const std::string& nameB) {

  ComparisonResult result;
  result.strategyA = nameA;
  result.strategyB = nameB;
  result.statsA = statsA;
  result.statsB = statsB;

  // Simple comparison based on Sharpe ratio
  // In practice, this would use statistical tests for significance
  result.significanceLevel = 0.05; // 5% significance level
  result.isSignificant =
      std::abs(statsA.sharpeRatio - statsB.sharpeRatio) > 0.1;

  if (statsA.sharpeRatio > statsB.sharpeRatio) {
    result.winner = nameA;
  } else if (statsB.sharpeRatio > statsA.sharpeRatio) {
    result.winner = nameB;
  } else {
    result.winner = "Tie";
  }

  return result;
}

// BacktestRunner Implementation
std::vector<BacktestRunner::BatchResult> BacktestRunner::runBatchBacktests(
    const std::vector<std::pair<std::string, BacktestConfiguration>>& configs,
    const std::string& symbol) {

  std::vector<BatchResult> results;
  results.reserve(configs.size());

  spdlog::info("Running batch backtests with {} configurations",
               configs.size());

  for (const auto& [configName, config] : configs) {
    spdlog::info("Running backtest: {}", configName);

    BatchResult result;
    result.configName = configName;
    result.config = config;
    result.successful = false;

    try {
      BacktestEngine engine(config);
      if (engine.initialize()) {
        if (engine.runBacktest(symbol)) {
          result.results = engine.getResults();
          result.successful = true;
          spdlog::info("Backtest {} completed successfully", configName);
        } else {
          result.error = "Backtest execution failed";
          spdlog::error("Backtest {} failed: {}", configName, result.error);
        }
      } else {
        result.error = "Engine initialization failed";
        spdlog::error("Backtest {} failed: {}", configName, result.error);
      }
    } catch (const std::exception& e) {
      result.error = e.what();
      spdlog::error("Backtest {} failed with exception: {}", configName,
                    result.error);
    }

    results.push_back(result);
  }

  // Log summary
  size_t successful =
      std::count_if(results.begin(), results.end(),
                    [](const BatchResult& r) { return r.successful; });

  spdlog::info("Batch backtests completed: {}/{} successful", successful,
               results.size());

  return results;
}

BacktestRunner::OptimizationResult BacktestRunner::optimizeParameters(
    const std::string& symbol, const BacktestConfiguration& baseConfig,
    const std::map<std::string, std::vector<double>>& parameterGrid) {

  spdlog::info("Starting parameter optimization");

  OptimizationResult optimizationResult;
  optimizationResult.bestConfig = baseConfig;

  // Generate all parameter combinations
  std::vector<std::pair<std::string, BacktestConfiguration>> configs;

  // For simplicity, I'll implement a basic grid search
  // In practice, this could use more sophisticated optimization algorithms

  if (parameterGrid.find("trading_fee") != parameterGrid.end()) {
    for (double fee : parameterGrid.at("trading_fee")) {
      BacktestConfiguration config = baseConfig;
      config.tradingFee = fee;
      configs.emplace_back("fee_" + std::to_string(fee), config);
    }
  }

  if (parameterGrid.find("max_position") != parameterGrid.end()) {
    for (double maxPos : parameterGrid.at("max_position")) {
      BacktestConfiguration config = baseConfig;
      config.maxPosition = maxPos;
      configs.emplace_back("maxpos_" + std::to_string(maxPos), config);
    }
  }

  if (parameterGrid.find("slippage_bps") != parameterGrid.end()) {
    for (double slippage : parameterGrid.at("slippage_bps")) {
      BacktestConfiguration config = baseConfig;
      config.slippageBps = slippage;
      configs.emplace_back("slippage_" + std::to_string(slippage), config);
    }
  }

  // If no specific parameters to optimize, create a few variations
  if (configs.empty()) {
    configs.emplace_back("base", baseConfig);

    BacktestConfiguration lowFeeConfig = baseConfig;
    lowFeeConfig.tradingFee *= 0.5;
    configs.emplace_back("low_fee", lowFeeConfig);

    BacktestConfiguration highFeeConfig = baseConfig;
    highFeeConfig.tradingFee *= 1.5;
    configs.emplace_back("high_fee", highFeeConfig);
  }

  // Run all configurations
  optimizationResult.allResults = runBatchBacktests(configs, symbol);

  // Find best configuration based on Sharpe ratio
  double bestSharpe = -std::numeric_limits<double>::infinity();
  for (const auto& result : optimizationResult.allResults) {
    if (result.successful && result.results.sharpeRatio > bestSharpe) {
      bestSharpe = result.results.sharpeRatio;
      optimizationResult.bestConfig = result.config;
      optimizationResult.bestResults = result.results;
    }
  }

  spdlog::info("Parameter optimization completed. Best Sharpe ratio: {:.3f}",
               bestSharpe);

  return optimizationResult;
}

BacktestEngine::ComparisonResult BacktestRunner::runABTest(
    const std::string& symbol, const BacktestConfiguration& configA,
    const std::string& nameA, const BacktestConfiguration& configB,
    const std::string& nameB) {

  spdlog::info("Running A/B test: {} vs {}", nameA, nameB);

  // Run both backtests
  std::vector<std::pair<std::string, BacktestConfiguration>> configs = {
      {nameA, configA}, {nameB, configB}};

  auto results = runBatchBacktests(configs, symbol);

  if (results.size() != 2 || !results[0].successful || !results[1].successful) {
    spdlog::error("A/B test failed - one or both backtests failed");
    // Return empty comparison result
    return BacktestEngine::ComparisonResult{};
  }

  // Compare results
  auto comparison = BacktestEngine::compareStrategies(
      results[0].results, nameA, results[1].results, nameB);

  spdlog::info("A/B test completed. Winner: {}", comparison.winner);

  return comparison;
}

BacktestRunner::MonteCarloResult
BacktestRunner::runMonteCarloAnalysis(const std::string& symbol,
                                      const BacktestConfiguration& config,
                                      int numSimulations) {

  spdlog::info("Running Monte Carlo analysis with {} simulations",
               numSimulations);

  MonteCarloResult mcResult;
  mcResult.simulations.reserve(numSimulations);

  std::vector<std::pair<std::string, BacktestConfiguration>> configs;
  std::random_device rd;
  std::mt19937 gen(rd());

  // Generate perturbed configurations
  for (int i = 0; i < numSimulations; ++i) {
    BacktestConfiguration perturbedConfig =
        perturbeConfiguration(config, 0.1); // 10% perturbation
    configs.emplace_back("mc_sim_" + std::to_string(i), perturbedConfig);
  }

  // Run all simulations
  auto results = runBatchBacktests(configs, symbol);

  // Collect successful results
  for (const auto& result : results) {
    if (result.successful) {
      mcResult.simulations.push_back(result.results);
    }
  }

  if (mcResult.simulations.empty()) {
    spdlog::error("Monte Carlo analysis failed - no successful simulations");
    return mcResult;
  }

  // Calculate statistics
  mcResult.meanResults = calculateStatistics(mcResult.simulations);

  // Calculate probability of profit
  size_t profitableRuns = std::count_if(
      mcResult.simulations.begin(), mcResult.simulations.end(),
      [](const TradingStatistics& stats) { return stats.totalPnL > 0; });

  mcResult.probabilityOfProfit =
      static_cast<double>(profitableRuns) / mcResult.simulations.size();

  // Calculate VaR from simulation results
  std::vector<double> pnlValues;
  pnlValues.reserve(mcResult.simulations.size());
  for (const auto& sim : mcResult.simulations) {
    pnlValues.push_back(sim.totalPnL);
  }

  std::sort(pnlValues.begin(), pnlValues.end());

  size_t var95Index = static_cast<size_t>(0.05 * pnlValues.size());
  size_t var99Index = static_cast<size_t>(0.01 * pnlValues.size());

  mcResult.valueAtRisk95 =
      var95Index < pnlValues.size() ? -pnlValues[var95Index] : 0.0;
  mcResult.valueAtRisk99 =
      var99Index < pnlValues.size() ? -pnlValues[var99Index] : 0.0;

  spdlog::info("Monte Carlo analysis completed. Probability of profit: {:.1f}%",
               mcResult.probabilityOfProfit * 100.0);

  return mcResult;
}

BacktestConfiguration
BacktestRunner::perturbeConfiguration(const BacktestConfiguration& base,
                                      double perturbationStrength) const {
  BacktestConfiguration perturbed = base;

  std::random_device rd;
  std::mt19937 gen(rd());
  std::normal_distribution<double> dist(1.0, perturbationStrength);

  // Perturb key parameters
  perturbed.tradingFee *= std::max(0.0001, dist(gen)); // Ensure positive
  perturbed.slippageBps *= std::max(0.1, dist(gen));   // Ensure positive
  perturbed.maxPosition *= std::max(1.0, dist(gen));   // Ensure positive

  return perturbed;
}

TradingStatistics BacktestRunner::calculateStatistics(
    const std::vector<TradingStatistics>& results) const {
  if (results.empty())
    return TradingStatistics{};

  TradingStatistics mean;

  // Calculate means
  mean.totalPnL = std::accumulate(results.begin(), results.end(), 0.0,
                                  [](double sum, const TradingStatistics& s) {
                                    return sum + s.totalPnL;
                                  }) /
                  results.size();

  mean.sharpeRatio =
      std::accumulate(results.begin(), results.end(), 0.0,
                      [](double sum, const TradingStatistics& s) {
                        return sum + s.sharpeRatio;
                      }) /
      results.size();

  mean.maxDrawdown =
      std::accumulate(results.begin(), results.end(), 0.0,
                      [](double sum, const TradingStatistics& s) {
                        return sum + s.maxDrawdown;
                      }) /
      results.size();

  mean.winRate = std::accumulate(results.begin(), results.end(), 0.0,
                                 [](double sum, const TradingStatistics& s) {
                                   return sum + s.winRate;
                                 }) /
                 results.size();

  mean.totalVolume =
      std::accumulate(results.begin(), results.end(), 0.0,
                      [](double sum, const TradingStatistics& s) {
                        return sum + s.totalVolume;
                      }) /
      results.size();

  mean.totalTrades =
      std::accumulate(results.begin(), results.end(), 0ULL,
                      [](uint64_t sum, const TradingStatistics& s) {
                        return sum + s.totalTrades;
                      }) /
      results.size();

  return mean;
}

} // namespace pinnacle::backtesting
