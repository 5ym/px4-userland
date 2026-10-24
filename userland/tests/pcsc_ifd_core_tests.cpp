// SPDX-License-Identifier: GPL-2.0-only
#include "px4/pcsc_ifd_adapter.h"

#include "dtv/userland/host_listener_context/enclosure_context.h"
#include "dtv/userland/host_runtime/control_socket.h"
#include "px4/tuner_service.h"
#include <dtv/userland/host_daemon/card_service.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <unistd.h>
#include <sys/stat.h>

namespace {
namespace w = dtv::userland::wire;
using namespace dtv::userland;
using namespace dtv::userland::host_listener_context;
using namespace dtv::userland::host_runtime;
using namespace px4::userland;
using namespace px4::userland::pcsc;

#define CHECK(condition)                                                                    \
    do {                                                                                    \
        if (!(condition)) {                                                                 \
            std::fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__,       \
                         #condition);                                                       \
            return false;                                                                   \
        }                                                                                   \
    } while (false)

class CardBackend final : public CardServiceBackend {
public:
    Result<void> set_power(bool) noexcept override { return Result<void>::success(); }
    Result<void> initialize_uart() noexcept override { return Result<void>::success(); }
    Result<bool> detect_card() noexcept override
    { return Result<bool>::success(present.load(std::memory_order_relaxed)); }
    std::atomic<bool> present{true};
};

class CardSession final : public CardProtocolSession {
public:
    Result<void> initialize() noexcept override
    {
        initialized_ = true;
        atr_.bytes[0U] = 0x3bU;
        atr_.bytes[1U] = 0x00U;
        atr_.length = 2U;
        return Result<void>::success();
    }
    Result<std::size_t> transmit(ByteView, MutableByteView response) noexcept override
    {
        if (response.data == nullptr || response.size < 2U)
            return Result<std::size_t>::failure(Error::BUFFER_TOO_SMALL);
        response.data[0U] = 0x90U;
        response.data[1U] = 0x00U;
        return Result<std::size_t>::success(2U);
    }
    bool initialized() const noexcept override { return initialized_; }
    const CardAtrBytes& atr() const noexcept override { return atr_; }
    void invalidate() noexcept override
    {
        initialized_ = false;
        atr_ = CardAtrBytes{};
    }
private:
    CardAtrBytes atr_{};
    bool initialized_ = false;
};

class TunerBackend final : public TunerServiceBackend {
public:
    Result<void> open_receiver(std::uint8_t) noexcept override { return Result<void>::success(); }
    Result<void> tune_terrestrial(std::uint8_t, std::uint32_t, std::uint32_t) noexcept override
    { return Result<void>::success(); }
    Result<void> tune_satellite(std::uint8_t, std::uint32_t, std::uint32_t) noexcept override
    { return Result<void>::success(); }
    Result<bool> is_locked(std::uint8_t, ipc::System) noexcept override
    { return Result<bool>::success(true); }
    Result<void> select_satellite_slot(std::uint8_t, std::uint8_t, std::uint32_t) noexcept override
    { return Result<void>::success(); }
    Result<void> select_satellite_tsid(std::uint8_t, std::uint16_t, std::uint32_t) noexcept override
    { return Result<void>::success(); }
    Result<void> close_receiver(std::uint8_t) noexcept override { return Result<void>::success(); }
};

class TestTime final : public TunerServiceTime {
public:
    std::uint64_t monotonic_ms() noexcept override
    {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    void sleep_ms(std::uint32_t milliseconds) noexcept override
    { std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds)); }
};

class Nonce final : public TunerNonceSource {
public:
    Result<std::array<std::uint8_t, ipc::kNonceLength>> generate() noexcept override
    { return Result<std::array<std::uint8_t, ipc::kNonceLength>>::success({}); }
};

class Family final : public EnclosureFamily {
public:
    Result<void> shutdown() noexcept override { return Result<void>::success(); }
    TunerServiceBackend& tuner_backend() noexcept override { return tuner_; }
    CardServiceBackend& card_backend() noexcept override { return card_; }
    CardProtocolSession& card_protocol() noexcept override { return card_session_; }
    TunerServiceTime& time() noexcept override { return time_; }
    TunerStreamControl* stream_control() noexcept override { return nullptr; }
    bool card_initialized() const noexcept { return card_session_.initialized(); }
    CardBackend card_;
private:
    TunerBackend tuner_;
    CardSession card_session_;
    TestTime time_;
};

template <std::size_t N>
w::WString<N> text(const char* value)
{
    w::WString<N> result;
    while (value[result.length] != '\0') {
        result.bytes[result.length] = value[result.length];
        ++result.length;
    }
    return result;
}

w::ListResponse make_list()
{
    w::ListResponse list;
    for (std::size_t i = 0U; i < list.epoch.size(); ++i)
        list.epoch[i] = static_cast<std::uint8_t>(i + 1U);
    list.producer.product = text<32U>("px4-userland");
    list.producer.version = text<64U>("1");
    list.topology.state = w::TopologyState::complete;
    list.topology.expected_function_count.present = true;
    list.topology.expected_function_count.value = 1U;
    list.functions.count = 1U;
    auto& function = list.functions.items[0U];
    function.id = 1U;
    function.vid = 0x0511U;
    function.pid = 0x084aU;
    function.mode = w::Mode::runtime;
    function.serial_state = w::SerialState::absent;
    function.identification_reason = w::IdentificationReason::descriptor_unreadable;
    function.present = true;
    list.enclosure.present = true;
    list.enclosure.value.id = 0U;
    list.enclosure.value.identity.serial_state = w::IdentitySerialState::absent;
    list.receivers.present = true;
    list.receivers.value.count = 1U;
    list.receivers.value.items[0U].id = 0U;
    list.receivers.value.items[0U].systems.mask = 1U;
    list.receivers.value.items[0U].binding.present = true;
    list.receivers.value.items[0U].binding.value.function_id = 1U;
    list.readers.present = true;
    list.readers.value.count = 1U;
    list.readers.value.items[0U].id = 0U;
    return list;
}

