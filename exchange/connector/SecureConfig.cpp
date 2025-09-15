#include "SecureConfig.h"
#include "core/utils/TimeUtils.h"

#include <atomic>
#include <boost/filesystem.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <random>
#include <sstream>

namespace pinnacle {
namespace utils {

// Define API credentials key prefix
const std::string ApiCredentials::API_KEY_PREFIX = "api.credentials.";

// SecureConfig implementation
SecureConfig::SecureConfig() : m_modified(false) {
  // Initialize OpenSSL
  OpenSSL_add_all_algorithms();
}

SecureConfig::~SecureConfig() {
  // Clean up sensitive data in memory
  clear();

  // Clean up OpenSSL
  EVP_cleanup();
}

bool SecureConfig::loadFromFile(const std::string& filename,
                                const std::string& masterPassword) {
  std::lock_guard<std::mutex> lock(m_mutex);

  // Check if file exists
  if (!boost::filesystem::exists(filename)) {
    return false;
  }

  // Read encrypted JSON data
  auto encryptedDataWithSalt = readEncryptedJson(filename);
  if (!encryptedDataWithSalt) {
    return false;
  }

  const auto& encryptedData = encryptedDataWithSalt->first;
  const auto& salt = encryptedDataWithSalt->second;

  // Decrypt data
  std::string decryptedData;
  try {
    decryptedData = decryptValue(encryptedData, masterPassword, salt);
  } catch (const std::exception&) {
    return false;
  }

  // Parse JSON
  try {
    nlohmann::json json = nlohmann::json::parse(decryptedData);

    // Clear existing entries
    m_entries.clear();

    // Load entries
    for (auto it = json.begin(); it != json.end(); ++it) {
      const std::string& key = it.key();

      // Check if this is a sensitive entry
      bool sensitive = false;
      if (key.find(ApiCredentials::API_KEY_PREFIX) == 0) {
        sensitive = true;
      }

      SecureEntry entry;
      entry.value = it.value().get<std::string>();
      entry.sensitive = sensitive;

      m_entries[key] = entry;
    }

    m_modified = false;
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

bool SecureConfig::saveToFile(const std::string& filename,
                              const std::string& masterPassword) {
  std::lock_guard<std::mutex> lock(m_mutex);

  // Create JSON
  nlohmann::json json;

  // Add entries
  for (const auto& pair : m_entries) {
    json[pair.first] = pair.second.value;
  }

  // Convert to string
  std::string jsonStr = json.dump();

  // Generate salt and encrypt data
  auto salt = generateSalt();
  std::string encryptedData;
  try {
    encryptedData = encryptValue(jsonStr, masterPassword, salt);
  } catch (const std::exception&) {
    return false;
  }

  // Write to file
  if (!writeEncryptedJson(filename, encryptedData, salt)) {
    return false;
  }

  m_modified = false;
  return true;
}

void SecureConfig::setValue(const std::string& key, const std::string& value) {
  std::lock_guard<std::mutex> lock(m_mutex);

  // Check if this is a sensitive entry
  bool sensitive = false;
  if (key.find(ApiCredentials::API_KEY_PREFIX) == 0) {
    sensitive = true;
  }

  SecureEntry entry;
  entry.value = value;
  entry.sensitive = sensitive;

  m_entries[key] = entry;
  m_modified = true;
}

std::optional<std::string>
SecureConfig::getValue(const std::string& key) const {
  std::lock_guard<std::mutex> lock(m_mutex);

  auto it = m_entries.find(key);
  if (it != m_entries.end()) {
    return it->second.value;
  }

  return std::nullopt;
}

bool SecureConfig::hasKey(const std::string& key) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_entries.find(key) != m_entries.end();
}

bool SecureConfig::removeKey(const std::string& key) {
  std::lock_guard<std::mutex> lock(m_mutex);

  auto it = m_entries.find(key);
  if (it != m_entries.end()) {
    m_entries.erase(it);
    m_modified = true;
    return true;
  }

  return false;
}

void SecureConfig::clear() {
  std::lock_guard<std::mutex> lock(m_mutex);

  // Securely clear memory for sensitive entries
  for (auto& pair : m_entries) {
    if (pair.second.sensitive) {
      // Secure overwrite with zeros
      secureMemzero(&pair.second.value[0], pair.second.value.size());
    }
  }

  m_entries.clear();
  m_modified = true;
}

bool SecureConfig::isModified() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_modified;
}

std::string
SecureConfig::encryptValue(const std::string& value,
                           const std::string& password,
                           const std::vector<unsigned char>& salt) const {
  // Generate a random 16-byte IV
  unsigned char iv[16];
  if (RAND_bytes(iv, sizeof(iv)) != 1) {
    throw std::runtime_error("Failed to generate random IV");
  }

  // Derive a 32-byte key from the password
  auto key = deriveKeyFromPassword(password, salt);

  // Create and initialize the context
  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  if (!ctx) {
    throw std::runtime_error("Failed to create encryption context");
  }

  // Initialize the encryption operation
  if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key.data(), iv) !=
      1) {
    EVP_CIPHER_CTX_free(ctx);
    throw std::runtime_error("Failed to initialize encryption");
  }

