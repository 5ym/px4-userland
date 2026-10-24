// SPDX-License-Identifier: GPL-2.0-only
#include "px4/identity.h"

#include <host_ctl.h>

#if defined(_WIN32)
#include "px4_windows_args.h"
#endif

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

using namespace px4::userland;
using namespace dtv::userland;

bool normalize_px4_instance(std::string_view input,
                            identity::InstanceText& output) noexcept
{
    if (valid_device_instance(input) || !valid_runtime_instance(input)) return false;
    const auto normalized = identity::canonicalize_instance(input);
    if (!normalized) return false;
    output = normalized.value();
    return true;
}

bool matches_px4_serial_identity(
    const wire::ListResponse& list,
    const identity::ResolvedIdentity& expected) noexcept
{
    if (!list.enclosure.present || expected.kind != identity::IdentityKind::serial ||
        list.enclosure.value.identity.uniqueness != wire::Uniqueness::unique_in_scan ||
        list.functions.count == 0U)
        return false;
    const auto& observed = list.enclosure.value.identity;
    const DeviceProfile* profile = device_profile_for_usb_id(
        list.functions.items[0U].vid, list.functions.items[0U].pid);
    if (profile == nullptr) return false;
    for (std::size_t index = 1U; index < list.functions.count; ++index) {
        const DeviceProfile* other = device_profile_for_usb_id(
            list.functions.items[index].vid, list.functions.items[index].pid);
        if (other == nullptr || other->model != profile->model) return false;
    }
    return observed.serial_state == wire::IdentitySerialState::present &&
           observed.serial.present &&
           observed.serial.value.length == expected.serial.length &&
           std::memcmp(observed.serial.value.bytes.data(), expected.serial.bytes.data(),
                       expected.serial.length) == 0;
}

const host_ctl::Profile kProfile{
    "px4ctl",
    "px4-userland",
    PX4_USERLAND_PRODUCT_VERSION,
    identity::Product::px4,
    true,
    true,
    normalize_px4_instance,
    matches_px4_serial_identity,
    true,
    false,
};

}  // namespace

int main(int argc, char** argv)
{
#if defined(_WIN32)
    const std::vector<std::string> owned_argv =
        px4::userland::cli::windows_argv_utf8(argc, argv);
    std::vector<const char*> argv_views;
    argv_views.reserve(owned_argv.size());
    for (const std::string& value : owned_argv) argv_views.push_back(value.c_str());
    return dtv::userland::host_ctl::run_control_cli(
        static_cast<int>(argv_views.size()), argv_views.data(), kProfile);
#else
    return dtv::userland::host_ctl::run_control_cli(
        argc, const_cast<const char* const*>(argv), kProfile);
#endif
}
