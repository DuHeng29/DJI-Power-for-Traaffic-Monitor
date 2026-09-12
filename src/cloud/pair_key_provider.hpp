// 本文件定义一次性云端凭据换取本地 pair_key 的可替换 Provider 接口。
#pragma once
#include <string>
#include <vector>

namespace dji_power {
struct CloudDevice { std::wstring name; std::wstring serial_number; std::string pair_key; };
class PairKeyProvider {
public:
    virtual ~PairKeyProvider() = default;
    virtual std::vector<CloudDevice> FetchWithMemberToken(const std::wstring& token, std::wstring& error) = 0;
};
PairKeyProvider& DefaultPairKeyProvider();
} // namespace dji_power