  // Allocate memory for the ciphertext
  std::vector<unsigned char> ciphertext(
      value.size() + EVP_CIPHER_block_size(EVP_aes_256_cbc()));
  int ciphertext_len = 0;

  // Encrypt the plaintext
  if (EVP_EncryptUpdate(ctx, ciphertext.data(), &ciphertext_len,
                        reinterpret_cast<const unsigned char*>(value.data()),
                        static_cast<int>(value.size())) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    throw std::runtime_error("Failed to encrypt data");
  }

  // Finalize the encryption
  int final_len = 0;
  if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + ciphertext_len,
                          &final_len) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    throw std::runtime_error("Failed to finalize encryption");
  }

  // Update the ciphertext length
  ciphertext_len += final_len;

  // Clean up
  EVP_CIPHER_CTX_free(ctx);

  // Combine IV and ciphertext
  std::vector<unsigned char> result(iv, iv + sizeof(iv));
  result.insert(result.end(), ciphertext.begin(),
                ciphertext.begin() + ciphertext_len);

  // Convert to Base64
  static const std::string base64_chars =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  std::string base64;
  int i = 0;
  unsigned char char_array_3[3];
  unsigned char char_array_4[4];

  for (const auto& byte : result) {
    char_array_3[i++] = byte;
    if (i == 3) {
      char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
      char_array_4[1] =
          ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
      char_array_4[2] =
          ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
      char_array_4[3] = char_array_3[2] & 0x3f;

      for (i = 0; i < 4; i++) {
        base64 += base64_chars[char_array_4[i]];
      }
      i = 0;
    }
  }

  if (i) {
    for (int j = i; j < 3; j++) {
      char_array_3[j] = '\0';
    }

    char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
    char_array_4[1] =
        ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
    char_array_4[2] =
        ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);

    for (int j = 0; j < i + 1; j++) {
      base64 += base64_chars[char_array_4[j]];
    }

    while (i++ < 3) {
      base64 += '=';
    }
  }

  return base64;
}

std::string
SecureConfig::decryptValue(const std::string& encryptedValue,
                           const std::string& password,
                           const std::vector<unsigned char>& salt) const {
  // Decode Base64
  static const std::string base64_chars =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  std::vector<unsigned char> data;
  int in_len = static_cast<int>(encryptedValue.size());
  int i = 0;
  int in_ = 0;
  unsigned char char_array_4[4], char_array_3[3];

  while (in_len-- && (encryptedValue[in_] != '=') &&
         (isalnum(encryptedValue[in_]) || (encryptedValue[in_] == '+') ||
          (encryptedValue[in_] == '/'))) {
    char_array_4[i++] = encryptedValue[in_];
    in_++;
    if (i == 4) {
      for (i = 0; i < 4; i++) {
        char_array_4[i] =
            static_cast<unsigned char>(base64_chars.find(char_array_4[i]));
      }

      char_array_3[0] =
          (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
      char_array_3[1] =
          ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
      char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

      for (i = 0; i < 3; i++) {
        data.push_back(char_array_3[i]);
      }
      i = 0;
    }
  }

  if (i) {
    for (int j = i; j < 4; j++) {
      char_array_4[j] = 0;
    }

    for (int j = 0; j < 4; j++) {
      char_array_4[j] =
          static_cast<unsigned char>(base64_chars.find(char_array_4[j]));
    }

    char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
    char_array_3[1] =
        ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
    char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

    for (int j = 0; j < i - 1; j++) {
      data.push_back(char_array_3[j]);
    }
  }

  // Extract IV (first 16 bytes)
  if (data.size() < 16) {
    throw std::runtime_error("Invalid encrypted data");
  }

  unsigned char iv[16];
  std::copy(data.begin(), data.begin() + 16, iv);

  // Extract ciphertext
  std::vector<unsigned char> ciphertext(data.begin() + 16, data.end());

  // Derive key from password
  auto key = deriveKeyFromPassword(password, salt);

  // Create and initialize the context
  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  if (!ctx) {
    throw std::runtime_error("Failed to create decryption context");
  }

  // Initialize the decryption operation
  if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key.data(), iv) !=
      1) {
    EVP_CIPHER_CTX_free(ctx);
    throw std::runtime_error("Failed to initialize decryption");
  }

  // Allocate memory for the plaintext
  std::vector<unsigned char> plaintext(ciphertext.size());
  int plaintext_len = 0;

  // Decrypt the ciphertext
  if (EVP_DecryptUpdate(ctx, plaintext.data(), &plaintext_len,
                        ciphertext.data(),
                        static_cast<int>(ciphertext.size())) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    throw std::runtime_error("Failed to decrypt data");
  }

  // Finalize the decryption
  int final_len = 0;
  if (EVP_DecryptFinal_ex(ctx, plaintext.data() + plaintext_len, &final_len) !=
      1) {
    EVP_CIPHER_CTX_free(ctx);
    throw std::runtime_error("Failed to finalize decryption");
  }

  // Update the plaintext length
  plaintext_len += final_len;

  // Clean up
  EVP_CIPHER_CTX_free(ctx);

  // Convert to string
  return std::string(reinterpret_cast<char*>(plaintext.data()), plaintext_len);
}

