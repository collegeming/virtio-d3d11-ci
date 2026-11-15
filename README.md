# virtio-d3d11-ci (experimental)

CI-only repository for building an **experimental** `dx11um_virtio.dll` from
[Droid-VM/virtio-d3d11](https://github.com/Droid-VM/virtio-d3d11) (`wingpu`)
and [Droid-VM/dxvk](https://github.com/Droid-VM/dxvk) (`wingpu`).

This repository intentionally contains **no upstream source code** and **no
Windows SDK/WDK headers**. The workflow fetches them at build time:

- UMD and DXVK source: cloned from the upstream `wingpu` branches.
- DXVK submodules: fetched after the clone (the UMD's pinned `thirdparty/dxvk`
  gitlink `035608a...` no longer exists, so the workflow does not use
  `git clone --recursive` on the UMD).
- Windows SDK/WDK headers: extracted from the Microsoft NuGet packages
  `Microsoft.Windows.SDK.CPP` and `Microsoft.Windows.WDK.arm64` into the
  gitignored `umd/include/winddk/` directory. They are not committed and not
  uploaded.
- Toolchain: llvm-mingw Linux release and Khronos glslang Linux release.

## Current validation state (2026-11-15)

Build run `43758177235` (artifact `dx11um-virtio-exp-9`) produced
`dx11um_virtio.dll` SHA-256
`0a4dfc0ab43deb7e2afe312d99b2ff35e55176763a9caf1d560f5a9da7443c19`.
This build defaults `DXVK_UMD_RELAX_REQUIRED` to `mali` when the variable is
empty; `strict`/`none` restores upstream behavior.

Offscreen D3D11 validation (artifact `fb6be454...`, explicit `mali`):
default, 10_0, 10_1 and 11_0 all returned `hr=0x00000000`; the 256x256 RTV
clear + staging readback returned pixel `64/128/191/255` and `RESULT: PASS`.
The earlier `0x887A0020` came from the runtime's format-7
(`DXGI_FORMAT_R32G32B32_UINT`) check: the DDI mask advertised RENDER_TARGET,
BLENDABLE and MSAA bits that D3D11 forbids for the R32G32B32 family. The
`triton-rgb32-clamp.patch` clears those bits; the runtime then finishes device
finalization.

DWM system-level validation (artifact9, default `mali`): DWM successfully
creates the DXVK device (`Found device`, `internal D3D11CreateDevice returned`,
D3D11.0 DDI), but its shared-texture path fails with:

```text
create_shared_vk_image: Failed to allocate Vulkan memory: ERROR_INVALID_EXTERNAL_HANDLE
virtio_wddm_create_resource: failed to create shared blob texture: 0x8007000e
Triton: tritonSetError_impl: virtio_wddm_create_resource: 0x8007000e
```

DWM then crashes in `dwmcore.dll` (`0xC00001AD`); LogonUI also crashes in
`Windows.UI.Xaml.dll` (`0xC000027B`). Host crosvm logged 4 MiB/10 MiB GPU blobs,
followed by `Failed to unlock and post for surface scanout` and a GPU reset
with the scanout surface dropped. The offscreen path works; the remaining
system-level blocker is the PanVK/UMD external-memory shared-blob path
(`VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_KMT_BIT`), not D3D11 device
creation.

## What the patches change

`patches/umd-fallback-luid.patch` changes the UMD's adapter selection: if
`IDXGIFactory4::EnumAdapterByLuid` fails because PanVK's Vulkan LUID differs
from the KMD LUID, the UMD uses DXGI adapter 0. This is a single-adapter
bring-up workaround; it does not change the multi-adapter path when the LUID
matches.

`patches/umd-ddi-table-init.patch` zeroes the runtime-supplied device-function
table before filling it and logs the table size, key function pointers, the
deferred-context/command-list slots, and the Triton base object pointers before
`CreateDevice` returns. It also turns the Triton format/MSAA diagnostics on.
This is for the `DXGI_ERROR_DRIVER_INTERNAL_ERROR (0x887A0020)` bring-up
investigation.

`patches/triton-rgb32-clamp.patch` clears render-target, blend, and MSAA bits
for the `R32G32B32_*` family (DXGI formats 5-8). D3D11 does not allow those
formats as RTV/MSAA targets; the runtime otherwise rejects device finalization
with `DXGI_ERROR_DRIVER_INTERNAL_ERROR (0x887A0020)` right after the format-7
check.

`patches/triton-d3d11-0-only.patch` clamps Triton's advertised DDI versions to
D3D11.0. The UMD's common device-function assignments use the D3D11.0 table
layout, so advertising D3D11.1 makes the runtime select the 11.1 table and the
UMD corrupts it (runtime returns `DXGI_ERROR_DRIVER_INTERNAL_ERROR`). This is
a bring-up-only clamp; the offscreen test only needs D3D11.0.

`patches/dxvk-relax-required.patch` adds an opt-in relaxation to
`src/dxvk/dxvk_device_info.cpp`:

- Empty `DXVK_UMD_RELAX_REQUIRED`: defaults to the `mali` preset so DWM and
  other processes that do not inherit the shell environment still get the
  bring-up relaxation.
- `DXVK_UMD_RELAX_REQUIRED=mali`: relax the four Mali/PanVK blockers
  `geometryShader`, `multiViewport`, `shaderClipDistance`,
  `shaderCullDistance`; `VK_KHR_swapchain` is relaxed too because the imported
  UMD device path does not expose it.
- `DXVK_UMD_RELAX_REQUIRED=strict` (or `none`): restore the upstream strict
  required-feature behavior.
- `DXVK_UMD_RELAX_REQUIRED=all` (or `1`): relax every required feature and
  extension check.
- A comma-separated list relaxes exactly those feature names. Any non-empty
  value also relaxes `khrSwapchain`.

The environment variable is read by the UMD process at `D3D11CreateDevice`
time. The built DLL does not change behavior unless the caller sets it.

This is a bring-up experiment. It does not imply that Mali/PanVK strictly
supports the relaxed D3D11 features. Applications that actually use geometry
shaders, multiple viewports, clip/cull distances, BC textures or swapchains
will still fail or render incorrectly.

## Usage

1. Run the `build-dx11um-virtio-exp` workflow (push to `main` or manual
   dispatch).
2. Download the `dx11um-virtio-exp-<run_number>` artifact.
3. On the Windows 11 ARM64 guest, put the DLL outside the DriverStore first,
   then point only the D3D11 slot of `UserModeDriverName` at a new DriverStore
   copy (for example `dx11um_virtio_exp.dll`). Do not overwrite the UMD file
   that DWM has mapped.
4. Run the D3D11 test in a new process with:

   ```bat
   set DXVK_UMD_RELAX_REQUIRED=mali
   set VIRTIO_WDDM_LOG_FILE=C:\panvk\dx11um_umd_exp.log
   C:\panvk\test_d3d11_arm64.exe
   ```

5. Restore the registry value and remove the experimental DriverStore file
   afterwards.

## Licensing

See `NOTICE`. The build artifact contains the upstream UMD (AGPL-3.0) and
DXVK (zlib) code. It is not an official DroidVM release and is not intended
for redistribution.
