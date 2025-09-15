#include "CertificatePinner.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <spdlog/spdlog.h>

namespace pinnacle {
namespace utils {

// Global certificate pinner instance for callbacks
static std::shared_ptr<CertificatePinner> g_certificatePinner;

CertificatePinner::CertificatePinner() : m_enabled(true) {
  initializeDefaultPins();
}

void CertificatePinner::addPin(const std::string& hostname,
                               const std::string& sha256Pin, bool enforce) {
  auto it = m_pins.find(hostname);
  if (it != m_pins.end()) {
    it->second.sha256Pins.push_back(sha256Pin);
    it->second.enforcePin = it->second.enforcePin || enforce;
  } else {
    PinConfig config(hostname, enforce);
    config.sha256Pins.push_back(sha256Pin);
    m_pins[hostname] = config;
  }

  spdlog::info("Added certificate pin for {}, enforce: {}", hostname, enforce);
}

bool CertificatePinner::verifyCertificate(const std::string& hostname,
                                          X509* cert) {
  if (!m_enabled || !cert) {
    return true; // Pass through if disabled or no certificate
  }

  auto it = m_pins.find(hostname);
  if (it == m_pins.end()) {
    // No pins configured for this hostname - allow connection
    spdlog::debug("No certificate pins configured for {}", hostname);
    return true;
  }

  const auto& pinConfig = it->second;

  // Get certificate fingerprint
  std::string certFingerprint = getCertificateFingerprint(cert);
  if (certFingerprint.empty()) {
    spdlog::error("Failed to compute certificate fingerprint for {}", hostname);
    return !pinConfig.enforcePin; // Fail if enforcing, pass if not
  }

  // Check if certificate matches any pinned certificates
  for (const auto& pin : pinConfig.sha256Pins) {
    if (certFingerprint == pin) {
      spdlog::debug("Certificate pin matched for {}", hostname);
      return true;
    }
  }

  // Certificate doesn't match any pins
  if (pinConfig.enforcePin) {
    spdlog::error("Certificate pin verification failed for {}. Expected one of "
                  "{} pins, got: {}",
                  hostname, pinConfig.sha256Pins.size(),
                  certFingerprint.substr(0, 20) + "...");
    return false;
  } else {
    spdlog::warn("Certificate pin mismatch for {} (not enforced)", hostname);
    return true;
  }
}

bool CertificatePinner::loadPinsFromFile(const std::string& configPath) {
  try {
    std::ifstream file(configPath);
    if (!file.is_open()) {
      spdlog::warn("Certificate pin configuration file not found: {}",
                   configPath);
      return false;
    }

    nlohmann::json config;
    file >> config;

    if (!config.contains("certificate_pins") ||
        !config["certificate_pins"].is_object()) {
      spdlog::error("Invalid certificate pin configuration format");
      return false;
    }

    for (const auto& [hostname, pinData] : config["certificate_pins"].items()) {
      if (!pinData.contains("pins") || !pinData["pins"].is_array()) {
        spdlog::warn("Invalid pin data for hostname: {}", hostname);
        continue;
      }

      bool enforce = pinData.value("enforce", true);

      for (const auto& pin : pinData["pins"]) {
        if (pin.is_string()) {
          addPin(hostname, pin.get<std::string>(), enforce);
        }
      }
    }

    m_enabled = config.value("enabled", true);

    spdlog::info("Loaded certificate pins from: {}", configPath);
    return true;

  } catch (const std::exception& e) {
    spdlog::error("Failed to load certificate pins: {}", e.what());
    return false;
  }
}

bool CertificatePinner::savePinsToFile(const std::string& configPath) {
  try {
    nlohmann::json config;
    config["enabled"] = m_enabled;
    config["certificate_pins"] = nlohmann::json::object();

    for (const auto& [hostname, pinConfig] : m_pins) {
      nlohmann::json hostData;
      hostData["enforce"] = pinConfig.enforcePin;
      hostData["pins"] = pinConfig.sha256Pins;

      config["certificate_pins"][hostname] = hostData;
    }

    std::ofstream file(configPath);
    if (!file.is_open()) {
      spdlog::error(
          "Failed to open certificate pin config file for writing: {}",
          configPath);
      return false;
    }

    file << config.dump(4);

    spdlog::info("Saved certificate pins to: {}", configPath);
    return true;

  } catch (const std::exception& e) {
    spdlog::error("Failed to save certificate pins: {}", e.what());
    return false;
  }
}

std::string CertificatePinner::getCertificateFingerprint(X509* cert) {
  unsigned char hash[SHA256_DIGEST_LENGTH];

  CertificatePinner pinner;
  if (!pinner.extractPublicKeyHash(cert, hash)) {
    return "";
  }

  return pinner.hashToBase64(hash, SHA256_DIGEST_LENGTH);
}

void CertificatePinner::initializeDefaultPins() {
  // Coinbase Pro pins (note to self:just an example - these should be updated
  // with real pins)
  addPin("ws-feed.exchange.coinbase.com",
         "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=", false);

  // Kraken pins
  addPin("ws.kraken.com", "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB=", false);

  // Binance pins
  addPin("stream.binance.com",
         "CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC=", false);

  // TODO and note to remind myself to update these pins
  // Self Note: In production, these should be real certificate pins
  // obtained by connecting to the services and extracting public key hashes
  spdlog::info("Initialized default certificate pins (example pins - update "
               "for production)");
}

bool CertificatePinner::extractPublicKeyHash(
    X509* cert, unsigned char hash[SHA256_DIGEST_LENGTH]) {
  EVP_PKEY* pubkey = X509_get_pubkey(cert);
  if (!pubkey) {
    return false;
  }

  // Extract DER-encoded public key
  unsigned char* pubkey_der = nullptr;
  int pubkey_der_len = i2d_PUBKEY(pubkey, &pubkey_der);

  EVP_PKEY_free(pubkey);

  if (pubkey_der_len <= 0 || !pubkey_der) {
    return false;
  }

  // Compute SHA256 hash of the public key
  SHA256(pubkey_der, pubkey_der_len, hash);

  OPENSSL_free(pubkey_der);
  return true;
}

std::string CertificatePinner::hashToBase64(const unsigned char* hash,
                                            size_t length) {
  BIO* bio = BIO_new(BIO_s_mem());
  BIO* b64 = BIO_new(BIO_f_base64());
  BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
  bio = BIO_push(b64, bio);

  BIO_write(bio, hash, length);
  BIO_flush(bio);

  BUF_MEM* bufferPtr;
  BIO_get_mem_ptr(bio, &bufferPtr);

  std::string result(bufferPtr->data, bufferPtr->length);
  BIO_free_all(bio);

  return result;
}

size_t CertificatePinner::base64ToHash(const std::string& base64,
                                       unsigned char* hash, size_t maxLength) {
  BIO* bio = BIO_new_mem_buf(base64.c_str(), base64.length());
  BIO* b64 = BIO_new(BIO_f_base64());
  BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
  bio = BIO_push(b64, bio);

  int length = BIO_read(bio, hash, maxLength);
  BIO_free_all(bio);

  return (length > 0) ? length : 0;
}

// Global functions
void setGlobalCertificatePinner(std::shared_ptr<CertificatePinner> pinner) {
  g_certificatePinner = pinner;
}

std::shared_ptr<CertificatePinner> getGlobalCertificatePinner() {
  return g_certificatePinner;
}

int certificatePinningCallback(int preverify_ok, X509_STORE_CTX* ctx) {
  // Get the certificate being verified
  X509* cert = X509_STORE_CTX_get_current_cert(ctx);
  if (!cert) {
    return preverify_ok;
  }

  // Only check end-entity certificates (not CA certificates)
  int depth = X509_STORE_CTX_get_error_depth(ctx);
  if (depth != 0) {
    return preverify_ok;
  }

  auto pinner = getGlobalCertificatePinner();
  if (!pinner) {
    return preverify_ok;
  }

  // Get the hostname from somewhere (this would need to be set in context)
  // For now, we'll skip hostname-specific verification in the callback
  // and rely on manual verification in the WebSocket connection code

  return preverify_ok;
}

} // namespace utils
} // namespace pinnacle
