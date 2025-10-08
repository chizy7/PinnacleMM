#pragma once

#include "../../core/orderbook/Order.h"
#include "../../core/utils/TimeUtils.h"
#include "../../strategies/analytics/MarketRegimeDetector.h"
#include "../../strategies/basic/MLEnhancedMarketMaker.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace pinnacle::backtesting {

// Import MarketDataPoint from analytics namespace
using pinnacle::analytics::MarketDataPoint;

/**
 * @struct BacktestConfiguration
 * @brief Configuration for backtesting parameters
 */
struct BacktestConfiguration {
  // Time range
  uint64_t startTimestamp;
  uint64_t endTimestamp;

  // Simulation parameters
  double initialBalance = 100000.0;
  double tradingFee = 0.001; // 0.1% fee

  // Risk management
  double maxPosition = 1000.0;
  double maxDrawdown = 0.2; // 20% max drawdown

  // Data replay settings
  double speedMultiplier = 1.0; // 1.0 = real-time, >1.0 = faster
  bool enableSlippage = true;
  double slippageBps = 2.0; // 2 basis points

  // Strategy settings
  bool enableML = true;
  bool enableRL = true;
  bool enableRegimeDetection = true;

  // Performance calculation
  uint64_t performanceUpdateIntervalMs = 1000;

  // Output settings
  bool saveIntermediateResults = true;
  std::string outputDirectory = "backtest_results";
};

/**
 * @struct TradingStatistics
 * @brief Comprehensive trading performance statistics
 */
struct TradingStatistics {
  // Basic metrics
  double totalPnL = 0.0;
  double totalVolume = 0.0;
  uint64_t totalTrades = 0;

  // Performance metrics
  double sharpeRatio = 0.0;
  double maxDrawdown = 0.0;
  double winRate = 0.0;
  double avgWin = 0.0;
  double avgLoss = 0.0;
  double profitFactor = 0.0;

  // Risk metrics
  double valueAtRisk95 = 0.0; // 95% VaR
  double valueAtRisk99 = 0.0; // 99% VaR
  double expectedShortfall = 0.0;
  double beta = 0.0;
  double alpha = 0.0;

  // Trading metrics
  double avgSpread = 0.0;
  double fillRate = 0.0;
  double marketImpact = 0.0;
  double avgHoldingTime = 0.0;

  // Time-based metrics
  uint64_t startTime = 0;
  uint64_t endTime = 0;
  uint64_t duration = 0;

  // Position metrics
  double maxPosition = 0.0;
  double minPosition = 0.0;
  double avgPosition = 0.0;

  // ML/Strategy metrics
  double mlAccuracy = 0.0;
  double regimeDetectionAccuracy = 0.0;
  uint64_t rlEpisodes = 0;
  double adaptationRate = 0.0;
};

/**
 * @struct BacktestTrade
 * @brief Record of a single trade execution
 */
struct BacktestTrade {
  uint64_t timestamp;
  std::string orderId;
  std::string symbol;
  OrderSide side;
  double quantity;
  double price;
  double fee;
  double pnl;
  double position;
  double balance;
  std::string strategy;
  std::string regime;
};

/**
 * @struct PerformanceSnapshot
 * @brief Point-in-time performance snapshot
 */
struct PerformanceSnapshot {
  uint64_t timestamp;
  double balance;
  double position;
  double unrealizedPnL;
  double realizedPnL;
  double totalPnL;
  double drawdown;
  double sharpeRatio;
  TradingStatistics statistics;
};

/**
 * @class HistoricalDataManager
 * @brief Manages historical market data for backtesting
 */
class HistoricalDataManager {
public:
  explicit HistoricalDataManager(const std::string& dataDirectory);
  ~HistoricalDataManager() = default;

  // Data loading
  bool loadData(const std::string& symbol, uint64_t startTime,
                uint64_t endTime);
  bool hasMoreData() const;
  MarketDataPoint getNextDataPoint();

  // Data statistics
  size_t getDataPointCount() const { return m_dataPoints.size(); }
  uint64_t getStartTime() const;
  uint64_t getEndTime() const;

  // Data validation
  bool validateDataIntegrity() const;
  void printDataStatistics() const;

private:
  std::string m_dataDirectory;
  std::vector<MarketDataPoint> m_dataPoints;
  size_t m_currentIndex = 0;
  mutable std::mutex m_dataMutex;

  // Data loading helpers
  bool loadFromCSV(const std::string& filename);
  bool loadFromBinary(const std::string& filename);
  MarketDataPoint parseCSVLine(const std::string& line);
};

/**
 * @class PerformanceAnalyzer
 * @brief Analyzes and calculates performance metrics
 */
class PerformanceAnalyzer {
public:
  PerformanceAnalyzer() = default;
  ~PerformanceAnalyzer() = default;

  // Trade recording
  void recordTrade(const BacktestTrade& trade);
  void recordMarketData(const MarketDataPoint& data);

  // Performance calculation
  TradingStatistics calculateStatistics() const;
  std::vector<PerformanceSnapshot> getPerformanceHistory() const;

  // Risk metrics
  double calculateSharpeRatio() const;
  double calculateMaxDrawdown() const;
  double calculateValueAtRisk(double confidence) const;
  double calculateBeta(const std::vector<double>& marketReturns) const;

  // Trading metrics
  double calculateWinRate() const;
  double calculateProfitFactor() const;
  double calculateMarketImpact() const;

