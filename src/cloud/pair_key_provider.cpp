// 本文件通过 WinHTTP 完成 DJI 官方网页短信登录，并用临时 member token 查询设备 pair_key。
#include "cloud/pair_key_provider.hpp"

#include <windows.h>
#include <objbase.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <map>
#include <string_view>

namespace dji_power {
namespace {

bool IsPairKey(std::string_view value) {
    return value.size() == 32 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isxdigit(c) != 0;
    });
}

constexpr std::wstring_view kAccountHost = L"account.dji.com";
constexpr std::wstring_view kInitialSessionRandom = L"1qazXSW@#EDC4rfv!$&";
constexpr std::size_t kMaximumResponseSize = 4U * 1024U * 1024U;

struct InternetHandle {
    HINTERNET value{};
    ~InternetHandle() { if (value) WinHttpCloseHandle(value); }
    InternetHandle() = default;
    explicit InternetHandle(HINTERNET handle) : value(handle) {}
    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;
};

struct HttpResponse {
    DWORD status{};
    std::vector<std::uint8_t> body;
    std::wstring raw_headers;
};

std::string Utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                         nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring Wide(std::string_view value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::string UrlEncode(std::string_view value) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(value.size() * 3);
    for (const unsigned char byte : value) {
        if (std::isalnum(byte) || byte == '-' || byte == '_' || byte == '.' || byte == '~') {
            result.push_back(static_cast<char>(byte));
        } else {
            result.push_back('%');
            result.push_back(hex[byte >> 4]);
            result.push_back(hex[byte & 0x0F]);
        }
    }
    return result;
}

void AppendForm(std::string& form, std::string_view key, std::string_view value) {
    if (!form.empty()) form.push_back('&');
    form += UrlEncode(key);
    form.push_back('=');
    form += UrlEncode(value);
}

std::string CommonForm() {
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::string form;
    AppendForm(form, "locale", "zh_CN");
    AppendForm(form, "mode", "redirect");
    AppendForm(form, "appId", "store");
    AppendForm(form, "guestLoginButtonVisible", "false");
    AppendForm(form, "timestamp", std::to_string(milliseconds));
    AppendForm(form, "backUrl", "backUrl");
    AppendForm(form, "pnonce", "");
    AppendForm(form, "psign", "");
    AppendForm(form, "authTicket", "");
    return form;
}

std::string JsonString(std::string_view json, std::string_view key, std::size_t from = 0) {
    const std::string marker = "\"" + std::string(key) + "\"";
    const auto key_pos = json.find(marker, from);
    if (key_pos == std::string_view::npos) return {};
    const auto colon = json.find(':', key_pos + marker.size());
    const auto quote = colon == std::string_view::npos ? std::string_view::npos : json.find('"', colon + 1);
    if (quote == std::string_view::npos) return {};
    std::string result;
    for (std::size_t i = quote + 1; i < json.size(); ++i) {
        const char c = json[i];
        if (c == '"') return result;
        if (c == '\\' && i + 1 < json.size()) {
            const char escaped = json[++i];
            if (escaped == 'n') result.push_back('\n');
            else if (escaped == 'r') result.push_back('\r');
            else if (escaped == 't') result.push_back('\t');
            else result.push_back(escaped);
        } else {
            result.push_back(c);
        }
    }
    return {};
}

int JsonCode(std::string_view json) {
    const auto pos = json.find("\"code\"");
    if (pos == std::string_view::npos) return -1;
    const auto colon = json.find(':', pos + 6);
    if (colon == std::string_view::npos) return -1;
    std::size_t i = colon + 1;
    while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) ++i;
    int value = 0;
    bool any = false;
    while (i < json.size() && std::isdigit(static_cast<unsigned char>(json[i]))) {
        any = true;
        value = value * 10 + json[i++] - '0';
    }
    return any ? value : -1;
}

std::string Text(const HttpResponse& response) {
    return response.body.empty() ? std::string{} :
        std::string(reinterpret_cast<const char*>(response.body.data()), response.body.size());
}

