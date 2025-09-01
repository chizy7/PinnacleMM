#include "ExchangeConnectorFactory.h"
#include <algorithm>
#include <mutex>
#include <spdlog/spdlog.h>

namespace pinnacle {
namespace exchange {

// Initialize static instance
ExchangeConnectorFactory& ExchangeConnectorFactory::getInstance() {
    static ExchangeConnectorFactory instance;
    return instance;
}

ExchangeConnectorFactory::ExchangeConnectorFactory() : m_isInitialized(false) {
    // Create secure config
    m_secureConfig = std::make_shared<utils::SecureConfig>();
    
    // Create API credentials manager
    m_apiCredentials = std::make_shared<utils::ApiCredentials>(*m_secureConfig);
}

ExchangeConnectorFactory::~ExchangeConnectorFactory() {
    // Save API credentials if modified
    if (m_isInitialized && m_secureConfig->isModified()) {
        saveApiCredentials();
    }
    
    // Stop all market data feeds
    for (auto& pair : m_marketDataFeeds) {
        if (pair.second && pair.second->isRunning()) {
            pair.second->stop();
        }
    }
}

bool ExchangeConnectorFactory::initialize(
    const std::string& configPath,
    const std::string& masterPassword
) {
    // Store configuration path
    m_configPath = configPath;
    
    // Store master password
    m_masterPassword = masterPassword;
    
    // Ensure config directory exists
    if (!boost::filesystem::exists(configPath)) {
        try {
            boost::filesystem::create_directories(configPath);
        } catch (const std::exception& e) {
            std::cerr << "Failed to create config directory: " << e.what() << std::endl;
            return false;
        }
    }
    
    // Path to secure config file
    std::string secureConfigPath = configPath + "/secure_config.json";
    
    // Load secure config if it exists
    if (boost::filesystem::exists(secureConfigPath)) {
        if (!m_secureConfig->loadFromFile(secureConfigPath, masterPassword)) {
            std::cerr << "Failed to load secure config file" << std::endl;
            return false;
        }
    } else {
        // Create new secure config
        if (!m_secureConfig->saveToFile(secureConfigPath, masterPassword)) {
            std::cerr << "Failed to create secure config file" << std::endl;
            return false;
        }
    }
    
    // Mark as initialized
    m_isInitialized = true;
    
    return true;
}

std::shared_ptr<MarketDataFeed> ExchangeConnectorFactory::getMarketDataFeed(
    const std::string& exchangeName
) {
    // Check if initialized
    if (!m_isInitialized) {
        std::cerr << "ExchangeConnectorFactory not initialized" << std::endl;
        return nullptr;
    }
    
    // Check if exchange is supported
    if (!isExchangeSupported(exchangeName)) {
        std::cerr << "Exchange not supported: " << exchangeName << std::endl;
        return nullptr;
    }
    
    // Check if already created
    auto it = m_marketDataFeeds.find(exchangeName);
    if (it != m_marketDataFeeds.end()) {
        return it->second;
    }
    
    // Create new market data feed
    spdlog::info("Creating WebSocket market data feed for exchange: {}", exchangeName);
    WebSocketMarketDataFeed::Exchange exchangeEnum = getExchangeEnum(exchangeName);
    
    if (!m_apiCredentials) {
        spdlog::warn("No API credentials available for live trading");
    } else {
        spdlog::info("API credentials loaded successfully");
    }
    
    auto webSocketFeed = WebSocketMarketDataFeed::create(exchangeEnum, m_apiCredentials);
    auto marketDataFeed = std::dynamic_pointer_cast<MarketDataFeed>(webSocketFeed);
    
    // Store in cache
    m_marketDataFeeds[exchangeName] = marketDataFeed;
    
    spdlog::info("Market data feed created successfully");
    return marketDataFeed;
}

bool ExchangeConnectorFactory::isExchangeSupported(const std::string& exchangeName) const {
    return std::find(m_supportedExchanges.begin(), m_supportedExchanges.end(), exchangeName) != m_supportedExchanges.end();
}

std::vector<std::string> ExchangeConnectorFactory::getSupportedExchanges() const {
    return m_supportedExchanges;
}

bool ExchangeConnectorFactory::setApiCredentials(
    const std::string& exchangeName,
    const std::string& apiKey,
    const std::string& apiSecret,
    const std::optional<std::string>& passphrase
) {
    // Check if initialized
    if (!m_isInitialized) {
        std::cerr << "ExchangeConnectorFactory not initialized" << std::endl;
        return false;
    }
    
    // Check if exchange is supported
    if (!isExchangeSupported(exchangeName)) {
        std::cerr << "Exchange not supported: " << exchangeName << std::endl;
        return false;
    }
    
    // Set credentials
    if (!m_apiCredentials->setCredentials(exchangeName, apiKey, apiSecret, passphrase)) {
        std::cerr << "Failed to set API credentials for " << exchangeName << std::endl;
        return false;
    }
    
    // Save to secure config
    return saveApiCredentials();
}

bool ExchangeConnectorFactory::hasApiCredentials(const std::string& exchangeName) const {
    // Check if initialized
    if (!m_isInitialized) {
        return false;
    }
    
    // Check if exchange is supported
    if (!isExchangeSupported(exchangeName)) {
        return false;
    }
    
    // Check credentials
    return m_apiCredentials->hasCredentials(exchangeName);
}

bool ExchangeConnectorFactory::saveApiCredentials() {
    // Check if initialized
    if (!m_isInitialized) {
        std::cerr << "ExchangeConnectorFactory not initialized" << std::endl;
        return false;
    }
    
    // Path to secure config file
    std::string secureConfigPath = m_configPath + "/secure_config.json";
    
    // Save to secure config
    return m_secureConfig->saveToFile(secureConfigPath, m_masterPassword);
}

WebSocketMarketDataFeed::Exchange ExchangeConnectorFactory::getExchangeEnum(
    const std::string& exchangeName
) const {
    if (exchangeName == "coinbase") {
        return WebSocketMarketDataFeed::Exchange::COINBASE;
    } else if (exchangeName == "kraken") {
        return WebSocketMarketDataFeed::Exchange::KRAKEN;
    } else if (exchangeName == "gemini") {
        return WebSocketMarketDataFeed::Exchange::GEMINI;
    } else if (exchangeName == "binance") {
        return WebSocketMarketDataFeed::Exchange::BINANCE;
    } else if (exchangeName == "bitstamp") {
        return WebSocketMarketDataFeed::Exchange::BITSTAMP;
    } else {
        // Default to Coinbase
        return WebSocketMarketDataFeed::Exchange::COINBASE;
    }
}

} // namespace exchange
} // namespace pinnacle
