#include <flowq/quic/session.hpp>

int main() {
    flowq::quic::session_config config{};
    return config.protection_policy == flowq::quic::packet_protection_policy::production_required ? 0 : 1;
}