std::map<std::wstring, std::wstring> ParseCookies(std::wstring_view cookie_header) {
    std::map<std::wstring, std::wstring> result;
    std::size_t cursor = 0;
    while (cursor < cookie_header.size()) {
        const auto end = cookie_header.find(L';', cursor);
        auto item = cookie_header.substr(cursor, end == std::wstring_view::npos ? cookie_header.size() - cursor : end - cursor);
        while (!item.empty() && item.front() == L' ') item.remove_prefix(1);
        const auto equals = item.find(L'=');
        if (equals != std::wstring_view::npos) result[std::wstring(item.substr(0, equals))] = std::wstring(item.substr(equals + 1));
        if (end == std::wstring_view::npos) break;
        cursor = end + 1;
    }
    return result;
}

void MergeCookies(std::wstring& cookie_header, std::wstring_view raw_headers) {
    auto cookies = ParseCookies(cookie_header);
    std::size_t cursor = 0;
    while (cursor < raw_headers.size()) {
        const auto end = raw_headers.find(L"\r\n", cursor);
        auto line = raw_headers.substr(cursor, end == std::wstring_view::npos ? raw_headers.size() - cursor : end - cursor);
        constexpr std::wstring_view prefix = L"Set-Cookie:";
        if (line.size() > prefix.size() && _wcsnicmp(line.data(), prefix.data(), prefix.size()) == 0) {
            line.remove_prefix(prefix.size());
            while (!line.empty() && line.front() == L' ') line.remove_prefix(1);
            const auto semi = line.find(L';');
            const auto pair = line.substr(0, semi);
            const auto equals = pair.find(L'=');
            if (equals != std::wstring_view::npos) cookies[std::wstring(pair.substr(0, equals))] = std::wstring(pair.substr(equals + 1));
        }
        if (end == std::wstring_view::npos) break;
        cursor = end + 2;
    }
    cookie_header.clear();
    for (const auto& [name, value] : cookies) {
        if (!cookie_header.empty()) cookie_header += L"; ";
        cookie_header += name + L"=" + value;
    }
}

bool Request(std::wstring_view host, std::wstring_view method, std::wstring_view path,
             const std::wstring& extra_headers, std::string_view body,
             std::wstring& cookies, HttpResponse& response, std::wstring& error,
             std::wstring_view accept = L"application/json", bool follow_redirects = true) {
    response = {};
    InternetHandle session(WinHttpOpen(L"DJIPowerTrafficMonitor/0.3", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                       nullptr, nullptr, 0));
    if (!session.value) { error = L"无法初始化 WinHTTP"; return false; }
    WinHttpSetTimeouts(session.value, 10000, 10000, 10000, 15000);
    InternetHandle connection(WinHttpConnect(session.value, std::wstring(host).c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0));
    InternetHandle request(connection.value ? WinHttpOpenRequest(connection.value, std::wstring(method).c_str(),
        std::wstring(path).c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr);
    if (!request.value) { error = L"无法创建 HTTPS 请求"; return false; }
    if (!follow_redirects) {
        DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
    }

    std::wstring headers = L"Accept: " + std::wstring(accept) +
        L"\r\nUser-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) DJI-Power-Plugin\r\n";
    if (!cookies.empty()) headers += L"Cookie: " + cookies + L"\r\n";
    headers += extra_headers;
    const auto body_size = static_cast<DWORD>(body.size());
    void* body_pointer = body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data());
    bool ok = WinHttpSendRequest(request.value, headers.c_str(), static_cast<DWORD>(-1L),
                                 body_pointer, body_size, body_size, 0) &&
              WinHttpReceiveResponse(request.value, nullptr);
    DWORD status_size = sizeof(response.status);
    if (ok) ok = WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                     nullptr, &response.status, &status_size, nullptr) != FALSE;

    DWORD header_size = 0;
    WinHttpQueryHeaders(request.value, WINHTTP_QUERY_RAW_HEADERS_CRLF, nullptr, nullptr, &header_size, nullptr);
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && header_size >= sizeof(wchar_t)) {
        response.raw_headers.resize(header_size / sizeof(wchar_t));
        if (WinHttpQueryHeaders(request.value, WINHTTP_QUERY_RAW_HEADERS_CRLF, nullptr,
                                response.raw_headers.data(), &header_size, nullptr)) {
            if (!response.raw_headers.empty() && response.raw_headers.back() == L'\0') response.raw_headers.pop_back();
            MergeCookies(cookies, response.raw_headers);
        }
    }

    while (ok) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.value, &available) || available == 0) break;
        if (response.body.size() + available > kMaximumResponseSize) { ok = false; break; }
        const auto old_size = response.body.size();
        response.body.resize(old_size + available);
        DWORD bytes_read = 0;
        if (!WinHttpReadData(request.value, response.body.data() + old_size, available, &bytes_read)) { ok = false; break; }
        response.body.resize(old_size + bytes_read);
    }
    const bool accepted_redirect = !follow_redirects && response.status >= 300 && response.status < 400;
    if (!ok || (!accepted_redirect && (response.status < 200 || response.status >= 300))) {
        error = response.status == 429 ? L"DJI 请求过于频繁，请稍后重试" : L"DJI 账号服务请求失败";
        return false;
    }
    return true;
}