bool SecureConfig::writeEncryptedJson(
    const std::string& filename, const std::string& encryptedData,
    const std::vector<unsigned char>& salt) const {
  try {
    // Create JSON object
    nlohmann::json json;

    // Add timestamp
    json["timestamp"] = utils::TimeUtils::getCurrentISOTimestamp();

    // Add encrypted data and salt
    json["data"] = encryptedData;

    // Encode salt as base64 for JSON storage
    std::string saltBase64;
    static const std::string base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    int i = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    for (const auto& byte : salt) {
      char_array_3[i++] = byte;
      if (i == 3) {
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] =
            ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] =
            ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;

        for (i = 0; i < 4; i++) {
          saltBase64 += base64_chars[char_array_4[i]];
        }
        i = 0;
      }
    }

    if (i) {
      for (int j = i; j < 3; j++) {
        char_array_3[j] = '\0';
      }

      char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
      char_array_4[1] =
          ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
      char_array_4[2] =
          ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);

      for (int j = 0; j < i + 1; j++) {
        saltBase64 += base64_chars[char_array_4[j]];
      }

      while (i++ < 3) {
        saltBase64 += '=';
      }
    }

    json["salt"] = saltBase64;

    // Write to file
    std::ofstream file(filename);
    if (!file.is_open()) {
      return false;
    }

    file << json.dump(4); // Pretty print with 4-space indent
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

