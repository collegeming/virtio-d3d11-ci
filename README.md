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

Build run 43749569277 produced `dx11um_virtio.dll` SHA-256
`cc79360ac06909fa63ef09055f679c31f427bea70fff766fc62d3e42673d7d2e`.
On the Mali-G2-Ultra / PanVK guest, with a direct
`cmd /c "set DXVK_UMD_RELAX_REQUIRED=mali&& ..."` launch:

- DXVK enumerates the Mali device and no longer rejects it on required
  features or `VK_KHR_swapchain`.
- The UMD handles the PanVK/KMD LUID mismatch by using the first DXGI adapter,
  and the runtime selects D3D11.0 after the Triton DDI clamp.
- The test then fastfails (`0xC0000409`) inside the PanVK ICD
  (`vulkan_virtio.dll`, offset `+0x6FA46C`) before printing a D3D11 result.
  A separate compute probe pins this to PanVK's pipeline compiler
  (`vkCreateComputePipelines` fastfail, `brk #0xf003`), not to the UMD.

The remaining blocker is therefore upstream PanVK shader compilation, pending
the Panfrost bitfield-patch rebase. This repository does not claim a working
D3D11 rendering stack yet.

## What the patches change

`patches/umd-fallback-luid.patch` changes the UMD's adapter selection: if
`IDXGIFactory4::EnumAdapterByLuid` fails because PanVK's Vulkan LUID differs
from the KMD LUID, the UMD uses DXGI adapter 0. This is a single-adapter
bring-up workaround; it does not change the multi-adapter path when the LUID
matches.

`patches/umd-ddi-table-init.patch` zeroes the runtime-supplied device-function
table before filling it, installs stubs for the two entries Triton leaves
unassigned (`pfnResetPrimitiveID`, `pfnSetVertexPipelineOutput`), and logs the
table size plus key function pointers before `CreateDevice` returns. This is
for the `DXGI_ERROR_DRIVER_INTERNAL_ERROR (0x887A0020)` bring-up
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
