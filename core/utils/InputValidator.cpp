#include "InputValidator.h"
#include <algorithm>
#include <cctype>
#include <limits>
#include <nlohmann/json.hpp>
#include <boost/filesystem.hpp>
#include <spdlog/spdlog.h>

namespace pinnacle {
namespace utils {

// Security patterns initialization
const std::vector<std::regex> InputValidator::SQL_INJECTION_PATTERNS = {
    std::regex(R"(\b(union|select|insert|update|delete|drop|alter|exec|execute)\b)", std::regex_constants::icase),
    std::regex(R"((\s|^)or\s+\d+\s*=\s*\d+)", std::regex_constants::icase),
    std::regex(R"((\s|^)and\s+\d+\s*=\s*\d+)", std::regex_constants::icase),
    std::regex(R"('(\s|;|$))", std::regex_constants::icase),
    std::regex(R"(--.*$)", std::regex_constants::icase),
    std::regex(R"(/\*.*\*/)", std::regex_constants::icase)
};

const std::vector<std::regex> InputValidator::XSS_PATTERNS = {
    std::regex(R"(<script\b[^<]*(?:(?!<\/script>)<[^<]*)*<\/script>)", std::regex_constants::icase),
    std::regex(R"(javascript:)", std::regex_constants::icase),
    std::regex(R"(on\w+\s*=)", std::regex_constants::icase),
    std::regex(R"(<iframe\b)", std::regex_constants::icase),
    std::regex(R"(<object\b)", std::regex_constants::icase),
    std::regex(R"(<embed\b)", std::regex_constants::icase)
};

const std::vector<std::regex> InputValidator::PATH_TRAVERSAL_PATTERNS = {
    std::regex(R"(\.\./)", std::regex_constants::icase),
    std::regex(R"(\.\.\\)", std::regex_constants::icase),
    std::regex(R"(%2e%2e%2f)", std::regex_constants::icase),
    std::regex(R"(%252e%252e%252f)", std::regex_constants::icase),
    std::regex(R"(\.\.%2f)", std::regex_constants::icase)
};

ValidationResult InputValidator::validateString(const std::string& input, 
                                               size_t minLength, 
                                               size_t maxLength,
                                               const std::string& allowedChars) {
    if (input.length() < minLength) {
        return ValidationResult(false, "String too short (minimum " + std::to_string(minLength) + " characters)");
    }
    
    if (input.length() > maxLength) {
        return ValidationResult(false, "String too long (maximum " + std::to_string(maxLength) + " characters)");
    }
    
    if (!allowedChars.empty() && !containsOnlyAllowedChars(input, allowedChars)) {
        return ValidationResult(false, "String contains invalid characters");
    }
    
    return ValidationResult(true);
}

ValidationResult InputValidator::validateDouble(const std::string& input, double minValue, double maxValue) {
    try {
        double value = std::stod(input);
        
        if (std::isnan(value) || std::isinf(value)) {
            return ValidationResult(false, "Invalid numeric value");
        }
        
        if (value < minValue || value > maxValue) {
            return ValidationResult(false, "Value out of range [" + 
                                  std::to_string(minValue) + ", " + std::to_string(maxValue) + "]");
        }
        
        return ValidationResult(true);
    } catch (const std::exception&) {
        return ValidationResult(false, "Invalid numeric format");
    }
}

ValidationResult InputValidator::validateInteger(const std::string& input, int minValue, int maxValue) {
    try {
        int value = std::stoi(input);
        
        if (value < minValue || value > maxValue) {
            return ValidationResult(false, "Value out of range [" + 
                                  std::to_string(minValue) + ", " + std::to_string(maxValue) + "]");
        }
        
        return ValidationResult(true);
    } catch (const std::exception&) {
        return ValidationResult(false, "Invalid integer format");
    }
}

ValidationResult InputValidator::validateSymbol(const std::string& symbol) {
    if (symbol.empty()) {
        return ValidationResult(false, "Symbol cannot be empty");
    }
    
    if (symbol.length() > 20) {
        return ValidationResult(false, "Symbol too long (maximum 20 characters)");
    }
    
    for (char c : symbol) {
        if (!isValidSymbolChar(c)) {
            return ValidationResult(false, "Symbol contains invalid character: '" + std::string(1, c) + "'");
        }
    }
    
    return ValidationResult(true);
}

ValidationResult InputValidator::validatePrice(double price) {
    if (price < 0) {
        return ValidationResult(false, "Price cannot be negative");
    }
    
    if (std::isnan(price) || std::isinf(price)) {
        return ValidationResult(false, "Invalid price value");
    }
    
    if (price > 1000000000.0) { // 1 billion limit
        return ValidationResult(false, "Price too high");
    }
    
    return ValidationResult(true);
}

ValidationResult InputValidator::validateQuantity(double quantity) {
    if (quantity <= 0) {
        return ValidationResult(false, "Quantity must be positive");
    }
    
    if (std::isnan(quantity) || std::isinf(quantity)) {
        return ValidationResult(false, "Invalid quantity value");
    }
    
    if (quantity > 1000000000.0) { // 1 billion limit
        return ValidationResult(false, "Quantity too high");
    }
    
    return ValidationResult(true);
}

ValidationResult InputValidator::validateOrderId(const std::string& orderId) {
    if (orderId.empty()) {
        return ValidationResult(false, "Order ID cannot be empty");
    }
    
    if (orderId.length() > 64) {
        return ValidationResult(false, "Order ID too long (maximum 64 characters)");
    }
    
    // Allow alphanumeric, hyphens, and underscores
    for (char c : orderId) {
        if (!std::isalnum(c) && c != '-' && c != '_') {
            return ValidationResult(false, "Order ID contains invalid character: '" + std::string(1, c) + "'");
        }
    }
    
    return ValidationResult(true);
}

ValidationResult InputValidator::validateUrl(const std::string& url) {
    if (url.empty()) {
        return ValidationResult(false, "URL cannot be empty");
    }
    
    if (url.length() > 2048) {
        return ValidationResult(false, "URL too long");
    }
    
    // Basic URL validation
    std::regex urlPattern(R"(^https?:\/\/[a-zA-Z0-9\-\.]+\.[a-zA-Z]{2,}(\/.*)?$)");
    if (!std::regex_match(url, urlPattern)) {
        return ValidationResult(false, "Invalid URL format");
    }
    
    return ValidationResult(true);
}

ValidationResult InputValidator::validateIPAddress(const std::string& ip) {
    std::regex ipPattern(R"(^(?:[0-9]{1,3}\.){3}[0-9]{1,3}$)");
    if (!std::regex_match(ip, ipPattern)) {
        return ValidationResult(false, "Invalid IP address format");
    }
    
    // Validate octets
    size_t pos = 0;
    for (int i = 0; i < 4; ++i) {
        size_t dotPos = ip.find('.', pos);
        if (i == 3) dotPos = ip.length();
        
        std::string octet = ip.substr(pos, dotPos - pos);
        int value = std::stoi(octet);
        if (value < 0 || value > 255) {
            return ValidationResult(false, "IP address octet out of range");
        }
        
        pos = dotPos + 1;
    }
    
    return ValidationResult(true);
}

ValidationResult InputValidator::validatePort(int port) {
    if (port < 1 || port > 65535) {
        return ValidationResult(false, "Port out of range (1-65535)");
    }
    
    return ValidationResult(true);
}

ValidationResult InputValidator::validateApiKey(const std::string& apiKey) {
    if (apiKey.empty()) {
        return ValidationResult(false, "API key cannot be empty");
    }
    
    if (apiKey.length() < 8) {
        return ValidationResult(false, "API key too short");
    }
    
    if (apiKey.length() > 256) {
        return ValidationResult(false, "API key too long");
    }
    
    // Check for basic format (alphanumeric + some special chars)
    for (char c : apiKey) {
        if (!std::isalnum(c) && c != '-' && c != '_' && c != '.' && c != '=') {
            return ValidationResult(false, "API key contains invalid character");
        }
    }
    
    return ValidationResult(true);
}

ValidationResult InputValidator::validateExchangeName(const std::string& exchange) {
    auto supportedExchanges = getSupportedExchanges();
    if (supportedExchanges.find(exchange) == supportedExchanges.end()) {
        return ValidationResult(false, "Unsupported exchange: " + exchange);
    }
    
    return ValidationResult(true);
}

ValidationResult InputValidator::validateJson(const std::string& jsonStr) {
    try {
        auto parsed = nlohmann::json::parse(jsonStr);
        (void)parsed; // Suppress unused variable warning
        return ValidationResult(true);
    } catch (const std::exception& e) {
        return ValidationResult(false, "Invalid JSON: " + std::string(e.what()));
    }
}

ValidationResult InputValidator::validateFilePath(const std::string& path) {
    if (path.empty()) {
        return ValidationResult(false, "File path cannot be empty");
    }
    
    // Check for path traversal
    auto traversalResult = checkForPathTraversal(path);
    if (!traversalResult.valid) {
        return traversalResult;
    }
    
    try {
        boost::filesystem::path p(path);
        if (!p.is_absolute() && path[0] != '.') {
            // Allow relative paths starting with ./
            return ValidationResult(false, "Path must be absolute or start with ./");
        }
        
        return ValidationResult(true);
    } catch (const std::exception& e) {
        return ValidationResult(false, "Invalid file path: " + std::string(e.what()));
    }
}

ValidationResult InputValidator::validateConfigPath(const std::string& path) {
    auto fileResult = validateFilePath(path);
    if (!fileResult.valid) {
        return fileResult;
    }
    
    // Additional checks for config files
    auto hasExtension = [&path](const std::string& ext) {
        return path.length() >= ext.length() && 
               path.substr(path.length() - ext.length()) == ext;
    };
    
    if (!hasExtension(".json") && !hasExtension(".conf") && !hasExtension(".config")) {
        return ValidationResult(false, "Config file must have .json, .conf, or .config extension");
    }
    
    return ValidationResult(true);
}

std::string InputValidator::sanitizeString(const std::string& input) {
    std::string result;
    result.reserve(input.length());
    
    for (char c : input) {
        if (std::isprint(c) && c != '<' && c != '>' && c != '&' && c != '"' && c != '\'') {
            result += c;
        } else {
            result += '_'; // Replace suspicious characters
        }
    }
    
    return result;
}

std::string InputValidator::sanitizeFilePath(const std::string& path) {
    std::string result = path;
    
    // Remove any path traversal attempts
    result = std::regex_replace(result, std::regex(R"(\.\./?)"), "");
    result = std::regex_replace(result, std::regex(R"(\.\.\\?)"), "");
    
    // Remove null bytes
    result.erase(std::remove(result.begin(), result.end(), '\0'), result.end());
    
    return result;
}

std::string InputValidator::sanitizeSymbol(const std::string& symbol) {
    std::string result;
    result.reserve(symbol.length());
    
    for (char c : symbol) {
        if (isValidSymbolChar(c)) {
            result += std::toupper(c);
        }
    }
    
    return result;
}

ValidationResult InputValidator::validatePassword(const std::string& password) {
    if (password.length() < 8) {
        return ValidationResult(false, "Password must be at least 8 characters long");
    }
    
    if (password.length() > 128) {
        return ValidationResult(false, "Password too long (maximum 128 characters)");
    }
    
    bool hasUpper = false, hasLower = false, hasDigit = false, hasSpecial = false;
    
    for (char c : password) {
        if (std::isupper(c)) hasUpper = true;
        else if (std::islower(c)) hasLower = true;
        else if (std::isdigit(c)) hasDigit = true;
        else if (std::ispunct(c)) hasSpecial = true;
    }
    
    if (!(hasUpper && hasLower && hasDigit && hasSpecial)) {
        return ValidationResult(false, "Password must contain uppercase, lowercase, digit, and special character");
    }
    
    return ValidationResult(true);
}

ValidationResult InputValidator::checkForSqlInjection(const std::string& input) {
    for (const auto& pattern : SQL_INJECTION_PATTERNS) {
        if (std::regex_search(input, pattern)) {
            spdlog::warn("Potential SQL injection attempt detected: {}", input.substr(0, 50));
            return ValidationResult(false, "Input contains potentially malicious SQL patterns");
        }
    }
    
    return ValidationResult(true);
}

ValidationResult InputValidator::checkForXss(const std::string& input) {
    for (const auto& pattern : XSS_PATTERNS) {
        if (std::regex_search(input, pattern)) {
            spdlog::warn("Potential XSS attempt detected: {}", input.substr(0, 50));
            return ValidationResult(false, "Input contains potentially malicious script patterns");
        }
    }
    
    return ValidationResult(true);
}

ValidationResult InputValidator::checkForPathTraversal(const std::string& path) {
    for (const auto& pattern : PATH_TRAVERSAL_PATTERNS) {
        if (std::regex_search(path, pattern)) {
            spdlog::warn("Potential path traversal attempt detected: {}", path);
            return ValidationResult(false, "Path contains potential traversal patterns");
        }
    }
    
    return ValidationResult(true);
}

// Private helper functions
bool InputValidator::containsOnlyAllowedChars(const std::string& input, const std::string& allowed) {
    for (char c : input) {
        if (allowed.find(c) == std::string::npos) {
            return false;
        }
    }
    return true;
}

bool InputValidator::isValidSymbolChar(char c) {
    return std::isalnum(c) || c == '-' || c == '_' || c == '/';
}

std::unordered_set<std::string> InputValidator::getSupportedExchanges() {
    return {
        "coinbase", "kraken", "binance", "bitstamp", "gemini",
        "interactive_brokers", "oanda", "fxcm", "simulator"
    };
}

} // namespace utils
} // namespace pinnacle