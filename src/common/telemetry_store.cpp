// 本文件实现线程安全的遥测快照读写。
#include "common/telemetry_store.hpp"

namespace dji_power {
TelemetrySnapshot TelemetryStore::Get() const { std::scoped_lock lock(mutex_); return snapshot_; }
void TelemetryStore::Update(const TelemetrySnapshot& value) { std::scoped_lock lock(mutex_); snapshot_ = value; }
void TelemetryStore::SetStatus(ConnectionState state, std::wstring status) {
    std::scoped_lock lock(mutex_);
    snapshot_.connection = state;
    snapshot_.status = std::move(status);
}
} // namespace dji_power

