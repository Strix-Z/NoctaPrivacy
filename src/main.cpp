// NoctaPrivacy v0.1.2 - always-on blurred Spout2 overlay for Streamlabs Desktop.
// Read-only visual capture. No OpenProcess/ReadProcessMemory/injection/PKA IPC.
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <wrl/client.h>
#include <SpoutDX.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <algorithm>
#include <cstring>
#include <cwctype>

using Microsoft::WRL::ComPtr;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
using Clock = std::chrono::steady_clock;

static void hr(HRESULT code, const char* what) {
    if (FAILED(code)) { std::cerr << what << " failed: HRESULT 0x" << std::hex << static_cast<unsigned>(code) << std::dec << "\n";
        winrt::check_hresult(code); }
}
static std::wstring lowercase(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c){ return static_cast<wchar_t>(towlower(c)); });
    return s;
}
struct Finder { std::wstring search; HWND result = nullptr; };
static BOOL CALLBACK enumWindows(HWND hwnd, LPARAM param) {
    auto& f = *reinterpret_cast<Finder*>(param);
    if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) return TRUE;
    int len = GetWindowTextLengthW(hwnd);
    if (len <= 0) return TRUE;
    std::wstring title(static_cast<size_t>(len + 1), L'\0');
    int n = GetWindowTextW(hwnd, title.data(), len + 1);
    title.resize(static_cast<size_t>(std::max(n, 0)));
    if (lowercase(title).find(f.search) != std::wstring::npos) {
        f.result = hwnd;
        std::wcout << L"Captured window: " << title << L"\n";
        return FALSE;
    }
    return TRUE;
}
static HWND findWindow(std::wstring term) {
    Finder f{lowercase(std::move(term)), nullptr};
    EnumWindows(enumWindows, reinterpret_cast<LPARAM>(&f));
    return f.result;
}

static char const* shader = R"HLSL(
cbuffer BlurSettings : register(b0) { float2 inverseSize; float2 axis; };
Texture2D image : register(t0);
SamplerState linearClamp : register(s0);
struct Varyings { float4 position:SV_POSITION; float2 uv:TEXCOORD0; };
Varyings VS(uint index:SV_VertexID) {
    float2 uv = float2((index << 1) & 2, index & 2);
    Varyings o;
    o.position = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
    o.uv = uv;
    return o;
}
float4 PS(Varyings i):SV_TARGET {
    float2 stepUV = inverseSize * axis;
    float4 color = image.Sample(linearClamp, i.uv) * 0.204164f;
    color += (image.Sample(linearClamp, i.uv + stepUV * 1.0f) + image.Sample(linearClamp, i.uv - stepUV * 1.0f)) * 0.180174f;
    color += (image.Sample(linearClamp, i.uv + stepUV * 2.0f) + image.Sample(linearClamp, i.uv - stepUV * 2.0f)) * 0.123832f;
    color += (image.Sample(linearClamp, i.uv + stepUV * 3.0f) + image.Sample(linearClamp, i.uv - stepUV * 3.0f)) * 0.066283f;
    color += (image.Sample(linearClamp, i.uv + stepUV * 4.0f) + image.Sample(linearClamp, i.uv - stepUV * 4.0f)) * 0.027630f;
    return float4(color.rgb, 1.0);
}
)HLSL";