std::wstring AccountHeaders(const SmsLoginSession& login, bool form) {
    std::wstring headers;
    if (form) headers += L"Content-Type: application/x-www-form-urlencoded\r\n";
    headers += L"X-Html-Version: " + login.html_version + L"\r\n";
    headers += L"X-Session-Random: " + (login.csrf_token.empty() ? std::wstring(kInitialSessionRandom) : login.csrf_token) + L"\r\n";
    return headers;
}

std::string RandomHex() {
    GUID guid{};
    if (CoCreateGuid(&guid) != S_OK) return std::to_string(GetTickCount64());
    const auto* bytes = reinterpret_cast<const unsigned char*>(&guid);
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(sizeof(guid) * 2);
    for (std::size_t i = 0; i < sizeof(guid); ++i) {
        result.push_back(hex[bytes[i] >> 4]);
        result.push_back(hex[bytes[i] & 0x0F]);
    }
    return result;
}

bool CrackHttpsUrl(std::wstring_view url, std::wstring& host, std::wstring& path) {
    if (url.starts_with(L"/")) {
        host = std::wstring(kAccountHost);
        path = url;
        return true;
    }
    std::wstring owned(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = static_cast<DWORD>(-1);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(owned.data(), static_cast<DWORD>(owned.size()), 0, &parts) ||
        parts.nScheme != INTERNET_SCHEME_HTTPS || !parts.lpszHostName) {
        return false;
    }
    host.assign(parts.lpszHostName, parts.dwHostNameLength);
    path.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.lpszExtraInfo && parts.dwExtraInfoLength > 0) {
        path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    }
    if (path.empty()) path = L"/";
    return true;
}

bool IsTrustedDjiHost(std::wstring_view host) {
    const auto matches = [host](std::wstring_view root) {
        return _wcsicmp(std::wstring(host).c_str(), std::wstring(root).c_str()) == 0 ||
            (host.size() > root.size() && host[host.size() - root.size() - 1] == L'.' &&
             _wcsicmp(std::wstring(host.substr(host.size() - root.size())).c_str(),
                       std::wstring(root).c_str()) == 0);
    };
    return matches(L"dji.com") || matches(L"djigate.com");
}

std::wstring HeaderValue(std::wstring_view raw_headers, std::wstring_view name) {
    const auto prefix = std::wstring(name) + L":";
    std::size_t cursor = 0;
    while (cursor < raw_headers.size()) {
        const auto end = raw_headers.find(L"\r\n", cursor);
        auto line = raw_headers.substr(cursor, end == std::wstring_view::npos
            ? raw_headers.size() - cursor : end - cursor);
        if (line.size() >= prefix.size() &&
            _wcsnicmp(line.data(), prefix.data(), prefix.size()) == 0) {
            line.remove_prefix(prefix.size());
            while (!line.empty() && (line.front() == L' ' || line.front() == L'\t')) line.remove_prefix(1);
            return std::wstring(line);
        }
        if (end == std::wstring_view::npos) break;
        cursor = end + 2;
    }
    return {};
}

std::wstring ResolveRedirect(std::wstring_view host, std::wstring_view path,
                             std::wstring_view location) {
    if (location.starts_with(L"https://")) return std::wstring(location);
    if (location.starts_with(L"//")) return L"https:" + std::wstring(location);
    const auto origin = L"https://" + std::wstring(host);
    if (location.starts_with(L"/")) return origin + std::wstring(location);
    auto base = std::wstring(path);
    const auto query = base.find_first_of(L"?#");
    if (query != std::wstring::npos) base.erase(query);
    const auto slash = base.find_last_of(L'/');
    base.erase(slash == std::wstring::npos ? 0 : slash + 1);
    return origin + base + std::wstring(location);
}

bool ApiSucceeded(const HttpResponse& response, std::wstring& error, std::wstring_view fallback) {
    const auto json = Text(response);
    if (JsonCode(json) == 0) return true;
    error = cloud_detail::ExtractApiError(json, fallback);
    return false;
}

