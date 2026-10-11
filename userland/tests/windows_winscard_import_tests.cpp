// SPDX-License-Identifier: GPL-2.0-only
// Linked through the toolchain's ordinary winscard import library. Never opens
// a card or sends an APDU; checks named code imports and the real PCI DATA ABI.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winscard.h>
#include <tlhelp32.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>

namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); ++failures; } } while(false)
template <typename T> HMODULE module_of(T address)
{
    LPCWSTR pointer = nullptr;
    static_assert(sizeof(pointer) == sizeof(address));
    std::memcpy(&pointer,&address,sizeof(pointer));
    HMODULE module = nullptr;
    CHECK(::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              pointer,&module) != 0);
    return module;
}
bool same_file(const wchar_t* a, const wchar_t* b)
{
    const HANDLE left = ::CreateFileW(a,FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                                      nullptr,OPEN_EXISTING,0,nullptr);
    const HANDLE right = ::CreateFileW(b,FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                                       nullptr,OPEN_EXISTING,0,nullptr);
    BY_HANDLE_FILE_INFORMATION x{}, y{};
    const bool result = left != INVALID_HANDLE_VALUE && right != INVALID_HANDLE_VALUE &&
        ::GetFileInformationByHandle(left,&x) && ::GetFileInformationByHandle(right,&y) &&
        x.dwVolumeSerialNumber == y.dwVolumeSerialNumber && x.nFileIndexHigh == y.nFileIndexHigh &&
        x.nFileIndexLow == y.nFileIndexLow;
    if (left != INVALID_HANDLE_VALUE) ::CloseHandle(left);
    if (right != INVALID_HANDLE_VALUE) ::CloseHandle(right);
    return result;
}
}
int main()
{
    const HMODULE proxy = module_of(&SCardEstablishContext);
    CHECK(module_of(&SCardReleaseContext) == proxy);
    CHECK(module_of(&SCardListReadersA) == proxy && module_of(&SCardListReadersW) == proxy);
    CHECK(module_of(&SCardConnectA) == proxy && module_of(&SCardConnectW) == proxy);
    CHECK(module_of(&SCardDisconnect) == proxy && module_of(&SCardTransmit) == proxy);
    CHECK(module_of(&SCardIsValidContext) == proxy && module_of(&SCardFreeMemory) == proxy);
    CHECK(module_of(&SCardStatusW) == proxy && module_of(&SCardGetStatusChangeW) == proxy);
    CHECK(module_of(&g_rgSCardT1Pci) == proxy);
    CHECK(sizeof(g_rgSCardT1Pci) == 8U && SCARD_PCI_T1->dwProtocol == 2U && SCARD_PCI_T1->cbPciLength == 8U);
    std::array<wchar_t,32768U> executable{}, actual{}, system{};
    CHECK(::GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size())) != 0);
    CHECK(::GetModuleFileNameW(proxy,actual.data(),static_cast<DWORD>(actual.size())) != 0);
    std::wstring expected(executable.data());
    const auto slash = expected.find_last_of(L"\\/");
    CHECK(slash != std::wstring::npos);
    if (slash == std::wstring::npos) return 1;
    expected.resize(slash + 1U); expected += L"WinSCard.dll";
    CHECK(same_file(actual.data(),expected.c_str()));
    std::printf("Imported WinSCard path: %ls\n",actual.data());
    // Context creation exercises the actual lazy System32 loader without
    // enumeration, card access, daemon startup or hardware dependencies.
    CHECK(::SetEnvironmentVariableW(L"PX4_WINSCARD_RUNTIME_DIR",L"C:\\px4-import-fixture-absent") != 0);
    SCARDCONTEXT context = 0U;
    CHECK(SCardEstablishContext(SCARD_SCOPE_USER,nullptr,nullptr,&context) == SCARD_S_SUCCESS);
    CHECK(context != 0U);
    CHECK(SCardIsValidContext(context) == SCARD_S_SUCCESS);
    CHECK(SCardReleaseContext(context) == SCARD_S_SUCCESS);
    CHECK(SCardIsValidContext(context) == ERROR_INVALID_HANDLE);
    const UINT size = ::GetSystemDirectoryW(system.data(),static_cast<UINT>(system.size()));
    CHECK(size > 0U && size < system.size());
    const std::wstring native_path = std::wstring(system.data()) + L"\\WinSCard.dll";
    const HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,0U);
    CHECK(snapshot != INVALID_HANDLE_VALUE);
    bool found_system = false;
    if (snapshot != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W entry{}; entry.dwSize = sizeof(entry);
        if (::Module32FirstW(snapshot,&entry)) do {
            if (entry.hModule != proxy && same_file(entry.szExePath,native_path.c_str())) {
                std::printf("Forwarding WinSCard path: %ls\n",entry.szExePath);
                found_system = true;
            }
        } while (::Module32NextW(snapshot,&entry));
        ::CloseHandle(snapshot);
    }
    CHECK(found_system);
    std::printf("WinSCard SDK import tests: %d failures\n",failures);
    return failures == 0 ? 0 : 1;
}