class GPU {
    ComPtr<ID3D11Device> dev_;
    ComPtr<ID3D11DeviceContext> ctx_;
    ComPtr<ID3D11Texture2D> src_, tmp_, out_;
    ComPtr<ID3D11ShaderResourceView> srcView_, tmpView_;
    ComPtr<ID3D11RenderTargetView> tmpTarget_, outTarget_;
    ComPtr<ID3D11VertexShader> vertex_;
    ComPtr<ID3D11PixelShader> pixel_;
    ComPtr<ID3D11SamplerState> sampler_;
    ComPtr<ID3D11Buffer> constants_;
    unsigned w_ = 0, h_ = 0;
    struct Params { float inverseSize[2]; float axis[2]; };
public:
    void init() {
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        D3D_FEATURE_LEVEL level{};
        hr(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
              nullptr, 0, D3D11_SDK_VERSION, &dev_, &level, &ctx_), "D3D11CreateDevice");
        ComPtr<ID3DBlob> vs, ps, errors;
        HRESULT rc = D3DCompile(shader, strlen(shader), "blur", nullptr, nullptr, "VS", "vs_5_0", 0, 0, &vs, &errors);
        if (FAILED(rc) && errors) std::cerr << static_cast<const char*>(errors->GetBufferPointer()) << '\n';
        hr(rc, "Compile vertex shader");
        errors.Reset();
        rc = D3DCompile(shader, strlen(shader), "blur", nullptr, nullptr, "PS", "ps_5_0", 0, 0, &ps, &errors);
        if (FAILED(rc) && errors) std::cerr << static_cast<const char*>(errors->GetBufferPointer()) << '\n';
        hr(rc, "Compile pixel shader");
        hr(dev_->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vertex_), "Vertex shader");
        hr(dev_->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &pixel_), "Pixel shader");
        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        hr(dev_->CreateSamplerState(&sd, &sampler_), "Sampler");
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = sizeof(Params); bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        hr(dev_->CreateBuffer(&bd, nullptr, &constants_), "Blur constants");
    }
    ID3D11Device* device() const { return dev_.Get(); }
    void resize(unsigned width, unsigned height) {
        if (width == w_ && height == h_) return;
        if (!width || !height) throw std::runtime_error("Invalid capture dimensions");
        ctx_->ClearState();
        srcView_.Reset(); tmpView_.Reset(); tmpTarget_.Reset(); outTarget_.Reset();
        src_.Reset(); tmp_.Reset(); out_.Reset();
        w_ = width; h_ = height;
        D3D11_TEXTURE2D_DESC t{};
        t.Width = w_; t.Height = h_; t.ArraySize = 1; t.MipLevels = 1;
        t.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        t.SampleDesc.Count = 1; t.Usage = D3D11_USAGE_DEFAULT;
        t.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        hr(dev_->CreateTexture2D(&t, nullptr, &src_), "Source texture");
        t.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        hr(dev_->CreateTexture2D(&t, nullptr, &tmp_), "Temporary texture");
        hr(dev_->CreateTexture2D(&t, nullptr, &out_), "Output texture");
        hr(dev_->CreateShaderResourceView(src_.Get(), nullptr, &srcView_), "Source SRV");
        hr(dev_->CreateShaderResourceView(tmp_.Get(), nullptr, &tmpView_), "Temporary SRV");
        hr(dev_->CreateRenderTargetView(tmp_.Get(), nullptr, &tmpTarget_), "Temporary RTV");
        hr(dev_->CreateRenderTargetView(out_.Get(), nullptr, &outTarget_), "Output RTV");
        std::cout << "Output resolution " << w_ << "x" << h_ << "\n";
    }
    bool valid() const { return out_.Get() != nullptr; }
    void copyFrame(ID3D11Texture2D* capture, unsigned width, unsigned height) {
        // Content can be smaller than the capture pool's allocation after a resize.
        D3D11_TEXTURE2D_DESC desc{}; capture->GetDesc(&desc);
        if (desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM) throw std::runtime_error("HDR/unsupported capture pixel format");
        width = std::min(width, desc.Width); height = std::min(height, desc.Height);
        resize(width, height);
        D3D11_BOX area{0, 0, 0, width, height, 1};
        ctx_->CopySubresourceRegion(src_.Get(), 0, 0, 0, 0, capture, 0, &area);
    }
    void blackout() {
        float black[4]{0.0f, 0.0f, 0.0f, 1.0f};
        ctx_->OMSetRenderTargets(1, outTarget_.GetAddressOf(), nullptr);
        ctx_->ClearRenderTargetView(outTarget_.Get(), black);
        ctx_->OMSetRenderTargets(0, nullptr, nullptr);
    }
    void pass(ID3D11ShaderResourceView* input, ID3D11RenderTargetView* output, float x, float y) {
        Params cb{{1.0f / static_cast<float>(w_), 1.0f / static_cast<float>(h_)}, {x, y}};
        ctx_->UpdateSubresource(constants_.Get(), 0, nullptr, &cb, 0, 0);
        D3D11_VIEWPORT vp{0.f, 0.f, static_cast<float>(w_), static_cast<float>(h_), 0.f, 1.f};
        ctx_->RSSetViewports(1, &vp);
        ctx_->OMSetRenderTargets(1, &output, nullptr);
        ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx_->IASetInputLayout(nullptr);
        ctx_->VSSetShader(vertex_.Get(), nullptr, 0);
        ctx_->PSSetShader(pixel_.Get(), nullptr, 0);
        ctx_->PSSetConstantBuffers(0, 1, constants_.GetAddressOf());
        ctx_->PSSetSamplers(0, 1, sampler_.GetAddressOf());
        ctx_->PSSetShaderResources(0, 1, &input);
        ctx_->Draw(3, 0);
        ID3D11ShaderResourceView* empty = nullptr;
        ctx_->PSSetShaderResources(0, 1, &empty);
        ctx_->OMSetRenderTargets(0, nullptr, nullptr);
    }
    void process(bool blur, float strength) {
        if (!blur) { ctx_->CopyResource(out_.Get(), src_.Get()); return; }
        pass(srcView_.Get(), tmpTarget_.Get(), strength, 0.f);
        pass(tmpView_.Get(), outTarget_.Get(), 0.f, strength);
    }
    ID3D11Texture2D* output() const { return out_.Get(); }
};

