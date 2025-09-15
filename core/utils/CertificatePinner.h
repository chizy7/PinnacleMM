#pragma once

#include <memory>
#include <openssl/sha.h>
#include <openssl/x509.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace pinnacle {
namespace utils {

/**
 * @class CertificatePinner
 * @brief Certificate pinning implementation for enhanced SSL security
 */
class CertificatePinner {
public:
  /**
   * @brief Pin configuration for a host
   */
  struct PinConfig {
    std::string hostname;
    std::vector<std::string>
        sha256Pins; // Base64-encoded SHA256 hashes of public keys
    bool enforcePin;

    PinConfig() : enforcePin(true) {}

    PinConfig(const std::string& host, bool enforce = true)
        : hostname(host), enforcePin(enforce) {}
  };

  /**
   * @brief Initialize certificate pinner with default exchange pins
   */
  CertificatePinner();

  /**
   * @brief Add certificate pin for a hostname
   *
   * @param hostname Target hostname
   * @param sha256Pin Base64-encoded SHA256 hash of the public key
   * @param enforce Whether to enforce the pin (fail on mismatch)
   */
  void addPin(const std::string& hostname, const std::string& sha256Pin,
              bool enforce = true);

  /**
   * @brief Verify certificate against pinned certificates
   *
   * @param hostname Target hostname
   * @param cert X.509 certificate to verify
   * @return true if certificate is valid (pinned or allowed)
   */
  bool verifyCertificate(const std::string& hostname, X509* cert);

  /**
   * @brief Load certificate pins from configuration file
   *
   * @param configPath Path to certificate pin configuration
   * @return true if loaded successfully
   */
  bool loadPinsFromFile(const std::string& configPath);

  /**
   * @brief Save current pins to configuration file
   *
   * @param configPath Path to save certificate pin configuration
   * @return true if saved successfully
   */
  bool savePinsToFile(const std::string& configPath);

  /**
   * @brief Enable or disable certificate pinning
   *
   * @param enabled Whether to enable pinning
   */
  void setEnabled(bool enabled) { m_enabled = enabled; }

  /**
   * @brief Check if certificate pinning is enabled
   *
   * @return true if enabled
   */
  bool isEnabled() const { return m_enabled; }

  /**
   * @brief Get SHA256 fingerprint of a certificate's public key
   *
   * @param cert X.509 certificate
   * @return Base64-encoded SHA256 hash
   */
  static std::string getCertificateFingerprint(X509* cert);

private:
  std::unordered_map<std::string, PinConfig> m_pins;
  bool m_enabled;

  /**
   * @brief Initialize default certificate pins for major exchanges
   */
  void initializeDefaultPins();

  /**
   * @brief Extract public key from certificate and compute SHA256
   *
   * @param cert X.509 certificate
   * @param hash Output buffer for SHA256 hash (32 bytes)
   * @return true if successful
   */
  bool extractPublicKeyHash(X509* cert,
                            unsigned char hash[SHA256_DIGEST_LENGTH]);

  /**
   * @brief Convert binary hash to Base64 string
   *
   * @param hash Binary hash data
   * @param length Hash length in bytes
   * @return Base64-encoded string
   */
  std::string hashToBase64(const unsigned char* hash, size_t length);

  /**
   * @brief Convert Base64 string to binary hash
   *
   * @param base64 Base64-encoded string
   * @param hash Output buffer
   * @param maxLength Maximum output buffer size
   * @return Number of bytes written
   */
  size_t base64ToHash(const std::string& base64, unsigned char* hash,
                      size_t maxLength);
};

/**
 * @brief SSL context verification callback for certificate pinning
 *
 * @param preverify_ok Pre-verification result from OpenSSL
 * @param ctx SSL context
 * @return 1 if verification passes, 0 otherwise
 */
int certificatePinningCallback(int preverify_ok, X509_STORE_CTX* ctx);

/**
 * @brief Set global certificate pinner instance for callbacks
 *
 * @param pinner Certificate pinner instance
 */
void setGlobalCertificatePinner(std::shared_ptr<CertificatePinner> pinner);

/**
 * @brief Get global certificate pinner instance
 *
 * @return Shared pointer to certificate pinner
 */
std::shared_ptr<CertificatePinner> getGlobalCertificatePinner();

} // namespace utils
} // namespace pinnacle
