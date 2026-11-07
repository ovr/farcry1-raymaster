# XRenderD3D12

Our renderer on Direct3D 12 (x64). How to build, load and check it: `AGENTS.md`. This is the
reference of its features and console variables.

## Window and full screen

- The window: the video options set `r_Width`, `r_Height` and `r_Fullscreen` while the game runs
  and ask for no restart, so the renderer applies them after the frame they change in
  (`CheckResolutionChange`). Windowed, the window's outer size is `r_Width` x `r_Height` and the
  back buffer gets its client area. Full screen is a borderless topmost window over the monitor,
  which gets the display mode `r_Width` x `r_Height` (32 bits, the highest refresh rate of that
  size), as the original's exclusive full screen switched the monitor; without such a mode the
  monitor keeps its own and the picture is stretched over it. Losing the focus (Alt+Tab)
  minimises the window and puts the desktop's mode back; leaving full screen and quitting do too.
  Loaded directly (as `XRenderD3D9.dll`, `r_Driver = "Direct3D9"`), the renderer makes the
  process per monitor DPI aware itself, as XRenderD3D9Traced does.

## Upscaler

- `d3d12_Upscaler` is the upscaler of the main view's 3D scene: `none` (the default; the frames
  are those of the renderer without one, DemoCompare difference 0) or `fsr` (AMD FSR Upscaling:
  FSR 4 on the AMD GPUs it supports, FSR 3.1 on the others). An unknown or unavailable one is
  logged once and acts as `none`. `d3d12_UpscaleMode` is the render size: 0 Native AA (the
  display's size, anti-aliasing only), 1 Quality, 2 Balanced, 3 Performance, 4 Ultra
  Performance. `d3d12_UpscaleSharpness` is 0 to 1 (0.5; 0 turns sharpening off; TAA's is CAS's
  sharpness). `d3d12_UpscaleVersion` is the upscaler's major version: 0 (the default) the one it
  picks for the GPU, 3 or 4 (FSR 3.1 or FSR 4, the newest of that major the upscaler DLL has; one it
  does not have is logged and acts as 0); a new value makes the upscaler anew. On a Radeon RX 7900
  XTX (RDNA 3; FSR 4 is made for RDNA 4), Native AA at 3840 x 2160 takes the GPU some
  4 ms a frame with FSR 4 and 1.6 to 1.75 ms with FSR 3.1 (DemoLevel sweeps of Swamp and Pier:
  369 and 424 FPS without it, 154 and 163 with FSR 4, 235 and 257 with FSR 3.1).
  `d3d12_UpscaleDebug 1` shows FSR's debug view (FSR 4: the motion vectors at the top left, the
  weight of the history at the top right), and `d3d12_UpscaleDebug 2` the depth it gets (near
  black, far white, log scale). All of them change at run time, and the frame dump names the
  upscaler, its render size, the frame's jitter and whether it gets the HDR scene's light
  (`HDR input`, `d3d12_UpscaleHdr`, in the HDR section below).
  The game's menu gets them from `assets/scripts/05-video-upscaler.patch`: in Video Options under
  Resolution, Upscaler, None / FSR (auto) / FSR 3.1 / FSR 4 (`d3d12_Upscaler` and
  `d3d12_UpscaleVersion` 0, 3 or 4), and Upscaler Mode, Native AA to Ultra Performance
  (`d3d12_UpscaleMode` 0 to 4, greyed out while the upscaler is None); Apply sets them without a
  restart, and both are greyed out without `d3d12_Upscaler`.
  - With an upscaler, the main view's scene (`BeginScene3D` to `UpscaleScene` before the screen
    effects, `Upscale.cpp`) draws into render-size targets: colour, a depth buffer, motion vectors
    (R16G16, written by the general program's opaque draws and the water) and the depth the
    upscaler gets (R32, the nearest of the depth writes and of the water, which does not write
    the depth buffer). It is jittered and its textures get a mip bias. The screen effects and the
    2D stay at the display's size. The targets are bound before the opaque list's passes (the
    ambient light's target, the scene for rays), so the ambient occlusion and the rays' GI work on
    the scene's depth buffer at its size (bound after them, they took the back buffer's, empty
    with an upscaler, and did nothing).
  - Motion comes from the previous frame's camera for static geometry and the water's surface, and
    from the previous world matrix of a character instance or a permanent object. The weapon
    (`FOB_NEAREST`) has none.
  - The history is reset after a frame without the main view, a camera jump of more than 5 m, a
    field of view change of more than 10 degrees, and a new mode.
  - The path tracer (`d3d12_PathTracing`) needs the motion vectors: with `none` it gets the scene
    targets of the display's size, without jitter or mip bias, and the scene's picture as it is
    (`none (the path tracer's scene targets)` in the log and the frame dump).
  - The build downloads the upscaler DLL (`third_party/amd-fsr-sdk/README.md`) and copies it, the
    loader and AMD's license next to `XRenderD3D12.dll`.
  - For comparisons, DemoLevel with `-DemoArgs '--exec-at 1 "d3d12_Upscaler fsr|d3d12_UpscaleMode
    0"'` against the same run without it.

## Anti-aliasing

- `d3d12_AA` is the anti-aliasing of the main view's 3D scene without an upscaler (by default
  `taa`, its CAS sharpening at 0.5): `none` (the frames are those of the renderer without it,
  DemoCompare difference 0), `fxaa`,
  `smaa`, `cmaa2`, `taa`, `msaa2`, `msaa4` or `msaa8`. An unknown value is logged once and acts as `none`.
  `d3d12_AAQuality` is 0 to 3 (low, medium, high, ultra; 2; MSAA has none), `d3d12_AADebug 1` shows what it changes (the picture grey, red by how
  much a pixel is blended; SMAA's and CMAA2's edges too; TAA's red where its history goes, yellow
  where it is clipped). All of them change at run time. With an upscaler
  that anti-aliases (FSR) `d3d12_AA` is not used (logged once); r_DisplayInfo's `Anti-aliasing`
  row names the one that is (`FXAA (High)`, `SMAA 1x (High)`, `CMAA2 (High)`, `TAA (High, HDR input)`,
  `MSAA 4x, alpha to coverage`, `FSR, temporal`, `none`) and the frame dump has an `anti-aliasing:` line. The original's FSAA modes
  (`r_FSAA`, `EnumAAFormats`) are not offered: MSAA is `d3d12_AA`'s.
- The game's menu gets it from `assets/scripts/09-video-anti-aliasing.patch`: Video Options'
  Anti-Aliasing list is None / FXAA / SMAA 1x / CMAA2 / TAA / MSAA 2x / 4x / 8x (`d3d12_AA`) in place of the game's FSAA modes, on
  in a game (the game greys it out there: the Direct3D 9 renderer had to relaunch) and greyed out
  while the Upscaler is not None; Apply sets it without a restart.
- FXAA, SMAA and CMAA2 are drawn over the finished, tone mapped 3D scene in the back buffer (`DrawPostAA`,
  `PostAA.cpp`, between `EndMainScene` and the screen effects): before the render modes, the
  screen effects, the HUD and the 2D, so the text stays sharp. Its input is a copy of the back
  buffer's colour (the water's scene copy); the alpha the HUD masks by stays. The program
  `AntiAlias.hlsl` discards the pixels it keeps, so they stay bit for bit. With the HDR10
  highlight layer each pixel's ratio is blended the same way as its colour (a copy of the layer).
  The GPU time's section is `anti-aliasing`.
- FXAA (`Shaders/Fxaa.hlsli`) is ours, written from the published description of FXAA 3.11's
  quality algorithm (T. Lottes, NVIDIA): a pixel whose 4 neighbours' luminance range is above
  max(t_min, t lmax) is on an edge; the larger second difference of the 3 x 3 luminance across
  the rows or the columns gives its direction, the neighbour with the steeper step its side; the
  search walks both ways along the edge (4, 7, 11 or 14 steps by the quality) to where the mean of
  the pair across it leaves the edge's luminance by a quarter of the step; the pixel nearer the
  end that leaves on the side against its own blends towards the neighbour across the edge by
  0.5 - its distance / the edge's length, and a pixel unlike its 3 x 3 mean (a thin line, a
  single pixel) by a subpixel share (0.25, 0.5, 0.75, 0.75 by the quality). The thresholds t:
  0.25, 0.166, 0.166, 0.125 by the quality, t_min 0.0833, 0.0833, 0.0833, 0.0625 (High: FXAA's
  own defaults). Against the same view supersampled 4 x (Training's islands from the hill, the
  render tests' `aa`) every quality takes the error at the edges to 0.73-0.78 of none's; the
  subpixel share is what softens the textures, the foliage's above all (High keeps 0.75-0.81 of
  their detail; 0.0312 as t_min softened them to 0.69-0.78 for no better edges).
  - Unit tests `Fxaa_*`: an edge drawn without anti-aliasing comes out nearer its exact coverage
    (0.33-0.41 of the error without it at 8 pixel steps), along the edge's direction only; flat
    pictures and the alpha stay bit for bit; an edge along the pixels' grid stays sharp.
- SMAA 1x (`smaa`) is SMAA's (J. Jimenez et al., `third_party/smaa`, MIT), in three passes:
  - its luma edge detection and its blending weights (with its precomputed area and search
    textures, `Textures/AreaTex.h`, `SearchTex.h`) are two compute kernels of its `SMAA.hlsl` as
    it is (`Shaders/AntiAliasSmaa.hlsl`, `Shaders/Smaa.hlsli`: `SMAA_CUSTOM_SL` with the
    renderer's samplers; a kernel writes no edges where SMAA's pixel shader discards) into an
    R8G8 and an R8G8B8A8 target of the display's size;
  - its neighbourhood blending is the `AntiAlias` program's pass (`Shaders/SmaaBlend.hlsli`):
    SMAA's choice of the neighbours and their weights, with exact loads in place of its bilinear
    taps, in linear light (SMAA asks for this pass's sRGB read and write, which the UNORM back
    buffer and its copy have not); a pixel without weights is not written.
  - `d3d12_AAQuality` is SMAA's preset: low, medium, high, ultra (edge threshold 0.15, 0.1, 0.1,
    0.05; 4, 8, 16, 32 search steps; the diagonals and the corners from high on). Against the
    reference of the render tests' `aa` the error at the edges is 0.62-0.67 of none's at High
    (0.61-0.69 at the four presets) and the textures keep 0.85-0.91 of their detail (0.81-0.92):
    sharper edges and textures than FXAA's (0.73-0.75 and 0.75-0.81). At 3840 x 2160 its GPU
    section `anti-aliasing` (with the copy of the picture) takes 0.15, 0.16, 0.17-0.18 and 0.22 ms
    (low to ultra; FXAA 0.09-0.12 ms).
  - Unit tests `Smaa_*` (the passes in turn on the test harness's textures, each one's output the
    next one's texture as the renderer's targets store it): an edge drawn without anti-aliasing
    comes out at 0.22-0.27 of the error without it at 8 pixel steps (in linear light), across the
    edge only; on 32 pixel steps ultra 0.21, low 0.60; the edges at the threshold; the blending in
    linear light; flat pictures, an edge along the grid and the alpha stay.
- CMAA2 (`cmaa2`) is Intel's (F. Strugar, `third_party/cmaa2`, Apache-2.0): its four compute
  kernels of its `CMAA2.hlsl` as it is (`Shaders/AntiAliasCmaa2.hlsl`, the host `DrawCmaa2Passes`):
  - the edges and the shapes to process (the kernel compiled at each preset), the next dispatch's
    arguments, the shapes' new colours and those colours into the picture, the last two by
    ExecuteIndirect, bound by the wide root signature of `D3D12Compute.h` (one table of 4 SRVs and
    8 UAVs; CMAA2 has 7 UAVs). It anti-aliases a copy of the picture in place, a typeless texture:
    its sRGB view for the reads (it works in linear light), its UNORM view for the typed stores
    (converted to sRGB). Its working buffers have its sample's sizes (the shapes a quarter of the
    pixels, the colours half, the quads a sixth); its counters are cleared once, then by its own
    kernel for the next frame.
  - The `AntiAlias` program's pass writes the pixels CMAA2 changed, with the copy's alpha (CMAA2
    writes 0), and keeps the picture's border: CMAA2's loads outside the picture read black, so it
    darkens the corners. Without a typed UAV store of the back buffer's format (B8G8R8A8_UNORM)
    it is logged and acts as `none`.
  - `d3d12_AAQuality` is CMAA2's preset (`CMAA2_STATIC_QUALITY_PRESET`, edge threshold 0.15, 0.10,
    0.07, 0.05). Against the reference of the render tests' `aa` the error at the edges is
    0.63-0.69 of none's at High (0.63-0.75 at the four presets) and the textures keep 0.87-0.92 of
    their detail (0.85-0.97): about SMAA's edges (0.62-0.67) with a little more of the detail
    (0.85-0.91). At 3840 x 2160 its GPU section `anti-aliasing` (with the copies of the picture)
    takes 0.15, 0.16, 0.17 and 0.18 ms (low to ultra; SMAA 0.15-0.22 ms).
  - Unit tests `Cmaa2_*` (the renderer's kernels, run as `DrawCmaa2Passes` runs them by the test
    harness's `RunCmaa2`): an edge drawn without anti-aliasing comes out at 0.23 of the error
    without it at 8 pixel steps (in linear light), next to the edge only; flat pictures stay bit
    for bit but for the corners; an edge along the grid stays sharp; a faint edge is one at high
    and none at low; the third frame on the same buffers is the first; B8G8R8A8 as R8G8B8A8; the
    pixels it writes get the alpha 0.
- TAA (`taa`) is ours (`UpscalerTaa.cpp`, `Shaders/Taa.hlsli`, `Taa.hlsl`; after B. Karis's
  "High Quality Temporal Supersampling", 2014, and M. Salvi's variance clipping, 2016), an upscaler
  of the display's size (`IUpscaler`, as FSR's Native AA): with `d3d12_Upscaler none` the main
  view's scene draws into the upscaler's targets, jittered (Halton 2, 3 over 8 frames), with the
  motion vectors, the textures' mip bias -1 and the history's reset of the Upscaler section, and
  with the HDR scene's light (`d3d12_UpscaleHdr`) as FSR gets it. The path tracer gets those
  targets too. It is not drawn by `DrawPostAA`; its GPU section is the upscalers' `upscale`.
  - Three compute kernels: the motion of each pixel as the nearest (by the depth) of its 3 x 3's;
    the resolve, the history where the pixel was (Catmull-Rom, 5 taps) clipped towards the 3 x 3's
    box (YCoCg, mean +- 1.25 standard deviations within their range) and blended with the frame,
    each weighted by 1 / (1 + its luma): the frame's share 1 / (n + 1) after n frames of history
    (the history's alpha counts them, up to 32), at least 1 / 5, 1 / 7, 1 / 10, 1 / 14 by
    `d3d12_AAQuality` and up to 1.5 times it from a pixel's motion a frame on (the history,
    resampled every frame, blurs as it moves); then the sharpening, `d3d12_UpscaleSharpness` (0.5;
    0 turns it off and the resolve writes the picture). The history is two R16G16B16A16_FLOAT
    targets of the display's size in turn; the HDR light is blended exposed and compressed
    (c / (1 + max(r, g, b)), undone after).
  - The sharpening is AMD FidelityFX CAS (T. Lottes's Contrast Adaptive Sharpening,
    `third_party/fidelityfx-cas`, its `ffx_cas.h` as it is; `Shaders/TaaCas.hlsli`) in its
    sharpen-only form, 32-bit, with the 3 x 3's diagonals in its soft minimum and maximum
    (`CAS_BETTER_DIAGONALS`) and the exact reciprocals and square root (`CAS_GO_SLOWER`): a
    negative lobe of -1/8 to -1/5 around the pixel by its sharpness (`d3d12_UpscaleSharpness`,
    `CasSetup` on the CPU), scaled by the 3 x 3's distance to black and to white over its maximum,
    so that the textures' small contrasts get the most of it. It takes linear light from 0 to 1:
    the HDR light exposed and compressed as the history blends it, the LDR colours decoded (c^2.2)
    and encoded again. The history stays unsharpened; only the picture is. Its result, back in the
    picture's colours, is kept within the 3 x 3's range per channel (as FSR's RCAS limits its lobe):
    CAS's lobe takes a pixel a little past its neighbours, and near the top of the compressed HDR
    light undoing the compression made of that the light's many times (the sky at Training's start
    in its cave and the windows of its bunker went white, with a bloom over them).
  - Against the reference of the render tests' `aa` (Training's islands from the hill) the error at
    the edges is 0.66-0.73 of none's and the textures have 1.10 of its detail: CAS at 0.5 is a
    little sharper than the view supersampled 4 x (at 0.75 0.70-0.79 and 1.15, at 1 0.77-0.87 and
    1.22-1.23; without the sharpening 0.48-0.51 and 0.81-0.83; the sharpening of our own it
    replaced, a clamped unsharp mask that left the silhouettes, 0.65-0.68 and 1.03; CAS without the
    3 x 3's limit 0.70-0.76 and 1.35-1.39; FSR's Native AA at 0.5 0.69-0.75 and 0.95-1.04). In
    motion (DemoLevelAAMove, the camera turning over the islands) the edges are 0.70-0.77 of none's,
    the textures keep 0.79-0.84 of the reference's detail (at 0.75 0.83-0.88, at 1 0.90-0.94; none
    1.03-1.04; with our own sharpening 0.75-0.79) and the flicker's high part (`flicker.py` over 118
    frames) is 0.81 of none's (0.83 at 0.75, 0.86 at 1; 0.79 with our own sharpening): the sharper
    textures flicker more. At 3840 x 2160 its GPU section `upscale` takes 0.60-0.62 ms (0.59-0.61
    without the 3 x 3's limit in the same runs; 0.56 with the sharpening of our own and 0.49-0.51
    without the sharpening in others; FSR 4's Native AA 4.10-4.15; PerfSweep Swamp and Pier, with the
    scene's light for it in `hdr` and the motion vectors).
  - Unit tests `Taa_*`: the jitter covers the pixel; a still edge gathers the frames (under half
    of the error without it after 48 frames at every quality); a reset takes the frame; the history
    follows the motion (by the nearest of the 3 x 3) and is ignored off the picture; a stale history
    is clipped; the HDR light comes back as it was; a highlight of one frame weighs less; the
    sharpening is CAS's filter as `ffx_cas.h` writes it (worked out on the CPU, for the LDR colours
    and the HDR light, at the sharpness 0, 0.5 and 1) with `UpscalerTaa.cpp`'s constants
    (`CasSetup`), leaves flat pictures as they are to their border and raises a texture's
    contrast, more the higher the sharpness; the HDR light sharpened stays within its 3 x 3's range
    (a bright sky's texture and a cave's opening at the exposures 1 and 4).
