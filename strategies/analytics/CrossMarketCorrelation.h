#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace pinnacle {
namespace analytics {

/**
 * @struct CorrelationPair
 * @brief Statistics for a pair of correlated instruments
 */
struct CorrelationPair {
  std::string symbolA;
  std::string symbolB;
  double pearsonCorrelation{0.0}; // [-1, 1]
  double rollingCorrelation{0.0}; // recent-window correlation
  double leadLagCoefficient{0.0}; // strength of lead-lag relationship
  int leadLagBarsA{0};            // positive = A leads B
  double cointegrationScore{
      0.0}; // Engle-Granger t-stat (more negative = stronger)
  bool isCointegrated{false};
};

/**
 * @struct CrossMarketSignal
 * @brief Trading signal from cross-market analysis
 */
struct CrossMarketSignal {
  std::string leadSymbol;
  std::string lagSymbol;
  double signalStrength{0.0}; // [0, 1]
  double expectedMove{0.0};   // expected % move in lag symbol
  double confidence{0.0};     // [0, 1]
  uint64_t timestamp{0};
};

/**
 * @struct CrossMarketConfig
 * @brief Configuration for cross-market correlation analysis
 */
struct CrossMarketConfig {
  size_t returnWindowSize{100};     // window for return calculations
  size_t rollingWindowSize{30};     // rolling correlation window
  int maxLagBars{10};               // max lead-lag offset to test
  double minCorrelation{0.5};       // min absolute correlation to consider
  double signalThreshold{0.3};      // min signal strength to emit
  double cointegrationPValue{0.05}; // significance level for cointegration
};

/**
 * @class CrossMarketCorrelation
 * @brief Statistical models to detect when one instrument's movement predicts
 * another's
 *
 * Implements Pearson correlation, rolling correlation, lead-lag analysis,
 * and Engle-Granger cointegration test for pairs of instruments.
 */
class CrossMarketCorrelation {
public:
  explicit CrossMarketCorrelation(const CrossMarketConfig& config = {});
  ~CrossMarketCorrelation() = default;

  CrossMarketCorrelation(const CrossMarketCorrelation&) = delete;
  CrossMarketCorrelation& operator=(const CrossMarketCorrelation&) = delete;

  /**
   * @brief Add a price observation for a symbol
   * @param symbol Trading symbol
   * @param price Current price
   * @param volume Current volume
   * @param timestamp Nanosecond timestamp
   */
  void addPriceObservation(const std::string& symbol, double price,
                           double volume, uint64_t timestamp);

  /**
   * @brief Register a pair for correlation tracking
   * @param symbolA First symbol
   * @param symbolB Second symbol
   */
  void addPair(const std::string& symbolA, const std::string& symbolB);

  /**
   * @brief Remove a pair
   */
  void removePair(const std::string& symbolA, const std::string& symbolB);

  /**
   * @brief Get correlation statistics for a pair
   */
  CorrelationPair getCorrelation(const std::string& symbolA,
                                 const std::string& symbolB) const;

  /**
   * @brief Get all currently active cross-market signals
   */
  std::vector<CrossMarketSignal> getActiveSignals() const;

  /**
   * @brief Get all registered pairs with their correlations
   */
  std::vector<CorrelationPair> getAllCorrelations() const;

  /**
   * @brief Get statistics string
   */
  std::string getStatistics() const;

private:
  CrossMarketConfig m_config;

  // Per-symbol price series
  struct PriceSeries {
    std::deque<double> prices;
    std::deque<double> returns; // log returns
    std::deque<double> volumes;
    std::deque<uint64_t> timestamps;
  };

  mutable std::mutex m_dataMutex;
  std::unordered_map<std::string, PriceSeries> m_series;

  // Registered pairs
  struct PairKey {
    std::string symbolA;
    std::string symbolB;
    bool operator==(const PairKey& other) const {
      return symbolA == other.symbolA && symbolB == other.symbolB;
    }
  };

  struct PairKeyHash {
    size_t operator()(const PairKey& k) const {
      return std::hash<std::string>{}(k.symbolA) ^
             (std::hash<std::string>{}(k.symbolB) << 1);
    }
  };

  std::unordered_map<PairKey, CorrelationPair, PairKeyHash> m_pairs;

  // Cached signals
  mutable std::vector<CrossMarketSignal> m_signals;
  mutable bool m_signalsDirty{true};

  // Statistical computation methods
  double computePearsonCorrelation(const std::deque<double>& x,
                                   const std::deque<double>& y) const;

  double computeRollingCorrelation(const std::deque<double>& x,
                                   const std::deque<double>& y) const;

  struct LeadLagResult {
    double coefficient{0.0};
    int bestLag{0};
  };
  LeadLagResult computeLeadLag(const std::deque<double>& x,
                               const std::deque<double>& y) const;

  double computeCointegration(const std::deque<double>& pricesA,
                              const std::deque<double>& pricesB) const;

  void updatePair(const PairKey& key);
  void updateSignals() const;
};

} // namespace analytics
} // namespace pinnacle
