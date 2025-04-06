# Security & API Key Management

This document describes PinnacleMM's approach to security, focusing on how exchange API credentials are handled, stored, and protected.

## Overview

PinnacleMM requires API keys to connect to cryptocurrency exchanges for live trading. These credentials are highly sensitive and require proper security measures. Our system uses industry-standard encryption practices to protect this data both in storage and during use.

## Credential Management Architecture

The credential management system consists of several key components:

1. **ApiCredentials Class**: Manages encrypted API keys, secrets, and passphrases
2. **SecureConfig**: Handles loading and saving encrypted configuration data
3. **Master Password**: Single key used to encrypt/decrypt all credentials
4. **Encryption Engine**: Uses AES-256-GCM for secure encryption

### Security Principles

PinnacleMM follows these security principles for API key management:

- **Never store plaintext credentials**: All sensitive data is encrypted
- **Minimize exposure**: Credentials are decrypted only when needed
- **Defense in depth**: Multiple layers of protection
- **Secure defaults**: Conservative security settings by default

## Encryption Details

PinnacleMM uses the following encryption techniques:

- **Algorithm**: AES-256-GCM (Galois/Counter Mode)
- **Key Derivation**: PBKDF2 with high iteration count
- **Salt**: Unique, random salt for each encryption operation
- **Authentication**: GCM provides built-in authentication
- **IV Handling**: Random initialization vector for each encryption

The encryption implementation uses OpenSSL for cryptographic operations.

## Credential Storage Format

Encrypted credentials are stored in the following format:

```
[16-bytes Salt][12-bytes IV][N-bytes Ciphertext][16-bytes Auth Tag]
```

Each component serves a specific purpose:
- **Salt**: Used for key derivation, unique per file
- **IV**: Initialization vector, unique per encryption operation
- **Ciphertext**: The encrypted API credentials
- **Auth Tag**: Authentication tag to verify data integrity

## API Credential Setup

### Initial Setup

When setting up API credentials for the first time:

1. Run PinnacleMM with the `--setup-credentials` flag
2. Create a master password when prompted
3. Enter your exchange API credentials
4. Credentials are encrypted and saved to the configuration directory

### Example:

```bash
$ ./pinnaclemm --setup-credentials

Create master password: **********
Confirm master password: **********

Setting up exchange credentials
-------------------------------

Exchange [coinbase, kraken, binance, gemini, bitstamp]: coinbase
API Key: ****************************************
API Secret: ****************************************
API Passphrase (if required): ********

Credentials saved successfully.
Add another exchange? [y/n]: n
```

### Using Stored Credentials

When running PinnacleMM in live mode, you'll be prompted for your master password:

```bash
$ ./pinnaclemm --mode live --exchange coinbase --symbol BTC-USD

Enter master password: **********
[2025-04-05 20:17:57.483] [pinnaclemm] [info] Connected to live exchange: coinbase
```

## Implementation Details

### ApiCredentials Class

The `ApiCredentials` class provides a secure interface for accessing exchange credentials:

```cpp
class ApiCredentials {
public:
    // Create empty credentials
    ApiCredentials();
    
    // Load credentials from encrypted file
    bool loadFromFile(const std::string& filename, const std::string& masterPassword);
    
    // Save credentials to encrypted file
    bool saveToFile(const std::string& filename, const std::string& masterPassword) const;
    
    // Add or update credentials for an exchange
    void setCredentials(
        const std::string& exchange,
        const std::string& apiKey,
        const std::string& apiSecret,
        const std::string& passphrase = ""
    );
    
    // Check if credentials exist for an exchange
    bool hasCredentials(const std::string& exchange) const;
    
    // Get API key for an exchange
    std::optional<std::string> getApiKey(const std::string& exchange) const;
    
    // Get API secret for an exchange
    std::optional<std::string> getApiSecret(const std::string& exchange) const;
    
    // Get API passphrase for an exchange
    std::optional<std::string> getPassphrase(const std::string& exchange) const;
};
```

### Encryption Implementation

The encryption and decryption functions use OpenSSL's EVP interface:

```cpp
// Encrypt data using AES-256-GCM
std::vector<unsigned char> encrypt(const std::string& plaintext, const std::array<unsigned char, 32>& key) {
    // Generate a random IV (Initialization Vector)
    std::vector<unsigned char> iv(12); // 96 bits for GCM
    if (RAND_bytes(iv.data(), iv.size()) != 1) {
        throw std::runtime_error("Failed to generate random IV");
    }
    
    // Initialize encryption context
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        throw std::runtime_error("Failed to create encryption context");
    }
    
    // Initialize encryption
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, key.data(), iv.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to initialize encryption");
    }
    
    // Encrypt data
    // [...]
}
```

## Best Practices for Users

As a user of PinnacleMM, follow these guidelines to keep your API credentials secure:

1. **Use a strong master password**: Choose a complex password with a mix of characters
2. **Limit API key permissions**: Only grant trading permissions needed for market making
3. **Use IP restrictions**: Set up IP restrictions on exchange API keys when possible
4. **Separate test and production keys**: Use different API keys for testing and production
5. **Regular rotation**: Change API keys periodically
6. **Secure your system**: Keep the system running PinnacleMM secure with updates

## Security Considerations for Development

If you're contributing to PinnacleMM, follow these guidelines:

1. **Never commit credentials**: Don't include API keys in source code or commits
2. **Avoid debug logging**: Don't log sensitive information, even in debug mode
3. **Memory handling**: Clear sensitive data from memory after use
4. **Error messages**: Don't expose sensitive information in error messages
5. **Input validation**: Validate all user input before processing

## Future Security Enhancements

Planned security improvements include:

1. **Hardware security module (HSM) support**: Store keys in dedicated hardware
2. **Two-factor authentication**: Add 2FA for accessing credentials
3. **Key rotation automation**: Automate the process of rotating API keys
4. **Audit logging**: Enhanced logging of all credential access
5. **Secure enclave support**: Use secure enclaves on supporting hardware