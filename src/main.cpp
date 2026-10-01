#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <cwctype>
#include <string>
#include <vector>
#include "MazeLogic.h"
#include "MazeRenderer.h"
#include "resource.h"

#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

using Microsoft::WRL::ComPtr;
namespace {
constexpr wchar_t regPath[] = L"Software\\Microsoft\\Screensavers\\3DMaze";
struct Settings { DWORD mazeSize=16, speed=3, quality=2, showMap=1, fullScreen=1; };
DWORD readValue(HKEY key,const wchar_t* name,DWORD fallback) {
    DWORD result=fallback,bytes=sizeof(result),type=0;
    return RegQueryValueExW(key,name,nullptr,&type,reinterpret_cast<BYTE*>(&result),&bytes)==ERROR_SUCCESS &&
           type==REG_DWORD && bytes==sizeof(result) ? result : fallback;
}
void saveSettings(const Settings& s) {
    HKEY key{};
    if (RegCreateKeyExW(HKEY_CURRENT_USER,regPath,0,nullptr,0,KEY_WRITE,nullptr,&key,nullptr)!=ERROR_SUCCESS) return;
    const struct {const wchar_t* name;DWORD value;} values[]={{L"MazeSize",s.mazeSize},{L"Speed",s.speed},
        {L"Quality",s.quality},{L"ShowMap",s.showMap},{L"FullScreen",s.fullScreen}};
    for (auto& item:values) RegSetValueExW(key,item.name,0,REG_DWORD,
        reinterpret_cast<const BYTE*>(&item.value),sizeof(DWORD));
    RegCloseKey(key);
}
Settings loadSettings() {
    Settings s; HKEY key{};
    if (RegOpenKeyExW(HKEY_CURRENT_USER,regPath,0,KEY_READ,&key)==ERROR_SUCCESS) {
        s.mazeSize=std::clamp(readValue(key,L"MazeSize",16),DWORD(8),DWORD(32));
        s.speed=std::clamp(readValue(key,L"Speed",3),DWORD(1),DWORD(10));
        s.quality=std::min(readValue(key,L"Quality",2),DWORD(2));
        s.showMap=readValue(key,L"ShowMap",1)?1:0;
        s.fullScreen=readValue(key,L"FullScreen",1)?1:0;
        RegCloseKey(key);
    }
    saveSettings(s); return s;
}

std::vector<uint8_t> resourceData(int id) {
    HRSRC resource=FindResourceW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(id),RT_RCDATA);
    if (!resource) return {};
    HGLOBAL loaded=LoadResource(GetModuleHandleW(nullptr),resource);
    const auto* bytes=static_cast<const uint8_t*>(LockResource(loaded));
    return bytes ? std::vector<uint8_t>(bytes,bytes+SizeofResource(GetModuleHandleW(nullptr),resource))
                 : std::vector<uint8_t>{};
}
HBITMAP thumbnail(int id) {
    auto bytes=resourceData(id);
    if (bytes.empty()) return nullptr;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICBitmapScaler> scaler;
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory2,nullptr,CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromMemory(bytes.data(),UINT(bytes.size()))) ||
        FAILED(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder)) ||
        FAILED(decoder->GetFrame(0,&frame)) ||
        FAILED(factory->CreateBitmapScaler(&scaler)) ||
        FAILED(scaler->Initialize(frame.Get(),64,32,WICBitmapInterpolationModeFant)) ||
        FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(scaler.Get(),GUID_WICPixelFormat32bppBGR,
                                     WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom))) return nullptr;
    BITMAPINFO info{};
    info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=64; info.bmiHeader.biHeight=-32;
    info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
    info.bmiHeader.biCompression=BI_RGB;
    HDC dc=GetDC(nullptr); void* pixels=nullptr;
    HBITMAP image=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    ReleaseDC(nullptr,dc);
    if (!image) return nullptr;
    if (FAILED(converter->CopyPixels(nullptr,64*4,64*32*4,static_cast<BYTE*>(pixels)))) {
        DeleteObject(image); return nullptr;
    }
    return image;
}
struct ConfigState { Settings values; HBITMAP previews[3]{}; };
INT_PTR CALLBACK configProc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    auto* state=reinterpret_cast<ConfigState*>(GetWindowLongPtrW(window,DWLP_USER));
    switch (message) {
    case WM_INITDIALOG: {
        state=reinterpret_cast<ConfigState*>(lp);
        SetWindowLongPtrW(window,DWLP_USER,reinterpret_cast<LONG_PTR>(state));
        const int previewIds[]={IDC_WALL_PREVIEW,IDC_FLOOR_PREVIEW,IDC_CEILING_PREVIEW};
        for (int i=0;i<3;++i) {
            state->previews[i]=thumbnail(IDR_WALL+i);
            SendMessageW(GetDlgItem(window,previewIds[i]),STM_SETIMAGE,IMAGE_BITMAP,
                         reinterpret_cast<LPARAM>(state->previews[i]));
        }
        CheckDlgButton(window,IDC_SHOW_MAP,state->values.showMap?BST_CHECKED:BST_UNCHECKED);
        CheckDlgButton(window,IDC_FULL_SCREEN,state->values.fullScreen?BST_CHECKED:BST_UNCHECKED);
        HWND combo=GetDlgItem(window,IDC_QUALITY);
        for (const wchar_t* label:{L"低（点采样）",L"中（双线性）",L"高（各向异性）"})
            SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
        SendMessageW(combo,CB_SETCURSEL,state->values.quality,0);
        HWND size=GetDlgItem(window,IDC_MAZE_SIZE);
        SendMessageW(size,TBM_SETRANGE,TRUE,MAKELONG(8,32));
        SendMessageW(size,TBM_SETPOS,TRUE,state->values.mazeSize);
        HWND speed=GetDlgItem(window,IDC_SPEED);
        SendMessageW(speed,TBM_SETRANGE,TRUE,MAKELONG(1,10));
        SendMessageW(speed,TBM_SETPOS,TRUE,state->values.speed);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wp)==IDOK) {
            state->values.mazeSize=DWORD(SendMessageW(GetDlgItem(window,IDC_MAZE_SIZE),TBM_GETPOS,0,0));
            state->values.speed=DWORD(SendMessageW(GetDlgItem(window,IDC_SPEED),TBM_GETPOS,0,0));
            state->values.quality=DWORD(SendMessageW(GetDlgItem(window,IDC_QUALITY),CB_GETCURSEL,0,0));
            state->values.showMap=IsDlgButtonChecked(window,IDC_SHOW_MAP)==BST_CHECKED;
            state->values.fullScreen=IsDlgButtonChecked(window,IDC_FULL_SCREEN)==BST_CHECKED;
            saveSettings(state->values);
            EndDialog(window,IDOK);
            return TRUE;
        }
        if (LOWORD(wp)==IDCANCEL) { EndDialog(window,IDCANCEL); return TRUE; }
        if (LOWORD(wp)==IDC_ABOUT) {
            MessageBoxW(window,
                L"Win95-3Dmaze-Remake\nWin95 3D迷宫重制版\n\n"
                L"月半菌制作 · 2026年9月\nwww.ybjun.com\n"
                L"https://github.com/ElliottZhao666666/win95-3dmaze-remake",
                L"关于 Win95-3Dmaze-Remake",MB_OK|MB_ICONINFORMATION);
            return TRUE;
        }
        return FALSE;
    case WM_CLOSE: EndDialog(window,IDCANCEL); return TRUE;
    }
    return FALSE;
}
int configure(HINSTANCE instance,Settings settings) {
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_BAR_CLASSES|ICC_UPDOWN_CLASS};
    if (!InitCommonControlsEx(&common)) return 1;
    ConfigState state{settings};
    INT_PTR result=DialogBoxParamW(instance,MAKEINTRESOURCEW(IDD_SETTINGS),nullptr,configProc,
                                   reinterpret_cast<LPARAM>(&state));
    for (HBITMAP image:state.previews) if (image) DeleteObject(image);
    return result==-1 ? 1 : 0;
}

