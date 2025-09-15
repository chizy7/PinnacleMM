#include "InteractiveBrokersFixConnector.h"
#include <iomanip>
#include <iostream>
#include <sstream>

namespace pinnacle {
namespace exchange {
namespace fix {

InteractiveBrokersFixConnector::InteractiveBrokersFixConnector(
    std::shared_ptr<utils::ApiCredentials> credentials)
    : FixConnector(FixConfig{}, credentials) {
  initializeIBConfig();
}

InteractiveBrokersFixConnector::~InteractiveBrokersFixConnector() { stop(); }

void InteractiveBrokersFixConnector::initializeIBConfig() {
  // IB FIX Gateway configuration
  m_config.senderCompId = "CLIENT1"; // Will be updated with actual client ID
  m_config.targetCompId = "IBKRFIX";
  m_config.fixVersion = "FIX.4.2";
  m_config.host = "localhost"; // IB Gateway typically runs locally
  m_config.port = 4101;        // Default IB FIX port for paper trading
  m_config.useSSL = false;     // IB typically uses unencrypted local connection
  m_config.heartbeatInterval = 30;
  m_config.logonTimeout = 30;
  m_config.resetSeqNumsOnLogon = "Y";

  // Update sender comp ID if we have credentials
  if (m_credentials) {
    auto apiKey = m_credentials->getApiKey("interactive_brokers");
    if (apiKey && !apiKey->empty()) {
      m_config.senderCompId = *apiKey; // Use API key as sender comp ID
    }
  }
}

bool InteractiveBrokersFixConnector::sendNewOrderSingle(
    const pinnacle::Order &order) {
  if (!isLoggedOn()) {
    std::cerr << "Cannot send order - not logged on to IB" << std::endl;
    return false;
  }

  auto msg = createNewOrderSingle(order);

  // Store order ID mapping
  {
    std::lock_guard<std::mutex> lock(m_orderIdMappingMutex);
    m_orderIdMapping[order.getOrderId()] = order.getOrderId();
  }

  return sendMessage(msg);
}

bool InteractiveBrokersFixConnector::cancelOrder(const std::string &orderId) {
  if (!isLoggedOn()) {
    std::cerr << "Cannot cancel order - not logged on to IB" << std::endl;
    return false;
  }

  auto msg = createOrderCancelRequest(orderId);
  return sendMessage(msg);
}

bool InteractiveBrokersFixConnector::replaceOrder(
    const std::string &orderId, const pinnacle::Order &newOrder) {
  if (!isLoggedOn()) {
    std::cerr << "Cannot replace order - not logged on to IB" << std::endl;
    return false;
  }

  // IB doesn't support order replace in FIX, so we cancel and send new
  // First cancel the existing order
  if (!cancelOrder(orderId)) {
    return false;
  }

  // Then send the new order
  return sendNewOrderSingle(newOrder);
}

void InteractiveBrokersFixConnector::onLogon() {
  std::cout << "IB FIX: Successfully logged on to Interactive Brokers"
            << std::endl;

  // Subscribe to any pending market data requests
  subscribeToConfiguredSymbols();
}

void InteractiveBrokersFixConnector::onLogout() {
  std::cout << "IB FIX: Logged out from Interactive Brokers" << std::endl;
}

void InteractiveBrokersFixConnector::onMarketDataMessage(
    const hffix::message_reader &msg) {
  hffix::field_reader msgType;
  if (!msg.find_with_hint(hffix::tag::MsgType, msgType)) {
    return;
  }

  std::string msgTypeStr(msgType.begin(), msgType.end());

  if (msgTypeStr == "W") {
    // Market Data Snapshot/Full Refresh
    parseMarketDataSnapshot(msg);
  } else if (msgTypeStr == "X") {
    // Market Data Incremental Refresh
    parseMarketDataIncrementalRefresh(msg);
  }
}

void InteractiveBrokersFixConnector::onExecutionReport(
    const hffix::message_reader &msg) {
  handleExecutionReport(msg);
}

void InteractiveBrokersFixConnector::onOrderCancelReject(
    const hffix::message_reader &msg) {
  hffix::field clOrdId, origClOrdId, ordStatus, cxlRejReason, text;

  std::string orderIdStr, origOrderIdStr, statusStr, reasonStr, textStr;

  if (msg.find_with_hint(hffix::tag::ClOrdID, clOrdId)) {
    orderIdStr = std::string(clOrdId.begin(), clOrdId.end());
  }

  if (msg.find_with_hint(hffix::tag::OrigClOrdID, origClOrdId)) {
    origOrderIdStr = std::string(origClOrdId.begin(), origClOrdId.end());
  }

  if (msg.find_with_hint(hffix::tag::OrdStatus, ordStatus)) {
    statusStr = std::string(ordStatus.begin(), ordStatus.end());
  }

  if (msg.find_with_hint(hffix::tag::CxlRejReason, cxlRejReason)) {
    reasonStr = std::string(cxlRejReason.begin(), cxlRejReason.end());
  }

  if (msg.find_with_hint(hffix::tag::Text, text)) {
    textStr = std::string(text.begin(), text.end());
  }

  std::cout << "IB FIX: Order cancel rejected - Order: " << origOrderIdStr
            << ", Reason: " << reasonStr << ", Text: " << textStr << std::endl;
}

hffix::message_writer InteractiveBrokersFixConnector::createIBMarketDataRequest(
    const std::string &symbol, char subscriptionRequestType) {

  char buffer[1024];
  hffix::message_writer msg(buffer, sizeof(buffer));

  msg.push_back_header("V"); // Market Data Request
  msg.push_back_string(hffix::tag::SenderCompID, m_config.senderCompId.c_str());
  msg.push_back_string(hffix::tag::TargetCompID, m_config.targetCompId.c_str());
  msg.push_back_int(hffix::tag::MsgSeqNum, getNextSeqNum());

  // Current UTC time
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);
  std::tm *utc_tm = std::gmtime(&time_t);

