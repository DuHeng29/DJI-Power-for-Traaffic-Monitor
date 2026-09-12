// 本文件独立实现 DUML 校验、分包和 DJI Power 0x5A/0x61 遥测字段解析。
#include "protocol/duml.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace dji_power::duml {
namespace {
std::uint16_t Read16(std::span<const std::uint8_t> data, std::size_t offset) {
    return static_cast<std::uint16_t>(data[offset] | (static_cast<std::uint16_t>(data[offset + 1]) << 8U));
}
struct Tlv { std::uint16_t tag; std::span<const std::uint8_t> value; };
std::vector<Tlv> ParseTlvs(std::span<const std::uint8_t> data) {
    std::vector<Tlv> result;
    for (std::size_t offset = 0; offset + 4 <= data.size();) {
        const auto tag = Read16(data, offset);
        const auto length = Read16(data, offset + 2);
        const auto end = offset + 4U + length;
        if (end > data.size()) break;
        result.push_back({tag, data.subspan(offset + 4, length)});
        offset = end;
    }
    return result;
}
std::optional<std::span<const std::uint8_t>> Find(std::span<const std::uint8_t> data, std::uint16_t tag) {
    for (const auto& item : ParseTlvs(data)) if (item.tag == tag) return item.value;
    return std::nullopt;
}
} // namespace

std::uint8_t Crc8(std::span<const std::uint8_t> data) {
    std::uint8_t value = 0x77;
    for (const auto byte : data) {
        value ^= byte;
        for (int bit = 0; bit < 8; ++bit) value = (value & 1U) ? static_cast<std::uint8_t>((value >> 1U) ^ 0x8CU) : static_cast<std::uint8_t>(value >> 1U);
    }
    return value;
}
std::uint16_t Crc16(std::span<const std::uint8_t> data) {
    std::uint16_t value = 0x3692;
    for (const auto byte : data) {
        value ^= byte;
        for (int bit = 0; bit < 8; ++bit) value = (value & 1U) ? static_cast<std::uint16_t>((value >> 1U) ^ 0x8408U) : static_cast<std::uint16_t>(value >> 1U);
    }
    return value;
}
std::vector<std::uint8_t> Encode(const Packet& packet) {
    const auto length = packet.payload.size() + 13U;
    if (length > 0x3FFU) throw std::invalid_argument("DUML frame too large");
    std::vector<std::uint8_t> out{
        0x55, static_cast<std::uint8_t>(length), static_cast<std::uint8_t>(0x04U | ((length >> 8U) & 0x03U)), 0,
        packet.source, packet.destination, static_cast<std::uint8_t>(packet.sequence), static_cast<std::uint8_t>(packet.sequence >> 8U),
        packet.flags, packet.command_set, packet.command_id};
    out[3] = Crc8(std::span(out).first(3));
    out.insert(out.end(), packet.payload.begin(), packet.payload.end());
    const auto crc = Crc16(out);
    out.push_back(static_cast<std::uint8_t>(crc));
    out.push_back(static_cast<std::uint8_t>(crc >> 8U));
    return out;
}
std::optional<Packet> Decode(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < 13 || bytes[0] != 0x55) return std::nullopt;
    const auto length = static_cast<std::size_t>(bytes[1] | ((bytes[2] & 0x03U) << 8U));
    if (length != bytes.size() || bytes[3] != Crc8(bytes.first(3)) || Read16(bytes, bytes.size() - 2) != Crc16(bytes.first(bytes.size() - 2))) return std::nullopt;
    Packet packet{bytes[4], bytes[5], Read16(bytes, 6), bytes[8], bytes[9], bytes[10], {}};
    packet.payload.assign(bytes.begin() + 11, bytes.end() - 2);
    return packet;
}
std::vector<Packet> StreamDecoder::Feed(std::span<const std::uint8_t> chunk) {
    buffer_.insert(buffer_.end(), chunk.begin(), chunk.end());
    std::vector<Packet> result;
    while (!buffer_.empty()) {
        const auto start = std::find(buffer_.begin(), buffer_.end(), 0x55);
        buffer_.erase(buffer_.begin(), start);
        if (buffer_.size() < 4) break;
        const auto length = static_cast<std::size_t>(buffer_[1] | ((buffer_[2] & 0x03U) << 8U));
        if (length < 13 || length > 0x3FF) { buffer_.erase(buffer_.begin()); continue; }
        if (buffer_.size() < length) break;
        if (auto packet = Decode(std::span(buffer_).first(length))) {
            result.push_back(std::move(*packet));
            buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(length));
        } else {
            buffer_.erase(buffer_.begin());
        }
    }
    return result;
}
std::optional<Report> ParseReport(std::span<const std::uint8_t> payload) {
    // 推送通常有 16 字节公共头；同时接受裸 TLV，便于离线测试和抓包回放。
    auto body = payload;
    if (payload.size() >= 20 && payload[2] == 0x10 && payload[3] == 0x00) body = payload.subspan(16);
    Report report;
    bool found = false;
    if (const auto battery = Find(body, 0x3020); battery && battery->size() >= 4) {
        report.battery_percent = static_cast<int>(Read16(*battery, 0) / 100U);
        report.runtime_min = Read16(*battery, 2);
        found = true;
    }
    if (const auto power = Find(body, 0x3030); power && power->size() >= 4) {
        report.output_w = Read16(*power, 0);
        report.input_w = Read16(*power, 2);
        found = true;
    }
    return found ? std::optional<Report>(report) : std::nullopt;
}
std::string NormalizePairKey(std::string value) {
    value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c) != 0; }), value.end());
    if (value.size() != 32 || !std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isxdigit(c) != 0; })) throw std::invalid_argument("pair_key must be 32 hex characters");
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}
} // namespace dji_power::duml

