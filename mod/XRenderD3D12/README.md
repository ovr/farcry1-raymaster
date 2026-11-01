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
  Performance. `d3d12_UpscaleSharpness` is 0 to 1 (0.5; 0 turns sharpening off).
  `d3d12_UpscaleVersion` is the upscaler's major version: 0 (the default) the one it picks for the
  GPU, 3 or 4 (FSR 3.1 or FSR 4, the newest of that major the upscaler DLL has; one it does not
  have is logged and acts as 0); a new value makes the upscaler anew. On a Radeon RX 7900 XTX
  (RDNA 3; FSR 4 is made for RDNA 4), Native AA at 3840 x 2160 takes the GPU some
  4 ms a frame with FSR 4 and 1.6 to 1.75 ms with FSR 3.1 (DemoLevel sweeps of Swamp and Pier:
  369 and 424 FPS without it, 154 and 163 with FSR 4, 235 and 257 with FSR 3.1).
  `d3d12_UpscaleDebug 1` shows FSR's debug view (FSR 4: the motion vectors at the top left, the
  weight of the history at the top right), and `d3d12_UpscaleDebug 2` the depth it gets (near
  black, far white, log scale). All of them change at run time, and the frame dump names the
  upscaler, its render size and the frame's jitter.
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
  - Then, at the scene's size: a meter (the centre-weighted mean log2 luminance of the light,
    reduced to 1 x 1), the adaptation of the exposure towards it (a 1 x 1
    R32 EV, twice as fast towards a brighter scene; it starts at the meter's after a frame
    without the main view), the bloom (a soft-threshold prefilter against single bright pixels,
    6 levels from half the size down with 13 taps, back up with a 3 x 3 tent) and the resolve
    (the exposed light plus the bloom, tone mapped, gamma encoded) into the back buffer. With an
    upscaler the resolve goes into an LDR target of the render size, which the upscaler gets as
    it would the scene. The screen effects and the 2D stay LDR on the back buffer. In heat
    vision the scene is only clamped (its colours are a palette), and the exposure is held.
  - The meter's first level and each level of the bloom from one twice its size are compute
    kernels (`Shaders/Meter.hlsl`, `Shaders/Bloom.hlsl`; the maths the pixel shader does too in
    `Shaders/HdrShared.hlsli`). The meter's threads read its taps side by side and add them up in
    the pixel shader's order (the same bits). The bloom's group reads each tap its texels share
    once, at the uv of the texel centre or corner it is at: in a frame 0 to 2 pixels of the
    picture differ from the pixel shader's by 1 of 255. At 4K the meter's first level and the
    bloom's levels down take 0.14 to 0.17 ms, with pixel shaders 0.34 to 0.35.
  - `d3d12_ToneMap`: 0 clamp, 1 soft shoulder (the default: the game's picture up to
    `d3d12_ToneMapKnee`, then each channel rolls off smoothly to 1 where the original clips it,
    so the sun still goes white and bright fire yellow), 2 ACES (fitted), 3 AgX.
    `d3d12_Exposure` is a compensation in EV; `d3d12_AutoExposure 0` makes the exposure
    2^`d3d12_Exposure`; `d3d12_ExposureMin` / `d3d12_ExposureMax` limit the automatic one (EV,
    0 is the game's daylight; by default 0 and 1.5: the game's sky and daylight are made for the
    display, so only dark views get brighter) and `d3d12_ExposureSpeed` is its rate per second.
    `d3d12_Bloom` is the bloom's strength (0 off), `d3d12_BloomThreshold` the exposed light it
    starts from.
    `d3d12_HDRDebug 1` shows the luminance in bands of 1 EV around middle grey (green), `2` the
    bloom alone. The frame dump gets the settings, the meter's mean and the adapted EV.
  - What the game draws as light sources is brighter in the HDR targets (`IsHdrTarget`: not in
    heat vision), each a factor of white's light, 1 the game's: `d3d12_HDRSun` (8) the sun's
    flare, `d3d12_HDRSky` (2) the sky towards the sun (`DF2_SKY_GLOW`, falling off with the
    cosine of the angle to the sun to the power 32), `d3d12_HDRGlow` (3) what glows. The flares,
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
    `Shaders/Common.hlsli`). The tone map goes on above its knee up to the peak instead of white
    (`HighlightRatio`, `Shaders/Hdr.hlsl`: the soft shoulder's excess, which ACES and AgX are
    mapped to), so below the knee the ratio is 1. The output pass before Present (`HDR_OUTPUT`)
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
    and four times that and at the peak, to set `d3d12_HDRPeak` by eye.
    `XRenderD3D12_ReadFrameHdr10` (exported, for DemoHost's `--video-hdr`) runs the output pass
    in HDR10 into a target of its own and reads it; from its first call the layer is kept
    whatever the display and `d3d12_HDROutput`.
  - The game's menu gets entries from `assets/scripts/02-hdr-options.patch` in Advanced Video
    Options: HDR, None / Soft / ACES / AgX for `r_HDRRendering` 0 or 1 with `d3d12_ToneMap` 1 to 3,
    and next to the Basic Video Options button HDR output, SDR / HDR10 / scRGB for
    `d3d12_HDROutput` (choosing HDR10 or scRGB with HDR at None sets it to Soft); both greyed out
    without the variables, that is with another renderer.

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
  Beyond the cascades the objects' shadow maps work as before.
  `d3d12_ShadowSoftness` (0.05 m) is the filter's radius (at least a texel and a half).
  `d3d12_ShadowDebug` 1 also shows the slices, 3 colours the cascades, 4 shows the full set's
  shadow in red and the live set's in green. With `d3d12_ShadowCascades 0` (from the start,
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
  leaves, the windows and the gaps between the rocks, and a haze that thickens with the distance.
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
  The physical share of the sun's light (a white wall facing the sun gives its light over pi, a
  metre of air with the extinction a quarter of that) shows no shafts at a density the open sea
  stands, so the sun is brighter in the air than that; at 2 the sky over the sea of Fort turned
  white. With the defaults, the sky over the sea at Fort's spawn is 164 178 211 for 135 155 200,
  the dark leaves in Training's village against the sun 87 99 101 for 25 46 13.
  At 3840 x 2160 the froxels take 0.06 to 0.09 ms of the GPU outdoors (the sweeps of ten levels)
  and the resolve 0.04 to 0.06 ms more; 3 to 10% of the frame rate at 400 to 600 FPS.
  With an upscaler the resolve before it (at the render size) takes the froxels at the depth of
  its scene; the froxels are of the view without the jitter, which moves a pixel by a small part
  of a froxel. None in heat vision or under water, and indoors where the outdoor lights do
  not reach (the camera's vis area, `IVisArea::IsAfectedByOutLights`; it fades out and in over
  half a second, and nothing of it runs there). `d3d12_VolumetricLight 0` gives the frames of the
  renderer without it (DemoCompare difference 0), 2 shows the light scattered alone. All are read
  every frame. The GPU times have a `volumetric light` section and the frame dump a
  `volumetric light:` line.

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
  the history was sure of turned the other way, so that a shadow that moves does not stay behind),
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
  `d3d12_RTLightShadows` (0 by default, read every frame; not registered without rays) gives the lamps
  hard shadows: the main view's draws lit per pixel by point lights (the draw's lights of
  `DF_PIXEL_LIGHTING` and `DF2_MATERIAL_LIGHT`, the dynamic lights over a light map,
  `DF2_LIGHTMAP_LIGHTS`, the light passes, the projectors such as the flashlight)
  take the same programs, also compiled with `XR_RAY_LIGHT`: `DrawLighting` and `MaterialLighting` in
  the pixel shader trace a ray from the pixel's point, a little off its face on the lamp's side,
  towards each point light that lights it (`TraceLight`, the first hit ends it, 4 alpha tests), up to
  `d3d12_RTLightShadowsMax` (4) of them a pixel, ending `d3d12_RTLightShadowGap` (0.1 m) short of the
  lamp (not its own fitting); where it hits, the light and its specular are left out. Directional
  lights, the per vertex lighting and the vegetation are as they were. 2 shows the light taken away
  in red, 3 by where the ray hits (green next to the surface, red next to the lamp). The frame
  constants' `nRaySun.z` carries the mode, the number and the gap beside the cube maps' mode; a frame
  whose scene has a light besides the sun builds the scene for rays (with what is out of the view
  while such draws were there the frame before). In the game's light the lamps light the characters
  through the walls; with it they are in the shade of what stands between, and of their own arms.
  At 3840 x 2160 under 8 lamps (DemoMultiLight with `DemoMultiLightTeapot.ini`: `main_view = 1`
  draws it as the main view) the opaque list takes 0.05 -> 0.23 ms, the GPU 0.96 -> 1.18; in
  Research's hall 1.05 -> 1.23 (the opaque list waits for the scene for rays).
  `d3d12_PathTracing`, the path tracer of the main view's lighting, has its own section (Path tracing).
  `d3d12_RTDebug` views the scene for rays from the camera: 1 instances (a colour of the mesh and
  where it is to 0.25 m, the same from frame to frame), 2 shaded hits on the right
  half, 3 distance, 4 mip level, 5 kind, 6 alpha tests, 7 shaded hits over the whole picture; the path
  tracer's: 8 the albedo of the hits, 9 their emission, 10 them lit by the sun and the lamps; 11 a
  colour per material the rays take of the hit's instance (the same while it is: to see the rays'
  materials steady);
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
  `d3d12_RTOffscreen` 1 does for the effects) and what is behind the camera (`d3d12_PTBehind`, 40 m: the
  static objects and entities the 3D engine gives a camera at the view looking back, 170 degrees wide,
  as far as that, noted as the cascades' parts, `UpdateRayBehind`; the cascades have only what is
  towards the sun, a room without the sun none of it: Research's hall has 42 instances without it, 259
  with it; 0.1 to 0.3 ms of the CPU, 0.1 to 0.2 ms more of the GPU's builds on the sweeps), the motion
  vectors (the scene targets without an upscaler, Upscaler), and the lights for its kernels and the
  programs with rays: `SPTLight` (`Shaders/RayConstants.h`), the root SRV t2 space5, the point lights,
  the directional ones and the projectors with their cube. They are the frame's lights and the level's
  static ones (`I3DEngine::GetStaticLightSources`, the lamps the light maps were baked with: the frame
  has only those whose sphere reaches the view, one or two of the 85 of Research, and many are
  `DLF_FAKE | DLF_LM`, never in a frame), the nearest 128 within `d3d12_PTLightDistance` (60 m); not the sun, a
  fake light the light maps do not have, a heat source that gives no light, the characters' fill lights
  (`DLF_AMBIENT_LIGHT`) without `d3d12_PTFillLights`. The frame dump lists them (`pt lights:`).
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
  sun and sky on an open face towards the sun times ((1 + (r - 1) N.L V) / r)^2.2, V the sun's
  visibility over the terrain alone, r `d3d12_PTTerrainDelight` (a lit face over a shadowed one): a lit
  face is as bright as the editor had it. While the path tracer is on the vegetation's vertex colours
  are baked without the light (the engine's values again on a change), and the grass that fades in or
  out is alpha tested among the opaque surfaces (its alpha times the fade: it thins out), not blended
  after them.
- Its G-buffer (`Shaders/PathGBuffer.hlsli`) is what the main view's opaque list writes with the general
  program's `XR_PT` permutation (`General_PT_*`, `SPipelineKey::bPathTrace`): the colour gets only what
  the path tracer does not light (glow maps, alpha glow, the unlit templates, the fog), a second target
  the albedo (RGBA16F, the game's colour without its light, unfogged; decals and the terrain's layers
  blend into it as into the colour), a third the normal on the side of its triangle the camera sees, as
  the rays' hits take it (R10G10B10A2: octahedral, the fog's share, the path material: lit, terrain,
  vegetation; by the view's direction a crown's normals turned over at the eyes' height, a line that
  moved over the leaves with the camera), a fourth the sun's visibility as the raster finds it (RGBA8,
  r: its shadow maps, the sun's cascades and their mask by rays, which the path tracer turns on, from
  all the casters; 0 where the raster has no sun) and the game's ambient light on the surface (gba: what
  the object's ambient colour adds to its light maps or its lights; the terrain, whose light is in its
  texture, the outdoor ambient), a fifth its highlights' colour (RGBA8, half of the raster's 2 x
  specular colour x gloss of the specular templates; it blends as the albedo). Its lit draws lose their
  light (the light maps, the sun, the lamps and their passes, the ambient and the baked AO); the screen
  space AO, the rays' GI and the projectors' passes are not drawn.
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
  9 the highlights' colour, 10 the highlights, 11 the radiance cache's light at the surfaces.
