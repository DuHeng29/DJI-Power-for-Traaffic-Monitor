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

    const auto devices = ParseDevicesJson(R"({"data":{"list":[
        {"name":"Power 1000 V2","sn":"ABC123","pair_key":"00112233445566778899aabbccddeeff"},
        {"name":"无效设备","sn":"BAD","pair_key":"1234"}
    ]}})");
    Check(devices.size() == 1, "应仅保留合法 Pair Key 的设备");
    if (!devices.empty()) {
        Check(devices[0].name == L"Power 1000 V2", "应解析设备名称");
        Check(devices[0].serial_number == L"ABC123", "应解析序列号");
        Check(devices[0].pair_key == "00112233445566778899aabbccddeeff", "应解析 Pair Key");
    }

    Check(ExtractMemberToken(R"({"data":{"token":"US_example_token"}})") == "US_example_token",
          "应提取 US_ Member Token");
    Check(ExtractMemberToken(R"({"token":"other_token"})").empty(),
          "应拒绝非 Member Token");
    Check(ExtractApiError(R"({"code":1001,"message":"验证码错误"})", L"请求失败") ==
              L"验证码错误（错误码 1001）",
          "应返回服务端错误信息和错误码");
    Check(ExtractApiError("{}", L"请求失败") == L"请求失败", "缺失错误字段时应使用回退文案");

    if (failures == 0) std::cout << "云端解析测试通过\n";
    return failures == 0 ? 0 : 1;
}
