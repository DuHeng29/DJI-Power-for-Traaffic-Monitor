// 本文件使用 C++/WinRT 在插件后台线程中维持 DJI Power 的 BLE/DUML 会话。
#include "ble/ble_manager.hpp"
#include "protocol/duml.hpp"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/base.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;
namespace bt = winrt::Windows::Devices::Bluetooth;
namespace adv = winrt::Windows::Devices::Bluetooth::Advertisement;
namespace gatt = winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;
namespace streams = winrt::Windows::Storage::Streams;

namespace dji_power {
namespace {
streams::IBuffer ToBuffer(const std::vector<std::uint8_t>& bytes) {
    streams::DataWriter writer;
    writer.WriteBytes(bytes);
    return writer.DetachBuffer();
}
std::vector<std::uint8_t> FromBuffer(const streams::IBuffer& buffer) {
    streams::DataReader reader = streams::DataReader::FromBuffer(buffer);
    std::vector<std::uint8_t> bytes(reader.UnconsumedBufferLength());
    reader.ReadBytes(bytes);
    return bytes;
}
std::wstring AddressText(std::uint64_t value) {
    wchar_t text[18]{};
    swprintf_s(text, L"%02llX:%02llX:%02llX:%02llX:%02llX:%02llX",
        (value >> 40) & 0xFF, (value >> 32) & 0xFF, (value >> 24) & 0xFF,
        (value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF);
    return text;
}
bool IsDjiPowerAdvertisement(const adv::BluetoothLEAdvertisementReceivedEventArgs& args,
                             std::uint64_t configured_address) {
    if (configured_address != 0 && args.BluetoothAddress() == configured_address) return true;

    auto name = std::wstring(args.Advertisement().LocalName());
    std::transform(name.begin(), name.end(), name.begin(), ::towlower);
    if (name.starts_with(L"power") || name.find(L"dji power") != std::wstring::npos) return true;

    const auto service_uuid = winrt::guid(duml::kServiceUuid);
    for (const auto& uuid : args.Advertisement().ServiceUuids()) {
        if (uuid == service_uuid) return true;
    }

    // DJI 的 Windows 广播项把公司 ID 与厂商数据分开暴露；已知 Power 型号码为 91/94/97/98。
    for (const auto& item : args.Advertisement().ManufacturerData()) {
        if (item.CompanyId() != 0x08AA) continue;
        const auto bytes = FromBuffer(item.Data());
        if (bytes.empty()) return true;
        switch (bytes.front()) {
        case 0x91: case 0x94: case 0x97: case 0x98:
            return true;
        default:
            break;
        }
    }
    return false;
}
} // namespace

struct BleManager::Impl {
    mutable std::mutex mutex;
    std::condition_variable condition;
    std::jthread worker;
    PluginConfig config;
    std::vector<DiscoveredDevice> devices;
    TelemetryStore store;
    bool reconnect_requested{};
    bool link_lost{};

    std::mutex response_mutex;
    std::condition_variable response_condition;
    std::optional<duml::Packet> response;
    std::uint16_t sequence{0x1000};
    duml::StreamDecoder decoder;

    adv::BluetoothLEAdvertisementWatcher watcher{nullptr};
    bt::BluetoothLEDevice device{nullptr};
    gatt::GattCharacteristic write_characteristic{nullptr};
    gatt::GattCharacteristic notify_characteristic{nullptr};
    winrt::event_token received_token{};
    winrt::event_token value_token{};
    winrt::event_token connection_token{};

