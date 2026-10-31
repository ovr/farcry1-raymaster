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
  `d3d12_UpscaleDebug 1` shows FSR's debug view (FSR 4: the motion vectors at the top left, the
  weight of the history at the top right), and `d3d12_UpscaleDebug 2` the depth it gets (near
  black, far white, log scale). All of them change at run time, and the frame dump names the
  upscaler, its render size and the frame's jitter.
  - With an upscaler, the main view's scene (`BeginScene3D` to `UpscaleScene` before the screen
    effects, `Upscale.cpp`) draws into render-size targets: colour, a depth buffer, motion vectors
    (R16G16, written by the general program's opaque draws and the water) and the depth the
    upscaler gets (R32, the nearest of the depth writes and of the water, which does not write
    the depth buffer). It is jittered and its textures get a mip bias. The screen effects and the
    2D stay at the display's size.
  - Motion comes from the previous frame's camera for static geometry and the water's surface, and
    from the previous world matrix of a character instance or a permanent object. The weapon
    (`FOB_NEAREST`) has none.
  - The history is reset after a frame without the main view, a camera jump of more than 5 m, a
    field of view change of more than 10 degrees, and a new mode.
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
  - The game's menu gets entries from `assets/scripts/hdr-options.patch` in Advanced Video
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

- `d3d12_AO` is the screen space ambient occlusion of the main view: `none` (the default; the
  frames are those of the renderer without it, DemoCompare difference 0), `gtao` (Intel XeGTAO
  1.30, `AOXeGtao.cpp` with `Shaders/XeGtao.hlsl` on our compute passes, `D3D12Compute.h`) or
  `cacao` (AMD FidelityFX CACAO 1.2 through its own Direct3D 12 host, `AOCacao.cpp`, with a
  descriptor heap and root signatures of its own; the build compiles its shaders with our dxc). An
  unknown one is logged once and acts as `none`. After the opaque list (`AmbientOcclusion.cpp`),
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
  `d3d12_AOQuality` 0 to 3 (2: XeGTAO Low to Ultra, CACAO LOW to HIGHEST), `d3d12_AORadius` (1 m),
  `d3d12_AOStrength` (1, the exponent of the visibility), `d3d12_AODenoise` (2: XeGTAO's denoise
  passes 0 to 3, CACAO's blur passes 1 to 8); with an upscaler XeGTAO's noise and CACAO's kernel
  change from frame to frame for it to gather. All are read every frame. `d3d12_AODebug 1` shows
  the factor in grey over the whole frame (with `d3d12_AOApply 1` the visibility to the power of
  the strength, without the fog), 2 the ambient light, 3 the light taken. The frame dump has an
  `ao:` line (with the baked share) and the GPU times an `ao` section. For comparisons, DemoLevel
  with `--scene Demos\DemoLevelPierWalk.ini` and `-DemoArgs '--exec-at 1 "d3d12_AO gtao"'` (or
  `cacao`, and `d3d12_AOApply 0` for the old way) against the same run without it.
  The game's menu gets it from `assets/scripts/video-ambient-occlusion.patch`: Ambient Occlusion,
  None / GTAO / CACAO in Advanced Video Options, in the row of the Basic Video Options button
  (which moves to the bar of Apply; `video-ray-tracing.patch` moves it up a row), greyed out
  without `d3d12_AO`.

## Ray tracing

- Ray tracing (`RayTracing.h`, DXR 1.1 with inline `RayQuery`, Shader Model 6.5): the main view's
  opaque draws and the static ones of the water's planar reflection become acceleration
  structures (a `ray scene:` line in the frame dump), once a frame and only while something wants
  rays (else their memory goes). The top level one and those of the frame's geometry are built
  only in a frame with a water surface (or with `d3d12_RTDebug`); in another one only the new
  static geometry's, ready for when the water shows.
  They are built on the compute queue (`d3d12_RTAsync`, default 1; 0 in the frame's list after the
  ambient occlusion, as before), beside the frame's shadows, opaque geometry and ambient occlusion:
  the frame's list so far goes to the GPU after the uploads, the builds once the uploads are done
  (they read only vertices and indices, uploaded or written by the CPU), the rest of the frame
  after the builds. The GPU times have them as `ray scene (compute queue)` beside the frame's
  sections (not part of its time); the frame's `ray scene` section is its wait for them. The
  levels' sweeps (4K) run 6–8 % faster (River 392 → 423 frames a second, Dam 444 → 476), the
  time demos Training 386 → 398 and Regulator 343 → 359 (Volcano and Research, without water,
  the same).
  `XRENDERD3D12_NORT=1` (or a device without tier 1.1) turns them off; the log says why.
  `Shaders/RayShade.hlsli` traces (`TraceScene`, alpha tested triangles tested: of the triangle
  only what the test reads, the normal only for a cube map, the vertex colour not where the alpha
  is the texture's) and shades the hits as the general program does without the draw's lights:
  textures, vertex colour, light maps (dot3 ones too), the sun with its shadow by a ray
  (`d3d12_RTShadows`, default 1). `d3d12_RTReflections` (0 by default, read every frame): 1 the
  water's reflection by rays in place of the screen space reflection, 2 hybrid (the screen space
  reflection where it is sure, the rays elsewhere, also where its march passes behind an object);
  not registered without rays.
  `d3d12_RTDistance` (150 m) the rays' length, `d3d12_RTMemory` (384 MB) the static BLAS kept,
  `d3d12_RTMask` (31) the kinds the rays see (1 static, 2 alpha tested, 4 of the frame; a draw is
  in the scene when the mask has all of its kinds).
  `d3d12_RTDebug` views the scene for rays from the camera: 1 instances, 2 shaded hits on the right
  half, 3 distance, 4 mip level, 5 kind, 6 alpha tests, 7 shaded hits over the whole picture;
  `d3d12_WaterDebug` 13, 14, 15 the water rays' colour, distance and weight. With the rays off the
  frames are the same as without the code (WaterMove, bit for bit). Check the rays under the debug
  layer with `XRENDERD3D12_DEBUG=2`: GPU based validation turns the water rays' frames black.
  The game's menu gets it from `assets/scripts/video-ray-tracing.patch`: Ray Traced Reflections,
  None / Rays / Hybrid in Advanced Video Options; the rows of the page move closer together to make
  room for it, greyed out without `d3d12_RTReflections`.

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
  per frame and per second, and the main view's lights (the sun, the others; counted at its
  `EF_EndEf3D`). The renderer takes
  the variable over (`TakeDisplayInfo`, an `IConsoleVarSink`): it keeps the console's value at 0, so
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
