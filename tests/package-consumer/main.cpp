#include <flowq/quic/session.hpp>

int main() {
#if defined(FLOWQ_ENABLE_TEST_PACKET_PROTECTION_BYPASS)
    return 2;
#endif

    flowq::quic::session_config config{};
    return config.protection_policy == flowq::quic::packet_protection_policy::production_required ? 0 : 1;
}
