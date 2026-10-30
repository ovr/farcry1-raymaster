# XeGTAO

The parts of Intel's XeGTAO (version 1.30) that `mod/XRenderD3D12` uses for its ambient
occlusion (`d3d12_AO gtao`), unchanged from the tag `2021-12-15-bins` (commit
`188587a986f94db285c83c99676123019a0fd9b6`) of <https://github.com/GameTechDev/XeGTAO>, in the
repository's folder layout:

- `Source/Rendering/Shaders/XeGTAO.h`: the constants, shared by C++ (`GTAOUpdateConstants`) and HLSL
- `Source/Rendering/Shaders/XeGTAO.hlsli`: the passes (depth MIPs, normals from depth, the main
  pass, the denoiser)
- `LICENSE`: Intel's MIT license of all the files above

The renderer's own entry points and bindings are `mod/XRenderD3D12/Shaders/XeGtao.hlsl`, the host
`mod/XRenderD3D12/AOXeGtao.cpp`.

To update: replace these files with those of a newer tag, and update the tag and the commit here.
