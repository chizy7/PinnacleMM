# Certificate Pinning Guide

## Overview

Certificate pinning enhances security by validating that the server's SSL/TLS certificate matches a known "pin" (the SHA256 hash of the certificate's public key). This prevents man-in-the-middle (MITM) attacks, even if an attacker has a valid certificate from a compromised Certificate Authority.

## Current Configuration

### Coinbase Endpoints (Enabled & Enforced)

| Endpoint | Pin | Expiry |
|----------|-----|--------|
| `ws-feed.exchange.coinbase.com` | `mpzb4t3w5gAFZJGODlP0+FJa+wjD/bOQszdCDs6BTmU=` | Dec 22, 2025 |
| `ws-feed.prime.coinbase.com` | `ERzVGmVjfqDVEe2YEp5l1B7zaXEJoSYinwL9InU8Pis=` | Jan 6, 2026 |
| `advanced-trade-ws.coinbase.com` | `Is81uMxmmDbwnPDQSpN+FgZ5nfv2XenZ8Ql8zE4Vbzs=` | Dec 22, 2025 |

**Status:** Pinning enabled and enforced
**Issuer:** Google Trust Services (WE1)
**Last Updated:** October 13, 2025

## How It Works

1. **Connection Attempt:** When connecting to a WebSocket endpoint (e.g., Coinbase)
2. **Certificate Received:** Server presents its SSL/TLS certificate
3. **Pin Extraction:** Extract SHA256 hash of the certificate's public key
4. **Pin Validation:** Compare against configured pins
5. **Decision:**
   - Match -> Connection allowed
   - Mismatch -> Connection rejected (if enforcement enabled)

## Extracting Certificate Pins

### Using the Script (Recommended)

```bash
# Run the extraction script
./scripts/extract_cert_pin.sh

# Output shows pins for all configured endpoints
```

### Manual Extraction

```bash
# Connect and extract certificate pin
echo | openssl s_client -connect ws-feed.exchange.coinbase.com:443 \
  -servername ws-feed.exchange.coinbase.com 2>/dev/null \
  | openssl x509 -pubkey -noout \
  | openssl pkey -pubin -outform DER \
  | openssl dgst -sha256 -binary \
  | openssl base64
```

## Updating Certificate Pins

### When to Update

- **Before Expiry:** Update pins 1-2 weeks before certificate expiry
- **Certificate Rotation:** When exchange rotates certificates
- **Security Incident:** Immediately if certificate compromise suspected

### Update Process

1. **Extract New Pins:**
   ```bash
   ./scripts/extract_cert_pin.sh
   ```

2. **Update Source Code:**
   - Edit: `core/utils/CertificatePinner.cpp`
   - Function: `CertificatePinner::initializeDefaultPins()`
   - Lines: 166-187

3. **Update Pins:**
   ```cpp
   addPin("ws-feed.exchange.coinbase.com",
          "NEW_PIN_HERE_BASE64_ENCODED=", true);
   ```

4. **Test:**
   ```bash
   # Rebuild
   cd build && cmake --build . --target certificate_pinner_tests

   # Run tests
   ./certificate_pinner_tests
   ```

5. **Verify:**
   - All tests pass
   - Real certificate validation succeeds
   - Check logs for "Certificate pin matched"

## Configuration

### In Code (Default)

Pins are configured in `core/utils/CertificatePinner.cpp`:

```cpp
void CertificatePinner::initializeDefaultPins() {
  addPin("ws-feed.exchange.coinbase.com",
         "mpzb4t3w5gAFZJGODlP0+FJa+wjD/bOQszdCDs6BTmU=", true);
  // ... more pins
}
```

### Via Configuration File (Optional)

Create `config/certificate_pins.json`:

```json
{
  "enabled": true,
  "certificate_pins": {
    "ws-feed.exchange.coinbase.com": {
      "enforce": true,
      "pins": [
        "mpzb4t3w5gAFZJGODlP0+FJa+wjD/bOQszdCDs6BTmU=",
        "BACKUP_PIN_HERE_IF_NEEDED="
      ]
    }
  }
}
```

Load at runtime:
```cpp
pinner->loadPinsFromFile("config/certificate_pins.json");
```