std::string JsonStringBefore(std::string_view json, std::size_t before, std::string_view key) {
    const auto marker = std::string("\"") + std::string(key) + "\"";
    const auto pos = json.rfind(marker, before);
    if (pos == std::string_view::npos || before - pos > 1600) return {};
    return JsonString(json, key, pos);
}

std::string DownloadDevices(std::wstring_view host, const std::wstring& token, std::wstring& error) {
    std::wstring unused_cookies;
    HttpResponse response;
    const auto headers = L"x-member-token: " + token + L"\r\n";
    if (!Request(host, L"GET", L"/app/api/v1/users/devices/list", headers, {},
                 unused_cookies, response, error)) return {};
    return Text(response);
}

class WinHttpPairKeyProvider final : public PairKeyProvider {
public:
    std::vector<CloudDevice> FetchWithMemberToken(const std::wstring& token, std::wstring& error) override {
        if (!token.starts_with(L"US_")) { error = L"Member Token 应以 US_ 开头"; return {}; }
        static constexpr std::array hosts{L"home-api.djigate.com", L"home-api-vg.djigate.com", L"home-api-hz.djigate.com"};
        std::string json;
        for (const auto* host : hosts) {
            json = DownloadDevices(host, token, error);
            if (!json.empty()) {
                auto devices = cloud_detail::ParseDevicesJson(json);
                if (!devices.empty()) return devices;
            }
        }
        error = L"账号设备列表中没有可用的 pair_key";
        return {};
    }

    bool BeginSmsLogin(SmsLoginSession& login, std::vector<std::uint8_t>& captcha_png,
                       std::wstring& error) override {
        login.Clear();
        HttpResponse page;
        if (!Request(kAccountHost, L"GET", L"/login?appId=store&locale=zh_CN&region=CN",
                     {}, {}, login.cookies, page, error,
                     L"text/html,application/xhtml+xml")) return false;
        const auto html = Text(page);
        constexpr std::string_view version_marker = "window.__version=\"";
        const auto version_pos = html.find(version_marker);
        if (version_pos == std::string::npos) { error = L"无法识别 DJI 登录页版本"; return false; }
        const auto version_end = html.find('"', version_pos + version_marker.size());
        if (version_end == std::string::npos) { error = L"DJI 登录页版本字段不完整"; return false; }
        login.html_version = Wide(html.substr(version_pos + version_marker.size(),
                                               version_end - version_pos - version_marker.size()));

        auto form = CommonForm();
        AppendForm(form, "lang", "zh_CN");
        AppendForm(form, "region", "CN");
        AppendForm(form, "actualAppId", "store");
        HttpResponse init;
        if (!Request(kAccountHost, L"POST", L"/user/webrest/v1/initData.do",
                     AccountHeaders(login, true), form, login.cookies, init, error) ||
            !ApiSucceeded(init, error, L"初始化 DJI 登录失败")) return false;
        const auto init_json = Text(init);
        login.csrf_token = Wide(JsonString(init_json, "csrfToken"));
        if (login.csrf_token.empty()) login.csrf_token = Wide(JsonString(init_json, "nonce"));
        if (login.csrf_token.empty()) { error = L"DJI 登录服务没有返回会话令牌"; return false; }
        return RefreshCaptcha(login, captcha_png, error);
    }

    bool RefreshCaptcha(SmsLoginSession& login, std::vector<std::uint8_t>& captcha_png,
                        std::wstring& error) override {
        login.captcha_random = RandomHex();
        const auto path = L"/user/webrest/v1/vcode.do?appId=store&srandom=" + Wide(login.captcha_random);
        HttpResponse response;
        if (!Request(kAccountHost, L"GET", path, AccountHeaders(login, false), {},
                     login.cookies, response, error, L"image/avif,image/webp,image/*,*/*")) return false;
        if (response.body.size() < 64) { error = L"DJI 图片验证码加载失败"; return false; }
        captcha_png = std::move(response.body);
        return true;
    }

