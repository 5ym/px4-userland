// SPDX-License-Identifier: GPL-2.0-only
#include "px4/q3u4_stream.h"

#include <cstdio>
#include <memory>

namespace {

using namespace px4::userland;

class TestTransport final : public Transport {
public:
    Result<std::size_t> bulk_read(std::uint8_t, MutableByteView, Timeout,
                                  BulkReadObservation*) noexcept override
    {
        return Result<std::size_t>::failure(Error::UNSUPPORTED);
    }

    Result<std::size_t> bulk_write(std::uint8_t, ByteView, Timeout) noexcept override
    {
        return Result<std::size_t>::failure(Error::UNSUPPORTED);
    }

    Result<void> start_stream(const StreamConfig&) noexcept override
    {
        active_ = true;
        return Result<void>::success();
    }

    Result<StreamEvent> wait_stream(Timeout) noexcept override
    {
        return Result<StreamEvent>::failure(Error::TIMEOUT);
    }

    Result<void> cancel_stream() noexcept override
    {
        active_ = false;
        return Result<void>::success();
    }

    Result<void> stop_stream() noexcept override
    {
        active_ = false;
        return Result<void>::success();
    }

    bool stream_active() const noexcept override { return active_; }

private:
    bool active_ = false;
};

TunerAttachment attachment(ipc::System system, std::uint64_t id) noexcept
{
    TunerAttachment value{};
    value.owner_client_id = 1U;
    value.lease_id = id;
    value.attachment_id = id + 100U;
    value.receiver = 0U;
    value.system = system;
    value.nonce[0U] = static_cast<std::uint8_t>(id);
    return value;
}

bool check_profile(DeviceModel model, bool dual_system) noexcept
{
    TestTransport device;
    const auto created = Q3U4StreamDataPlane::create_single_receiver(
        device, model, Q3U4StreamDataPlane::kMinQueuePackets);
    if (!created) return false;
    auto& plane = *created.value();
    const auto terrestrial = attachment(ipc::System::ISDB_T, 1U);
    if (!plane.attach(terrestrial) || !plane.detach(terrestrial)) return false;

    const auto satellite = attachment(ipc::System::ISDB_S, 2U);
    const auto satellite_result = plane.attach(satellite);
    if (dual_system) {
        if (!satellite_result || !plane.detach(satellite)) return false;
    } else if (satellite_result.error() != Error::INVALID_ARGUMENT) {
        return false;
    }
    return true;
}

}  // namespace

int main()
{
    const bool passed =
        check_profile(DeviceModel::px_s1ur, false) &&
        check_profile(DeviceModel::dtv03a_1tu, false) &&
        check_profile(DeviceModel::px_m1ur, true) &&
        check_profile(DeviceModel::dtv02_1t1s_u, true) &&
        check_profile(DeviceModel::dtv02a_1t1s_u, true);
    if (!passed) std::fprintf(stderr, "single receiver stream attach test failed\n");
    return passed ? 0 : 1;
}
