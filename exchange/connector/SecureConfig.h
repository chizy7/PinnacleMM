#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace pinnacle {
namespace utils {

/**
 * @class SecureConfig
 * @brief Manages secure storage and retrieval of sensitive configuration data
 *
 * This class provides encrypted storage for API keys and other sensitive data
 * with secure memory handling to minimize exposure of credentials in memory.
 */
class SecureConfig {
public:
  /**
   * @brief Constructor
   */
  SecureConfig();

  /**
   * @brief Destructor
   */
  ~SecureConfig();

  /**
   * @brief Load encrypted configuration from file
   *
   * @param filename Path to the encrypted configuration file
   * @param masterPassword Password to decrypt the configuration
   * @return true if loaded successfully, false otherwise
   */
  bool loadFromFile(const std::string &filename,
                    const std::string &masterPassword);

  /**
   * @brief Save current configuration to an encrypted file
   *
   * @param filename Path to save the encrypted configuration file
   * @param masterPassword Password to encrypt the configuration
   * @return true if saved successfully, false otherwise
   */
  bool saveToFile(const std::string &filename,
                  const std::string &masterPassword);

  /**
   * @brief Set a configuration value
   *
   * @param key Configuration key
   * @param value Configuration value
   */
  void setValue(const std::string &key, const std::string &value);

  /**
   * @brief Get a configuration value
   *
   * @param key Configuration key
   * @return Configuration value, or empty optional if not found
   */
  std::optional<std::string> getValue(const std::string &key) const;

  /**
   * @brief Check if a configuration key exists
   *
   * @param key Configuration key to check
   * @return true if key exists, false otherwise
   */
  bool hasKey(const std::string &key) const;

  /**
   * @brief Remove a configuration key
   *
   * @param key Configuration key to remove
   * @return true if key was removed, false if key didn't exist
   */
  bool removeKey(const std::string &key);

  /**
   * @brief Clear all configuration values
   */
  void clear();

  /**
   * @brief Check if the configuration has been modified since load/save
   *
   * @return true if modified, false otherwise
   */
  bool isModified() const;

private:
  // Struct representing a secure entry
  struct SecureEntry {
    std::string value;
    bool sensitive;
  };

  // Internal storage
  std::unordered_map<std::string, SecureEntry> m_entries;

  // Track modification state
  bool m_modified;

  // Thread safety
  mutable std::mutex m_mutex;

  // Encryption/Decryption helpers
  std::string encryptValue(const std::string &value,
                           const std::string &password,
                           const std::vector<unsigned char> &salt) const;
  std::string decryptValue(const std::string &encryptedValue,
                           const std::string &password,
                           const std::vector<unsigned char> &salt) const;

  // File format helpers
  bool writeEncryptedJson(const std::string &filename,
                          const std::string &encryptedData,
                          const std::vector<unsigned char> &salt) const;
  std::optional<std::pair<std::string, std::vector<unsigned char>>>
  readEncryptedJson(const std::string &filename) const;

  // Generate secure encryption key from password
  std::vector<unsigned char>
  deriveKeyFromPassword(const std::string &password,
                        const std::vector<unsigned char> &salt) const;

  // Generate random salt
  std::vector<unsigned char> generateSalt() const;

  // Secure memory clearing
  void secureMemzero(void *ptr, size_t len) const;
};

/**
 * @class ApiCredentials
 * @brief Helper class for managing exchange API credentials
 *
 * This class provides a convenient interface for accessing and managing
 * exchange-specific API credentials stored in the SecureConfig.
 */
class ApiCredentials {
public:
  /**
   * @brief Constructor
   *
   * @param config Reference to SecureConfig instance
   */
  explicit ApiCredentials(SecureConfig &config);

  /**
   * @brief Set API credentials for an exchange
   *
   * @param exchange Exchange name (e.g., "coinbase", "kraken")
   * @param apiKey API key
   * @param apiSecret API secret
   * @param passphrase Optional passphrase (if required by exchange)
   * @return true if successfully set, false otherwise
   */
  bool
  setCredentials(const std::string &exchange, const std::string &apiKey,
                 const std::string &apiSecret,
                 const std::optional<std::string> &passphrase = std::nullopt);

  /**
   * @brief Get API key for an exchange
   *
   * @param exchange Exchange name
   * @return API key, or empty optional if not found
   */
  std::optional<std::string> getApiKey(const std::string &exchange) const;

  /**
   * @brief Get API secret for an exchange
   *
   * @param exchange Exchange name
   * @return API secret, or empty optional if not found
   */
  std::optional<std::string> getApiSecret(const std::string &exchange) const;

  /**
   * @brief Get passphrase for an exchange
   *
   * @param exchange Exchange name
   * @return Passphrase, or empty optional if not found
   */
  std::optional<std::string> getPassphrase(const std::string &exchange) const;

  /**
   * @brief Check if credentials exist for an exchange
   *
   * @param exchange Exchange name
   * @return true if credentials exist, false otherwise
   */
  bool hasCredentials(const std::string &exchange) const;

  /**
   * @brief Remove credentials for an exchange
   *
   * @param exchange Exchange name
   * @return true if credentials were removed, false otherwise
   */
  bool removeCredentials(const std::string &exchange);

public:
  // Reference to config instance
  SecureConfig &m_config;

  // Key prefix for API credentials
  static const std::string API_KEY_PREFIX;
};

} // namespace utils
} // namespace pinnacle