- MSAA (`msaa2`, `msaa4`, `msaa8`, `Msaa.cpp`): from `BeginMainScene` to `EndMainScene` the main view draws into the
  device's multisampled colour, depth buffer and ambient light (`CD3D12Device::SetMsaaTargets`, `BeginMsaa`; the
  pipelines with `SPipelineKey::nSamples`). What comes after the scene, and what reads the depth buffer in it (the sun's
  mask by rays, the ambient occlusion, the water's copy of the scene), takes the main view's own targets, which get the
  samples resolved: the colour by `Msaa.hlsl`'s pass (`Msaa.hlsli`: the mean of the display's light of the samples, the
  HDR target's tone mapped at the scene's exposure for it, so that a highlight in one sample counts as much as the
  display shows it), the depth buffer the nearest sample, the ambient light by the GPU's resolve. Not with an upscaler's
  scene (FSR anti-aliases) nor the path tracer's (its G-buffer has one sample a pixel): logged once, then none.
  - Alpha to coverage (`d3d12_AlphaToCoverage`, 1 by default): the alpha-tested surfaces (the grass, the leaves, the
    gratings) cover the samples of their alpha sharpened to the test's edge in place of the test:
    `AlphaCoverage.hlsli`'s `CoverageAlpha`, saturate((a - ref) / fwidth(a) + 0.5) (B. Golus, "Anti-aliased Alpha Test:
    The Esoteric Alpha To Coverage", 2017), with `AlphaToCoverageEnable` in their pipelines
    (`SPipelineKey::bAlphaToCoverage`). `DrawGeometry` sets it for the general program's draws with `DF_ALPHATEST` (not
    `DF_ALPHATEST_LESS`) whose blend does not take the source's alpha, nor those whose alpha after the test is another
    (the far sprites' pictures, the detail layer, the caustics, the shadow's passes, the fog volumes, `DF2_SHADER_FX`),
    and negates the reference in their constants (the scene for rays keeps the draw's own). An edge steps through the
    samples as the surface's edge crosses the pixel, a leaf keeps its area (the share of each pixel the test keeps), a
    leaf's inside and a grating's holes stay as the test draws them. In `DemoLevelAA.ini`'s two views at 1280 x 720
    (render_tests' `aa` suite, 110 draws with it) MSAA 4x's error against the 4 x supersampled reference where alpha to
    coverage changes the frame (3.2 % and 7.7 % of it: the palms' leaves, the grass) is 0.74 / 0.79 of the alpha
    test's, the detail there 0.90 of the reference's (the test's steps 1.09: detail of their own), off those pixels as
    without it; at 3840 x 2160 with MSAA 4x the frame takes 0.05 / 0.12 ms more (5.48 -> 5.53, 5.43 -> 5.55 ms: the
    opaque objects 0.01 / 0.04, the ambient occlusion 0.01 / 0.05, the water 0.00 / 0.03). `0`: the test, the frames
    before (MSAA 4x's 0 px against step 294's build).
    The frame dump's line is `anti-aliasing: MSAA 4x, alpha to coverage: N draws`, r_DisplayInfo's row
    `MSAA 4x, alpha to coverage`.
  - Unit tests `Msaa_*`: a pixel of one surface's samples stays as it is; an edge's pixel is the mean of the display's
    light of its samples; a highlight in one sample counts as the display shows it; the alpha's plain mean; the
    nearest depth. `Msaa_CoverageEdge`, `Msaa_CoverageFlat`, `Msaa_CoverageRange`: alpha to coverage's alpha is the
    share of the pixel the test keeps where the alpha changes evenly across it (65536 points across the pixel, at the
    references 0.25, 0.5, 128 / 255, 0.75 and the alpha's change 0.002 to 2 a pixel), the test's own 0 or 1 where it
    does not change, and grows with the alpha.

## HDR

- The game's own `r_HDRRendering` (0 by default) turns on the HDR pipeline of the main view
  (`Hdr.cpp`, `Shaders/Hdr.hlsl`); it is read every frame. With it 0 the frames are those of the
  renderer without it (DemoCompare difference 0). The game's other `r_HDR*` variables are not
  read.
  - The main view's 3D scene (`BeginMainScene` in `BeginScene3D` to `EndMainScene` before the
    screen effects) draws into R16G16B16A16_FLOAT: an HDR target of the back buffer's size with
    the back buffer's depth buffer, or with an upscaler its scene colour. The materials keep the
    game's maths (gamma space), only nothing is clamped at 1 any more. The water's and the screen
    effects' copies of the scene are kept apart (`m_SceneCopies`). The water's reflection and the
    environment cubes are drawn in that format too (they come before `BeginMainScene`, so they go
    by `IsHdrOn`), so what they reflect keeps its light above white. The draws into that format
    get pixel shaders of their own (`XR_HDR` in `Shaders/Shaders.cmake`, by
    `SPipelineKey::nColorFormat`) with the steps below that only HDR has, so the general and water
    programs of the other targets stay those without HDR instruction for instruction.
  - The light of a colour is `pow(colour, 2.2)` up to 1 and linear above, `1 + 2.2 (colour - 1)`
    (`HdrDecode` in `Shaders/HdrLight.hlsli`): the game adds its colours in gamma space, and what it
    adds up above white (additive sprites over each other) is light added up. With the power
    above 1 too, the bloom of a fire is a ball.
  - Then, at the scene's size: a meter (by default a histogram of the light, below; with
    `d3d12_ExposureMeter 0` the centre-weighted mean log2 luminance of the light, reduced to
    1 x 1), the adaptation of the exposure towards it (a 1 x 1
    R32 EV, twice as fast towards a brighter scene; it starts at the meter's after a frame
    without the main view), the bloom (a soft-threshold prefilter against single bright pixels,
    6 levels from half the size down with 13 taps, back up with a 3 x 3 tent) and the resolve
    (the exposed light plus the bloom, tone mapped, gamma encoded) into the back buffer. The
    screen effects and the 2D stay LDR on the back buffer. In heat vision the scene is only
    clamped (its colours are a palette), and the exposure is held.
  - With an upscaler (since step 260, `d3d12_UpscaleHdr` 1, the default) the upscaler gets the
    scene's light, and what is done at the display's size comes after it: before it, at the render
    size, the meter and the adaptation, then a pass (`HDR_SCENE_LIGHT`) puts the light (decoded,
    with the volumetric light, which needs the depth of the render size) into an R16G16B16A16_FLOAT
    target of the render size, and another (`HDR_EXPOSURE_SCALE`) the exposure factor into a 1 x 1
    R32 target. FSR takes the light as HDR (`FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE`, with that
    exposure in place of its own automatic exposure) into an R16G16B16A16_FLOAT target of the
    display's size; the resolve of that (`HDR_LINEAR`: the light as it is, not decoded) does the
    bloom, the tone map, the colour grading and the highlight layer of the HDR output at the
    display's size, as without an upscaler (the debug views of `d3d12_UpscaleDebug` show the
    upscaler's picture or the depth as they are, and a dispatch that fails the light tone mapped at
    the render size, stretched). With `d3d12_UpscaleHdr 0` (the frames as up to step 259) the whole
    resolve comes before the upscaler, into an LDR target of the render size, which it gets as it
    would the scene: the bloom and the highlight layer were of the render size, and the layer of the
    jittered frame, which the upscaler does not gather, so the lamps of Research's hall flickered in
    HDR10 (the change of its highlights from a frame to the next 1.64 times that without an
    upscaler; with the light upscaled 0.82 to 0.86, FSR 3.1 and 4). At 4K with FSR Quality it costs
    the GPU 0.24 to 0.26 ms with FSR 4 (the `hdr` section 0.16 to 0.34 ms, `upscale` 0.1 ms more
    with 16-bit floats in and out) and 0.08 to 0.13 ms with FSR 3.1 (whose `upscale` takes 0.07 to
    0.1 ms less without its automatic exposure): FSR 4 171 to 165 FPS on Fort, FSR 3.1 313 to 304.
  - The meter's first level and each level of the bloom from one twice its size are compute
    kernels (`Shaders/Meter.hlsl`, `Shaders/Bloom.hlsl`; the maths the pixel shader does too in
    `Shaders/HdrShared.hlsli`). The meter's threads read its taps side by side and add them up in
    the pixel shader's order (the same bits). The bloom's group reads each tap its texels share
    once, at the uv of the texel centre or corner it is at: in a frame 0 to 2 pixels of the
    picture differ from the pixel shader's by 1 of 255. At 4K the meter's first level and the
    bloom's levels down take 0.14 to 0.17 ms, with pixel shaders 0.34 to 0.35.
  - The meter by a histogram (`d3d12_ExposureMeter 1`, the default since step 257;
    `Shaders/Exposure.hlsl`, the maths in `Shaders/HdrExposure.hlsli`): a kernel adds up about
    131 000 samples of the scene's light (one every 4 pixels across and down at 1920 x 1080, 8
    at 4K) into 64 bins of log2 from -12 to +8, by the luminance with the old meter's centre
    weight, and where the depth buffer is at the far plane (the sky) by the brightest channel;
    a second kernel of one group reads them (and clears them for the next frame) and writes the
    target EV into a 1 x 1 target, which the adaptation takes instead of the old meter's mean. The
    target is the key (the old one, -2.6) over the mean log2 luminance between
    `d3d12_ExposureLow` and `High` of the samples (5 and 95 %: a black border or the sun no longer
    move it), lowered where the sky is in the frame: the EV that puts the sky's
    `d3d12_ExposureHighlights` (90th) percentile at the tone map's knee is a ceiling, which
    counts from none at 2 % of the frame of sky to whole at 8 %, so a big sky is never pushed into
    the shoulder while a room seen with a little sky through its windows is still made brighter.
    The old meter's mean took every pixel and knew no sky: in River's forest with the sky between
    the crowns it gave +1.5 EV and the sky 2.4 times the game's light, white (now +0.4 EV, the
    sky 1.3 times, its bright part at the knee); under the palms of Training +0.38 EV and the sky
    1.16 times (now +0.03, 0.99); inside the hut and in Research's hall it still gives +1.5 EV.
    Without the kernels the old meter. The two kernels cost less than the old meter's levels they
    replace: at 3840×2160 the hdr section takes 0.30 instead of 0.32 ms at fort, 0.33 instead of
    0.36 at river, 0.32 instead of 0.34 at dam.
  - `d3d12_ToneMap`: 0 clamp, 1 soft shoulder (the game's picture up to `d3d12_ToneMapKnee`,
    then each channel rolls off smoothly to 1 where the original clips it), 2 ACES (fitted),
    3 AgX, 4 neutral (the default since step 256, `Shaders/HdrToneMap.hlsli`): the game's picture
    up to the knee too, then the brightest channel rolls off along the soft shoulder's curve,
    k + d (1 - e^(-(m - k) / d)) with d = P - k (slope 1 at the knee, P far above; P 1 for the
    picture, the peak's display value for the HDR output's highlight layer: a grey as with 1), and
    the colour is scaled with it as a whole, so its hue and saturation stay. The light far above
    white goes over to the soft shoulder of each channel, by 1 - 1 / (1 + s e^2) of the brightest
    channel's excess e (m less the rolled off m; s `d3d12_ToneMapDesaturation`, 3): next to
    nothing just above the knee, most where the game itself clips each channel: the sun white and
    fire yellow with a white core, as in the game's picture (with the hue kept the sun was a peach
    disc with a lilac rim and fire white in the middle and red around; towards white alone a
    white ball). The soft shoulder rolled the blue of a bright sky off and not its red and green:
    the sky between the huts of Training went cyan and white (its hue 15 degrees off the game's
    look, its saturation 0.71 of it; neutral 0.4 degrees, 0.99), and the sun was a white glare
    over a fifth of the frame. No toe: below the knee nothing changes. (Khronos' PBR Neutral's
    own shoulder, P - d^2 / (m - k + d), reaches P too slowly: the sun of the HDR output 380
    nits of a 455 nit peak.)
    `d3d12_Exposure` is a compensation in EV; `d3d12_AutoExposure 0` makes the exposure
    2^`d3d12_Exposure`; `d3d12_ExposureMin` / `d3d12_ExposureMax` limit the automatic one (EV,
    0 is the game's daylight; by default 0 and 1.5: the game's sky and daylight are made for the
    display, so only dark views get brighter) and `d3d12_ExposureSpeed` is its rate per second;
    `d3d12_ExposureMeter` picks the meter (1 the histogram, 0 the mean), `d3d12_ExposureLow` /
    `High` the histogram's window and `d3d12_ExposureHighlights` the sky's percentile kept at the
    knee (0: no ceiling).
    `d3d12_Bloom` is the bloom's strength (0 off), `d3d12_BloomThreshold` the exposed light it
    starts from.
    `d3d12_HDRDebug 1` shows the luminance in bands of 1 EV around middle grey (green), `2` the
    bloom alone, `3` the sky mask (white where the depth buffer is at the far plane: nothing but the
    sky drawn there; for the checks of the HDR test bench). The frame dump gets the settings, the
    meter's mean (the histogram's: its target, the sky's share and ceiling) and the adapted EV.
  - The HDR statistics (`HdrStats.cpp`, `Shaders/HdrStats.hlsl`), only while `d3d12_DisplayHDR`
    or `d3d12_HDRLog` is on (without them nothing runs and the frames are as before): after the
    adaptation a kernel adds up a histogram of the scene's light (a sample every 2 pixels across
    and down, before the exposure, as the meter takes it: 64 bins of log2 from -12 to +8, by the
    luminance as it is and by the meter's centre weight, and by the brightest channel), how much
    of it exposed is in the tone map's shoulder (above `d3d12_ToneMapKnee`) and at the game's
    white or above, and the frame's EV and meter's mean; when the output pass runs or a demo
    captures HDR10, another adds up the output's nits as the output pass gives them (the
    brightest channel of each sample: their mean, the most, how many above paper white and at the
    peak). The words are copied to a readback buffer per frame slot and read when the slot comes
    again: the numbers are `D3D12_FRAMES` frames old, and the GPU is never waited for.
    `d3d12_DisplayHDR 1` draws them as a panel at the top right, below `r_DisplayInfo`'s (in the
    demos' frames and clips too): the histogram (the bars by the square root of their samples,
    grey below the knee, orange in the shoulder, red at white and above; the marks: white yellow,
    the knee orange, the meter's mean green), the exposure with its target and range, the meter's
    mean and key (the histogram's: its window, and the sky's share of the frame and the EV that
    puts its bright part at the knee), the 50th, 95th and 99th percentiles of the exposed light in EV from white, the
    tone map, the shares in the shoulder and at white, the bloom and the volumetric light, the
    output and its levels, the display's (peak, full frame, SDR white), and with an HDR output or
    capture the measured nits: the frame's average and its MaxCLL, the shares above paper white
    and at the peak. `d3d12_HDRLog 1` writes a line of them per frame to
    `farcry1_testing1/XRenderD3D12_hdr.txt` (never committed): the engine's frame, the EV, the
    automatic exposure's target, the meter's mean, log2 of the scene's light at the percentiles
    1, 5, 25, 50, 75, 95 and 99, the shares in the shoulder and at white, the output's mean and
    most nits and the shares above paper white and at the peak, the output and its levels, the
    camera, the samples of each of the histogram's bins, and the histogram meter's sky ceiling and
    the sky's share. The log has the frames drawn while it is on, each with its own camera,
    written when they are read back (also after it is turned off); turned on again in a run, it is
    added to. The output's nits are summed in two words (the carries in the second), exact at any
    size and peak.
  - What the game draws as light sources is brighter in the HDR targets (`IsHdrTarget`: not in
    heat vision), each a factor of white's light, 1 the game's: `d3d12_HDRSun` (8) the sun's
    flare, `d3d12_HDRSky` (2) the sky towards the sun (`DF2_SKY_GLOW`, `HdrSkyGlow` in
    `Shaders/HdrLight.hlsli`: falling off with the cosine of the angle to the sun to the power
    that makes it half way at `d3d12_HDRSkyAngle`, 4 degrees since step 258: a small aureole
    round the sun's disc; 0 the power 32 of up to step 257, half way at 11.9 degrees, which made
    a quarter of the sky towards the sun white in SDR), `d3d12_HDRGlow` (3) what glows. The flares,
    coronas, procedural flares, additive particles and additive materials (not the stealth
    effect) get it in their cores (`DF2_HDR_CORE`: by the fourth power of their texture's
    brightness, so their soft edges stay the game's); the glow maps, which light a surface, all
    over.
  - Colour grading after the tone map, baked into a 32 x 32 x 32 table when a setting changes
    (`ColorGrading.cpp`, a strip of 1024 x 32): `d3d12_Temperature` (-1 cooler to 1 warmer, up
    to 100 mired from 6504 K, a Bradford adaptation) and `d3d12_Tint` (-1 green to 1 magenta),
    `d3d12_Contrast` (around middle grey), `d3d12_Saturation`, then `d3d12_ColorLut`, a 3D
    `.cube` file through the game's file system (a path in the game folder or its packs; an
    error goes to the log once per file name, seen with `log_Verbosity` 1 or more, and the frame
    dump gives the size read, 0 for none), mixed in by `d3d12_ColorLutAmount`. Neutral settings
    use no table (the frames are those without it).
  - HDR output to a display in HDR mode (Windows: Use HDR), `HdrOutput.cpp`: `d3d12_HDROutput`
    0 SDR (the default), 1 HDR10 (an R10G10B10A2 swap chain, PQ, Rec. 2020, with the HDR10
    metadata), 2 scRGB (R16G16B16A16_FLOAT, linear Rec. 709, 80 nits per 1). The frame is drawn
    as without it into an 8-bit back buffer, then the device's own frame target (the screen
    effects, the 2D with its masks through the back buffer's alpha, the screen copies, every read
    of the frame and `--video` stay as they are; with output 0 the frames are those of the
    renderer without it). Next to it a highlight layer (R16G16B16A16_FLOAT, cleared to 1, 1, 1,
    -1): the pass that puts the scene into the back buffer (the resolve, or the upscaler's
    composite) also writes the ratio of the scene's light on the HDR display to the light of its
    SDR colour per channel, and a fingerprint of that colour (`HighlightFingerprint`,
    `Shaders/HdrLight.hlsli`). The tone map goes on above its knee up to the peak instead of white
    (`HighlightRatio`, `Shaders/Hdr.hlsl`: the soft shoulder's excess, which ACES and AgX are
    mapped to), so below the knee the ratio is 1. With the neutral tone map (since step 258,
    `HdrOutputToneMap`, `Shaders/HdrToneMap.hlsli`) the output is the SDR picture up to the game's
    white, and the neutral tone map to the peak only from `d3d12_HDRHighlights` (2: twice the
    game's white) up, smoothly between: what the game shows as white or below, the sky, the clouds,
    what the sun lights, is at paper white at most also where the exposure lifted it into the
    shoulder, and the headroom is the highlights' (the sun, fire, lamps, glints). With the tone map
    to the peak from the knee (`d3d12_HDRHighlights` 1 or less, up to step 257) the sky between the
    huts of Training, which the exposure lifted by 0.3 EV, was 1.1 times the game's light in HDR10
    (now 0.97), and 9.6 % of the frame looking at the sun was above paper white (now 5.3 %, the
    sun's core still at the peak; of a 1000 nit peak with paper white at 203 it reaches 830 nits,
    910 with `d3d12_HDRSun` 12, which also widens the bloom round the sun in SDR, by up to 11
    levels). The output pass before Present (`HDR_OUTPUT`)
    gives each pixel the light of its SDR colour, times the ratio where the fingerprint still
    matches (nothing was drawn over the scene), times paper white, at most the peak. So the HUD,
    the menu and the screen effects show as SDR content does under Windows HDR, and the scene's
    highlights (the sun, fire, lamps) go up to the peak; night vision, the blur, the flash and
    motion blur take the highlights away where they change the pixels. `d3d12_HDRPaperWhite`
    (0: Windows' SDR content brightness of the display, `DisplayConfigGetDeviceInfo`) and
    `d3d12_HDRPeak` (0: the display's, `IDXGIOutput6::GetDesc1`) in nits; without the display's,
    203 and 1000. The display is looked at once a second (the window's monitor, a new DXGI factory
    when Windows' displays changed): HDR switched off in Windows, `r_HDRRendering 0` or a swap
    chain without the colour space and the output is SDR, the log says why (a warning). The
    frame dump gives the mode, the levels, the display's and how much of the frame has the scene
    and highlights. `d3d12_HDROutputDebug 1` shows the highlights (grey none, yellow to red at
    the peak, blue where something was drawn over the scene), `2` patches at paper white, twice
    and four times that and at the peak, to set `d3d12_HDRPeak` by eye, `3` the test of the
    menu's HDR page (since step 261, `PageTest`) in `d3d12_HDROutputTestRect` (left, top, right,
    bottom in the screen's 0..1; the default is the page's free area): at its left a square at
    10000 nits round a smaller one at the peak, which disappears into it once the peak is the
    display's (it shows nothing brighter: the lowest peak where it does is the display's real
    one), at its right patches at paper white, twice and four times that and at the peak (each at
    most the peak, as the game's light), black between. For the page, read only and not saved:
    `d3d12_HDROutputState` (SDR, HDR10 or scRGB, with `d3d12_HDROutput` "SDR: " and why) and the
    window's display as it reports itself, `d3d12_HDRDisplayOn` (1 in HDR mode, 0 in SDR, -1 not
    found), `d3d12_HDRDisplayPeak`, `d3d12_HDRDisplayFullFrame` and `d3d12_HDRDisplayWhite`
    (Windows' SDR content brightness), set through the console when they change (read a few times
    a second with the output off).
    The frame average limit (since step 259, `d3d12_HDRFrameAverage` 1, 0 off): a display holds
    its peak over a small part of the screen only, and an OLED dims the whole picture by itself
    when a frame's average is above its full frame light. Before the output pass two kernels
    (`Shaders/FrameAverage.hlsl`, the math in `Shaders/HdrFrameAverage.hlsli`) add up, every few
    pixels (about 65536 samples), each pixel's brightest channel (as HDR10's MaxFALL) of the SDR
    picture and of what the highlight layer adds, and give the factor of the layer's excess
    (1 + (ratio - 1) times it in the output pass and the statistics) that keeps the frame's
    average at `d3d12_HDRFrameAverage` times the full frame light (`d3d12_HDRFullFrame`, 0: the
    display's `MaxFullFrameLuminance`; none when it reports none, or at the peak or above): down
    within 0.1 s, back within 0.5 s, 1 exactly within the limit (the frames as without it). The
    SDR picture stays as it is (the limit takes the highlights only); the HDR10 metadata's MaxFALL
    is the limit, paper white at least (without it paper white). At 240 / 455 nits with a full
    frame of 254 it acts where the exposure is still on its way down from a darker view and the
    highlights are many: the sun's demo starts at 356 to 375 nits, held at 254 from its sixth
    frame (the sun's core at the peak again within half a second); a fire close up (163) and the
    sun in a settled view (198) are left alone. Its cost: 0.011 ms at 4K (the `hdr frame
    average` GPU section), none in SDR.
    `XRenderD3D12_ReadFrameHdr10` (exported, for DemoHost's `--video-hdr`) runs the output pass
    in HDR10 into a target of its own and reads it; from its first call the layer is kept
    whatever the display and `d3d12_HDROutput`.
  - The game's menu gets entries from `assets/scripts/02-hdr-options.patch` in Advanced Video
    Options: HDR, None / Soft / ACES / AgX for `r_HDRRendering` 0 or 1 with `d3d12_ToneMap` 4, 2
    or 3 (Soft also shows the clamp and the old soft shoulder, 0 and 1, set from the console),
    and next to the Basic Video Options button HDR output, SDR / HDR10 / scRGB for
    `d3d12_HDROutput` (choosing HDR10 or scRGB with HDR at None sets it to Soft); both greyed out
    without the variables, that is with another renderer. Since step 261
    (`assets/scripts/08-hdr-page.patch`) HDR output is a button that shows the output and opens
    the HDR page: HDR Output (with a line of what the display is), SDR Brightness
    (`d3d12_HDRPaperWhite`, Windows' at the slider's left end, else 80 to 480 nits), Peak
    Brightness (`d3d12_HDRPeak`, the display's at the left end, else 250 to 4000 nits), Frame
    Average Limit (`d3d12_HDRFrameAverage` 1 or 0), applied at once and saved when the page
    closes; while it is open `d3d12_HDROutputDebug` is 3 (as it was again after). Greyed out
    without `d3d12_HDROutputState` (another renderer: system.cfg may keep `d3d12_HDROutput`).

## Per pixel lighting

- `d3d12_PixelLighting` (1, read every frame) lights every lit material per pixel
  (`DF_PIXEL_LIGHTING`): the bump mapping templates as before (with `r_Quality_BumpMapping`), the
  others and the projected lights' passes with the lights as the game's vertex lighting takes
  them (`DF2_VERTEX_LIGHTS`: the same sum, the colours not clamped; a character's point light
  falls off as 1 / (a0 + a1 d), a static object's by the curve the original fits a line to per
  object, 2 (1 - smoothstep(0, 0.9 R, d)), `DF2_SMOOTH_FALLOFF`). The auto cube map materials keep
  the fixed function's lighting. An object with a light map also gets the lights of its light
  mask that the light map does not have (not the sun, not a `DLF_LM` light) per pixel, in place
  of the ambient under the light map (`DF2_LIGHTMAP_LIGHTS`), as the original adds its vertex
  lighting there. With 0 the frames are those of the renderer without it (DemoCompare difference
  0, e.g. `-DemoArgs '--exec-at 1 "d3d12_PixelLighting 0"'`): the new paths are branches of their
  own, so the old ones compile instruction for instruction as before (dxc reassociates the
  `fast` arithmetic of a branch that is changed).
- The tangent frames of bump mapping (`CLeafBuffer::CreateTangBuffer`, `TangentFrame.h`): the
  tangent and the binormal from the triangles' texture coordinates, the normal the vertex's own
  while it is within 60 degrees of its triangles' normal, else theirs (summed by the angle at the
  vertex, as the SDK's `TangentSpaceCalculation.h` takes it). The game's meshes carry one normal
  per position, and one exported without split vertices at a box's edges has the normal of one
  side on the corners of the next: on the shelves of Training's store (`shelve_indoor_*.cgf`,
  `TemplBumpSpec_GlossAlpha`) two thirds of the faces' corners (816 of 1224) have a normal at
  right angles to their face. With that normal in the frame the normal map's normal lay in the face's plane, and the
  path tracer, which turns it to the side of the triangle the camera sees, turned it up and down
  from a pixel to the next: noise all over the shelves (normals turned over on 5.8 % of a shelf's
  pixels, 0.6 % now). The raster lights them with no lamp, so its frames were the same. Meshes
  whose normals fit keep their frames bit for bit (unit tests `TangentFrame_*`).
- Normal maps and height maps (`NormalMapHeight.h`, `CShaderManager::LoadNormalMap`): a bump map
  whose name is no normal map's (not `*_ddn*` or `*.ddn*`) is a height map, made a normal map
  when it loads (the slopes 4 times the height's forward differences, the height in alpha). A
  material may name a normal map in its bump slot (`EFTT_BUMP`) and a height map in its normal
  map slot (`EFTT_NORMALMAP`, which the material loader fills from the displacement map):
  Research's cave rock has `cave_wall12_ddn` / `cave_22_ddn` and the grey `cave_bump`. The
  original makes the two one texture; traced, its texture is the normal map with the height map's
  slopes (the same slopes) added to x and y and normalised, the height map repeating over the
  normal map's texels (its 256 x 256 twice each way over 512 x 512), alpha the mean of the normal
  map's and the height (to the 8-bit level: correlation 0.99 per channel). `d3d12_NormalMapHeight`
  (1) does the same (`CTextureManager::LoadNormalWithHeight`; a height map alone in that slot is
  made a normal map as a height bump map is). Before, the grey height map was taken for a normal
  map: the bumps' normals were noise, the cave rock under Research's lamps 0.22-0.47 of the
  original's light and 1.6-2.6 times its local contrast (now 0.99-1.01 and 0.91). 0: as before,
  for the materials loaded after it is set. Unit tests `NormalMapHeight_*`.
- Dot3 light maps (`Shaders/LightmapLight.hlsli`): the original's light-map bump shaders take the
  light map's light times its direction map's alpha times N'.D mixed towards 1 by the light map's
  alpha (where that is 0 the bumps do not shade its light), plus the texture times the ambient
  colour. `d3d12_LightmapDot3` (1) mixes the same (0: N'.D alone, as before). On the specular bump
  templates (a template name with "spec": the cave's rock and the machines below Research's lamps;
  not the diffuse ones, such as Research's concrete walls, which the original draws with the light
  map's shader alone) a lamp baked into the light map (`DLF_LM`) in reach of the object gives a
  highlight there, added after the light map:
  2 times the lamp's specular colour times the material's, the texture's alpha (the gloss), the
  falloff of the original's attenuation texture where those shaders look it up (half of (1 - d / r)^2
  and half of min(1, 1.5 (1 - d / r))) and saturate(R.V)^32 with R the direction to the lamp
  reflected about N' (no N.L cut-off, as the original's). `d3d12_LightmapSpecular` (1) adds it
  (`AddLightmapLamps`: the lamps go into the draw's lights as `XR_LIGHT_LIGHTMAP_LAMP`, which light
  nothing else and cast no ray); 0: none, as before. Unit tests `LightmapLight_*`.
- A bump template's light colour (`d3d12_LightColorScale`, 1): the original's bump shaders take
  the light's colour times the material's and, where its largest channel is above 1, scale the
  whole colour down by it (its hue kept), then add the lights' terms and clamp only their sum.
  Training's bunker's lamp (1.81 2.22 1.31) on its concrete (0.59 0.65 0.65) gives 0.74 1 0.59;
  each channel of the light clamped to 1 first, as before, gave 0.59 0.65 0.65, the lamp's green
  lost. The highlight's colour likewise. 0: clamped as before.
- `r_Skin` (1, read every frame; not saved) lights the characters' faces and heads as skin
  (`Shaders/Skin.hlsli`; the textures under `objects/characters/` whose name says face or head, not
  their `_ddn`, `_ddp` or `_bump` maps: `PbrCompanion::IsSkin`, 28 in the paks; the physical
  materials do not tell it, most faces are `mat_default`). The model templates give a face the
  highlight of every material, saturate(R.V)^32 times the texture's alpha (a glossy plastic's),
  and `r_PbrRoughness` took that alpha for smoothness (a broad faint lobe all over: matte); under
  the paths' soft light a doll's face. Skin's highlight instead: GGX with Smith's visibility and
  Schlick's Fresnel of F0 0.028 (2.8 % head on, more towards grazing views), the roughness by the
  alpha, the artists' gloss: `r_SkinRoughness` (0.55) with none, `r_SkinRoughnessOily` (0.35) at
  0.3 (the lips, the nose's tip, the hair's strands), on to 0.25 at 1 (the eyes' wet rims), wider
  where the normal changes across the pixel; its share whole from the bare skin's gloss (0.08) up,
  less below (the stubble, the brows); times the light's scale and the material's specular colour.
  Its light wrapped a little round the line where the light passes it, ((N.L + w) / (1 + w))^(1 +
  w) with w red 0.12, green 0.04, blue 0.02 times `r_SkinScatter` (1; 0 lambert's): the light under
  the skin, red the farthest (3 times as far made Jack's face orange under Training's cave lamps).
  Neither `r_PbrRoughness` nor the reflections by rays on skin. The path tracer the same: the
  G-buffer's light map target's alpha has the skin's roughness (`PathGBufferSkin`, 1 elsewhere),
  `CSSpecular` its lobe, the composite the sun's wrap. The frame dump's ` skin (r_Skin) roughness
  M to O scatter S` and the item line `skin (r_Skin 1)`. 0: the frames before.

## Parallax and generated normals (RayMaster)

- A texture tool (a local one, not in this repository) makes, for the game's textures, a material
  map `<texture>_pbr_m.dds` (BC3: red the roughness, green the metalness, blue the occlusion,
  alpha the height: 0 a hollow, 1 a bump, 0.5 flat; with its mips) and, where the material has no
  normal map of its own, a normal map `<texture>_pbr_n.dds` (BC1, the game's green), and the
  roughness alone `<texture>_pbr_r.dds` (BC4: in the material map's BC3 the red shares each block's
  two colours with the metalness and the occlusion, 5 bits and 4 levels, its error as large as the
  roughness's own detail). They go into the game's folder `RayMaster`, at the texture's path in the
  paks:
  `RayMaster/textures/terrain/stone_01_pbr_m.dds` for `textures/terrain/stone_01.dds`
  (`PbrCompanion.h`'s `Path`: the path the texture loaded from, lower case, its extension off).
  Without the folder nothing changes.
- `r_Parallax` (1) draws a lit opaque surface with parallax occlusion mapping from the material
  map's height (`Shaders/Parallax.hlsli`): the textures at uv0 (the diffuse, the normal map, the
  detail layers that move with it) are read where the view meets the height, `r_ParallaxDepth`
  (0.03) deep in the texture's coordinates (a share of its side; DF_TEXMATRIX's turn and stretch
  taken into the shift, its scale out). The ray from the height 1 down to 0 in 8 to 24 steps (more
  the more slanting the view), the first step under the height refined by the secant, as
  Tatarchuk's (2006). The height 0.5 is not offset: on a surface's flat parts the light map, the
  decals and the depth, all the geometry's, stay where they were. The depth less where the view is
  under 0.2 of the normal (none edge on), and none where it is under a pixel on the screen (from 4
  pixels on all of it): far off the march costs nothing. The textures read by the geometry's uv
  gradients, not the offset's (no seams where the offset jumps).
- `r_PbrNormals` (1) gives a material without a normal map (and without the bump offset) the
  generated one: the bump mapping's own path (`DF_BUMP`, its tangent frames), the normal map in
  place of none. A diffuse texture shared with a material that has its own normal map keeps that.
- `r_ParallaxMaterials` (1) gives each texture its own depth: `r_ParallaxDepth` times its share in
  the table `RayMaster/pbr_depth.tsv` beside the maps, a row `<texture>`, tab, `<share>` (a note
  after another tab), the texture as the maps' path has it (its pak path, lower case, `/`
  separated, no extension), `#` a comment, the share 0 to 4 (`PbrCompanion.h`'s
  `ParseDepthTable`). The texture tool writes it by the physical material most of the texture's
  materials have: stone, rock, brick and concrete 1; sand, mud and dirt 0.7; wood 0.6; cloth and
  canvas 0.5; ceramic 0.4; leaves, grass and rubber 0.3; metal and plastic 0.25 (the height
  networks make their panels, screws and print into bumps); glass, water and flesh 0 (none). A
  material of no class (the weapons, the barrels and the crates are `mat_default`) goes by the
  words of its path (`weapons`, `barrel`, `vehicles` metal; `crate`, `palette` wood), else 0.5.
  The table is read once, when the first map is found; a texture not in it, or no table, gets 1.
  Edit it and load the level again to tune a material. Of the game's 2286 textures with maps 290
  get 1, 26 0.7, 193 0.6, 405 0.5, 10 0.4, 189 0.3, 1126 0.25 and 47 none.
- `r_ParallaxFilter` (4) has the march read the height averaged over that many texels of the
  height map at least, and over its step where that is longer (each of the pixel's uv gradients
  that long, its direction kept: a coarser level of the map). The blocks and the joints stay in
  relief, and the height map's fine noise is no longer cut 3 % of the texture deep: at a slanting
  view that noise stood up out of a flat stone as spikes (the moss on River's temple threshold).
  The textures are still read at the pixel's footprint. 0 reads the height at the pixel, as
  before; 1 to 64. Filtering by the march's step alone (its gradient stretched along the march)
  changed nothing there: the spikes are the map's noise, not the steps missing it.
- `r_PbrRoughness` (1) gives the highlights of the specular materials lit per pixel (the specular
  templates' `DF_SPECULAR`: the sun's, the lamps', the light map's lamps') the lobe of their
  texture's roughness, the roughness map's red (else the material map's) where the view meets the
  texture, in place of the templates' `saturate(R.V)^32` for all (`Shaders/PbrSpecular.hlsli`):
  the power `n = (2 / alpha^2 - 2) / 4` of `alpha = r^2` (Blinn-Phong's power of N.H for that
  roughness; R.V's lobe as wide at a quarter of it), 1 to 512, its height `(n + 1) / 33` times the
  templates' (the lobe's integral about R the templates': as much light reflected, a rough lobe
  broad and faint, a smooth one narrow and bright; the templates' lobe is that of r = 0.35). Where
  the normal changes across the pixel the lobe widens (alpha^2 plus at most 0.18 of the normal's
  change, Tokuyoshi and Kaplanyan 2019): no sparkle of the narrow lobes over bumps far off. A
  character's too (no tangent frames needed), not the scene for rays (the paths' highlights are
  their own). The texture tool's roughness of a specular material without gloss in its alpha (the
  renderer's highlight `2 * spec` all over) has the mean a gloss alpha of 1 would give
  (`lerp(0.85, 0.35, min(1, 2 * spec))`) and the network's detail about it: the highlight the
  artists gave the material kept, the network's wear and dirt in it (311 of the 2286 textures).
  Treehouse's lab floor plates (`textures/ww2style/floor78378`, metal by their physics, the
  highlight 0.90) had 0.56 from the network alone, a dull broad sheen in place of the plates'
  shine; now 0.35 on average (0.31-0.39), the raised treads smoother than the rusty pits.
- The maps are looked up when a scene first draws a material (`Scene.cpp`'s `WantsPbrMaps`,
  `CTextureManager::FindPbrCompanions`), once for each texture (a miss is remembered), and only
  while one of `r_Parallax`, `r_PbrNormals` and `r_PbrRoughness` is 1 (a character's for the
  roughness alone; the roughness map only while that is 1): with all three 0 from the start no map
  loads and the frames are the build before's. A height map flat all over (each texel within 1 of 128) takes no parallax. A map
  without its mips (to 4 texels), of another aspect than its texture, or of another format, is
  left out with a warning in the log. `r_TexResolution` and `r_TexBumpResolution` drop their top
  levels as they do the textures'.
- Parallax not with: the alpha tested and the blended surfaces, the decals, the characters (their
  tangents the bind pose's), the world texgen, the cube and array textures, the point sampled
  textures, the shader effects (`DF2_SHADER_FX`), the bump offset, the glow falloff and the bump
  refraction. The generated normals not on the characters, nor where the bump offset reads the
  normal map's slot.
- Limits: no silhouettes, no change of the depth (the shadows, the rays, the ambient occlusion and
  the decals take the flat surface), no self shadowing; the path tracer's first hit takes the
  offset texture and normal (its G-buffer is the raster's pass), its later hits the geometry's uv.
  A large `r_ParallaxDepth` stretches the steep edges of a height map (grout lines at 0.05).
- With a frame dump an item with the maps gets the line `RayMaster maps: material '...' normals
  '...' depth share S`, a draw ` parallax (r_Parallax) depth D filter N`, ` normals
  (r_PbrNormals)` or ` roughness (r_PbrRoughness) map|material map specular (r g b) gloss alpha|1`.
- The renderer reads BC4 files too (`ATI1`, `BC4U`; none of the game's own): the one channel in red,
  green and blue 0, alpha 1, as the GPU reads them.
- `r_TexUpscaled` (1) gives a texture of the paks its upscaled picture where the folder `RayMaster`
  has it: `RayMaster/<texture>.dds` beside the maps (the texture's path as the maps have it; `.dds`
  for a `.tga` and a `.jpg` too), made by the texture tool from the game's own (a cleanup network
  for the compression's blocks, 1xDeJPG, then 4x-UltraSharpV2 Lite, to twice the side, at most 4096,
  compressed as the texture was, with all its levels). Only while `RayMaster/upscaled_manifest.tsv`
  is there (looked for once); not the cube maps, the height maps made into normal maps, nor a normal
  map with its height map (their sizes must match). A file that is not one face wider than the
  texture, of its aspect, with levels down as small as the texture's (or to 4 x 4), is left out
  with a warning in the log (`PbrCompanion.h`'s `IsUpscaledUsable`). The engine is told the
  texture's own sides (`GetOriginalWidth`, `GetOriginalHeight`: the interface maps its texel
  rectangles by them), smaller by the levels `r_TexResolution` drops. Read as a texture loads: a
  change takes effect from the next level's load; on the game's command line (`"r_TexUpscaled 0"`)
  it counts from the start (`CommandLine.h`: the engine sets the line's variables only after the
  menu's and the shaders' textures have loaded). The tool leaves out the light maps and the levels'
  files, the interface, the `_bump` height maps and the textures of 32 px or less: of the game's
  8829 textures 5725, 2.42 GB. The frame dump's line `upscaled textures (r_TexUpscaled): N of M
  textures, A MB of B MB`, an item's `upscaled (r_TexUpscaled): W x H, its file's own w x h`; the
  statistics' line `Textures N (A MB), upscaled N (B MB)`. 0: the paks' own pictures (the frames
  before).
- Cost (RX 7900 XTX, 3840 x 2160, the raster without the upscaler, `Parallax_river.ini`'s views): in
  River's temple the opaque list 0.65-0.72 to 0.77-0.84 ms, the frame 2.79-2.95 to 2.91-3.05 ms;
  from outside, 37 m off, the same (the depth under a pixel: no march). `r_ParallaxMaterials` and
  `r_ParallaxFilter` add 0.04-0.12 ms to the temple's opaque list (0.74-0.81 to 0.81-0.92 ms, the
  frame 2.83-3.03 to 2.92-3.11 ms), the filter: over the smoother height the ray goes more steps
  before it meets it. `r_PbrRoughness` in Treehouse's corridor end (`Parallax_treehouse_pan.ini`,
  its metal floor plates under the lamps): the opaque list 0.89-0.92 to 0.90-0.93 ms, the frame the
  same (2.68-2.71 ms; with the player's settings 3.53 ms).
- Tests: `render_tests` suite `parallax` (`Parallax_river.ini`, River's temple by day at 1080p, the
  exposure fixed). Both on change 0.17-0.26 of each view in the temple and 0.0004 of the view from
  outside, with no more black pixels than both off (the grout's dark bottoms seen deeper: at most
  0.0003 of a frame). Against the generated normals alone the frames change more the deeper the
  depth: 2.7, 4.2 and 5.3 levels on average at 0.015, 0.03 and 0.05. The frame dump has 94 draws
  with parallax and 40 with the generated normals. Both 0 give the build before's frames (0 px in
  the nearest of 3 x 3 runs; the PerfSweep frames of Pier, River and Swamp 0-1 px in the nearest of
  3 x 4, the build before's own runs as far apart). The unit tests `Pbr_*` check the path rule,
  the BC3 alpha decoding of both modes, the flat height and the mip chain; `Parallax_*` the march
  (no offset at the reference height or straight on, a constant height's shift, a ramp's and a
  step's hit, grazing views bounded), the fade, the texture matrix and the steps.
- Tests of the depth by material and the filter (the suite `parallax` too): each of the 94 draws
  with parallax in the frame dump has `r_ParallaxDepth` times its material's share of the table
  (0.25, 0.6 and 1 there) and the filter 4; at the temple's threshold (view 20) the moss's
  streaks (its luma's steps across over those along the view) exceed both maps off's by 0.36 of
  what they do without the filter, the relief kept (the mean change against the normals alone 4.09
  levels, 4.17 without it); `r_ParallaxMaterials 0` and `r_ParallaxFilter 0` give the build
  before's frames (0 px in the nearest of 3 x 3 runs there; the PerfSweep frames of Pier and
  River 0 px in the nearest of 3 x 4, Swamp's 1 and 8 px of 1 level at its frames 181 and 661,
  its frame 1 1 to 2 levels over the frame, another state of the glare: the build before's own
  runs 893712 px apart there). Where the materials show: Treehouse's lab corridor (its metal
  tread plates, share 0.25) changes 0.076 of the frame, Training's hut (wood 0.6) 0.082,
  Research's pump hall 0.028. The unit tests `Pbr_DepthTable` (the table's rows, comments, Windows line ends, numbers
  left out or clamped, the last row counting) and `Parallax_MarchGrad` (the gradients at least the
  texels and the step, their directions kept).
- Tests of the roughness: `render_tests` suite `roughness` (`Parallax_treehouse_pan.ini` at 1080p,
  the exposure fixed). The light on the ceiling above the lamp there is not the same from run to
  run (two runs of one build 4-10 % of the frame apart): the checks take the pixels 16 px or more
  from any that two runs of one build have apart. The frame dump has 19 draws with the roughness
  map and none with the material map's red; on against off 0.06 of the steady pixels change, their
  mean luma 0.2 levels apart (as much light reflected), 0.00001 of a frame gone black; `r_PbrRoughness 0` gives
  the build before's frames (0 px of the 0.71 of a frame steady in both builds; the PerfSweep
  frames of Pier, River and Swamp 0 px in the nearest of 2 x 2 runs at Pier's frame 181, River's 1
  and 181 and Swamp's 661, the others as far apart as the build before's own runs). The unit tests
  `Pbr_SpecTemplate` (r = 0.35 the templates' lobe), `Pbr_SpecMonotonic`, `Pbr_SpecBounds`,
  `Pbr_SpecEnergy` (the lobe's integral about R the templates' wherever its power is not clamped) and
  `Pbr_SpecNormalChange` (the lobe widened where the normal changes, by at most 0.18).
- `d3d12_RTPbrReflections` (Ray tracing) reflects the scene by rays on the materials their material
  map makes a metal or smooth: the roughness the highlights' (`r_PbrRoughness`'s map) or the
  material map's red, the metalness its green. The texture tool gives a metal's specular template
  without gloss in its alpha the metalness `min(1, 2 spec)` all over, as a gloss alpha of 1 would
  (214 of the 2286 textures, 324 by their gloss alpha; before 0: Treehouse's lab floor plates).
- Tests of the reflections: `render_tests` suite `pbrrefl` (`Parallax_treehouse_pan.ini` at 1080p, the
  exposure fixed, no anti-aliasing): the frame dump at 199 has 32 first passes reflecting by rays, all
  with the last frame's picture, none with the quads' rays (all 32 with `d3d12_RTPbrQuad 1`), and 14
  light passes; on against off 0.46 of the steady pixels change, their mean luma 3.5 levels apart,
  none gone black; where off has a highlight (16 levels over its 15 x 15 mean) on keeps 0.89 of its
  luma; the hits tinted green 0.24 of a frame (0.31 with TAA, 0.24 with MSAA), red without the picture
  0.13; with TAA the quads' rays keep 0.95 of a ray a pixel's detail in the frame's lower half, their
  luma 1.3 levels apart; `d3d12_RTPbrReflections 0` gives the build before's frames (0 px of the
  steady pixels; the PerfSweep frames 0 px in the nearest of 6 x 2 runs at River's three, Pier's 181
  and Swamp's 1 and 661, Pier's 1 and Swamp's 181 6 and 9 px of 1 level, Pier's 661 1.34 million px of
  1 level, the sky and the far sea in a state none of the build before's 6 runs fell into, one of 4
  runs of this step's first build did, theirs in 3 states up to 3.1 million px apart). The unit tests
  `Pbr_ReflectMirror`, `Pbr_ReflectAbove`, `Pbr_ReflectSpread`, `Pbr_EnvBrdf`, `Pbr_ReflectWeight`,
  `Pbr_PrevReproject`, `Pbr_PrevShare`, `Pbr_QuadLane`, `Pbr_QuadStart`, `Pbr_QuadShares`. The debug
  layer no new messages (with TAA and with MSAA).
- `r_PbrAO` (1 by default, read every frame; not saved) takes the reflections by rays
  (`d3d12_RTPbrReflections`) away where the texture's ambient occlusion (the material map's blue)
  hides them: the share of the lobe in the open cone (`PbrSpecOcclusion`, Lagarde and de Rousiers
  2014: `saturate((N.V + ao) ^ (2 ^ (-16 alpha - 1)) - 1 + ao)`; 1 where nothing occludes, about the
  occlusion for a rough lobe, more than it for a narrow one seen from above, less towards the grazing
  view), a miss's ambient light as much: the pits between the lab floor plates' treads and the holes
  of the wall panels' mesh no longer reflect the corridor as their treads do. The frame dump's `
  occlusion (r_PbrAO) A` (the map's mean). Not the ambient light, the light maps' and the rays' GI:
  the game's textures have their hollows painted dark already (the map's occlusion made from them),
  they would darken twice. 0 the frames before. At 3840 x 2160 (RX 7900 XTX,
  `Parallax_treehouse_pan.ini`, the raster with the rays' reflections) the frame 5.04-5.08 ->
  5.03-5.07 ms, with the player's settings 5.86-5.90 -> 5.86-5.94 ms: the same (the map's blue read
  with its red and green). Tests: `render_tests` suite `pbrao` (`pbrrefl`'s scene and settings): the
  frame dump at 199 has all 32 first passes reflecting by rays occluded; in the frame's lower half on
  against each of 4 runs without (`r_PbrAO 0` twice, the build before twice) 0.057 of the pixels more
  darker by more than 2 levels than any other of them has against it, none more brighter, the mean
  luma there 0.8 levels apart (with the reflections the ceiling's lamp light, different from run to
  run, is in them all over the floor and the walls); `r_PbrAO 0` gives the build before's frames (0 px
  of the pixels steady in both builds, 0.30 of a frame; the PerfSweep frames 0 px in the nearest of 2
  x 2 runs at River's three, Pier's 181 and Swamp's 1, Pier's and Swamp's 661 64 and 1 px of 1 level,
  Swamp's 181 4803 px of at most 7 levels scattered (the rays' GI; the build before's own runs 219486
  px apart), Pier's 1 12186 px, one of the level's first frame's states). The unit test
  `Pbr_SpecOcclusion` (the reference, bounds, 1 with nothing occluded, the occlusion for a rough lobe,
  more open more light).

## Depth prepass

- `d3d12_DepthPrepass` (1, read every frame) draws the main view's opaque surfaces that write the
  depth and discard nothing (the general program's opaque, vertex colour, terrain and sea floor
  draws without the alpha test) into the depth buffer before the opaque list, without a pixel
  shader or colour writes; the list then shades only the pixels it keeps. At 4K the list shaded
  4 to 6 times the screen's pixels outdoors (Regulator's time demo: 35 to 50 million pixel
  shader invocations a frame, 14 million indoors). The list's depth test is unchanged: whatever
  the order, a pixel ends with the nearest surface, and each surface gets the same depth in both
  passes from the same vertex shader, matrix and vertex streams (in a pipeline without an
  object's tangents the GPU's compiler put some of its pixels a step nearer than with them), so
  the frames are those without it (the PerfSweep frames of ten levels the same to the bit). The
  prepass costs the CPU the items' transforms and textures once more, not their lights.
  Time demos: Training 406 to 425 FPS (its opaque list 0.52 to 0.32 ms in frames 600 to 1200),
  Volcano 453 to 479, Research 562 to 600, Regulator 369 to 381 (0.63 to 0.90 ms to 0.51 to 0.69);
  the prepass itself 0.02 to 0.04 ms, the CPU's `EF_EndEf3D` 0.03 to 0.15 ms more.

## Sun shadow cascades

- `d3d12_ShadowCascades` (4, 0 to 4) gives the sun's cascaded shadow maps around the camera while
  the game's shadows are on (`e_shadow_maps`, as the high and very high presets have them), in a
  main view with the sun (`Cascades.cpp`). The view's depth up to `d3d12_ShadowDistance` (120 m)
  is split mostly logarithmically; each cascade is a slice of `d3d12_ShadowCascadeSize` (2048)
  seen from the sun, a square around the sphere of its part of the frustum, moved in whole
  texels so that the shadows do not crawl. There are two sets of slices:
  - the full set, with all that casts: the static objects and entities that
    `I3DEngine::DrawLowDetail` gives a camera at the view's position looking towards the sun,
    170 degrees wide (the engine culls by its own frustum of the camera it is given, so the
    casters between the view and the sun come from there), the main view's own items (the
    terrain, what is indoors) and the live casters. The camera stays at the view's position and
    does not draw the terrain: a camera 30 m above the view changed the main view's terrain
    textures and some objects in the frames after it, even after the cascades were turned off
    (the engine seems to keep what it chose for the last camera); at the view's position without the
    terrain, the frames differ by one level along one rock's edge;
  - the live set, with the casters the engine names for its shadow maps in the frame
    (characters, vehicles, the vegetation that casts), each only where nothing in the full set
    is nearer to the sun.

  Each part of the full set is drawn once, into all the slices its sphere reaches: an instance
  per slice, which the vertex shader sends to its slice (`SV_RenderTargetArrayIndex`,
  `General_CascadeInstances_VS`). The depth is the same as drawn slice by slice (DemoLevelSurvey
  with `d3d12_CascadeInstancing 0`, which draws slice by slice, and 1: DemoCompare difference 0;
  elsewhere only what the game animates differently from run to run) with far fewer draws
  (Training's time demo, frame 2600: 1,677 in the frame instead of 3,120). The live set is
  drawn slice by slice, each compared with the full set's. A part goes only into the slices
  where its shadow (its sphere drawn out away from the sun) falls on the slice's receivers: seen
  from the sun, the outline of the part of the view's frustum that takes its shadow from the
  slice, with the filter's reach around it; elsewhere a slice is not read, so the frames are the
  same (Regulator's time demo outdoors: some 400 draws and 0.11 ms of the GPU less a frame, 301
  to 311 FPS; `d3d12_ShadowDebug 1` shows the slices without the casters outside).

  Lit objects take the sun from the full set. Light maps and the terrain, whose light has the
  static world's shadows already, take it from the live set; the terrain is darkened to the
  colour of the engine's shadows on it (`EntityShadowOnTerrain`, which fade out where the
  cascades reach; a pass whose triangles all lie before the last cascade's blend is not drawn:
  Dam's spawn, where the bushes around the camera have shadow maps, 1 ms of the GPU less).
  Beyond the cascades the objects' shadow maps work as before. A lit object in the full set's
  shadow is darkened as the terrain is, not to its ambient light alone: its light there is at least
  its light in the sun times the colour of the engine's shadows on the terrain (`DF2_SHADOW_TINT`;
  that colour about 0.5 on Training). The original has no shadow of the static world on its
  objects, and with the ambient alone a lit object under a roof was nearly black: the papers on the
  floor of Training's hut 7 of 255 where the original has 46, now 15, as dark as the ground in the
  shadow beside the hut.
  `d3d12_StaticCasters` (1) puts the full set's static casters into the live set too, and into the
  rays' live casters (`XR_RAY_LIVE`): the parts of the objects the engine makes shadow maps for at
  the level's load (`MakeShadowMapFrustum`'s objects, every level of detail's leaf buffer: the
  palms and the trees that cast; not the grass and the bushes it makes none for). The engine names
  such an object for its receivers near the camera only: a palm on Training's beach for the player
  standing in its shadow or a mercenary beside it, as well as for the terrain. Its sharp shadow on
  the ground was there while they were near and gone a few metres farther off, and back again as
  one walked up to it (the user: 8 to 10 m); the terrain's own light has only a blurred one of it.
  0 gives the frames before. At 3840 x 2160 (RX 7900 XTX, the rays' sun, the camera backing away
  along the beach) the rays' sun shadows 0.39 -> 0.44 ms, the cascades 0.13 -> 0.14 ms, the GPU's
  frame 6.22 -> 6.27 ms; with the cascades alone the frame the same.
  `d3d12_ShadowSoftness` (0.05 m) is the filter's radius (at least a texel and a half).
  `d3d12_ShadowDebug` 1 also shows the slices, 3 colours the cascades, 4 shows the full set's
  shadow in red and the live set's in green. The frame dump's line `static casters
  (d3d12_StaticCasters): N parts of the full set in the live set too, of M leaf buffers` counts
  them (158 of 47 on Training's beach). Tests: the suite `casters` (`ShadowRange_training.ini`, the
  camera backing away from a palm on Training's beach; `ShadowToggle_training.ini`, the player
  walking up to it and away): on against off at 14 to 36 m 0.039 of the frame darker by more than 8
  levels with the cascades and with the rays, 0.049 at the walk's middle, none brighter; off the
  build before's frames (0 px in the nearest of 2 x 2 runs); the PerfSweep frames with 0 on the
  game's command line (before the level loads) 0 px in the nearest of 3 x 2 runs at River's three
  and Pier's frame 181 and Swamp's frame 1, 1 px of 1 level at Swamp's 181 and 661, Pier's frames
  1 and 661 as far apart as the build before's own runs. With `d3d12_ShadowCascades 0` (from the start,
  `--exec-at 0`) or `e_shadow_maps 0` the frames are those of the renderer without them
  (DemoCompare difference 0 for the former). The cascades' code is in pixel shaders of its own
  (`XR_CASCADES`, for the draws with their flags): in the general one, with dxc's code for the old
  paths unchanged, the GPU's compiler still changed a few pixels by a level.

## Ambient occlusion

- `d3d12_AO` is the screen space ambient occlusion of the main view: `none` (the frames are those
  of the renderer without it, DemoCompare difference 0), `gtao` (Intel XeGTAO
  1.30, `AOXeGtao.cpp` with `Shaders/XeGtao.hlsl` on our compute passes, `D3D12Compute.h`),
  `cacao` (the default; AMD FidelityFX CACAO 1.2 through its own Direct3D 12 host, `AOCacao.cpp`, with a
  descriptor heap and root signatures of its own; the build compiles its shaders with our dxc; at 3840 x 2160
  with its quality 2 some 0.65 to 0.85 ms of the GPU, outdoors and in) or `rt` (by rays among the
  scene, not from the screen: see Ray tracing; on a device without DXR 1.1 it acts as `none`). An
  unknown one is logged once and acts as `none`. `d3d12_RTGI` (see Ray tracing) puts the ambient
  light by rays in place of any of them. After the opaque list (`AmbientOcclusion.cpp`),
  before the weapon and the water (which then refracts the occluded bottom), the backend computes
  the visibility from the depth buffer (the back buffer's depth is `R24G8_TYPELESS` for that; both
  make the normals from the depth), `Shaders/AoResolve.hlsl` turns it into a factor (1 for the
  sky, faded out with the fog: `lerp(1, visibility ^ (strength / 2.2), fog)`), and a blend of
  `DESTCOLOR, ZERO` multiplies the scene drawn so far by it (colour only; the alpha keeps the HUD's
  masks). Not in reflections, the sprites, shadow and cube passes, nor with the heat vision.
  That is `d3d12_AOApply 0` (the frames of the build before it, DemoCompare difference 0); 1 (the
  default) darkens the ambient light alone, not the sun's and the lamps' direct light. The opaque
  list then draws with a target of the main view's size (`R11G11B10_FLOAT`, cleared to 0) bound as
  the fourth (`CD3D12Device::BindAmbient`, `SPipelineKey::bAmbient`; without an upscaler after two
  null targets), where the general program's pixel shaders with `XR_AMBIENT` (`General.hlsl`, their
  own programs: dxc's code of the others is unchanged) write how much of the colour is the ambient
  light's: per pixel the lights' sums with the ambient colour less those without it, under a light
  map the colour times the ambient colour; for the terrain and the vegetation, whose light is baked
  into them, the colour times the share the shadow leaves (`DF2_AO_BAKED`: the colour of the
  engine's shadows on the terrain in this frame, else `d3d12_AOBakedShare`, 0.5); all of it for
  the templates lit by the ambient colour alone and the terrain's shadow (`DF2_AO_ALL`); fogged.
  It blends as the colour, so the lamps' passes add none and the detail layers scale it. The
  light taken, `(1 - visibility ^ strength) * ambient` (at float16 precision; the strength not
  divided by 2.2: the game adds its light in gamma space), is made by the draw that takes it from
  the scene, a blend of `ONE, ONE` with `REV_SUBTRACT` (`SPipelineKey::nBlendOp`; `Post.hlsl`'s
  `POST_AO_TAKEN`, with the depth buffer bound read only for its pixel shader,
  `CD3D12Device::SetDepthReadOnly`); with `d3d12_AODebug` the resolve writes it into an
  `R16G16B16A16_FLOAT` target first, for the debug view (0.1 ms more at 3840 x 2160). A light map's light
  counts as direct, so in the light-mapped interiors it darkens little; the target costs some
  0.03 ms of the opaque list at 2560 x 1440.
  `d3d12_AOQuality` 0 to 3 (2: XeGTAO Low to Ultra, CACAO LOW to HIGHEST; `rt` 0 to 2 a ray for
  each 2 x 2 pixels, 3 for each pixel), `d3d12_AORadius` (1 m),
  `d3d12_AOStrength` (1, the exponent of the visibility), `d3d12_AODenoise` (2: XeGTAO's denoise
  passes 0 to 3, CACAO's blur passes 1 to 8, `rt` 0 without its history and filter, else with
  them); with an upscaler XeGTAO's noise and CACAO's kernel change from frame to frame for it to
  gather. All are read every frame. `d3d12_AODebug 1` shows
  the factor in grey over the whole frame (with `d3d12_AOApply 1` the visibility to the power of
  the strength, without the fog), 2 the ambient light, 3 the light taken. The frame dump has an
  `ao:` line (with the baked share) and the GPU times an `ao` section. For comparisons, DemoLevel
  with `--scene Demos\DemoLevelPierWalk.ini` and `-DemoArgs '--exec-at 1 "d3d12_AO gtao"'` (or
  `cacao`, and `d3d12_AOApply 0` for the old way) against the same run without it.
  The game's menu gets it from `assets/scripts/03-video-ambient-occlusion.patch`: Ambient Occlusion,
  None / GTAO / CACAO in Advanced Video Options, in the row of the Basic Video Options button
  (which moves to the bar of Apply; `04-video-ray-tracing.patch` moves it up a row), greyed out
  without `d3d12_AO`.

## Volumetric light

- `d3d12_VolumetricLight` (1) is the light the air of the main view scatters, with
  `r_HDRRendering` (`Volumetric.cpp`, `Shaders/Volumetric.hlsl`): the shafts of the sun through the
  leaves, the windows and the gaps between the rocks (by default those alone, see
  `d3d12_VolumetricLightShafts` below; with 0 also a haze that thickens with the distance).
  Its froxels are 160 across the view and as many down as its shape gives (90 at 16:9), in 128
  slices from 0.5 m to `d3d12_VolumetricLightDistance` (60 m), each the same factor deeper than
  the one before (`R16G16B16A16_FLOAT`: this frame's, the frame before's and the integrated ones,
  15 MB each at 16:9). After the main scene, before the HDR resolve, a compute
  pass puts into each froxel, at a point that moves within it from frame to frame (a Halton
  sequence, and a noise across the columns), the air's extinction (`d3d12_VolumetricLightDensity`,
  0.002 / m up to the water's level, thinning to half over each `d3d12_VolumetricLightHeight`,
  40 m, above) and the light it scatters towards the camera: the sun's
  (`d3d12_VolumetricLightSun`, 1: the sun's colour as it is, with the Henyey-Greenstein phase of
  `d3d12_VolumetricLightAnisotropy`, 0.3, scaled so that the same all round is 1) where the full
  set of the sun's cascades shows the sun (one bilinear comparison in the cascade of the froxel's
  depth; beyond the cascades the sun reaches everywhere), and the light from all round
  (`d3d12_VolumetricLightAmbient`, 1: the fog's colour, so that far off the air takes as much
  light as it gives; a quarter of it where the sun is hidden, as the leaves and the roofs that hide
  the sun hide most of the sky too). A froxel keeps 90% of the frame before's at the point's
  place in the view then, which smooths the moving points out. A second pass sums each column
  front to back (energy conserving, as in Frostbite's): the light scattered towards the camera up
  to each froxel's far end and the part of the light behind that reaches the camera. The resolve
  takes them at each pixel's depth (the depth buffer bound read only; the sky is beyond the far
  end): the scene's light times that part plus the light scattered, before the exposure.
  At 3840 x 2160 the froxels take 0.06 to 0.09 ms of the GPU outdoors (the sweeps of ten levels)
  and the resolve 0.04 to 0.06 ms more; 3 to 10% of the frame rate at 400 to 600 FPS.
  With an upscaler the pass before it (at the render size: the scene's light since step 260, the
  whole resolve with `d3d12_UpscaleHdr 0`) takes the froxels at the depth of its scene; the froxels
  are of the view without the jitter, which moves a pixel by a small part of a froxel. None in heat
  vision or under water, and indoors where the outdoor lights do not reach (the camera's vis area,
  `IVisArea::IsAfectedByOutLights`; it fades out and in over half a second, and nothing of it runs
  there). `d3d12_VolumetricLight 0` gives the frames of the renderer without it (DemoCompare
  difference 0), 2 shows the light scattered alone. All are read every frame. The GPU times have a
  `volumetric light` section and the frame dump a `volumetric light:` line.
- `d3d12_VolumetricLightShafts` (4 m) keeps the shafts alone: all the air lit alike is a haze
  over everything (the sky over the sea, the leaves in the shade; the game's own fog is the haze
  already), while a shaft shows as lit air against air in shadow. So a froxel scatters the sun's
  light only as much as the air around it is in shadow: 8 points of a disc of that radius across
  the sun's direction (a Vogel disc, turned from frame to frame and froxel to froxel), each looked
  up in the cascade of the froxel's depth (one texel: the sun reaches it, or a shadow is cast onto
  it from more than 1.5 m nearer the sun; a point less than that under a surface is the ground or
  inside a thing, not air, and left out), that part to the power
  `d3d12_VolumetricLightShaftsPower` (3: a narrow shaft all of it, the edge of a wide shadow and the
  dappled light under the leaves little), times `d3d12_VolumetricLightShaftsSun` (16, times
  `d3d12_VolumetricLightSun`: the shafts are a little of the air, brighter than all of it alike).
  No light from all round, and the light scattered is added without hiding what is behind it.
  With the defaults (the HDR test bench, 1920 x 1080), the sky over the sea at Fort's spawn is
  136 156 204, as without the volumetric light (all the air: 168 181 215), the dark leaves in
  Training's village against the sun 49 61 47 for 42 57 43 without it (all the air: 80 91 90; the
  game's own look 35 49 35).
  The 8 points take the froxels from 0.08-0.09 to 0.27-0.32 ms of the GPU at 3840 x 2160 (the
  sweeps of Fort, River and Dam; 5% of the frame rate at 280 FPS).
  `d3d12_VolumetricLightShafts 0` is the light of all the air as before (the frames as before:
  DemoCompare difference 0): the sun's at `d3d12_VolumetricLightSun` 1 (the physical share, a
  white wall facing the sun gives its light over pi and a metre of air with the extinction a
  quarter of that, shows no shafts at a density the open sea stands) and the light from all round
  of `d3d12_VolumetricLightAmbient`, the scene behind it hidden by its transmittance.

## Foliage translucency

- `d3d12_FoliageTranslucency` (1.5) lets the sun through the leaves and the grass of the main view
  seen against it (the vegetation's alpha tested class, `eDC_AlphaTestVColor`, which the game
  lights by its vertex colours alone, so that against the sun the leaves were as dark as on the
  shaded side). Their pixel shader (`DF2_FOLIAGE`, the general program's of their own, `XR_FOLIAGE`,
  so that the others stay as they were; `FoliageLight` in `Shaders/General.hlsl`) adds the leaf's texture times the object's and the
  world's colour times the sun's colour, times the strength, times the cosine of the angle between
  the view and the sun to the power `d3d12_FoliageTranslucencyPower` (4), where the full set of the
  sun's cascades shows the sun at the leaf (one bilinear comparison, `CascadeTap` in
  `Shaders/Common.hlsli`): the outer leaves of a crown glow, those inside it and in the shadow of
  others stay dark. It fades out from half of `d3d12_FoliageTranslucencyDistance` (60 m) to it.
  At 3840 x 2160 the GPU's frame takes 0 to 0.07 ms more on the sweeps of ten levels (Swamp's
  jungle the most), 0 to 3% of the frame rate. Only with the sun's cascades, not in heat vision, the reflections or
  the sprites' pictures; with 0 the frames are those of the renderer without it (DemoCompare
  difference 0). All three are read every frame.