  char sendingTime[32];
  std::snprintf(sendingTime, sizeof(sendingTime), "%04d%02d%02d-%02d:%02d:%02d",
                utc_tm->tm_year + 1900, utc_tm->tm_mon + 1, utc_tm->tm_mday,
                utc_tm->tm_hour, utc_tm->tm_min, utc_tm->tm_sec);
  msg.push_back_string(hffix::tag::SendingTime, sendingTime);

  // Market Data Request specific fields for IB
  std::string mdReqId = "MD" + std::to_string(m_mdReqIdCounter.fetch_add(1));
  msg.push_back_string(hffix::tag::MDReqID, mdReqId.c_str());
  msg.push_back_char(hffix::tag::SubscriptionRequestType,
                     subscriptionRequestType);
  msg.push_back_int(hffix::tag::MarketDepth, 1); // Top of book

  // NoMDEntryTypes - IB supports Bid(0), Offer(1), Trade(2)
  msg.push_back_int(hffix::tag::NoMDEntryTypes, 3);
  msg.push_back_char(hffix::tag::MDEntryType, '0'); // Bid
  msg.push_back_char(hffix::tag::MDEntryType, '1'); // Offer
  msg.push_back_char(hffix::tag::MDEntryType, '2'); // Trade

  // NoRelatedSym
  msg.push_back_int(hffix::tag::NoRelatedSym, 1);
  std::string ibSymbol = convertInternalSymbolToIB(symbol);
  msg.push_back_string(hffix::tag::Symbol, ibSymbol.c_str());

