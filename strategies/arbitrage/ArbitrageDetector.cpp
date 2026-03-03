#include "ArbitrageDetector.h"

#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>
#include <sstream>

namespace pinnacle {
namespace arbitrage {

ArbitrageDetector::ArbitrageDetector(const ArbitrageConfig& config)
    : m_config(config) {}

ArbitrageDetector::~ArbitrageDetector() { stop(); }

bool ArbitrageDetector::start() {
  bool expected = false;
  if (!m_running.compare_exchange_strong(expected, true,
                                         std::memory_order_acq_rel,
                                         std::memory_order_acquire)) {
    return true; // Already running
  }

  try {
    m_scanThread = std::thread(&ArbitrageDetector::scanLoop, this);
  } catch (const std::exception& e) {
    m_running.store(false, std::memory_order_release);
    spdlog::error("ArbitrageDetector failed to start scan thread: {}",
                  e.what());
    return false;
  }

  spdlog::info("ArbitrageDetector started (scanInterval={}ms dryRun={})",
               m_config.scanIntervalMs, m_config.dryRun);
  return true;
}

bool ArbitrageDetector::stop() {
  bool expected = true;
  if (!m_running.compare_exchange_strong(expected, false,
                                         std::memory_order_acq_rel,
                                         std::memory_order_acquire)) {
    return true; // Already stopped
  }

  if (m_scanThread.joinable()) {
    m_scanThread.join();
  }
  spdlog::info("ArbitrageDetector stopped (total opportunities={})",
               m_totalOpportunities.load(std::memory_order_relaxed));
  return true;
}

bool ArbitrageDetector::isRunning() const {
  return m_running.load(std::memory_order_acquire);
}

void ArbitrageDetector::updateVenueQuote(const std::string& venue,
                                         const std::string& symbol, double bid,
                                         double bidSize, double ask,
                                         double askSize, uint64_t timestamp) {
  VenueQuote quote;
  quote.bidPrice = bid;
  quote.bidSize = bidSize;
  quote.askPrice = ask;
  quote.askSize = askSize;
  quote.timestamp = timestamp;

  std::lock_guard<std::mutex> lock(m_quotesMutex);
  m_quotes[venue][symbol] = quote;
}

std::vector<ArbitrageOpportunity>
ArbitrageDetector::getCurrentOpportunities() const {
  std::lock_guard<std::mutex> lock(m_opportunitiesMutex);
  return m_opportunities;
}

void ArbitrageDetector::setOpportunityCallback(OpportunityCallback callback) {
  std::lock_guard<std::mutex> lock(m_callbackMutex);
  m_callback = std::move(callback);
}

std::string ArbitrageDetector::getStatistics() const {
  std::ostringstream oss;
  oss << "ArbitrageDetector Statistics:\n";
  oss << "  Total scans: " << m_totalScans.load(std::memory_order_relaxed)
      << "\n";
  oss << "  Total opportunities: "
      << m_totalOpportunities.load(std::memory_order_relaxed) << "\n";
  oss << "  Dry run: " << (m_config.dryRun ? "yes" : "no") << "\n";
  oss << "  Min spread: " << m_config.minSpreadBps << " bps\n";

  auto currentOpps = getCurrentOpportunities();
  oss << "  Active opportunities: " << currentOpps.size() << "\n";
  for (const auto& opp : currentOpps) {
    oss << "    " << opp.symbol << ": buy@" << opp.buyVenue << " "
        << opp.buyPrice << " sell@" << opp.sellVenue << " " << opp.sellPrice
        << " spread=" << opp.spreadBps << "bps profit=$" << opp.estimatedProfit
        << "\n";
  }

  return oss.str();
}

uint64_t ArbitrageDetector::getTotalOpportunitiesDetected() const {
  return m_totalOpportunities.load(std::memory_order_relaxed);
}

void ArbitrageDetector::scanLoop() {
  while (m_running.load(std::memory_order_acquire)) {
    std::vector<ArbitrageOpportunity> allOpps;

    for (const auto& symbol : m_config.symbols) {
      auto opps = detectOpportunities(symbol);
      allOpps.insert(allOpps.end(), opps.begin(), opps.end());
    }

    m_totalScans.fetch_add(1, std::memory_order_relaxed);

    // Save a copy for callbacks before moving into shared state
    size_t newOppsCount = allOpps.size();
    std::vector<ArbitrageOpportunity> callbackOpps;
    if (newOppsCount > 0) {
      callbackOpps = allOpps;
    }

    // Update current opportunities
    {
      std::lock_guard<std::mutex> lock(m_opportunitiesMutex);
      m_opportunities = std::move(allOpps);
    }

    // Fire callbacks outside any lock to prevent deadlock
    // (callback may call getCurrentOpportunities() which locks
    // m_opportunitiesMutex)
    if (newOppsCount > 0) {
      m_totalOpportunities.fetch_add(newOppsCount, std::memory_order_relaxed);

      OpportunityCallback cb;
      {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        cb = m_callback;
      }

      if (cb) {
        for (const auto& opp : callbackOpps) {
          cb(opp);
        }
      }
    }

    std::this_thread::sleep_for(
        std::chrono::milliseconds(m_config.scanIntervalMs));
  }
}

std::vector<ArbitrageOpportunity>
ArbitrageDetector::detectOpportunities(const std::string& symbol) const {
  std::vector<ArbitrageOpportunity> opportunities;

  // Collect non-stale quotes for this symbol across venues under the lock
  struct VenueData {
    std::string venue;
    VenueQuote quote;
    double fee;
  };

  std::vector<VenueData> venueData;
  {
    std::lock_guard<std::mutex> lock(m_quotesMutex);
    for (const auto& venue : m_config.venues) {
      auto venueIt = m_quotes.find(venue);
      if (venueIt == m_quotes.end()) {
        continue;
      }

      auto symbolIt = venueIt->second.find(symbol);
      if (symbolIt == venueIt->second.end()) {
        continue;
      }

      const auto& quote = symbolIt->second;
      if (isStale(quote)) {
        continue;
      }

      if (quote.bidPrice <= 0 || quote.askPrice <= 0) {
        continue;
      }

      venueData.push_back({venue, quote, getVenueFee(venue)});
    }
  } // lock released — all data is in local venueData

  // Compare all venue pairs for arbitrage (no lock held)
  uint64_t now = utils::TimeUtils::getCurrentNanos();

  for (size_t i = 0; i < venueData.size(); ++i) {
    for (size_t j = 0; j < venueData.size(); ++j) {
      if (i == j) {
        continue;
      }

      const auto& buyer = venueData[i];  // Buy at this venue's ask
      const auto& seller = venueData[j]; // Sell at this venue's bid

      double buyPrice = buyer.quote.askPrice;
      double sellPrice = seller.quote.bidPrice;

      // Gross spread
      double spread = sellPrice - buyPrice;
      if (spread <= 0) {
        continue;
      }

      // Deduct fees
      double totalFees = (buyPrice * buyer.fee) + (sellPrice * seller.fee);
      double netSpread = spread - totalFees;

      if (netSpread <= 0) {
        continue;
      }

      double midPrice = (buyPrice + sellPrice) / 2.0;
      double spreadBps = (netSpread / midPrice) * 10000.0;

      if (spreadBps < m_config.minSpreadBps) {
        continue;
      }

      double maxQty = std::min(buyer.quote.askSize, seller.quote.bidSize);
      double estimatedProfit = netSpread * maxQty;

      if (estimatedProfit < m_config.minProfitUsd) {
        continue;
      }

      ArbitrageOpportunity opp;
      opp.symbol = symbol;
      opp.buyVenue = buyer.venue;
      opp.sellVenue = seller.venue;
      opp.buyPrice = buyPrice;
      opp.sellPrice = sellPrice;
      opp.spread = netSpread;
      opp.spreadBps = spreadBps;
      opp.maxQuantity = maxQty;
      opp.estimatedProfit = estimatedProfit;
      opp.detectedAt = now;

      opportunities.push_back(opp);
    }
  }

  return opportunities;
}

bool ArbitrageDetector::isStale(const VenueQuote& quote) const {
  uint64_t now = utils::TimeUtils::getCurrentNanos();
  return (now - quote.timestamp) > m_config.maxStalenessNs;
}

double ArbitrageDetector::getVenueFee(const std::string& venue) const {
  auto it = m_config.venueFees.find(venue);
  if (it != m_config.venueFees.end()) {
    return it->second;
  }
  return 0.0;
}

} // namespace arbitrage
} // namespace pinnacle