class WindowCapture {
    GraphicsCaptureItem item_{nullptr};
    Direct3D11CaptureFramePool pool_{nullptr};
    GraphicsCaptureSession session_{nullptr};
    IDirect3DDevice winrtDevice_{nullptr};
    std::atomic<bool> closed_{false};
    winrt::event_token closeToken_{};
    bool subscribed_ = false;
    winrt::Windows::Graphics::SizeInt32 poolSize_{};
public:
    void start(HWND hwnd, ID3D11Device* d3d) {
        auto factory = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        hr(factory->CreateForWindow(hwnd, winrt::guid_of<GraphicsCaptureItem>(), winrt::put_abi(item_)), "CreateForWindow");
        ComPtr<IDXGIDevice> dxgi;
        hr(d3d->QueryInterface(IID_PPV_ARGS(&dxgi)), "Query IDXGIDevice");
        hr(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(),
            reinterpret_cast<::IInspectable**>(winrt::put_abi(winrtDevice_))),
            "CreateDirect3D11DeviceFromDXGIDevice");
        poolSize_ = item_.Size();
        pool_ = Direct3D11CaptureFramePool::CreateFreeThreaded(
            winrtDevice_, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, poolSize_);
        session_ = pool_.CreateCaptureSession(item_);
        closeToken_ = item_.Closed([this](auto&&, auto&&){ closed_ = true; });
        subscribed_ = true;
        session_.IsCursorCaptureEnabled(false);
        session_.StartCapture();
    }
    ~WindowCapture() {
        // Closed callbacks must be removed before the state they refer to is destroyed.
        if (item_ && subscribed_) item_.Closed(closeToken_);
        if (session_) session_.Close();
        if (pool_) pool_.Close();
    }
    bool closed() const { return closed_; }
    unsigned width() const { return static_cast<unsigned>(std::max(0, poolSize_.Width)); }
    unsigned height() const { return static_cast<unsigned>(std::max(0, poolSize_.Height)); }
    bool read(GPU& gpu) {
        auto frame = pool_.TryGetNextFrame();
        if (!frame) return false;
        auto size = frame.ContentSize();
        if (size.Width < 1 || size.Height < 1) return false;
        auto native = frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        winrt::com_ptr<ID3D11Texture2D> texture;
        hr(native->GetInterface(winrt::guid_of<ID3D11Texture2D>(), texture.put_void()), "Get captured texture");
        gpu.copyFrame(texture.get(), static_cast<unsigned>(size.Width), static_cast<unsigned>(size.Height));
        bool changed = (size.Width != poolSize_.Width || size.Height != poolSize_.Height);
        frame.Close();
        if (changed) { poolSize_ = size; pool_.Recreate(winrtDevice_, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size); }
        return true;
    }
};

