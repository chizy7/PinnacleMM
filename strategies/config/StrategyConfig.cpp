#include "StrategyConfig.h"

#include <fstream>
#include <sstream>
#include <iomanip>

namespace pinnacle {
namespace strategy {

bool StrategyConfig::validate() const {
    // Ensure spread parameters are valid
    if (baseSpreadBps <= 0 || minSpreadBps <= 0 || maxSpreadBps <= 0) {
        return false;
    }
    
    // Ensure order parameters are valid
    if (orderQuantity <= 0 || minOrderQuantity <= 0 || maxOrderQuantity <= 0) {
        return false;
    }
    
    // Ensure timing parameters are valid
    if (quoteUpdateIntervalMs == 0 || orderTimeoutMs == 0) {
        return false;
    }
    
    // Ensure position parameters are valid
    if (maxPosition <= 0) {
        return false;
    }
    
    // Ensure risk parameters are valid
    if (maxDrawdownPct <= 0 || stopLossPct <= 0 || takeProfitPct <= 0) {
        return false;
    }
    
    // Ensure order book parameters are valid
    if (volumeDepthFactor < 0 || volumeDepthFactor > 1 ||
        imbalanceThreshold < 0 || imbalanceThreshold > 1 ||
        volumeWeightFactor < 0 || volumeWeightFactor > 1) {
        return false;
    }
    
    // Ensure timing parameters are valid
    if (quoteUpdateIntervalMs == 0 || orderTimeoutMs == 0 || 
        cancelRetryIntervalMs == 0 || tradeMonitoringIntervalMs == 0) {
        return false;
    }
    
    // Ensure position management parameters are valid
    if (hedgeThresholdPct <= 0 || hedgeThresholdPct > 100 || hedgeIntervalMs == 0) {
        return false;
    }
    
    // Ensure market stress parameters are valid
    if (volatilityThreshold <= 0 || spreadWidenFactor <= 1 || marketStressCheckMs == 0) {
        return false;
    }
    
    return true;
}

bool StrategyConfig::loadFromFile(const std::string& filename) {
    // Note: For Phase 1, we're providing a stub implementation
    // In a real implementation, this would parse a JSON file using a library like nlohmann/json
    
    // For now, just simulate loading by returning success
    // This will be properly implemented in Phase 2
    return true;
}

bool StrategyConfig::saveToFile(const std::string& filename) const {
    // Note: For Phase 1, we're providing a stub implementation
    // In a real implementation, this would serialize to JSON using a library like nlohmann/json
    
    // For now, just simulate saving by returning success
    // This will be properly implemented in Phase 2
    return true;
}

std::string StrategyConfig::toString() const {
    std::ostringstream oss;
    
    oss << "Strategy Configuration:" << std::endl;
    oss << "  Strategy Name: " << strategyName << std::endl;
    oss << "  Symbol: " << symbol << std::endl;
    oss << std::endl;
    
    oss << "Quote Parameters:" << std::endl;
    oss << "  Base Spread (bps): " << baseSpreadBps << std::endl;
    oss << "  Min Spread (bps): " << minSpreadBps << std::endl;
    oss << "  Max Spread (bps): " << maxSpreadBps << std::endl;
    oss << "  Order Quantity: " << orderQuantity << std::endl;
    oss << "  Min Order Quantity: " << minOrderQuantity << std::endl;
    oss << "  Max Order Quantity: " << maxOrderQuantity << std::endl;
    oss << std::endl;
    
    oss << "Market Making Parameters:" << std::endl;
    oss << "  Target Position: " << targetPosition << std::endl;
    oss << "  Max Position: " << maxPosition << std::endl;
    oss << "  Inventory Skew Factor: " << inventorySkewFactor << std::endl;
    oss << "  Price Level Spacing: " << priceLevelSpacing << std::endl;
    oss << "  Max Levels: " << maxLevels << std::endl;
    oss << std::endl;
    
    oss << "Risk Parameters:" << std::endl;
    oss << "  Max Drawdown (%): " << maxDrawdownPct << std::endl;
    oss << "  Stop Loss (%): " << stopLossPct << std::endl;
    oss << "  Take Profit (%): " << takeProfitPct << std::endl;
    oss << "  Max Trading Volume: " << maxTradingVolume << std::endl;
    oss << std::endl;
    
    oss << "Timing Parameters:" << std::endl;
    oss << "  Quote Update Interval (ms): " << quoteUpdateIntervalMs << std::endl;
    oss << "  Order Timeout (ms): " << orderTimeoutMs << std::endl;
    oss << "  Cancel Retry Interval (ms): " << cancelRetryIntervalMs << std::endl;
    oss << "  Trade Monitoring Interval (ms): " << tradeMonitoringIntervalMs << std::endl;
    oss << std::endl;
    
    oss << "Position Management:" << std::endl;
    oss << "  Auto-Hedge Enabled: " << (autoHedgeEnabled ? "Yes" : "No") << std::endl;
    oss << "  Hedge Threshold (%): " << hedgeThresholdPct << std::endl;
    oss << "  Hedge Interval (ms): " << hedgeIntervalMs << std::endl;
    oss << std::endl;
    
    oss << "Market Stress Detection:" << std::endl;
    oss << "  Volatility Threshold: " << volatilityThreshold << std::endl;
    oss << "  Spread Widen Factor: " << spreadWidenFactor << std::endl;
    oss << "  Market Stress Check Interval (ms): " << marketStressCheckMs << std::endl;
    oss << std::endl;
    
    oss << "Performance Optimization:" << std::endl;
    oss << "  Low Latency Mode: " << (useLowLatencyMode ? "Enabled" : "Disabled") << std::endl;
    oss << "  Publish Stats Interval (ms): " << publishStatsIntervalMs << std::endl;
    
    return oss.str();
}

} // namespace strategy
} // namespace pinnacle