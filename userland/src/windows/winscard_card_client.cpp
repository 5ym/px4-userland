// SPDX-License-Identifier: GPL-2.0-only
#include "windows/winscard_card_client.h"

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>

#include "windows/windows_security.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <utility>

namespace px4::userland::windows_card {
namespace {

using namespace ipc;
using namespace ipc::posix;
using Clock = std::chrono::steady_clock;

constexpr std::size_t kMaxDiscoveryEntries = 64U;
constexpr std::uint32_t kDiscoveryTimeoutMs = 1500U;
constexpr std::uint32_t kEndpointTimeoutMs = 200U;
constexpr std::uint32_t kRequestTimeoutMs = 4000U;
constexpr std::uint32_t kRequiredCapabilities =
    kCapabilityCard | kCapabilityCardGenerationBinding;

Timeout remaining_timeout(Clock::time_point deadline) noexcept
{
    const auto now = Clock::now();
    if (now >= deadline) return Timeout{0U};
    // Round down so a sequence of requests cannot each extend the budget.
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - now).count();
    return Timeout{static_cast<std::uint32_t>(remaining)};
}

bool reserved_device_name(std::string_view value) noexcept
{
    // Win32 device names are aliases even inside an ordinary directory and
    // even when followed by an extension. Never pass one to a path API.
    const auto dot = value.find('.');
    const auto base = value.substr(0U, dot);
    std::array<char, 5U> upper{};
    if (base.size() < upper.size()) {
        for (std::size_t i = 0U; i < base.size(); ++i) {
            const char c = base[i];
            upper[i] = c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
        }
        const std::string_view name(upper.data(), base.size());
        if (name == "CON" || name == "PRN" || name == "AUX" || name == "NUL" ||
            (name.size() == 4U && name[3] >= '1' && name[3] <= '9' &&
             (name.substr(0U, 3U) == "COM" || name.substr(0U, 3U) == "LPT")))
            return true;
    }
    return false;
}

bool valid_instance(std::string_view value) noexcept
{
    if (value.empty() || value.size() > 80U || value == "." || value == ".." ||
        value.back() == '.' || reserved_device_name(value)) return false;
    for (const char c : value) {
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') || c == '-' || c == '_' || c == '.')
            continue;
        return false;
    }
    return true;
}

Result<std::wstring> local_runtime_path(const std::string& input) noexcept
{
    // Check syntax before any filesystem call, including FindFirstFile.
    // Accept only a canonical drive-absolute path. UNC, extended/device
    // namespaces, ADS, drive-relative paths and normalization aliases fail
    // closed rather than accidentally probing a remote host.
    if (input.size() < 3U || input.size() >= kStoredPathCapacity ||
        input.find('\0') != std::string::npos ||
        !((input[0] >= 'A' && input[0] <= 'Z') ||
          (input[0] >= 'a' && input[0] <= 'z')) ||
        input[1] != ':' || input[2] != '\\') {
        return Result<std::wstring>::failure(Error::INVALID_ARGUMENT);
    }
    std::array<wchar_t, kStoredPathCapacity> converted{};
    const int count = ::MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()),
        converted.data(), static_cast<int>(converted.size() - 1U));
    if (count <= 0) return Result<std::wstring>::failure(Error::INVALID_ARGUMENT);
    const std::wstring path(converted.data(), static_cast<std::size_t>(count));
    if (path.size() > 3U && path.back() == L'\\')
        return Result<std::wstring>::failure(Error::INVALID_ARGUMENT);
    std::size_t start = 3U;
    for (std::size_t i = start; i <= path.size(); ++i) {
        if (i != path.size() && path[i] != L'\\') {
            const wchar_t c = path[i];
            if (c < 0x20 || c == L'/' || c == L':' || c == L'*' || c == L'?' ||
                c == L'"' || c == L'<' || c == L'>' || c == L'|')
                return Result<std::wstring>::failure(Error::INVALID_ARGUMENT);
            continue;
        }
        if (i == start || path[i - 1U] == L'.' || path[i - 1U] == L' ')
            return Result<std::wstring>::failure(Error::INVALID_ARGUMENT);
        std::string component;
        for (std::size_t j = start; j < i; ++j) {
            if (path[j] > 0x7f) { component.clear(); break; }
            component.push_back(static_cast<char>(path[j]));
        }
        if (reserved_device_name(component))
            return Result<std::wstring>::failure(Error::INVALID_ARGUMENT);
        start = i + 1U;
    }
    return Result<std::wstring>::success(path);
}