  msg.push_back_trailer();
  return msg;
}

void InteractiveBrokersFixConnector::parseMarketDataSnapshot(
    const hffix::message_reader &msg) {
  hffix::field symbol, noMDEntries;

  if (!msg.find_with_hint(hffix::tag::Symbol, symbol)) {
    return;
  }

  std::string symbolStr =
      convertIBSymbolToInternal(std::string(symbol.begin(), symbol.end()));

  if (!msg.find_with_hint(hffix::tag::NoMDEntries, noMDEntries)) {
    return;
  }

  int numEntries;
  if (!hffix::extract_int(noMDEntries, numEntries)) {
    return;
  }

  MarketUpdate update;
  update.symbol = symbolStr;
  update.timestamp = std::chrono::steady_clock::now();

  // Parse MD entries
  hffix::field entryType, entryPx, entrySize;
  auto entryTypeIter = msg.begin();
  auto entryPxIter = msg.begin();
  auto entrySizeIter = msg.begin();

  for (int i = 0; i < numEntries; ++i) {
    if (msg.find_with_hint(hffix::tag::MDEntryType, entryType, entryTypeIter)) {
      char type;
      if (hffix::extract_char(entryType, type)) {
        if (msg.find_with_hint(hffix::tag::MDEntryPx, entryPx, entryPxIter)) {
          double price;
          if (hffix::extract_decimal(entryPx, price)) {
            if (type == '0') { // Bid
              update.bid = price;
            } else if (type == '1') { // Offer
              update.ask = price;
            } else if (type == '2') { // Trade
              update.lastPrice = price;
            }
          }
        }

        if (msg.find_with_hint(hffix::tag::MDEntrySize, entrySize,
                               entrySizeIter)) {
          double size;
          if (hffix::extract_decimal(entrySize, size)) {
            if (type == '0') { // Bid
              update.bidSize = size;
            } else if (type == '1') { // Offer
              update.askSize = size;
            } else if (type == '2') { // Trade
              update.volume = size;
            }
          }
        }
      }
    }
  }

  publishMarketUpdate(update);
}

void InteractiveBrokersFixConnector::parseMarketDataIncrementalRefresh(
    const hffix::message_reader &msg) {
  // Similar to snapshot parsing but for incremental updates
  parseMarketDataSnapshot(msg); // Simplified implementation
}

std::string InteractiveBrokersFixConnector::convertIBSymbolToInternal(
    const std::string &ibSymbol) {
  // IB uses different symbol formats, convert to our internal format
  // For example: "EUR.USD" -> "EURUSD", "AAPL" -> "AAPL"
  std::string internal = ibSymbol;
  std::replace(internal.begin(), internal.end(), '.', '\0');
  internal.erase(std::find(internal.begin(), internal.end(), '\0'),
                 internal.end());
  return internal;
}

std::string InteractiveBrokersFixConnector::convertInternalSymbolToIB(
    const std::string &internalSymbol) {
  // Convert our internal format to IB format
  // This is a simplified conversion - in practice you'd need more sophisticated
  // mapping
  if (internalSymbol.length() == 6 &&
      internalSymbol.find("USD") != std::string::npos) {
    // Currency pair
    std::string base = internalSymbol.substr(0, 3);
    std::string quote = internalSymbol.substr(3, 3);
    return base + "." + quote;
  }
  return internalSymbol; // Stock symbols typically unchanged
}

void InteractiveBrokersFixConnector::handleExecutionReport(
    const hffix::message_reader &msg) {
  hffix::field clOrdId, symbol, side, orderQty, price, cumQty, avgPx, ordStatus,
      execType;

  std::string orderIdStr, symbolStr, sideStr, statusStr, execTypeStr;
  double orderQuantity = 0.0, orderPrice = 0.0, cumQuantity = 0.0,
         avgPrice = 0.0;

  if (msg.find_with_hint(hffix::tag::ClOrdID, clOrdId)) {
    orderIdStr = std::string(clOrdId.begin(), clOrdId.end());
  }

  if (msg.find_with_hint(hffix::tag::Symbol, symbol)) {
    symbolStr =
        convertIBSymbolToInternal(std::string(symbol.begin(), symbol.end()));
  }

  if (msg.find_with_hint(hffix::tag::Side, side)) {
    sideStr = std::string(side.begin(), side.end());
  }

  if (msg.find_with_hint(hffix::tag::OrderQty, orderQty)) {
    hffix::extract_decimal(orderQty, orderQuantity);
  }

  if (msg.find_with_hint(hffix::tag::Price, price)) {
    hffix::extract_decimal(price, orderPrice);
  }

  if (msg.find_with_hint(hffix::tag::CumQty, cumQty)) {
    hffix::extract_decimal(cumQty, cumQuantity);
  }

  if (msg.find_with_hint(hffix::tag::AvgPx, avgPx)) {
    hffix::extract_decimal(avgPx, avgPrice);
  }

  if (msg.find_with_hint(hffix::tag::OrdStatus, ordStatus)) {
    statusStr = std::string(ordStatus.begin(), ordStatus.end());
  }

  if (msg.find_with_hint(hffix::tag::ExecType, execType)) {
    execTypeStr = std::string(execType.begin(), execType.end());
  }

  std::cout << "IB FIX: Execution Report - Order: " << orderIdStr
            << ", Symbol: " << symbolStr << ", Side: " << sideStr
            << ", Status: " << statusStr << ", Exec Type: " << execTypeStr
            << ", Cum Qty: " << cumQuantity << ", Avg Px: " << avgPrice
            << std::endl;

  // Typically here would update internal order state, notify strategy, etc.
}

void InteractiveBrokersFixConnector::subscribeToConfiguredSymbols() {
  std::lock_guard<std::mutex> lock(m_pendingSubscriptionsMutex);

  for (const auto &symbol : m_pendingSubscriptions) {
    auto mdRequest = createIBMarketDataRequest(symbol, '1'); // Subscribe
    sendMessage(mdRequest);
    std::cout << "IB FIX: Subscribed to market data for " << symbol
              << std::endl;
  }

  m_pendingSubscriptions.clear();
}

} // namespace fix
} // namespace exchange
} // namespace pinnacle