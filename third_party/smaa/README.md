# SMAA

The parts of SMAA (Enhanced Subpixel Morphological Antialiasing, Jorge Jimenez, Jose I. Echevarria,
Belen Masia, Fernando Navarro, Diego Gutierrez) that `mod/XRenderD3D12` uses for its anti-aliasing
`d3d12_AA smaa` (SMAA 1x), unchanged from the commit `71c806a838bdd7d517df19192a20f0c61b3ca29d`
(2013-11-06) of <https://github.com/iryoku/smaa>, in the repository's folder layout:

- `SMAA.hlsl`: the three passes (edge detection, blending weight calculation, neighborhood blending)
  and the presets
- `Textures/AreaTex.h`: the precomputed area texture (160 x 560, R8G8)
- `Textures/SearchTex.h`: the precomputed search texture (64 x 16, R8)
- `LICENSE.txt`: the MIT license of all the files above

Not a tag: the repository's last tag, `v2.8` (2013-08-22), has the files of an older license (with a
clause on binary distributions) and an older search texture; its MIT license came with the commit
`27fad0b` after it, and its last commit, the one here, has no tag of its own.

The renderer's own entry points and bindings are `mod/XRenderD3D12/Shaders/AntiAliasSmaa.hlsl`,
`Shaders/Smaa.hlsli` and `Shaders/SmaaBlend.hlsli`, the host `mod/XRenderD3D12/PostAA.cpp`.

To update: replace these files with those of a newer commit or tag, and update the commit here.
