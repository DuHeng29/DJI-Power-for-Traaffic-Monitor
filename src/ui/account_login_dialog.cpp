// 本文件实现 Windows 8.1 风格的 DJI Home 账号登录及 Pair Key 获取窗口。
#include "ui/account_login_dialog.hpp"

#include <commctrl.h>
#include <uxtheme.h>
#include <wincodec.h>

#include <array>
#include <string>

extern HMODULE g_module;

namespace dji_power {
namespace {
enum : int {
    IDC_ACCOUNT = 400,
    IDC_PASSWORD,
    IDC_IMAGE_CODE,
    IDC_CAPTCHA_IMAGE,
    IDC_REFRESH_CAPTCHA,
    IDC_VERIFY_CODE,
    IDC_STATUS,
    IDC_DEVICE_SELECT,
    IDC_LOGIN
};

constexpr UINT WM_DJI_INITIALIZE_ACCOUNT = WM_APP + 41;

HBITMAP DecodeCaptcha(const std::vector<std::uint8_t>& png, UINT dpi) {
    if (png.empty()) return nullptr;
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, png.size());
    if (!memory) return nullptr;
    void* destination = GlobalLock(memory);
    if (!destination) { GlobalFree(memory); return nullptr; }
    CopyMemory(destination, png.data(), png.size());
    GlobalUnlock(memory);

    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(memory, TRUE, &stream) != S_OK) {
        GlobalFree(memory);
        return nullptr;
    }
    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICBitmapScaler* scaler = nullptr;
    IWICFormatConverter* converter = nullptr;
    HBITMAP bitmap = nullptr;
    const int width = MulDiv(105, static_cast<int>(dpi), 96);
    const int height = MulDiv(32, static_cast<int>(dpi), 96);

    if (CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                         IID_PPV_ARGS(&factory)) == S_OK &&
        factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad,
                                         &decoder) == S_OK &&
        decoder->GetFrame(0, &frame) == S_OK &&
        factory->CreateBitmapScaler(&scaler) == S_OK &&
        scaler->Initialize(frame, width, height, WICBitmapInterpolationModeFant) == S_OK &&
        factory->CreateFormatConverter(&converter) == S_OK &&
        converter->Initialize(scaler, GUID_WICPixelFormat32bppPBGRA,
                              WICBitmapDitherTypeNone, nullptr, 0.0,
                              WICBitmapPaletteTypeCustom) == S_OK) {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* pixels = nullptr;
        bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!bitmap || converter->CopyPixels(nullptr, static_cast<UINT>(width * 4),
                                             static_cast<UINT>(width * height * 4),
                                             static_cast<BYTE*>(pixels)) != S_OK) {
            if (bitmap) DeleteObject(bitmap);
            bitmap = nullptr;
        }
    }
    if (converter) converter->Release();
    if (scaler) scaler->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (factory) factory->Release();
    stream->Release();
    return bitmap;
}

class AccountLoginWindow {
public:
    explicit AccountLoginWindow(HWND parent) : parent_(parent) {}