- Open places are as bright as the raster, a hut or a tunnel lit through an opening about as bright
  (without the ambient light far darker, the reference too); a hall lit by its light maps' lamps is
  darker, the crowns of the trees seen from below too (their leaves pass no light). The highlights are
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
  already lost close to the camera, so it is grey over most of the sea).
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

## Vegetation sprites and terrain detail

- The far vegetation sprites are made when a level is loaded (before its first frame): each object
  that `MakeSprite` gets is drawn from its 24 directions, `d3d12_SpriteRes` texels a side (128,
  32 to 256) into a texture array per object; the log says how many were made and how long it
  took.
- The terrain's detail layers near the camera (very high: `e_detail_texture_quality 1`) are drawn
  while `r_DetailTextures` is 1 and fade out between a quarter of the distance the engine puts in
  the layer's custom data and that distance (13 and 52 m on Training, whatever `r_DetailDistance`
  is); a layer with a material of its own (the cliffs, projected on a vertical plane) is drawn with
  the material's diffuse texture, bump mapped with its normal map, and fades out linearly to the
  distance in its custom data (156 m). With the layers on, the
  sectors (shader "terrain") get no detail texture, only their cliff texture where the engine
  marks cliffs; without them (shader "terrainwithdefaultdetailtexture") the detail texture
  everywhere.
