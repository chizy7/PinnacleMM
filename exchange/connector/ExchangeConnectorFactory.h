#ifndef EXCHANGE_CONNECTOR_FACTORY_H
#define EXCHANGE_CONNECTOR_FACTORY_H

#include <boost/filesystem.hpp>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "SecureConfig.h"
#include "WebSocketMarketDataFeed.h"

namespace pinnacle {
namespace exchange {

class ExchangeConnectorFactory {
public:
  static ExchangeConnectorFactory& getInstance();

  ExchangeConnectorFactory();
  ~ExchangeConnectorFactory();

  bool initialize(const std::string& configPath,
                  const std::string& masterPassword);
  bool isInitialized() const { return m_isInitialized; }

  std::shared_ptr<MarketDataFeed>
  getMarketDataFeed(const std::string& exchangeName);
  bool isExchangeSupported(const std::string& exchangeName) const;
  std::vector<std::string> getSupportedExchanges() const;

  bool setApiCredentials(
      const std::string& exchangeName, const std::string& apiKey,
      const std::string& apiSecret,
      const std::optional<std::string>& passphrase = std::nullopt);

  bool hasApiCredentials(const std::string& exchangeName) const;
  bool saveApiCredentials();

private:
  WebSocketMarketDataFeed::Exchange
  getExchangeEnum(const std::string& exchangeName) const;

  bool m_isInitialized;
  std::string m_configPath;
  std::string m_masterPassword;
  std::shared_ptr<utils::SecureConfig> m_secureConfig;
  std::shared_ptr<utils::ApiCredentials> m_apiCredentials;
  std::unordered_map<std::string, std::shared_ptr<MarketDataFeed>>
      m_marketDataFeeds;
  std::mutex m_dataFeedsMutex;
  std::vector<std::string> m_supportedExchanges = {
      "coinbase", "kraken", "gemini", "binance", "bitstamp"};
};

} // namespace exchange
} // namespace pinnacle

#endif // EXCHANGE_CONNECTOR_FACTORY_H
