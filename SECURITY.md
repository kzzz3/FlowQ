# Security Policy

## Supported Versions

| Version | Supported |
|---------|-----------|
| 1.1.x | Yes |
| 1.0.x | Yes |
| < 1.0 | No |

## Reporting a Vulnerability

Report security vulnerabilities via GitHub Security Advisories.

Do NOT open public issues for security vulnerabilities.

### What to Include

- Description of the vulnerability
- Steps to reproduce
- Potential impact
- Suggested fix (if available)

### Response Timeline

- Initial response: 48 hours
- Triage: 7 days
- Fix or mitigation: 30 days

## Security Boundaries

### Crypto Provider

FlowQ uses OpenSSL for TLS 1.3 and AEAD packet protection. The crypto provider boundary (`crypto_provider.hpp`) enforces:

- Fail-closed when OpenSSL backend is absent
- Cipher-suite-aware header protection (AES-ECB, ChaCha20)
- Secure key material zeroing on destruction

### Key Material

All traffic secrets, AEAD keys, and IVs are securely erased on destruction:

- Windows: `SecureZeroMemory`
- macOS: `memset_s`
- Linux: `explicit_bzero`

### Thread Safety

Connection and session objects are NOT thread-safe. Concurrent access requires external synchronization.

## Known Limitations

- Single cipher suite per connection (no renegotiation)
- No 0-RTT replay protection beyond OpenSSL's built-in mechanisms
- No formal security audit has been conducted