    std::vector<CloudDevice> Show() {
        const HRESULT com_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        uninitialize_com_ = SUCCEEDED(com_result);
        WNDCLASSW window_class{};
        window_class.lpfnWndProc = WndProc;
        window_class.hInstance = g_module;
        window_class.lpszClassName = L"DJIPowerAccountLoginWindowV2";
        window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
        window_class.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
        RegisterClassW(&window_class);

        hwnd_ = CreateWindowExW(WS_EX_DLGMODALFRAME, window_class.lpszClassName,
            L"DJI Home 账号登录", WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN,
            CW_USEDEFAULT, CW_USEDEFAULT, 480, 275, parent_, nullptr, g_module, this);
        if (!hwnd_) return {};
        CenterAndShow();
        if (parent_) EnableWindow(parent_, FALSE);
        MSG message{};
        while (IsWindow(hwnd_) && GetMessageW(&message, nullptr, 0, 0) > 0) {
            if (!IsDialogMessageW(hwnd_, &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        if (parent_) { EnableWindow(parent_, TRUE); SetForegroundWindow(parent_); }
        if (uninitialize_com_) CoUninitialize();
        return std::move(devices_);
    }

private:
    HWND parent_{};
    HWND hwnd_{};
    HWND account_{};
    HWND password_{};
    HWND image_code_{};
    HWND captcha_image_{};
    HWND verify_code_{};
    HWND status_caption_{};
    HWND status_{};
    HWND device_select_{};
    HWND login_button_{};
    HFONT font_{};
    HFONT bold_font_{};
    HBITMAP captcha_bitmap_{};
    UINT dpi_{96};
    bool uninitialize_com_{};
    bool selecting_device_{};
    AccountLoginSession login_;
    std::vector<CloudDevice> devices_;

    int Scale(int value) const { return MulDiv(value, static_cast<int>(dpi_), 96); }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<AccountLoginWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<AccountLoginWindow*>(
                reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
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

    void CenterAndShow() {
        RECT window{};
        GetWindowRect(hwnd_, &window);
        RECT owner{};
        if (!parent_ || !GetWindowRect(parent_, &owner)) {
            owner = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
        }
        SetWindowPos(hwnd_, nullptr,
            owner.left + ((owner.right - owner.left) - (window.right - window.left)) / 2,
            owner.top + ((owner.bottom - owner.top) - (window.bottom - window.top)) / 2,
            0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW);
    }

    void Build() {
        dpi_ = GetDpiForWindow(hwnd_);
        const auto make_font = [&](int weight) {
            return CreateFontW(-MulDiv(9, static_cast<int>(dpi_), 72), 0, 0, 0, weight,
                FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        };
        font_ = make_font(FW_NORMAL);
        bold_font_ = make_font(FW_SEMIBOLD);

        RECT desired{0, 0, Scale(570), Scale(262)};
        AdjustWindowRectExForDpi(&desired, WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN,
                                 FALSE, WS_EX_DLGMODALFRAME, dpi_);
        SetWindowPos(hwnd_, nullptr, 0, 0, desired.right - desired.left,
                     desired.bottom - desired.top, SWP_NOMOVE | SWP_NOZORDER);

        // 与主设置页使用同一列网格和控件尺寸，保持紧凑的 Windows 8.1 桌面风格。
        Add(L"BUTTON", L"DJI Home 账号", BS_GROUPBOX, 12, 10, 546, 192, 0, bold_font_);
        Add(L"STATIC", L"账号：", SS_LEFT, 28, 38, 70, 20);
        account_ = Add(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                       105, 35, 335, 24, IDC_ACCOUNT);
        Add(L"STATIC", L"密码：", SS_LEFT, 28, 72, 70, 20);
        password_ = Add(L"EDIT", L"", WS_BORDER | ES_PASSWORD | ES_AUTOHSCROLL | WS_TABSTOP,
                        105, 69, 335, 24, IDC_PASSWORD);

        Add(L"STATIC", L"图片码：", SS_LEFT, 28, 106, 70, 20);
        image_code_ = Add(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                          105, 103, 104, 24, IDC_IMAGE_CODE);
        captcha_image_ = Add(L"STATIC", L"", SS_BITMAP | SS_CENTERIMAGE | WS_BORDER,
                             218, 99, 107, 34, IDC_CAPTCHA_IMAGE);
        Add(L"BUTTON", L"换一张", BS_PUSHBUTTON | WS_TABSTOP,
            449, 102, 95, 26, IDC_REFRESH_CAPTCHA);

        Add(L"STATIC", L"二次码：", SS_LEFT, 28, 143, 70, 20);
        verify_code_ = Add(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                           105, 139, 104, 24, IDC_VERIFY_CODE);
        Add(L"STATIC", L"仅账号要求二次验证时填写", SS_LEFT,
            218, 143, 222, 20);
        status_caption_ = Add(L"STATIC", L"状态：", SS_LEFT, 28, 174, 70, 20);
        status_ = Add(L"STATIC", L"正在加载 DJI Home 验证码…", SS_LEFT,
                      105, 174, 439, 20, IDC_STATUS);
        device_select_ = Add(WC_COMBOBOXW, L"",
            CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL,
            105, 168, 335, 120, IDC_DEVICE_SELECT);
        ShowWindow(device_select_, SW_HIDE);

        login_button_ = Add(L"BUTTON", L"登录并获取 Key",
                            BS_DEFPUSHBUTTON | WS_TABSTOP,
                            340, 218, 126, 27, IDC_LOGIN);
        Add(L"BUTTON", L"取消", BS_PUSHBUTTON | WS_TABSTOP,
            474, 218, 76, 27, IDCANCEL);
        EnableWindow(login_button_, FALSE);
        PostMessageW(hwnd_, WM_DJI_INITIALIZE_ACCOUNT, 0, 0);
    }

    void SetStatus(const std::wstring& text) {
        SetWindowTextW(status_, text.c_str());
        UpdateWindow(hwnd_);
    }

    void ShowCaptcha(const std::vector<std::uint8_t>& png) {
        const auto bitmap = DecodeCaptcha(png, dpi_);
        if (!bitmap) return;
        const auto old = reinterpret_cast<HBITMAP>(SendMessageW(
            captcha_image_, STM_SETIMAGE, IMAGE_BITMAP, reinterpret_cast<LPARAM>(bitmap)));
        if (old) DeleteObject(old);
        captcha_bitmap_ = bitmap;
        InvalidateRect(captcha_image_, nullptr, TRUE);
    }

    void LoadCaptcha(bool reset_session) {
        SetStatus(L"正在加载 DJI Home 验证码…");
        std::vector<std::uint8_t> png;
        std::wstring error;
        const bool loaded = reset_session
            ? DefaultPairKeyProvider().BeginAccountLogin(login_, png, error)
            : DefaultPairKeyProvider().RefreshAccountCaptcha(login_, png, error);
        if (!loaded) {
            SetStatus(error);
            MessageBoxW(hwnd_, error.c_str(), L"DJI Home 登录初始化失败", MB_ICONERROR);
            return;
        }
        ShowCaptcha(png);
        SetWindowTextW(image_code_, L"");
        EnableWindow(login_button_, TRUE);
        SetStatus(L"请输入 DJI 账号、密码和图片验证码。");
        SetFocus(account_);
    }

    static std::wstring ReadText(HWND control, std::size_t capacity) {
        std::wstring value(capacity, L'\0');
        const int size = GetWindowTextW(control, value.data(), static_cast<int>(capacity));
        value.resize(size > 0 ? static_cast<std::size_t>(size) : 0);
        return value;
    }

    void Login() {
        if (selecting_device_) {
            const auto selected = static_cast<int>(
                SendMessageW(device_select_, CB_GETCURSEL, 0, 0));
            if (selected < 0 || static_cast<std::size_t>(selected) >= devices_.size()) {
                MessageBoxW(hwnd_, L"请选择要绑定的 DJI Power。", L"选择设备", MB_ICONWARNING);
                return;
            }
            auto chosen = std::move(devices_[static_cast<std::size_t>(selected)]);
            devices_.clear();
            devices_.push_back(std::move(chosen));
            DestroyWindow(hwnd_);
            return;
        }

        auto account = ReadText(account_, 256);
        auto password = ReadText(password_, 256);
        auto image_code = ReadText(image_code_, 64);
        auto verification = ReadText(verify_code_, 64);
        if (account.empty() || password.empty() || image_code.empty()) {
            MessageBoxW(hwnd_, L"请输入账号、密码和图片验证码。", L"DJI Home", MB_ICONWARNING);
            return;
        }
        EnableWindow(login_button_, FALSE);
        SetStatus(L"正在登录 DJI Home 并查询绑定设备…");
        std::wstring error;
        devices_ = DefaultPairKeyProvider().LoginAndFetch(
            login_, account, password, image_code, verification, error);

        // 输入框与临时副本都属于敏感数据，用完立即覆盖。
        if (!password.empty()) SecureZeroMemory(password.data(), password.size() * sizeof(wchar_t));
        if (!verification.empty()) SecureZeroMemory(verification.data(), verification.size() * sizeof(wchar_t));
        if (devices_.empty()) {
            SetStatus(error);
            MessageBoxW(hwnd_, error.c_str(), L"获取 Key 失败", MB_ICONERROR);
            EnableWindow(login_button_, TRUE);
            return;
        }
        SetWindowTextW(password_, L"");
        SetWindowTextW(verify_code_, L"");
        SetWindowTextW(image_code_, L"");
        if (devices_.size() == 1) {
            SetStatus(L"设备 Pair Key 获取成功。");
            DestroyWindow(hwnd_);
            return;
        }

        // 一个账号可能绑定多台电源，必须由用户明确选择，不能静默使用第一项。
        SendMessageW(device_select_, CB_RESETCONTENT, 0, 0);
        for (const auto& device : devices_) {
            auto label = device.name;
            if (!device.serial_number.empty()) label += L"  [" + device.serial_number + L"]";
            SendMessageW(device_select_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }
        SendMessageW(device_select_, CB_SETCURSEL, 0, 0);
        selecting_device_ = true;
        SetWindowTextW(status_caption_, L"设备：");
        ShowWindow(status_, SW_HIDE);
        ShowWindow(device_select_, SW_SHOW);
        SetWindowTextW(login_button_, L"使用所选 Key");
        EnableWindow(login_button_, TRUE);
        SetFocus(device_select_);
    }

    LRESULT Handle(UINT message, WPARAM wp, LPARAM lp) {
        switch (message) {
        case WM_CREATE: Build(); return 0;
        case WM_DJI_INITIALIZE_ACCOUNT: LoadCaptcha(true); return 0;
        case WM_CTLCOLORSTATIC: {
            const auto dc = reinterpret_cast<HDC>(wp);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        }
        case WM_COMMAND:
            switch (LOWORD(wp)) {
            case IDC_REFRESH_CAPTCHA: LoadCaptcha(false); return 0;
            case IDC_LOGIN: Login(); return 0;
            case IDCANCEL: DestroyWindow(hwnd_); return 0;
            default: break;
            }
            break;
        case WM_CLOSE: DestroyWindow(hwnd_); return 0;
        case WM_DESTROY:
            login_.Clear();
            SetWindowTextW(password_, L"");
            SetWindowTextW(verify_code_, L"");
            if (captcha_bitmap_) DeleteObject(captcha_bitmap_);
            DeleteObject(font_);
            DeleteObject(bold_font_);
            return 0;
        default: break;
        }
        return DefWindowProcW(hwnd_, message, wp, lp);
    }
};
} // namespace

std::vector<CloudDevice> ShowAccountLoginDialog(HWND parent) {
    AccountLoginWindow window(parent);
    return window.Show();
}
} // namespace dji_power
