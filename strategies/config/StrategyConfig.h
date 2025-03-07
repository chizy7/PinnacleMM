#pragma once

#include <string>
#include <cstdint>

namespace pinnacle {
namespace strategy {

/**
 * @struct StrategyConfig
 * @brief Configuration parameters for market making strategies
 */
struct StrategyConfig {
    // General strategy parameters
    std::string strategyName = "BasicMarketMaker";
    std::string symbol = "BTC-USD";
    
    // Quote parameters
    double baseSpreadBps = 10.0;        // Base spread in basis points
    double minSpreadBps = 5.0;          // Minimum spread in basis points
    double maxSpreadBps = 50.0;         // Maximum spread in basis points
    double orderQuantity = 0.01;        // Base order quantity
    double minOrderQuantity = 0.001;    // Minimum order quantity
    double maxOrderQuantity = 1.0;      // Maximum order quantity
    
    // Market making parameters
    double targetPosition = 0.0;        // Target position (0 = neutral)
    double maxPosition = 10.0;          // Maximum absolute position
    double inventorySkewFactor = 0.5;   // How much to skew quotes based on inventory (0-1)
    double priceLevelSpacing = 0.1;     // Spacing between price levels as percentage of base spread
    size_t maxLevels = 3;               // Maximum number of price levels to quote
    
    // Order book parameters
    double volumeDepthFactor = 0.5;     // How much to consider order book depth (0-1)
    double imbalanceThreshold = 0.3;    // Threshold for order book imbalance (0-1)
    double volumeWeightFactor = 0.5;    // Weight for volume-based price adjustment (0-1)
    
    // Risk parameters
    double maxDrawdownPct = 5.0;        // Maximum drawdown percentage before stopping
    double stopLossPct = 3.0;           // Stop loss percentage for individual position
    double takeProfitPct = 5.0;         // Take profit percentage for individual position
    double maxTradingVolume = 100.0;    // Maximum daily trading volume
    
    // Timing parameters
    uint64_t quoteUpdateIntervalMs = 100;  // Quote update interval in milliseconds
    uint64_t orderTimeoutMs = 5000;        // Order timeout in milliseconds
    uint64_t cancelRetryIntervalMs = 100;  // Cancel retry interval in milliseconds
    uint64_t tradeMonitoringIntervalMs = 50; // Trade monitoring interval in milliseconds
    
    // Position management
    bool autoHedgeEnabled = false;     // Enable auto-hedging
    double hedgeThresholdPct = 50.0;   // Threshold for auto-hedging (% of max position)
    uint64_t hedgeIntervalMs = 5000;   // Hedging interval in milliseconds
    
    // Market stress detection
    double volatilityThreshold = 0.5;  // Volatility threshold for market stress
    double spreadWidenFactor = 2.0;    // Factor to widen spread during market stress
    uint64_t marketStressCheckMs = 1000; // Market stress check interval in milliseconds
    
    // Performance optimization
    bool useLowLatencyMode = true;     // Enable low latency optimizations
    uint64_t publishStatsIntervalMs = 5000; // Statistics publishing interval
    
    // Constructor with default values
    StrategyConfig() = default;
    
    // Validate the configuration parameters
    bool validate() const;
    
    // Load from JSON file
    bool loadFromFile(const std::string& filename);
    
    // Save to JSON file
    bool saveToFile(const std::string& filename) const;
    
    // Create a string representation
    std::string toString() const;
};

} // namespace strategy
} // namespace pinnacle