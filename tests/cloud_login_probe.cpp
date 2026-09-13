// 本文件提供只初始化 DJI 登录会话的诊断程序；不会发送短信或保存验证码。
#include "cloud/pair_key_provider.hpp"

#include <iostream>

int wmain() {
    dji_power::SmsLoginSession login;
    std::vector<std::uint8_t> captcha;
    std::wstring error;
    if (!dji_power::DefaultPairKeyProvider().BeginSmsLogin(login, captcha, error)) {
        std::wcerr << L"DJI 登录初始化失败：" << error << L'\n';
        return 1;
    }
    std::wcout << L"DJI 登录初始化成功，验证码字节数：" << captcha.size() << L'\n';
    return captcha.empty() ? 1 : 0;
}
