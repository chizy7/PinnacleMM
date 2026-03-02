#include "CrossMarketCorrelation.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>

namespace pinnacle {
namespace analytics {

CrossMarketCorrelation::CrossMarketCorrelation(const CrossMarketConfig& config)
    : m_config(config) {}

void CrossMarketCorrelation::addPriceObservation(const std::string& symbol,
                                                 double price, double volume,
                                                 uint64_t timestamp) {
  std::lock_guard<std::mutex> lock(m_dataMutex);

  auto& series = m_series[symbol];
  series.prices.push_back(price);
  series.volumes.push_back(volume);
  series.timestamps.push_back(timestamp);

  // Compute log return (guard against non-positive prices)
  if (series.prices.size() >= 2) {
    size_t n = series.prices.size();
    if (series.prices[n - 2] > 0.0 && series.prices[n - 1] > 0.0) {
      double ret = std::log(series.prices[n - 1] / series.prices[n - 2]);
      series.returns.push_back(ret);
    }
  }

  // Trim to window size (keep 2x for lead-lag analysis)
  size_t maxSize = m_config.returnWindowSize * 2;
  while (series.prices.size() > maxSize) {
    series.prices.pop_front();
    series.volumes.pop_front();
    series.timestamps.pop_front();
  }
  while (series.returns.size() > maxSize) {
    series.returns.pop_front();
  }

  // Update all pairs involving this symbol
  m_signalsDirty = true;
  for (auto& [key, pair] : m_pairs) {
    if (key.symbolA == symbol || key.symbolB == symbol) {
      updatePair(key);
    }
  }
}

void CrossMarketCorrelation::addPair(const std::string& symbolA,
                                     const std::string& symbolB) {
  std::lock_guard<std::mutex> lock(m_dataMutex);

  PairKey key{symbolA, symbolB};
  if (m_pairs.count(key)) {
    return;
  }

  CorrelationPair pair;
  pair.symbolA = symbolA;
  pair.symbolB = symbolB;
  m_pairs[key] = pair;
  m_signalsDirty = true;
}

void CrossMarketCorrelation::removePair(const std::string& symbolA,
                                        const std::string& symbolB) {
  std::lock_guard<std::mutex> lock(m_dataMutex);
  m_pairs.erase(PairKey{symbolA, symbolB});
  m_signalsDirty = true;
}

CorrelationPair
CrossMarketCorrelation::getCorrelation(const std::string& symbolA,
                                       const std::string& symbolB) const {
  std::lock_guard<std::mutex> lock(m_dataMutex);
  auto it = m_pairs.find(PairKey{symbolA, symbolB});
  if (it != m_pairs.end()) {
    return it->second;
  }
  return CorrelationPair{symbolA, symbolB};
}

std::vector<CrossMarketSignal>
CrossMarketCorrelation::getActiveSignals() const {
  std::lock_guard<std::mutex> lock(m_dataMutex);
  if (m_signalsDirty) {
    updateSignals();
  }
  return m_signals;
}

std::vector<CorrelationPair>
CrossMarketCorrelation::getAllCorrelations() const {
  std::lock_guard<std::mutex> lock(m_dataMutex);
  std::vector<CorrelationPair> result;
  result.reserve(m_pairs.size());
  for (const auto& [key, pair] : m_pairs) {
    result.push_back(pair);
  }
  return result;
}

std::string CrossMarketCorrelation::getStatistics() const {
  std::lock_guard<std::mutex> lock(m_dataMutex);

  std::ostringstream oss;
  oss << "CrossMarketCorrelation Statistics:\n";
  oss << "  Tracked symbols: " << m_series.size() << "\n";
  oss << "  Registered pairs: " << m_pairs.size() << "\n";

  for (const auto& [key, pair] : m_pairs) {
    oss << "  " << pair.symbolA << "/" << pair.symbolB << ":\n";
    oss << "    Pearson: " << pair.pearsonCorrelation << "\n";
    oss << "    Rolling: " << pair.rollingCorrelation << "\n";
    oss << "    Lead-lag: " << pair.leadLagBarsA << " bars"
        << " (coeff=" << pair.leadLagCoefficient << ")\n";
    oss << "    Cointegration: " << pair.cointegrationScore
        << (pair.isCointegrated ? " [cointegrated]" : "") << "\n";
  }

  return oss.str();
}

// --- Statistical computation methods ---

double CrossMarketCorrelation::computePearsonCorrelation(
    const std::deque<double>& x, const std::deque<double>& y) const {
  size_t n = std::min(x.size(), y.size());
  if (n < 3) {
    return 0.0;
  }

  // Use last n elements
  double sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0, sumY2 = 0;

  for (size_t i = 0; i < n; ++i) {
    size_t xi = x.size() - n + i;
    size_t yi = y.size() - n + i;
    sumX += x[xi];
    sumY += y[yi];
    sumXY += x[xi] * y[yi];
    sumX2 += x[xi] * x[xi];
    sumY2 += y[yi] * y[yi];
  }

  double denom =
      std::sqrt((n * sumX2 - sumX * sumX) * (n * sumY2 - sumY * sumY));
  if (denom < 1e-15) {
    return 0.0;
  }

  return (n * sumXY - sumX * sumY) / denom;
}

double CrossMarketCorrelation::computeRollingCorrelation(
    const std::deque<double>& x, const std::deque<double>& y) const {
  size_t window = m_config.rollingWindowSize;
  size_t n = std::min({x.size(), y.size(), window});
  if (n < 3) {
    return 0.0;
  }

  // Use the last 'n' elements
  std::deque<double> xWindow(x.end() - n, x.end());
  std::deque<double> yWindow(y.end() - n, y.end());

  return computePearsonCorrelation(xWindow, yWindow);
}

CrossMarketCorrelation::LeadLagResult
CrossMarketCorrelation::computeLeadLag(const std::deque<double>& x,
                                       const std::deque<double>& y) const {
  LeadLagResult result;
  double bestCorr = 0.0;
  int maxLag = m_config.maxLagBars;

  size_t n = std::min(x.size(), y.size());
  if (n < static_cast<size_t>(maxLag + 3)) {
    return result;
  }

  for (int lag = -maxLag; lag <= maxLag; ++lag) {
    // Compute correlation with offset
    size_t effectiveN = n - std::abs(lag);
    if (effectiveN < 3) {
      continue;
    }

    double sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0, sumY2 = 0;

    for (size_t i = 0; i < effectiveN; ++i) {
      size_t xi = (lag >= 0) ? (x.size() - effectiveN + i)
                             : (x.size() - effectiveN + i - lag);
      size_t yi = (lag >= 0) ? (y.size() - effectiveN + i + lag)
                             : (y.size() - effectiveN + i);

      if (xi >= x.size() || yi >= y.size()) {
        continue;
      }

      sumX += x[xi];
      sumY += y[yi];
      sumXY += x[xi] * y[yi];
      sumX2 += x[xi] * x[xi];
      sumY2 += y[yi] * y[yi];
    }

    double denom = std::sqrt((effectiveN * sumX2 - sumX * sumX) *
                             (effectiveN * sumY2 - sumY * sumY));
    if (denom < 1e-15) {
      continue;
    }

    double corr = (effectiveN * sumXY - sumX * sumY) / denom;

    if (std::abs(corr) > std::abs(bestCorr)) {
      bestCorr = corr;
      result.bestLag = lag;
      result.coefficient = corr;
    }
  }

  return result;
}

double CrossMarketCorrelation::computeCointegration(
    const std::deque<double>& pricesA,
    const std::deque<double>& pricesB) const {
  // Simplified Engle-Granger: OLS regression of A on B, then ADF on residuals
  size_t n = std::min(pricesA.size(), pricesB.size());
  if (n < 20) {
    return 0.0;
  }

  // Step 1: OLS regression Y = alpha + beta * X
  double sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0;

  for (size_t i = 0; i < n; ++i) {
    size_t ai = pricesA.size() - n + i;
    size_t bi = pricesB.size() - n + i;
    sumX += pricesB[bi];
    sumY += pricesA[ai];
    sumXY += pricesA[ai] * pricesB[bi];
    sumX2 += pricesB[bi] * pricesB[bi];
  }

  double denom = n * sumX2 - sumX * sumX;
  if (std::abs(denom) < 1e-15) {
    return 0.0;
  }

  double beta = (n * sumXY - sumX * sumY) / denom;
  double alpha = (sumY - beta * sumX) / n;

  // Step 2: Compute residuals
  std::vector<double> residuals(n);
  for (size_t i = 0; i < n; ++i) {
    size_t ai = pricesA.size() - n + i;
    size_t bi = pricesB.size() - n + i;
    residuals[i] = pricesA[ai] - alpha - beta * pricesB[bi];
  }

  // Step 3: Simplified ADF test on residuals
  // delta_e(t) = gamma * e(t-1) + noise
  // t-stat for gamma
  double sumE1 = 0, sumDE = 0, sumE1DE = 0, sumE12 = 0;
  for (size_t i = 1; i < n; ++i) {
    double e1 = residuals[i - 1];
    double de = residuals[i] - residuals[i - 1];
    sumE1 += e1;
    sumDE += de;
    sumE12 += e1 * e1;
    sumE1DE += e1 * de;
  }

  size_t m = n - 1;
  double gammaDenom = m * sumE12 - sumE1 * sumE1;
  if (std::abs(gammaDenom) < 1e-15) {
    return 0.0;
  }

  double gamma = (m * sumE1DE - sumE1 * sumDE) / gammaDenom;

  // Compute standard error of gamma
  double gammaAlpha = (sumDE - gamma * sumE1) / m;
  double sse = 0.0;
  for (size_t i = 1; i < n; ++i) {
    double predicted = gammaAlpha + gamma * residuals[i - 1];
    double actual = residuals[i] - residuals[i - 1];
    double err = actual - predicted;
    sse += err * err;
  }
  double se = std::sqrt(sse / (m - 2));
  double varianceTerm = sumE12 - sumE1 * sumE1 / m;
  if (varianceTerm < 1e-15) {
    return 0.0;
  }
  double seGamma = se / std::sqrt(varianceTerm);

  if (seGamma < 1e-15) {
    return 0.0;
  }

  // t-statistic (more negative = stronger cointegration)
  return gamma / seGamma;
}

void CrossMarketCorrelation::updatePair(const PairKey& key) {
  // Must be called with m_dataMutex held

  auto itA = m_series.find(key.symbolA);
  auto itB = m_series.find(key.symbolB);

  if (itA == m_series.end() || itB == m_series.end()) {
    return;
  }

  auto& seriesA = itA->second;
  auto& seriesB = itB->second;

  auto pairIt = m_pairs.find(key);
  if (pairIt == m_pairs.end()) {
    return;
  }

  auto& pair = pairIt->second;

  // Pearson correlation on returns
  if (seriesA.returns.size() >= 10 && seriesB.returns.size() >= 10) {
    pair.pearsonCorrelation =
        computePearsonCorrelation(seriesA.returns, seriesB.returns);
    pair.rollingCorrelation =
        computeRollingCorrelation(seriesA.returns, seriesB.returns);

    auto leadLag = computeLeadLag(seriesA.returns, seriesB.returns);
    pair.leadLagCoefficient = leadLag.coefficient;
    pair.leadLagBarsA = leadLag.bestLag;
  }

  // Cointegration on prices
  if (seriesA.prices.size() >= 20 && seriesB.prices.size() >= 20) {
    pair.cointegrationScore =
        computeCointegration(seriesA.prices, seriesB.prices);

    // Approximate critical values for Engle-Granger (2 variables)
    // At 5% significance: ~ -3.37
    pair.isCointegrated = pair.cointegrationScore < -3.37;
  }
}

void CrossMarketCorrelation::updateSignals() const {
  // Must be called with m_dataMutex held

  m_signals.clear();

  for (const auto& [key, pair] : m_pairs) {
    // Only generate signals for pairs with sufficient lead-lag relationship
    if (std::abs(pair.leadLagCoefficient) < m_config.minCorrelation) {
      continue;
    }

    if (pair.leadLagBarsA == 0) {
      continue; // No lead-lag detected
    }

    // Determine which symbol leads
    std::string leadSym, lagSym;
    if (pair.leadLagBarsA > 0) {
      leadSym = pair.symbolA;
      lagSym = pair.symbolB;
    } else {
      leadSym = pair.symbolB;
      lagSym = pair.symbolA;
    }

    // Get the leader's recent return
    auto leadIt = m_series.find(leadSym);
    if (leadIt == m_series.end() || leadIt->second.returns.empty()) {
      continue;
    }

    double leaderReturn = leadIt->second.returns.back();
    double signalStrength =
        std::abs(pair.leadLagCoefficient) * std::abs(leaderReturn);

    if (signalStrength < m_config.signalThreshold) {
      continue;
    }

    CrossMarketSignal signal;
    signal.leadSymbol = leadSym;
    signal.lagSymbol = lagSym;
    signal.signalStrength = std::min(1.0, signalStrength);
    signal.expectedMove = leaderReturn * pair.leadLagCoefficient;
    signal.confidence = std::abs(pair.rollingCorrelation);

    if (!leadIt->second.timestamps.empty()) {
      signal.timestamp = leadIt->second.timestamps.back();
    }

    m_signals.push_back(signal);
  }

  m_signalsDirty = false;
}

} // namespace analytics
} // namespace pinnacle