    bool SendSmsCode(SmsLoginSession& login, const std::wstring& area_code,
                     const std::wstring& phone, const std::wstring& image_code,
                     std::wstring& error) override {
        auto captcha_form = CommonForm();
        AppendForm(captcha_form, "captchaModule", "WebSmsLoginOrRegister");
        AppendForm(captcha_form, "captchaType", "imageCaptcha");
        AppendForm(captcha_form, "verificationCode", Utf8(image_code));
        AppendForm(captcha_form, "srandom", login.captcha_random);
        AppendForm(captcha_form, "cid", RandomHex());
        HttpResponse captcha;
        if (!Request(kAccountHost, L"POST", L"/user/webrest/v1/validCaptcha.do",
                     AccountHeaders(login, true), captcha_form, login.cookies, captcha, error) ||
            !ApiSucceeded(captcha, error, L"图片验证码错误")) return false;
        login.captcha_ticket = JsonString(Text(captcha), "captchaTicket");
        if (login.captcha_ticket.empty()) { error = L"图片验证码校验成功，但未返回票据"; return false; }

        auto sms_form = CommonForm();
        AppendForm(sms_form, "areaCode", Utf8(area_code));
        AppendForm(sms_form, "phone", Utf8(phone));
        AppendForm(sms_form, "smsType", "8");
        AppendForm(sms_form, "captchaTicket", login.captcha_ticket);
        AppendForm(sms_form, "cid", RandomHex());
        HttpResponse sms;
        if (!Request(kAccountHost, L"POST", L"/user/webrest/v1/sendSmsCode.do",
                     AccountHeaders(login, true), sms_form, login.cookies, sms, error)) return false;
        return ApiSucceeded(sms, error, L"短信验证码发送失败");
    }

    bool CompleteSmsLogin(SmsLoginSession& login, const std::wstring& area_code,
                          const std::wstring& phone, const std::wstring& sms_code,
                          std::wstring& error) override {
        auto form = CommonForm();
        AppendForm(form, "phone", Utf8(phone));
        AppendForm(form, "smsCode", Utf8(sms_code));
        AppendForm(form, "areaCode", Utf8(area_code));
        AppendForm(form, "subscription", "false");
        AppendForm(form, "captchaTicket", login.captcha_ticket);
        AppendForm(form, "cid", RandomHex());
        HttpResponse response;
        if (!Request(kAccountHost, L"POST", L"/user/webrest/v1/login_or_register_with_sms_code.do",
                     AccountHeaders(login, true), form, login.cookies, response, error) ||
            !ApiSucceeded(response, error, L"短信验证码登录失败")) return false;

        // 官方网页在登录成功后返回 callbackUrl，而不是 DJI Home Member Token。
        // 保留回调票据到当前窗口关闭，后续 Key 获取将在真实登录验证后单独适配。
        login.callback_url = cloud_detail::ExtractCallbackUrl(Text(response));
        if (login.callback_url.empty()) {
            error = L"短信验证成功，但 DJI 未返回登录回调地址";
            return false;
        }
        return true;
    }
    std::vector<CloudDevice> FetchAfterWebLogin(
        SmsLoginSession& login, std::wstring& error) override {
        if (login.callback_url.empty()) {
            error = L"当前窗口中没有已完成的 DJI 登录会话";
            return {};
        }

        std::string member_token;
        std::wstring current_url = login.callback_url;
        std::wstring final_host;
        std::map<std::wstring, std::wstring> cookie_jars;
        cookie_jars[std::wstring(kAccountHost)] = login.cookies;
        std::size_t followed_redirects = 0;

        // 禁用 WinHTTP 自动跳转，逐跳检查 Location 和 Set-Cookie，避免登录票据在中间响应中丢失。
        for (std::size_t hop = 0; hop < 8; ++hop) {
            std::wstring host;
            std::wstring path;
            if (!CrackHttpsUrl(current_url, host, path)) {
                error = L"DJI 返回的登录回调地址不是有效的 HTTPS 地址";
                return {};
            }
            if (!IsTrustedDjiHost(host)) {
                error = L"为保护登录票据，已拒绝非 DJI 域名的回调地址";
                return {};
            }
            final_host = host;
            auto& callback_cookies = cookie_jars[host];
            HttpResponse callback;
            if (!Request(host, L"GET", path, {}, {}, callback_cookies, callback, error,
                         L"text/html,application/json,*/*", false)) {
                return {};
            }

            const auto location = HeaderValue(callback.raw_headers, L"Location");
            const std::array candidates{
                Utf8(current_url), Text(callback), Utf8(callback.raw_headers), Utf8(callback_cookies)};
            for (const auto& candidate : candidates) {
                member_token = cloud_detail::ExtractMemberToken(candidate);
                if (!member_token.empty()) break;
            }
            if (!member_token.empty()) break;
            if (callback.status < 300 || callback.status >= 400 || location.empty()) break;
            current_url = ResolveRedirect(host, path, location);
            ++followed_redirects;
        }
        if (member_token.empty()) {
            error = L"登录回调已完成，但重定向链没有提供 DJI Home Member Token（跳转 " +
                std::to_wstring(followed_redirects) + L" 次，最终主机：" + final_host + L"）";
            return {};
        }

        auto wide_token = Wide(member_token);
        auto devices = FetchWithMemberToken(wide_token, error);
        SecureZeroMemory(member_token.data(), member_token.size());
        SecureZeroMemory(wide_token.data(), wide_token.size() * sizeof(wchar_t));
        if (!devices.empty()) login.Clear();
        return devices;
    }
};
} // namespace

