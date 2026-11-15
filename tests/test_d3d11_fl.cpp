// D3D11 feature-level A/B + offscreen clear probe for Windows ARM64.
// Builds with the Windows SDK; no external dependencies.
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cstdio>
#include <cstring>

static bool probe(D3D_FEATURE_LEVEL requested, bool useDefault, bool debugLayer) {
    ID3D11Device *dev = nullptr;
    ID3D11DeviceContext *ctx = nullptr;
    D3D_FEATURE_LEVEL chosen = requested;
    UINT flags = debugLayer ? D3D11_CREATE_DEVICE_DEBUG : 0;

    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        useDefault ? nullptr : &requested, useDefault ? 0 : 1,
        D3D11_SDK_VERSION, &dev, &chosen, &ctx);

    std::printf("FL_REQ=%s(0x%x) debug=%d hr=0x%08lx chosen=0x%x\n",
                useDefault ? "default" : "explicit", (unsigned)requested,
                debugLayer ? 1 : 0, (unsigned long)hr, (unsigned)chosen);
    std::fflush(stdout);
    if (FAILED(hr)) return false;

    D3D11_TEXTURE2D_DESC td{};
    td.Width = 256;
    td.Height = 256;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    ID3D11Texture2D *tex = nullptr;
    hr = dev->CreateTexture2D(&td, nullptr, &tex);
    std::printf("  CreateTexture2D hr=0x%08lx\n", (unsigned long)hr);
    std::fflush(stdout);
    if (FAILED(hr)) { dev->Release(); ctx->Release(); return false; }

    ID3D11RenderTargetView *rtv = nullptr;
    hr = dev->CreateRenderTargetView(tex, nullptr, &rtv);
    std::printf("  CreateRenderTargetView hr=0x%08lx\n", (unsigned long)hr);
    std::fflush(stdout);
    if (FAILED(hr)) { tex->Release(); dev->Release(); ctx->Release(); return false; }

    const float color[4] = {0.25f, 0.5f, 0.75f, 1.0f};
    ctx->ClearRenderTargetView(rtv, color);
    ctx->Flush();

    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D *staging = nullptr;
    hr = dev->CreateTexture2D(&td, nullptr, &staging);
    std::printf("  CreateStaging hr=0x%08lx\n", (unsigned long)hr);
    std::fflush(stdout);
    if (FAILED(hr)) { rtv->Release(); tex->Release(); dev->Release(); ctx->Release(); return false; }

    ctx->CopyResource(staging, tex);
    D3D11_MAPPED_SUBRESOURCE map{};
    hr = ctx->Map(staging, 0, D3D11_MAP_READ, 0, &map);
    std::printf("  Map hr=0x%08lx\n", (unsigned long)hr);
    std::fflush(stdout);
    bool ok = false;
    if (SUCCEEDED(hr)) {
        unsigned char *p = (unsigned char *)map.pData;
        std::printf("  pixel: %u %u %u %u\n", p[0], p[1], p[2], p[3]);
        ok = p[0] > 50 && p[0] < 80 && p[1] > 110 && p[1] < 145 &&
             p[2] > 175 && p[2] < 210 && p[3] > 250;
        ctx->Unmap(staging, 0);
    }
    std::printf("  RESULT: %s\n", ok ? "PASS" : "FAIL");
    std::fflush(stdout);

    staging->Release();
    rtv->Release();
    tex->Release();
    ctx->Release();
    dev->Release();
    return ok;
}

int main(int argc, char **argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    bool debugLayer = argc > 1 && std::strcmp(argv[1], "debug") == 0;
    std::printf("probe=d3d11_fl debug_layer=%d\n", debugLayer ? 1 : 0);

    probe(D3D_FEATURE_LEVEL_0, true, debugLayer);
    probe(D3D_FEATURE_LEVEL_10_0, false, debugLayer);
    probe(D3D_FEATURE_LEVEL_10_1, false, debugLayer);
    probe(D3D_FEATURE_LEVEL_11_0, false, debugLayer);
    probe(D3D_FEATURE_LEVEL_11_1, false, debugLayer);
    return 0;
}
