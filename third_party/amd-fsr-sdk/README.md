# AMD FSR SDK (upscaler only)

The parts of the AMD FSR SDK (formerly AMD FidelityFX SDK) that `mod/XRenderD3D12` uses for
AMD FSR Upscaling, unchanged from the tag `v2.4.0` (FSR Upscaling 4.1.2) of
<https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK>, in the SDK's folder layout:

- `Kits/FidelityFX/api/include/`: `ffx_api.h`, `ffx_api_types.h`, `ffx_api_loader.h`,
  `dx12/ffx_api_dx12.h`
- `Kits/FidelityFX/upscalers/include/ffx_upscale.h`
- `Kits/FidelityFX/signedbin/amd_fidelityfx_loader_dx12.dll` (SHA-256
  `e2191d3c0828be45eb8affd4e3ec0378f2293adc345b772a7170edcf3b021ecb`)
- `docs/license.md`: AMD's license of the SDK; all the files above are under its MIT license.

The upscaler itself, `Kits/FidelityFX/signedbin/amd_fidelityfx_upscaler_dx12.dll` of the same tag
(57 664 272 bytes, SHA-256 `aad4fccccb29084a655f9a4f9f613bd22c8c03fb4a0f8840148cc6543b4ccace`), is
not in this repository: CMake downloads it when it configures `mod/XRenderD3D12`, or takes the
file that `XRENDER_FSR_UPSCALER_DLL` names. Both DLLs are signed by AMD and are not to be modified.

To update: replace these files with those of a newer tag, and update the tag, the hashes and the
size here and in `mod/XRenderD3D12/CMakeLists.txt`.