## Testing

### Run Certificate Pinning Tests

```bash
# Build tests
cd build && cmake --build . --target certificate_pinner_tests

# Run tests
./certificate_pinner_tests
```

### Expected Output

```
[==========] Running 6 tests from 1 test suite.
...
=== Testing Real Coinbase Certificate Pinning ===
Actual certificate pin: mpzb4t3w5gAFZJGODlP0+FJa+wjD/bOQszdCDs6BTmU=
Certificate Subject: /CN=exchange.coinbase.com
Certificate Issuer: /C=US/O=Google Trust Services/CN=WE1
=== Certificate Pinning Test PASSED ===
...
[  PASSED  ] 6 tests.
```

## Troubleshooting

### Connection Fails with "Certificate pin verification failed"

**Cause:** Certificate changed (rotation or MITM attack)

**Solution:**
1. Extract current certificate pin
2. Compare with configured pin
3. If legitimate rotation, update pin
4. If unexpected, investigate security incident

### Test Fails: "Could not connect to Coinbase"

**Cause:** Network issue or endpoint unavailable

**Solution:**
- Check internet connection
- Verify endpoint is accessible: `curl -I https://ws-feed.exchange.coinbase.com`
- Firewall/proxy may be blocking connection

### Warning: "Certificate pin mismatch (not enforced)"

**Cause:** Pin mismatch but enforcement disabled

**Solution:**
- This is informational when `enforce: false`
- Update pin or enable enforcement for production

## Best Practices

### Multiple Pins per Endpoint

Include backup pins for certificate rotation:

```cpp
// Current certificate
addPin("ws-feed.exchange.coinbase.com",
       "mpzb4t3w5gAFZJGODlP0+FJa+wjD/bOQszdCDs6BTmU=", true);

// Backup pin (for rotation)
addPin("ws-feed.exchange.coinbase.com",
       "BACKUP_PIN_BASE64=", true);
```

### Pin Expiry Monitoring

Set calendar reminders:
- **90 days before expiry:** Extract backup pins
- **30 days before expiry:** Test with backup pins
- **7 days before expiry:** Deploy updated pins

### Development vs Production

```cpp
// Development: Log warnings but don't enforce
addPin("ws-feed.exchange.coinbase.com", "pin==", false);

// Production: Enforce pinning
addPin("ws-feed.exchange.coinbase.com", "pin==", true);
```

## Security Considerations

### Benefits

- Prevents MITM attacks
- Protects against compromised CAs
- Detects certificate changes immediately

### Risks

- **Availability Risk:** Outdated pins block legitimate connections
- **Maintenance:** Requires pin updates before cert expiry
- **Monitoring:** Must track certificate expiry dates

### Recommendations

1. **Always enable pinning in production**
2. **Monitor certificate expiry dates**
3. **Include backup pins for rotation**
4. **Test pin updates in staging first**
5. **Document pin update procedures**

## API Reference

### Adding Pins

```cpp
// Add single pin
pinner->addPin(hostname, sha256Pin, enforce);

// Example
pinner->addPin("ws-feed.exchange.coinbase.com",
               "mpzb4t3w5gAFZJGODlP0+FJa+wjD/bOQszdCDs6BTmU=",
               true);
```

### Verifying Certificates

```cpp
// Verify certificate against pins
bool valid = pinner->verifyCertificate(hostname, x509_cert);

if (!valid) {
    // Connection rejected - potential security issue
}
```

### Enable/Disable Pinning

```cpp
// Disable pinning (not recommended for production)
pinner->setEnabled(false);

// Check status
bool enabled = pinner->isEnabled();
```

## References

- **Implementation:** `core/utils/CertificatePinner.cpp`
- **Header:** `core/utils/CertificatePinner.h`
- **Tests:** `tests/unit/CertificatePinnerTest.cpp`
- **Extraction Script:** `scripts/extract_cert_pin.sh`

## Support

For questions or issues:
1. Check test output: `./certificate_pinner_tests`
2. Review logs for pin verification errors
3. Re-extract pins if certificates rotated
4. See [Credentials Guide](./credentials.md) for API key security practices
