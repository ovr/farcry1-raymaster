# AMD FidelityFX CAS

The parts of AMD FidelityFX CAS (Contrast Adaptive Sharpening, Timothy Lottes) that `mod/XRenderD3D12`
uses for the sharpening of its TAA (`d3d12_AA taa`, `d3d12_UpscaleSharpness`), unchanged from the tag
`v1.0.2` (commit `9fabcc9a2c45f958aff55ddfda337e74ef894b7f`) of
<https://github.com/GPUOpen-Effects/FidelityFX-CAS>, in the repository's folder layout:

- `ffx-cas/ffx_cas.h`: CAS's filter (`CasFilter`) and its constants (`CasSetup`), for the GPU and the
  CPU
- `ffx-cas/ffx_a.h`: the portability header it is written in (HLSL, GLSL and C)
- `LICENSE.txt`: AMD's MIT license of the repository (the headers carry it too)

The renderer's own kernel and bindings are `mod/XRenderD3D12/Shaders/TaaCas.hlsli` and `Shaders/Taa.hlsl`,
the host `mod/XRenderD3D12/UpscalerTaa.cpp` (`CasSetup` on the CPU).

To update: replace these files with those of a newer tag, and update the tag and the commit here.
