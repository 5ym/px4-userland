// SPDX-License-Identifier: GPL-2.0-only
#ifndef PX4_USERLAND_WINSCARD_CARD_CLIENT_H
#define PX4_USERLAND_WINSCARD_CARD_CLIENT_H

#include "px4/control_client.h"

#include <memory>
#include <string>
#include <vector>

namespace px4::userland::windows_card {

struct Reader final {
    std::string instance;
    std::string display_name;
};

struct ReaderStatus final {
    bool present = false;
    bool initialized = false;
    std::uint64_t generation = 0U;
    std::vector<std::uint8_t> atr;
};

// runtime_root is the UTF-8 local-absolute runtime parent (normally
// LOCALAPPDATA), not its px4-userland child. Only already-running private
// endpoints with CARD + generation binding and a ready LIST are returned.
// Discovery has a 64-entry cap and a 1500 ms cooperative aggregate deadline;
// it fails rather than returning a partial list if either bound is exhausted.
Result<std::vector<Reader>> list_readers(const std::string& runtime_root) noexcept;

// One current snapshot, not an idle wait. The existing daemon CARD_STATUS may
// temporarily power on/off an otherwise-unused reader to detect presence; it
// does not connect, initialize the card protocol, reset, or send an APDU.
Result<ReaderStatus> reader_status(const std::string& runtime_root,
                                  const std::string& instance) noexcept;

class CardClient final {
public:
    static Result<std::unique_ptr<CardClient>> connect(
        const std::string& runtime_root, const std::string& instance,
        ipc::ShareMode mode) noexcept;
    ~CardClient() noexcept;
    CardClient(const CardClient&) = delete;
    CardClient& operator=(const CardClient&) = delete;
    // No implicit reconnect or retry. Ambiguous I/O and observed card loss
    // permanently close this connection; the wire u64 never leaves this type.
    Result<std::vector<std::uint8_t>> transmit(ByteView apdu) noexcept;
    Result<ReaderStatus> status() noexcept;
    Result<void> disconnect(ipc::Disposition disposition) noexcept;

private:
    explicit CardClient(std::unique_ptr<ipc::posix::PosixControlClient> client,
                        std::uint64_t handle, std::vector<std::uint8_t> atr = {}) noexcept
        : client_(std::move(client)), handle_(handle), atr_(std::move(atr)) {}
    std::unique_ptr<ipc::posix::PosixControlClient> client_;
    std::uint64_t handle_ = 0U;
    std::vector<std::uint8_t> atr_;
};

}  // namespace px4::userland::windows_card

#endif  // PX4_USERLAND_WINSCARD_CARD_CLIENT_H
