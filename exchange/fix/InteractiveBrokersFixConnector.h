#pragma once

#include "FixConnector.h"

namespace pinnacle {
namespace exchange {
namespace fix {

/**
 * @class InteractiveBrokersFixConnector
 * @brief FIX protocol connector for Interactive Brokers
 *
 * Implements FIX 4.2/4.4 protocol for IB's API
 * Supports market data subscriptions and order execution
 */
class InteractiveBrokersFixConnector : public FixConnector {
public:
  /**
   * @brief Constructor
   *
   * @param credentials API credentials for IB
   */
  explicit InteractiveBrokersFixConnector(
      std::shared_ptr<utils::ApiCredentials> credentials);

  /**
   * @brief Destructor
   */
  ~InteractiveBrokersFixConnector() override;

  // Order execution interface
  bool sendNewOrderSingle(const pinnacle::Order &order) override;
  bool cancelOrder(const std::string &orderId) override;
  bool replaceOrder(const std::string &orderId,
                    const pinnacle::Order &newOrder) override;

protected:
  // FIX message handlers
  void onLogon() override;
  void onLogout() override;
  void onMarketDataMessage(const hffix::message_reader &msg) override;
  void onExecutionReport(const hffix::message_reader &msg) override;
  void onOrderCancelReject(const hffix::message_reader &msg) override;

private:
  /**
   * @brief Initialize IB-specific configuration
   */
  void initializeIBConfig();

  /**
   * @brief Create IB-specific market data request
   */
  hffix::message_writer createIBMarketDataRequest(const std::string &symbol,
                                                  char subscriptionRequestType);

  /**
   * @brief Parse IB market data snapshot
   */
  void parseMarketDataSnapshot(const hffix::message_reader &msg);

  /**
   * @brief Parse IB market data incremental refresh
   */
  void parseMarketDataIncrementalRefresh(const hffix::message_reader &msg);

  /**
   * @brief Convert IB symbol format to internal format
   */
  std::string convertIBSymbolToInternal(const std::string &ibSymbol);

  /**
   * @brief Convert internal symbol format to IB format
   */
  std::string convertInternalSymbolToIB(const std::string &internalSymbol);

  /**
   * @brief IB-specific order handling
   */
  void handleExecutionReport(const hffix::message_reader &msg);

  /**
   * @brief Subscribe to market data after logon
   */
  void subscribeToConfiguredSymbols();

  /**
   * @brief Market data request ID counter
   */
  std::atomic<int> m_mdReqIdCounter{1};

  /**
   * @brief Order ID mapping (internal -> IB)
   */
  std::unordered_map<std::string, std::string> m_orderIdMapping;
  std::mutex m_orderIdMappingMutex;

  /**
   * @brief Pending subscriptions (to be sent after logon)
   */
  std::vector<std::string> m_pendingSubscriptions;
  std::mutex m_pendingSubscriptionsMutex;
};

} // namespace fix
} // namespace exchange
} // namespace pinnacle