bool test_ifd_against_core_context()
{
    const auto root = std::filesystem::temp_directory_path() /
        ("px4-ifd-core-" + std::to_string(static_cast<unsigned long>(::getpid())));
    const std::string runtime_directory = root.string();
    std::error_code error;
    std::filesystem::create_directories(root, error);
    CHECK(!error);
    CHECK(::chmod(root.c_str(), 0700) == 0);
    Family* family_ptr = new Family;
    std::unique_ptr<EnclosureFamily> family(family_ptr);
    ContextConfig config;
    config.runtime_directory = runtime_directory;
    config.product = "px4-userland";
    config.instance = "ifd-test";
    config.server.epoch = make_list().epoch;
    config.server.producer = make_list().producer;
    config.server.features = w::kFeatureCardService;
    config.canonical_list = make_list();
    config.context.epoch = config.server.epoch;
    config.context.instance.present = true;
    config.context.instance.value = text<64U>("ifd-test");
    config.stream_engine_epoch = 0x505834494644ULL;
    config.stream_arm_budget_ms = 5000U;
    auto assembled = EnclosureContext::assemble(std::move(family),
        std::unique_ptr<Nonce>(new Nonce), config);
    CHECK(assembled);
    CHECK(assembled.value()->reserve_namespace());
    CHECK(assembled.value()->publish());
    auto context = std::move(assembled.value());
    std::atomic<bool> serving{true};
    std::thread server([&] {
        while (serving.load(std::memory_order_acquire))
            (void)context->poll_once(Timeout{100U});
    });

    PosixIfdCardClientFactory factory;
    IfdAdapter adapter(factory);
    const std::string device_name = "px4-userland:runtime=" + root.string() +
        ":instance=ifd-test:access=user";
    const IfdResult opened = adapter.create_channel_by_name(0U, device_name.c_str());
    if (opened != IfdResult::success) {
        serving.store(false, std::memory_order_release);
        server.join();
        (void)context->close(Timeout{1000U});
        std::filesystem::remove_all(root, error);
        std::fprintf(stderr, "IFD Core bridge open result=%u\n", static_cast<unsigned>(opened));
        return false;
    }
    bool passed = true;
    const auto check = [](bool condition, const char* message) {
        if (!condition) std::fprintf(stderr, "IFD Core bridge check failed: %s\n", message);
        return condition;
    };
    do {
        const auto presence = adapter.presence(0U);
        if (!check(presence == IfdResult::icc_present, "initial presence") ) {
            std::fprintf(stderr, "presence result=%u\n", static_cast<unsigned>(presence));
            passed = false;
            break;
        }
        if (!check(!family_ptr->card_initialized(), "presence does not initialize card")) {
            passed = false;
            break;
        }
        if (!check(adapter.presence(0U) == IfdResult::icc_present,
                  "repeated initial presence")) { passed = false; break; }
        if (!check(!family_ptr->card_initialized(), "repeated presence does not initialize card")) {
            passed = false;
            break;
        }
        std::array<std::uint8_t, kIfdAtrMaxLength> atr{};
        std::size_t atr_length = atr.size();
        if (!check(adapter.power(0U, IfdPowerAction::power_up,
                  {atr.data(), atr.size()}, atr_length) == IfdResult::success,
                  "POWER_UP")) { passed = false; break; }
        if (!check(family_ptr->card_initialized(), "POWER_UP initializes card")) {
            passed = false;
            break;
        }
        if (!check(atr_length == 2U && atr[0U] == 0x3bU && atr[1U] == 0x00U,
                  "ATR")) { passed = false; break; }
        atr_length = atr.size();
        if (!check(adapter.power(0U, IfdPowerAction::power_down,
                  {atr.data(), atr.size()}, atr_length) == IfdResult::success,
                  "POWER_DOWN")) { passed = false; break; }
        if (!check(!family_ptr->card_initialized(), "POWER_DOWN disconnects card")) {
            passed = false;
            break;
        }
        if (!check(atr_length == 0U, "POWER_DOWN ATR clear")) { passed = false; break; }
        if (!check(adapter.presence(0U) == IfdResult::icc_present,
                  "presence after POWER_DOWN")) { passed = false; break; }
        std::uint8_t interface_status = 1U;
        std::size_t interface_status_length = sizeof(interface_status);
        if (!check(adapter.get_capability(0U, kAttrIccInterfaceStatus,
                  {&interface_status, sizeof(interface_status)}, interface_status_length) ==
                  IfdResult::success, "interface status")) { passed = false; break; }
        if (!check(interface_status == 0U, "disconnected interface status")) {
            passed = false;
            break;
        }
        family_ptr->card_.present.store(false, std::memory_order_relaxed);
        if (!check(adapter.presence(0U) == IfdResult::icc_not_present,
                  "presence after card removal")) { passed = false; break; }
        if (!check(adapter.close_channel(0U) == IfdResult::success,
                  "close channel")) { passed = false; break; }
    } while (false);

    serving.store(false, std::memory_order_release);
    server.join();
    if (!context->close(Timeout{1000U})) passed = false;
    context.reset();
    std::filesystem::remove_all(root, error);
    return passed && !error;
}
}

int main()
{
    if (!test_ifd_against_core_context()) return 1;
    std::puts("PASS px4_ifd_core_bridge");
    return 0;
}