struct App {
    HWND window=nullptr;
    bool preview=false,ready=false,quit=false;
    POINT startCursor{};
    Settings settings;
    maze::World world;
    MazeRenderer renderer;
};
LRESULT CALLBACK saverProc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    auto* app=reinterpret_cast<App*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if (message==WM_NCCREATE) {
        app=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app)); return TRUE;
    }
    switch (message) {
    case WM_SIZE: if (app && app->ready) app->renderer.resize(); return 0;
    case WM_MOUSEMOVE:
        if (app && app->ready && !app->preview) {
            POINT current{}; GetCursorPos(&current);
            int dx=current.x-app->startCursor.x,dy=current.y-app->startCursor.y;
            if (dx*dx+dy*dy>400) app->quit=true;
        }
        return 0;
    case WM_KEYDOWN: case WM_SYSKEYDOWN: case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN:
        if (app && !app->preview) app->quit=true;
        return 0;
    case WM_DESTROY: if (app) app->quit=true; return 0;
    }
    return DefWindowProcW(window,message,wp,lp);
}
bool parseHandle(const std::wstring& text,HWND& parent) {
    wchar_t* end=nullptr;
    unsigned long long number=wcstoull(text.c_str(),&end,0);
    if (!end || *end || !number) return false;
    parent=reinterpret_cast<HWND>(static_cast<UINT_PTR>(number));
    return IsWindow(parent)!=FALSE;
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    Settings settings=loadSettings();
    int argc=0; LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    std::wstring mode=argc>1?argv[1]:L"/c";
    std::transform(mode.begin(),mode.end(),mode.begin(),towlower);
    if (mode==L"/c" || mode==L"-c") {
        int result=configure(instance,settings);
        LocalFree(argv); if (SUCCEEDED(com)) CoUninitialize(); return result;
    }
    bool preview=mode==L"/p" || mode==L"-p";
    if (!preview && mode!=L"/s" && mode!=L"-s") {
        LocalFree(argv); if (SUCCEEDED(com)) CoUninitialize(); return 1;
    }
    HWND parent=nullptr;
    if (preview && (argc<3 || !parseHandle(argv[2],parent))) {
        LocalFree(argv); if (SUCCEEDED(com)) CoUninitialize(); return 1;
    }
    WNDCLASSW cls{}; cls.lpfnWndProc=saverProc; cls.hInstance=instance;
    cls.hCursor=preview?LoadCursorW(nullptr,IDC_ARROW):nullptr;
    cls.hbrBackground=static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    cls.lpszClassName=L"ThreeDMazeNative";
    if (!RegisterClassW(&cls)) {
        LocalFree(argv); if (SUCCEEDED(com)) CoUninitialize(); return 1;
    }
    App app; app.preview=preview; app.settings=settings;
    app.world.reset(int(settings.mazeSize));
    RECT rect{};
    if (preview) GetClientRect(parent,&rect);
    else {rect.right=GetSystemMetrics(SM_CXSCREEN);rect.bottom=GetSystemMetrics(SM_CYSCREEN);}
    app.window=CreateWindowExW(preview?0:WS_EX_TOPMOST,cls.lpszClassName,L"Win95-3Dmaze-Remake",
        preview?WS_CHILD|WS_VISIBLE:WS_POPUP|WS_VISIBLE,0,0,rect.right,rect.bottom,
        parent,nullptr,instance,&app);
    if (!app.window) {
        LocalFree(argv); if (SUCCEEDED(com)) CoUninitialize(); return 1;
    }
    if (!app.renderer.initialize(app.window,int(settings.quality),app.world.grid)) {
        DestroyWindow(app.window); LocalFree(argv);
        if (SUCCEEDED(com)) CoUninitialize(); return 2;
    }
    app.ready=true;
    bool cursorHidden=false;
    if (!preview) {GetCursorPos(&app.startCursor);ShowCursor(FALSE);cursorHidden=true;}
    auto previous=std::chrono::steady_clock::now();
    MSG message{};
    while (!app.quit) {
        while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            if (message.message==WM_QUIT) app.quit=true;
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        if (app.quit) break;
        auto now=std::chrono::steady_clock::now();
        float dt=std::clamp(std::chrono::duration<float>(now-previous).count(),0.f,.05f);
        previous=now;
        if (app.world.update(dt,int(settings.speed)) && !app.renderer.rebuild(app.world.grid)) break;
        if (!app.renderer.render(app.world,settings.showMap!=0)) break;
    }
    if (cursorHidden) ShowCursor(TRUE);
    if (IsWindow(app.window)) DestroyWindow(app.window);
    LocalFree(argv); if (SUCCEEDED(com)) CoUninitialize();
    return 0;
}
