// Modified/ported for px4-userland on 2026-10-26.
//
// Copyright (c) 2018-2021 nns779
// Derived from tsukumijima/px4_drv commit d748866f0da1cb3656106a520de4e9d7f073aacd (v0.6.1).
// Origin paths: driver/cxd2856er.c, driver/cxd2858er.c.
// Source snapshot maintained by tsukumijima.
//
// The expected J.83 Annex B register sequences are written from cxd2878.c in
// koreapyj/cxd28xx at commit c5fa27eecbe80633cbb365cc44656daea1c0e2e3 and
// compared with tsukumijima/px4_drv pull request #46 at commit
// 2ce5304043d5efd47142ab247e6b815a16302c93 (driver/cxd2856er.c,
// driver/cxd2858er.c); unlike pull request #46 they keep the bank 0x10 select
// at the start of SLtoACC_BandSetting().  The TS clock, TS output, post-tune
// and tuner stop steps are the existing px4-userland steps derived from the
// px4_drv snapshot above.
// Copyright (c) 2022 Sony Semiconductor Solutions Corporation
// Copyright (c) 2021 Davin zhang <Davin@tbsdtv.com> www.Turbosight.com
// Copyright (c) 2026 Yoonji Park <koreapyj@dcmys.kr>
// SPDX-License-Identifier: GPL-2.0-only
#include "mlt5pe_frontend.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <map>
#include <tuple>
#include <vector>

