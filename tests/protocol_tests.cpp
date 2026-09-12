// 本文件提供无需蓝牙硬件即可执行的 DUML 协议回归测试。
#include "protocol/duml.hpp"
#include <cassert>
#include <iostream>

using namespace dji_power::duml;

int main() {
    Packet request{0x02, 0xAB, 0x1001, 0x20, 0x5A, 0x6A, {0x00}};
    const auto encoded = Encode(request);
    const auto decoded = Decode(encoded);
    assert(decoded && decoded->sequence == 0x1001 && decoded->payload == std::vector<std::uint8_t>{0x00});

    StreamDecoder stream;
    assert(stream.Feed(std::span(encoded).first(5)).empty());
    const auto packets = stream.Feed(std::span(encoded).subspan(5));
    assert(packets.size() == 1 && packets[0].command_id == 0x6A);

    // 78.00%、222 分钟、输出 127 W、输入 436 W。
    const std::vector<std::uint8_t> report{
        0x20,0x30,0x09,0x00,0x78,0x1E,0xDE,0x00,0x02,0x78,0x1E,0xDE,0x00,
        0x30,0x30,0x04,0x00,0x7F,0x00,0xB4,0x01};
    const auto parsed = ParseReport(report);
    assert(parsed && parsed->battery_percent == 78 && parsed->runtime_min == 222);
    assert(parsed->output_w == 127 && parsed->input_w == 436);
    assert(NormalizePairKey("0123456789ABCDEF0123456789abcdef") == "0123456789abcdef0123456789abcdef");
    std::cout << "protocol tests passed\n";
    return 0;
}

