# AMD FidelityFX CACAO

The parts of AMD FidelityFX CACAO (Combined Adaptive Compute Ambient Occlusion) v1.2 that
`mod/XRenderD3D12` uses for its ambient occlusion (`d3d12_AO cacao`), unchanged from the tag
`v1.2` (commit `0ddca95e6714727a252ead345591ca8f2598f261`) of
<https://github.com/GPUOpen-Effects/FidelityFX-CACAO>, in the repository's folder layout:

- `ffx-cacao/inc/`: `ffx_cacao.h`, `ffx_cacao_impl.h`
- `ffx-cacao/src/`: `ffx_cacao.cpp`, `ffx_cacao_impl.cpp` (the Direct3D 12 host, built with
  `FFX_CACAO_ENABLE_D3D12`), `ffx_cacao.hlsl`, `ffx_cacao_bindings.hlsl`, `ffx_cacao_defines.h`, and
  `build_shaders_dxil.bat`, whose dxc commands the build follows to make the
  `PrecompiledShadersDXIL/CACAO*.h` headers that `ffx_cacao_impl.cpp` includes
- `license.txt`, `NOTICES.txt`: AMD's MIT license of all the files above

`ffx_cacao_impl.cpp` also includes `d3dx12.h` (`third_party/directx-headers`) and the debug markers of
AMD's Cauldron framework, which the renderer replaces by an empty stub
(`mod/XRenderD3D12/Cacao/base/UserMarkers.h`).

To update: replace these files with those of a newer tag, and update the tag and the commit here.
