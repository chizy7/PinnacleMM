#include "RateLimiter.h"
#include "AuditLogger.h"
#include <algorithm>
#include <spdlog/spdlog.h>

namespace pinnacle {
namespace utils {

// TokenBucket implementation
TokenBucket::TokenBucket(size_t capacity, double refillRate)
    : m_capacity(capacity), m_tokens(capacity), m_refillRate(refillRate),
      m_lastRefill(std::chrono::steady_clock::now()) {}

bool TokenBucket::tryConsume(size_t tokens) {
  std::lock_guard<std::mutex> lock(m_mutex);
  refill();

  if (m_tokens >= tokens) {
    m_tokens -= tokens;
    return true;
  }

  return false;
}

size_t TokenBucket::getAvailableTokens() {
  std::lock_guard<std::mutex> lock(m_mutex);
  refill();
  return static_cast<size_t>(m_tokens);
}

void TokenBucket::reset() {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_tokens = m_capacity;
  m_lastRefill = std::chrono::steady_clock::now();
}

void TokenBucket::refill() {
  auto now = std::chrono::steady_clock::now();
  auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastRefill);

  double tokensToAdd = (elapsed.count() / 1000.0) * m_refillRate;
  m_tokens = std::min(static_cast<double>(m_capacity), m_tokens + tokensToAdd);
  m_lastRefill = now;
}

// SlidingWindowRateLimiter implementation
SlidingWindowRateLimiter::SlidingWindowRateLimiter(
    const RateLimitConfig &config)
    : m_config(config) {}

RateLimitResult SlidingWindowRateLimiter::checkRequest(const std::string &key) {
  std::lock_guard<std::mutex> lock(m_mutex);

  auto &window = m_windows[key];
  cleanupWindow(window);

  // Check if in cooldown period
  if (isInCooldown(window)) {
    auto now = std::chrono::steady_clock::now();
    auto retryAfter = std::chrono::duration_cast<std::chrono::milliseconds>(
        window.cooldownUntil - now);

    return RateLimitResult(false, 0, retryAfter, "In cooldown period");
  }

  // Check rate limit
  if (!m_config.enforceLimit || window.requests.size() < m_config.maxRequests) {
    size_t remaining = m_config.maxRequests - window.requests.size() - 1;
    return RateLimitResult(true, remaining, std::chrono::milliseconds(0), "");
  }

  // Rate limit exceeded
  if (m_config.cooldownPeriod.count() > 0) {
    window.cooldownUntil =
        std::chrono::steady_clock::now() + m_config.cooldownPeriod;
  }

  // Log rate limit violation
  AuditLogger::getInstance().logSuspiciousActivity(
      "Rate limit exceeded for key: " + key, "RateLimiter", "medium");

  return RateLimitResult(false, 0, m_config.cooldownPeriod,
                         "Rate limit exceeded");
}

void SlidingWindowRateLimiter::recordRequest(const std::string &key) {
  std::lock_guard<std::mutex> lock(m_mutex);

  auto now = std::chrono::steady_clock::now();
  auto &window = m_windows[key];

  window.requests.push(now);
  window.lastRequest = now;

  cleanupWindow(window);
}

void SlidingWindowRateLimiter::reset(const std::string &key) {
  std::lock_guard<std::mutex> lock(m_mutex);

  auto it = m_windows.find(key);
  if (it != m_windows.end()) {
    m_windows.erase(it);
  }
}

void SlidingWindowRateLimiter::cleanup() {
  std::lock_guard<std::mutex> lock(m_mutex);

  auto now = std::chrono::steady_clock::now();
  auto cutoff = now - m_config.windowSize - m_config.cooldownPeriod;

  for (auto it = m_windows.begin(); it != m_windows.end();) {
    if (it->second.lastRequest < cutoff) {
      it = m_windows.erase(it);
    } else {
      cleanupWindow(it->second);
      ++it;
    }
  }
}

void SlidingWindowRateLimiter::cleanupWindow(RequestWindow &window) {
  auto now = std::chrono::steady_clock::now();
  auto cutoff = now - m_config.windowSize;

  while (!window.requests.empty() && window.requests.front() < cutoff) {
    window.requests.pop();
  }
}

bool SlidingWindowRateLimiter::isInCooldown(const RequestWindow &window) {
  if (m_config.cooldownPeriod.count() == 0) {
    return false;
  }

  auto now = std::chrono::steady_clock::now();
  return window.cooldownUntil > now;
}

// RateLimiter implementation
RateLimiter &RateLimiter::getInstance() {
  static RateLimiter instance;
  return instance;
}

void RateLimiter::initialize() {
  initializeDefaults();
  spdlog::info("Rate limiter initialized with default configurations");
}

void RateLimiter::addRateLimit(const std::string &category,
                               const RateLimitConfig &config) {
  std::lock_guard<std::mutex> lock(m_mutex);

  m_limiters[category] = std::make_unique<SlidingWindowRateLimiter>(config);

  spdlog::info("Added rate limit for category '{}': {} requests per {}ms",
               category, config.maxRequests, config.windowSize.count());
}

RateLimitResult RateLimiter::checkRequest(const std::string &category,
                                          const std::string &key) {
  if (!m_enabled) {
    return RateLimitResult(true, SIZE_MAX, std::chrono::milliseconds(0),
                           "Rate limiting disabled");
  }

  std::lock_guard<std::mutex> lock(m_mutex);

  auto it = m_limiters.find(category);
  if (it == m_limiters.end()) {
    spdlog::warn("No rate limiter configured for category: {}", category);
    return RateLimitResult(true, SIZE_MAX, std::chrono::milliseconds(0),
                           "No rate limit configured");
  }

  return it->second->checkRequest(key);
}

void RateLimiter::recordRequest(const std::string &category,
                                const std::string &key) {
  if (!m_enabled) {
    return;
  }

  std::lock_guard<std::mutex> lock(m_mutex);

  auto it = m_limiters.find(category);
  if (it != m_limiters.end()) {
    it->second->recordRequest(key);
  }
}

RateLimitResult RateLimiter::checkAndRecord(const std::string &category,
                                            const std::string &key) {
  if (!m_enabled) {
    return RateLimitResult(true, SIZE_MAX, std::chrono::milliseconds(0),
                           "Rate limiting disabled");
  }

  auto result = checkRequest(category, key);
  if (result.allowed) {
    recordRequest(category, key);
  }

  return result;
}

void RateLimiter::reset(const std::string &category, const std::string &key) {
  std::lock_guard<std::mutex> lock(m_mutex);

  auto it = m_limiters.find(category);
  if (it != m_limiters.end()) {
    it->second->reset(key);
  }
}

void RateLimiter::performCleanup() {
  std::lock_guard<std::mutex> lock(m_mutex);

  for (auto &[category, limiter] : m_limiters) {
    limiter->cleanup();
  }

  spdlog::debug("Rate limiter cleanup completed");
}

void RateLimiter::initializeDefaults() {
  // API rate limits
  addRateLimit("api_general", RateLimitConfig(1000, std::chrono::minutes(1),
                                              std::chrono::seconds(60)));
  addRateLimit("api_market_data", RateLimitConfig(5000, std::chrono::minutes(1),
                                                  std::chrono::seconds(30)));

  // Authentication rate limits
  addRateLimit("auth_login", RateLimitConfig(5, std::chrono::minutes(15),
                                             std::chrono::minutes(5)));
  addRateLimit(
      "auth_password_reset",
      RateLimitConfig(3, std::chrono::hours(1), std::chrono::minutes(15)));

  // Trading operation rate limits
  addRateLimit("orders_submit", RateLimitConfig(100, std::chrono::minutes(1),
                                                std::chrono::seconds(30)));
  addRateLimit("orders_cancel", RateLimitConfig(500, std::chrono::minutes(1),
                                                std::chrono::seconds(15)));
  addRateLimit("orders_modify", RateLimitConfig(200, std::chrono::minutes(1),
                                                std::chrono::seconds(30)));

  // Configuration access rate limits
  addRateLimit("config_read", RateLimitConfig(100, std::chrono::minutes(1)));
  addRateLimit("config_write", RateLimitConfig(10, std::chrono::minutes(5),
                                               std::chrono::minutes(1)));

  // Network connection rate limits
  addRateLimit(
      "websocket_connections",
      RateLimitConfig(50, std::chrono::minutes(5), std::chrono::minutes(2)));
  addRateLimit("fix_connections", RateLimitConfig(10, std::chrono::minutes(5),
                                                  std::chrono::minutes(5)));
}

// RateLimitGuard implementation
RateLimitGuard::RateLimitGuard(const std::string &category,
                               const std::string &key)
    : m_result(RateLimiter::getInstance().checkAndRecord(category, key)) {

  if (!m_result.allowed) {
    spdlog::warn("Rate limit exceeded for category '{}', key '{}': {}",
                 category, key, m_result.reason);
  }
}

} // namespace utils
} // namespace pinnacle