    void Start() {
        std::scoped_lock lock(mutex);
        if (worker.joinable()) return;
        config = LoadConfig();
        worker = std::jthread([this](std::stop_token token) { Run(token); });
    }
    void Stop() {
        if (!worker.joinable()) return;
        worker.request_stop();
        condition.notify_all();
        response_condition.notify_all();
        worker.join();
    }
    void BeginScan() {
        watcher = adv::BluetoothLEAdvertisementWatcher();
        watcher.ScanningMode(adv::BluetoothLEScanningMode::Active);
        received_token = watcher.Received([this](auto const&, const adv::BluetoothLEAdvertisementReceivedEventArgs& args) {
            std::uint64_t configured_address = 0;
            { std::scoped_lock lock(mutex); configured_address = config.bluetooth_address; }
            if (!IsDjiPowerAdvertisement(args, configured_address)) return;
            const auto name = args.Advertisement().LocalName().empty() ? AddressText(args.BluetoothAddress()) : std::wstring(args.Advertisement().LocalName());
            std::scoped_lock lock(mutex);
            const auto found = std::find_if(devices.begin(), devices.end(), [&](const auto& item) { return item.address == args.BluetoothAddress(); });
            if (found == devices.end()) devices.push_back({args.BluetoothAddress(), name, args.RawSignalStrengthInDBm()});
            else { found->name = name; found->rssi = args.RawSignalStrengthInDBm(); }
            condition.notify_all();
        });
        watcher.Start();
        store.SetStatus(ConnectionState::scanning, L"正在扫描 DJI Power");
    }
    void EndScan() {
        if (!watcher) return;
        watcher.Stop();
        watcher.Received(received_token);
        watcher = nullptr;
    }
    std::uint64_t TargetAddress() {
        std::scoped_lock lock(mutex);
        if (config.bluetooth_address) {
            const auto present = std::find_if(devices.begin(), devices.end(), [&](const auto& item) { return item.address == config.bluetooth_address; });
            return present == devices.end() ? 0 : config.bluetooth_address;
        }
        if (devices.empty()) return 0;
        return std::max_element(devices.begin(), devices.end(), [](const auto& a, const auto& b) { return a.rssi < b.rssi; })->address;
    }
    void HandleValue(const gatt::GattValueChangedEventArgs& args) {
        const auto bytes = FromBuffer(args.CharacteristicValue());
        for (auto& packet : decoder.Feed(bytes)) {
            if (packet.IsResponse()) {
                std::scoped_lock lock(response_mutex);
                response = packet;
                response_condition.notify_all();
            }
            if (packet.command_set != 0x5A || packet.command_id != 0x61) continue;
            const auto report = duml::ParseReport(packet.payload);
            if (!report) continue;
            auto value = store.Get();
            if (report->battery_percent) value.battery_percent = *report->battery_percent;
            if (report->runtime_min) value.runtime_min = *report->runtime_min;
            if (report->input_w) value.input_w = *report->input_w;
            if (report->output_w) value.output_w = *report->output_w;
            value.connection = ConnectionState::connected;
            value.status = L"已连接";
            value.updated_at = std::chrono::steady_clock::now();
            store.Update(value);
        }
    }
    std::optional<duml::Packet> Request(std::uint8_t command, std::vector<std::uint8_t> payload, std::chrono::seconds timeout = 8s) {
        const auto current_sequence = ++sequence;
        {
            std::scoped_lock lock(response_mutex);
            response.reset();
        }
        const auto bytes = duml::Encode({0x02, 0xAB, current_sequence, 0x20, 0x5A, command, std::move(payload)});
        const auto result = write_characteristic.WriteValueAsync(ToBuffer(bytes), gatt::GattWriteOption::WriteWithResponse).get();
        if (result != gatt::GattCommunicationStatus::Success) return std::nullopt;
        std::unique_lock lock(response_mutex);
        const auto matched = response_condition.wait_for(lock, timeout, [&] {
            return response && response->sequence == current_sequence && response->command_set == 0x5A && response->command_id == command;
        });
        return matched ? response : std::nullopt;
    }
    bool Authenticate(const std::string& pair_key) {
        store.SetStatus(ConnectionState::authenticating, L"正在验证 Pair Key");
        const auto challenge = Request(0x6A, {0x00});
        if (!challenge || challenge->payload.size() < 5 || challenge->payload[0] != 0) return false;
        std::vector<std::uint8_t> material{0x01};
        material.insert(material.end(), challenge->payload.begin() + 1, challenge->payload.begin() + 5);
        material.insert(material.end(), pair_key.begin(), pair_key.end());
        material.push_back(0);
        const auto answer = Request(0x6A, std::move(material));
        return answer && !answer->payload.empty() && answer->payload[0] == 0;
    }
    bool Connect(std::uint64_t address, const std::string& pair_key) {
        store.SetStatus(ConnectionState::connecting, L"正在连接 " + AddressText(address));
        device = bt::BluetoothLEDevice::FromBluetoothAddressAsync(address).get();
        if (!device) return false;
        connection_token = device.ConnectionStatusChanged([this](auto const& sender, auto const&) {
            if (sender.ConnectionStatus() == bt::BluetoothConnectionStatus::Disconnected) {
                std::scoped_lock lock(mutex);
                link_lost = true;
                condition.notify_all();
            }
        });
        const auto services = device.GetGattServicesForUuidAsync(winrt::guid(duml::kServiceUuid), bt::BluetoothCacheMode::Uncached).get();
        if (services.Status() != gatt::GattCommunicationStatus::Success || services.Services().Size() == 0) return false;
        const auto service = services.Services().GetAt(0);
        const auto writes = service.GetCharacteristicsForUuidAsync(winrt::guid(duml::kWriteUuid), bt::BluetoothCacheMode::Uncached).get();
        const auto notifies = service.GetCharacteristicsForUuidAsync(winrt::guid(duml::kNotifyUuid), bt::BluetoothCacheMode::Uncached).get();
        if (writes.Status() != gatt::GattCommunicationStatus::Success || notifies.Status() != gatt::GattCommunicationStatus::Success || writes.Characteristics().Size() == 0 || notifies.Characteristics().Size() == 0) return false;
        write_characteristic = writes.Characteristics().GetAt(0);
        notify_characteristic = notifies.Characteristics().GetAt(0);
        value_token = notify_characteristic.ValueChanged([this](auto const&, const gatt::GattValueChangedEventArgs& args) { HandleValue(args); });
        const auto subscribe = notify_characteristic.WriteClientCharacteristicConfigurationDescriptorAsync(gatt::GattClientCharacteristicConfigurationDescriptorValue::Notify).get();
        if (subscribe != gatt::GattCommunicationStatus::Success || !Authenticate(pair_key)) return false;
        auto value = store.Get();
        value.connection = ConnectionState::connected;
        value.device_name = device.Name().empty() ? AddressText(address) : std::wstring(device.Name());
        value.status = L"已连接，等待遥测";
        store.Update(value);
        // 请求两个只读配置模块；周期性 0x61 推送随后会更新展示字段。
        Request(0x60, {0x00, 0x01, 0x10});
        Request(0x60, {0x00, 0x04, 0x10});
        return true;
    }
    void Disconnect() {
        try {
            if (notify_characteristic) {
                notify_characteristic.ValueChanged(value_token);
                notify_characteristic.WriteClientCharacteristicConfigurationDescriptorAsync(gatt::GattClientCharacteristicConfigurationDescriptorValue::None).get();
            }
            if (device) device.ConnectionStatusChanged(connection_token);
        } catch (...) {}
        notify_characteristic = nullptr;
        write_characteristic = nullptr;
        device = nullptr;
        decoder.Clear();
    }
    void Run(std::stop_token token) {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        try {
            BeginScan();
            int retry_seconds = 1;
            bool reconnect_allowed = true;
            while (!token.stop_requested()) {
                PluginConfig current;
                bool requested = false;
                {
                    std::unique_lock lock(mutex);
                    condition.wait_for(lock, 1s, [&] { return token.stop_requested() || reconnect_requested || link_lost; });
                    current = config;
                    requested = reconnect_requested;
                    reconnect_requested = false;
                }
                if (token.stop_requested()) break;
                if ((!current.auto_connect && !requested) || (!reconnect_allowed && !requested) || !IsValidPairKey(current.pair_key)) continue;
                if (requested) reconnect_allowed = true;
                const auto address = TargetAddress();
                if (!address) continue;
                link_lost = false;
                bool connected = false;
                try { connected = Connect(address, current.pair_key); }
                catch (const winrt::hresult_error& error) { store.SetStatus(ConnectionState::error, L"BLE 错误：" + std::wstring(error.message())); }
                catch (...) { store.SetStatus(ConnectionState::error, L"BLE 连接失败"); }
                if (!connected) {
                    Disconnect();
                    store.SetStatus(ConnectionState::error, L"连接或 Pair Key 验证失败");
                    std::this_thread::sleep_for(std::chrono::seconds(retry_seconds));
                    retry_seconds = std::min(retry_seconds * 2, 30);
                    continue;
                }
                retry_seconds = 1;
                reconnect_allowed = true;
                std::unique_lock lock(mutex);
                condition.wait(lock, [&] { return token.stop_requested() || reconnect_requested || link_lost; });
                if (link_lost && !current.auto_reconnect) reconnect_allowed = false;
                lock.unlock();
                Disconnect();
                if (!token.stop_requested()) store.SetStatus(ConnectionState::scanning, L"连接已断开，正在重连");
            }
            EndScan();
            Disconnect();
        } catch (const winrt::hresult_error& error) { store.SetStatus(ConnectionState::error, L"蓝牙初始化失败：" + std::wstring(error.message())); }
        winrt::uninit_apartment();
    }
};

BleManager& BleManager::Instance() { static BleManager instance; return instance; }
BleManager::BleManager() : impl_(std::make_unique<Impl>()) {}
BleManager::~BleManager() { Stop(); }
void BleManager::Start() { impl_->Start(); }
void BleManager::Stop() { impl_->Stop(); }
void BleManager::Reconfigure(const PluginConfig& value) { std::scoped_lock lock(impl_->mutex); impl_->config = value; impl_->reconnect_requested = true; impl_->condition.notify_all(); }
void BleManager::Rescan() {
    std::scoped_lock lock(impl_->mutex);
    impl_->devices.clear();
    impl_->store.SetStatus(ConnectionState::scanning, L"正在扫描 DJI Power");
    impl_->condition.notify_all();
}
void BleManager::ConnectNow() { std::scoped_lock lock(impl_->mutex); impl_->reconnect_requested = true; impl_->condition.notify_all(); }
std::vector<DiscoveredDevice> BleManager::Devices() const { std::scoped_lock lock(impl_->mutex); return impl_->devices; }
TelemetrySnapshot BleManager::Snapshot() const { return impl_->store.Get(); }
} // namespace dji_power

