#pragma once

#include "../../core/orderbook/Order.h"
#include "../../core/utils/TimeUtils.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace pinnacle {
namespace arbitrage {

/**
 * @struct ArbitrageOpportunity
 * @brief Represents a detected cross-exchange arbitrage opportunity
 */
struct ArbitrageOpportunity {
  std::string symbol;
  std::string buyVenue;
  std::string sellVenue;
  double buyPrice{0.0};
  double sellPrice{0.0};
  double spread{0.0};
  double spreadBps{0.0};
  double maxQuantity{0.0};
  double estimatedProfit{0.0};
  uint64_t detectedAt{0};
};

/**
 * @struct ArbitrageConfig
 * @brief Configuration for the arbitrage detector
 */
struct ArbitrageConfig {
  double minSpreadBps{5.0};
  double minProfitUsd{1.0};
  uint64_t maxStalenessNs{500000000}; // 500ms
  uint64_t scanIntervalMs{10};
  bool dryRun{true};
  std::vector<std::string> symbols;
  std::vector<std::string> venues;
  std::unordered_map<std::string, double> venueFees; // venue -> fee ratio
};

/**
 * @struct VenueQuote
 * @brief Quote from a specific venue
 */
struct VenueQuote {
  double bidPrice{0.0};
  double bidSize{0.0};
  double askPrice{0.0};
  double askSize{0.0};
  uint64_t timestamp{0};
};

/**
 * @class ArbitrageDetector
 * @brief Scans multi-venue quotes for profitable cross-exchange spreads
 *
 * Maintains a per-venue quote cache and runs a background scan thread
 * to detect arbitrage opportunities based on configurable thresholds.
 */
class ArbitrageDetector {
public:
  using OpportunityCallback = std::function<void(const ArbitrageOpportunity&)>;

  explicit ArbitrageDetector(const ArbitrageConfig& config);
  ~ArbitrageDetector();

  ArbitrageDetector(const ArbitrageDetector&) = delete;
  ArbitrageDetector& operator=(const ArbitrageDetector&) = delete;

  /**
   * @brief Start the background scan thread
   * @return true if started successfully
   */
  bool start();

  /**
   * @brief Stop the background scan thread
   * @return true if stopped successfully
   */
  bool stop();

  /**
   * @brief Check if the detector is running
   */
  bool isRunning() const;

  /**
   * @brief Update the quote for a specific venue and symbol
   */
  void updateVenueQuote(const std::string& venue, const std::string& symbol,
                        double bid, double bidSize, double ask, double askSize,
                        uint64_t timestamp);

  /**
   * @brief Get all current opportunities (thread-safe snapshot)
   */
  std::vector<ArbitrageOpportunity> getCurrentOpportunities() const;

  /**
   * @brief Register a callback for new opportunities
   */
  void setOpportunityCallback(OpportunityCallback callback);

  /**
   * @brief Get statistics string
   */
  std::string getStatistics() const;

  /**
   * @brief Get total number of opportunities detected
   */
  uint64_t getTotalOpportunitiesDetected() const;

private:
  ArbitrageConfig m_config;

  // Venue quotes: venue -> symbol -> quote
  using QuoteMap =
      std::unordered_map<std::string,
                         std::unordered_map<std::string, VenueQuote>>;
  QuoteMap m_quotes;
  mutable std::mutex m_quotesMutex;

  // Current opportunities
  std::vector<ArbitrageOpportunity> m_opportunities;
  mutable std::mutex m_opportunitiesMutex;

  // Callback
  OpportunityCallback m_callback;
  std::mutex m_callbackMutex;

  // Scan thread
  std::thread m_scanThread;
  std::atomic<bool> m_running{false};

  // Statistics
  std::atomic<uint64_t> m_totalOpportunities{0};
  std::atomic<uint64_t> m_totalScans{0};

  /**
   * @brief Background scan loop
   */
  void scanLoop();

  /**
   * @brief Detect opportunities for a specific symbol
   */
  std::vector<ArbitrageOpportunity>
  detectOpportunities(const std::string& symbol) const;

  /**
   * @brief Check if a quote is stale
   */
  bool isStale(const VenueQuote& quote) const;

  /**
   * @brief Get the fee for a venue (0.0 if not configured)
   */
  double getVenueFee(const std::string& venue) const;
};

} // namespace arbitrage
} // namespace pinnacle
