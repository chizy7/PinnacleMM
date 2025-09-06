#pragma once

// #include "FixConnector.h"
// #include "InteractiveBrokersFixConnector.h"
#include "../connector/SecureConfig.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <mutex>

namespace pinnacle {
namespace exchange {
namespace fix {

/**
 * @class FixConnectorFactory
 * @brief Factory for creating FIX protocol connectors for different exchanges
 */
class FixConnectorFactory {
public:
    /**
     * @brief Supported FIX exchanges
     */
    enum class Exchange {
        INTERACTIVE_BROKERS,
        COINBASE_FIX,  // Coinbase Pro has FIX support
        KRAKEN_FIX,    // Kraken has FIX support for institutional clients
        BINANCE_FIX    // Binance has FIX support for institutional clients
    };

    /**
     * @brief Get factory instance (singleton)
     */
    static FixConnectorFactory& getInstance();

    /**
     * @brief Create FIX connector for specified exchange
     * 
     * @param exchange Exchange type
     * @param credentials API credentials
     * @return Shared pointer to FIX connector
     */
    // Placeholder for now - returning nullptr until hffix API is resolved
    void* createConnector(
        Exchange exchange,
        std::shared_ptr<utils::ApiCredentials> credentials);

    /**
     * @brief Get existing connector or create new one
     * 
     * @param exchange Exchange type
     * @param credentials API credentials
     * @return Shared pointer to FIX connector
     */
    void* getConnector(
        Exchange exchange,
        std::shared_ptr<utils::ApiCredentials> credentials);

    /**
     * @brief Check if exchange supports FIX protocol
     */
    bool isFixSupported(const std::string& exchangeName);

    /**
     * @brief Convert string to Exchange enum
     */
    Exchange stringToExchange(const std::string& exchangeName);

    /**
     * @brief FIX Configuration structure
     */
    struct FixConfig {
        std::string senderCompId;
        std::string targetCompId;
        std::string fixVersion{"FIX.4.4"};
        std::string host;
        int port{0};
        bool useSSL{true};
        int heartbeatInterval{30};
        int logonTimeout{30};
        std::string resetSeqNumsOnLogon{"Y"};
    };

    /**
     * @brief Get FIX configuration for exchange
     */
    FixConfig getDefaultFixConfig(Exchange exchange);

private:
    FixConnectorFactory() = default;
    ~FixConnectorFactory() = default;
    
    // Non-copyable
    FixConnectorFactory(const FixConnectorFactory&) = delete;
    FixConnectorFactory& operator=(const FixConnectorFactory&) = delete;

    /**
     * @brief Active connectors cache - placeholder
     */
    std::unordered_map<std::string, void*> m_connectors;
    std::mutex m_connectorsMutex;

    /**
     * @brief Generate unique key for connector caching
     */
    std::string generateConnectorKey(Exchange exchange, const std::string& apiKey);
};

} // namespace fix
} // namespace exchange
} // namespace pinnacle