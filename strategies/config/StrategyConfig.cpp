#include "StrategyConfig.h"

#include <fstream>
#include <sstream>
#include <iomanip>

namespace pinnacle {
namespace strategy {

bool StrategyConfig::validate(std::string& errorReason) const {
    // Validate spread parameters
    if (baseSpreadBps <= 0) {
        errorReason = "baseSpreadBps must be greater than 0, got " + std::to_string(baseSpreadBps);
        return false;
    }
    if (minSpreadBps <= 0) {
        errorReason = "minSpreadBps must be greater than 0, got " + std::to_string(minSpreadBps);
        return false;
    }
    if (maxSpreadBps <= 0) {
        errorReason = "maxSpreadBps must be greater than 0, got " + std::to_string(maxSpreadBps);
        return false;
    }
    if (minSpreadBps > baseSpreadBps) {
        errorReason = "minSpreadBps (" + std::to_string(minSpreadBps) + ") must be <= baseSpreadBps (" + std::to_string(baseSpreadBps) + ")";
        return false;
    }
    if (baseSpreadBps > maxSpreadBps) {
        errorReason = "baseSpreadBps (" + std::to_string(baseSpreadBps) + ") must be <= maxSpreadBps (" + std::to_string(maxSpreadBps) + ")";
        return false;
    }
    
    // Validate order quantity parameters
    if (orderQuantity <= 0) {
        errorReason = "orderQuantity must be greater than 0, got " + std::to_string(orderQuantity);
        return false;
    }
    if (minOrderQuantity <= 0) {
        errorReason = "minOrderQuantity must be greater than 0, got " + std::to_string(minOrderQuantity);
        return false;
    }
    if (maxOrderQuantity <= 0) {
        errorReason = "maxOrderQuantity must be greater than 0, got " + std::to_string(maxOrderQuantity);
        return false;
    }
    if (minOrderQuantity > orderQuantity) {
        errorReason = "minOrderQuantity (" + std::to_string(minOrderQuantity) + ") must be <= orderQuantity (" + std::to_string(orderQuantity) + ")";
        return false;
    }
    if (orderQuantity > maxOrderQuantity) {
        errorReason = "orderQuantity (" + std::to_string(orderQuantity) + ") must be <= maxOrderQuantity (" + std::to_string(maxOrderQuantity) + ")";
        return false;
    }
    
    // Validate position parameters
    if (maxPosition <= 0) {
        errorReason = "maxPosition must be greater than 0, got " + std::to_string(maxPosition);
        return false;
    }
    
    // Validate risk parameters
    if (maxDrawdownPct <= 0) {
        errorReason = "maxDrawdownPct must be greater than 0, got " + std::to_string(maxDrawdownPct);
        return false;
    }
    if (stopLossPct <= 0) {
        errorReason = "stopLossPct must be greater than 0, got " + std::to_string(stopLossPct);
        return false;
    }
    if (takeProfitPct <= 0) {
        errorReason = "takeProfitPct must be greater than 0, got " + std::to_string(takeProfitPct);
        return false;
    }
    
    // Validate timing parameters
    if (quoteUpdateIntervalMs == 0) {
        errorReason = "quoteUpdateIntervalMs must be greater than 0";
        return false;
    }
    if (orderTimeoutMs == 0) {
        errorReason = "orderTimeoutMs must be greater than 0";
        return false;
    }
    if (cancelRetryIntervalMs == 0) {
        errorReason = "cancelRetryIntervalMs must be greater than 0";
        return false;
    }
    if (tradeMonitoringIntervalMs == 0) {
        errorReason = "tradeMonitoringIntervalMs must be greater than 0";
        return false;
    }
    
    // Validate position management parameters
    if (hedgeThresholdPct <= 0) {
        errorReason = "hedgeThresholdPct must be greater than 0, got " + std::to_string(hedgeThresholdPct);
        return false;
    }
    if (hedgeThresholdPct > 100) {
        errorReason = "hedgeThresholdPct must be <= 100, got " + std::to_string(hedgeThresholdPct);
        return false;
    }
    if (hedgeIntervalMs == 0) {
        errorReason = "hedgeIntervalMs must be greater than 0";
        return false;
    }
    
    // Validate market stress parameters
    if (volatilityThreshold <= 0) {
        errorReason = "volatilityThreshold must be greater than 0, got " + std::to_string(volatilityThreshold);
        return false;
    }
    if (spreadWidenFactor <= 1) {
        errorReason = "spreadWidenFactor must be greater than 1, got " + std::to_string(spreadWidenFactor);
        return false;
    }
    if (marketStressCheckMs == 0) {
        errorReason = "marketStressCheckMs must be greater than 0";
        return false;
    }
    
    // All checks passed
    errorReason = "";
    return true;
}

bool StrategyConfig::loadFromFile(const std::string& filename) {
    // Suppress unused parameter warning
    (void)filename;
    // Note: For Phase 1, we're providing a stub implementation
    // In a real implementation, this would parse a JSON file using a library like nlohmann/json
    
    // For now, just simulate loading by returning success
    // This will be properly implemented in Phase 2
    return true;
}

bool StrategyConfig::saveToFile(const std::string& filename) const {
    // Suppress unused parameter warning
    (void)filename;
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