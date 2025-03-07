#pragma once

#include "MarketDataFeed.h"
#include "../../core/orderbook/OrderBook.h"

#include <memory>
#include <string>
#include <atomic>
#include <thread>
#include <random>
#include <vector>
#include <unordered_map>
#include <mutex>

namespace pinnacle {
namespace exchange {

/**
 * @class ExchangeSimulator
 * @brief Simulates an exchange for testing strategies
 */
class ExchangeSimulator {
public:
    /**
     * @brief Constructor
     * 
     * @param orderBook Reference to the order book
     */
    explicit ExchangeSimulator(std::shared_ptr<OrderBook> orderBook);
    
    /**
     * @brief Destructor
     */
    ~ExchangeSimulator();
    
    /**
     * @brief Start the simulator
     * 
     * @return true if the simulator was started successfully, false otherwise
     */
    bool start();
    
    /**
     * @brief Stop the simulator
     * 
     * @return true if the simulator was stopped successfully, false otherwise
     */
    bool stop();
    
    /**
     * @brief Check if the simulator is running
     * 
     * @return true if the simulator is running, false otherwise
     */
    bool isRunning() const;
    
    /**
     * @brief Set the market data feed
     * 
     * @param marketDataFeed Reference to the market data feed
     */
    void setMarketDataFeed(std::shared_ptr<MarketDataFeed> marketDataFeed);
    
    /**
     * @brief Set the volatility of the simulated market
     * 
     * @param volatility Volatility value (0.0-1.0, where 1.0 is high volatility)
     */
    void setVolatility(double volatility);
    
    /**
     * @brief Set the drift of the simulated market
     * 
     * @param drift Drift value (-1.0 to 1.0, where positive values indicate upward drift)
     */
    void setDrift(double drift);
    
    /**
     * @brief Set the tick size for price movements
     * 
     * @param tickSize Minimum price movement
     */
    void setTickSize(double tickSize);
    
    /**
     * @brief Add a simulated market participant
     * 
     * @param type Participant type ("taker", "maker", "arbitrageur", "noise")
     * @param frequency Activity frequency (trades per minute)
     * @param volumeRatio Ratio of market volume (0.0-1.0)
     */
    void addMarketParticipant(const std::string& type, double frequency, double volumeRatio);
    
private:
    // Core components
    std::shared_ptr<OrderBook> m_orderBook;
    std::shared_ptr<MarketDataFeed> m_marketDataFeed;
    
    // Simulator state
    std::atomic<bool> m_isRunning{false};
    std::atomic<bool> m_shouldStop{false};
    
    // Simulator threads
    std::thread m_mainThread;
    std::thread m_marketDataThread;
    std::thread m_participantThread;
    
    // Market parameters
    double m_volatility{0.2};
    double m_drift{0.0};
    double m_tickSize{0.01};
    double m_lastPrice{10000.0}; // For BTC-USD
    
    // Random number generation
    std::mt19937 m_rng;
    std::normal_distribution<double> m_priceDistribution;
    
    // Market participants
    struct MarketParticipant {
        std::string type;
        double frequency;
        double volumeRatio;
        std::exponential_distribution<double> activityDistribution;
        uint64_t nextActivityTime;
    };
    
    std::vector<MarketParticipant> m_participants;
    std::mutex m_participantsMutex;
    
    // Internal implementation
    void mainLoop();
    void marketDataLoop();
    void participantLoop();
    
    void simulateMarketOrder(OrderSide side, double quantity);
    void simulateLimitOrder(OrderSide side, double price, double quantity);
    void simulateOrderCancellation();
    void updateMarketPrice();
    
    double generateRandomPrice();
    double roundToTickSize(double price) const;
    
    // Generate random order ID
    std::string generateOrderId();
};

} // namespace exchange
} // namespace pinnacle