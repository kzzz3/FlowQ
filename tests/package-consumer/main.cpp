#include <flowq/quic/session.hpp>

#include <type_traits>
#include <utility>

namespace {

template <typename T, typename = void>
struct has_protection_policy : std::false_type {};

template <typename T>
struct has_protection_policy<T, std::void_t<decltype(std::declval<T&>().protection_policy)>> : std::true_type {};

static_assert(!has_protection_policy<flowq::quic::session_config>::value);
static_assert(!has_protection_policy<flowq::quic::connection_loop_config>::value);

} // namespace

int main() {
    flowq::quic::session_config config{};
    return config.version == 1 ? 0 : 1;
}
