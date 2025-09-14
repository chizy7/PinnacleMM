#pragma once

#include <string>
#include <chrono>
#include <memory>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>

namespace pinnacle {
namespace utils {

/**
 * @enum AuditEventType
 * @brief Types of security events to audit
 */
enum class AuditEventType {
    AUTHENTICATION_SUCCESS,
    AUTHENTICATION_FAILURE,
    CREDENTIAL_ACCESS,
    CREDENTIAL_MODIFICATION,
    CONFIG_ACCESS,
    CONFIG_MODIFICATION,
    NETWORK_CONNECTION,
    NETWORK_FAILURE,
    ORDER_SUBMISSION,
    ORDER_MODIFICATION,
    ORDER_CANCELLATION,
    DATA_ACCESS,
    PRIVILEGE_ESCALATION,
    SUSPICIOUS_ACTIVITY,
    SYSTEM_START,
    SYSTEM_STOP,
    ERROR_CONDITION
};

/**
 * @struct AuditEvent
 * @brief Security audit event data
 */
struct AuditEvent {
    AuditEventType type;
    std::string userId;
    std::string sessionId;
    std::string source;
    std::string target;
    std::string description;
    std::chrono::system_clock::time_point timestamp;
    bool success;
    std::string additionalData;
    
    AuditEvent(AuditEventType t, const std::string& desc, bool s = true)
        : type(t), description(desc), timestamp(std::chrono::system_clock::now()), success(s) {}
};

/**
 * @class AuditLogger
 * @brief Security audit logging system
 */
class AuditLogger {
public:
    /**
     * @brief Get singleton instance
     */
    static AuditLogger& getInstance();
    
    /**
     * @brief Initialize audit logger
     * 
     * @param logPath Path to audit log file
     * @param maxFileSize Maximum size per log file (bytes)
     * @param maxFiles Maximum number of rotating log files
     * @return true if initialized successfully
     */
    bool initialize(const std::string& logPath, 
                   size_t maxFileSize = 10 * 1024 * 1024,  // 10MB
                   size_t maxFiles = 5);
    
    /**
     * @brief Log audit event
     * 
     * @param event Audit event to log
     */
    void logEvent(const AuditEvent& event);
    
    /**
     * @brief Log authentication event
     * 
     * @param success Whether authentication succeeded
     * @param userId User identifier
     * @param source Source of authentication attempt
     */
    void logAuthentication(bool success, const std::string& userId, const std::string& source);
    
    /**
     * @brief Log credential access
     * 
     * @param userId User accessing credentials
     * @param credentialType Type of credential accessed
     * @param success Whether access was successful
     */
    void logCredentialAccess(const std::string& userId, const std::string& credentialType, bool success);
    
    /**
     * @brief Log network connection
     * 
     * @param destination Connection destination
     * @param success Whether connection succeeded
     * @param protocol Protocol used
     */
    void logNetworkConnection(const std::string& destination, bool success, const std::string& protocol = "");
    
    /**
     * @brief Log order activity
     * 
     * @param userId User submitting order
     * @param orderId Order identifier
     * @param action Order action (submit, modify, cancel)
     * @param symbol Trading symbol
     * @param success Whether action succeeded
     */
    void logOrderActivity(const std::string& userId, const std::string& orderId, 
                         const std::string& action, const std::string& symbol, bool success);
    
    /**
     * @brief Log configuration access
     * 
     * @param userId User accessing configuration
     * @param configPath Configuration path/key
     * @param action Action performed (read, write, modify)
     * @param success Whether action succeeded
     */
    void logConfigAccess(const std::string& userId, const std::string& configPath,
                        const std::string& action, bool success);
    
    /**
     * @brief Log suspicious activity
     * 
     * @param description Description of suspicious activity
     * @param source Source of activity
     * @param severity Severity level
     */
    void logSuspiciousActivity(const std::string& description, const std::string& source,
                              const std::string& severity = "medium");
    
    /**
     * @brief Log system event
     * 
     * @param event Event description
     * @param success Whether event was successful
     */
    void logSystemEvent(const std::string& event, bool success);
    
    /**
     * @brief Set current user session
     * 
     * @param userId Current user ID
     * @param sessionId Current session ID
     */
    void setCurrentSession(const std::string& userId, const std::string& sessionId);
    
    /**
     * @brief Enable or disable audit logging
     * 
     * @param enabled Whether to enable audit logging
     */
    void setEnabled(bool enabled) { m_enabled = enabled; }
    
    /**
     * @brief Check if audit logging is enabled
     * 
     * @return true if enabled
     */
    bool isEnabled() const { return m_enabled; }

private:
    AuditLogger() = default;
    
    std::shared_ptr<spdlog::logger> m_logger;
    bool m_enabled = true;
    std::string m_currentUserId;
    std::string m_currentSessionId;
    
    /**
     * @brief Convert audit event type to string
     * 
     * @param type Event type
     * @return String representation
     */
    std::string eventTypeToString(AuditEventType type);
    
    /**
     * @brief Format audit event as JSON
     * 
     * @param event Audit event
     * @return JSON formatted string
     */
    std::string formatEventAsJson(const AuditEvent& event);
    
    /**
     * @brief Get current timestamp as ISO string
     * 
     * @return ISO formatted timestamp
     */
    std::string getCurrentTimestamp();
};

/**
 * @brief Convenience macros for audit logging
 */
#define AUDIT_AUTH_SUCCESS(userId, source) \
    AuditLogger::getInstance().logAuthentication(true, userId, source)

#define AUDIT_AUTH_FAILURE(userId, source) \
    AuditLogger::getInstance().logAuthentication(false, userId, source)

#define AUDIT_CREDENTIAL_ACCESS(userId, type, success) \
    AuditLogger::getInstance().logCredentialAccess(userId, type, success)

#define AUDIT_NETWORK_CONNECTION(dest, success, protocol) \
    AuditLogger::getInstance().logNetworkConnection(dest, success, protocol)

#define AUDIT_ORDER_ACTIVITY(userId, orderId, action, symbol, success) \
    AuditLogger::getInstance().logOrderActivity(userId, orderId, action, symbol, success)

#define AUDIT_CONFIG_ACCESS(userId, path, action, success) \
    AuditLogger::getInstance().logConfigAccess(userId, path, action, success)

#define AUDIT_SUSPICIOUS_ACTIVITY(desc, source, severity) \
    AuditLogger::getInstance().logSuspiciousActivity(desc, source, severity)

#define AUDIT_SYSTEM_EVENT(event, success) \
    AuditLogger::getInstance().logSystemEvent(event, success)

} // namespace utils
} // namespace pinnacle