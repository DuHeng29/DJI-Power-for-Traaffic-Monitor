// 本文件实现 Windows 8.1 风格的紧凑短信登录窗口，并在内存中显示 DJI 图片验证码。
#include "ui/sms_login_dialog.hpp"

#include <commctrl.h>
#include <wincodec.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <string>

extern HMODULE g_module;

namespace dji_power {
namespace {

enum : int {
    IDC_AREA_CODE = 300,
    IDC_PHONE,
    IDC_IMAGE_CODE,
    IDC_CAPTCHA_IMAGE,
    IDC_REFRESH_CAPTCHA,
    IDC_SMS_CODE,
    IDC_SEND_SMS,
    IDC_AGREE,
    IDC_LOGIN_STATUS,
    IDC_LOGIN,
    IDC_FETCH_KEY
};

constexpr UINT WM_DJI_INITIALIZE_LOGIN = WM_APP + 31;

HBITMAP DecodeCaptcha(const std::vector<std::uint8_t>& png, UINT dpi) {
    if (png.empty()) return nullptr;
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, png.size());
    if (!memory) return nullptr;
    void* destination = GlobalLock(memory);
    if (!destination) { GlobalFree(memory); return nullptr; }
    CopyMemory(destination, png.data(), png.size());
    GlobalUnlock(memory);

    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(memory, TRUE, &stream) != S_OK) { GlobalFree(memory); return nullptr; }
    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICBitmapScaler* scaler = nullptr;
    IWICFormatConverter* converter = nullptr;
    HBITMAP bitmap = nullptr;

    if (CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                         IID_PPV_ARGS(&factory)) == S_OK &&
        factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder) == S_OK &&
        decoder->GetFrame(0, &frame) == S_OK &&
        factory->CreateBitmapScaler(&scaler) == S_OK &&
        scaler->Initialize(frame, MulDiv(105, static_cast<int>(dpi), 96),
                           MulDiv(32, static_cast<int>(dpi), 96), WICBitmapInterpolationModeFant) == S_OK &&
        factory->CreateFormatConverter(&converter) == S_OK &&
        converter->Initialize(scaler, GUID_WICPixelFormat32bppPBGRA,
                              WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom) == S_OK) {
        const int width = MulDiv(105, static_cast<int>(dpi), 96);
        const int height = MulDiv(32, static_cast<int>(dpi), 96);
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
    stream->Release(); // 同时释放由流拥有的 HGLOBAL。
    return bitmap;
}

bool DigitsOnly(std::wstring_view value, std::size_t minimum, std::size_t maximum) {
    return value.size() >= minimum && value.size() <= maximum &&
           std::all_of(value.begin(), value.end(), [](wchar_t c) { return std::iswdigit(c) != 0; });
}

class SmsLoginWindow {
public:
    explicit SmsLoginWindow(HWND parent) : parent_(parent) {}

    std::vector<CloudDevice> Show() {
        const HRESULT com_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        uninitialize_com_ = SUCCEEDED(com_result);

        WNDCLASSW window_class{};
        window_class.lpfnWndProc = WndProc;
        window_class.hInstance = g_module;
        window_class.lpszClassName = L"DJIPowerSmsLoginWindowV1";
        window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
        window_class.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
        RegisterClassW(&window_class);

        hwnd_ = CreateWindowExW(WS_EX_DLGMODALFRAME, window_class.lpszClassName,
            L"DJI 账号短信登录", WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN,
            CW_USEDEFAULT, CW_USEDEFAULT, 480, 300, parent_, nullptr, g_module, this);
        if (!hwnd_) {
            if (uninitialize_com_) CoUninitialize();
            return {};
        }
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
    HWND area_{};
    HWND phone_{};
    HWND image_code_{};
    HWND captcha_image_{};
    HWND sms_code_{};
    HWND send_sms_{};
    HWND status_{};
    HWND login_button_{};
    HWND fetch_button_{};
    HFONT font_{};
    HFONT bold_font_{};
    HBITMAP captcha_bitmap_{};
    UINT dpi_{96};
    int countdown_{};
    bool sms_sent_{};
    bool uninitialize_com_{};
    SmsLoginSession login_;
    std::vector<CloudDevice> devices_;

    int Scale(int value) const { return MulDiv(value, static_cast<int>(dpi_), 96); }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<SmsLoginWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<SmsLoginWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
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
        return control;
    }

