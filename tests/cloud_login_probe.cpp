// 本文件提供 DJI Home 移动端登录初始化诊断；不会提交账号、密码或验证码。
#include "cloud/pair_key_provider.hpp"

#include <iostream>

int wmain() {
    dji_power::AccountLoginSession login;
    std::vector<std::uint8_t> captcha;
    std::wstring error;
    if (!dji_power::DefaultPairKeyProvider().BeginAccountLogin(login, captcha, error)) {
        std::wcerr << L"DJI Home 登录初始化失败：" << error << L'\n';
        return 1;
    }
    std::wcout << L"DJI Home 登录初始化成功，验证码字节数：" << captcha.size() << L'\n';
    return captcha.empty() ? 1 : 0;
}