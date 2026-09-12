// 本文件定义 DJI Power BLE 上使用的 DUML 帧、流重组和只读遥测解析接口。
#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace dji_power::duml {
inline constexpr wchar_t kServiceUuid[] = L"0000a002-0000-1000-8000-00805f9b34fb";
inline constexpr wchar_t kWriteUuid[] = L"0000c304-0000-1000-8000-00805f9b34fb";
inline constexpr wchar_t kNotifyUuid[] = L"0000c305-0000-1000-8000-00805f9b34fb";

struct Packet {
    std::uint8_t source{};
    std::uint8_t destination{};
    std::uint16_t sequence{};
    std::uint8_t flags{};
    std::uint8_t command_set{};
    std::uint8_t command_id{};
    std::vector<std::uint8_t> payload;
    bool IsResponse() const { return (flags & 0x80U) != 0; }
};

struct Report {
    std::optional<int> battery_percent;
    std::optional<int> runtime_min;
    std::optional<int> input_w;
    std::optional<int> output_w;
};

std::uint8_t Crc8(std::span<const std::uint8_t> data);
std::uint16_t Crc16(std::span<const std::uint8_t> data);
std::vector<std::uint8_t> Encode(const Packet& packet);
std::optional<Packet> Decode(std::span<const std::uint8_t> bytes);
std::optional<Report> ParseReport(std::span<const std::uint8_t> payload);
std::string NormalizePairKey(std::string value);

class StreamDecoder {
public:
    std::vector<Packet> Feed(std::span<const std::uint8_t> chunk);
    void Clear() { buffer_.clear(); }
private:
    std::vector<std::uint8_t> buffer_;
};
} // namespace dji_power::duml

