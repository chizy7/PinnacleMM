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

- **Algorithm**: AES-256-CBC (Cipher Block Chaining)
- **Key Derivation**: PBKDF2 with 100,000 iterations (increased from 10,000)
- **Salt**: Unique 32-byte random salt per file (replaces fixed salt vulnerability)
- **Initialization Vector**: Random 16-byte IV for each encryption
- **Secure Input**: Password masking with platform-specific terminal control
- **Memory Security**: Secure memory clearing using volatile operations
- **Input Validation**: Comprehensive validation preventing injection attacks

The encryption implementation uses OpenSSL for cryptographic operations.

## Credential Storage Format

Encrypted credentials are stored in the following format:

```json
{
  "data": "base64-encoded-ciphertext",
  "salt": "base64-encoded-32-byte-salt",
  "timestamp": "ISO-8601-timestamp"
}
```

Each component serves a specific purpose:
- **data**: Base64-encoded encrypted credentials (includes 16-byte IV + ciphertext)
- **salt**: Base64-encoded 32-byte random salt unique per file
- **timestamp**: Creation timestamp for audit purposes

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

## Live Trading with Real Market Data

**Current Implementation Status**: Production-ready for Coinbase

PinnacleMM now supports live market data from Coinbase Pro WebSocket feeds:

- **Real-time ticker updates**: Live BTC-USD prices (~$109,200+)
- **WebSocket connectivity**: Secure SSL/TLS connection using Boost.Beast
- **Data processing**: Multiple market updates per second with volume data
- **Automatic reconnection**: Robust connection handling for 24/7 operation

### Getting Coinbase Pro API Credentials

1. **Create Coinbase Account**: Sign up at [coinbase.com](https://coinbase.com) or [pro.coinbase.com](https://pro.coinbase.com)
2. **Generate API Key**: Go to Settings → API → Create API Key
3. **Set Permissions**: Enable "View" permissions (no trading permissions needed for market data)
4. **Note Important**: Coinbase Pro is now Coinbase Advanced Trade, but the WebSocket feeds still work
5. **Save Credentials**: Record your API Key, Secret, and Passphrase securely

**Note**: For ticker data (current implementation), API credentials are optional as the ticker channel is public. However, credentials are required for order book data (level2/level3 channels) and order execution.

### Quick Start with Live Data

1. **Setup** (one-time):
   ```bash
   ./pinnaclemm --setup-credentials
   ```

2. **Run live**:
   ```bash
   ./pinnaclemm --mode live --exchange coinbase --symbol BTC-USD --verbose
   ```

3. **Monitor**: Watch real-time price feeds and trading activity

### Security Notes for Live Trading

- **Credential Encryption**: All API keys encrypted with AES-256-CBC + 100,000 PBKDF2 iterations + unique salt
- **Master Password**: Required for each live session, never stored, secure input with masking
- **Input Validation**: All user inputs validated and sanitized
- **Rate Limiting**: API operations protected with configurable rate limits
- **Audit Logging**: All security events logged for monitoring
- **Certificate Pinning**: WebSocket connections use certificate pinning
- **Minimal Permissions**: Only "View" permissions needed for market data
- **No Trading**: Current implementation is market data only (no order execution)

## Future Security Enhancements

Recently implemented security enhancements:

1. **Enhanced Encryption**: Upgraded to unique salt generation and 100,000 PBKDF2 iterations
2. **Secure Input**: Password masking and secure memory clearing
3. **Input Validation**: Comprehensive validation framework preventing attacks
4. **Audit Logging**: Complete security event logging system
5. **Rate Limiting**: Configurable rate limiting for API operations
6. **Certificate Pinning**: SSL certificate validation for WebSocket connections

Planned future improvements:

1. **Hardware security module (HSM) support**: Store keys in dedicated hardware
2. **Two-factor authentication**: Add 2FA for accessing credentials
3. **Key rotation automation**: Automate the process of rotating API keys
4. **Secure enclave support**: Use secure enclaves on supporting hardware