int wmain(int argc, wchar_t** argv) {
    constexpr int HOTKEY_BLACK = 101, HOTKEY_QUIT = 102;
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        std::wstring title = argc > 1 ? argv[1] : L"PokeAlliance";
        HWND hwnd = findWindow(title);
        if (!hwnd) { std::wcerr << L"No visible game window with title containing: " << title << L"\n";
            return 2; }
        GPU gpu;
        gpu.init();
        WindowCapture capture;
        capture.start(hwnd, gpu.device());
        gpu.resize(capture.width(), capture.height());
        spoutDX sender;
        if (!sender.OpenDirectX11(gpu.device()) || !sender.SetSenderName("NoctaPrivacy"))
            throw std::runtime_error("Spout2 sender initialization failed");
        // Never show uncaptured content on startup.
        gpu.blackout();
        if (!sender.SendTexture(gpu.output())) throw std::runtime_error("Initial Spout2 SendTexture failed");
        bool emergency = false, alive = true, hasCapturedFrame = false;
        float strength = 7.0f; // ~28-pixel max sample displacement at source resolution.
        // Streamlabs controls overlay show/hide; do not register F10 here.
        bool hot2 = RegisterHotKey(nullptr, HOTKEY_BLACK, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_F11) != 0;
        bool hot3 = RegisterHotKey(nullptr, HOTKEY_QUIT, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_F12) != 0;
        if (!hot2 || !hot3) std::cerr << "WARNING: one or more hotkeys could not register. Check for conflicts.\n";
        std::cout << "Sender: NoctaPrivacy (always blurred). Ctrl+Alt+F11 = BLACKOUT, F12 = exit.\n";
        std::cout << "In Streamlabs, keep Game Capture below Spout2; use source visibility to show/hide blur.\n";
        auto lastCapture = Clock::now();
        while (alive) {
            MSG m{};
            while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
                if (m.message == WM_HOTKEY) {
                    if (m.wParam == HOTKEY_BLACK) { emergency = !emergency; std::cout << "Emergency opaque " << (emergency ? "ON" : "OFF") << "\n"; }
                    if (m.wParam == HOTKEY_QUIT) { alive = false; }
                } else if (m.message == WM_QUIT) alive = false;
                else { TranslateMessage(&m); DispatchMessageW(&m); }
            }
            if (!alive) break;
            bool received = false;
            if (!capture.closed() && IsWindow(hwnd) && !IsIconic(hwnd)) {
                try { received = capture.read(gpu); }
                catch (winrt::hresult_error const&) { /* Fail closed below. */ }
            }
            if (received) { lastCapture = Clock::now(); hasCapturedFrame = true; }
            bool stale = !hasCapturedFrame || capture.closed() || !IsWindow(hwnd) || IsIconic(hwnd) ||
                (Clock::now() - lastCapture > std::chrono::milliseconds(1000));
            if (!gpu.valid()) break;
            if (emergency || stale) gpu.blackout();
            else gpu.process(true, strength);
            if (!sender.SendTexture(gpu.output())) std::cerr << "Spout send failed (check receiver/GPU).\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(33));
        }
        // Best effort on normal exit. Streamlabs may freeze the last Spout frame.
        gpu.blackout(); sender.SendTexture(gpu.output());
        if (hot2) UnregisterHotKey(nullptr, HOTKEY_BLACK);
        if (hot3) UnregisterHotKey(nullptr, HOTKEY_QUIT);
        sender.ReleaseSender(); sender.CloseDirectX11();
        return 0;
    } catch (std::exception const& e) {
        std::cerr << "ERROR: " << e.what() << '\n';
        return 1;
    }
}
