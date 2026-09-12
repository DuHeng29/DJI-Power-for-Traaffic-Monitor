// 本文件实现 INI 配置读写；pair_key 只以当前用户可解密的 DPAPI 密文落盘。
#include "common/config.hpp"
#include <windows.h>
#include <wincrypt.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <vector>

extern HMODULE g_module;

namespace dji_power {
namespace {
std::wstring ConfigPath() { return ModuleDirectory() + L"\\config.ini"; }
std::wstring ReadIni(const wchar_t* key, const wchar_t* fallback = L"") {
    std::array<wchar_t, 2048> buffer{};
    GetPrivateProfileStringW(L"connection", key, fallback, buffer.data(), static_cast<DWORD>(buffer.size()), ConfigPath().c_str());
    return buffer.data();
}
std::wstring Protect(const std::string& plain) {
    if (plain.empty()) return {};
    DATA_BLOB input{static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()))};
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"DJI Power pair key", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) return {};
    DWORD chars = 0;
    CryptBinaryToStringW(output.pbData, output.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &chars);
    std::wstring encoded(chars, L'\0');
    CryptBinaryToStringW(output.pbData, output.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, encoded.data(), &chars);
    LocalFree(output.pbData);
    if (!encoded.empty() && encoded.back() == L'\0') encoded.pop_back();
    return encoded;
}
std::string Unprotect(const std::wstring& encoded) {
    if (encoded.empty()) return {};
    DWORD bytes = 0;
    if (!CryptStringToBinaryW(encoded.c_str(), 0, CRYPT_STRING_BASE64, nullptr, &bytes, nullptr, nullptr)) return {};
    std::vector<BYTE> encrypted(bytes);
    if (!CryptStringToBinaryW(encoded.c_str(), 0, CRYPT_STRING_BASE64, encrypted.data(), &bytes, nullptr, nullptr)) return {};
    DATA_BLOB input{bytes, encrypted.data()}, output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) return {};
    std::string plain(reinterpret_cast<char*>(output.pbData), output.cbData);
    LocalFree(output.pbData);
    return plain;
}
} // namespace

std::wstring ModuleDirectory() {
    std::array<wchar_t, 32768> path{};
    const auto length = GetModuleFileNameW(g_module, path.data(), static_cast<DWORD>(path.size()));
    return std::filesystem::path(std::wstring(path.data(), length)).parent_path().wstring();
}
bool IsValidPairKey(const std::string& value) {
    return value.size() == 32 && std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
}
PluginConfig LoadConfig() {
    PluginConfig result;
    result.device_name = ReadIni(L"device_name");
    result.auto_connect = ReadIni(L"auto_connect", L"1") != L"0";
    result.auto_reconnect = ReadIni(L"auto_reconnect", L"1") != L"0";
    try { result.bluetooth_address = std::stoull(ReadIni(L"bluetooth_address", L"0"), nullptr, 16); } catch (...) { result.bluetooth_address = 0; }
    result.pair_key = Unprotect(ReadIni(L"pair_key_dpapi"));
    if (!IsValidPairKey(result.pair_key)) result.pair_key.clear();
    return result;
}
bool SaveConfig(const PluginConfig& config) {
    const auto path = ConfigPath();
    wchar_t address[17]{};
    swprintf_s(address, L"%012llX", static_cast<unsigned long long>(config.bluetooth_address));
    const auto secret = Protect(config.pair_key);
    return WritePrivateProfileStringW(L"connection", L"device_name", config.device_name.c_str(), path.c_str()) &&
           WritePrivateProfileStringW(L"connection", L"bluetooth_address", address, path.c_str()) &&
           WritePrivateProfileStringW(L"connection", L"pair_key_dpapi", secret.c_str(), path.c_str()) &&
           WritePrivateProfileStringW(L"connection", L"auto_connect", config.auto_connect ? L"1" : L"0", path.c_str()) &&
           WritePrivateProfileStringW(L"connection", L"auto_reconnect", config.auto_reconnect ? L"1" : L"0", path.c_str());
}
} // namespace dji_power

