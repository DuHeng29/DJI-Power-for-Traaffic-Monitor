// 本文件定义 DJI Home 账号登录会话与 Pair Key 查询所需的接口边界。
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dji_power {

struct CloudDevice {
    std::wstring name;
    std::wstring serial_number;
    std::string pair_key;
};

// DJI Home 移动端登录所需的短期会话；密码不保存在此结构中。
struct AccountLoginSession {
    std::string client_name{"android-1.5.16"};
    std::string device_id;
    std::string captcha_random;
    std::string captcha_ticket;

    AccountLoginSession() = default;
    AccountLoginSession(const AccountLoginSession&) = delete;
    AccountLoginSession& operator=(const AccountLoginSession&) = delete;
    ~AccountLoginSession();
    void Clear() noexcept;
};

// 登录会话只存在于“获取 Key”窗口生命周期内，析构时主动擦除敏感字段。
struct SmsLoginSession {
    std::wstring cookies;
    std::wstring csrf_token;
    std::wstring callback_url;
    std::wstring html_version;
    std::string captcha_random;
    std::string captcha_ticket;

    SmsLoginSession() = default;
    SmsLoginSession(const SmsLoginSession&) = delete;
    SmsLoginSession& operator=(const SmsLoginSession&) = delete;
    ~SmsLoginSession();
    void Clear() noexcept;
};

class PairKeyProvider {
public:
    virtual ~PairKeyProvider() = default;

    virtual std::vector<CloudDevice> FetchWithMemberToken(
        const std::wstring& token, std::wstring& error) = 0;

    // 与已验证的 get_pari_key.py 一致：图片验证码、账号密码登录、查询设备。
    virtual bool BeginAccountLogin(AccountLoginSession& login,
                                   std::vector<std::uint8_t>& captcha_png,
                                   std::wstring& error) = 0;
    virtual bool RefreshAccountCaptcha(AccountLoginSession& login,
                                       std::vector<std::uint8_t>& captcha_png,
                                       std::wstring& error) = 0;
    virtual std::vector<CloudDevice> LoginAndFetch(
        AccountLoginSession& login, const std::wstring& account,
        const std::wstring& password, const std::wstring& image_code,
        const std::wstring& verification_code, std::wstring& error) = 0;

    // 初始化 DJI 官方账号会话，并返回本次会话对应的图片验证码 PNG。
    virtual bool BeginSmsLogin(SmsLoginSession& login, std::vector<std::uint8_t>& captcha_png,
                               std::wstring& error) = 0;
    virtual bool RefreshCaptcha(SmsLoginSession& login, std::vector<std::uint8_t>& captcha_png,
                                std::wstring& error) = 0;

    // 只有用户明确点击“发送验证码”时才会触发真实短信。
    virtual bool SendSmsCode(SmsLoginSession& login, const std::wstring& area_code,
                             const std::wstring& phone, const std::wstring& image_code,
                             std::wstring& error) = 0;

    // 本阶段只完成短信登录并保留网页回调票据，不擅自继续请求设备 Key。
    virtual bool CompleteSmsLogin(
        SmsLoginSession& login, const std::wstring& area_code, const std::wstring& phone,
        const std::wstring& sms_code, std::wstring& error) = 0;

    // 登录成功后由用户单独触发：完成网页回调，并尝试从回调会话读取 Home Token 和设备 Key。
    virtual std::vector<CloudDevice> FetchAfterWebLogin(
        SmsLoginSession& login, std::wstring& error) = 0;
};

PairKeyProvider& DefaultPairKeyProvider();

namespace cloud_detail {
// 这些解析函数独立于网络，供自动化测试覆盖 DJI 返回字段变化。
std::string BuildMobileSignatureForTest(std::string_view key, std::string_view material);
bool IsMemberTokenForTest(std::string_view value);
std::vector<CloudDevice> ParseDevicesJson(std::string_view json);
std::wstring ExtractCallbackUrl(std::string_view json);
std::string ExtractMemberToken(std::string_view json);
std::wstring ExtractApiError(std::string_view json, std::wstring_view fallback);
} // namespace cloud_detail
} // namespace dji_power