void SmsLoginSession::Clear() noexcept {
    const auto wipe_wide = [](std::wstring& value) {
        if (!value.empty()) SecureZeroMemory(value.data(), value.size() * sizeof(wchar_t));
        value.clear();
    };
    const auto wipe_narrow = [](std::string& value) {
        if (!value.empty()) SecureZeroMemory(value.data(), value.size());
        value.clear();
    };
    wipe_wide(cookies);
    wipe_wide(csrf_token);
    wipe_wide(html_version);
    wipe_wide(callback_url);
    wipe_narrow(captcha_random);
    wipe_narrow(captcha_ticket);
}

SmsLoginSession::~SmsLoginSession() { Clear(); }

namespace cloud_detail {
std::vector<CloudDevice> ParseDevicesJson(std::string_view json) {
    std::vector<CloudDevice> result;
    constexpr std::string_view marker = "\"pair_key\"";
    std::size_t cursor = 0;
    while ((cursor = json.find(marker, cursor)) != std::string_view::npos) {
        const auto key = JsonString(json, "pair_key", cursor);
        if (IsPairKey(key)) {
            const auto name = JsonStringBefore(json, cursor, "name");
            const auto serial = JsonStringBefore(json, cursor, "sn");
            result.push_back({name.empty() ? L"DJI Power" : Wide(name), Wide(serial), key});
        }
        cursor += marker.size();
    }
    return result;
}

std::wstring ExtractCallbackUrl(std::string_view json) {
    return Wide(JsonString(json, "callbackUrl"));
}

std::string ExtractMemberToken(std::string_view json) {
    // Token 可能位于 JSON、重定向 URL 或 Set-Cookie 中，因此按安全字符边界扫描。
    std::size_t cursor = 0;
    while ((cursor = json.find("US_", cursor)) != std::string_view::npos) {
        std::size_t end = cursor + 3;
        while (end < json.size()) {
            const unsigned char c = static_cast<unsigned char>(json[end]);
            if (!std::isalnum(c) && c != '_' && c != '-' && c != '.' && c != '~' &&
                c != '+' && c != '/' && c != '=' && c != '%') break;
            ++end;
        }
        std::string token(json.substr(cursor, end - cursor));
        for (std::size_t i = 0; i + 2 < token.size();) {
            if (token[i] != '%') { ++i; continue; }
            const auto nibble = [](char value) -> int {
                if (value >= '0' && value <= '9') return value - '0';
                if (value >= 'a' && value <= 'f') return value - 'a' + 10;
                if (value >= 'A' && value <= 'F') return value - 'A' + 10;
                return -1;
            };
            const int high = nibble(token[i + 1]);
            const int low = nibble(token[i + 2]);
            if (high < 0 || low < 0) { ++i; continue; }
            token.replace(i, 3, 1, static_cast<char>((high << 4) | low));
            ++i;
        }
        if (token.size() >= 12) return token;
        cursor += 3;
    }
    return {};
}

std::wstring ExtractApiError(std::string_view json, std::wstring_view fallback) {
    const auto message = JsonString(json, "message");
    const int code = JsonCode(json);
    std::wstring result = message.empty() ? std::wstring(fallback) : Wide(message);
    if (code >= 0) result += L"（错误码 " + std::to_wstring(code) + L"）";
    return result;
}
} // namespace cloud_detail

PairKeyProvider& DefaultPairKeyProvider() {
    static WinHttpPairKeyProvider provider;
    return provider;
}
} // namespace dji_power