## Ray tracing

- Ray tracing (`RayTracing.h`, DXR 1.1 with inline `RayQuery`, Shader Model 6.5): the main view's
  opaque draws and the static ones of the water's planar reflection become acceleration
  structures (a `ray scene:` line in the frame dump), once a frame and only while something wants
  rays (else their memory goes). The top level one and those of the frame's geometry are built
  only in a frame with a water surface (or with `d3d12_RTDebug`, `d3d12_RTSunShadows`,
  `d3d12_AO rt` or `d3d12_RTGI`); in
  another one only the new static geometry's, ready for when the water shows.
  The scene is made before the opaque list is drawn (`BuildRayScene`): a walk over the list sets
  each item up as it will be drawn, without its lights, and hands its draws to the scene instead of
  drawing them, so that the opaque list and the passes after it can trace rays too.
  The builds run on the compute queue (`d3d12_RTAsync`, default 1; 0 in the frame's list before the
  opaque list), beside the frame's shadows, opaque geometry and ambient occlusion: they start once
  the uploads so far are done (they read only vertices and indices, uploaded or written by the CPU),
  and the frame's list waits for them at its first draw or pass that traces rays (the water), else
  at its end. The GPU times have them as `ray scene (compute queue)` beside the frame's sections
  (not part of its time); the frame's `ray scene` section is its wait for them. Built on the compute
  queue the levels' sweeps (4K) ran 6–8 % faster (River 392 → 423 frames a second, Dam 444 → 476),
  the time demos Training 386 → 398 and Regulator 343 → 359 (Volcano and Research, without water,
  the same). The walk before the opaque list costs the CPU 0.03–0.08 ms a frame; the frames are
  those of the capture during the list, bit for bit.
  `XRENDERD3D12_NORT=1` (or a device without tier 1.1) turns them off; the log says why.
  `Shaders/RayShade.hlsli` traces (`TraceScene`, alpha tested triangles tested: of the triangle
  only what the test reads, the normal only for a cube map, the vertex colour not where the alpha
  is the texture's) and shades the hits as the general program does without the draw's lights:
  textures, vertex colour, light maps (dot3 ones too), the sun with its shadow by a ray
  (`d3d12_RTShadows`, default 1). `d3d12_RTReflections` (0 by default, read every frame): 1 the
  water's reflection by rays in place of the screen space reflection, 2 hybrid (the screen space
  reflection where it is sure, the rays elsewhere, also where its march passes behind an object);
  with either the surfaces reflecting a cube map trace rays too (`d3d12_RTReflectionSurfaces`);
  not registered without rays.
  `d3d12_RTDistance` (150 m) the rays' length, `d3d12_RTMemory` (384 MB) the static BLAS kept,
  `d3d12_RTMask` (31) the kinds the rays see (1 static, 2 alpha tested, 4 of the frame; a draw is
  in the scene when the mask has all of its kinds).
  `d3d12_RTOffscreen` (1) puts what is out of the main view into the scene too: the parts of the
  sun's cascades (what the 3D engine gives the camera towards the sun at the view's position, with
  the detail it picks for the view, and the live casters), set up as the main view would draw them,
  those it has too left out (a `ray scene:` count). 1 for the effects that trace rays from the main
  view's surfaces (`d3d12_RTSunShadows`, `d3d12_AO rt`, `d3d12_RTGI`, `d3d12_RTDebug`; not the water's
  reflection, which sees what is in front of the camera and has the planar reflection's draws), 2
  always, 0 never; only with the sun's cascades on. On the sweeps it adds 180 to 360 instances to
  about 300 of the main view, 0.05 to 0.07 ms of the CPU and 0.04 ms of the compute queue's builds.
  A cache of the static draws seen in earlier frames was tried and left out: flying over River it
  held 70 000 draws after 600 frames, 38 000 of them within 150 m, a tree seen from several
  distances with its LODs on top of one another.
  `d3d12_RTSunShadows` (0 by default, read every frame; not registered without rays) gives the sun's
  shadow by rays in place of the cascades' (`RaySun.cpp`, `Shaders/RaySun.hlsl`). In the main view's
  opaque list, after the depth prepass and before its surfaces are shaded, a pass makes the mask of
  the sun's visibility at the surfaces in the depth buffer: from a pixel's point (from the depth), a
  little off its face (the normal from the depths beside it), a ray towards a point of the sun's
  disc (`d3d12_RTSunAngle`, 0.53 degrees as the sun's; 0 hard shadows), another point for each pixel
  and frame. With 1 a ray for each 2 x 2 pixels, from each of them in turn over 4 frames (a quarter
  of the rays; the history has them all), with 2 a ray for each pixel. The ray looks among all the
  casters first; where what it finds is not a live caster, a second one looks among those
  (`XR_RAY_LIVE` in the instances' masks and their `InstanceContributionToHitGroupIndex`: the parts
  of the cascades' live set), so that the light maps and the terrain, whose light has the shadows of
  the rest, take the sun from them, as from the live set of the cascades. Each pixel takes the rays
  round it on its surface, the nearer the more (with 2 nearly only its own); with
  `d3d12_RTSunDenoise` (1) the frames before are blended in where the surface was (at most 24,
  clamped to what the rays round the pixel show this frame, and left out where the rays find what
  the history was sure of turned the other way, so that a shadow that moves does not stay behind;
  with the path tracer at most `d3d12_PTSunHistory` (64) frames, a quarter of them kept where the
  shadow moves and up to half a step off what the rays round the pixel show, against the flicker of
  the crowns' shadows in the wind, where the sun is all the light: on Pier's walk 2 % less, the
  shadows of what moves lagging a little),
  and where the result is neither all sun nor all shadow (or the history is young) it is filtered
  over the width the sun's disc gives at the distance of what casts it: sharp where an object stands
  on the ground, softer the higher the leaves are. With `d3d12_RTSunDenoise 2` the rays are a ray
  for each pixel and AMD FidelityFX Denoiser's shadows denoiser (v1.2, `third_party/fidelityfx-denoiser`,
  a git submodule; our kernels round its headers in `Shaders/RaySunFfx.hlsl`; without it 1) takes
  the history's and the filter's place, for all the casters and again for the live ones: the rays as
  its bit masks of 8 x 4 pixels, its tile classification (tiles all in the sun or all in the shadow
  skipped; the history where the point was in the frame before, the camera's move from the depth,
  clamped to the rays round it; the moments of the visibility) and its three edge-avoiding passes on
  the depth and the normals from it, the second's output the next frame's history; it costs
  more than 1 (on an RX 7900 XTX 0.3 to 0.7 ms with the path tracer under FSR Quality, 0.8 to 1.6 ms in the
  raster at 3840 x 2160). The mask reaches as far as the cascades and gives
  way to them over the last tenth. The general program takes it where its pixel is the mask's
  surface (the view depth the same: the depth prepass's surfaces; `XR_RAY_SUN`, pixel shaders of
  their own); the others (alpha tested surfaces, the leaves, what is blended, the weapon in the
  hands) keep the cascades. It needs `d3d12_DepthPrepass` and `d3d12_ShadowCascades`;
  `d3d12_ShadowDebug 4` shows it where it counts over the cascades' view: blue where the sun shows,
  with red where all the casters hide it, with green too where the live ones do (yellow). GPU
  sections `rt sun shadows` (the rays), `rt sun shadows history`, `rt sun shadows filter`. With it
  off nothing of it runs and the frames are the same as without the code.
  `d3d12_AO rt` is the ambient occlusion by rays (`RayAO.cpp`, `Shaders/RayAO.hlsl`; what it shares
  with the sun's rays in `Shaders/RayView.hlsli`), a backend of the ambient occlusion with the same
  visibility for its resolve, after the opaque list: from a pixel's point, a little off its face, a
  ray `d3d12_AORadius` long in a direction of the hemisphere about the face as the cosine spreads
  them (each pixel its own, a hash, moved along an R2 sequence by its rays so far), among the scene
  with what is out of the view, so that what is behind the camera or beside the view darkens the
  surfaces near it too (the screen space ones see only what is in the picture). Where it finds
  nothing the pixel is open; what it finds within the first 0.385 of the radius hides it all, less
  and less over the rest (XeGTAO's falloff). `d3d12_AOQuality` 0 to 2 a ray for each 2 x 2 pixels,
  from each of them in turn over 4 frames, 3 a ray for each pixel. With `d3d12_AODenoise` (not 0)
  each pixel takes the rays round it on its surface, the nearer the more, blended into the frames
  before where the surface was (at most 16), then a spiral of taps on its plane filters it: 16 over
  16 pixels where the history is young, down to 8 over 5 pixels at 8 frames. As far as the AO fades
  out with the fog. GPU sections `rt ao` (the rays), `rt ao history`, `rt ao filter`; the frame dump
  an `rt ao:` line. Without `rt` nothing of it runs (the frames as without the code). At 3840 x 2160
  in the hut (DemoShadowsHut) the rays take 0.53 ms, the history 0.28, the filter 0.35 (CACAO 0.89
  in all), the GPU 2.45 -> 2.84 ms a frame; the sweeps over Pier, River and Swamp 520, 493, 487 ->
  446, 416, 411 frames a second. In a palm's crown, the view full of alpha tested leaves, the rays
  take up to 3.6 ms.
  `d3d12_RTGI` (0 by default, read every frame; not registered without rays) is the ambient light by rays,
  one bounce, in the ambient occlusion's place (whatever `d3d12_AO` says; it needs `d3d12_AOApply 1`, the
  ambient light's target): the rays of `d3d12_AO rt` (`RayAO.cpp`, `Shaders/RayAO.hlsl` with `RAY_GI`),
  `d3d12_RTGIDistance` (4 m) long, and what they find is shaded as the water's rays shade it (`RayShade`:
  textures, light maps, the sun with its shadow by a ray towards it, the ambient colour). The light a hit
  gives back over its own ambient colour (the 3D engine's outdoor ambient where it has none; at most 4
  times it, times `d3d12_RTGIBounce`, 1) stands for the ambient light from there: a ray that finds nothing
  brings 1, the ambient light as it was. Their mean, with the same falloff, history (at most 24 frames,
  the frames in a texture of their own) and filter as the AO's, in rgb, is the factor of the pixel's
  ambient light (`R16G16B16A16_FLOAT`): under 1 in a corner, under a roof, by a dark wall, over 1 by a lit
  one, its colour the colour of what is there. Two draws take the ambient light times what it is under 1
  from the scene (`REV_SUBTRACT`) and add it times what it is over 1 (`ADD`), each only where there is
  something (`discard`), so that in the open the frame is as it was. 1 a ray for each 2 x 2 pixels, from
  each of them in turn, 2 a ray for each pixel; `d3d12_AODenoise`, `d3d12_AOStrength` (the factor's
  exponent) as for the AO; `d3d12_AODebug 1` shows the factor's half in colour (grey 0.5 as it was), 3 the
  light taken. GPU sections `rt gi`, `rt gi history`, `rt gi filter`; the frame dump an `rt gi:` line
  (with the outdoor ambient colour). Off, nothing of it runs and the frames are those of the AO. At 3840 x
  2160 in the hut (DemoShadowsHut) its rays take 1.32 ms, the history 0.38, the filter 0.36, the resolve
  and the two draws 0.28: the GPU 2.43 ms a frame with CACAO, 2.78 with the AO by rays, 3.81 with it
  (inside the hut 2.08, 2.60, 3.84; with 2 its rays 5.1 ms); the sweeps over Pier, River and Swamp 509,
  488, 467 -> 372, 341, 323 frames a second. Its rays alpha test at most 4 of the triangles they pass
  (`TraceScene`'s, `TraceSun`'s and `RayShade`'s `nMaxTests`, 16 for the others; what is past them counts
  as hit): in a palm's crown they take up to 4.9 ms, with 16 up to 6.
  `d3d12_RTReflectionSurfaces` (1 by default, read every frame; not registered without rays) with
  `d3d12_RTReflections` 1 or 2 gives the surfaces that reflect a cube map (`DF_CUBE0`: the chrome
  and metal of `TemplReflCM` and `TemplBumpReflCM`, the screens and consoles of `TemplReflCMDecal*`,
  the glass of `TemplGlassCM`, the environment cubes of `TemplReflEnvCM`) the scene by rays where
  they look the cube up: in the main view's draws (the opaque list, the weapon in the hands and the
  blended glass) the general program's pixel shaders with `XR_RAY_REFL` (Shader Model 6.5, programs
  of their own, `General_*Rays_*PS`: `SPipelineKey::bRayTrace` for the general program; with the
  cascades or the mask of the sun's shadow by rays, as the draw's other programs) trace a ray from the pixel's
  point, a little off its face, `d3d12_RTDistance` long, the reflected one and the glass's one
  through it, among the scene for rays (`SurfaceRay`: `TraceScene`, `RayShade`, fogged by the path
  from the camera as the water's). What it hits takes the cube's place; where it hits nothing (the
  sky, the far) or the ocean first (not in the scene for rays: a ray down from above the ocean's
  level ends there) the cube stays, and it takes over again over the last quarter of the rays'
  length. The rest of the material is as with the cube (its lighting, `DF2_REFL_AMOUNT`'s gloss map,
  the glass's Fresnel and texture). The frame constants' `nRaySun` y z w carry the length, the mask
  with the mode and the ocean's level. 0 the cubes alone (the frames as without the code), 2 the
  hits tinted red, the misses blue. A frame with such a draw builds the scene for rays (with what is
  out of the view while there were some the frame before). The environment cubes are still updated:
  the misses take them. At 3840 x 2160 on the beach of the surfaces scene (DemoSurfacesBeach: chrome
  spheres and teapot, glass spheres and pane) the GPU takes 4.36 ms a frame with the cubes and 5.00
  with the rays (the opaque list 0.64 -> 0.99, the blended glass 0.07 -> 0.42); from the side,
  orbiting, 3.96 -> 4.83: the frame's list now waits for the build of the scene for rays at the
  opaque list in place of the water, up to 3 ms in the frames where new objects come in. The sweeps
  over Pier, River and Swamp, with next to no such surfaces, stay within their noise.
  `d3d12_RTPbrReflections` (1 by default, read every frame; not registered without rays) with
  `d3d12_RTReflections` 1 or 2 and `d3d12_RTReflectionSurfaces` gives the metals and the smooth
  materials of the RayMaster maps (Parallax and generated normals (RayMaster)) a reflection of the
  scene by rays: a lit opaque or alpha tested draw (`DF_LIGHTING` or a light map; not the cube map
  surfaces above, the array textures, the shader effects) whose texture's material map has a mean
  metalness of 0.25 or more and a mean roughness of 0.6 or less, or a mean roughness of 0.3 or less
  (`PbrReflects`, by the map's smallest mip), takes the same programs (`XR_RAY_REFL`) in its first
  pass: a ray whose direction is the view reflected about a normal of GGX's visible normals for the
  roughness where the view meets the texture (`Shaders/PbrSpecular.hlsli`'s `PbrReflectDir`, Heitz
  2018; the highlights' roughness with `r_PbrRoughness`, else the material map's red), another each
  frame (the temporal anti-aliasing gathers them), its cone wider by the lobe; what it finds
  weighted by the share the surface reflects over the lobe (Karis's fit of the split sum, F0 4 %
  for a dielectric, the texture's colour for a metal), less towards the rough (a dielectric's from
  0.2 to 0.35, a metal's from 0.45 to 0.65, between them by the metalness: `PbrReflectWeight`). A
  miss (the sky, beyond the rays' length, what is out of the view) takes the draw's ambient light. A
  metal keeps `d3d12_RTPbrMetalDiffuse` (0.5) of its diffuse light beside its reflection, its light
  passes and the projectors' over it as much (`XR_PBR_LIGHT_PASS`, no ray), and its ambient part
  for the ambient occlusion as much (else the occlusion took all a dark metal's colour); its
  highlights (the sun's, the lamps', the light map's lamps') stay whole (`General.hlsl`'s `vHigh`: the
  part of the colour they make, scaled as the light map, the projector and the detail texture scale
  it; at first they were less as well, the plates' lamp glints at 0.69-0.76 of their brightness). The frame
  dump's line ` reflection (d3d12_RTPbrReflections)[ light pass] roughness R metal M[ screen
  (d3d12_RTPbrScreen)][ quad (d3d12_RTPbrQuad)][ occlusion (r_PbrAO) A]` (the material map's means). With
  `r_PbrAO` less where the texture's ambient occlusion hides the lobe (Parallax and generated normals
  (RayMaster)).
  `d3d12_RTPbrScreen` (1 by default) lights what such a ray hits as the last frame's picture shows
  it where that frame saw it: the main view's picture and depth are copied once a frame with such
  reflections (before the engine clears the depth for the weapon in the hands, with MSAA its depth
  resolved first, else at the scene's end; `m_ReflCopy`), and the next frame's hits are projected
  into it by that frame's view and jitter (`PbrPrevUV`), taken where its depth there is as far as
  the hit give or take 3 % (5 cm at least, and the cone's width), fading over the picture's sides
  (`PbrPrevShare`); elsewhere the rays' own light (`RayShade`: the light maps and the sun, no lamps),
  not looked up where the picture has all of it (the hit's texture, light map and ray to the sun
  left out). The lab's metal floor plates under Treehouse's lamps reflect the lamp-lit corridor in
  place of a dark one. 2 for `d3d12_RTReflectionSurfaces` tints those hits green. 0 the rays' own
  light alone.
  `d3d12_RTPbrQuad` (0 by default) traces one ray for the 2 x 2 pixels of a quad (the pixel shader's
  `QuadReadLaneAt`): one of its pixels that reflect (not a helper lane, `SV_Coverage` 0, its point off
  the triangle) traces it, another each frame and quad (`PbrQuadStart`, `PbrQuadLane`), its cone as
  wide as the 2 pixels, and those whose view reflects about the same way (their mirror directions at
  most 0.03 rad and half the lobe's alpha apart: `PbrQuadShares`) take its light, each weighting it by
  its own share; the others trace their own. One ray for all of a quad made the plates' treads milky
  (each pixel's bump its own reflection: with TAA 0.86-0.92 of the detail of a ray a pixel left, the
  light up to 4 levels apart); so 0.94-0.97, within a level. But the rays' noise is then the same in a
  quad's 4 pixels, and the temporal anti-aliasing (its history held to the colours about a pixel)
  leaves it as a coarse pattern and sparkles on fine relief (the lab's wall panels' mesh, as well with
  the mirror directions at most 0.03 rad apart), their light 1-2 levels darker: so a ray a pixel by
  default, 1 where speed counts more (at 3840 x 2160 below the opaque list 2.46-2.50 ms, the frame
  4.68-4.71 ms; with the player's settings 5.57-5.62 ms).
  At 3840 x 2160 (RX 7900 XTX, `Parallax_treehouse_pan.ini`, the raster with the rays' reflections, no
  anti-aliasing): the opaque list 0.90-0.93 -> 2.79-2.82 ms, the frame 2.69-2.73 -> 5.03-5.07 ms, the
  scene for rays built for it 0.22 ms (0.25 more on the compute queue); `d3d12_RTPbrScreen 0`
  4.72-4.82 ms (the copy of the picture 0.2 ms); with every hit shaded it was 5.43 ms. Of the opaque
  list's time more the programs with rays take 0.3 ms without a ray, the rest the rays: in a pixel
  shader a wave runs as long as its longest ray (a quarter of its lanes tracing, `d3d12_RTPbrQuad 1`,
  took a third less). With the player's settings (TAA, the rays' sun): the frame 3.53-3.57 ->
  5.89-5.93 ms.
  `d3d12_RTPbrReflections 0` gives the build before's frames.
  `d3d12_RTLightShadows` (0 by default, read every frame; not registered without rays) gives the lamps
  hard shadows: the main view's draws lit per pixel by point lights (the draw's lights of
  `DF_PIXEL_LIGHTING` and `DF2_MATERIAL_LIGHT`, the dynamic lights over a light map,
  `DF2_LIGHTMAP_LIGHTS`, the light passes, the projectors such as the flashlight)
  take the same programs, also compiled with `XR_RAY_LIGHT`: `DrawLighting` and `MaterialLighting` in
  the pixel shader trace a ray from the pixel's point, a little off its face on the lamp's side,
  towards each point light that lights it (`TraceLight`, the first hit ends it, 4 alpha tests), up to
  `d3d12_RTLightShadowsMax` (4) of them a pixel, ending `d3d12_RTLightShadowGap` (0.1 m) short of the
  lamp (not its own fitting), a projector's `d3d12_RTProjectorShadowGap` (0.5 m) short where that is
  more (the draw light's `vLightColor.w`): its picture draws the shade of its fitting already, which
  otherwise cut a hard edge into it (Treehouse's hanging lamps lit the corridor's walls and floor in
  a sharp trapezoid); where it hits, the light and its specular are left out. Directional
  lights, the per vertex lighting and the vegetation are as they were. 2 shows the light taken away
  in red, 3 by where the ray hits (green next to the surface, red next to the lamp). The frame
  constants' `nRaySun.z` carries the mode, the number and the gap beside the cube maps' mode; a frame
  whose scene has a light besides the sun builds the scene for rays (with what is out of the view
  while such draws were there the frame before). In the game's light the lamps light the characters
  through the walls; with it they are in the shade of what stands between, and of their own arms.
  At 3840 x 2160 under 8 lamps (DemoMultiLight with `DemoMultiLightTeapot.ini`: `main_view = 1`
  draws it as the main view) the opaque list takes 0.05 -> 0.23 ms, the GPU 0.96 -> 1.18; in
  Research's hall 1.05 -> 1.23 (the opaque list waits for the scene for rays).
  `d3d12_RTFaceNormal` (1; read every frame, not registered without rays) picks the side of its
  surface these rays (the lamps' shadows', the reflections') start from by the face of the pixel's
  triangle from the plane 1 / w makes over the screen (`RayFace.hlsli`, `nRaySun.z`'s bit 20; the
  cube maps' mode now bits 16-19). The cross product of the world position's differences from pixel
  to pixel took it before, and they round where the camera is close: a pixel of Jack's face in
  Training's opening cut scene is 0.1 mm, a float step of a position 400 m from the level's origin
  0.03 mm, and that face turned by degrees from pixel to pixel (the unit test: over 3 degrees, the
  plane's under 0.25). Where a lamp grazed the face its side flipped too: the ray started behind the
  surface in every other pixel and met the head itself, a stipple of the lamp's shadow on the
  temple, the cheek and the neck. Where the face is turned from a lamp but the vertices' normal
  faces it (the coarse mesh's terminator: the smooth shading lights past the triangle's edge), the
  ray towards that lamp starts by the vertices' normal (bit 21), so a head is not cut into its
  triangles' shadows; on skin (`r_Skin`'s wrap and the normal map light past where the light
  passes) for every lit pixel. A thin surface lit from behind (its vertices' normal turned from the
  lamp too) keeps its face. 2 takes the face alone, 0 gives the frames before. That cut scene's
  close up at 3840 x 2160 with the film's RT settings: the GPU's frame 14.36 -> 14.39 ms (noise).
  `d3d12_PathTracing`, the path tracer of the main view's lighting, has its own section (Path tracing).
  `d3d12_RTDebug` views the scene for rays from the camera: 1 instances (a colour of the mesh and
  where it is to 0.25 m, the same from frame to frame), 2 shaded hits on the right
  half, 3 distance, 4 mip level, 5 kind, 6 alpha tests, 7 shaded hits over the whole picture; the path
  tracer's: 8 the albedo of the hits, 9 their emission, 10 them lit by the sun and the lamps; 11 a
  colour per material the rays take of the hit's instance (the same while it is: to see the rays'
  materials steady); 12 the side of the hit's triangle as the raster's culling takes it (the front
  green, the back red: a red surface is one the rays meet and the raster does not draw from there;
  Training's cave hole is green, a white unlit disc the level puts in it for the sky);
  `d3d12_WaterDebug` 13, 14, 15 the water rays' colour, distance and weight. With the rays off the
  frames are the same as without the code (WaterMove, bit for bit). Check the rays under the debug
  layer with `XRENDERD3D12_DEBUG=2`: GPU based validation turns the water rays' frames black.
  The game's menu gets it from `assets/scripts/04-video-ray-tracing.patch`: Ray Traced Reflections,
  None / Rays / Hybrid in Advanced Video Options (Rays and Hybrid also give the cube map surfaces
  their rays); the rows of the page move closer together to make room for it, greyed out without
  `d3d12_RTReflections`.

## Path tracing

- `d3d12_PathTracing` (0 by default, read every frame; not registered without rays; nothing without
  `r_HDRRendering` 1, once in the log) is the path tracer of the main view's lighting (`PathTrace.cpp`):
  1 in real time, 2 the reference. It has the scene for rays, with what is out of the view (as
  `d3d12_RTOffscreen` 1 does for the effects) and what is around the camera (`d3d12_PTBehind`, 40 m: the
  static objects and entities the 3D engine gives cameras at the view looking back, up and down, 170
  degrees wide each, as far as that, noted as the cascades' parts, `UpdateRayBehind`; the cascades have
  only what is towards the sun, a room without the sun none of it: Research's hall has 42 instances
  without it, 259 with it. With the view's and the one looking back alone, what is above and below the
  view out of its frustum was not there: a roof's parts dropped out of the scene as the camera looked
  down a little, the paths went out of the room through it, and the history held their light after the
  camera looked up again. The three take 0.1 to 0.3 ms of the CPU; the sweeps' FPS is the same as with
  the one), the motion
  vectors (the scene targets without an upscaler, Upscaler), and the lights for its kernels and the
  programs with rays: `SPTLight` (`Shaders/RayConstants.h`), the root SRV t2 space5, the point lights,
  the directional ones and the projectors with their cube (and their rays' gap, Projected lights).
  They are the frame's lights and the level's static ones (`I3DEngine::GetStaticLightSources`, the lamps the light maps were baked with: the frame
  has only those whose sphere reaches the view, one or two of the 85 of Research, and many are
  `DLF_FAKE | DLF_LM`, never in a frame), the nearest 128 within `d3d12_PTLightDistance` (60 m); not the sun, a
  fake light the light maps do not have, a heat source that gives no light, the characters' fill lights
  (`DLF_AMBIENT_LIGHT`) without `d3d12_PTFillLights`: those are in the characters' ambient light, which
  the paths fill in with (below), unshadowed as the raster's (left out, Jack in the Training intro's
  dream, lit mostly by it in the raster, had 0.12 of the raster's light on his shirt, 0.65 with it; as a
  lamp a cave's roof hid it). The frame dump lists them (`pt lights:`).
- The lamps fall off as the raster's dynamic light, (1 - smoothstep(0, 0.9, d / r))^2.2, nothing from
  0.9 r on (`PathLampFalloff`). The light maps were baked with a flatter falloff that reaches the whole
  radius, so a room lit by its maps is darker in the path tracer (the light maps fill it in, below). `d3d12_PTBakedFalloff` k > 0 (0 by
  default: the picture as it was) makes the lamps baked into the light maps (`XR_PT_LIGHT_LM`) fall off
  as ((1 - d / r)^k)^2.2 to the radius. On Research's SUB-LEVEL wall (step 271: `Film_research-lamp.ini`
  frames 400 and 460, HDR with the exposure fixed and a clamp) the raster has 2.3 times the path
  tracer's light on the frame's mean, up to 5 in places; k = 1 brings it to 1.45 and k = 2 makes it
  darker, and the game's ambient unoccluded (`d3d12_PTAmbientRange 0`) does not change it. Near the
  lamps k = 1 is brighter than the maps (a still view: 19.5 % of its pixels, 7.4 % with 0). Test: unit
  test `PathTrace_LampFalloff`.
- Where the frame has no sun (indoors) the light maps give the light their lamps do not
  (`d3d12_PTLightMaps` 1 times it, 0 none: the path tracer's picture as before): the G-buffer keeps the
  light map's light (its sixth target, below), and `CSTrace` adds to the paths' light that of the raster
  (the light map's 4 x light + the ambient, as light: `PathLightmapFill`) less the ambient the paths fill
  in with and less the light each lamp baked into the light maps (`XR_PT_LIGHT_LM`) gives there
  unshadowed, nothing below 0. Where a lamp reaches its light and its shadows stay the path tracer's;
  beyond its reach, in the shadows the maps were baked with and where their light is flatter than the
  lamps', the light is the maps'. On Research's SUB-LEVEL wall (as above) the raster has 1.07 and 1.08
  times the path tracer's light (2.46 and 2.58 without it); Training's bunker, which the lamps light
  about as the raster, stays so (`Film_training-bunker.ini` frames 310 and 490: 0.86 and 1.10, 0.89 and
  1.28 without it). A frame with the sun has the path tracer's own sun and sky and no light maps. It
  costs 0.27 ms of the paths at 2560x1440 where it is on (Research), its target 0.02 to 0.09 ms of the
  opaque list. Test: unit test `PathTrace_LightmapFill`, `render_tests` suite `ptlightmap`.
- The scene for rays has its materials (`SRayInstance` `vAlbedo`, `vSpecular`, `nPath`, set while the
  opaque list is captured): its hits are shaded by `Shaders/PathShade.hlsli` in linear light (the HDR
  target's, `HdrDecode`): the albedo is the draw's colour without its light (no light map, ambient or
  sun; the templates lit by the ambient colour alone take the object's colour), what glows (glow maps,
  alpha glow, the templates without lights) is its emission, and the sun and the frame's lamps light it
  by a ray towards each (`d3d12_PTSun`, `d3d12_PTLamps`, `d3d12_PTEmission`: times twice their colour,
  the lamps falling off as the raster fits the static objects' to). The game's lights multiply its
  colours, so in light they are to the power 2.2 (a colour times a light below 1 is the same picture as
  the raster's); a colour that glows is `HdrDecode`d. The terrain's texture has the sun and the
  terrain's own shadows baked in: its albedo (as light) is divided by that light, the path tracer's own
  sun and sky on an open face towards the sun (each channel: the texture has the light's colour too)
  times ((1 + (r - 1) N.L V) / r)^2.2, V the sun's visibility over the terrain alone, r
  `d3d12_PTTerrainDelight` (a lit face over a shadowed one): a lit face is as bright as the editor had
  it, and of its colour (Rebellion's sand under the blue moon is not blue twice). A pixel's point (where
  its paths start, the sun's ray of that share) is the raster's depth snapped onto the scene for rays
  (`PixelSurface`, `PixelTerrain`): a ray from the camera through it within 0.02 m + 0.2 % of its
  distance, the face's normal the triangle's it hits. The raster's depth is off by about 0.02-0.08 % of
  the distance (float transforms at a level's 1000 m, the near plane at 0.25 m), tenths of a metre at
  300 m, and a point under the terrain shaded its sun ray along the terrain: the share fell, the albedo
  divided by it flashed bright from frame to frame, in stripes (Pier's canyon; test: unit tests
  `PathTrace_PixelSurface`, `PathTrace_TerrainShare`, `render_tests` suite `ptshare`). The leaves keep
  the raster's point (they bend in the wind, the rays' scene's do not). While the path tracer is on the vegetation's vertex colours
  are baked without the light (the engine's values again on a change), and the grass that fades in or
  out is alpha tested among the opaque surfaces (its alpha times the fade: it thins out), not blended
  after them.
- Its G-buffer (`Shaders/PathGBuffer.hlsli`) is what the main view's opaque list writes with the general
  program's `XR_PT` permutation (`General_PT_*`, `SPipelineKey::bPathTrace`): the colour gets only what
  the path tracer does not light (glow maps, alpha glow, the unlit templates, the fog), a second target
  the albedo (RGBA16F, the game's colour without its light, unfogged; decals and the terrain's layers
  blend into it as into the colour), a third the normal (R10G10B10A2: octahedral, the fog's share, the
  path material: lit, terrain, vegetation) on the side of its triangle the camera sees, as the rays'
  hits take it, but the vegetation's as it is: its normals are the crown's, most of them up, which the
  raster lights both sides of a leaf by, and a path from a leaf goes through it where it is under the
  leaf's face (turned by the view's direction, a crown's turned over at the eyes' height, a line that
  moved over the leaves with the camera; by the leaf's side, every other leaf); the plants' opaque
  parts (a trunk, its branches: `DF_VCOLOR_X2` without the alpha test) the triangle's normal and the
  lit material (`XR_PT_VEGETATION_MATERIAL`, `PathSurfaceNormal` at the rays' hits): their vertices
  carry the crown's normal too, and a trunk's paths went up into the crown or, turned to the camera,
  down into the ground, through the trunk as from a leaf: the trunks were black (the film's river-temple
  and swamp-path; test: unit test `PathTrace_SurfaceNormal`, `render_tests` suite `pttrunk`), a fourth the sun's
  visibility as the raster finds it (RGBA8, r: its shadow maps, the sun's cascades and their mask by
  rays, which the path tracer turns on, from all the casters; 0 where the raster has no sun) and the
  game's ambient light on the surface (gba: what the object's ambient colour adds to its light maps or
  its lights, a lit object's fill lights 2 x colour x N.L; the terrain, whose light is in its texture, the outdoor ambient; a lit object in the
  cascades' shadow at least its light in the sun times the terrain's shadow colour, as the raster's
  Sun shadow cascades), a fifth its highlights'
  colour (RGBA8, half of the raster's 2 x specular colour x gloss of the specular templates; it blends
  as the albedo), a sixth the light map's light (RGBA8: the light map times its dot3 share, a quarter of
  the raster's light from it; with `d3d12_PTLightMaps`). Its lit draws lose their light (the light maps, the sun, the lamps and their passes,
  the ambient and the baked AO); the screen space AO, the rays' GI and the projectors' passes are not
  drawn.
- After the opaque list its kernels (`Shaders/PathTrace.hlsl`, compute with rays, at the scene's size)
  light it. `CSTrace`, a path from the surface of half the pixels, in a checkerboard, the others the
  next frame (`d3d12_PTCheckerboard` 1; a pixel left out takes the light of its four neighbours, weighed
  by how near they are to its plane and how alike their normals are, before the denoiser, so any
  denoiser has it; 0 a path from each pixel, as the reference has): a lamp picked among 16 of them by
  how much each gives (resampled importance sampling) with a ray towards it, then `d3d12_PTBounces` (1)
  bounces in directions the cosine spreads; a hit gives its emission and its albedo times the sun and a
  lamp there, a path that leaves the scene the sky (`d3d12_PTSky` times the outdoor ambient as light).
  The game's ambient light fills in (`d3d12_PTAmbient` 1): a ray that finds nothing within
  `d3d12_PTAmbientRange` (2 m) brings the ambient light of the point it leaves (the G-buffer's, then the
  hit's, kept in its instance), as the raster's ambient times its occlusion, besides what it finds; one
  that finds nothing at all the brighter of that and the sky. Rooms have it from the light maps'
  ambient, which a path of a few bounces through a window does not make up for; in real time a sample is
  at most 16 (as light) and a bounce's ray takes 4 alpha tests. With `d3d12_PTCache` (1) the last
  bounce's hit takes all the light that comes to it from a radiance cache (`Shaders/PathCache.hlsli`),
  with no rays towards the sun and a lamp there where its cell has some: a hash grid over the world
  (2^20 entries, 28 MB) of the light that comes to a point (the sun's and the lamps' straight to it and
  by its bounces), cells of 0.25 m near the camera, twice as large each time the distance doubles past
  12.5 m, one for each axis a face's normal leans along most (no light through a thin wall); a lookup
  takes a cell across the face picked at random (noise for the denoiser, not edges). `CSCacheUpdate`
  keeps it up to date: from a pixel of each 8 x 8 (another each frame) a path of 3 bounces at most as
  `CSTrace`'s, the sun's and a lamp's light by rays at each of its points, the cache's at its end, and
  back from there all that comes to each of its points is a sample of the cell there; `CSCacheResolve`
  blends a cell's samples into its light (32 frames at most) and lets an entry go after 120 frames
  without one. So the light of many bounces costs less than one: rooms and tunnels come closer to the
  reference (the hut, Research's hall); it costs 0.5 to 0.6 ms at 2560x1440, and the paths 0.4 to 0.5 ms
  less.
- `CSTemporal` blends it into the history where the surface was in the frame before (32 frames at most),
  `CSFilter` filters that on the pixel's plane and normal, wider where the history is young
  (`d3d12_PTDenoiser simple`; `none` the history alone). With `d3d12_PTDenoiser nrd`, the default where
  NVIDIA NRD is built in (`third_party/nrd`, a git submodule at v4.18.0, with ShaderMake and MathLib;
  without them the renderer is built without it and takes `simple`), NRD's REBLUR_DIFFUSE takes
  `CSTemporal`'s place: `CSPack` (`Shaders/PathNrd.hlsl`) gives it the paths' light with the first
  bounce's distance, the normal, the linear depth and the scene's motion vectors (without them the
  camera's), the renderer's device runs its passes (`NrdHost.cpp`: root signatures and pipeline states
  from its descriptions, its DXIL, its textures as compute targets; 30 frames of history at most), and
  `CSResolve` makes its light the history that `CSFilter` takes without its spatial filter; at 2560x1440
  it takes 1.1 to 1.2 ms with its packing, 0.3 to 0.4 ms more than `CSTemporal`.
- `CSSpecular` makes the highlights as the raster does, in the game's colour: the highlights' colour
  times the sun's specular colour times its visibility and each lamp's times its falloff, times
  saturate(R.V)^32; a lamp's where a ray towards it reaches it (4 rays a pixel at most, only for a
  highlight of 1/512 or more), so they are not noisy and the reference has them as they are
  (`d3d12_PTSpecular` 1 times them, 0 none). The composite (`Shaders/PathComposite.hlsl`) adds the fog's
  share times (`HdrEncode`(albedo times (the sun's light times N.L times its visibility + the paths'
  light)) + the highlights) to the colour, before the blended surfaces, the weapon, the water and the
  particles, which stay the raster's. Where the frame has no sun (indoors) there is no sun or sky
  either: what keeps the sky out (a roof above the view) is not in the scene for rays.
- The reference (`d3d12_PathTracing 2`) adds up its frames while the view and the path tracer's settings
  stay, `d3d12_PTReferenceBounces` (8) bounces with Russian roulette, every alpha test, no clamp or
  filter. `d3d12_PTDebug` shows it: 1 the albedo, 2 the normal, 3 the fog's share, 4 the material, 5 the
  paths' light on the game's grey 0.5, 6 one frame's paths (with the checkerboard half of them the frame
  before's), 7 the sun's visibility, 8 the ambient light,
  9 the highlights' colour, 10 the highlights, 11 the radiance cache's light at the surfaces, 12 the
  terrain's share of the light baked into its texture as the composite divides its albedo by it (grey, 1 on
  a lit face towards the sun; what is not the terrain black).
- Open places are as bright as the raster, a hut or a tunnel lit through an opening about as bright
  (without the ambient light far darker, the reference too); a hall lit by its light maps' lamps about
  as bright with the light maps' light (without it 2.3 times darker), the crowns of the trees seen from
  below darker (their leaves pass no light). The highlights are
  where the raster has them (the specular templates: characters, some rocks and machines) and from the
  light maps' lamps too. On a Radeon RX 7900 XTX at 3840x2160 with FSR Quality the flights over Pier,
  River and Swamp run at 107, 99 and 93 FPS with NRD, the radiance cache and the checkerboard (1 % lows
  77, 60 and 64), 93, 84 and 77 with a path from each pixel (63, 48 and 51; the raster 201, 193 and
  193): of the GPU's 9 to 10 ms FSR 4 takes 3.6, the paths 1.6 to 2 (most among the grass and the palms'
  crowns), NRD 1.1 to 1.2, the cache 0.5 to 0.6, the scene for rays 0.5 to 0.6 and its builds on the
  compute queue 0.7 to 0.8 meanwhile, the highlights under 0.05.
- `r_DisplayInfo` has a Path tracing line (Statistics and profiling): waits for HDR, on with no 3D scene
  now, the denoiser, the bounces, the radiance cache and the checkerboard of the last frame traced, or
  the reference's bounces and frames; the ambient occlusion is none (path tracing) meanwhile.
- The game's menu gets it from `assets/scripts/07-video-path-tracing.patch`: Path Tracing, None / Real
  Time / Reference in Advanced Video Options, in the last row of the right column under HDR output;
  choosing it while HDR is None sets HDR to Soft, greyed out without `d3d12_PathTracing`.
- Its unit tests (`Tests/`, the console program `XRenderD3D12Tests`, `ctest --preset x64-release`)
  run its shader code without the game, on a D3D12 device of their own (a hardware adapter; WARP
  with `--warp` or without one; `--debug` fails a test on a debug layer message): the G-buffer's
  normal (the octahedral map, the side it is turned to, the vegetation's left as it is), its fog and
  material fields, the HDR targets' light curve, the radiance cache (cells, keys, insertion from many
  threads, the samples' sum and `CSCacheResolve` itself), `PathLeafSide`, a trunk's normal and material, the cosine directions, the
  checkerboard, the structures C++ shares with the shaders word for word, and on a scene for rays of
  their own (a BLAS of the test's triangles in a TLAS, the bindless buffers and samplers as the
  renderer's) the pixel's point snapped onto it and the terrain's share at a hill side 300 m away with
  the raster's depth off by up to 0.16 %.

## Pipeline states

A pipeline state is made the first time a draw asks for its key (`D3D12Pipeline.h`), which holds
that frame up: 0.1-0.2 ms each with the driver's shader cache warm, 155 of them in the first run
of the Training time demo, up to 40 in one frame while the level loads and about 20 while it plays.
The keys made are appended to `XRenderD3D12_pipelines.bin` in the working directory (after a frame
that made none, so a burst is one write; the demos end the game without a shutdown), and the
renderer's initialization makes the keys of that file on several threads before the first frame
(the 193 of the four time demos and ten sweeps in 9 ms on 8 threads), so a later run makes none
while it plays. The file is a header (magic,
`PIPELINE_KEY_VERSION`, the key's size) and the keys; one with another version or key size, the
keys the device refuses and the repeats are dropped and the file written again. Deleting it gives
the first run's behaviour back; the pictures are the same either way.

## Statistics and profiling

- The engine's statistics: `GetPolyCount` gives the triangles drawn in the frame the last `Update`
  ended (every draw into every target; no shadow volumes), which the game asks for after the frame
  is shown: the time demo's `Tri/Frame`, `Tri/Sec` and ratio to the recording in
  `Levels/<level>/<level>.log`, and the `Polygons` and `P/Sec` of the engine's `r_DisplayInfo` block
  (the engine draws that block at the top right itself, with its own font, when the variable is not
  0; with `XRenderD3D12` it is the renderer's, below).
  The renderer's work is in the engine's frame profiler under the subsystem Renderer
  (`FUNCTION_PROFILER(iSystem, PROFILE_RENDERER)`, `FrameProfiler.h`; `profile 1` shows it, the
  tracer's `profilers CD3D12` writes it): `CD3D12Renderer::BeginFrame` with
  `CD3D12Device::BeginFrame:WaitForGpu` (the CPU waiting for the GPU), the passes inside the 3D
  engine's own `Renderer::EF_EndEf3D` (`UpdateSunCascades`, `UpdateWaterReflection`,
  `DrawItems:Opaque`, `:Nearest`, `:Blended`, `BuildRayScene`, `DrawScreenEffects`, ...),
  `CD3D12Renderer::Update` with `CD3D12Device::EndFrame:Present`, `FlushFrame`, and the loading
  (`EF_LoadTexture`, `DownLoadToVideoMemory`, `EF_LoadShaderItem`, `CD3D12Pipeline::CreatePSO`, ...).
  They are CPU times; the GPU's are `d3d12_GpuTimes` (a section `flush` is the GPU waiting for
  the CPU after a `FlushFrame`, e.g. a demo reading every frame for its clip; the compute queue's
  lists come after the frame's sections, beside them).
- `r_Stats` shows the renderer's statistics at the top left (below the time demo's lines while it
  plays, measured in `DrawText800`, else at the top), averaged over half a second (`Stats.cpp`):
  `1` the GPU time of each pass in the order they run (the
  sections of `d3d12_GpuTimes`, with a bar of each one's share of the GPU's frame), the time
  between frames, the CPU's wait for the GPU at the start of a frame and in `Present`, the draws,
  how many of them changed the pipeline state, and the triangles; `2` (and more) also the memory:
  the process' video memory and its budget (DXGI), the textures and buffers, the static
  geometry's acceleration structures, the frame memory and the pipeline states made. A frame read
  before `Update` (a demo's frames and clips) has them too. It turns the GPU's timestamps on, as
  `d3d12_GpuTimes` does; both get the same times.
- `r_DisplayInfo` is drawn by `XRenderD3D12` in the same way, a panel at the top right in place of
  the engine's block: the game's version and level, the view's camera position, angles and
  distance, the frame rate of the half second with its slowest and fastest frames', the triangles
  per frame and per second, the main view's lights (the sun, the others; counted at its
  `EF_EndEf3D`), the ambient occlusion's backend as it is made (XeGTAO, CACAO, by rays, GI by rays,
  none; none (path tracing) under the path tracer), ray tracing (not supported, off, or the last scene for rays' instances and triangles; then
  the shadows by rays, sun and lamps, the reflections by rays, the water's and the cube map
  surfaces', and `d3d12_RTDebug`'s view, those that are on), the path tracer (Path tracing), the anti-aliasing (the upscaler's: FSR, temporal, also in Native AA; the
  renderer has no multisampling, so the game's Anti-Aliasing list has None alone), the upscaler
  with its version and mode, and the 3D scene's size before and after it (`2560x1440 to
  3840x2160`, or `, native`). The renderer takes the variable over (`TakeDisplayInfo`, an `IConsoleVarSink`): it keeps the console's value at 0, so
  the engine draws nothing, and the value set (the console, `system.cfg`) is the renderer's.
  Typing `r_DisplayInfo` alone therefore prints 0, and the game saves 0 in its `system.cfg`.
- `d3d12_FrameLog 1` writes a line per frame to `farcry1_testing1/XRenderD3D12_frames.txt` (never
  committed; `Stats.cpp`, `LogFrame`): the time since the last frame, the CPU's wait for the GPU at
  the start of the frame and in `Present`, the CPU's time in the main view's `EF_EndEf3D` and, of
  it, in the engine's `DrawLowDetail` for the water's reflection, the sun's cascades and the
  environment cubes, the GPU's time (of the frame `D3D12_FRAMES` earlier) and its sections, the
  draws, the pipeline changes, the triangles, the dynamic lights, the camera's position and angles.
  `2` also turns the engine's frame profiler on (collecting, without its overlay) and adds the self
  time of each subsystem and of the 8 profilers that took the most in the engine's last frame; the
  profiler's numbers add up to about 12 times the frame's milliseconds, so only their shares mean
  something.
- `d3d12_CpuSamples N` samples the stack of the thread that draws every N microseconds
  (`CpuSampler.cpp`: a thread of its own stops it and walks its stack with the modules' unwind
  data, a profiler of the wall clock that sees the engine's code, ours, the tracer's and the
  driver's alike). Set back to 0, or at the end, it writes the modules and the samples (module and
  offset, leaf first) to `farcry1_testing1/XRenderD3D12_samples.txt`; with 250 a time demo runs
  about 5 % slower. In DemoReplay's arguments `--exec-at 60 "d3d12_CpuSamples 250" --exec-at 2800
  "d3d12_CpuSamples 0"` samples a part of a run.
- `d3d12_DumpFrame 1` writes the render items of the next frame to
  `farcry1_testing1/XRenderD3D12_frame.txt` (never committed): the camera (position, angles,
  field of view, near and far plane, view and projection matrices), then for each item its list, sort
  group, draw class, shader, diffuse texture, leaf buffer, object (position, flags, light map,
  light mask, ambient and colour) and resources (alpha reference, opacity, flags, the names of the
  other texture slots, `*` when loaded), and for each draw what it got (vertex format, states,
  shader flags, textures, colour, lights), and each draw of particle sprites (a run of sprites with the same texture and blending: their count, texture,
  state, sort, the first one's ambient colour, a vertex colour's bytes); for a terrain sector, its
  custom texture binds and custom data; for a light mapped element, its chunk's vertex and index
  ranges and the object's light map texture coordinates (count, those of the chunk's first
  vertex). With
  `XRENDERTRACED_EXEC_LEVEL="d3d12_DumpFrame 1"` and `XRENDERTRACED_EXEC_LEVEL_FRAME=<n - 1>` it
  is the frame that `XRENDERTRACED_CAPTURE=<n>` captures of the original.

## Device removal (DRED)

When the device is removed (a GPU hang, a page fault, a driver reset) the renderer logs the
reason once (`XRenderD3D12: device removed (...)`) and, with DRED (Device Removed Extended Data,
the Direct3D 12 runtime's, no debug layer needed), what the GPU did: a summary in the engine's
log and all of it in `farcry1_testing1/XRenderD3D12_dred.txt` (`D3D12Dred.cpp`; never
committed). The report has the device's state (hung, fault, page fault), the command lists in
flight (done with their last section, not begun, or stopped part way with the commands about the
place) and, for a page fault, its GPU address with the objects whose memory it is in and those
freed lately, by name: the engine's textures carry their names, the renderer's targets, buffers
and lists theirs (`NamesObjects`).

- `d3d12_DRED` (read at start-up; `XRENDERD3D12_DRED` before it): `1` (the default) the page
  faults and the renderer's sections as markers (those of `d3d12_GpuTimes`, in the PIX event
  runtime's format, the one DRED takes as a breadcrumb's context; PIX shows them too), `2` also
  every command (`DrawIndexedInstanced`, `Dispatch`, ...), `0` off. At 3840 x 2160 on the Pier,
  Swamp and River sweeps `1` costs nothing measurable (0 to 0.5 %, frames bit for bit the same),
  `2` about 1 to 2 %.
- A page fault does not stop the list that makes it (the GPU goes on to the end and the device is
  removed after): look at the address, the freed objects and the last section of the frame's
  last list done.
- `d3d12_DredTest` tests the report in the next frame: `1` removes the device
  (`ID3D12Device5::RemoveDevice`; the report has the reason, DRED no lists for a removal the GPU
  did not cause), `2` writes to the address of a buffer just freed (`XRenderD3D12 DRED test`, in
  the section `dred test`): a real page fault, the driver resets the GPU (the screen goes black
  for a second or two). The renderer draws nothing after either; restart the game.

## Water

- `d3d12_WaterDebug <n>` shows one term of the water's colour instead of the whole: 1 refraction,
  2 reflection, 3 water thickness (black to white over 0-2 m), 4 normal, 5 where the screen space
  reflection hits, 6 its colour, 7 the planar reflection (blurred as the water uses it), 12 the
  RMS of the wave slopes finer than a pixel (white: 0.1; the finest of the three wave layers is
  already lost close to the camera, so it is grey over most of the sea), 16 the sun's glint, 17 the
  sun's light through the crests, 18 the variance of the glint's slopes (white: 0.05), 19 the sun's
  shadow on the water (black), 20 the height of the waves the crests' light reads (black -2, white 2
  of its RMS); with the foam 21 how much of the water's light it hides, 22 the whitecaps' amount, 23
  the shore's (the three without the game's foam strips on the beaches); with the light in the ocean's
  water 24 the light it scatters towards the camera, 25 the share of the scene behind it left (from
  below the share of Snell's window), 26 the share of the sun's light left in the light it scatters;
  with the wakes 27 their slopes (red and green along x and y, 0.5 none, saturated at 0.25 either
  way; blue the RMS of the slopes their mip levels smoothed out, white 0.1), 28 their foam's share
  white; with the ocean's grid 29 its height, 30 the shares of the waves left; from under the
  surface 31 the light the water scatters, 32 the sun's light in it over its mean, 33 the share of
  the scene left.
- The FFT ocean (`d3d12_WaterFFT`, 1; 0 draws the static wave texture and the frames of the water
  before it; not saved; `OceanSpectrum.h`, `OceanFFT.cpp`, `Shaders/OceanFFT.hlsl`): the ocean's
  waves are a wind sea made by inverse FFTs on the GPU each frame (Tessendorf's "Simulating Ocean
  Water"), in place of the texture's sum of sines that only slid along, one shape repeating every
  61, 17.3 and 5.9 m. JONSWAP's spectrum of the wind `d3d12_WaterWind` (3.5 m/s, at least 0.5; a 50
  km fetch: a significant height of 0.57 m, the peak's waves 19 m long), spread about its direction (the old texture's main layer's) by Mitsuyasu's cos^2s, deep
  water's dispersion with the capillary term: each wave runs at its own speed, the long ones faster,
  so the sea changes its shape as it goes and repeats only after 1024 s (its frequencies whole
  multiples of 2 pi / 1024 s, the phase exact in a float). Three cascades of 256 x 256 texels, 250,
  40.5 and 6.9 m a side, each wave in exactly one of them (longer than 4 m, 4 to 0.7 m, 0.7 m down
  to 4 texels, 11 cm; each about a third of the slopes): no two of the tiles nearly share a
  multiple under 900 m (40 and 7 would every 280 m), so their sum does not repeat where the camera
  can see it. The starting amplitudes come from a hash of the cascade and the texel, the same sea
  every run. Once a frame, before the environment cubes, when the frame's list has water, four
  compute kernels make the four packed complex fields of each cascade at the frame's time (Dx + i
  Dy, h + i dDx/dy, dh/dx + i dh/dy, dDx/dx + i dDy/dy), their FFTs along the rows and along the
  columns (Stockham's radix 2 in group shared memory), then the waves' texture: a slice a cascade
  (RGBA16F, 9 mip levels) of the slopes, the height in the weight of the cascade's slopes (as the
  crests' light had it from the old texture) and the squared slope, whose mean over a mip level
  less the square of the mean slope is LEAN's lost variance as before. The sharp crests
  (`d3d12_WaterChop`, 2.5, Tessendorf's lambda; 0 none; not saved): each cascade's waves are moved
  along their horizontal displacement D = i k / |k| H times it and taken again at the texels (three
  Newton's steps towards the point the displacement moves to the texel, the slopes there through
  the inverse of its Jacobian, whose determinant is kept at 0.1 or more where a crest would fold
  over): narrow sharp crests and wide flat troughs, one wave of it Gerstner's trochoid (at 1). The
  wind sea at 3.5 m/s with lambda 1 has its Jacobian's determinant down to 0.7 only, hardly sharper
  than the sines; at 2.5 down to 0.31 (the crests up to 3 times narrower), none folding over; it is
  the wind sea's own displacement, so how much narrower the crests get does not change with
  `d3d12_WaterSlope`. The water's program samples
  the three slices at the world position over their tiles (no scrolling: the FFT moves them) and
  sums their slopes; the planar, screen space and ray traced reflections, their blur, the glint and
  the crests' light read the normal and the lost variance as they did. Their steepness is the old
  texture's (`d3d12_WaterSlope`, 0.0446 the RMS slope along an axis at the default bump amounts,
  0.03 and 0.02, other amounts in proportion): the physical sea's at any wind (0.082 at 2 m/s, 0.096
  at 3.5, 0.106 at 5) is 1.8 to 2.4 times steeper than the game's water was drawn for, so the wind sets only the waves'
  lengths, directions and speeds; `d3d12_WaterSlope 0` gives the wind sea's own. The glint's ripple
  finer than the short cascade is Cox and Munk's variance of the slopes of the wind (0.003 + 0.00512
  U, both axes) less the cascades' (0.0025 at 3.5 m/s, 0.006 at 5, none at 2: then 0.001, the
  least, against a glint glittering in single pixels), times the bump amounts' factor alone, in
  place of `d3d12_WaterGlintRoughness` (0.004, the old texture's): the glint and the sun's path
  widen with the wind (scaled as the slopes, Carrier's glint shrank to a small core of the game's
  patch). The default wind is fitted by the frames: at 5 m/s Bunker's sunset path got as wide as
  tall and milky near the camera (the glint suite's column, 1.02 against at least 1.2), at 3.5 it
  keeps the sparkle and the column (1.25), at 3 Carrier's glint shrinks. The scopes' glass
  keeps its own static texture (the caustics are the waves' since step 290, below). The pass costs 0.02-0.04 ms at any resolution,
  and the water's three samples of a 16-bit array cost less than the old texture's three layers
  (3840 x 2160, the glint suite's views: Carrier 0.75 -> 0.58 ms, with the rays' sun shadows and
  reflections 1.07 -> 0.80; Bunker's sunset 0.63 -> 0.58, with the rays 0.77 -> 0.78; Carrier's
  frame 3.22 -> 3.14 ms with the pass, 4.45 -> 4.22 with the rays); the spectrum, made on the CPU at
  the first water and when the wind changes, takes under 65 ms.
- The FFT ocean's foam (`d3d12_WaterFoam`, 1; 0 draws the frames of the water before it; not saved;
  `Shaders/WaterFoam.hlsli`, `FoamPattern.h`, the foam kernel in `Shaders/OceanFFT.hlsl`):
  - Whitecaps. Their share of the sea is Monahan and O'Muircheartaigh's law of the wind,
    3.84e-6 U^3.41: 0.03 % at the default 3.5 m/s (hardly any), 0.3 % at 7, 1 % at 10, 3.9 % at 15.
    - Once a frame after the waves, a kernel finds where the long and the middle cascades' crests
      break (half of the share each; the short cascade's ripple traps no air). That is where the
      surface is squeezed most: the first order of the Jacobian's determinant of the displacement,
      1 + lambda div D, falls below a threshold.
    - The squeeze is a gaussian with the RMS of the cascade's slopes, so the threshold is the normal
      quantile of the share breaking at a time, `WHITECAP_ACTIVE` (a quarter) of Monahan's. The
      determinant's own second order lifts its lowest values: at 12 m/s only 0.009 % of the long
      cascade lay below the threshold meant for 0.23 %.
    - The foam stays where its crest broke (the water does not travel with the wave) and fades as
      exp(-t / 4 s). It spreads as it fades, a diffusion of 0.5 m^2/s (the middle cascade's as much
      in its texels): its patches grow about 2 m and the share white is kept.
    - Its state is kept in two RGBA32F arrays in turn. A GPU may round a half float towards 0 as
      it stores it, and in 16 bits the foam faded 4 % faster over 2 s at 60 frames a second. A
      second kernel copies the state into the view the water reads, RGBA16F with its mip levels as
      the waves' (two 32-bit texels a pixel cost the water 0.11 ms more at 4K on Carrier).
    - The kernels run at most 120 times a second; between, the water reads the last view.
    - Run over 12 s of the sea, the share white is Monahan's within 8 % at 7, 10 and 15 m/s
      (1.07, 1.01 and 1.06 of it; `Ocean_WhitecapCoverage`).
    - The middle cascade's whitecaps break more on the long waves' crests (Longuet-Higgins and
      Stewart's modulation), their share over the sea the same.
  - Shore. A wave breaks where the water under the surface is shallower than its height over the
    breaker index.
    - The index is McCowan's 0.78 on the mean, 0.55-1.0 from a slowly drifting noise (it changes
      from wave to wave and along a crest).
    - The wave's height is twice the long waves' crest there, the highest of now and of what was
      0.7 and 1.4 s up the wind at the spectrum's peak speed. It is at least a quarter of the
      significant height, scaled as the slopes.
    - The foam is the square of the share of that depth left: it gathers at the edge, with lace
      further out.
    - Against rocks, walls and what stands in the water, where the bottom drops steeply and no
      wave breaks on its way there, the foam of the waves running up them reaches out twice their
      height along the view's path through the water.
    - At the default wind both are thin rims (a calm lagoon). On the beaches the game draws its own
      foam strips running up the sand, as before.
  - Surf. The shores' waves are mostly swell, long waves from far away that break even in a calm:
    `d3d12_WaterSurf` (0.8 m; 0 none, only the wind sea's) is its height there, its period 9 s
    (a trade wind swell's 8-12 s). It is not in the waves drawn, only in their foam.
    - It breaks where the water gets shallower than its height over the breaker index above (1 m
      on the mean) and runs on to the shore as bores. A bore's front is where its travel time from
      the break point has come: sqrt(depth / that depth) of the whole on a plane beach (a bore runs
      at sqrt(g h)), so the fronts follow the depth's lines and run shorewards, one in the zone at a
      time (`XR_FOAM_SURF_BORES`; on a beach of 1:14 the travel time is a period). A noise 40 m
      across shifts their phase along the shore.
    - Each front carries a white roller, most of it over the fifth of the period after it, and
      leaves a lace that fades until the next one. The foam rises over the 3 % of the period before
      a front, so there is no hard edge in time or across the screen. The breaking grows over the
      first fifth of the way, and each bore has its own strength (0.55-1: the swell's sets).
    - A breaking wave's white is torn into clumps with water between them: three octaves of value
      noise (2.2 m, a third and a seventh of it), steep about their mean, scale each bore's foam
      0.1-1.9 times, each bore's its own and in place while it passes. Without them the roller
      was an even white band, like fog, a few metres off (where the pattern is finer than the
      pixel).
    - The game's beaches drop steeply under the water, so the surf zone is a few metres wide: a
      white band that runs to the edge and its lace, under and beyond the game's strips.
  - Pattern. Foam is a lace of bubbles: a 512 x 512 tiling texture made at the start.
    - It holds four octaves of Worley's cells (rims and fill) and patches of value noise, ranked so
      that each value is as common as any other. The water is white where the pattern is above one
      less the amount, so the amount is the share white.
    - Two samples (tiles of 3.7 and 2.3 m, the second turned 37 degrees) are mapped back to one
      uniform value.
    - The pattern sways with the waves, taken where the water moved from, and drifts at 3 % of the
      wind. Where it is finer than the pixel it is the amount itself.
  - Light. Thin foam (a low amount) lets some of the water's light through. The foam is a white
    diffuse layer (albedo 0.6) under the sun (wrapped a little, in its shadow none) and the sky,
    over the water's colour; the glint and the crests' light are gone under it. A patch is thicker
    in its middle and thins to its edge: its brightness rises 0.75-1.25 times from its edge to its
    middle by the pattern's value, 1 on the mean, so dense foam shows its bubbles' clusters (none
    where the pattern is finer than the pixel).
  - None from below, in the environment cubes or in heat vision. `d3d12_WaterDebug` 21, 22 and 23
    show it.
  - Cost at 4K: the water 0.05-0.08 ms more (Carrier 0.58 -> 0.64 ms, Bunker's sunset 0.57 ->
    0.65, the surf's beach 0.51 -> 0.58), the foam's kernels 0.03 ms. The shore's terms and the
    pattern run only where a pixel's quad needs them (most of the sea has no foam).
- The FFT ocean's caustics (`d3d12_WaterCaustics`, 1; 0 draws the static caustics texture, the
  frames before; not saved; `d3d12_Caustics 0` none at all; the caustics kernel in
  `Shaders/OceanFFT.hlsl`, `Shaders/WaterCaustics.hlsli`): the sun's light on the shallow floor
  gathered and spread by the same waves the water draws, in place of a texture of its own waves.
  - Once a frame after the waves, when the frame draws the floor under the ocean in daylight, a
    kernel follows the sun's rays through the middle and the short cascades (40.5 and 6.9 m; the
    long one's waves focus only far below the game's floors). Each texel's ray is refracted at its
    normal (Snell's law, 1.333) and carries the light the surface lets in for its area seen from
    above (a face turned to the sun more, one turned away none); its move against a level
    surface's at each of four depths, each twice the one before (the short cascade's 0.5-4 m, the
    middle one's 2-16 m: where their waves focus the sun at the drawn steepness), is where it
    lands.
  - The rays are 4 x 4 a cell between four texels, their moves and light bilinear between the
    texels' (where a wave spreads them 1.5 times 2 x 2 left a ripple of 8.7 %, 4 x 4 1.1 %), splatted
    bilinearly into the four depths' texels in group shared memory, a tile of 32 x 32 texels a
    group with a margin of 8 (at the deepest depth 0.05 % of the game's sea's rays move more than 5
    texels, the physical sea's 4 % more than 7; a cell whose rays cannot reach the tile is
    skipped). The light is kept in fixed point and its mean is the sun's light, 1 at every depth:
    the rays leaving a tile land in the next one, so the caustics move the floor's light around
    and do not add to it (the static texture brightened the floor by 4-6 %).
  - The kernel writes the texture (RGBA16F, the four depths, a slice a cascade) and its mip levels
    down to 16 x 16 texels (no kernel for the rest). A GPU may round a half float towards 0 as it
    stores it: the kernel rounds to the nearest itself (the mean was 0.9993, now 0.99999).
  - The floor's program takes each cascade where the sun's refracted ray from the floor crossed the
    surface, at the floor's depth (linearly to the first depth from 1 at the surface, between two
    by the log of the depth), blurred by the sun's disc (0.53 degrees over 1.333: its width at
    that depth is the least of the gradients), the cascade's mean where a pixel takes more than its
    last mip level's texel (no shimmer far off), and multiplies the two. The floor's light is
    1 + k (I - 1), at most 4 times: k is the sun's share of it (0.6; less at a low sun), fading to
    none at 16 m (the light scattered in the water evens them out), none in the sun's shadow (one
    bilinear comparison of the cascades where the caustics change the light, the rays' mask with
    `d3d12_RTSunShadows`).
  - The game's sea at its 3.5 m/s wind and the drawn slopes still shows them by 1-2 m of depth (the
    short cascade's light's RMS 0.31 at 1 m, 0.67 at 2) and focuses them by 4 m; the middle
    cascade's longer waves are faint there. They change with the waves: the frames 0.02 s apart
    correlate 0.67, 1 s apart 0.01.
  - Cost at 3840 x 2160: the kernel 0.07-0.08 ms (the ocean's section 0.07 -> 0.14-0.15 ms), the
    floor 0.03 ms more (Carrier 0.53 -> 0.56 ms, the surf's beach 0.71 -> 0.74).
- The light in the ocean's water (`d3d12_WaterVolume`, 1; 0 draws the fog volume's fog, the frames
  before; not saved; `Shaders/WaterVolume.hlsli`): the scene behind the surface seen through the
  water and the light the water scatters towards the camera, in each channel, in place of the fog
  volume's colour laid over the scene by distance.
  - The water's attenuation per metre in each channel comes from the level's fog volume, its colour
    C (in linear light) and density (ln 100 over twice its distance): a channel as bright as C's
    luminance by the density, one half as bright twice as fast (deep water's colour is the light it
    scatters back over the light it absorbs), at most 64 times, at least pure water's absorption
    (Pope and Fry 1997: 0.26, 0.057 and 0.011 per metre at 610, 550 and 470 nm). Carrier's water (C
    0.18, 0.35, 0.57 in gamma, 60 m) is pure water's in red and green, 0.013 in blue; Bunker's and
    the river's murkier water attenuates all three faster.
  - The scene behind (the copy of the colour buffer) keeps exp(-c (s + d / cos t)): s the view's path
    down to it along its ray refracted at the waves' normal (Snell's law, 1.333), d its depth under the
    surface, t the angle of the sun's ray refracted into the water. The water scatters the light
    coming down (attenuated as fast) towards the camera along the refracted ray, single scattering:
    C 2 / (1 + m) (1 - exp(-c (1 + m) s)), m the ray's cosine to the vertical, so deep water seen
    straight down is C. Shallows over sand turn turquoise (red fades in the first metres, green and
    blue stay: at 2 m in Carrier's water the sand's green over red twice its own), the deep water the
    level's colour, darker with the depth.
  - The sun's share of that light (0.6, less at a low sun; none in the environment cubes and in heat
    vision) is the sun's light where each of four segments of the ray takes it (their ends at the
    reach times (j / 4)^2, the reach 3 over the least attenuation: 95 % of the least attenuated
    channel's light comes from nearer): one bilinear comparison of the cascades where the sun's
    refracted ray to the segment's middle left the surface. A pixel's quad shares its four, lane q the
    q-th segment's of its own ray (a quarter of the cost of four a pixel; the quad's rays a pixel
    apart); none where all of the ray's points are beyond the cascades. The water darkens in the
    shadows of the cliffs, the wreck and the pier, and the sun through gaps lights shafts in it.
  - From below, Snell's window: the light from the air through the waves' surface by Fresnel's
    transmission from water to air (unpolarized; none beyond the critical angle, 48.6 degrees, where
    the surface mirrors the water below: its colour), the scene above along the view's ray refracted
    out of the water (the copy of the colour buffer where it shows that direction above the surface,
    else the sky box), then the fog under the water as before. The sun's flare and coronas seen from
    under the ocean are moved to the sun's direction refracted into the water.
  - `d3d12_WaterDebug` 24, 25 and 26 show its terms (with `r_HDRRendering 0`: else tone mapped).
  - Cost at 3840 x 2160: the water 0.04-0.08 ms more (Carrier 0.65 -> 0.72 ms, Bunker's sunset 0.66
    -> 0.74, the surf's beach 0.58 -> 0.64, Fort's reefs 0.34 -> 0.38; with the rays' sun shadows and
    reflections Carrier 0.90 -> 1.07), most of it the sun's comparisons (four a pixel cost 0.12 ms on
    Carrier, the quad's shared 0.05); with `d3d12_WaterVolume 0` 0.01-0.02 ms. The debug views'
    terms are returned where they are made: kept to the end of the program they held registers
    through all of it, the water 0.05 ms slower with `d3d12_WaterVolume 0` too.
- The wakes (`d3d12_WaterWakes`, 1; 0 draws the frames before; not saved; `Wake.cpp`, `WakeBodies.h`,
  `Shaders/Wake.hlsl`, `Shaders/Wake.hlsli`): the waves of what moves through the ocean's surface, a
  boat's wake and the ripple round a swimmer, and their foam.
  - Waves. Linear deep water's waves in a window of 1024 x 1024 cells of 12.5 cm (128 m) around the
    camera, Tessendorf's eWave: the height and the velocity potential's filtered part, each wave
    number turned exactly over a step (FFTs along the rows and the columns in group shared memory,
    three kernels a step), so the waves run at deep water's speeds whatever the frame's time (within
    1e-6 m over 50 steps, `Wake_Dispersion`). Each texel is the cell of its index modulo the window's
    size: the window follows the camera by cells with nothing copied (the cells new to it start
    calm), and a sponge 12 m wide at its edge takes the waves before they would wrap (0.0001 of their
    energy left). They fade over a minute (0.02/s), the shortest the cells carry faster.
  - Bodies. The entities with physics that move (rigid bodies, vehicles, characters, ragdolls) whose
    box crosses the ocean's level over water in the window, the 32 nearest the camera, none inside a
    larger one (a crew in its boat). Each is an ellipse across its box at the waterline (0.85 of its
    half sides, a hull narrows to its ends; at least 0.5 m) pushing aside its depth below the level
    (at most 1 m), deepest in its middle ((1 - r^2)^2); a character's 0.35 m across and 0.05 m deep
    (only a swimmer's head and shoulders cross the surface). Each step adds the change of that
    displacement to the height: a body at rest makes no waves, a moving one its wake (Michell's thin
    ship). The steps follow the frame's time, up to four a frame, a body moving at most 0.5 m a step;
    a frame longer than 0.1 s steps 0.1 s (the waves slow down rather than jump), a pause of more
    than 0.5 s (a load, a cut scene) clears the window, a body first seen makes none.
  - Behind a slow body the wedge is Kelvin's 19.47 degrees (`Wake_Kelvin`: 17.7 at 2.5 m/s), a fast
    hull's narrower, as Rabaud and Moisy measured (about 0.2 / Fr at a Froude number over its length
    above 0.6): Fort's Zodiac at 7.5 m/s draws its strongest crests at 5.5 degrees and the wedge's
    edge at 12. A boat's waves are 0.29 m high (a planing boat's 0.1-0.5 m), a swimmer's at 1.5 m/s
    6 cm with slopes up to 0.35, below breaking (`Wake_Height`). In cells of 25 cm a swimmer's
    waves, under a metre long, were left a blur of ripple finer than the cells.
  - Foam. Once a frame the view: the slopes (central differences), their squares and the foam's
    share of the surface white, RGBA16F with 4 mip levels. The foam is at least a fast body's where it
    is (from 1.5 m/s, 0.7 at 5.5 m/s: its white water) and the waves' that break (steeper than
    Stokes' limit, k A 0.44; none under a body, its hollow's sides), fading over 4 s and spreading
    0.18 m^2/s: a trail widening behind a boat. The water tears it into clumps (three octaves of value
    noise 1.2 m across, in place, the less the thicker the foam: the white water just behind a hull
    white all over, its trail patches with water between them; even it was fog a few metres off).
  - The water adds the view's slopes to the ocean's waves (the variance their mip levels smoothed out
    to the reflections' blur and the glint's, as LEAN's) and its foam to the ocean's, faded out over
    the sponge. None on the water volumes, in the environment cubes or from below.
  - Calm. After 60 s with nothing in the window faster than 0.1 m/s (a boat at its mooring bobs at
    0.03) the waves have faded and most have run out through the sponge: no more steps, the water's
    wakes fading out over the last 15 s; the first body that moves again clears the window. A window
    cleared (at the start, after a pause) is calm until something stirs it.
  - Cost at 3840 x 2160: the wakes' kernels 0.08 ms a step (one a frame at 50 frames a second; the
    window of 512 cells of 25 cm cost 0.03 ms), the water 0.09 ms more with a boat's wake in view
    (Fort's chase 0.84 -> 0.93 ms, with the rays' reflections and sun shadows 1.23 -> 1.39; the frame
    4.27 -> 4.42 ms, 5.53 -> 5.76), nothing where nothing moves (Carrier's water 0.75 ms either way).
- The waves' geometry (`d3d12_WaterGeometry`, 1; 0 draws the frames before; not saved; `OceanGrid.cpp`,
  `OceanGrid.h`, `Shaders/OceanGrid.hlsli`): the ocean's surface raised to the waves' height, so that
  their crests stand against the sky and the shore and their troughs hide what is behind them.
  - The grid. The game draws the ocean as one disc around the camera at the level (rings of
    segments 10 degrees wide, 1244 m across on Fort), the terrain hiding it on land. In its place a
    polar grid of 512 segments around the camera: rings 12.5 cm apart (the wakes' cells) out to
    10.2 m, where the segments are as wide, then each a segment's width further out (square cells,
    as wide on the screen at every distance) out to 500 m, then 1.25 times as far each out to 20 km;
    the vertex program pulls the vertices beyond the game's disc onto its edge, so that the grid
    covers what the disc did (214,017 vertices, 427,520 triangles, made once).
  - Rings by the frame (`OceanGrid::FrameRings`). Far off from a low camera those rings are finer on
    the screen than a pixel, triangles thinner than a pixel that shade their quads for nothing. Each
    frame the radii are spaced so that a ring's step rises at least 4 pixels on the screen where the
    waves face the camera: dr >= 4 r^2 / (P (h + 0.4 s r)), P the pixels a radian (the projection's
    y scale times half the target's height), h the camera's height over the level, s the long
    cascade's RMS slope as drawn; never closer than the rings above. From 2 m over the water fewer
    than two thirds of the rings fall within Fort's disc; from 100 m up the rings within 200 m are
    those above. The vertices' z is their ring; the vertex program reads its radius from a table
    after the disc's.
  - Depth. The grid writes the depth buffer, its rings drawn from the camera out: the faces of the
    waves behind a nearer crest fail the test (drawn later over it, the storm's water cost 0.9 ms
    more at 4K). What reads the depth after the water (the volumetric light's shafts, the blended
    list's test) stops at the waves.
  - Shaded once a pixel (`d3d12_WaterDeferred`, 1; 0 shades the grid as it is drawn; not saved).
    The grid's cells are some 30 pixels across at 4K, and its triangles shaded the 2 x 2 quads
    along their edges twice (Carrier's water 0.41 ms more than the flat disc's at 512 segments,
    0.13 ms at 128). So the grid is drawn into the depth alone, without a pixel shader, and the
    water's program (`Shaders/Water.hlsl`'s `PSDeferred`) shades a triangle over the view where
    the depth buffer holds a nearer surface than the scene's copy: the world position from the
    depth along the pixel's ray (the upscaler's jitter taken off), the disc's alpha from a table
    made each frame (below), the still surface's motion by the grid's matrices; a quad without the
    waves does none of it, the pixels of a quad with them that have none are discarded after.
    The triangle lies at the depth of the farthest water (the disc's edge beyond the camera, 20 m
    further), the depth test GREATER: the sky and what is further off start none of the program
    (over them it only found no water: Bunker's sunset, two fifths sky, 0.96 -> 0.84 ms, with the
    rays 1.50 -> 1.29). The textures' mip levels come from the position's derivatives across the
    quad: where a quad holds a crest and the water behind it, or the water and what stands in
    front of it, a level too coarse for single pixels along the edge (the neighbour nearer in
    depth's derivatives with `SampleGrad` cost 0.06-0.15 ms more). With MSAA's targets and in the
    debug views 29 and 30 the grid is shaded as drawn. The frames are those of the grid shaded as
    drawn but for such pixels (at 3840 x 2160 more than 24 levels apart: none of Carrier's frame
    250, 0.001 % of Fort's chase, along the gun; the storm at 1280 x 720 0.09 %, along the
    horizon).
  - The disc's vertex alpha (the game's fade of the water towards the shore and with the distance,
    which weighs the reflection) is read from its vertices each frame (`OceanGrid::ReadDisc`: its
    centre, level, rings and each ring's alpha by segment, whatever order its strip gives them in)
    into a table the vertex program interpolates between, bilinear in the ring and the segment as
    the disc's triangles are along their edges. The deferred water takes it per pixel from a second
    table made each frame (`OceanGrid::AlphaTable`): 256 rows of the 36 segments' alphas, the rows
    a geometric series of 1 + r / 0.5 m (22.6 a doubling out to 1244.5 m: 3 % apart far off, 1.5 cm
    at the centre), the last at the disc's edge; bilinear between them, two loads (the search among
    the disc's rings for each pixel cost the water 0.04-0.12 ms at 4K). Where the alpha bends at a
    ring between two rows it is within a quarter of the slope's change times their spacing.
  - The height: the FFT ocean's cascades, each at the mip level whose texel is as wide as the grid's
    spacing there (no wave shorter than two cells: those would crawl over the vertices), and the
    wakes' (the window's height, a texture of its own with 4 mip levels written after their view).
    Vertical only: the cascades are already moved onto the world's grid (`OceanFinish`), their
    height the displaced surface's.
  - Limits. In shallow water a crest at most 0.39 of the depth under the level above it, a trough
    as far under it (half McCowan's breaker index; a smooth limit; the depth from the terrain's
    heights, a texture made for each level), none on the shore, where the terrain hides the grid as
    it hid the disc. None of the ocean's waves under the bodies in the water (the game floats a boat
    on the level: the waves would run through its hull), all of them again 1 m beyond its box; a
    wake's waves stay. Around a camera within 0.75 m of the level the surface is flat out to 2 m, all
    of the waves from 8 m on (the game takes the camera as under the water by the level alone: a
    crest over it would cut the view in two); no crest rises closer than 0.3 m under a camera over
    the water there, the limit rising 0.5 m a metre further out (from a boat in a high sea the
    crests a few metres off do stand over the eye), a trough as far over one under it. The waves
    fade out from 250 to 500 m, where the rings are too sparse for them.
  - The water's program takes the camera as under the water by the level, as the game does, and the
    foam of the shore and of what stands in the water by the depth under the still level along the
    view (with the displaced surface's, a crest standing in front of a boat made the boat behind it
    the shore's white).
  - What stands over the water behind a crest is hidden by it (`GridHiddenShare`): the view's ray
    refracted into a wave goes down into the water (at least 41 degrees under the surface, Snell's
    window), under the trough beyond the crest, not on to a boat's side, the shore or the sky behind
    it. The water's light through the crest is taken down to the floor under it (the terrain's
    heights; deep water off them), the scene's colour as that floor's (in shallow water the beach
    behind as its sand): all of the scene from 0.5 m over the depth of the crest's height under the
    level, none of it lower (none under the flat disc or a trough, none of a shallow floor: a crest
    there at most 0.39 of the depth). Seen as through the water down to their height, the boats and
    the sky behind the storm's crests showed through them as through glass (debug view 25 over the
    storm's crests 1.0, now 0.095: Fort's floor under them). The foam of the shore and of what
    stands in the water takes the same floor's depth (`GridBehindZ`: a boat's waterline behind a
    crest drew the foam of the waves against its side over the crest).
  - The default sea (3.5 m/s, its significant height 0.15 m) changes little but at the silhouettes
    and the wakes; at 12 m/s (`d3d12_WaterWind 12`, `d3d12_WaterSlope 0`) the crests 1-2 m high a
    few tens of metres off stand over a camera 1.3 m over the water and hide the boats behind them
    (the game's boats do not ride the waves: they float on the level).
  - Cost at 3840 x 2160 (`d3d12_GpuTimes`, no upscaler), the water against the flat disc's, the
    raster / with the rays' sun shadows and reflections: Carrier 1.15 -> 1.24 / 1.66 -> 1.81 ms,
    Bunker's sunset 0.77 -> 0.84 / 1.18 -> 1.29, Fort's chase 0.96 -> 1.07 / 1.50 -> 1.65, the storm
    from 1.3 m 0.81 -> 0.87 / 1.24 -> 1.35 (the grid shaded as drawn 1.56 / 2.15, 1.06 / 1.63,
    1.33 / 2.03, 1.72 / 2.57); the frame as much more. The motion vectors are the surface's as it
    is (as the grid's shaded as drawn: the waves' own rise and fall left out).
  - `d3d12_WaterDebug` 29 the height (grey 0; red up and blue down, 0.3 m from grey; green 3 cm),
    30 the shares of the waves left (red the depth's, green the bodies', blue the camera's and the
    distance's).
- The view from under the ocean's surface (`d3d12_WaterUnder`, 1, with `d3d12_WaterVolume`; 0 draws
  the frames before; not saved; `Underwater.cpp`, `Shaders/Underwater.hlsl`,
  `Shaders/Underwater.hlsli`): with the camera under the level, the light in the water seen from
  inside it, in each channel, in place of the fog volume's colour laid over each surface by its
  distance.
  - The pass. The main view leaves out the ocean's fog volume's fog passes, the ocean seen from below
    gives its surface's light alone (Snell's window and, beyond it, the water below it mirrors: the
    deep water's light along the mirrored ray, below), and after the water
    surfaces the blended list copies the scene and draws a triangle over the view: each pixel's
    scene, at its distance from the depth copy (through the water up to the surface at most: the
    level, or 4 m over it with the ocean's grid where the copy has none of its surface), keeps
    exp(-c (s + d / cos t)) of its light, s its path through the water, d its depth, t the sun's
    refracted angle (as the light in the water's from above), and the light the water scatters
    towards the camera along the path is added.
  - The light it scatters: the light coming down attenuated by exp(-c d) to the depth d of each
    point of the ray, single scattering, taken by the water on the way back (exp(-c t) at the
    distance t), in closed form over each of 16 segments of the ray, each the same share of that
    light in green (the nearer ones shorter; out to the reach, 3 over green's attenuation along the
    ray, the last one's sun beyond). Looking level from 5 m under Carrier's water: blue over green
    over red, darker the deeper the camera; the floor's red gone within some 10 m of its light's
    path, its sand blue further off.
  - Where that light comes from (`d3d12_WaterUnderClear`, 1; not saved): the scene's sun and sky
    coming down through the surface, scattered by the water as clear sea water scatters
    (`UnderDownLight`, `UnderScattering`, `UnderClearSource`; the same for every pixel, worked out
    once a frame on the CPU, `UnderwaterLight.h`, the plants' program in its own). The light
    coming down just under the surface: the sun's (a white face turned to it, twice its colour) through the flat surface
    (Fresnel's transmission at its refracted ray, the beam's light over a horizontal face's: cos in
    the air over cos in the water, none under the horizon), the sky's (the fog's colour, on a face
    facing up) 0.934 / 0.83 times (through the surface within Snell's window), as much again coming
    back up from the floor and the water below; over 4 pi a steradian's share. The scattering: pure
    water's molecules' (0.00288 (500 / wavelength)^4.32, Morel 1974) and the particles': in green
    all that the water takes beyond pure water, in red and blue green's over the wavelength (610,
    550, 470 nm), at most what each takes beyond pure water (the rest of blue's absorbed by what is
    dissolved and the plankton). Clear water scatters little of what it takes, so the water far off
    is a few times darker than the sand the sun lights, about its fog colour's hue (a little bluer),
    hardly any red, darker deeper and looking down. Beyond Snell's window the surface sends down
    the light the water below scatters up along the mirrored ray, s / (c (1 + m)) times the phase
    function's share towards it, the same as a level view sees far off at the surface's depth: at
    the horizon the surface and the water below it meet without a line. 0: the water seen from
    above's light (2 c times its fog colour, deep water seen straight down that colour), the
    surface beyond the window 2 / (1 + m) of its colour: the water far off as bright as the sunlit
    sand (a milky fog), the surface over a line at the horizon (the frames before). Under Fort's
    water (`Under_fort.ini`, the film fort-islet's underwater views; at 1280 x 720 the
    raster: the water above the floor 0.47 of the luma it had, the near sand 2.1 times the far
    water's in place of 0.94, green 14 times red, Snell's window 0.90 of the surface beyond it in
    place of 0.56, the horizon's steepest step 0.42 of what it was).
  - The eye under the water (`d3d12_WaterUnderAdapt`, 1, and `d3d12_WaterUnderExposure`, 1 EV; not
    saved; with `r_HDRRendering`): the HDR resolve multiplies each channel of the light by a factor
    before the tone map (`UnderBalance`, `UnderwaterLight.h`'s `Balance` and `UnderExposure`). The
    white balance at the camera's depth z, as a diver's eye (or a camera set at the depth): the
    light there, the light coming down just under the surface times exp(-c z) (red gone first),
    taken towards that light's colour by von Kries' factors exp(c z) raised to the degree
    (0 to 1), scaled to keep that light's luminance (the exposure as without); none at the surface.
    The factors at most 4 times apart, the degree less where they would be more (under Fort's water
    deeper than 5.7 m: at 20 m red's would be 53 times, a lamp or the sky in Snell's window red
    there), as an eye adapts only so far: the deep water bluer again; without light all 1.
    The exposure: 2^EV times the light, coming in over the first half metre under the surface (the
    view through the surface the same either side of it). The clear water's light (above) is about
    middle grey in luminance but a dense blue in colour, red's share a tenth of green's at a few
    metres: through the ACES tone map it was a dark saturated blue, the sand in it blue too. With
    both (the user's choice of four: neither, the balance, the balance and 0.5 or 1 EV) the water
    is a lighter, less saturated blue, the floor nearer its own colour. 0 and 0: the frames before.
    Under Fort's water (`Under_fort.ini`, 1280 x 720, the raster, the game's glare off): the
    balance takes the water's saturation 1.00 -> 0.75, its red 0 -> 0.36 of green, the near sand's
    blue over red 3.1 -> 2.0; the exposure the frames 1.6 times brighter; above the surface
    nothing changes (0 px); the resolve's time at 3840 x 2160 the same (0.23-0.24 ms).
  - The sun's share of it (0.6, less at a low sun) is the sun's light at a point of each segment (one
    bilinear comparison of the cascades where the sun's refracted ray to it left the surface, then
    the FFT ocean's caustics there at its depth, as on the floor: their mip level from the pixel's
    footprint and the sun's disc's blur): the shafts of light under the waves, radiating from the
    sun's refracted direction, and the dark between them in the shadows of the cliffs. They are
    taken in a pass of their own (`d3d12_WaterUnderShare`, 1; not saved; the GPU section
    `underwater taps`): one point of each pixel's ray into a target of the scene's size (R32F),
    in the segment (x mod 4) + 4 (y mod 4) of the sixteen, moved within it by the pixel's noise
    over the frames (the temporal anti-aliasing's); the view's pass reads the sixteen of the 4 x 4
    window round the pixel (each segment once wherever it starts) in four gathers, its rows and
    columns turned by the window's place modulo 4 (the aligned blocks' sixteen, apart from their
    neighbours', were squares in the shafts' noise), and weighs them along its own ray, the
    segments' weights in closed form: green's each
    1/16 of its light by the segments' making, red's and blue's the light at the j-th end
    u^(k / k_g), u = 1 - (j / 16) (1 - exp(-k_g r)) (one log and two exps an end in place of six
    exps and three divisions a segment; segment by segment, two exps each, where a channel's
    attenuation along the reach is under 0.05: looking up at a surface within half a metre). The
    same light along each ray as before to a float's rounding (test `Under_Closed`: 1e-4 of it);
    the shafts' points over a window of 16 pixels in place of a quad's 4, their noise elsewhere
    (Under_carrier's views at 1280 x 720 against `d3d12_WaterUnderShare 0`: the means of the
    debug views 31 and 32 within 0.08 %, their differences blurred over 32 x 32 pixels 0.2-0.4 %
    and 0.9-1.2 % of them, the frames' mean |d| 0.5-1.2 levels). 0: a pixel's quad shares them,
    lane q the segments 4 i + q of its own ray, the point in each moved by the pixel's noise and by
    the golden ratio from one to the next, each segment's weights by its own exponentials (the
    frames before). Scattered forwards most: sea water's phase function, half the molecules'
    (Morel's, 1 + 0.835 cos^2), half the particles' (Henyey and Greenstein at Petzold's mean cosine
    0.924), its mean 1: looking up the sun's refracted rays the water glows round them (half the
    peak within 3.5 degrees, a tenth at 8.6), some 230 times as bright as away from them.
  - The blended objects after the pass (the plants in the water) take the same light over their
    own colour in their own program (`General.hlsl` with `UnderFog`: the same path, attenuation and
    scattered light, the sun's light its mean along the way): blended over the water behind by
    their alpha, a (colour T + L) + (1 - a) (behind, already so), exactly; an added light
    attenuated alone; none over a blend that multiplies the frame. With the fog volume's fog over
    them they were yellow-green on the blue sand a few metres off (the share of Carrier's view
    greener than blue 0.134, now 0.0004). Those before the water surfaces (wholly over the water)
    are the pass's.
  - The fog of the scene, the flares and the sprites as before; the sun's flare from under the
    ocean at the sun's refracted direction (`d3d12_WaterVolume`) and through Snell's window no
    longer fogged.
  - `d3d12_WaterDebug` 31 the light the water scatters, 32 the sun's light in it over its mean (grey
    1, white 2: the shafts), 33 the share of the scene left (with `r_HDRRendering 0`); the blended
    objects after the pass show the same term.
  - The cost at 3840 x 2160 (`d3d12_GpuTimes`, `Under_carrier.ini`'s five views, no upscaler, RX
    7900 XTX): the pass (`underwater` and `underwater taps`) 0.77-0.84 ms (0.45-0.55 and
    0.22-0.39), the frame 3.01-3.93 ms; with the rays' sun shadows and reflections 0.74-0.85 ms,
    the frame 3.78-4.76 ms. With `d3d12_WaterUnderShare 0` the pass 1.10-1.31 ms, the frame
    3.32-4.42 ms; with the rays 1.06-1.29 ms, 4.17-5.27 ms (without the light in the water, step
    293's frames, 2.20-3.29 ms and with the rays 3.02-4.15). Above the water none of it runs.
- The sun's glint (`d3d12_WaterGlint`, 1; 0 draws the frames of the water before it; not saved;
  `d3d12_WaterSunGlint`, a highlight fitted by eye and off as in the game, is gone with it) is the
  sun mirrored by the waves' facets (`Shaders/WaterLight.hlsli`): Beckmann's distribution of the
  slopes about the pixel's normal, whose variance is the one the mip levels smoothed out of it (the
  LEAN variance below, in full) and `d3d12_WaterGlintRoughness` (0.004) for the ripple finer than
  the wave texture's 2.3 cm, Smith's masking, Schlick's Fresnel of 2 %, as bright as a white face
  turned to the sun (the game's twice the sun's colour). Close to the camera the waves the pixels
  resolve glitter; further on the lost slopes widen them into the sun's path towards the camera, a
  column under the sun at Bunker's sunset (36 degrees up), a wide patch under the higher suns of the
  other levels (41-80 degrees), Steam's moon's too. It is at most the light of the sun's flare
  (`d3d12_HDRSun` with `r_HDRRendering`, white without), so in HDR the path goes on above paper
  white to the peak. In the sun's shadow there is none: the cascades' (a tap; beyond them the ocean
  is lit, the water volumes are not), with `d3d12_RTSunShadows` a ray, cast only where there is a
  glint to shade. None in the environment cubes and in heat vision. The water costs 0.04-0.07 ms
  more at 3840 x 2160 (Bunker's sunset 0.56 -> 0.60 ms, Carrier towards the sun 1.01 -> 1.07, with
  the rays' shadows and reflections 1.22 -> 1.29).
- The sun's light through the thin crests of the waves (`d3d12_WaterCrestLight`, 1, its strength;
  0 draws the frames of the water before it; not saved; `Shaders/WaterLight.hlsli`): lit from
  behind, the top of a wave scatters the sun's light forwards, towards a camera that looks at the
  sun past it, turquoise (the red absorbed on the way). A model fitted by eye, not a simulation of
  the light in the water: the upper part of the ripple (the wave texture's height, each layer's in
  the weight of its slopes, so that the light lies on the waves the normal shows, not in patches of
  the 61 m layer tens of metres wide; above the mean height, all of it from twice the RMS), the
  square of the cosine between the view's ray and the sun's, more on the faces turned away from
  the sun (half on a level face, all from the waves' RMS slope away from it, none from it towards
  it), the slant (1 - N.V); 8 times that, at most 1, times 4 the light of a white face turned to
  the sun (at 1 it hardly shows against the sky the water mirrors at a slant), times what the
  surface does not reflect (1 - F). Its colour a turquoise (0.10, 0.62, 0.50) half in the hue of
  the level's deep water, times the sun's light: blue-green by day (Carrier, most where the water
  mirrors a dark rock), green-gold at Bunker's sunset. None with the sun behind the camera or set,
  in its shadow (the glint's), in the environment cubes and in heat vision. It costs the water
  0.03-0.08 ms more at 3840 x 2160 (Bunker's sunset 0.62 -> 0.65 ms, Carrier towards the sun 1.09 ->
  1.13; with the rays' shadows and reflections 0.75 -> 0.82 and 1.30 -> 1.38, the shadow's ray now
  cast for more of the water).
- With `r_WaterReflections 1` the ocean reflects the scene seen by the camera mirrored in the water
  plane, which the 3D engine renders with `I3DEngine::DrawLowDetail` before the main view;
  `d3d12_WaterReflectionFlags` are the `EDrawLowDetailFlags` it gets (default 4184: the sky, the
  terrain, the static objects, among them the pier and the huts, and the far sprites; 4160, the
  sky and the terrain, is what the original renderer passes; not saved in `system.cfg`, which kept
  4160 before). The entities (+ 2048) are left out: drawn for the mirrored camera, they change
  what the main view draws after it (the fish of Training swim with other poses; with the water at
  `d3d12_WaterDebug 1` the frames otherwise stay those of 4160). `d3d12_WaterReflectionScale` is
  its size as a fraction of the screen (0.5), and
  `r_WaterUpdateFactor`, `r_WaterUpdateDistance`, `r_WaterUpdateDeltaAngle` how often it is redrawn
  (0, 0, 0: every frame). Its target has mip levels (up to 8, made after each redraw, about
  0.03 ms at 2560 x 1440), from which the water blurs it where the ripple gets finer than a pixel:
  the wave texture's alpha holds the squared slope, and its mean less the square of the mean slope
  is the variance the mip levels smoothed out of the normal. That turns the reflected rays up and
  down most (vertical streaks towards the horizon); the screen space reflection gives way to the
  blurred one where the blur is wider than 4 to 8 pixels. Where it would be blurred away entirely
  (or the water reflects nothing) neither it nor the rays are traced, the frames the same: most of
  open water seen from afar (Fort's sweep: the water 0.91 -> 0.44 ms on average at 3840 x 2160,
  1.6 -> 0.75 over the open sea). `d3d12_WaterReflectionBlur` scales that
  variance (0.1; 0.25 matches the halved slopes of the calmer normal the reflection is looked up
  along, but already washes the reflections of the bay in PierWalk out; 0 gives exactly the frames
  of the mirror before it; not saved).
- The march of the ocean's screen space reflection skips each batch of 4 steps whose stretch of the
  ray the depth tiles of the scene's copy show to pass in front of the scene: 7 levels of the
  copy's least depth (level 0 the least of 8 x 8 texels, each next one of 2 x 2 tiles of the one
  before), made once a frame by two compute kernels (`Shaders/DepthTiles.hlsl`, about 0.03 ms at
  3840 x 2160) when the frame draws an ocean. A batch it doesn't skip takes the same steps as
  before, so the frames stay the same bit for bit (Carrier's sweep: the water 1.74 -> 1.53 ms in
  its worst 100 frames, 0.67 -> 0.61 on average). The water volumes go without them: their
  reflection meets the banks near the water, where the tiles cost more than they save.
- The glass of the scopes (`r_ScopeLens_fx`, the AG36's, the OICW's, the AW50's and the rocket
  launcher's) shows the scene around the middle of the screen through the same copy of the scene
  when its list has one; else (the weapon's list) a copy of only the part of the target it shows
  (the middle out to `SCOPE_LENS_FIELD` times the glass's size on the screen, with room to spare;
  the colour alone), whose edges its samples are clamped to: 0.14 ms less than the whole target
  and its depth at 3840 x 2160. Each list copies the scene for itself.
- The templates that show the scene around them (`Mirrors.cpp`): a mirror (`TemplGlassEnvTex`) gets
  the scene rendered with the frame's camera mirrored in its plane (the plane through its vertices;
  the nearest mirror of the frame only, every frame, half the screen's size, its near plane the
  mirror's, as the water reflection's), 0.9 of it + 0.1 of its texture, blended; the copy of the
  scene is wrapped round a `TemplRefrEnvTex` model by its texture coordinates (the original's
  screen copy at TexGen Base, which makes its swatch look like chrome) and seen through a
  `TemplBumpOffsetEnv` one moved by its normal map. The heat haze particles (`ParticleMinHeatHaze`
  and the others, with `r_HeatHaze 1`, as the original) show the copy of the scene behind them moved
  by the slopes of `WaterBump` and blended by `haze_mask`; the copy is taken where the particles are
  drawn, so the grass and the fire stay in it (the original's, made before the water, loses them).
- The templates with shaders of their own (`TemplateFx.cpp`; one mode of `General.hlsl`'s `ShaderFx`
  each, `DF2_SHADER_FX` with the mode in `vParams3.w`, the screen copies' and the mirrors' too): the
  rainbow (`Defaults/colors2` at N.E, N.H, unlit), the membrane (a row of colours at E.N moving with
  time, added), cartoon shading (the steps of the sun's light times a dark outline), fur (the model
  and 8 shells out along the normal, blended), iridescence (a wing lit from both sides, see-through
  by its alpha, with a rainbow gloss) and the soap bubble (wobbling along its normal by a noise, the
  cube reflected, see-through but at its edges). Per pixel where the original's vertex programs take
  the dot products at the vertices; the original draws neither the fur nor the wing (its trace), ours
  draws what their scripts set out. `TemplDecalLight_SpecAdd` is lit as the fixed function lights a
  material: its texture plus its gloss map, times the light, with the material's highlights.
- The lava of Volcano (`LavaVolume`, `TemplateFx.cpp`, `Lava` in `General.hlsl`; step 255): opaque
  and unlit, the original's program: the material's texture flowing with time under its lava colour,
  brighter where the glow map (`Defaults/white_plain_d`, a noise of greens, rippled by `WaterBump`)
  is greener, mixed by `OverlayAmount` and doubled (the original's last instruction; in the HDR
  target the lava's brightest parts are above white). Before it the lava was drawn as lit ground
  with the terrain's rock: grey. Its colour, speeds and tiling come from the material's public
  parameters, which the engine sets only for the names the template lists (`GetPublicParams`):
  `s_PublicParams` (`Shader.cpp`) lists them for `LavaVolume`; the other templates list none (their
  materials' parameters, where a material has any, do not arrive: the template's defaults are used).
- The decals multiplied into the frame (`TemplDecalModulate`: DESTCOLOR, ZERO) whose material has an
  opacity below 1 are transparent, as the opaque templates are with it: their texture is laid over
  the frame by its alpha times the opacity (SRCALPHA, INVSRCALPHA), unlit, not multiplied in (step
  265). Research's sign "SUB-LEVEL 1" by the lamp (opacity 0.15, black with its letters in the alpha)
  turned 10 x 5 m of the wall black in the raster and the path tracer; now the wall is lit with light
  letters on it, as in the original (test: `render_tests` suite `decalsign`).
- What is added (blending ONE, ONE or SRCALPHA, ONE: fire, glows, the smoke of Volcano's lava) is
  fogged towards black (`DF2_FOG_BLACK`), particles and materials alike: towards the fog's colour
  each quad added that colour over all of its rectangle, also where its texture is black, and
  dozens of big smoke quads over Volcano's lava were white rectangles. The original draws its
  particles unfogged; their smoke is as bright as the original's in the game's picture
  (`r_HDRRendering 0`: the light the particles add in the lava yard, averaged over 37 frames of
  the time demo, 4.08 against 4.07 of 255). In the original's HDR mode its particles add next to
  nothing; ours keep the game's picture there too.

## Vegetation sprites and terrain detail

- The far vegetation sprites are made when a level is loaded (before its first frame): each object
  that `MakeSprite` gets is drawn from its 24 directions, `d3d12_SpriteRes` texels a side (128,
  32 to 256) into a texture array per object; the log says how many were made and how long it
  took. A mip level's alpha is its texels' average, as the original's (alpha tested at 0.5): what is
  thinner than a texel fades out with the distance. Scaled to keep level 0's coverage, the vines of
  Pier's ancient trees were short dark lines in the air 600 m away; the crowns look the same either
  way.
- An instance's quad shows the picture of the direction from the camera to the quad's centre (the
  middle of the object's box), as the original's (`FarSpriteAngles.h`). Taken from the instance's
  position, a palm's crown metres beside it, the picture changed frames away from the original's:
  on Carrier the palm by the wreck 72 m away changed 14 frames later in the comparison film's flight,
  a jump of its fronds where the original's had none. The original shows the picture of the 15
  degrees the direction falls in, so the far trees change their outline at once every 15 degrees
  as the camera goes round them (`d3d12_SpriteBlend 0`). `d3d12_SpriteBlend` (0.2, 0 to 1) is the
  part of the 15 degrees about that change over which the two pictures are blended: outside it the
  original's picture alone, the same pixels; inside it (3 degrees, 23 frames of the palm's
  change in the flight) the weight goes from the one to the other, their colours blended where
  both cover, the outline of the one dissolved into the other's by a noise over the pixels (alpha
  tested, of the pixels only one of them covers as many are kept as its weight says). A temporal
  anti-aliasing or upscaler (TAA, FSR) moves the noise along each frame; without one it stays put.
  A dissolve is no clean blend either way, since TAA's clamp to the neighbours and FSR keep part of
  the noise as a fine hatching on the leaves, and over all of the 15 degrees (1) the trees
  were hatched all the time. Over a fifth of them it is seen only during a change, in place of the
  jump. The quad's v gives the shaders the second picture's weight (`Shaders/ArrayCoords.hlsli`);
  the rays take the nearer picture's outline. Test: `render_tests` suite `sprites` (the palm's
  picture changes at the original's frame with 0, with and without TAA, and no jump with 0.2), unit
  tests `FarSprite_*`.
- The low detail terrain beyond the sectors (shader "terrainlowlod") has the level's low resolution
  cover texture over the whole terrain; the engine gives it u and v one whole texture further (an
  offset of 1), which wrapping does not see but the terrain's clamped sampling did: all the far land
  took the texture's corner, one flat colour, which the fog made the colour of the sky's mountains
  while the trees on it stayed, as if in the air.
- The terrain's detail layers near the camera (very high: `e_detail_texture_quality 1`) are drawn
  while `r_DetailTextures` is 1 and fade out between a quarter of the distance the engine puts in
  the layer's custom data and that distance (13 and 52 m on Training, whatever `r_DetailDistance`
  is); a layer with a material of its own (the cliffs, projected on a vertical plane) is drawn with
  the material's diffuse texture, bump mapped with its normal map, and fades out linearly to the
  distance in its custom data (156 m). A layer lies 5 cm out along the terrain's normal with its
  clip w 0.005 further, as the original draws it: the engine builds the layers' vertices a few
  centimetres off the sectors' grid, and without it they lose the depth test to their own sector
  in whole triangles wherever the ground falls the wrong way. With the layers on, the
  sectors (shader "terrain") get no detail texture, only their cliff texture where the engine
  marks cliffs; without them (shader "terrainwithdefaultdetailtexture") the detail texture
  everywhere.

## View distances (r_DrawDistance)

- `r_DrawDistance` (1, read every frame; not saved) sets the engine's distances for the level it
  has (`DrawDistance.cpp`, the table and the values in `DrawDistance.h`): on the game's levels but
  Control and Cooler the terrain is drawn at its full detail as far as it is seen
  (`e_terrain_lod_ratio` 0, the level's own is 3: the far cliffs keep their faces, the rocks in the
  sea their shape, the far islands their ridges) and the vegetation stays a model farther before it
  turns into its sprite: `e_vegetation_sprites_distance_ratio` the player's own (the menu's 0.9 or 1)
  times the level's factor, 3 on Training, Pier, Fort, River, Steam and Carrier, 4 on Research and
  Dam, 5 on Catacombs, 10 on the others (Volcano 1: its terrain alone). The factors are each level's
  limit, found level by level by a draw distance fix of the game's players (for the 32-bit game,
  through the levels' Lua scripts and `-DEVMODE`). In the game's dev mode (`-DEVMODE`) also on every
  level the objects are seen farther (`e_obj_view_dist_ratio` 75, the game's 55: the palms on the far
  islets, the characters farther away) and the characters keep their detailed models farther
  (`ca_LodBias` 0.015, the game's 0.125); both are cheat variables (`VF_CHEAT`), which the engine lets
  change only in its dev mode, not even from a Lua script or the console's `ExecuteString` that
  ignores it, so without `-DEVMODE` they stay as the game has them (the log says so for each level).
- The game's own value of each variable is kept and given back with 0 (the frames before, render
  test `drawdist`), at a level that is not in the table and at the end (`Release`): once a frame,
  from the loading screen's frames on, a value the game set since r_DrawDistance's (a level's load
  sets the terrain's ratio, the menu the vegetation's) becomes the variable's own and r_DrawDistance
  sets its own again (`DrawDistance::CHeldValue`). The vegetation's ratio is one the game saves into
  `system.cfg` (`VF_DUMPTODISK`) when it quits, also from a level: while it is held the flag is off,
  so that the game saves none of r_DrawDistance's values (the next start, another renderer and
  r_DrawDistance 0 get the player's own; the engine's default, 1, when the game saved while it was
  held). The log has a line for each value set or given back
  (`XRenderD3D12: r_DrawDistance, level Training: e_terrain_lod_ratio 0 (the game's 3.000000)`).
- Not taken from that fix: the water's reflection without the far sprites. It took the flicker of
  the original's sprites out of the water; here the reflection keeps them (the far forests on the
  mountains in the water), with the same flicker as without them (`flicker.py` on Training's sea
  3 m above the water, the camera moving: 4.81 and 4.81).
- Its cost (RX 7900 XTX, 3840x2160, the raster with the rays of system.cfg; the PerfSweep flights
  without the dev mode): 1 to 14 % of the FPS (River 219 to 188, Pier 211 to 194, Treehouse 282 to
  279), the draws 1.05 to 3.6 times as many and the triangles 1.2 to 3 times; mostly the CPU's (our
  `EF_EndEf3D` up to 1.5 ms more), the GPU's frame 0.04 to 0.28 ms longer.
- Tests: unit tests `DrawDistance_*` (the table, the vegetation's ratio from the player's own, the
  game's value kept, followed and given back), `render_tests` suite `drawdist` (the variables and
  their flags in the game without the dev mode and in it, on, 0 and on again; Training's mountain
  spot and Swamp changed on against 0, the same as the variables set by the console before the
  level loads, 0 the build before's).

## Objects fading with the distance

- An object the engine fades in or out by its distance (its render parameters' alpha below 1: the
  leaf buffer's draw sets `FOB_HASALPHA` and puts the alpha in the object's colour) is drawn as the
  original draws it (`d3d12_FadeBlend 1`): among the blended surfaces after the water, laid over
  what is behind it by its alpha (SRCALPHA, INVSRCALPHA) without depth writes, its light passes
  added by it. The alpha is the object's alpha squared, as the original's ambient pass takes it (its
  `c0.w`; the vegetation's is the same), not the texture's: most of the bump templates keep their
  gloss in the texture's alpha, and through it the crates would have faded in with holes. The alpha
  test still cuts out what it cuts out (a grating's holes stay holes). The decals multiplied over
  such an object are blended with it: they write no depth, and in the opaque list they multiplied
  the scene behind it.
- Drawn opaque (`d3d12_FadeBlend 0`, the frames before), the crates, the gas cylinders and the
  crane on Carrier's deck popped in whole at the far end of their fade. In the comparison film's
  `carrier-wreck` flight 22 of them are between the ends of their fade at its frame 261 (alpha
  0.00-0.97).
- The sun's cascades and the scene for rays take the object as opaque all through its fade (it
  casts its shadow and is seen in the reflections from the start, as before). The path tracer keeps
  it opaque, as the grass fading in or out (its G-buffer), and so does the first person weapon.
  The water reflection's own capture for the rays takes no blended draw: an object fading in or
  out that only the reflection sees is not in the scene for rays until its fade ends.
- Tests: `render_tests` suite `fade` (`Fade_carrier.ini`, Carrier's deck from the film at 1080p).
  Over the water the blend changes the frame where the objects fade, and three quarters of those
  pixels come nearer the original's: the mean |d| to it is 26.5-27.8 levels in place of 52.8-56.6.
  The unit tests `Fade_*` check the alpha: the fade's alone whatever the texture's alpha, the
  alpha test first, the texture's without a fade, and an even fade.

## The first person weapon

- The engine draws the first person weapon and arms (sorted `eS_Nearest`, the muzzle flash with
  `FOB_NEAREST` alone) at the player's eyes. From a camera that is not the player's (a demo's
  fly-through) they hang there in the world. Fort starts with Jack sitting in the inflatable boat
  `Zodiac0`: moved along a path, the boat carried his arms and his M4 over its stern.
- `r_NoDrawNear 1` (the original renderer's variable) leaves them out, as the original leaves them
  out. Until now this renderer registered the variable without reading it. `0`, the default, draws
  them as before. With a frame dump, each item left out gets a line `not drawn (r_NoDrawNear)`.
- Tests: `render_tests` suite `nodrawnear` (`NoDrawNear_fort.ini`, Fort's start from the game's own
  camera at 1080p). `r_NoDrawNear 1` leaves out 11-12 % of the frame, the original's 13-14 %, the
  two the same place (0.82-0.87 of their union). Off the weapon nothing changes, and `0` gives the
  build before's frames (0 px in the nearest of 2 x 2 runs).

## Light beams

- A lamp whose light shader has a `ClientEffect 'Beam'` block in its script (the `LightBeam*`
  shaders: the hanging lamps of Treehouse's corridors, Steam's huts, ...) gets a cone of light
  under it, as the original draws it: the shader's cone model (`Model`) along the light's forward
  axis, from `StartRadius` at the light to `EndRadius` `Length` metres away, coloured from
  `StartColor` to `EndColor`; its alpha is the colours' alpha times the squared cosine between the
  model's normal and the way to the camera (the cone's outline fades out) times `min(10 t, 1)` (it
  fades in over the first tenth of its length), clamped, so a negative end alpha ends it early.
  Blended by that alpha after the rest of the 3D scene and before the flares, depth tested without
  depth writes, both sides, fogged; whatever `r_Coronas` and `r_Beams`. Every light of the scene
  with such a shader has one (`Beams.cpp`, `LightBeam.h`), with the path tracer too.
- `LightStyle` of the block is read and not used: the original draws the colours as given.
- Not in the water's reflection: the original draws the lamps' beams there as well, but the
  reflection's scene has no lamps here.
- The shafts of sunlight in Training's bunker are planes of the `TemplMuzzleFlash_Auto` template.
  The original's program draws them added, twice the texture, each vertex's colour times
  min((2 |N.V|)^4, 1), so a plane fades out where it is seen edge on (`d3d12_MuzzleFlashAuto`,
  1; `SDrawConstants::vEdgeFade`). 0: the texture alone, added, with hard plane edges.

## Projected lights

- A projector (`DLF_PROJECT` with its image, a cube with the picture on +X) lights an object in a
  draw of its own after the object's first pass: the object's diffuse texture and material colour,
  lit by that light alone (per pixel with the normal map and specular where bump mapping lights the
  object, else as a point light of its radius), times twice the cube at the point's lookup vector
  (`m_TextureMatrix`), added where the first pass is (depth `EQUAL`) and fogged towards black.
- The image's flags as the engine loads it (`d3d12_LightImages`, 1; `LightImage.h`):
  - `FT2_REPLICATETOALLSIDES` puts the picture on all six faces, not on +X alone, so the light
    goes every way the cube is looked up, not only into the +X quarter.
  - `FT2_CHECKFORALLSEQUENCES` makes the image the first frame of a sequence: `<name>00`, then
    `<name>01` and on while the files are found. The light casts frame (int)(t / s) modulo the
    frames at the timer's time t, s being the light's `m_fAnimSpeed` seconds a frame.
  - Training's bunker's two water caustics lamps (`causq00`, 32 frames, 0.05 s) have both. The
    original's cube for them, traced, is the same picture on every face and the frame of that
    count (times 10.28, 10.38 and 10.58 s: frames 13, 15 and 19).
  - With the +X face alone and frame 0 they lit almost nothing, and the bunker had 0.90 of the
    original's light (r_RenderMode 0). Now it has 1.00 at frame 210 and 1.07 at frame 490; at the
    lamps' points it was 0.35-0.62 and is now 0.80-1.03.
  - Steam's hut lamp has neither flag and keeps its picture on +X.
  - The faces are made when the image loads, so a new value takes effect with the next level
    loaded; the frame applies at once. 0: +X alone and frame 0, as before.
- The light comes in one of two ways, by the object's light mask:
  - the object's own pass, whose mask holds the projector among its lights;
  - a light pass. The engine lights an object with a light map in passes when a lamp in reach
    casts shadow volumes: first the light map (`FOB_ZPASS`, an empty mask), then a copy for each
    such lamp (`FOB_LIGHTPASS`, that lamp alone in the mask, no ambient). The copy of a projector
    has no light of its own to draw, only the projector's draw. Treehouse's corridors are lit this
    way: concrete walls, floors, doors and stairs under the hanging lamps.
- The original draws these copies through the stencil of the lamp's shadow volumes. Here there
  are no shadow volumes, so the light is unshadowed.
- Over a light-mapped specular bump template the projector's pass has the projector's highlight
  too (`d3d12_ProjectorSpecular`, 1), as the original's light-map shaders of those templates add
  it, with the material's specular colour (times the texture's alpha for the gloss alpha
  templates): Training's bunker's rusty plates under its lamp. 0: the diffuse light alone.
- With the lamps' shadows by rays (`d3d12_RTLightShadows`) and in the path tracer, the rays towards
  a projector end `d3d12_RTProjectorShadowGap` (0.5 m) short of it, not `d3d12_RTLightShadowGap`
  (0.1 m): the picture is the shape its fitting gives the light, so the fitting casts no shadow of
  its own (with 0.1 m the shade of Treehouse's hanging lamps cut a hard-edged trapezoid into their
  light on the walls, the floor and the door; the original has none). 0 takes the other lamps' gap.

## Render modes

- The video options' render mode (`r_RenderMode`, offered with `r_Glare` on: 1 Improved,
  2 Paradise, 3 Cold, 4 Cartoon) is drawn over the finished 3D scene, before the other screen
  effects and the 2D, as the original's screen process draws it (its passes traced at 1280 x 720;
  `PostFx.cpp`, `Shaders/RenderMode.hlsli`). `d3d12_RenderMode` (1); 0: none, as before. The
  player's `system.cfg` has `r_RenderMode 2` with `r_Glare 1`: without it Training's bunker had
  0.70-0.76 of the original's light, its shafts of sunlight no glare.
- Improved and Paradise: the scene reduced to 256 x 256 (D); the glare's source at 128 x 128,
  2 (saturate(D - t) 0.8)^2 (1 - A), blurred by four passes of 4 taps farther and farther apart
  (G); the scene F + (1 - L) G, then towards its luminance L (0.3 0.59 0.11) by k (t 0.25, k 0.2
  Improved; t 0.1, k 0.1 Paradise). The targets have the original's fixed sizes, so the glare is
  the same part of the screen at every resolution. D is the mean of the scene over each texel;
  a screen wider than about 1800 is first reduced to a quarter (the mean of 4 x 4 pixels), so at
  4K Paradise takes about 0.17 ms of the GPU (the GPU time's section `render mode`: the scene's
  copy 0.07, the reductions 0.05, the composite 0.04) rather than 0.34 with 8 x 8 taps a texel.
- Cold: F first 2 saturate(0.75 F - 0.3 D), the source's gain 0.65, no blur, k 0.425.
- Cartoon: (0.75 F + 0.225 L) (1.25 1.24 1.2), times saturate(1 - saturate(2 |a|^2) - 2 |b|^2),
  a = 2 saturate(the pixel above - the one below), b = 2 saturate(the left one - the right one):
  dark outlines where the colour falls downwards or to the right. The original also gives every
  texture of the frame a mip bias of 1.5 in this mode (with `r_Glare` off too), so the textures
  are softer and the outlines follow the shapes, not the textures' grain; the frame constants'
  `vMipBias.x` carries it.
- A, the adapted luminance, is a 1 x 1 RGBA8 target kept from frame to frame as the original's:
  every 0.08 s (the original's 4 frames at 50 per second) it steps 5 % of the way to the luminance
  of 4 m^2, m the scene at the four spots the original's 2 x 2 pass takes (a top left corner, the
  top and left edges' middles, the middle; D3D9's pixel centres). Its 8 bits stop it within
  about 10 / 255 of the target: in the dark bunker it stays near 22 / 255 and the glare is strong,
  in daylight it rises to about 245 and the glare is faint. `d3d12_DumpFrame` gives its value.
- With the HDR10 highlight layer the composite and the cartoon keep the ratio the layer had for
  the scene's colour (a copy of the layer before the pass).
- Checked against the original over Training's bunker (frames 210 and 490): each mode's effect on
  the light, the chroma and the local contrast (and the cartoon's outlines) 0.95-1.01 of the
  original's (render_tests' `rendermode`); unit tests `RenderMode_*`.
