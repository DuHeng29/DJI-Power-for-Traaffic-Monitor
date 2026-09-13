// 本文件以支持 DPI 和 ClearType 的原生 Win32 控件实现 DJI Power 插件设置窗口。
#include "ui/options_dialog.hpp"
#include "ble/ble_manager.hpp"
#include "cloud/pair_key_provider.hpp"
#include "ui/account_login_dialog.hpp"
#include "common/config.hpp"
#include "protocol/duml.hpp"

#include <commctrl.h>
#include <uxtheme.h>
#include <array>
#include <string>
#include <vector>

extern HMODULE g_module;

namespace dji_power {
namespace {
enum : int {
    IDC_TITLE = 100,
    IDC_SUBTITLE,
    IDC_BLE_MODE,
    IDC_CLOUD_MODE,
    IDC_DEVICE,
    IDC_SCAN,
    IDC_PAIR_KEY,
    IDC_GET_KEY,
    IDC_AUTO_CONNECT,
    IDC_AUTO_RECONNECT,
    IDC_STATUS,
    IDC_VALUES,
    IDC_TEST
};

class OptionsWindow {
public:
    explicit OptionsWindow(HWND parent) : parent_(parent), original_(LoadConfig()), editing_(original_) {}

    bool Show() {
        WNDCLASSW window_class{};
        window_class.lpfnWndProc = WndProc;
        window_class.hInstance = g_module;
        window_class.lpszClassName = L"DJIPowerOptionsWindowV2";
        window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
        window_class.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
        RegisterClassW(&window_class);

        constexpr DWORD style = WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN;
        hwnd_ = CreateWindowExW(WS_EX_DLGMODALFRAME, window_class.lpszClassName,
            L"DJI Power 设置", style, CW_USEDEFAULT, CW_USEDEFAULT, 680, 550,
            parent_, nullptr, g_module, this);
        if (!hwnd_) return false;

        RECT rectangle{};
        GetWindowRect(hwnd_, &rectangle);
        const int width = rectangle.right - rectangle.left;
        const int height = rectangle.bottom - rectangle.top;
        SetWindowPos(hwnd_, nullptr,
            (GetSystemMetrics(SM_CXSCREEN) - width) / 2,
            (GetSystemMetrics(SM_CYSCREEN) - height) / 2,
            0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW);

        if (parent_) EnableWindow(parent_, FALSE);
        MSG message{};
        while (IsWindow(hwnd_) && GetMessageW(&message, nullptr, 0, 0) > 0) {
            if (!IsDialogMessageW(hwnd_, &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        if (parent_) {
            EnableWindow(parent_, TRUE);
            SetForegroundWindow(parent_);
        }
        return changed_;
    }

private:
    HWND parent_{};
    HWND hwnd_{};
    HWND device_{};
    HWND pair_{};
    HWND status_{};
    HWND values_{};
    HFONT font_{};
    HFONT title_font_{};
    HFONT bold_font_{};
    UINT dpi_{96};
    PluginConfig original_;
    PluginConfig editing_;
    std::vector<DiscoveredDevice> devices_;
    std::size_t known_device_count_{static_cast<std::size_t>(-1)};
    bool changed_{};
    bool accepted_{};

    int Scale(int value) const { return MulDiv(value, static_cast<int>(dpi_), 96); }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<OptionsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<OptionsWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hwnd_ = hwnd;
        }
        return self ? self->Handle(message, wp, lp) : DefWindowProcW(hwnd, message, wp, lp);
    }

    HWND Add(const wchar_t* type, const wchar_t* text, DWORD style,
             int x, int y, int width, int height, int id = 0, HFONT font = nullptr) {
        const auto control = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style,
            Scale(x), Scale(y), Scale(width), Scale(height), hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_module, nullptr);
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font ? font : font_), TRUE);
        SetWindowTheme(control, L"Explorer", nullptr);
        return control;
    }

    void CreateFonts() {
        // 显式使用 Segoe UI 和 ClearType，避免默认 SYSTEM_FONT 导致中文显示锯齿。
        const auto create = [&](int points, int weight) {
            return CreateFontW(-MulDiv(points, static_cast<int>(dpi_), 72), 0, 0, 0,
                weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                L"Segoe UI");
        };
        font_ = create(9, FW_NORMAL);
        bold_font_ = create(9, FW_SEMIBOLD);
        title_font_ = create(9, FW_SEMIBOLD);
    }

