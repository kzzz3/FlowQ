option(FLOWQ_ENABLE_OPENSSL_QUIC_TLS "Enable experimental OpenSSL QUIC TLS provider surface" OFF)

if(FLOWQ_ENABLE_OPENSSL_QUIC_TLS)
    find_package(OpenSSL REQUIRED COMPONENTS SSL Crypto)

    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_LIBRARIES OpenSSL::SSL OpenSSL::Crypto)
    set(CMAKE_REQUIRED_INCLUDES ${OPENSSL_INCLUDE_DIR})
    check_cxx_source_compiles([[#include <openssl/opensslv.h>
#include <openssl/ssl.h>
#if OPENSSL_VERSION_NUMBER < 0x30500000L
#error OpenSSL QUIC TLS APIs require OpenSSL 3.5 or newer
#endif
int main() {
    auto symbol = &SSL_set_quic_tls_cbs;
    (void)symbol;
    return 0;
}
]] FLOWQ_OPENSSL_HAS_QUIC_TLS_API)
    unset(CMAKE_REQUIRED_LIBRARIES)
    unset(CMAKE_REQUIRED_INCLUDES)

    if(NOT FLOWQ_OPENSSL_HAS_QUIC_TLS_API)
        message(FATAL_ERROR "FLOWQ_ENABLE_OPENSSL_QUIC_TLS requires OpenSSL 3.5+ with SSL_set_quic_tls_cbs")
    endif()

    target_compile_definitions(flowq INTERFACE FLOWQ_ENABLE_OPENSSL_QUIC_TLS=1 FLOWQ_HAVE_OPENSSL_QUIC_TLS=1)
    target_link_libraries(flowq INTERFACE OpenSSL::SSL OpenSSL::Crypto)
endif()