namespace {

using namespace px4::userland;

#define J83B_CHECK(condition)                                                         \
    do {                                                                              \
        if (!(condition)) {                                                           \
            std::fprintf(stderr, "mlt_j83b check failed at %s:%d: %s\n", __FILE__,   \
                         __LINE__, #condition);                                       \
            return false;                                                             \
        }                                                                             \
    } while (false)

struct Operation final {
    bool read;
    std::uint8_t address;
    // Written bytes (register pointer first), or the read length.
    std::vector<std::uint8_t> data;

    bool operator==(const Operation& other) const
    {
        return read == other.read && address == other.address && data == other.data;
    }
};

Operation W(std::uint8_t address, std::initializer_list<std::uint8_t> data)
{
    return Operation{false, address, std::vector<std::uint8_t>(data)};
}

Operation R(std::uint8_t address)
{
    return Operation{true, address, {1U}};
}

// One bridge I2C bus with byte registers per 7-bit address and per bank
// (register 0x00 selects the bank), recording every operation.  fail_at makes
// the operation with that index fail.
class RecordingBus final : public BridgeI2cMaster {
public:
    Result<void> request(BridgeI2cRequest* requests, std::size_t count) noexcept override
    {
        for (std::size_t index = 0U; index < count; ++index) {
            BridgeI2cRequest& request = requests[index];
            if (fail_at >= 0 && operations.size() == static_cast<std::size_t>(fail_at))
                return Result<void>::failure(Error::USB_IO);
            if (request.type == BridgeI2cRequestType::write) {
                std::vector<std::uint8_t> data(request.write_data.data,
                                               request.write_data.data + request.write_data.size);
                operations.push_back(Operation{false, request.address, data});
                pointer[request.address] = data[0U];
                for (std::size_t offset = 1U; offset < data.size(); ++offset) {
                    const auto reg = static_cast<std::uint8_t>(data[0U] + offset - 1U);
                    if (reg == 0x00U && offset == 1U) bank[request.address] = data[offset];
                    registers[key(request.address, reg)] = data[offset];
                }
            } else {
                operations.push_back(Operation{
                    true, request.address,
                    {static_cast<std::uint8_t>(request.read_data.size)}});
                for (std::size_t offset = 0U; offset < request.read_data.size; ++offset) {
                    const auto reg = static_cast<std::uint8_t>(pointer[request.address] + offset);
                    const auto found = registers.find(key(request.address, reg));
                    request.read_data.data[offset] =
                        found == registers.end() ? 0U : found->second;
                }
            }
        }
        return Result<void>::success();
    }

    void set(std::uint8_t address, std::uint8_t bank_value, std::uint8_t reg,
             std::uint8_t value)
    {
        registers[std::make_tuple(address, bank_value, reg)] = value;
    }

    std::vector<Operation> since(std::size_t mark) const
    {
        return std::vector<Operation>(operations.begin() + static_cast<std::ptrdiff_t>(mark),
                                      operations.end());
    }

    std::vector<Operation> operations;
    std::map<std::tuple<std::uint8_t, std::uint8_t, std::uint8_t>, std::uint8_t> registers;
    std::map<std::uint8_t, std::uint8_t> pointer;
    std::map<std::uint8_t, std::uint8_t> bank;
    long fail_at = -1;

private:
    std::tuple<std::uint8_t, std::uint8_t, std::uint8_t> key(std::uint8_t address,
                                                            std::uint8_t reg)
    {
        return std::make_tuple(address, reg == 0x00U ? std::uint8_t{0U} : bank[address], reg);
    }
};

class RecordingDelay final : public Mlt5PeDelay {
public:
    void sleep_ms(std::uint32_t milliseconds) noexcept override
    {
        sleeps.push_back(milliseconds);
    }
    std::vector<std::uint32_t> sleeps;
};

class NullPower final : public Q3U4BackendPower {
public:
    Result<void> set_backend_power(bool, Q3U4Delay&) noexcept override
    {
        return Result<void>::success();
    }
};

bool contains(const std::vector<Operation>& operations, const Operation& wanted)
{
    return std::find(operations.begin(), operations.end(), wanted) != operations.end();
}

bool starts_with(const std::vector<Operation>& operations,
                 const std::vector<Operation>& prefix)
{
    return operations.size() >= prefix.size() &&
           std::equal(prefix.begin(), prefix.end(), operations.begin());
}

// FNV-1a over the recorded I2C operations and delays.
std::uint64_t fingerprint(const std::vector<Operation>& operations,
                          const std::vector<std::uint32_t>& sleeps)
{
    std::uint64_t hash = 14695981039346656037ULL;
    const auto mix = [&hash](std::uint8_t value) {
        hash ^= value;
        hash *= 1099511628211ULL;
    };
    for (const Operation& operation : operations) {
        mix(operation.read ? 1U : 0U);
        mix(operation.address);
        mix(static_cast<std::uint8_t>(operation.data.size()));
        for (const std::uint8_t value : operation.data) mix(value);
    }
    for (const std::uint32_t value : sleeps) {
        for (unsigned shift = 0U; shift < 32U; shift += 8U)
            mix(static_cast<std::uint8_t>(value >> shift));
    }
    return hash;
}

// Receiver 1 of PX-MLT5PE: demodulator SLVT 0x6c / SLVX 0x6e and tuner 0x60
// on bus 1.
constexpr std::uint8_t kT = 0x6cU;
constexpr std::uint8_t kX = 0x6eU;
constexpr std::uint8_t kTuner = 0x60U;
constexpr std::uint32_t kFrequencyKhz = 599143U;  // 0x092467

// Demodulator wakeup (cxd2878.c SLtoACC() for 5.60 Msps, with the PX-MLT TS
// clock), tuner tune (cxd2878.c helene_tune_terr() with the
// SONY_HELENE_DTV_J83B_5_6 row) and post-tune,
// from a sleeping demodulator after open.
std::vector<Operation> expected_j83b_tune()
{
    return {
        // TS clock: the existing ISDB-S steps (gated, half rate, high
        // frequency) with OTSRATECTRLOFF = 0.
        W(kT, {0x00, 0x00}), W(kT, {0xc4}), R(kT),
        W(kT, {0xd3}), R(kT), W(kT, {0xd3, 0x00}),
        W(kT, {0xde}), R(kT), W(kT, {0xde, 0x00}),
        W(kT, {0xda}), R(kT), W(kT, {0xda, 0x00}),
        W(kT, {0xc4}), R(kT), W(kT, {0xc4, 0x80}),
        W(kT, {0xd1}), R(kT), W(kT, {0xd1, 0x02}),
        W(kT, {0xd9, 0x10}),
        W(kT, {0x32}), R(kT), W(kT, {0x32, 0x00}),
        W(kT, {0x33}), R(kT), W(kT, {0x33, 0x00}),
        W(kT, {0x32}), R(kT), W(kT, {0x32, 0x01}),
        W(kT, {0x00, 0x10}), W(kT, {0x66}), R(kT), W(kT, {0x66, 0x01}),
        W(kT, {0x00, 0x40}), W(kT, {0x66}), R(kT), W(kT, {0x66, 0x01}),
        // SLtoACC().
        W(kX, {0x00, 0x00}), W(kX, {0x17, 0x08}),
        W(kT, {0x00, 0x00}), W(kT, {0xa9, 0x00}), W(kT, {0x2c, 0x01}),
        W(kT, {0x4b, 0x74}), W(kT, {0x49, 0x00}),
        W(kX, {0x18, 0x00}),
        W(kT, {0x00, 0x11}), W(kT, {0x6a, 0x48}),
        W(kT, {0x00, 0x10}), W(kT, {0xa5, 0x01}),
        W(kT, {0x00, 0x40}), W(kT, {0x21, 0x04}), W(kT, {0xb3, 0x05}),
        W(kT, {0x1e, 0x80}), W(kT, {0x8e, 0x58}),
        W(kT, {0x00, 0x41}), W(kT, {0xcf, 0x99}),
        W(kT, {0x00, 0x11}), W(kT, {0xa3, 0x14}),
        W(kT, {0x33, 0x00, 0x03, 0x3b}),
        W(kT, {0x00, 0x00}), W(kT, {0xce, 0x01, 0x01}),
        W(kT, {0x00, 0x10}),
        W(kT, {0xa6, 0x31, 0xa8, 0x29, 0x9b, 0x27, 0x9c, 0x28, 0x9e, 0x29, 0xa4, 0x29,
               0xa2, 0x29, 0xa8}),
        // SLtoACC_BandSetting(), 5.60 Msps: bank 0x10 again, IF 3.75 MHz,
        // bank 0x40 register 0x14 (datax14) and 0x26-0x29 (symRate).
        W(kT, {0x00, 0x10}),
        W(kT, {0xb6, 0x14, 0x00, 0x00}),
        W(kT, {0x00, 0x40}), W(kT, {0x14, 0x10}),
        W(kT, {0x26, 0x07, 0x77, 0x07, 0x77}),
        // TS output on.
        W(kT, {0x00, 0x00}), W(kT, {0x80}), R(kT), W(kT, {0x80, 0x08}),
        W(kT, {0x00, 0x00}), W(kT, {0xc4}), R(kT),
        W(kT, {0x00, 0x00}), W(kT, {0x81}), R(kT), W(kT, {0x81, 0x00}),
        // Tuner, through the demodulator I2C gate.
        W(kX, {0x08, 0x01}),
        W(kTuner, {0x01, 0x00}), W(kTuner, {0x74, 0x02}),
        W(kTuner, {0x87, 0xc4, 0x40}),
        W(kTuner, {0x91, 0x16, 0x26}),
        W(kTuner, {0x9c, 0x00, 0x00}),
        W(kTuner, {0x5e, 0xee, 0x02, 0x1e, 0x67, 0x10, 0x1c, 0x78, 0x08, 0x1c}),
        W(kTuner, {0x67}), R(kTuner), W(kTuner, {0x67, 0x00}),
        W(kTuner, {0x68, 0x00, 0x84, 0x00, 0x02, 0x21, 0x00, 0x1b, 0x02, 0x67, 0x24, 0x09,
                   0xff, 0x01, 0xd9, 0x0f, 0x24, 0x87}),
        W(kTuner, {0x88, 0x00}), W(kTuner, {0x87, 0xc0}),
        W(kX, {0x08, 0x00}),
        // Post-tune.
        W(kT, {0x00, 0x00}), W(kT, {0xfe, 0x01}), W(kT, {0xc3, 0x00}),
    };
}

// The J.83B demodulator sleep: the existing TS output stop, then the
// SONY_DTV_SYSTEM_J83B branch and the common tail of cxd2878_sleep().
std::vector<Operation> expected_j83b_sleep()
{
    return {
        // TS output off.
        W(kT, {0x00, 0x00}), W(kT, {0xc3, 0x01}),
        W(kT, {0x80}), R(kT), W(kT, {0x80, 0x1f}),
        W(kT, {0x00, 0x00}), W(kT, {0xc4}), R(kT),
        W(kT, {0x00, 0x00}), W(kT, {0x81}), R(kT), W(kT, {0x81, 0x01}),
        // cxd2878_sleep(), SONY_DTV_SYSTEM_J83B: restores part of the settings.
        W(kT, {0x00, 0x11}), W(kT, {0xa3, 0x00}),
        W(kT, {0x00, 0x40}), W(kT, {0x21, 0x00}), W(kT, {0xc3, 0x00}),
        W(kT, {0xb3, 0x02}), W(kT, {0x1e, 0x00}), W(kT, {0x8e, 0x0e}),
        W(kT, {0x00, 0x41}), W(kT, {0xcf, 0x77}),
        W(kT, {0x00, 0x40}), W(kT, {0x14, 0x1f}),
        W(kT, {0x26, 0x09, 0x9a, 0x00, 0xee}),
        // Common analog power-down.
        W(kX, {0x00, 0x00}), W(kX, {0x18, 0x01}),
        W(kT, {0x00, 0x00}), W(kT, {0x49, 0x33}), W(kT, {0x4b, 0x21}),
        W(kT, {0xfe, 0x01}), W(kT, {0x2c, 0x00}), W(kT, {0xa9, 0x00}),
        W(kX, {0x17, 0x01}),
    };
}

// The unchanged terrestrial tuner stop that close performs before the
// demodulator sleep.
std::vector<Operation> expected_tuner_stop()
{
    return {
        W(kX, {0x08, 0x01}),
        W(kTuner, {0x74, 0x02}), W(kTuner, {0x67}), R(kTuner), W(kTuner, {0x67, 0x00}),
        W(kTuner, {0x5e, 0x15, 0x00, 0x00}),
        W(kTuner, {0x88, 0x00}), W(kTuner, {0x87, 0xc0}),
        W(kX, {0x08, 0x00}),
    };
}

std::vector<Operation> concat(std::vector<Operation> first, const std::vector<Operation>& second)
{
    first.insert(first.end(), second.begin(), second.end());
    return first;
}

struct Fixture final {
    RecordingBus bus1;
    RecordingBus bus3;
    RecordingDelay delay;
    NullPower power;
    Mlt5PeFrontend frontend{bus1, bus3, power, delay};
};

// The hash of the I2C operations and the wait sequence of one receiver's
// ISDB-T/S open, tune, lock, retune and close matches the one recorded before
// J.83B was added.  The changes that these paths pass through are the
// reordered branches of Cxd2856er::wakeup(),
// the J.83B branch at the top of Cxd2856er::sleep(), the 0xd3 value in
// Cxd2856er::set_ts_clock(), the conditions of Cxd2858er::stop_t() and
// Cxd2858er::set_params_s_gated(), the swapped branch of Cxd2858er::terminate(),
// and the system dispatch in Mlt5PeFrontend::tune().
bool test_isdb_t_s_sequences_unchanged()
{
    Fixture fixture;
    Mlt5PeFrontend& frontend = fixture.frontend;
    J83B_CHECK(frontend.open_receiver(1U));
    J83B_CHECK(frontend.tune(1U, Cxd2856erSystem::isdb_t, 557143U));
    fixture.bus1.set(kT, 0x60U, 0x10U, 0x01U);
    J83B_CHECK(frontend.is_terrestrial_locked(1U).value());
    J83B_CHECK(frontend.select_satellite_tsid(1U, 0x4010U));
    J83B_CHECK(frontend.tune(1U, Cxd2856erSystem::isdb_s, 1049480U));
    fixture.bus1.set(kT, 0xa0U, 0x12U, 0x40U);
    J83B_CHECK(frontend.is_satellite_locked(1U).value());
    J83B_CHECK(frontend.tune(1U, Cxd2856erSystem::isdb_t, 473143U));
    J83B_CHECK(frontend.close_receiver(1U));
    J83B_CHECK(fixture.bus3.operations.empty());
    // Recorded from main at dee4f33f7b6e3f9474ebaf118bb2688a22b19294.
    J83B_CHECK(fixture.bus1.operations.size() == 486U);
    J83B_CHECK((fixture.delay.sleeps == std::vector<std::uint32_t>{4U, 1U, 1U, 10U, 1U, 50U,
                                                                  10U, 50U}));
    J83B_CHECK(fingerprint(fixture.bus1.operations, fixture.delay.sleeps) ==
               0x12919b0652816462ULL);
    return true;
}

// Same-system retunes and a close from ISDB-S, in addition to the sequence
// above.
bool test_isdb_t_s_retune_and_satellite_close_unchanged()
{
    Fixture fixture;
    Mlt5PeFrontend& frontend = fixture.frontend;
    J83B_CHECK(frontend.open_receiver(1U));
    J83B_CHECK(frontend.tune(1U, Cxd2856erSystem::isdb_t, 557143U));
    J83B_CHECK(frontend.tune(1U, Cxd2856erSystem::isdb_t, 473143U));
    J83B_CHECK(frontend.select_satellite_tsid(1U, 0x4010U));
    J83B_CHECK(frontend.tune(1U, Cxd2856erSystem::isdb_s, 1049480U));
    J83B_CHECK(frontend.select_satellite_slot(1U, 1U));
    J83B_CHECK(frontend.tune(1U, Cxd2856erSystem::isdb_s, 1087840U));
    J83B_CHECK(frontend.tune(1U, Cxd2856erSystem::isdb_t, 557143U));
    J83B_CHECK(frontend.select_satellite_tsid(1U, 0x4011U));
    J83B_CHECK(frontend.tune(1U, Cxd2856erSystem::isdb_s, 1049480U));
    J83B_CHECK(frontend.close_receiver(1U));
    J83B_CHECK(fixture.bus3.operations.empty());
    // Recorded from main at dee4f33f7b6e3f9474ebaf118bb2688a22b19294.
    J83B_CHECK(fixture.bus1.operations.size() == 640U);
    J83B_CHECK((fixture.delay.sleeps == std::vector<std::uint32_t>{4U, 1U, 1U, 10U, 1U, 50U, 50U,
                                                                  10U, 10U, 50U, 10U}));
    J83B_CHECK(fingerprint(fixture.bus1.operations, fixture.delay.sleeps) ==
               0x7cd075c7c215c52cULL);
    return true;
}

bool test_j83b_tune_and_close_sequences()
{
    Fixture fixture;
    Mlt5PeFrontend& frontend = fixture.frontend;
    J83B_CHECK(frontend.open_receiver(1U));
    std::size_t mark = fixture.bus1.operations.size();
    const std::size_t sleeps = fixture.delay.sleeps.size();
    J83B_CHECK(frontend.tune(1U, Cxd2856erSystem::j83b, kFrequencyKhz));
    J83B_CHECK(fixture.bus1.since(mark) == expected_j83b_tune());
    // The tuner waits 50 ms after the tune command.
    J83B_CHECK((std::vector<std::uint32_t>(fixture.delay.sleeps.begin() +
                                               static_cast<std::ptrdiff_t>(sleeps),
                                           fixture.delay.sleeps.end()) ==
                std::vector<std::uint32_t>{50U}));
    J83B_CHECK(frontend.receiver_state(1U) == Mlt5PeReceiverState::tuned);

    // Lock: bank 0x40 register 0x88 bit 0 and register 0x10 bit 5.  Register
    // 0x10 is read only when bit 0 of 0x88 is set.
    const std::vector<Operation> sync_read{W(kT, {0x00, 0x40}), W(kT, {0x88}), R(kT)};
    const std::vector<Operation> lock_read =
        concat(sync_read, std::vector<Operation>{W(kT, {0x10}), R(kT)});
    mark = fixture.bus1.operations.size();
    J83B_CHECK(frontend.is_j83b_locked(1U).value() == false);
    J83B_CHECK(fixture.bus1.since(mark) == sync_read);
    fixture.bus1.set(kT, 0x40U, 0x88U, 0x01U);
    mark = fixture.bus1.operations.size();
    J83B_CHECK(frontend.is_j83b_locked(1U).value() == false);
    J83B_CHECK(fixture.bus1.since(mark) == lock_read);
    fixture.bus1.set(kT, 0x40U, 0x10U, 0x20U);
    mark = fixture.bus1.operations.size();
    J83B_CHECK(frontend.is_j83b_locked(1U).value() == true);
    J83B_CHECK(fixture.bus1.since(mark) == lock_read);
    fixture.bus1.set(kT, 0x40U, 0x88U, 0x00U);
    mark = fixture.bus1.operations.size();
    J83B_CHECK(frontend.is_j83b_locked(1U).value() == false);
    J83B_CHECK(fixture.bus1.since(mark) == sync_read);
    J83B_CHECK(frontend.is_terrestrial_locked(1U).error() == Error::NOT_READY);
    J83B_CHECK(frontend.is_satellite_locked(1U).error() == Error::NOT_READY);

    J83B_CHECK(frontend.start_capture(1U));
    // The lock can still be read while capturing.
    J83B_CHECK(frontend.is_j83b_locked(1U).value() == false);
    J83B_CHECK(frontend.stop_capture(1U));

    // Close stops the tuner and runs the J.83B demodulator sleep.
    mark = fixture.bus1.operations.size();
    J83B_CHECK(frontend.close_receiver(1U));
    J83B_CHECK(fixture.bus1.since(mark) == concat(expected_tuner_stop(), expected_j83b_sleep()));
    J83B_CHECK(fixture.bus3.operations.empty());
    return true;
}

// Retuning J.83B restarts it from a completed sleep, and leaving J.83B for
// ISDB-T runs the J.83B sleep before the unchanged ISDB-T wakeup.
bool test_j83b_retune_restores_first()
{
    // The ISDB-T tune traffic of a freshly opened receiver.
    Fixture reference;
    J83B_CHECK(reference.frontend.open_receiver(1U));
    const std::size_t reference_mark = reference.bus1.operations.size();
    J83B_CHECK(reference.frontend.tune(1U, Cxd2856erSystem::isdb_t, 557143U));
    const std::vector<Operation> isdb_t_tune = reference.bus1.since(reference_mark);

    Fixture fixture;
    Mlt5PeFrontend& frontend = fixture.frontend;
    J83B_CHECK(frontend.open_receiver(1U));
    J83B_CHECK(frontend.tune(1U, Cxd2856erSystem::j83b, kFrequencyKhz));
    std::size_t mark = fixture.bus1.operations.size();
    J83B_CHECK(frontend.tune(1U, Cxd2856erSystem::j83b, kFrequencyKhz));
    J83B_CHECK(fixture.bus1.since(mark) == concat(expected_j83b_sleep(), expected_j83b_tune()));

    mark = fixture.bus1.operations.size();
    J83B_CHECK(frontend.tune(1U, Cxd2856erSystem::isdb_t, 557143U));
    J83B_CHECK(fixture.bus1.since(mark) == concat(expected_j83b_sleep(), isdb_t_tune));
    J83B_CHECK(frontend.is_j83b_locked(1U).error() == Error::NOT_READY);
    J83B_CHECK(frontend.close_receiver(1U));

    // From ISDB-S the existing ISDB-S demodulator sleep runs first, then the
    // J.83B wakeup; the tuner runs the existing satellite stop right after the
    // gate opens for the J.83B tune.  Both stop sequences are taken from a
    // close after an ISDB-S tune: gate open, satellite stop, gate close, and
    // the ISDB-S demodulator sleep.
    Fixture satellite_reference;
    J83B_CHECK(satellite_reference.frontend.open_receiver(1U));
    J83B_CHECK(satellite_reference.frontend.tune(1U, Cxd2856erSystem::isdb_s, 1049480U));
    const std::size_t close_mark = satellite_reference.bus1.operations.size();
    J83B_CHECK(satellite_reference.frontend.close_receiver(1U));
    const std::vector<Operation> closed = satellite_reference.bus1.since(close_mark);
    J83B_CHECK(!closed.empty() && closed.front() == W(kX, {0x08, 0x01}));
    const auto gate_closed = std::find(closed.begin(), closed.end(), W(kX, {0x08, 0x00}));
    J83B_CHECK(gate_closed != closed.end());
    const std::vector<Operation> satellite_stop(closed.begin() + 1, gate_closed);
    const std::vector<Operation> isdb_s_sleep(gate_closed + 1, closed.end());
    J83B_CHECK(contains(satellite_stop, W(kTuner, {0x15, 0x02})));
    J83B_CHECK(contains(isdb_s_sleep, W(kT, {0x6a, 0x11})));

    std::vector<Operation> expected = expected_j83b_tune();
    const auto gate = std::find(expected.begin(), expected.end(), W(kX, {0x08, 0x01}));
    J83B_CHECK(gate != expected.end());
    expected.insert(gate + 1, satellite_stop.begin(), satellite_stop.end());
    expected = concat(isdb_s_sleep, expected);

    Fixture satellite;
    J83B_CHECK(satellite.frontend.open_receiver(1U));
    J83B_CHECK(satellite.frontend.tune(1U, Cxd2856erSystem::isdb_s, 1049480U));
    mark = satellite.bus1.operations.size();
    J83B_CHECK(satellite.frontend.tune(1U, Cxd2856erSystem::j83b, kFrequencyKhz));
    J83B_CHECK(satellite.bus1.since(mark) == expected);
    J83B_CHECK(satellite.frontend.close_receiver(1U));
    return true;
}

// Switching from ISDB-T to J.83B runs the existing ISDB-T demodulator sleep
// (the unchecked sleep() in Cxd2856er::wakeup()) and then the J.83B wakeup;
// the tuner goes from ISDB-T to J.83B without a stop, as in pull request #46.
bool test_isdb_t_to_j83b()
{
    // The ISDB-T demodulator sleep, taken from a close after an ISDB-T tune.
    Fixture reference;
    J83B_CHECK(reference.frontend.open_receiver(1U));
    J83B_CHECK(reference.frontend.tune(1U, Cxd2856erSystem::isdb_t, 557143U));
    const std::size_t reference_mark = reference.bus1.operations.size();
    J83B_CHECK(reference.frontend.close_receiver(1U));
    const std::vector<Operation> closed = reference.bus1.since(reference_mark);
    const std::vector<Operation> stop = expected_tuner_stop();
    J83B_CHECK(starts_with(closed, stop));
    const std::vector<Operation> isdb_t_sleep(
        closed.begin() + static_cast<std::ptrdiff_t>(stop.size()), closed.end());

    Fixture fixture;
    Mlt5PeFrontend& frontend = fixture.frontend;
    J83B_CHECK(frontend.open_receiver(1U));
    J83B_CHECK(frontend.tune(1U, Cxd2856erSystem::isdb_t, 557143U));
    const std::size_t mark = fixture.bus1.operations.size();
    J83B_CHECK(frontend.tune(1U, Cxd2856erSystem::j83b, kFrequencyKhz));
    J83B_CHECK(fixture.bus1.since(mark) == concat(isdb_t_sleep, expected_j83b_tune()));
    J83B_CHECK(frontend.close_receiver(1U));
    return true;
}

// Leaving J.83B for ISDB-S runs the J.83B sleep first; the tuner then stops
// the terrestrial path before the ISDB-S tune, and the rest is the ISDB-S
// traffic of a freshly opened receiver.
bool test_j83b_to_isdb_s()
{
    Fixture reference;
    J83B_CHECK(reference.frontend.open_receiver(1U));
    J83B_CHECK(reference.frontend.select_satellite_tsid(1U, 0x4010U));
    const std::size_t reference_mark = reference.bus1.operations.size();
    J83B_CHECK(reference.frontend.tune(1U, Cxd2856erSystem::isdb_s, 1049480U));
    std::vector<Operation> expected = reference.bus1.since(reference_mark);
    // The tuner stop runs right after the gate opens for the ISDB-S tune.
    const auto gate = std::find(expected.begin(), expected.end(), W(kX, {0x08, 0x01}));
    J83B_CHECK(gate != expected.end());
    const std::vector<Operation> stop = expected_tuner_stop();
    expected.insert(gate + 1, stop.begin() + 1, stop.end() - 1);
    expected = concat(expected_j83b_sleep(), expected);

    Fixture fixture;
    Mlt5PeFrontend& frontend = fixture.frontend;
    J83B_CHECK(frontend.open_receiver(1U));
    J83B_CHECK(frontend.tune(1U, Cxd2856erSystem::j83b, kFrequencyKhz));
    J83B_CHECK(frontend.select_satellite_tsid(1U, 0x4010U));
    const std::size_t mark = fixture.bus1.operations.size();
    J83B_CHECK(frontend.tune(1U, Cxd2856erSystem::isdb_s, 1049480U));
    J83B_CHECK(fixture.bus1.since(mark) == expected);
    J83B_CHECK(frontend.is_j83b_locked(1U).error() == Error::NOT_READY);
    J83B_CHECK(frontend.close_receiver(1U));
    return true;
}

// A failed J.83B sleep keeps the demodulator in J.83B and refuses ISDB-T until
// the sleep succeeds.
bool test_j83b_restore_failure_blocks_other_systems()
{
    Fixture fixture;
    Mlt5PeFrontend& frontend = fixture.frontend;
    J83B_CHECK(frontend.open_receiver(1U));
    J83B_CHECK(frontend.tune(1U, Cxd2856erSystem::j83b, kFrequencyKhz));

    const std::vector<Operation> sleep = expected_j83b_sleep();
    for (std::size_t failing = 0U; failing < sleep.size(); ++failing) {
        const std::size_t mark = fixture.bus1.operations.size();
        fixture.bus1.fail_at = static_cast<long>(mark + failing);
        J83B_CHECK(frontend.tune(1U, Cxd2856erSystem::isdb_t, 557143U).error() ==
                   Error::USB_IO);
        fixture.bus1.fail_at = -1;
        // Only a prefix of the sleep ran; nothing of ISDB-T was written.
        const std::vector<Operation> attempted = fixture.bus1.since(mark);
        J83B_CHECK(attempted.size() == failing);
        J83B_CHECK(std::equal(attempted.begin(), attempted.end(), sleep.begin()));
        J83B_CHECK(frontend.receiver_state(1U) == Mlt5PeReceiverState::open);
        // A receiver whose tune failed reports no J.83B lock.
        J83B_CHECK(frontend.is_j83b_locked(1U).error() == Error::NOT_READY);
    }

    const std::size_t mark = fixture.bus1.operations.size();
    J83B_CHECK(frontend.tune(1U, Cxd2856erSystem::isdb_t, 557143U));
    const std::vector<Operation> recovered = fixture.bus1.since(mark);
    J83B_CHECK(starts_with(recovered, sleep));
    J83B_CHECK(contains(recovered, W(kX, {0x17, 0x06})));

    // A failure while closing leaves the J.83B state to the initialization
    // on the next open; the logical close still completes.
    J83B_CHECK(frontend.tune(1U, Cxd2856erSystem::j83b, kFrequencyKhz));
    fixture.bus1.fail_at = static_cast<long>(fixture.bus1.operations.size() +
                                             expected_tuner_stop().size() + 3U);
    J83B_CHECK(frontend.close_receiver(1U));
    fixture.bus1.fail_at = -1;
    J83B_CHECK(frontend.receiver_state(1U) == Mlt5PeReceiverState::closed);
    const std::size_t reopen = fixture.bus1.operations.size();
    J83B_CHECK(frontend.open_receiver(1U));
    // The initialization starts again (SLVX 0x10 = 0x01 (XSRST)).
    J83B_CHECK(fixture.bus1.operations[reopen + 1U] == W(kX, {0x10, 0x01}));
    const std::size_t retune = fixture.bus1.operations.size();
    J83B_CHECK(frontend.tune(1U, Cxd2856erSystem::isdb_t, 557143U));
    J83B_CHECK(!starts_with(fixture.bus1.since(retune), sleep));
    J83B_CHECK(frontend.close_receiver(1U));
    return true;
}

// A failure in the middle of the J.83B wakeup still leaves the J.83B state,
// so the next tune starts with the J.83B sleep.
bool test_j83b_wakeup_failure_restores_on_next_tune()
{
    const std::vector<Operation> tune = expected_j83b_tune();
    const std::vector<Operation> sleep = expected_j83b_sleep();
    // Fail at the SLVT 0x2c = 0x01 write, the third operation after the SLVX
    // 0x17 = 0x08 mode write.
    const auto mode = std::find(tune.begin(), tune.end(), W(kX, {0x17, 0x08}));
    J83B_CHECK(mode != tune.end());
    const auto failing = static_cast<std::size_t>(mode - tune.begin()) + 3U;

    Fixture fixture;
    Mlt5PeFrontend& frontend = fixture.frontend;
    J83B_CHECK(frontend.open_receiver(1U));
    std::size_t mark = fixture.bus1.operations.size();
    fixture.bus1.fail_at = static_cast<long>(mark + failing);
    J83B_CHECK(frontend.tune(1U, Cxd2856erSystem::j83b, kFrequencyKhz).error() == Error::USB_IO);
    fixture.bus1.fail_at = -1;
    J83B_CHECK(fixture.bus1.operations.size() - mark == failing);
    J83B_CHECK(frontend.receiver_state(1U) == Mlt5PeReceiverState::open);
    J83B_CHECK(frontend.is_j83b_locked(1U).error() == Error::NOT_READY);

    mark = fixture.bus1.operations.size();
    J83B_CHECK(frontend.tune(1U, Cxd2856erSystem::isdb_t, 557143U));
    const std::vector<Operation> next = fixture.bus1.since(mark);
    J83B_CHECK(starts_with(next, sleep));
    J83B_CHECK(contains(next, W(kX, {0x17, 0x06})));
    J83B_CHECK(frontend.close_receiver(1U));
    return true;
}

bool test_j83b_argument_bounds()
{
    Fixture fixture;
    Mlt5PeFrontend& frontend = fixture.frontend;
    J83B_CHECK(frontend.tune(1U, Cxd2856erSystem::j83b, kFrequencyKhz).error() ==
               Error::NOT_READY);
    J83B_CHECK(frontend.is_j83b_locked(5U).error() == Error::INVALID_ARGUMENT);
    J83B_CHECK(frontend.open_receiver(1U));
    J83B_CHECK(frontend.is_j83b_locked(1U).error() == Error::NOT_READY);
    // The tuner takes a 20-bit kHz frequency.
    J83B_CHECK(frontend.tune(1U, Cxd2856erSystem::j83b, 0U).error() ==
               Error::INVALID_ARGUMENT);
    J83B_CHECK(frontend.tune(1U, Cxd2856erSystem::j83b, 0x100000U).error() ==
               Error::INVALID_ARGUMENT);
    J83B_CHECK(frontend.receiver_state(1U) == Mlt5PeReceiverState::open);
    J83B_CHECK(frontend.is_j83b_locked(1U).error() == Error::NOT_READY);
    // The demodulator was already woken in J.83B before the tuner refused the
    // frequency, so close runs the J.83B sleep.
    const std::size_t mark = fixture.bus1.operations.size();
    J83B_CHECK(frontend.close_receiver(1U));
    const std::vector<Operation> closed = fixture.bus1.since(mark);
    const std::vector<Operation> sleep = expected_j83b_sleep();
    J83B_CHECK(closed.size() >= sleep.size());
    J83B_CHECK(std::equal(sleep.begin(), sleep.end(),
                          closed.end() - static_cast<std::ptrdiff_t>(sleep.size())));
    J83B_CHECK(frontend.tune(1U, static_cast<Cxd2856erSystem>(4U), kFrequencyKhz).error() ==
               Error::INVALID_ARGUMENT);
    return true;
}

}  // namespace

bool run_mlt_j83b_tests()
{
    return test_isdb_t_s_sequences_unchanged() &&
           test_isdb_t_s_retune_and_satellite_close_unchanged() &&
           test_j83b_tune_and_close_sequences() && test_j83b_retune_restores_first() &&
           test_j83b_to_isdb_s() && test_isdb_t_to_j83b() &&
           test_j83b_restore_failure_blocks_other_systems() &&
           test_j83b_wakeup_failure_restores_on_next_tune() && test_j83b_argument_bounds();
}
