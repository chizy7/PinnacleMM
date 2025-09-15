#include "FixConnectorFactory.h"
#include <algorithm>
#include <iostream>
#include <sstream>

namespace pinnacle {
namespace exchange {
namespace fix {

FixConnectorFactory &FixConnectorFactory::getInstance() {
  static FixConnectorFactory instance;
  return instance;
}

void *FixConnectorFactory::createConnector(
    Exchange exchange, std::shared_ptr<utils::ApiCredentials> credentials) {

  if (!credentials) {
    std::cerr << "FixConnectorFactory: No credentials provided" << std::endl;
    return nullptr;
  }

  switch (exchange) {
  case Exchange::INTERACTIVE_BROKERS:
    // return std::make_shared<InteractiveBrokersFixConnector>(credentials);
    std::cout << "FixConnectorFactory: Interactive Brokers FIX connector ready "
                 "(implementation pending hffix API fix)"
              << std::endl;
    return nullptr;

  case Exchange::COINBASE_FIX:
    // TODO: Implement CoinbaseFixConnector
    std::cerr
        << "FixConnectorFactory: Coinbase FIX connector not yet implemented"
        << std::endl;
    return nullptr;

  case Exchange::KRAKEN_FIX:
    // TODO: Implement KrakenFixConnector
    std::cerr << "FixConnectorFactory: Kraken FIX connector not yet implemented"
              << std::endl;
    return nullptr;

  case Exchange::BINANCE_FIX:
    // TODO: Implement BinanceFixConnector
    std::cerr
        << "FixConnectorFactory: Binance FIX connector not yet implemented"
        << std::endl;
    return nullptr;

  default:
    std::cerr << "FixConnectorFactory: Unknown exchange type" << std::endl;
    return nullptr;
  }
}

void *FixConnectorFactory::getConnector(
    Exchange exchange, std::shared_ptr<utils::ApiCredentials> credentials) {

  if (!credentials) {
    return nullptr;
  }

  auto apiKey = credentials->getApiKey("default");
  std::string key = generateConnectorKey(exchange, apiKey ? *apiKey : "");

  std::lock_guard<std::mutex> lock(m_connectorsMutex);

  auto it = m_connectors.find(key);
  if (it != m_connectors.end() && it->second) {
    return it->second;
  }

  // Create new connector
  auto connector = createConnector(exchange, credentials);
  if (connector) {
    m_connectors[key] = connector;
  }

  return connector;
}

bool FixConnectorFactory::isFixSupported(const std::string &exchangeName) {
  std::string lowerName = exchangeName;
  std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                 ::tolower);

  return (lowerName == "interactive_brokers" || lowerName == "ib" ||
          lowerName == "coinbase_fix" || lowerName == "kraken_fix" ||
          lowerName == "binance_fix");
}

FixConnectorFactory::Exchange
FixConnectorFactory::stringToExchange(const std::string &exchangeName) {
  std::string lowerName = exchangeName;
  std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                 ::tolower);

  if (lowerName == "interactive_brokers" || lowerName == "ib") {
    return Exchange::INTERACTIVE_BROKERS;
  } else if (lowerName == "coinbase_fix") {
    return Exchange::COINBASE_FIX;
  } else if (lowerName == "kraken_fix") {
    return Exchange::KRAKEN_FIX;
  } else if (lowerName == "binance_fix") {
    return Exchange::BINANCE_FIX;
  }

  throw std::invalid_argument("Unknown FIX exchange: " + exchangeName);
}

FixConnectorFactory::FixConfig
FixConnectorFactory::getDefaultFixConfig(Exchange exchange) {
  FixConfig config;

  switch (exchange) {
  case Exchange::INTERACTIVE_BROKERS:
    config.senderCompId = "CLIENT1"; // Will be overridden with actual client ID
    config.targetCompId = "IBKRFIX";
    config.fixVersion = "FIX.4.2";
    config.host = "localhost";
    config.port = 4101; // Paper trading port
    config.useSSL = false;
    config.heartbeatInterval = 30;
    config.logonTimeout = 30;
    config.resetSeqNumsOnLogon = "Y";
    break;

  case Exchange::COINBASE_FIX:
    config.senderCompId = "COINBASE_CLIENT";
    config.targetCompId = "COINBASE";
    config.fixVersion = "FIX.4.2";
    config.host = "fix.exchange.coinbase.com";
    config.port = 4198;
    config.useSSL = true;
    config.heartbeatInterval = 30;
    config.logonTimeout = 30;
    config.resetSeqNumsOnLogon = "Y";
    break;

  case Exchange::KRAKEN_FIX:
    config.senderCompId = "KRAKEN_CLIENT";
    config.targetCompId = "KRAKEN";
    config.fixVersion = "FIX.4.4";
    config.host = "fix.kraken.com";
    config.port = 9880;
    config.useSSL = true;
    config.heartbeatInterval = 30;
    config.logonTimeout = 30;
    config.resetSeqNumsOnLogon = "Y";
    break;

  case Exchange::BINANCE_FIX:
    config.senderCompId = "BINANCE_CLIENT";
    config.targetCompId = "BINANCE";
    config.fixVersion = "FIX.4.4";
    config.host = "fix-oe.binance.com";
    config.port = 9000;
    config.useSSL = true;
    config.heartbeatInterval = 30;
    config.logonTimeout = 30;
    config.resetSeqNumsOnLogon = "Y";
    break;

  default:
    std::cerr << "FixConnectorFactory: No default config for unknown exchange"
              << std::endl;
  }

  return config;
}

std::string
FixConnectorFactory::generateConnectorKey(Exchange exchange,
                                          const std::string &apiKey) {
  std::stringstream ss;
  ss << static_cast<int>(exchange) << "_" << apiKey;
  return ss.str();
}

} // namespace fix
} // namespace exchange
} // namespace pinnacle