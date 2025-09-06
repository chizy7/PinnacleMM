#include "FixConnector.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include <iostream>
#include <sstream>
#include <algorithm>
#include <cstring>

namespace pinnacle {
namespace exchange {
namespace fix {

FixConnector::FixConnector(const FixConfig& config, std::shared_ptr<utils::ApiCredentials> credentials)
    : m_config(config), m_credentials(credentials) {
    m_receiveBuffer.reserve(RECEIVE_BUFFER_SIZE);
}

FixConnector::~FixConnector() {
    stop();
}

bool FixConnector::start() {
    if (m_isRunning.load()) {
        return false;
    }

    m_shouldStop.store(false);
    m_isRunning.store(true);

    // Start network thread
    m_networkThread = std::thread(&FixConnector::networkThread, this);
    
    // Start message processing thread
    m_messageProcessingThread = std::thread(&FixConnector::messageProcessingLoop, this);

    // Wait a moment for connection
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    return m_isRunning.load();
}

bool FixConnector::stop() {
    if (!m_isRunning.load()) {
        return true;
    }

    m_shouldStop.store(true);

    if (m_isLoggedOn.load()) {
        sendLogout();
    }

    disconnectFromExchange();

    // Join threads
    if (m_networkThread.joinable()) {
        m_networkThread.join();
    }
    if (m_messageProcessingThread.joinable()) {
        m_messageProcessingThread.join();
    }

    m_isRunning.store(false);
    m_isLoggedOn.store(false);

    return true;
}

bool FixConnector::isRunning() const {
    return m_isRunning.load();
}

bool FixConnector::subscribeToMarketUpdates(
    const std::string& symbol,
    std::function<void(const MarketUpdate&)> callback) {
    
    std::lock_guard<std::mutex> lock(m_callbacksMutex);
    m_marketUpdateCallbacks[symbol].push_back(callback);

    if (m_isLoggedOn.load()) {
        // Subscribe to market data
        auto mdRequest = createMarketDataRequest(symbol, '1'); // Subscribe
        return sendMessage(mdRequest);
    }

    return true; // Will subscribe when logged on
}

bool FixConnector::subscribeToOrderBookUpdates(
    const std::string& symbol,
    std::function<void(const OrderBookUpdate&)> callback) {
    
    std::lock_guard<std::mutex> lock(m_callbacksMutex);
    m_orderBookUpdateCallbacks[symbol].push_back(callback);

    if (m_isLoggedOn.load()) {
        // Subscribe to market data
        auto mdRequest = createMarketDataRequest(symbol, '1'); // Subscribe
        return sendMessage(mdRequest);
    }

    return true; // Will subscribe when logged on
}

bool FixConnector::unsubscribeFromMarketUpdates(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(m_callbacksMutex);
    m_marketUpdateCallbacks.erase(symbol);

    if (m_isLoggedOn.load()) {
        // Unsubscribe from market data
        auto mdRequest = createMarketDataRequest(symbol, '2'); // Unsubscribe
        return sendMessage(mdRequest);
    }

    return true;
}

bool FixConnector::unsubscribeFromOrderBookUpdates(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(m_callbacksMutex);
    m_orderBookUpdateCallbacks.erase(symbol);

    if (m_isLoggedOn.load()) {
        // Unsubscribe from market data
        auto mdRequest = createMarketDataRequest(symbol, '2'); // Unsubscribe
        return sendMessage(mdRequest);
    }

    return true;
}

void FixConnector::publishMarketUpdate(const MarketUpdate& update) {
    std::lock_guard<std::mutex> lock(m_callbacksMutex);
    
    auto it = m_marketUpdateCallbacks.find(update.symbol);
    if (it != m_marketUpdateCallbacks.end()) {
        for (const auto& callback : it->second) {
            callback(update);
        }
    }
}

void FixConnector::publishOrderBookUpdate(const OrderBookUpdate& update) {
    std::lock_guard<std::mutex> lock(m_callbacksMutex);
    
    auto it = m_orderBookUpdateCallbacks.find(update.symbol);
    if (it != m_orderBookUpdateCallbacks.end()) {
        for (const auto& callback : it->second) {
            callback(update);
        }
    }
}

std::string FixConnector::getSessionStatus() const {
    std::lock_guard<std::mutex> lock(m_statusMutex);
    return m_sessionStatus;
}

bool FixConnector::sendMessage(const hffix::message_writer& msg) {
    if (m_socket == -1) {
        return false;
    }

    std::string message = std::string(msg.message_begin(), msg.message_end());
    
    // Add to outgoing queue for network thread to process
    return m_outgoingMessages.push(message);
}

hffix::message_writer FixConnector::createMarketDataRequest(const std::string& symbol, char subscriptionRequestType) {
    char buffer[1024];
    hffix::message_writer msg(buffer, sizeof(buffer));

    msg.push_back_header("V"); // Market Data Request
    msg.push_back_string(hffix::tag::SenderCompID, m_config.senderCompId.c_str());
    msg.push_back_string(hffix::tag::TargetCompID, m_config.targetCompId.c_str());
    msg.push_back_int(hffix::tag::MsgSeqNum, getNextSeqNum());
    
    // Current UTC time
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm* utc_tm = std::gmtime(&time_t);
    
    char sendingTime[32];
    std::snprintf(sendingTime, sizeof(sendingTime), "%04d%02d%02d-%02d:%02d:%02d",
                 utc_tm->tm_year + 1900, utc_tm->tm_mon + 1, utc_tm->tm_mday,
                 utc_tm->tm_hour, utc_tm->tm_min, utc_tm->tm_sec);
    msg.push_back_string(hffix::tag::SendingTime, sendingTime);

    // Market Data Request specific fields
    msg.push_back_string(hffix::tag::MDReqID, "MD001");
    msg.push_back_char(hffix::tag::SubscriptionRequestType, subscriptionRequestType);
    msg.push_back_int(hffix::tag::MarketDepth, 1); // Top of book
    
    // NoMDEntryTypes
    msg.push_back_int(hffix::tag::NoMDEntryTypes, 2);
    msg.push_back_char(hffix::tag::MDEntryType, '0'); // Bid
    msg.push_back_char(hffix::tag::MDEntryType, '1'); // Offer

    // NoRelatedSym
    msg.push_back_int(hffix::tag::NoRelatedSym, 1);
    msg.push_back_string(hffix::tag::Symbol, symbol.c_str());

    msg.push_back_trailer();
    return msg;
}

hffix::message_writer FixConnector::createNewOrderSingle(const pinnacle::Order& order) {
    char buffer[1024];
    hffix::message_writer msg(buffer, sizeof(buffer));

    msg.push_back_header("D"); // New Order Single
    msg.push_back_string(hffix::tag::SenderCompID, m_config.senderCompId.c_str());
    msg.push_back_string(hffix::tag::TargetCompID, m_config.targetCompId.c_str());
    msg.push_back_int(hffix::tag::MsgSeqNum, getNextSeqNum());
    
    // Current UTC time
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm* utc_tm = std::gmtime(&time_t);
    
    char sendingTime[32];
    std::snprintf(sendingTime, sizeof(sendingTime), "%04d%02d%02d-%02d:%02d:%02d",
                 utc_tm->tm_year + 1900, utc_tm->tm_mon + 1, utc_tm->tm_mday,
                 utc_tm->tm_hour, utc_tm->tm_min, utc_tm->tm_sec);
    msg.push_back_string(hffix::tag::SendingTime, sendingTime);

    // Order fields
    msg.push_back_string(hffix::tag::ClOrdID, order.getOrderId().c_str());
    msg.push_back_string(hffix::tag::Symbol, order.getSymbol().c_str());
    msg.push_back_char(hffix::tag::Side, order.getSide() == pinnacle::OrderSide::BUY ? '1' : '2');
    msg.push_back_char(hffix::tag::OrdType, order.getType() == pinnacle::OrderType::LIMIT ? '2' : '1'); // Market = 1, Limit = 2
    msg.push_back_decimal(hffix::tag::OrderQty, order.getQuantity(), 8);
    
    if (order.getType() == pinnacle::OrderType::LIMIT) {
        msg.push_back_decimal(hffix::tag::Price, order.getPrice(), 8);
    }
    
    msg.push_back_char(hffix::tag::TimeInForce, '1'); // Good Till Cancel

    msg.push_back_trailer();
    return msg;
}

hffix::message_writer FixConnector::createOrderCancelRequest(const std::string& orderId) {
    char buffer[1024];
    hffix::message_writer msg(buffer, sizeof(buffer));

    msg.push_back_header("F"); // Order Cancel Request
    msg.push_back_string(hffix::tag::SenderCompID, m_config.senderCompId.c_str());
    msg.push_back_string(hffix::tag::TargetCompID, m_config.targetCompId.c_str());
    msg.push_back_int(hffix::tag::MsgSeqNum, getNextSeqNum());
    
    // Current UTC time
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm* utc_tm = std::gmtime(&time_t);
    
    char sendingTime[32];
    std::snprintf(sendingTime, sizeof(sendingTime), "%04d%02d%02d-%02d:%02d:%02d",
                 utc_tm->tm_year + 1900, utc_tm->tm_mon + 1, utc_tm->tm_mday,
                 utc_tm->tm_hour, utc_tm->tm_min, utc_tm->tm_sec);
    msg.push_back_string(hffix::tag::SendingTime, sendingTime);

    msg.push_back_string(hffix::tag::OrigClOrdID, orderId.c_str());
    msg.push_back_string(hffix::tag::ClOrdID, ("CANCEL_" + orderId).c_str());

    msg.push_back_trailer();
    return msg;
}

void FixConnector::networkThread() {
    while (!m_shouldStop.load()) {
        if (!m_isLoggedOn.load()) {
            connectToExchange();
            if (m_socket != -1) {
                sendLogon();
            }
        }

        if (m_socket != -1) {
            processIncomingMessages();
            
            // Send any pending outgoing messages
            std::string outgoingMsg;
            while (m_outgoingMessages.pop(outgoingMsg)) {
                send(m_socket, outgoingMsg.c_str(), outgoingMsg.length(), 0);
            }

            // Check for heartbeat timeout
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - m_lastHeartbeatSent).count() 
                >= m_config.heartbeatInterval) {
                sendHeartbeat();
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (m_socket != -1) {
        close(m_socket);
        m_socket = -1;
    }
}

void FixConnector::connectToExchange() {
    if (m_socket != -1) {
        return; // Already connected
    }

    // Create socket
    m_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (m_socket == -1) {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        m_sessionStatus = "Failed to create socket";
        return;
    }

    // Resolve hostname
    struct hostent* server = gethostbyname(m_config.host.c_str());
    if (server == nullptr) {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        m_sessionStatus = "Failed to resolve hostname: " + m_config.host;
        close(m_socket);
        m_socket = -1;
        return;
    }

    // Connect to server
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(m_config.port);
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);

    if (connect(m_socket, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        m_sessionStatus = "Failed to connect to " + m_config.host + ":" + std::to_string(m_config.port);
        close(m_socket);
        m_socket = -1;
        return;
    }

    // Set socket to non-blocking
    int flags = fcntl(m_socket, F_GETFL, 0);
    fcntl(m_socket, F_SETFL, flags | O_NONBLOCK);

    {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        m_sessionStatus = "Connected to " + m_config.host + ":" + std::to_string(m_config.port);
    }
}

void FixConnector::disconnectFromExchange() {
    if (m_socket != -1) {
        close(m_socket);
        m_socket = -1;
    }
    m_isLoggedOn.store(false);
}

void FixConnector::processIncomingMessages() {
    if (m_socket == -1) {
        return;
    }

    char buffer[4096];
    ssize_t bytesRead = recv(m_socket, buffer, sizeof(buffer) - 1, 0);
    
    if (bytesRead > 0) {
        buffer[bytesRead] = '\0';
        m_receiveBuffer.append(buffer, bytesRead);
        m_lastMessageReceived = std::chrono::steady_clock::now();

        // Process complete messages
        size_t pos = 0;
        while (pos < m_receiveBuffer.length()) {
            // Find SOH (Start of Header, ASCII 1)
            size_t sohPos = m_receiveBuffer.find('\x01', pos);
            if (sohPos == std::string::npos) {
                break;
            }

            // Extract message
            std::string message = m_receiveBuffer.substr(pos, sohPos - pos + 1);
            m_incomingMessages.push(message);
            pos = sohPos + 1;
        }

        // Remove processed data
        if (pos > 0) {
            m_receiveBuffer.erase(0, pos);
        }
    } else if (bytesRead == 0) {
        // Connection closed
        disconnectFromExchange();
    } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
        // Error
        disconnectFromExchange();
    }
}

void FixConnector::messageProcessingLoop() {
    while (!m_shouldStop.load()) {
        std::string message;
        while (m_incomingMessages.pop(message)) {
            processMessage(message);
        }
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
}

void FixConnector::processMessage(const std::string& rawMessage) {
    hffix::message_reader msg(rawMessage.c_str(), rawMessage.c_str() + rawMessage.length());
    
    if (!validateMessage(msg)) {
        return;
    }

    hffix::field msgType;
    if (msg.find_with_hint(hffix::tag::MsgType, msgType)) {
        std::string msgTypeStr(msgType.begin(), msgType.end());
        
        if (msgTypeStr == "A") {
            handleLogon(msg);
        } else if (msgTypeStr == "5") {
            handleLogout(msg);
        } else if (msgTypeStr == "0") {
            handleHeartbeat(msg);
        } else if (msgTypeStr == "1") {
            handleTestRequest(msg);
        } else if (msgTypeStr == "2") {
            handleResendRequest(msg);
        } else if (msgTypeStr == "W") {
            onMarketDataMessage(msg);
        } else if (msgTypeStr == "8") {
            onExecutionReport(msg);
        } else if (msgTypeStr == "9") {
            onOrderCancelReject(msg);
        }
    }
}

bool FixConnector::validateMessage(const hffix::message_reader& msg) {
    // Basic validation - check sequence number
    hffix::field seqNum;
    if (msg.find_with_hint(hffix::tag::MsgSeqNum, seqNum)) {
        int receivedSeqNum;
        if (hffix::extract_int(seqNum, receivedSeqNum)) {
            if (receivedSeqNum == m_expectedSeqNum.load()) {
                m_expectedSeqNum.fetch_add(1);
                return true;
            } else {
                // Sequence number mismatch - send resend request
                // For simplicity, we'll just log this for now
                std::cerr << "Sequence number mismatch. Expected: " << m_expectedSeqNum.load() 
                         << ", Received: " << receivedSeqNum << std::endl;
            }
        }
    }
    return false;
}

void FixConnector::sendLogon() {
    char buffer[1024];
    hffix::message_writer msg(buffer, sizeof(buffer));

    msg.push_back_header("A"); // Logon
    msg.push_back_string(hffix::tag::SenderCompID, m_config.senderCompId.c_str());
    msg.push_back_string(hffix::tag::TargetCompID, m_config.targetCompId.c_str());
    msg.push_back_int(hffix::tag::MsgSeqNum, getNextSeqNum());
    
    // Current UTC time
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm* utc_tm = std::gmtime(&time_t);
    
    char sendingTime[32];
    std::snprintf(sendingTime, sizeof(sendingTime), "%04d%02d%02d-%02d:%02d:%02d",
                 utc_tm->tm_year + 1900, utc_tm->tm_mon + 1, utc_tm->tm_mday,
                 utc_tm->tm_hour, utc_tm->tm_min, utc_tm->tm_sec);
    msg.push_back_string(hffix::tag::SendingTime, sendingTime);

    msg.push_back_int(hffix::tag::HeartBtInt, m_config.heartbeatInterval);
    msg.push_back_string(hffix::tag::ResetSeqNumFlag, m_config.resetSeqNumsOnLogon.c_str());

    // Add authentication if credentials are provided
    if (m_credentials) {
        auto apiKey = m_credentials->getApiKey("default");
        if (apiKey && !apiKey->empty()) {
            msg.push_back_string(hffix::tag::Username, apiKey->c_str());
            auto apiSecret = m_credentials->getApiSecret("default");
            if (apiSecret && !apiSecret->empty()) {
                msg.push_back_string(hffix::tag::Password, apiSecret->c_str());
            }
        }
    }

    msg.push_back_trailer();
    
    std::string message = std::string(msg.message_begin(), msg.message_end());
    send(m_socket, message.c_str(), message.length(), 0);
    
    m_lastHeartbeatSent = std::chrono::steady_clock::now();
}

void FixConnector::sendLogout() {
    char buffer[1024];
    hffix::message_writer msg(buffer, sizeof(buffer));

    msg.push_back_header("5"); // Logout
    msg.push_back_string(hffix::tag::SenderCompID, m_config.senderCompId.c_str());
    msg.push_back_string(hffix::tag::TargetCompID, m_config.targetCompId.c_str());
    msg.push_back_int(hffix::tag::MsgSeqNum, getNextSeqNum());
    
    // Current UTC time
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm* utc_tm = std::gmtime(&time_t);
    
    char sendingTime[32];
    std::snprintf(sendingTime, sizeof(sendingTime), "%04d%02d%02d-%02d:%02d:%02d",
                 utc_tm->tm_year + 1900, utc_tm->tm_mon + 1, utc_tm->tm_mday,
                 utc_tm->tm_hour, utc_tm->tm_min, utc_tm->tm_sec);
    msg.push_back_string(hffix::tag::SendingTime, sendingTime);

    msg.push_back_trailer();
    
    std::string message = std::string(msg.message_begin(), msg.message_end());
    send(m_socket, message.c_str(), message.length(), 0);
}

void FixConnector::sendHeartbeat() {
    char buffer[512];
    hffix::message_writer msg(buffer, sizeof(buffer));

    msg.push_back_header("0"); // Heartbeat
    msg.push_back_string(hffix::tag::SenderCompID, m_config.senderCompId.c_str());
    msg.push_back_string(hffix::tag::TargetCompID, m_config.targetCompId.c_str());
    msg.push_back_int(hffix::tag::MsgSeqNum, getNextSeqNum());
    
    // Current UTC time
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm* utc_tm = std::gmtime(&time_t);
    
    char sendingTime[32];
    std::snprintf(sendingTime, sizeof(sendingTime), "%04d%02d%02d-%02d:%02d:%02d",
                 utc_tm->tm_year + 1900, utc_tm->tm_mon + 1, utc_tm->tm_mday,
                 utc_tm->tm_hour, utc_tm->tm_min, utc_tm->tm_sec);
    msg.push_back_string(hffix::tag::SendingTime, sendingTime);

    msg.push_back_trailer();
    
    std::string message = std::string(msg.message_begin(), msg.message_end());
    send(m_socket, message.c_str(), message.length(), 0);
    
    m_lastHeartbeatSent = std::chrono::steady_clock::now();
}

void FixConnector::sendTestRequest() {
    // Implementation for test request
}

void FixConnector::handleLogon(const hffix::message_reader& msg) {
    m_isLoggedOn.store(true);
    {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        m_sessionStatus = "Logged on successfully";
    }
    onLogon();
}

void FixConnector::handleLogout(const hffix::message_reader& msg) {
    m_isLoggedOn.store(false);
    {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        m_sessionStatus = "Logged out";
    }
    onLogout();
}

void FixConnector::handleHeartbeat(const hffix::message_reader& msg) {
    // Heartbeat received, update last message time
    m_lastMessageReceived = std::chrono::steady_clock::now();
}

void FixConnector::handleTestRequest(const hffix::message_reader& msg) {
    // Send heartbeat in response to test request
    sendHeartbeat();
}

void FixConnector::handleResendRequest(const hffix::message_reader& msg) {
    // For simplicity, we'll just ignore resend requests for now
    // In a production system, you'd need to store and resend messages
}

} // namespace fix
} // namespace exchange
} // namespace pinnacle