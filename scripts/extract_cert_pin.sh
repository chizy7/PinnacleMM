#!/bin/bash
# Script to extract certificate pins from Coinbase WebSocket endpoints
# This extracts the SHA256 hash of the Subject Public Key Info (SPKI) in base64 format

set -e

echo "=================================================="
echo "Certificate Pin Extractor for Coinbase"
echo "=================================================="
echo ""

# Coinbase WebSocket endpoints
ENDPOINTS=(
    "ws-feed.exchange.coinbase.com:443"
    "ws-feed.prime.coinbase.com:443"
    "advanced-trade-ws.coinbase.com:443"
)

for ENDPOINT in "${ENDPOINTS[@]}"; do
    HOST=$(echo "$ENDPOINT" | cut -d: -f1)
    PORT=$(echo "$ENDPOINT" | cut -d: -f2)

    echo "Extracting pin for: $HOST:$PORT"
    echo "----------------------------------------"

    # Get the certificate chain with timeout
    # Use gtimeout on macOS (brew install coreutils) or timeout on Linux
    if command -v timeout >/dev/null 2>&1; then
        TIMEOUT_CMD="timeout 10s"
    elif command -v gtimeout >/dev/null 2>&1; then
        TIMEOUT_CMD="gtimeout 10s"
    else
        TIMEOUT_CMD=""
    fi

    if [ -n "$TIMEOUT_CMD" ]; then
        CERT_CHAIN=$($TIMEOUT_CMD openssl s_client -connect "$ENDPOINT" -servername "$HOST" </dev/null 2>/dev/null | openssl x509 -outform PEM)
    else
        # Fallback without timeout
        CERT_CHAIN=$(openssl s_client -connect "$ENDPOINT" -servername "$HOST" </dev/null 2>/dev/null | openssl x509 -outform PEM)
    fi

    if [ -z "$CERT_CHAIN" ]; then
        echo "Failed to retrieve certificate for $HOST"
        echo ""
        continue
    fi

    # Extract the public key and compute SHA256 hash in base64
    PIN=$(echo "$CERT_CHAIN" | openssl x509 -pubkey -noout | openssl pkey -pubin -outform DER | openssl dgst -sha256 -binary | openssl base64)

    echo "Certificate Pin (SHA256/Base64): $PIN"

    # Also show certificate details
    echo ""
    echo "Certificate Details:"
    echo "$CERT_CHAIN" | openssl x509 -noout -subject -issuer -dates
    echo ""
    echo "=================================================="
    echo ""
done

echo "Script completed!"
echo ""
echo "To use these pins, update core/utils/CertificatePinner.cpp"
echo "Replace the placeholder pins with the extracted values above."
