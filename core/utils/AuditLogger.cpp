#include "AuditLogger.h"
#include <nlohmann/json.hpp>
#include <iomanip>
#include <sstream>
#include <filesystem>

namespace pinnacle {
namespace utils {

AuditLogger& AuditLogger::getInstance() {
    static AuditLogger instance;
    return instance;
}

bool AuditLogger::initialize(const std::string& logPath, size_t maxFileSize, size_t maxFiles) {
    try {
        // Create directory if it doesn't exist
        std::filesystem::path path(logPath);
        std::filesystem::create_directories(path.parent_path());
        
        // Create rotating file logger
        m_logger = spdlog::rotating_logger_mt("audit", logPath, maxFileSize, maxFiles);
        
        // Set pattern for audit logs (timestamp + message)
        m_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%f] %v");
        
        // Always flush audit logs immediately
        m_logger->set_level(spdlog::level::trace);
        m_logger->flush_on(spdlog::level::trace);
        
        // Log initialization
        AuditEvent initEvent(AuditEventType::SYSTEM_START, "Audit logging initialized", true);
        logEvent(initEvent);
        
        return true;
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to initialize audit logger: {}", e.what());
        return false;
    }
}

void AuditLogger::logEvent(const AuditEvent& event) {
    if (!m_enabled || !m_logger) {
        return;
    }
    
    try {
        std::string jsonEvent = formatEventAsJson(event);
        m_logger->info(jsonEvent);
        
        // Also log to main logger for critical security events
        if (event.type == AuditEventType::AUTHENTICATION_FAILURE ||
            event.type == AuditEventType::SUSPICIOUS_ACTIVITY ||
            event.type == AuditEventType::PRIVILEGE_ESCALATION) {
            spdlog::warn("SECURITY EVENT: {}", event.description);
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to log audit event: {}", e.what());
    }
}

void AuditLogger::logAuthentication(bool success, const std::string& userId, const std::string& source) {
    AuditEvent event(success ? AuditEventType::AUTHENTICATION_SUCCESS : AuditEventType::AUTHENTICATION_FAILURE,
                    "Authentication attempt", success);
    event.userId = userId;
    event.source = source;
    
    logEvent(event);
}

void AuditLogger::logCredentialAccess(const std::string& userId, const std::string& credentialType, bool success) {
    AuditEvent event(AuditEventType::CREDENTIAL_ACCESS, "Credential access attempt", success);
    event.userId = userId;
    event.target = credentialType;
    event.additionalData = "type: " + credentialType;
    
    logEvent(event);
}

void AuditLogger::logNetworkConnection(const std::string& destination, bool success, const std::string& protocol) {
    AuditEvent event(AuditEventType::NETWORK_CONNECTION, "Network connection attempt", success);
    event.target = destination;
    event.additionalData = "protocol: " + protocol;
    
    logEvent(event);
}

void AuditLogger::logOrderActivity(const std::string& userId, const std::string& orderId,
                                  const std::string& action, const std::string& symbol, bool success) {
    AuditEventType eventType;
    if (action == "submit") {
        eventType = AuditEventType::ORDER_SUBMISSION;
    } else if (action == "modify") {
        eventType = AuditEventType::ORDER_MODIFICATION;
    } else if (action == "cancel") {
        eventType = AuditEventType::ORDER_CANCELLATION;
    } else {
        eventType = AuditEventType::ORDER_SUBMISSION; // Default
    }
    
    AuditEvent event(eventType, "Order " + action + " attempt", success);
    event.userId = userId;
    event.target = orderId;
    
    nlohmann::json additionalData;
    additionalData["order_id"] = orderId;
    additionalData["action"] = action;
    additionalData["symbol"] = symbol;
    event.additionalData = additionalData.dump();
    
    logEvent(event);
}

void AuditLogger::logConfigAccess(const std::string& userId, const std::string& configPath,
                                 const std::string& action, bool success) {
    AuditEventType eventType = (action == "write" || action == "modify") ? 
                              AuditEventType::CONFIG_MODIFICATION : AuditEventType::CONFIG_ACCESS;
    
    AuditEvent event(eventType, "Configuration " + action + " attempt", success);
    event.userId = userId;
    event.target = configPath;
    
    nlohmann::json additionalData;
    additionalData["path"] = configPath;
    additionalData["action"] = action;
    event.additionalData = additionalData.dump();
    
    logEvent(event);
}

void AuditLogger::logSuspiciousActivity(const std::string& description, const std::string& source,
                                       const std::string& severity) {
    AuditEvent event(AuditEventType::SUSPICIOUS_ACTIVITY, description, false);
    event.source = source;
    
    nlohmann::json additionalData;
    additionalData["severity"] = severity;
    additionalData["requires_investigation"] = true;
    event.additionalData = additionalData.dump();
    
    logEvent(event);
}

void AuditLogger::logSystemEvent(const std::string& eventDesc, bool success) {
    AuditEventType eventType = success ? AuditEventType::SYSTEM_START : AuditEventType::ERROR_CONDITION;
    AuditEvent event(eventType, eventDesc, success);
    
    logEvent(event);
}

void AuditLogger::setCurrentSession(const std::string& userId, const std::string& sessionId) {
    m_currentUserId = userId;
    m_currentSessionId = sessionId;
    
    AuditEvent event(AuditEventType::AUTHENTICATION_SUCCESS, "Session established", true);
    event.userId = userId;
    event.sessionId = sessionId;
    
    logEvent(event);
}

std::string AuditLogger::eventTypeToString(AuditEventType type) {
    switch (type) {
        case AuditEventType::AUTHENTICATION_SUCCESS: return "AUTH_SUCCESS";
        case AuditEventType::AUTHENTICATION_FAILURE: return "AUTH_FAILURE";
        case AuditEventType::CREDENTIAL_ACCESS: return "CREDENTIAL_ACCESS";
        case AuditEventType::CREDENTIAL_MODIFICATION: return "CREDENTIAL_MODIFICATION";
        case AuditEventType::CONFIG_ACCESS: return "CONFIG_ACCESS";
        case AuditEventType::CONFIG_MODIFICATION: return "CONFIG_MODIFICATION";
        case AuditEventType::NETWORK_CONNECTION: return "NETWORK_CONNECTION";
        case AuditEventType::NETWORK_FAILURE: return "NETWORK_FAILURE";
        case AuditEventType::ORDER_SUBMISSION: return "ORDER_SUBMISSION";
        case AuditEventType::ORDER_MODIFICATION: return "ORDER_MODIFICATION";
        case AuditEventType::ORDER_CANCELLATION: return "ORDER_CANCELLATION";
        case AuditEventType::DATA_ACCESS: return "DATA_ACCESS";
        case AuditEventType::PRIVILEGE_ESCALATION: return "PRIVILEGE_ESCALATION";
        case AuditEventType::SUSPICIOUS_ACTIVITY: return "SUSPICIOUS_ACTIVITY";
        case AuditEventType::SYSTEM_START: return "SYSTEM_START";
        case AuditEventType::SYSTEM_STOP: return "SYSTEM_STOP";
        case AuditEventType::ERROR_CONDITION: return "ERROR_CONDITION";
        default: return "UNKNOWN";
    }
}

std::string AuditLogger::formatEventAsJson(const AuditEvent& event) {
    nlohmann::json json;
    
    json["timestamp"] = getCurrentTimestamp();
    json["event_type"] = eventTypeToString(event.type);
    json["user_id"] = event.userId.empty() ? m_currentUserId : event.userId;
    json["session_id"] = event.sessionId.empty() ? m_currentSessionId : event.sessionId;
    json["source"] = event.source;
    json["target"] = event.target;
    json["description"] = event.description;
    json["success"] = event.success;
    
    if (!event.additionalData.empty()) {
        try {
            json["additional_data"] = nlohmann::json::parse(event.additionalData);
        } catch (const std::exception&) {
            json["additional_data_raw"] = event.additionalData;
        }
    }
    
    return json.dump();
}

std::string AuditLogger::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
    
    return ss.str();
}

} // namespace utils
} // namespace pinnacle