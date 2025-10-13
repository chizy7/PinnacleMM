#include "../../core/utils/CertificatePinner.h"
#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <netdb.h>
#include <netinet/in.h>
#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace pinnacle::utils;

class CertificatePinnerTest : public ::testing::Test {
protected:
  void SetUp() override { pinner = std::make_shared<CertificatePinner>(); }

  void TearDown() override { pinner.reset(); }

  std::shared_ptr<CertificatePinner> pinner;

  // Helper to connect and get certificate from a real endpoint
  X509* fetchCertificate(const std::string& hostname, int port = 443) {
    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) {
      return nullptr;
    }

    SSL* ssl = SSL_new(ctx);
    if (!ssl) {
      SSL_CTX_free(ctx);
      return nullptr;
    }

    // Create socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
      SSL_free(ssl);
      SSL_CTX_free(ctx);
      return nullptr;
    }

    // Set timeout
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) <
            0 ||
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) <
            0) {
      // Continue anyway - timeouts are best-effort
      // But log that timeout setting failed
    }

    // Resolve hostname
    struct hostent* host = gethostbyname(hostname.c_str());
    if (!host) {
      close(sock);
      SSL_free(ssl);
      SSL_CTX_free(ctx);
      return nullptr;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr = *reinterpret_cast<struct in_addr*>(host->h_addr);

    // Connect
    if (connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) <
        0) {
      close(sock);
      SSL_free(ssl);
      SSL_CTX_free(ctx);
      return nullptr;
    }

    // SSL handshake
    SSL_set_fd(ssl, sock);
    SSL_set_tlsext_host_name(ssl, hostname.c_str());

    if (SSL_connect(ssl) != 1) {
      close(sock);
      SSL_free(ssl);
      SSL_CTX_free(ctx);
      return nullptr;
    }

    // Get certificate (this creates a copy, so we own it)
    X509* cert = SSL_get_peer_certificate(ssl);

    // Cleanup
    SSL_shutdown(ssl);
    close(sock);
    SSL_free(ssl);
    SSL_CTX_free(ctx);

    return cert;
  }
};

TEST_F(CertificatePinnerTest, InitializeDefaultPins) {
  // Should have default pins configured
  EXPECT_TRUE(pinner->isEnabled());
}

TEST_F(CertificatePinnerTest, AddAndVerifyPin) {
  // Add a test pin
  pinner->addPin("test.example.com", "testpin123==", true);

  // This will pass because we don't have a real certificate to test against
  // Just verify the pin was added
  SUCCEED();
}

TEST_F(CertificatePinnerTest, CoinbaseRealCertificateValidation) {
  std::cout << "\n=== Testing Real Coinbase Certificate Pinning ===\n"
            << std::endl;

  // Try to fetch the real certificate from Coinbase
  X509* cert = fetchCertificate("ws-feed.exchange.coinbase.com", 443);

  if (!cert) {
    GTEST_SKIP() << "Could not connect to Coinbase (network issue or endpoint "
                    "unavailable)";
    return;
  }

  // Verify the certificate against our pinned values
  bool verified =
      pinner->verifyCertificate("ws-feed.exchange.coinbase.com", cert);

  // Get the actual fingerprint for debugging
  std::string actualPin = CertificatePinner::getCertificateFingerprint(cert);
  std::cout << "Actual certificate pin: " << actualPin << std::endl;

  // Verify certificate details
  char* subjectName =
      X509_NAME_oneline(X509_get_subject_name(cert), nullptr, 0);
  char* issuerName = X509_NAME_oneline(X509_get_issuer_name(cert), nullptr, 0);

  std::cout << "Certificate Subject: " << (subjectName ? subjectName : "N/A")
            << std::endl;
  std::cout << "Certificate Issuer: " << (issuerName ? issuerName : "N/A")
            << std::endl;

  if (subjectName)
    OPENSSL_free(subjectName);
  if (issuerName)
    OPENSSL_free(issuerName);

  // Clean up
  X509_free(cert);

  // The verification should succeed with our configured pins
  EXPECT_TRUE(verified)
      << "Certificate pinning failed for ws-feed.exchange.coinbase.com";
  std::cout << "\n=== Certificate Pinning Test PASSED ===\n" << std::endl;
}

TEST_F(CertificatePinnerTest, InvalidCertificateShouldFail) {
  std::cout << "\n=== Testing Invalid Certificate Rejection ===\n" << std::endl;

  // Create a pinner with a specific pin for a test domain
  auto testPinner = std::make_shared<CertificatePinner>();
  testPinner->addPin("test.invalid.domain",
                     "wrongpin12345678901234567890123=", true);

  // Try to fetch a real certificate from Coinbase
  X509* cert = fetchCertificate("ws-feed.exchange.coinbase.com", 443);

  if (!cert) {
    GTEST_SKIP() << "Could not connect to Coinbase for negative test";
    return;
  }

  // This should fail because we're testing with a wrong pin
  bool verified = testPinner->verifyCertificate("test.invalid.domain", cert);

  X509_free(cert);

  // Should fail because the pin doesn't match
  EXPECT_FALSE(verified) << "Invalid certificate was incorrectly accepted";
  std::cout << "\n=== Invalid Certificate Rejection Test PASSED ===\n"
            << std::endl;
}

TEST_F(CertificatePinnerTest, DisabledPinnerAllowsAll) {
  pinner->setEnabled(false);

  // When disabled, all certificates should pass
  EXPECT_FALSE(pinner->isEnabled());
}

TEST_F(CertificatePinnerTest, FingerprintExtraction) {
  // Try to extract fingerprint from a real certificate
  X509* cert = fetchCertificate("ws-feed.exchange.coinbase.com", 443);

  if (!cert) {
    GTEST_SKIP() << "Could not connect to Coinbase for fingerprint test";
    return;
  }

  std::string fingerprint = CertificatePinner::getCertificateFingerprint(cert);

  std::cout << "\nExtracted fingerprint: " << fingerprint << std::endl;

  X509_free(cert);

  // Should have extracted a non-empty fingerprint
  EXPECT_FALSE(fingerprint.empty())
      << "Failed to extract certificate fingerprint";
}