  // Utilities
  void reset();
  void exportResults(const std::string& filename) const;

private:
  std::vector<BacktestTrade> m_trades;
  std::vector<MarketDataPoint> m_marketData;
  std::vector<PerformanceSnapshot> m_snapshots;
  mutable std::mutex m_analysisMutex;

  // Helper methods
  std::vector<double> calculateReturns() const;
  std::vector<double> calculateDrawdowns() const;
  double calculateVolatility() const;
};

/**
 * @class BacktestEngine
 * @brief Main backtesting engine for strategy validation
 */
class BacktestEngine {
public:
  explicit BacktestEngine(const BacktestConfiguration& config);
  ~BacktestEngine() = default;

  // Main backtesting interface
  bool initialize();
  bool runBacktest(const std::string& symbol);
  bool runBacktest(
      const std::string& symbol,
      std::shared_ptr<pinnacle::strategy::MLEnhancedMarketMaker> strategy);

  // Strategy management
  void setStrategy(
      std::shared_ptr<pinnacle::strategy::MLEnhancedMarketMaker> strategy);

  // Results access
  TradingStatistics getResults() const;
  std::vector<BacktestTrade> getTrades() const;
  std::vector<PerformanceSnapshot> getPerformanceHistory() const;

  // Configuration
  void updateConfiguration(const BacktestConfiguration& config);
  BacktestConfiguration getConfiguration() const { return m_config; }

  // Progress monitoring
  double getProgress() const;
  bool isRunning() const { return m_isRunning.load(); }
  void stop() { m_shouldStop.store(true); }

  // Utilities
  std::string getDetailedReport() const;
  bool exportResults(const std::string& filename) const;

  // Comparison and A/B testing
  struct ComparisonResult {
    std::string strategyA;
    std::string strategyB;
    TradingStatistics statsA;
    TradingStatistics statsB;
    double significanceLevel;
    bool isSignificant;
    std::string winner;
  };

  static ComparisonResult compareStrategies(const TradingStatistics& statsA,
                                            const std::string& nameA,
                                            const TradingStatistics& statsB,
                                            const std::string& nameB);

private:
  BacktestConfiguration m_config;
  std::shared_ptr<pinnacle::strategy::MLEnhancedMarketMaker> m_strategy;
  std::unique_ptr<HistoricalDataManager> m_dataManager;
  std::unique_ptr<PerformanceAnalyzer> m_analyzer;

  // Execution state
  std::atomic<bool> m_isRunning{false};
  std::atomic<bool> m_shouldStop{false};
  std::atomic<double> m_progress{0.0};

  // Portfolio state
  double m_balance;
  double m_position;
  double m_unrealizedPnL;
  double m_realizedPnL;

  // Time management
  uint64_t m_currentTime;
  std::chrono::steady_clock::time_point m_backtestStartTime;

  // Thread safety
  mutable std::mutex m_stateMutex;

  // Core backtesting logic
  void processMarketData(const MarketDataPoint& data);
  void processStrategyOrders();
  void updatePortfolio(const MarketDataPoint& data);
  void calculatePerformance();

  // Order execution simulation
  bool executeOrder(const Order& order, const MarketDataPoint& marketData,
                    BacktestTrade& trade);
  double calculateSlippage(const Order& order,
                           const MarketDataPoint& marketData) const;
  double calculateFee(double quantity, double price) const;

  // Risk management
  bool checkRiskLimits(const Order& order) const;
  void applyRiskConstraints();

  // Reporting
  void saveIntermediateResults();
  PerformanceSnapshot createSnapshot() const;

  // Initialization helpers
  bool setupDataManager();
  bool setupStrategy();
  bool setupOutputDirectory();
};

/**
 * @class BacktestRunner
 * @brief Utility class for running multiple backtests and comparisons
 */
class BacktestRunner {
public:
  BacktestRunner() = default;
  ~BacktestRunner() = default;

  // Batch testing
  struct BatchResult {
    std::string configName;
    BacktestConfiguration config;
    TradingStatistics results;
    bool successful;
    std::string error;
  };

  std::vector<BatchResult> runBatchBacktests(
      const std::vector<std::pair<std::string, BacktestConfiguration>>& configs,
      const std::string& symbol);

  // Parameter optimization
  struct OptimizationResult {
    BacktestConfiguration bestConfig;
    TradingStatistics bestResults;
    std::vector<BatchResult> allResults;
  };

  OptimizationResult optimizeParameters(
      const std::string& symbol, const BacktestConfiguration& baseConfig,
      const std::map<std::string, std::vector<double>>& parameterGrid);

  // A/B testing
  BacktestEngine::ComparisonResult
  runABTest(const std::string& symbol, const BacktestConfiguration& configA,
            const std::string& nameA, const BacktestConfiguration& configB,
            const std::string& nameB);

  // Monte Carlo analysis
  struct MonteCarloResult {
    std::vector<TradingStatistics> simulations;
    TradingStatistics meanResults;
    TradingStatistics stdResults;
    double probabilityOfProfit;
    double valueAtRisk95;
    double valueAtRisk99;
  };

  MonteCarloResult runMonteCarloAnalysis(const std::string& symbol,
                                         const BacktestConfiguration& config,
                                         int numSimulations);

private:
  // Helper methods
  BacktestConfiguration
  perturbeConfiguration(const BacktestConfiguration& base,
                        double perturbationStrength) const;
  TradingStatistics
  calculateStatistics(const std::vector<TradingStatistics>& results) const;
};

} // namespace pinnacle::backtesting
