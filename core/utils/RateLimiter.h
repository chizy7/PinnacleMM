#pragma once

#include <string>
#include <unordered_map>
#include <chrono>
#include <mutex>
#include <queue>
#include <memory>

namespace pinnacle {
namespace utils {

/**
 * @struct RateLimitConfig
 * @brief Configuration for rate limiting
 */
struct RateLimitConfig {
    size_t maxRequests;
    std::chrono::milliseconds windowSize;
    std::chrono::milliseconds cooldownPeriod;
    bool enforceLimit;
    
    RateLimitConfig(size_t max = 100, 
                   std::chrono::milliseconds window = std::chrono::minutes(1),
                   std::chrono::milliseconds cooldown = std::chrono::seconds(0),
                   bool enforce = true)
        : maxRequests(max), windowSize(window), cooldownPeriod(cooldown), enforceLimit(enforce) {}
};

/**
 * @struct RateLimitResult
 * @brief Result of rate limit check
 */
struct RateLimitResult {
    bool allowed;
    size_t remainingRequests;
    std::chrono::milliseconds retryAfter;
    std::string reason;
    
    RateLimitResult(bool allow = true, size_t remaining = 0, 
                   std::chrono::milliseconds retry = std::chrono::milliseconds(0),
                   const std::string& r = "")
        : allowed(allow), remainingRequests(remaining), retryAfter(retry), reason(r) {}
};

/**
 * @class TokenBucket
 * @brief Token bucket implementation for rate limiting
 */
class TokenBucket {
public:
    TokenBucket(size_t capacity, double refillRate);
    
    /**
     * @brief Try to consume tokens
     * 
     * @param tokens Number of tokens to consume
     * @return true if tokens were consumed successfully
     */
    bool tryConsume(size_t tokens = 1);
    
    /**
     * @brief Get number of available tokens
     * 
     * @return Number of available tokens
     */
    size_t getAvailableTokens();
    
    /**
     * @brief Reset the bucket to full capacity
     */
    void reset();

private:
    size_t m_capacity;
    double m_tokens;
    double m_refillRate;
    std::chrono::steady_clock::time_point m_lastRefill;
    mutable std::mutex m_mutex;
    
    void refill();
};

/**
 * @class SlidingWindowRateLimiter
 * @brief Sliding window rate limiter implementation
 */
class SlidingWindowRateLimiter {
public:
    SlidingWindowRateLimiter(const RateLimitConfig& config);
    
    /**
     * @brief Check if request is allowed
     * 
     * @param key Identifier for the rate limited entity
     * @return Rate limit result
     */
    RateLimitResult checkRequest(const std::string& key);
    
    /**
     * @brief Record a request
     * 
     * @param key Identifier for the rate limited entity
     */
    void recordRequest(const std::string& key);
    
    /**
     * @brief Reset rate limit for a key
     * 
     * @param key Identifier to reset
     */
    void reset(const std::string& key);
    
    /**
     * @brief Clear old entries (cleanup)
     */
    void cleanup();

private:
    struct RequestWindow {
        std::queue<std::chrono::steady_clock::time_point> requests;
        std::chrono::steady_clock::time_point lastRequest;
        std::chrono::steady_clock::time_point cooldownUntil;
    };
    
    RateLimitConfig m_config;
    std::unordered_map<std::string, RequestWindow> m_windows;
    mutable std::mutex m_mutex;
    
    void cleanupWindow(RequestWindow& window);
    bool isInCooldown(const RequestWindow& window);
};

/**
 * @class RateLimiter
 * @brief Main rate limiter with multiple strategies and configurations
 */
class RateLimiter {
public:
    /**
     * @brief Get singleton instance
     */
    static RateLimiter& getInstance();
    
    /**
     * @brief Initialize rate limiter with default configs
     */
    void initialize();
    
    /**
     * @brief Add rate limit configuration
     * 
     * @param category Rate limit category (e.g., "api", "orders", "auth")
     * @param config Rate limit configuration
     */
    void addRateLimit(const std::string& category, const RateLimitConfig& config);
    
    /**
     * @brief Check if request is allowed
     * 
     * @param category Rate limit category
     * @param key Identifier (e.g., user ID, IP address)
     * @return Rate limit result
     */
    RateLimitResult checkRequest(const std::string& category, const std::string& key);
    
    /**
     * @brief Record a successful request
     * 
     * @param category Rate limit category
     * @param key Identifier
     */
    void recordRequest(const std::string& category, const std::string& key);
    
    /**
     * @brief Check and record request in one call
     * 
     * @param category Rate limit category
     * @param key Identifier
     * @return Rate limit result
     */
    RateLimitResult checkAndRecord(const std::string& category, const std::string& key);
    
    /**
     * @brief Reset rate limits for a specific key
     * 
     * @param category Rate limit category
     * @param key Identifier to reset
     */
    void reset(const std::string& category, const std::string& key);
    
    /**
     * @brief Enable or disable rate limiting
     * 
     * @param enabled Whether to enable rate limiting
     */
    void setEnabled(bool enabled) { m_enabled = enabled; }
    
    /**
     * @brief Check if rate limiting is enabled
     * 
     * @return true if enabled
     */
    bool isEnabled() const { return m_enabled; }
    
    /**
     * @brief Perform cleanup of old entries
     */
    void performCleanup();

private:
    RateLimiter() = default;
    
    std::unordered_map<std::string, std::unique_ptr<SlidingWindowRateLimiter>> m_limiters;
    mutable std::mutex m_mutex;
    bool m_enabled = true;
    
    /**
     * @brief Initialize default rate limit configurations
     */
    void initializeDefaults();
};

/**
 * @brief Convenience macros for rate limiting
 */
#define RATE_LIMIT_CHECK(category, key) \
    RateLimiter::getInstance().checkRequest(category, key)

#define RATE_LIMIT_RECORD(category, key) \
    RateLimiter::getInstance().recordRequest(category, key)

#define RATE_LIMIT_CHECK_AND_RECORD(category, key) \
    RateLimiter::getInstance().checkAndRecord(category, key)

/**
 * @brief RAII class for automatic rate limit checking
 */
class RateLimitGuard {
public:
    RateLimitGuard(const std::string& category, const std::string& key);
    ~RateLimitGuard() = default;
    
    bool isAllowed() const { return m_result.allowed; }
    const RateLimitResult& getResult() const { return m_result; }

private:
    RateLimitResult m_result;
};

} // namespace utils
} // namespace pinnacle