    void Build() {
        dpi_ = GetDpiForWindow(hwnd_);
        CreateFonts();

        // Windows 8.1 时期的桌面设置窗口强调紧凑、对齐和信息密度。
        RECT desired{0, 0, Scale(570), Scale(360)};
        AdjustWindowRectExForDpi(&desired, WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN,
            FALSE, WS_EX_DLGMODALFRAME, dpi_);
        SetWindowPos(hwnd_, nullptr, 0, 0, desired.right - desired.left,
            desired.bottom - desired.top, SWP_NOMOVE | SWP_NOZORDER);

        Add(L"BUTTON", L"连接方式", BS_GROUPBOX, 12, 10, 546, 50, 0, bold_font_);
        const auto ble_mode = Add(L"BUTTON", L"本地 BLE", BS_AUTORADIOBUTTON | WS_GROUP,
            28, 29, 110, 22, IDC_BLE_MODE);
        const auto cloud_mode = Add(L"BUTTON", L"DJI Cloud（备用，暂未启用）",
            BS_AUTORADIOBUTTON, 158, 29, 215, 22, IDC_CLOUD_MODE);
        SendMessageW(ble_mode, BM_SETCHECK, BST_CHECKED, 0);
        EnableWindow(cloud_mode, FALSE);

        Add(L"BUTTON", L"设备与凭据", BS_GROUPBOX, 12, 66, 546, 156, 0, bold_font_);
        Add(L"STATIC", L"设备：", SS_LEFT, 28, 96, 70, 20);
        device_ = Add(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL,
            105, 92, 335, 180, IDC_DEVICE);
        Add(L"BUTTON", L"重新扫描", BS_PUSHBUTTON | WS_TABSTOP, 449, 91, 95, 26, IDC_SCAN);

        Add(L"STATIC", L"Pair Key：", SS_LEFT, 28, 132, 70, 20);
        pair_ = Add(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
            105, 128, 335, 24, IDC_PAIR_KEY);
        SendMessageW(pair_, EM_SETPASSWORDCHAR, L'●', 0);
        Add(L"BUTTON", L"账号登录", BS_PUSHBUTTON | WS_TABSTOP, 449, 127, 95, 26, IDC_GET_KEY);

        Add(L"STATIC", L"登录信息和临时 Token 仅保存在内存中，获取完成后立即清除。",
            SS_LEFT, 105, 162, 439, 18, IDC_SUBTITLE);

        const auto auto_connect = Add(L"BUTTON", L"启动后自动连接",
            BS_AUTOCHECKBOX | WS_TABSTOP, 28, 190, 155, 22, IDC_AUTO_CONNECT);
        const auto auto_reconnect = Add(L"BUTTON", L"断线自动重连",
            BS_AUTOCHECKBOX | WS_TABSTOP, 205, 190, 150, 22, IDC_AUTO_RECONNECT);
        SendMessageW(auto_connect, BM_SETCHECK, editing_.auto_connect ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(auto_reconnect, BM_SETCHECK, editing_.auto_reconnect ? BST_CHECKED : BST_UNCHECKED, 0);

        Add(L"BUTTON", L"运行状态", BS_GROUPBOX, 12, 230, 546, 66, 0, bold_font_);
        status_ = Add(L"STATIC", L"正在启动扫描…", SS_LEFT, 28, 251, 510, 20, IDC_STATUS, bold_font_);
        values_ = Add(L"STATIC", L"输入  -- W      输出  -- W      净功率  -- W      电量  -- %",
            SS_LEFT, 28, 272, 510, 20, IDC_VALUES);

        Add(L"BUTTON", L"测试连接", BS_PUSHBUTTON | WS_TABSTOP,
            300, 313, 82, 27, IDC_TEST);
        Add(L"BUTTON", L"确定", BS_DEFPUSHBUTTON | WS_TABSTOP,
            390, 313, 76, 27, IDOK);
        Add(L"BUTTON", L"取消", BS_PUSHBUTTON | WS_TABSTOP,
            474, 313, 76, 27, IDCANCEL);

        SetWindowTextA(pair_, editing_.pair_key.c_str());
        RefreshDevices();
        SetTimer(hwnd_, 1, 400, nullptr);
    }

    void RefreshDevices() {
        const auto selected_address = editing_.bluetooth_address;
        devices_ = BleManager::Instance().Devices();
        known_device_count_ = devices_.size();
        SendMessageW(device_, CB_RESETCONTENT, 0, 0);
        if (devices_.empty()) {
            SendMessageW(device_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"正在扫描附近的 DJI Power…"));
            SendMessageW(device_, CB_SETCURSEL, 0, 0);
            return;
        }

        int selected = -1;
        for (std::size_t index = 0; index < devices_.size(); ++index) {
            wchar_t address[24]{};
            swprintf_s(address, L"  [%012llX]", static_cast<unsigned long long>(devices_[index].address));
            const auto label = devices_[index].name + address;
            SendMessageW(device_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
            if (devices_[index].address == selected_address) selected = static_cast<int>(index);
        }
        if (selected < 0) selected = 0;
        SendMessageW(device_, CB_SETCURSEL, selected, 0);
    }

    bool ReadForm(bool show_error) {
        std::array<char, 128> key{};
        GetWindowTextA(pair_, key.data(), static_cast<int>(key.size()));
        try {
            editing_.pair_key = duml::NormalizePairKey(key.data());
        } catch (...) {
            if (show_error) MessageBoxW(hwnd_, L"Pair Key 必须是 32 个十六进制字符。", L"DJI Power", MB_ICONWARNING);
            return false;
        }

        const auto index = static_cast<int>(SendMessageW(device_, CB_GETCURSEL, 0, 0));
        if (index < 0 || static_cast<std::size_t>(index) >= devices_.size()) {
            if (editing_.bluetooth_address == 0) {
                if (show_error) MessageBoxW(hwnd_, L"尚未发现设备。请保持电源开启并点击“重新扫描”。", L"DJI Power", MB_ICONWARNING);
                return false;
            }
        } else {
            editing_.bluetooth_address = devices_[index].address;
            editing_.device_name = devices_[index].name;
        }
        editing_.auto_connect = SendDlgItemMessageW(hwnd_, IDC_AUTO_CONNECT, BM_GETCHECK, 0, 0) == BST_CHECKED;
        editing_.auto_reconnect = SendDlgItemMessageW(hwnd_, IDC_AUTO_RECONNECT, BM_GETCHECK, 0, 0) == BST_CHECKED;
        return true;
    }

    void FetchKey() {
        // 使用与已验证 Python 工具相同的 DJI Home 移动端登录流程。
        const auto cloud_devices = ShowAccountLoginDialog(hwnd_);
        if (cloud_devices.empty()) return;
        SetWindowTextA(pair_, cloud_devices.front().pair_key.c_str());
        editing_.device_name = cloud_devices.front().name;
        const auto message = L"已取得 " + std::to_wstring(cloud_devices.size()) +
            L" 台设备的凭据，当前填入：" + cloud_devices.front().name;
        MessageBoxW(hwnd_, message.c_str(), L"获取 Key 成功", MB_ICONINFORMATION);
    }

    void UpdateStatus() {
        const auto latest_devices = BleManager::Instance().Devices();
        if (latest_devices.size() != known_device_count_) RefreshDevices();

        const auto value = BleManager::Instance().Snapshot();
        SetWindowTextW(status_, value.status.c_str());
        const auto field = [](int number) { return number < 0 ? std::wstring(L"--") : std::to_wstring(number); };
        const auto net = value.input_w < 0 || value.output_w < 0 ? std::wstring(L"--") : std::to_wstring(value.output_w - value.input_w);
        const auto text = L"输入  " + field(value.input_w) + L" W      输出  " +
            field(value.output_w) + L" W      净功率  " + net + L" W      电量  " +
            field(value.battery_percent) + L" %";
        SetWindowTextW(values_, text.c_str());
    }

    LRESULT Handle(UINT message, WPARAM wp, LPARAM lp) {
        switch (message) {
        case WM_CREATE:
            Build();
            return 0;
        case WM_TIMER:
            UpdateStatus();
            return 0;
        case WM_CTLCOLORSTATIC: {
            const auto dc = reinterpret_cast<HDC>(wp);
            SetBkMode(dc, TRANSPARENT);
            const auto control = reinterpret_cast<HWND>(lp);
            const int id = GetDlgCtrlID(control);
            SetTextColor(dc, id == IDC_SUBTITLE ? RGB(96, 96, 96) : GetSysColor(COLOR_WINDOWTEXT));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        }
        case WM_COMMAND:
            switch (LOWORD(wp)) {
            case IDC_SCAN:
                BleManager::Instance().Rescan();
                known_device_count_ = static_cast<std::size_t>(-1);
                RefreshDevices();
                return 0;
            case IDC_GET_KEY:
                FetchKey();
                return 0;
            case IDC_TEST:
                if (ReadForm(true)) {
                    auto test = editing_;
                    test.auto_connect = true;
                    BleManager::Instance().Reconfigure(test);
                    BleManager::Instance().ConnectNow();
                }
                return 0;
            case IDOK:
                if (ReadForm(true)) {
                    changed_ = SaveConfig(editing_);
                    accepted_ = true;
                    BleManager::Instance().Reconfigure(editing_);
                    DestroyWindow(hwnd_);
                }
                return 0;
            case IDCANCEL:
                BleManager::Instance().Reconfigure(original_);
                DestroyWindow(hwnd_);
                return 0;
            default:
                break;
            }
            break;
        case WM_CLOSE:
            if (!accepted_) BleManager::Instance().Reconfigure(original_);
            DestroyWindow(hwnd_);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd_, 1);
            DeleteObject(font_);
            DeleteObject(title_font_);
            DeleteObject(bold_font_);
            return 0;
        default:
            break;
        }
        return DefWindowProcW(hwnd_, message, wp, lp);
    }
};
} // namespace

bool ShowOptionsWindow(HWND parent) {
    OptionsWindow window(parent);
    return window.Show();
}
} // namespace dji_power