std::optional<std::pair<std::string, std::vector<unsigned char>>>
SecureConfig::readEncryptedJson(const std::string& filename) const {
  try {
    // Open file
    std::ifstream file(filename);
    if (!file.is_open()) {
      return std::nullopt;
    }

    // Parse JSON
    nlohmann::json json;
    file >> json;

    // Extract encrypted data
    if (!json.contains("data") || !json["data"].is_string()) {
      return std::nullopt;
    }

    // Extract salt (backward compatibility: use default salt if not present)
    std::vector<unsigned char> salt;
    if (json.contains("salt") && json["salt"].is_string()) {
      // Decode base64 salt
      std::string saltBase64 = json["salt"].get<std::string>();
      static const std::string base64_chars =
          "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

      int in_len = static_cast<int>(saltBase64.size());
      int i = 0;
      int in_ = 0;
      unsigned char char_array_4[4], char_array_3[3];

      while (in_len-- && (saltBase64[in_] != '=') &&
             (isalnum(saltBase64[in_]) || (saltBase64[in_] == '+') ||
              (saltBase64[in_] == '/'))) {
        char_array_4[i++] = saltBase64[in_];
        in_++;
        if (i == 4) {
          for (i = 0; i < 4; i++) {
            char_array_4[i] =
                static_cast<unsigned char>(base64_chars.find(char_array_4[i]));
          }

          char_array_3[0] =
              (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
          char_array_3[1] =
              ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
          char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

          for (i = 0; i < 3; i++) {
            salt.push_back(char_array_3[i]);
          }
          i = 0;
        }
      }

      if (i) {
        for (int j = i; j < 4; j++) {
          char_array_4[j] = 0;
        }

        for (int j = 0; j < 4; j++) {
          char_array_4[j] =
              static_cast<unsigned char>(base64_chars.find(char_array_4[j]));
        }

        char_array_3[0] =
            (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] =
            ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
        char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

        for (int j = 0; j < i - 1; j++) {
          salt.push_back(char_array_3[j]);
        }
      }
    } else {
      // Backward compatibility: use fixed salt for old files
      const std::string fixedSalt = "PinnacleMM_SecureConfig_Salt";
      salt.assign(fixedSalt.begin(), fixedSalt.end());
    }

    return std::make_pair(json["data"].get<std::string>(), salt);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::vector<unsigned char> SecureConfig::deriveKeyFromPassword(
    const std::string& password, const std::vector<unsigned char>& salt) const {
  // Use PBKDF2 to derive a key from the password
  const int iterations =
      100000; // Increased from 10,000 to 100,000 for better security
  const int key_length = 32; // 256 bits

  // Allocate memory for the key
  std::vector<unsigned char> key(key_length);

  // Derive the key
  if (PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.size()),
                        salt.data(), static_cast<int>(salt.size()), iterations,
                        EVP_sha256(), key_length, key.data()) != 1) {
    throw std::runtime_error("Key derivation failed");
  }

  return key;
}

std::vector<unsigned char> SecureConfig::generateSalt() const {
  const int salt_length = 32; // 256 bits
  std::vector<unsigned char> salt(salt_length);

  if (RAND_bytes(salt.data(), salt_length) != 1) {
    throw std::runtime_error("Failed to generate random salt");
  }

  return salt;
}

void SecureConfig::secureMemzero(void* ptr, size_t len) const {
  if (ptr == nullptr || len == 0) {
    return;
  }

  // Use volatile to prevent compiler optimization
  volatile unsigned char* vptr = static_cast<volatile unsigned char*>(ptr);
  for (size_t i = 0; i < len; ++i) {
    vptr[i] = 0;
  }

  // Memory barrier to ensure the clearing is not optimized away
  std::atomic_thread_fence(std::memory_order_acq_rel);
}

// ApiCredentials implementation
ApiCredentials::ApiCredentials(SecureConfig& config) : m_config(config) {}

bool ApiCredentials::setCredentials(
    const std::string& exchange, const std::string& apiKey,
    const std::string& apiSecret,
    const std::optional<std::string>& passphrase) {
  // Construct key prefixes
  std::string baseKey = API_KEY_PREFIX + exchange + ".";
  std::string apiKeyKey = baseKey + "apiKey";
  std::string apiSecretKey = baseKey + "apiSecret";
  std::string passphraseKey = baseKey + "passphrase";

  // Set values
  m_config.setValue(apiKeyKey, apiKey);
  m_config.setValue(apiSecretKey, apiSecret);

  if (passphrase) {
    m_config.setValue(passphraseKey, *passphrase);
  } else {
    // Remove passphrase if it exists
    m_config.removeKey(passphraseKey);
  }

  return true;
}

std::optional<std::string>
ApiCredentials::getApiKey(const std::string& exchange) const {
  std::string key = API_KEY_PREFIX + exchange + ".apiKey";
  return m_config.getValue(key);
}

std::optional<std::string>
ApiCredentials::getApiSecret(const std::string& exchange) const {
  std::string key = API_KEY_PREFIX + exchange + ".apiSecret";
  return m_config.getValue(key);
}

std::optional<std::string>
ApiCredentials::getPassphrase(const std::string& exchange) const {
  std::string key = API_KEY_PREFIX + exchange + ".passphrase";
  return m_config.getValue(key);
}

bool ApiCredentials::hasCredentials(const std::string& exchange) const {
  std::string apiKeyKey = API_KEY_PREFIX + exchange + ".apiKey";
  std::string apiSecretKey = API_KEY_PREFIX + exchange + ".apiSecret";

  return m_config.hasKey(apiKeyKey) && m_config.hasKey(apiSecretKey);
}

bool ApiCredentials::removeCredentials(const std::string& exchange) {
  std::string baseKey = API_KEY_PREFIX + exchange + ".";
  std::string apiKeyKey = baseKey + "apiKey";
  std::string apiSecretKey = baseKey + "apiSecret";
  std::string passphraseKey = baseKey + "passphrase";

  bool removedApiKey = m_config.removeKey(apiKeyKey);
  bool removedApiSecret = m_config.removeKey(apiSecretKey);

  // Remove passphrase if it exists
  m_config.removeKey(passphraseKey);

  // Return true if at least one of the main credentials was removed
  return removedApiKey || removedApiSecret;
}

} // namespace utils
} // namespace pinnacle
