option(FLOWQ_ENABLE_OPENSSL_CRYPTO "Enable OpenSSL-backed QUIC crypto vector helpers" OFF)

if(FLOWQ_ENABLE_OPENSSL_CRYPTO)
    find_package(OpenSSL REQUIRED COMPONENTS Crypto)
    target_compile_definitions(flowq INTERFACE FLOWQ_ENABLE_OPENSSL_CRYPTO=1)
    target_link_libraries(flowq INTERFACE OpenSSL::Crypto)
endif()
