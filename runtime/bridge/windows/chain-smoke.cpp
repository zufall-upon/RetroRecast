// AI-free x86 chain probe. Run only in a fresh private directory containing
// the selected hook and d3d9_chain.dll; never load the system D3D9 as fallback.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <string>

static LRESULT CALLBACK window_proc(HWND w, UINT m, WPARAM a, LPARAM b) {
    return DefWindowProcW(w, m, a, b);
}
static int failed(const char* where, HRESULT hr) {
    std::fprintf(stderr, "%s failed: 0x%08lx win32=%lu\n", where,
                 static_cast<unsigned long>(hr), GetLastError());
    return 2;
}
int main() {
    wchar_t exe[32768]{};
    DWORD n=GetModuleFileNameW(nullptr,exe,32768);
    if(!n || n>=32768) return failed("executable path",E_FAIL);
    std::wstring dir(exe,n); auto cut=dir.find_last_of(L"\\/");
    if(cut==std::wstring::npos) return failed("executable directory",E_FAIL);
    dir.resize(cut+1);
    HMODULE hook=LoadLibraryW((dir+L"d3d9.dll").c_str());
    if(!hook) return failed("LoadLibrary hook",E_FAIL);
    auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(hook,"Direct3DCreate9"));
    if(!create) { FreeLibrary(hook); return failed("hook export",E_FAIL); }
    IDirect3D9* d3d=create(D3D_SDK_VERSION);
    if(!d3d) { FreeLibrary(hook); return failed("hook -> chain Direct3DCreate9",E_FAIL); }
    HMODULE chain=GetModuleHandleW(L"d3d9_chain.dll");
    wchar_t chain_path[32768]{};
    DWORD cn=chain?GetModuleFileNameW(chain,chain_path,32768):0;
    if(!cn || cn>=32768 || _wcsicmp(chain_path,(dir+L"d3d9_chain.dll").c_str())) {
        d3d->Release(); FreeLibrary(hook); return failed("explicit chain identity",E_FAIL);
    }
    WNDCLASSW wc{}; wc.lpfnWndProc=window_proc; wc.hInstance=GetModuleHandleW(nullptr);
    wc.lpszClassName=L"KF1PrivateChainSmoke";
    if(!RegisterClassW(&wc)) { d3d->Release(); FreeLibrary(hook); return failed("RegisterClass",E_FAIL); }
    HWND w=CreateWindowExW(0,wc.lpszClassName,L"KF1 private chain smoke",WS_OVERLAPPEDWINDOW,
                           CW_USEDEFAULT,CW_USEDEFAULT,640,480,nullptr,nullptr,wc.hInstance,nullptr);
    if(!w) { d3d->Release(); FreeLibrary(hook); return failed("CreateWindow",E_FAIL); }
    ShowWindow(w,SW_SHOWNOACTIVATE);
    D3DPRESENT_PARAMETERS p{};p.Windowed=TRUE;p.SwapEffect=D3DSWAPEFFECT_DISCARD;
    p.hDeviceWindow=w;p.BackBufferFormat=D3DFMT_UNKNOWN;p.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* device=nullptr;
    HRESULT hr=d3d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,w,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&p,&device);
    if(FAILED(hr)||!device) { DestroyWindow(w);d3d->Release();FreeLibrary(hook);return failed("CreateDevice",hr); }
    struct Vertex { float x,y,z,rhw; D3DCOLOR colour; };
    const Vertex v[]={{50,50,0,1,0xffff0000},{500,50,0,1,0xff00ff00},{275,350,0,1,0xff0000ff}};
    const char* step="Clear";
    for(int i=0;i<3;++i) {
        MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
        step="Clear";hr=device->Clear(0,nullptr,D3DCLEAR_TARGET,D3DCOLOR_XRGB(20,30,40),1,0); if(FAILED(hr)) break;
        step="BeginScene";hr=device->BeginScene();if(FAILED(hr))break;
        step="SetFVF";hr=device->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE);
        if(SUCCEEDED(hr)){step="DrawPrimitiveUP";hr=device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,v,sizeof(Vertex));}
        HRESULT end=device->EndScene();if(SUCCEEDED(hr)&&FAILED(end)){step="EndScene";hr=end;}
        if(FAILED(hr))break;
        step="Present";hr=device->Present(nullptr,nullptr,nullptr,nullptr);if(FAILED(hr))break;
    }
    device->Release();d3d->Release();DestroyWindow(w);UnregisterClassW(wc.lpszClassName,wc.hInstance);FreeLibrary(hook);
    if(FAILED(hr))return failed(step,hr);
    std::puts("hook -> explicit chain -> Device -> triangle -> Present x3 -> released; AI not used");
    return 0;
}
