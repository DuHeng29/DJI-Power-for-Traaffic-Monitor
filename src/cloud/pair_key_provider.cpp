// 本文件通过 WinHTTP 使用一次性 DJI member token 查询设备的本地 BLE pair_key。
#include "cloud/pair_key_provider.hpp"
#include "common/config.hpp"
#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <array>
#include <string_view>

namespace dji_power {
namespace {
std::string Utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}
std::wstring Wide(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}
std::string JsonStringBefore(std::string_view json, std::size_t before, std::string_view key) {
    const auto marker = std::string("\"") + std::string(key) + "\"";
    const auto pos = json.rfind(marker, before);
    if (pos == std::string_view::npos || before - pos > 1200) return {};
    auto colon = json.find(':', pos + marker.size());
    auto quote = json.find('"', colon + 1);
    if (colon == std::string_view::npos || quote == std::string_view::npos) return {};
    const auto end = json.find('"', quote + 1);
    return end == std::string_view::npos ? std::string{} : std::string(json.substr(quote + 1, end - quote - 1));
}
std::string Download(std::wstring_view host, const std::wstring& token, std::wstring& error) {
    HINTERNET session = WinHttpOpen(L"DJIPowerTrafficMonitor/0.2", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0);
    if (!session) { error = L"无法初始化 WinHTTP"; return {}; }
    WinHttpSetTimeouts(session, 10000, 10000, 10000, 15000);
    HINTERNET connection = WinHttpConnect(session, std::wstring(host).c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET", L"/app/api/v1/users/devices/list", nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    const std::wstring headers = L"x-member-token: " + token + L"\r\nAccept: application/json\r\nUser-Agent: DJIHome/1.5.16 (Android)\r\n";
    bool ok = request && WinHttpSendRequest(request, headers.c_str(), static_cast<DWORD>(-1L), nullptr, 0, 0, 0) && WinHttpReceiveResponse(request, nullptr);
    DWORD status = 0, status_size = sizeof(status);
    if (ok) ok = WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &status_size, nullptr) && status == 200;
    std::string body;
    while (ok) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available) || available == 0) break;
        const auto old = body.size();
        body.resize(old + available);
        DWORD read = 0;
        if (!WinHttpReadData(request, body.data() + old, available, &read)) { ok = false; break; }
        body.resize(old + read);
        if (body.size() > 4U * 1024U * 1024U) { ok = false; break; }
    }
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    if (!ok) { error = status == 401 ? L"Member Token 已失效" : L"DJI Home API 请求失败"; return {}; }
    return body;
}

class WinHttpPairKeyProvider final : public PairKeyProvider {
public:
    std::vector<CloudDevice> FetchWithMemberToken(const std::wstring& token, std::wstring& error) override {
        if (!token.starts_with(L"US_")) { error = L"Member Token 应以 US_ 开头"; return {}; }
        static constexpr std::array hosts{L"home-api.djigate.com", L"home-api-vg.djigate.com", L"home-api-hz.djigate.com"};
        std::string json;
        for (const auto* host : hosts) { json = Download(host, token, error); if (!json.empty()) break; }
        if (json.empty()) return {};
        std::vector<CloudDevice> result;
        constexpr std::string_view marker = "\"pair_key\"";
        std::size_t cursor = 0;
        while ((cursor = json.find(marker, cursor)) != std::string::npos) {
            const auto colon = json.find(':', cursor + marker.size());
            const auto quote = json.find('"', colon + 1);
            const auto end = json.find('"', quote + 1);
            if (colon == std::string::npos || quote == std::string::npos || end == std::string::npos) break;
            const auto key = json.substr(quote + 1, end - quote - 1);
            if (IsValidPairKey(key)) {
                auto name = JsonStringBefore(json, cursor, "name");
                auto sn = JsonStringBefore(json, cursor, "sn");
                result.push_back({name.empty() ? L"DJI Power" : Wide(name), Wide(sn), key});
            }
            cursor = end + 1;
        }
        if (result.empty()) error = L"账号设备列表中没有可用的 pair_key";
        return result;
    }
};
} // namespace

PairKeyProvider& DefaultPairKeyProvider() { static WinHttpPairKeyProvider provider; return provider; }
} // namespace dji_power