Error path_error(DWORD error) noexcept
{
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
        return Error::NOT_FOUND;
    if (error == ERROR_ACCESS_DENIED || error == ERROR_SHARING_VIOLATION ||
        error == ERROR_INVALID_NAME) return Error::INVALID_ARGUMENT;
    return Error::INTERNAL;
}

class DirectoryPin final {
public:
    DirectoryPin() noexcept = default;
    ~DirectoryPin() noexcept { if (handle_ != INVALID_HANDLE_VALUE) ::CloseHandle(handle_); }
    DirectoryPin(const DirectoryPin&) = delete;
    DirectoryPin& operator=(const DirectoryPin&) = delete;
    HANDLE get() const noexcept { return handle_; }

    Result<void> open(const std::wstring& path, bool read_security) noexcept
    {
        // No FILE_SHARE_WRITE/DELETE: hold the checked ancestor in place and
        // prevent converting it into a reparse point until discovery/connect
        // finishes. OPEN_REPARSE_POINT prevents following the final component.
        handle_ = ::CreateFileW(
            path.c_str(), FILE_READ_ATTRIBUTES | (read_security ? READ_CONTROL : 0U),
            FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE)
            return Result<void>::failure(path_error(::GetLastError()));
        BY_HANDLE_FILE_INFORMATION info{};
        if (::GetFileType(handle_) != FILE_TYPE_DISK ||
            ::GetFileInformationByHandle(handle_, &info) == 0 ||
            (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0U ||
            (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U)
            return Result<void>::failure(Error::INVALID_ARGUMENT);
        return Result<void>::success();
    }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

bool verified_security(HANDLE handle, bool private_directory) noexcept
{
    windows_security::SidBuffer sid_storage{};
    PSID current = windows_security::current_user_sid(sid_storage);
    if (current == nullptr) return false;
    PSID owner = nullptr;
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const DWORD status = ::GetSecurityInfo(
        handle, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
        &owner, nullptr, &dacl, nullptr, &descriptor);
    if (status != ERROR_SUCCESS) return false;
    bool valid = windows_security::sid_equal(owner, current) &&
        windows_security::acl_allows_only_trusted_writers(dacl, current);
    if (valid && private_directory) {
        SECURITY_DESCRIPTOR_CONTROL control = 0U;
        DWORD revision = 0U;
        valid = ::GetSecurityDescriptorControl(descriptor, &control, &revision) != 0 &&
                (control & SE_DACL_PROTECTED) != 0U;
        ACL_SIZE_INFORMATION information{};
        valid = valid && ::GetAclInformation(dacl, &information, sizeof(information),
                                             AclSizeInformation) != 0;
        for (DWORD i = 0U; valid && i < information.AceCount; ++i) {
            void* raw = nullptr;
            valid = ::GetAce(dacl, i, &raw) != 0 && raw != nullptr;
            if (!valid) break;
            const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(raw);
            valid = ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE &&
                windows_security::sid_equal(
                    reinterpret_cast<PSID>(const_cast<DWORD*>(&ace->SidStart)), current);
        }
    }
    ::LocalFree(descriptor);
    return valid;
}

struct RuntimePins final {
    std::array<DirectoryPin, 64U> ancestors;
    DirectoryPin product;
    std::wstring product_path;

    Result<void> open(const std::string& runtime) noexcept
    {
        const auto parsed = local_runtime_path(runtime);
        if (!parsed) return Result<void>::failure(parsed.error());
        const auto& path = parsed.value();
        const std::wstring drive = path.substr(0U, 3U);
        const UINT type = ::GetDriveTypeW(drive.c_str());
        if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE && type != DRIVE_RAMDISK)
            return Result<void>::failure(Error::INVALID_ARGUMENT);
        std::size_t count = 0U;
        std::size_t end = 3U;
        for (;;) {
            if (count == ancestors.size())
                return Result<void>::failure(Error::INVALID_ARGUMENT);
            const bool last = end == path.size();
            const auto opened = ancestors[count].open(path.substr(0U, end), last);
            if (!opened) return opened;
            if (last) {
                if (!verified_security(ancestors[count].get(), false))
                    return Result<void>::failure(Error::INVALID_ARGUMENT);
                break;
            }
            ++count;
            const auto separator = path.find(L'\\', end + (end == 3U ? 0U : 1U));
            end = separator == std::wstring::npos ? path.size() : separator;
        }
        product_path = path;
        if (product_path.back() != L'\\') product_path += L'\\';
        product_path += L"px4-userland";
        const auto opened = product.open(product_path, true);
        if (!opened) return opened;
        if (!verified_security(product.get(), true))
            return Result<void>::failure(Error::INVALID_ARGUMENT);
        return Result<void>::success();
    }
};

Result<void> pin_instance(const RuntimePins& runtime, const std::string& instance,
                          DirectoryPin& pin) noexcept
{
    if (!valid_instance(instance)) return Result<void>::failure(Error::INVALID_ARGUMENT);
    const std::wstring wide(instance.begin(), instance.end());
    const auto opened = pin.open(runtime.product_path + L'\\' + wide, true);
    if (!opened) return opened;
    return verified_security(pin.get(), true) ? Result<void>::success() :
                                              Result<void>::failure(Error::INVALID_ARGUMENT);
}

Result<std::unique_ptr<PosixControlClient>> ready_client(
    const std::string& runtime, const std::string& instance,
    Clock::time_point deadline) noexcept
{
    const EndpointConfig endpoint{runtime.c_str(), instance.c_str(),
                                   kControlEndpointName, EndpointAccess::private_user};
    // PosixControlClient::connect has one timeout for each of socket connect
    // and HELLO. Divide the remaining budget so their sum stays bounded.
    const auto remaining = remaining_timeout(deadline).milliseconds;
    if (remaining < 2U)
        return Result<std::unique_ptr<PosixControlClient>>::failure(Error::TIMEOUT);
    auto client = PosixControlClient::connect(endpoint, kRequiredCapabilities,
                                             Timeout{remaining / 2U});
    if (!client) return client;
    // Old daemons can retain card handles across removal/reinsertion. Require
    // the opt-in atomic generation binding; status polling cannot close that
    // race and reconnecting/replaying would be unsafe for an APDU consumer.
    if ((client.value()->negotiated_capabilities() & kRequiredCapabilities) !=
        kRequiredCapabilities)
        return Result<std::unique_ptr<PosixControlClient>>::failure(Error::UNSUPPORTED);
    const Timeout timeout = remaining_timeout(deadline);
    if (timeout.milliseconds == 0U)
        return Result<std::unique_ptr<PosixControlClient>>::failure(Error::TIMEOUT);
    const auto response = client.value()->request(MessageType::LIST, ByteView{nullptr, 0U},
                                                   timeout);
    if (!response)
        return Result<std::unique_ptr<PosixControlClient>>::failure(response.error());
    const auto list = decode_list_response_payload(
        ByteView{response.value().payload.data(), response.value().payload.size()});
    // The typed LIST decoder also verifies exactly one card reader and every
    // record/count/mask. Directory existence alone never exposes a reader.
    if (!list)
        return Result<std::unique_ptr<PosixControlClient>>::failure(list.error());
    if (list.value().ready == 0U)
        return Result<std::unique_ptr<PosixControlClient>>::failure(Error::NOT_READY);
    return client;
}

template <typename Payload>
Result<ControlResponse> request(PosixControlClient& client, MessageType type,
                                const Payload& payload, Timeout timeout) noexcept
{
    std::array<std::uint8_t, kMaxCardPayload + 12U> encoded{};
    const auto size = encode_payload(payload, MutableByteView{encoded.data(), encoded.size()});
    if (!size) return Result<ControlResponse>::failure(size.error());
    return client.request(type, ByteView{encoded.data(), size.value()}, timeout);
}

Result<ReaderStatus> read_status(PosixControlClient& client, Timeout timeout,
                                 std::uint64_t handle = 0U) noexcept
{
    if (timeout.milliseconds == 0U) return Result<ReaderStatus>::failure(Error::TIMEOUT);
    const auto response = handle == 0U ?
        client.request(MessageType::CARD_STATUS, ByteView{nullptr, 0U}, timeout) :
        request(client, MessageType::CARD_HANDLE_STATUS, CardHandleRequestPayload{handle}, timeout);
    if (!response) return Result<ReaderStatus>::failure(response.error());
    const auto decoded = decode_card_status_response_payload(
        ByteView{response.value().payload.data(), response.value().payload.size()});
    if (!decoded) return Result<ReaderStatus>::failure(decoded.error());
    ReaderStatus result;
    result.present = decoded.value().present != 0U;
    result.initialized = decoded.value().initialized != 0U;
    result.generation = decoded.value().reader_generation;
    const ByteView atr = decoded.value().atr;
    if (atr.size != 0U) result.atr.assign(atr.data, atr.data + atr.size);
    return Result<ReaderStatus>::success(std::move(result));
}

bool terminal_error(Error error) noexcept
{
    switch (error) {
    case Error::BUSY:
    case Error::UNSUPPORTED:
    case Error::INVALID_ARGUMENT:
    case Error::BUFFER_TOO_SMALL:
        return false;
    default:
        return true;
    }
}

}  // namespace

Result<std::vector<Reader>> list_readers(const std::string& runtime_root) noexcept
{
    const auto deadline = Clock::now() + std::chrono::milliseconds(kDiscoveryTimeoutMs);
    RuntimePins runtime;
    const auto opened = runtime.open(runtime_root);
    if (!opened) {
        if (opened.error() == Error::NOT_FOUND)
            return Result<std::vector<Reader>>::success({});
        return Result<std::vector<Reader>>::failure(opened.error());
    }
    std::vector<std::string> instances;
    const std::wstring pattern = runtime.product_path + L"\\*";
    WIN32_FIND_DATAW data{};
    const HANDLE search = ::FindFirstFileW(pattern.c_str(), &data);
    if (search == INVALID_HANDLE_VALUE) {
        const DWORD error = ::GetLastError();
        return error == ERROR_FILE_NOT_FOUND ? Result<std::vector<Reader>>::success({}) :
            Result<std::vector<Reader>>::failure(path_error(error));
    }
    Error scan_error = Error::OK;
    std::size_t entries = 0U;
    do {
        if (remaining_timeout(deadline).milliseconds == 0U) {
            scan_error = Error::TIMEOUT;
            break;
        }
        const std::wstring_view name(data.cFileName);
        if (name == L"." || name == L"..") continue;
        if (++entries > kMaxDiscoveryEntries) {
            scan_error = Error::BUFFER_TOO_SMALL;
            break;
        }
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0U ||
            (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U ||
            name.empty() || name.size() > 80U) continue;
        std::string instance;
        for (const wchar_t c : name) {
            if (c > 0x7f) { instance.clear(); break; }
            instance.push_back(static_cast<char>(c));
        }
        if (valid_instance(instance)) instances.push_back(std::move(instance));
    } while (::FindNextFileW(search, &data) != 0);
    const DWORD find_error = ::GetLastError();
    ::FindClose(search);
    if (scan_error != Error::OK)
        return Result<std::vector<Reader>>::failure(scan_error);
    if (find_error != ERROR_NO_MORE_FILES)
        return Result<std::vector<Reader>>::failure(path_error(find_error));
    std::sort(instances.begin(), instances.end());
    std::vector<Reader> readers;
    for (const auto& instance : instances) {
        if (remaining_timeout(deadline).milliseconds == 0U)
            return Result<std::vector<Reader>>::failure(Error::TIMEOUT);
        DirectoryPin pin;
        if (!pin_instance(runtime, instance, pin)) continue;
        const auto endpoint_deadline = std::min(
            deadline, Clock::now() + std::chrono::milliseconds(kEndpointTimeoutMs));
        const auto client = ready_client(runtime_root, instance, endpoint_deadline);
        if (client) readers.push_back(Reader{instance, "PX4 userland (" + instance + ")"});
    }
    if (remaining_timeout(deadline).milliseconds == 0U)
        return Result<std::vector<Reader>>::failure(Error::TIMEOUT);
    return Result<std::vector<Reader>>::success(std::move(readers));
}

Result<ReaderStatus> reader_status(const std::string& runtime_root,
                                  const std::string& instance) noexcept
{
    if (!valid_instance(instance)) return Result<ReaderStatus>::failure(Error::INVALID_ARGUMENT);
    const auto deadline = Clock::now() + std::chrono::milliseconds(kRequestTimeoutMs);
    RuntimePins runtime;
    const auto opened = runtime.open(runtime_root);
    if (!opened) return Result<ReaderStatus>::failure(opened.error());
    DirectoryPin pin;
    const auto pinned = pin_instance(runtime, instance, pin);
    if (!pinned) return Result<ReaderStatus>::failure(pinned.error());
    auto client = ready_client(runtime_root, instance, deadline);
    if (!client) return Result<ReaderStatus>::failure(client.error());
    return read_status(*client.value(), remaining_timeout(deadline));
}

Result<std::unique_ptr<CardClient>> CardClient::connect(
    const std::string& runtime_root, const std::string& instance,
    ShareMode mode) noexcept
{
    if (!valid_instance(instance) ||
        (mode != ShareMode::shared && mode != ShareMode::exclusive))
        return Result<std::unique_ptr<CardClient>>::failure(Error::INVALID_ARGUMENT);
    const auto deadline = Clock::now() + std::chrono::milliseconds(kRequestTimeoutMs);
    RuntimePins runtime;
    const auto opened = runtime.open(runtime_root);
    if (!opened) return Result<std::unique_ptr<CardClient>>::failure(opened.error());
    DirectoryPin pin;
    const auto pinned = pin_instance(runtime, instance, pin);
    if (!pinned) return Result<std::unique_ptr<CardClient>>::failure(pinned.error());
    auto client = ready_client(runtime_root, instance, deadline);
    if (!client) return Result<std::unique_ptr<CardClient>>::failure(client.error());
    const Timeout timeout = remaining_timeout(deadline);
    if (timeout.milliseconds == 0U)
        return Result<std::unique_ptr<CardClient>>::failure(Error::TIMEOUT);
    const auto connected = request(*client.value(), MessageType::CARD_CONNECT,
                                   CardConnectRequestPayload{mode}, timeout);
    if (!connected)
        return Result<std::unique_ptr<CardClient>>::failure(connected.error());
    const auto decoded = decode_card_connect_response_payload(
        ByteView{connected.value().payload.data(), connected.value().payload.size()});
    if (!decoded || decoded.value().card_handle == 0U || decoded.value().atr.size == 0U)
        return Result<std::unique_ptr<CardClient>>::failure(Error::PROTOCOL_ERROR);
    std::vector<std::uint8_t> atr(decoded.value().atr.data,
                                 decoded.value().atr.data + decoded.value().atr.size);
    return Result<std::unique_ptr<CardClient>>::success(std::unique_ptr<CardClient>(
        new CardClient(std::move(client.value()), decoded.value().card_handle,
                       std::move(atr))));
}

CardClient::~CardClient() noexcept
{
    // Closing the owning connection releases handles/transactions in px4d.
    // Destruction never resets the card or waits for an extra request.
    if (client_) client_->close();
}

Result<std::vector<std::uint8_t>> CardClient::transmit(ByteView apdu) noexcept
{
    if (apdu.data == nullptr || apdu.size == 0U || apdu.size > kMaxCardPayload)
        return Result<std::vector<std::uint8_t>>::failure(Error::INVALID_ARGUMENT);
    if (!client_ || handle_ == 0U)
        return Result<std::vector<std::uint8_t>>::failure(Error::NOT_FOUND);
    const auto response = request(*client_, MessageType::CARD_TRANSMIT,
                                  CardTransmitRequestPayload{handle_, apdu},
                                  Timeout{kRequestTimeoutMs});
    if (!response) {
        if (terminal_error(response.error())) { client_->close(); handle_ = 0U; }
        return Result<std::vector<std::uint8_t>>::failure(response.error());
    }
    const auto decoded = decode_card_transmit_response_payload(
        ByteView{response.value().payload.data(), response.value().payload.size()});
    if (!decoded) {
        client_->close();
        handle_ = 0U;
        return Result<std::vector<std::uint8_t>>::failure(decoded.error());
    }
    std::vector<std::uint8_t> result;
    const ByteView bytes = decoded.value().response;
    if (bytes.size != 0U) result.assign(bytes.data, bytes.data + bytes.size);
    return Result<std::vector<std::uint8_t>>::success(std::move(result));
}

Result<ReaderStatus> CardClient::status() noexcept
{
    if (!client_ || handle_ == 0U) return Result<ReaderStatus>::failure(Error::NOT_FOUND);
    // The daemon checks this owning handle's sticky stale state and obtains
    // the snapshot in one serialized card-lane operation. Reader-global epoch
    // sampling alone cannot prove handle validity after a protocol card loss.
    auto result = read_status(*client_, Timeout{kRequestTimeoutMs}, handle_);
    if (!result) {
        if (terminal_error(result.error())) { client_->close(); handle_ = 0U; }
        return result;
    }
    if (!result.value().present || !result.value().initialized) {
        client_->close();
        handle_ = 0U;
        return Result<ReaderStatus>::failure(result.value().present ? Error::NOT_READY :
                                                                     Error::CARD_REMOVED);
    }
    // Do not borrow a replacement card's ATR. Preserve the ATR that belongs
    // to this successful, atomically validated CARD_CONNECT handle.
    result.value().atr = atr_;
    return result;
}

Result<void> CardClient::disconnect(Disposition disposition) noexcept
{
    if (disposition != Disposition::leave && disposition != Disposition::reset)
        return Result<void>::failure(Error::INVALID_ARGUMENT);
    if (!client_ || handle_ == 0U) return Result<void>::failure(Error::NOT_FOUND);
    const auto response = request(*client_, MessageType::CARD_DISCONNECT,
                                  CardDispositionRequestPayload{handle_, disposition},
                                  Timeout{kRequestTimeoutMs});
    client_->close();
    handle_ = 0U;
    return response ? Result<void>::success() : Result<void>::failure(response.error());
}

}  // namespace px4::userland::windows_card

#endif  // _WIN32