    void CenterAndShow() {
        RECT rectangle{};
        GetWindowRect(hwnd_, &rectangle);
        RECT owner{};
        if (!parent_ || !GetWindowRect(parent_, &owner)) {
            owner = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
        }
        SetWindowPos(hwnd_, nullptr,
            owner.left + ((owner.right - owner.left) - (rectangle.right - rectangle.left)) / 2,
            owner.top + ((owner.bottom - owner.top) - (rectangle.bottom - rectangle.top)) / 2,
            0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW);
    }

    void Build() {
        dpi_ = GetDpiForWindow(hwnd_);
        const auto create_font = [&](int weight) {
            return CreateFontW(-MulDiv(9, static_cast<int>(dpi_), 72), 0, 0, 0, weight,
                FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        };
        font_ = create_font(FW_NORMAL);
        bold_font_ = create_font(FW_SEMIBOLD);

        RECT desired{0, 0, Scale(470), Scale(265)};
        AdjustWindowRectExForDpi(&desired, WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN,
                                 FALSE, WS_EX_DLGMODALFRAME, dpi_);
        SetWindowPos(hwnd_, nullptr, 0, 0, desired.right - desired.left, desired.bottom - desired.top,
                     SWP_NOMOVE | SWP_NOZORDER);

        Add(L"BUTTON", L"账号验证", BS_GROUPBOX, 12, 10, 446, 188, 0, bold_font_);
        Add(L"STATIC", L"手机：", SS_LEFT, 28, 38, 58, 20);
        Add(L"STATIC", L"+", SS_LEFT, 89, 39, 12, 20);
        area_ = Add(L"EDIT", L"86", WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP,
                    100, 35, 45, 24, IDC_AREA_CODE);
        phone_ = Add(L"EDIT", L"", WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP,
                     153, 35, 287, 24, IDC_PHONE);

        Add(L"STATIC", L"图片码：", SS_LEFT, 28, 75, 58, 20);
        image_code_ = Add(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                          89, 71, 105, 24, IDC_IMAGE_CODE);
        captcha_image_ = Add(L"STATIC", L"", SS_BITMAP | SS_CENTERIMAGE | WS_BORDER,
                             204, 67, 107, 34, IDC_CAPTCHA_IMAGE);
        Add(L"BUTTON", L"换一张", BS_PUSHBUTTON | WS_TABSTOP,
            322, 70, 76, 26, IDC_REFRESH_CAPTCHA);

        Add(L"STATIC", L"短信码：", SS_LEFT, 28, 113, 58, 20);
        sms_code_ = Add(L"EDIT", L"", WS_BORDER | ES_NUMBER | ES_PASSWORD | ES_AUTOHSCROLL | WS_TABSTOP,
                        89, 109, 222, 24, IDC_SMS_CODE);
        send_sms_ = Add(L"BUTTON", L"发送验证码", BS_PUSHBUTTON | WS_TABSTOP,
                        322, 108, 118, 26, IDC_SEND_SMS);

        const auto agree = Add(L"BUTTON", L"我已阅读并同意 DJI 账号条款；未注册手机号可能自动创建账号",
                               BS_AUTOCHECKBOX | WS_TABSTOP, 28, 145, 412, 22, IDC_AGREE);
        SendMessageW(agree, BM_SETCHECK, BST_UNCHECKED, 0);
        status_ = Add(L"STATIC", L"正在加载 DJI 验证码…", SS_LEFT,
                      28, 170, 410, 20, IDC_LOGIN_STATUS);

        login_button_ = Add(L"BUTTON", L"登录", BS_DEFPUSHBUTTON | WS_TABSTOP,
                            194, 216, 82, 28, IDC_LOGIN);
        fetch_button_ = Add(L"BUTTON", L"获取 Key", BS_PUSHBUTTON | WS_TABSTOP,
                            286, 216, 82, 28, IDC_FETCH_KEY);
        Add(L"BUTTON", L"取消", BS_PUSHBUTTON | WS_TABSTOP,
            378, 216, 80, 28, IDCANCEL);
        EnableWindow(send_sms_, FALSE);
        EnableWindow(login_button_, FALSE);
        EnableWindow(fetch_button_, FALSE);
        PostMessageW(hwnd_, WM_DJI_INITIALIZE_LOGIN, 0, 0);
    }

    void SetStatus(const std::wstring& text) {
        SetWindowTextW(status_, text.c_str());
        UpdateWindow(hwnd_);
    }

    void ShowCaptcha(const std::vector<std::uint8_t>& png) {
        const auto bitmap = DecodeCaptcha(png, dpi_);
        if (!bitmap) return;
        const auto old = reinterpret_cast<HBITMAP>(SendMessageW(captcha_image_, STM_SETIMAGE,
                                                                 IMAGE_BITMAP, reinterpret_cast<LPARAM>(bitmap)));
        if (old) DeleteObject(old);
        captcha_bitmap_ = bitmap;
        InvalidateRect(captcha_image_, nullptr, TRUE);
    }

    void LoadSession() {
        std::vector<std::uint8_t> png;
        std::wstring error;
        if (!DefaultPairKeyProvider().BeginSmsLogin(login_, png, error)) {
            SetStatus(error);
            MessageBoxW(hwnd_, error.c_str(), L"DJI 登录初始化失败", MB_ICONERROR);
            return;
        }
        ShowCaptcha(png);
        EnableWindow(send_sms_, TRUE);
        SetStatus(L"请输入手机号和图片验证码。");
        SetFocus(phone_);
    }

    void RefreshCaptcha() {
        SetStatus(L"正在刷新图片验证码…");
        std::vector<std::uint8_t> png;
        std::wstring error;
        if (DefaultPairKeyProvider().RefreshCaptcha(login_, png, error)) {
            ShowCaptcha(png);
            SetWindowTextW(image_code_, L"");
            SetStatus(L"图片验证码已刷新。");
        } else {
            SetStatus(error);
        }
    }

    bool ReadAccount(std::wstring& area, std::wstring& phone, std::wstring& image_code) {
        std::array<wchar_t, 32> area_buffer{};
        std::array<wchar_t, 64> phone_buffer{};
        std::array<wchar_t, 32> image_buffer{};
        GetWindowTextW(area_, area_buffer.data(), static_cast<int>(area_buffer.size()));
        GetWindowTextW(phone_, phone_buffer.data(), static_cast<int>(phone_buffer.size()));
        GetWindowTextW(image_code_, image_buffer.data(), static_cast<int>(image_buffer.size()));
        area = area_buffer.data();
        phone = phone_buffer.data();
        image_code = image_buffer.data();
        SecureZeroMemory(phone_buffer.data(), phone_buffer.size() * sizeof(wchar_t));
        SecureZeroMemory(image_buffer.data(), image_buffer.size() * sizeof(wchar_t));
        if (!DigitsOnly(area, 1, 4) || !DigitsOnly(phone, 5, 20)) {
            MessageBoxW(hwnd_, L"请输入正确的国家/地区代码和手机号。", L"DJI 账号", MB_ICONWARNING);
            return false;
        }
        if (image_code.empty()) {
            MessageBoxW(hwnd_, L"请输入图片中的验证码。", L"DJI 账号", MB_ICONWARNING);
            return false;
        }
        return true;
    }

    void SendSms() {
        std::wstring area, phone, image_code;
        if (!ReadAccount(area, phone, image_code)) return;
        SetStatus(L"正在校验图片并发送短信…");
        EnableWindow(send_sms_, FALSE);
        std::wstring error;
        const bool sent = DefaultPairKeyProvider().SendSmsCode(login_, area, phone, image_code, error);
        std::fill(phone.begin(), phone.end(), L'\0');
        std::fill(image_code.begin(), image_code.end(), L'\0');
        if (!sent) {
            SetStatus(error);
            MessageBoxW(hwnd_, error.c_str(), L"发送验证码失败", MB_ICONERROR);
            EnableWindow(send_sms_, TRUE);
            RefreshCaptcha();
            return;
        }
        sms_sent_ = true;
        countdown_ = 60;
        SetTimer(hwnd_, 2, 1000, nullptr);
        EnableWindow(login_button_, TRUE);
        SetStatus(L"短信已发送，请输入 6 位验证码。");
        SetFocus(sms_code_);
    }

    void Login() {
        if (!sms_sent_) return;
        if (SendDlgItemMessageW(hwnd_, IDC_AGREE, BM_GETCHECK, 0, 0) != BST_CHECKED) {
            MessageBoxW(hwnd_, L"请先确认已阅读并同意 DJI 账号条款。", L"DJI 账号", MB_ICONWARNING);
            return;
        }
        std::array<wchar_t, 32> area_buffer{};
        std::array<wchar_t, 64> phone_buffer{};
        std::array<wchar_t, 16> sms_buffer{};
        GetWindowTextW(area_, area_buffer.data(), static_cast<int>(area_buffer.size()));
        GetWindowTextW(phone_, phone_buffer.data(), static_cast<int>(phone_buffer.size()));
        GetWindowTextW(sms_code_, sms_buffer.data(), static_cast<int>(sms_buffer.size()));
        std::wstring area = area_buffer.data();
        std::wstring phone = phone_buffer.data();
        std::wstring sms = sms_buffer.data();
        SecureZeroMemory(phone_buffer.data(), phone_buffer.size() * sizeof(wchar_t));
        SecureZeroMemory(sms_buffer.data(), sms_buffer.size() * sizeof(wchar_t));
        if (!DigitsOnly(sms, 6, 6)) {
            MessageBoxW(hwnd_, L"短信验证码应为 6 位数字。", L"DJI 账号", MB_ICONWARNING);
            return;
        }
        EnableWindow(login_button_, FALSE);
        SetStatus(L"正在验证 DJI 账号…");
        std::wstring error;
        const bool success = DefaultPairKeyProvider().CompleteSmsLogin(login_, area, phone, sms, error);
        std::fill(phone.begin(), phone.end(), L'\0');
        std::fill(sms.begin(), sms.end(), L'\0');
        SetWindowTextW(phone_, L"");
        SetWindowTextW(sms_code_, L"");
        if (!success) {
            SetStatus(error);
            MessageBoxW(hwnd_, error.c_str(), L"登录失败", MB_ICONERROR);
            EnableWindow(login_button_, TRUE);
            return;
        }
        KillTimer(hwnd_, 2);
        SetStatus(L"登录成功；现在可以单独尝试获取设备 Key。");
        EnableWindow(area_, FALSE);
        EnableWindow(phone_, FALSE);
        EnableWindow(image_code_, FALSE);
        EnableWindow(captcha_image_, FALSE);
        EnableWindow(sms_code_, FALSE);
        EnableWindow(send_sms_, FALSE);
        EnableWindow(GetDlgItem(hwnd_, IDC_REFRESH_CAPTCHA), FALSE);
        EnableWindow(GetDlgItem(hwnd_, IDC_AGREE), FALSE);
        EnableWindow(fetch_button_, TRUE);
        MessageBoxW(hwnd_,
            L"DJI 账号短信登录成功，并已取得网页回调票据。\n\n"
            L"点击“获取 Key”后才会继续完成网页登录回调并查询设备。",
            L"登录成功", MB_ICONINFORMATION);
    }

    void FetchKey() {
        EnableWindow(fetch_button_, FALSE);
        SetStatus(L"正在完成登录回调并查询设备 Key…");
        std::wstring error;
        devices_ = DefaultPairKeyProvider().FetchAfterWebLogin(login_, error);
        if (devices_.empty()) {
            SetStatus(error);
            MessageBoxW(hwnd_, error.c_str(), L"获取 Key 失败", MB_ICONERROR);
            EnableWindow(fetch_button_, TRUE);
            return;
        }
        SetStatus(L"设备 Key 获取成功。");
        DestroyWindow(hwnd_);
    }

    void UpdateCountdown() {
        if (countdown_ > 0) --countdown_;
        if (countdown_ == 0) {
            KillTimer(hwnd_, 2);
            SetWindowTextW(send_sms_, L"重新发送");
            EnableWindow(send_sms_, TRUE);
        } else {
            const auto text = std::to_wstring(countdown_) + L" 秒后重发";
            SetWindowTextW(send_sms_, text.c_str());
        }
    }

    LRESULT Handle(UINT message, WPARAM wp, LPARAM lp) {
        switch (message) {
        case WM_CREATE: Build(); return 0;
        case WM_DJI_INITIALIZE_LOGIN: LoadSession(); return 0;
        case WM_TIMER: if (wp == 2) UpdateCountdown(); return 0;
        case WM_COMMAND:
            switch (LOWORD(wp)) {
            case IDC_REFRESH_CAPTCHA: RefreshCaptcha(); return 0;
            case IDC_SEND_SMS: SendSms(); return 0;
            case IDC_LOGIN: Login(); return 0;
            case IDCANCEL: DestroyWindow(hwnd_); return 0;
            case IDC_FETCH_KEY: FetchKey(); return 0;
            default: break;
            }
            break;
        case WM_CLOSE: DestroyWindow(hwnd_); return 0;
        case WM_DESTROY:
            KillTimer(hwnd_, 2);
            login_.Clear();
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

std::vector<CloudDevice> ShowSmsLoginDialog(HWND parent) {
    SmsLoginWindow window(parent);
    return window.Show();
}
} // namespace dji_power
