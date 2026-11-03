# CMAA2

The part of CMAA2 (Conservative Morphological Anti-Aliasing 2.3, Filip Strugar, Intel Corporation)
that `mod/XRenderD3D12` uses for its anti-aliasing `d3d12_AA cmaa2`, unchanged from the commit
`071c6b0857559f4e36f614362e6d2aab1b61938a` (2023-01-03) of <https://github.com/GameTechDev/CMAA2>,
in the repository's folder layout:

- `Projects/CMAA2/CMAA2/CMAA2.hlsl`: its compute kernels (edge detection, the shapes' processing,
  the deferred colour apply, the indirect dispatches' arguments) and its presets
- `license.txt`: the Apache License 2.0 of the repository (`CMAA2.hlsl` names it in its header)

Not a tag: the repository has none. It is archived; its last commit adds the archiving notice to its
`README.md` only, so these files are those of the last commit that changed its code, `c7df5a5`
(2019-12-05).

The renderer's own entry and bindings are `mod/XRenderD3D12/Shaders/AntiAliasCmaa2.hlsl`, the host
`mod/XRenderD3D12/PostAA.cpp` (with the wide root signature of `D3D12Compute.cpp`).

To update: replace these files with those of a newer commit or tag, and update the commit here.
