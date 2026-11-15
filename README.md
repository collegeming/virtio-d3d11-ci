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

## What the patch changes

`patches/dxvk-relax-required.patch` adds an opt-in relaxation to
`src/dxvk/dxvk_device_info.cpp`:

- Empty `DXVK_UMD_RELAX_REQUIRED`: upstream strict behavior (no change).
- `DXVK_UMD_RELAX_REQUIRED=mali`: relax the four Mali/PanVK blockers
  `geometryShader`, `multiViewport`, `shaderClipDistance`,
  `shaderCullDistance`; `VK_KHR_swapchain` is relaxed too because the imported
  UMD device path does not expose it.
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
