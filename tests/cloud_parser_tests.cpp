// 本文件验证云端设备列表、登录令牌和错误信息的纯 JSON 解析逻辑。
#include "cloud/pair_key_provider.hpp"

#include <iostream>
#include <string>

namespace {
int failures = 0;

void Check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "失败：" << message << '\n';
    ++failures;
}
} // namespace

int main() {
    using namespace dji_power::cloud_detail;

    Check(BuildMobileSignatureForTest("test-key", "test-material") ==
              "7ClZ4RrnFJJtHocTHiKZykIkn1g=", "移动端 HMAC-SHA1 签名应与 Python 一致");
    Check(IsMemberTokenForTest("US_example"), "应接受 US 区域 Token");
    Check(IsMemberTokenForTest("CN_example"), "应接受 CN 区域 Token");
    Check(IsMemberTokenForTest("EU_example"), "应兼容其他 DJI 区域 Token");
    Check(!IsMemberTokenForTest("token"), "应拒绝没有区域前缀的普通 Token");
    Check(!IsMemberTokenForTest("us_example"), "区域前缀必须为大写字母");

    const auto devices = ParseDevicesJson(R"({"data":{"list":[
        {"name":"Power1000Mini-110105","sn":"ABC123","pair_key":"00112233445566778899aabbccddeeff"},
        {"name":"Power 2000","sn":"DEF456","pair_key":"ffeeddccbbaa99887766554433221100"},
        {"name":"无效设备","sn":"BAD","pair_key":"1234"}
    ]}})");
    Check(devices.size() == 2, "应保留账号下所有具有合法 Pair Key 的设备");
    if (!devices.empty()) {
        Check(devices[0].name == L"DJI Power 1000 Mini", "应将原始名称转换为产品名称");
        Check(devices[0].serial_number == L"ABC123", "应解析序列号");
        Check(devices[0].pair_key == "00112233445566778899aabbccddeeff", "应解析 Pair Key");
        Check(devices[1].name == L"DJI Power 2000", "应解析第二台设备");
        Check(devices[1].serial_number == L"DEF456", "应保留第二台设备序列号");
    }

    Check(ExtractMemberToken(R"({"data":{"token":"US_example_token"}})") == "US_example_token",
          "应提取 US_ Member Token");
    Check(ExtractCallbackUrl(R"({"data":{"callbackUrl":"https://store.dji.com/login/callback?ticket=secret"}})") ==
              L"https://store.dji.com/login/callback?ticket=secret",
          "应提取网页登录回调地址");
    Check(ExtractMemberToken(R"({"token":"other_token"})").empty(),
          "应拒绝非 Member Token");
    Check(ExtractMemberToken("Set-Cookie: member=US_example-token.123; Secure") ==
              "US_example-token.123",
          "应从回调响应头中提取 Member Token");
    Check(ExtractMemberToken("token=US_example%2Fpart==&next=1") == "US_example/part==",
          "应解码 URL 编码并保留 Base64 填充字符");
    Check(ExtractMemberToken("value=US_short").empty(), "应拒绝过短的 Token 片段");
    Check(ExtractApiError(R"({"code":1001,"message":"验证码错误"})", L"请求失败") ==
              L"验证码错误（错误码 1001）",
          "应返回服务端错误信息和错误码");
    Check(ExtractApiError("{}", L"请求失败") == L"请求失败", "缺失错误字段时应使用回退文案");

    if (failures == 0) std::cout << "云端解析测试通过\n";
    return failures == 0 ? 0 : 1;
}
