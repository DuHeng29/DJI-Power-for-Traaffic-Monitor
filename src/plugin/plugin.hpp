// 本文件定义 TrafficMonitor 插件实例及五个 DJI Power 显示项目。
#pragma once
#include "PluginInterface.h"
#include <array>
#include <string>

namespace dji_power {
class PowerItem final : public IPluginItem {
public:
    PowerItem(const wchar_t* name, const wchar_t* id, const wchar_t* label, const wchar_t* sample)
        : name_(name), id_(id), label_(label), sample_(sample) {}
    const wchar_t* GetItemName() const override { return name_; }
    const wchar_t* GetItemId() const override { return id_; }
    const wchar_t* GetItemLableText() const override { return label_; }
    const wchar_t* GetItemValueText() const override { return value_.c_str(); }
    const wchar_t* GetItemValueSampleText() const override { return sample_; }
    void SetValue(std::wstring value) { value_ = std::move(value); }
private:
    const wchar_t* name_;
    const wchar_t* id_;
    const wchar_t* label_;
    const wchar_t* sample_;
    std::wstring value_{L"--"};
};

class Plugin final : public ITMPlugin {
public:
    static Plugin& Instance();
    IPluginItem* GetItem(int index) override;
    void DataRequired() override;
    const wchar_t* GetInfo(PluginInfoIndex index) override;
    OptionReturn ShowOptionsDialog(void* parent) override;
    void OnInitialize(ITrafficMonitor* app) override;
    const wchar_t* GetTooltipInfo() override;
private:
    Plugin();
    std::array<PowerItem, 5> items_;
    std::wstring tooltip_;
};
} // namespace dji_power

extern "C" __declspec(dllexport) ITMPlugin* TMPluginGetInstance();

