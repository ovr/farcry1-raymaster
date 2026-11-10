# Configuration

The options of the Direct3D 12 renderer (`mod/XRenderD3D12`) and of the game's script patches
(`assets/scripts`) are console variables. They come in two parts:

- [For players](#for-players): the quality and the look, most of them in the game's video options.
- [System (better not to touch)](#system-better-not-to-touch): debug views, profiling, checks of
  the device, switches kept to compare frames with the renderer before a change, and the tuning
  the defaults were chosen and measured with.

How to set one:

- In the game's menus: Video Options, Advanced Video Options and its HDR page (with the patches
  of `assets/scripts`, listed in [`README.md`](README.md)).
- In the game's console: `d3d12_AA smaa` sets it, `d3d12_AA` alone prints it. Most take effect at
  once; the descriptions say when one takes effect later (at start-up, a restart or the next
  level).
- In `system.cfg` in the game's folder: the game writes the variables marked *Saved* there when it
  quits and reads them when it starts.
- On the game's command line, after `FarCry.exe`, one quoted pair each: `FarCry.exe "r_Stats 1"
  "d3d12_AA taa"`. That way the unsaved ones get a value at every start.

How each variable works, and what it costs: the reference of the renderer,
[`mod/XRenderD3D12/README.md`](mod/XRenderD3D12/README.md). The variables of the tracing proxy:
[`mod/XRenderD3D9Traced/README.md`](mod/XRenderD3D9Traced/README.md).

## For players

Safe to change: each one is a quality or a look. A higher setting costs frames per second, not
correctness. The ones that need hardware say so (ray tracing and path tracing: a graphics card with
DXR 1.1); without it the renderer leaves them off.

### Display

Details: [Window and full screen](mod/XRenderD3D12/README.md#window-and-full-screen).

| Variable | Default | Saved | What it does |
|---|---|---|---|
| `r_Width` | the game's | yes | The screen's or the window's width (Video Options: Resolution); applied without a restart |
| `r_Height` | the game's | yes | Its height |
| `r_Fullscreen` | the game's | yes | 1 full screen, 0 a window |
| `r_VSync` | `0` | yes | Vertical sync (Video Options), applied in a game too |
| `cl_fov` | `0` | yes | The field of view across the screen in degrees; 0 automatic: the view up and down of a 4:3 screen (`assets/scripts/01-field-of-view.patch`) |

### Upscaling and anti-aliasing

Details: [Upscaler](mod/XRenderD3D12/README.md#upscaler), [Anti-aliasing](mod/XRenderD3D12/README.md#anti-aliasing).

| Variable | Default | Saved | What it does |
|---|---|---|---|
| `d3d12_Upscaler` | `none` | yes | `none` or `fsr` (AMD FSR Upscaling: FSR 4 where the GPU supports it, else FSR 3.1); Video Options: Upscaler |
| `d3d12_UpscaleMode` | `0` | yes | The render size: 0 Native AA (the display's, anti-aliasing only), 1 Quality, 2 Balanced, 3 Performance, 4 Ultra Performance |
| `d3d12_UpscaleVersion` | `0` | yes | 0 the upscaler's choice for the GPU, 3 FSR 3.1, 4 FSR 4 |
| `d3d12_UpscaleSharpness` | `0.5` | yes | The sharpening of FSR and of TAA, 0 (none) to 1 |
| `d3d12_CutReset` | `1` |  | Where the camera cuts to another shot (more than 4 m or 45 degrees in a frame), the histories of the screen start again: the upscaler's and TAA's, the rays' denoisers, the path tracer's light, the volumetric light; 0 they carry the shot before |
| `d3d12_AA` | `taa` | yes | The anti-aliasing without an upscaler: `none`, `fxaa`, `smaa`, `cmaa2`, `taa`, `msaa2`, `msaa4` or `msaa8`; Video Options: Anti-Aliasing |
| `d3d12_AAQuality` | `2` | yes | Its quality, 0 (low) to 3 (ultra) |

### HDR, exposure and colour

Details: [HDR](mod/XRenderD3D12/README.md#hdr).

| Variable | Default | Saved | What it does |
|---|---|---|---|
| `r_HDRRendering` | `0` | yes | The HDR pipeline (tone map, exposure, bloom, the HDR output, colour grading, the volumetric light, the path tracer): 1 on, 0 off; Advanced Video Options: HDR |
| `d3d12_ToneMap` | `4` | yes | The tone map: 0 clamp, 1 soft shoulder, 2 ACES, 3 AgX, 4 neutral (the hue kept); Advanced Video Options: HDR |
| `d3d12_Exposure` | `0` | yes | The exposure's compensation in EV |
| `d3d12_AutoExposure` | `1` | yes | 1 the exposure adapts to the scene, 0 `d3d12_Exposure` alone |
| `d3d12_Bloom` | `0.5` | yes | The bloom's strength, 0 none |
| `d3d12_HDROutput` | `0` | yes | On a display in HDR mode (Windows: Use HDR): 0 SDR, 1 HDR10, 2 scRGB; the HDR page |
| `d3d12_HDRPaperWhite` | `0` | yes | With the HDR output, the picture's white in nits; 0 Windows' SDR content brightness (the HDR page: SDR Brightness) |
| `d3d12_HDRPeak` | `0` | yes | With the HDR output, the brightest highlights in nits; 0 the display's peak (the HDR page: Peak Brightness) |
| `d3d12_HDRFrameAverage` | `1` | yes | With the HDR output, a frame's average light at most this times the display's full frame light, so that an OLED does not dim the picture; 0 no limit (the HDR page: Frame Average Limit) |
| `d3d12_HDRFullFrame` | `0` | yes | The display's full frame light in nits for that limit; 0 what the display reports |
| `d3d12_Saturation` | `1` | yes | The saturation, 0 (grey) to 4; 1 unchanged |
| `d3d12_Contrast` | `1` | yes | The contrast, 0.25 to 4; 1 unchanged |
| `d3d12_Temperature` | `0` | yes | The white balance, -1 (cooler) to 1 (warmer) |
| `d3d12_Tint` | `0` | yes | The tint, -1 (green) to 1 (magenta) |
| `d3d12_ColorLut` | (empty) | yes | A 3D LUT (a `.cube` file, a path in the game's folder or its paks) the picture goes through |
| `d3d12_ColorLutAmount` | `1` | yes | How much of it, 0 to 1 |

### Shadows and ambient occlusion

Details: [Sun shadow cascades](mod/XRenderD3D12/README.md#sun-shadow-cascades), [Ambient occlusion](mod/XRenderD3D12/README.md#ambient-occlusion).

| Variable | Default | Saved | What it does |
|---|---|---|---|
| `d3d12_Shadows` | `1` | yes | The objects' shadow maps (with the game's `e_shadow_maps`); 0 off |
| `d3d12_ShadowMapSize` | `1024` | yes | Their size, 128 to 4096; 0 the engine's |
| `d3d12_ShadowCascades` | `4` | yes | The sun's cascaded shadow maps around the camera, 1 to 4; 0 none |
| `d3d12_ShadowDistance` | `120` | yes | How far they reach (m) |
| `d3d12_ShadowCascadeSize` | `2048` | yes | Their size, 512 to 4096 |
| `d3d12_ShadowSoftness` | `0.05` | yes | The softness of the sun's shadows (m) |
| `d3d12_AO` | `cacao` | yes | The ambient occlusion: `none`, `gtao` (Intel XeGTAO), `cacao` (AMD FidelityFX CACAO) or `rt` (rays); Advanced Video Options: Ambient Occlusion |
| `d3d12_AOQuality` | `2` | yes | Its quality, 0 (low) to 3 |
| `d3d12_AORadius` | `1` | yes | Its radius (m) |
| `d3d12_AOStrength` | `1` | yes | Its strength; 0 none |

### Ray tracing (a graphics card with DXR 1.1)

Details: [Ray tracing](mod/XRenderD3D12/README.md#ray-tracing).

| Variable | Default | Saved | What it does |
|---|---|---|---|
| `d3d12_RTReflections` | `0` | yes | The reflections by rays: 0 off, 1 the water's by rays, 2 hybrid (the screen space reflection where it finds the scene, rays elsewhere); with 1 or 2 also chrome, glass and screens. Advanced Video Options: Ray Traced Reflections |
| `d3d12_RTReflectionSurfaces` | `1` | yes | With them, the surfaces that reflect a cube map (chrome, glass, screens) trace rays; 0 the cube alone |
| `d3d12_RTPbrReflections` | `1` |  | With them, the metals and the smooth materials of the RayMaster maps reflect by rays; 0 off |
| `d3d12_RTSunShadows` | `0` | yes | The sun's shadow by rays: 1 a ray for each 2 x 2 pixels, 2 a ray a pixel; 0 the cascades' |
| `d3d12_RTLightShadows` | `0` | yes | The lamps' shadows by rays (the flashlight's too): 1 on, 0 off |
| `d3d12_RTLightShadowsMax` | `4` | yes | The lamps a pixel traces rays towards, 1 to 15 |
| `d3d12_RTLightBehind` | `1` | yes | With the lamps' shadows by rays, what is behind, above and below the camera shades the lamps too, not only once it comes into the view; 0 only the view's |
| `d3d12_RTGI` | `0` | yes | The ambient light by rays, one bounce, in place of `d3d12_AO`: 1 a ray for each 2 x 2 pixels, 2 a ray a pixel; 0 off |

### Path tracing (a graphics card with DXR 1.1, HDR on)

Details: [Path tracing](mod/XRenderD3D12/README.md#path-tracing).

| Variable | Default | Saved | What it does |
|---|---|---|---|
| `d3d12_PathTracing` | `0` | yes | The lighting by paths in place of the light maps: 0 off, 1 real time, 2 the reference (the frames added up while the view stays); Advanced Video Options: Path Tracing |
| `d3d12_PTDenoiser` | `nrd` (simple without NRD) | yes | Its denoiser: `nrd` (NVIDIA NRD, where it is built in), `simple` or `none` |
| `d3d12_PTBounces` | `1` | yes | The bounces of its paths in real time; 0 the sun and the lamps alone |
| `d3d12_PTCheckerboard` | `1` | yes | 1 the paths of half the pixels each frame (faster), 0 a path a pixel |
| `d3d12_PTReferenceBounces` | `8` | yes | The bounces of the reference (`d3d12_PathTracing 2`) at most |

### Light in the air and in the leaves (HDR on)

Details: [Volumetric light](mod/XRenderD3D12/README.md#volumetric-light), [Foliage translucency](mod/XRenderD3D12/README.md#foliage-translucency).

| Variable | Default | Saved | What it does |
|---|---|---|---|
| `d3d12_VolumetricLight` | `1` | yes | The volumetric light: the air lit by the sun, the shafts of sunlight; 0 off |
| `d3d12_VolumetricLightRays` | `1` | yes | With the sun's shadow by rays or the path tracer: 1 the air's sun by rays (the shafts indoors too), 0 by the cascades |
| `d3d12_FoliageTranslucency` | `1.5` | yes | The sun's light through the leaves and the grass seen against it; 0 off |

### Water

Details: [Water](mod/XRenderD3D12/README.md#water).

| Variable | Default | Saved | What it does |
|---|---|---|---|
| `r_WaterReflections` | `1` | yes | The water's reflection; 0 off |
| `d3d12_WaterReflectionScale` | `0.5` | yes | Its size, a fraction of the screen's |
| `d3d12_WaterFFT` | `1` |  | The waves of a wind sea on the ocean (an FFT ocean); 0 the game's wave texture |
| `d3d12_WaterFoam` | `1` |  | The ocean's foam (whitecaps, the surf), its strength; 0 none |
| `d3d12_WaterFoamTerrain` | `0.2` |  | m: where the floor behind the water is the terrain as drawn (within this of the level's heights), the foam takes the depth over those heights, not over the terrain's level of detail; 0 over what is drawn |
| `d3d12_WaterFoamFar` | `1` |  | No shore's foam where nothing is drawn behind the water or its floor lies off the level's terrain (the filler past the map's edge): no thin line of foam across the open sea far out; 0 the foam of that floor |
| `d3d12_WaterFoamPonds` | `1` |  | No whitecaps, shore's foam or surf on the ponds and lakes the level's terrain cuts off from the sea (water under the level not joined to the map's edge): Swamp's shallow ponds still, not white from bank to bank; 0 the sea's foam on them too |
| `d3d12_WaterWhitecapWind` | `4` | yes | m/s: no whitecaps below this wind, the onset of whitecapping (Beaufort's 3): none on the game's calm sea (its wind 3.5 m/s), no lone whitecap on Training's bay; 0 at any wind |
| `d3d12_WaterWakes` | `1` |  | The waves behind boats and swimmers; 0 none |
| `d3d12_Caustics` | `1` | yes | The caustics under the water; 0 off |

### Textures and materials (the game's folder RayMaster)

Details: [Parallax and generated normals (RayMaster)](mod/XRenderD3D12/README.md#parallax-and-generated-normals-raymaster).

| Variable | Default | Saved | What it does |
|---|---|---|---|
| `r_TexUpscaled` | `1` |  | The textures' upscaled pictures (twice the side), from the next level's load; 0 the paks' own |
| `r_Parallax` | `1` |  | Parallax occlusion mapping by the textures' height maps; 0 off |
| `r_PbrNormals` | `1` |  | Normal maps made from the texture for the materials without one; 0 off |
| `r_PbrRoughness` | `1` |  | The highlights by the textures' roughness; 0 the game's |
| `r_PbrAO` | `1` |  | The reflections by rays darker in the textures' ambient occlusion; 0 off |
| `r_Skin` | `1` |  | The characters' faces lit as skin; 0 off |

### View distance and detail

Details: [View distances (r_DrawDistance)](mod/XRenderD3D12/README.md#view-distances-r_drawdistance), [Vegetation sprites and terrain detail](mod/XRenderD3D12/README.md#vegetation-sprites-and-terrain-detail).

| Variable | Default | Saved | What it does |
|---|---|---|---|
| `r_DrawDistance` | `1` |  | The levels' view distances farther: the terrain at its full detail, the vegetation as models farther; 0 the game's |
| `d3d12_SpriteRes` | `128` | yes | The size of the far vegetation sprites' textures, 32 to 256 (from the next level's load) |
| `r_DetailTextures` | `1` | yes | The terrain's detail textures; 0 off |
| `r_TexResolution` | `0` | yes | The texture quality: the mip levels left out of the textures as they load; 0 none |
| `r_TexBumpResolution` | `0` | yes | The same for the normal maps |
| `r_Quality_BumpMapping` | `2` | yes | The bump mapping quality (after a restart): 0 lit per vertex, 1 and 2 per pixel, 3 also the glow of self-illuminated materials |

### The game's render modes

Details: [Render modes](mod/XRenderD3D12/README.md#render-modes).

| Variable | Default | Saved | What it does |
|---|---|---|---|
| `r_RenderMode` | `0` | yes | Video Options' render mode over the scene, with `r_Glare 1`: 1 Improved, 2 Paradise, 3 Cold, 4 Cartoon; 0 none |
| `r_Glare` | `0` | yes | 1 the render modes on |

### On the screen

Details: [Statistics and profiling](mod/XRenderD3D12/README.md#statistics-and-profiling).

| Variable | Default | Saved | What it does |
|---|---|---|---|
| `r_DisplayInfo` | `0` |  | 1 the frame's information at the top right: frames per second, the renderer's features that are on, the render size |
| `r_Stats` | `0` |  | The renderer's statistics at the top left: 1 the GPU time of each pass, the frame times, the draws; 2 also the memory |

## System (better not to touch)

These are for development, measurements and tests. Their defaults are the values the renderer is
tested and measured with. Another value can:

- make the picture wrong (debug views, switches back to the way before a fix);
- make the game much slower (`d3d12_Debug`, `d3d12_DRED 2`, the logs);
- make the renderer stop drawing (`d3d12_DredTest`).

The tests and the measurement scripts set the ones they need on the command line. To undo a
change, set the default back, or delete the variable's line from `system.cfg` (the *Saved* ones).

### Debug views

| Variable | Default | Saved | What it does |
|---|---|---|---|
| `d3d12_AADebug` | `0` |  | The anti-aliasing's edges |
| `d3d12_AODebug` | `0` |  | 1 the ambient occlusion in grey, 2 the ambient light, 3 the light it takes away |
| `d3d12_DisplayHDR` | `0` |  | A panel of the HDR pipeline at the right (the histogram, the exposure) |
| `d3d12_HDRDebug` | `0` |  | 1 the light in EV bands, 2 the bloom alone, 3 the sky's mask |
| `d3d12_HDROutputDebug` | `0` |  | With the HDR output: 1 the highlights, 2 test patches, 3 the HDR page's test |
| `d3d12_HDROutputTestRect` | (empty) |  | Where `d3d12_HDROutputDebug 3` draws (the HDR page sets it) |
| `d3d12_PTDebug` | `0` |  | The path tracer's G-buffer and its terms over the picture |
| `d3d12_RTDebug` | `0` |  | The scene for rays seen from the camera (its instances, the hits, the distances, ...) |
| `d3d12_RTDebugSky` | `1` |  | 0 `d3d12_RTDebug`'s rays leave out the pieces of the sky (as the volumetric light's sun rays do) |
| `d3d12_ShadowDebug` | `0` |  | 1 the shadow maps at the top left, 2 to 4 the receivers' shadows and cascades |
| `d3d12_UpscaleDebug` | `0` |  | 1 FSR's debug view, 2 the depth it gets |
| `d3d12_WaterDebug` | `0` |  | The water shows one term (1 refraction, 2 reflection, ...) |

### Profiling and logs

Details: [Statistics and profiling](mod/XRenderD3D12/README.md#statistics-and-profiling).

| Variable | Default | Saved | What it does |
|---|---|---|---|
| `d3d12_GpuTimes` | `0` |  | N: every N frames the GPU time of each section to the log |
| `d3d12_FrameLog` | `0` |  | 1 a line per frame to `XRenderD3D12_frames.txt` |
| `d3d12_CpuSamples` | `0` |  | N: the drawing thread's stack sampled every N microseconds; back to 0 writes the report |
| `d3d12_DumpFrame` | `0` |  | 1 the next frame's render items and draws to `XRenderD3D12_frame.txt` |
| `d3d12_HDRLog` | `0` |  | 1 a line of the HDR statistics per frame to `XRenderD3D12_hdr.txt` |

### The device and its checks

Details: [Device removal (DRED)](mod/XRenderD3D12/README.md#device-removal-dred), [Resource states and barriers](mod/XRenderD3D12/README.md#resource-states-and-barriers).

| Variable | Default | Saved | What it does |
|---|---|---|---|
| `d3d12_Debug` | `0` | yes | The Direct3D 12 debug layer (read at start-up, much slower): 1 with GPU based validation, 2 without it |
| `d3d12_DRED` | `1` | yes | What the GPU did when the device is removed, to `XRenderD3D12_dred.txt` (read at start-up): 0 off, 1 the page faults and the sections, 2 every command (slower) |
| `d3d12_DredTest` | `0` |  | Tests that report: 1 removes the device, 2 makes a GPU page fault; the renderer draws nothing after either |
| `d3d12_BarrierCheck` | `0` |  | 1 each resource transition checked, the problems to the log |

### Switches for comparisons

1, the default, is the renderer as it is now; 0 is the way before a fix or a speed-up (the frames before it), kept to compare.

| Variable | Default | Saved | What it does |
|---|---|---|---|
| `d3d12_AlphaToCoverage` | `1` | yes | With MSAA, the alpha tested surfaces cover the samples by their alpha |
| `d3d12_BarrierBatch` | `1` |  | The barriers queued and given in one call before the next draw |
| `d3d12_BeachShadow` | `1` |  | The foam on the beach in the sun's shadow as the terrain under it (no glowing border round the shadow of Carrier's wreck) |
| `d3d12_BeachResearch` | `1` |  | The beach of the shore shader `TerrainWaterBeachResearch` (Steam, Research, Bunker, Dam, Rebellion, Archive) drawn as the original's, its foam fainter than `TerrainWaterBeach`'s (0: not drawn) |
| `d3d12_CascadeCasterShader` | `1` |  | A caster of the sun's cascades that discards (an alpha tested leaf) drawn by a pixel shader of its discards alone (the same shadows, faster) |
| `d3d12_CascadeCull` | `1` |  | Without rays, the parts that reach none of the sun's cascades left out as the engine adds them; 2 also the camera towards the sun ending where nothing can shade them (the level's heights: nothing measurable, the engine loads other textures); 0 all of them, as far as the view |
| `d3d12_CascadeInstancing` | `3` |  | A caster drawn once into all the sun's cascades it reaches: 1 the full set's, 2 the live set's too, 3 also the parts of one mesh and material once for all their objects, 0 once per slice |
| `d3d12_DecalAddOpacity` | `1` |  | An added decal whose material's opacity is below 1 laid over by its alpha times it, as the original (Research's cave cobwebs: thin strands, no bright card) |
| `d3d12_DepthPrepass` | `1` |  | The depth prepass (the sun's shadow by rays needs it) |
| `d3d12_AlphaTestEarly` | `1` |  | An alpha tested draw's empty texels left out before their light (the same frames, faster foliage) |
| `d3d12_FoliagePrepass` | `1` |  | The leaves' and the grass's depth before their colour: 1 with the path tracer, 2 always (the same frames, faster foliage) |
| `d3d12_DrawExt` | `1` |  | A draw uploads the rest of its constants only when it reads them |
| `d3d12_FadeBlend` | `1` |  | The objects that fade with the distance drawn blended |
| `d3d12_LightColorScale` | `1` | yes | A bump template's light colour as the original's shaders take it |
| `d3d12_LightImages` | `1` | yes | A projected light's picture as the engine asks for it |
| `d3d12_LightmapDot3` | `1` | yes | A dot3 light map mixed as the original's shaders mix it |
| `d3d12_LightmapSpecular` | `1` | yes | The highlights of the lamps baked into a dot3 light map |
| `d3d12_MuzzleFlashAuto` | `1` | yes | The planes of `TemplMuzzleFlash_Auto` (Training's bunker's shafts) as the original draws them |
| `d3d12_NormalMapHeight` | `1` | yes | A height map in a normal map's slot added to the normal map |
| `d3d12_PixelLighting` | `1` | yes | Every lit material lit per pixel |
| `d3d12_ProjectorSpecular` | `1` | yes | A projected light's highlight over a light mapped specular material |
| `d3d12_RenderMode` | `1` | yes | The video options' render mode (`r_RenderMode`) drawn over the scene |
| `d3d12_RenderModeOutdoor` | `1` | yes | The render modes' glare goes from its indoor threshold and gain to its outdoor ones, as the original's |
| `d3d12_RTAsync` | `1` |  | The acceleration structures built on the compute queue |
| `d3d12_RTAlphaCoverage` | `3` |  | The rays whose light is averaged take a leaf (an alpha tested triangle) at random by the share of it its alpha test passes on, made once a mesh, in place of the test. Bits: 1 the path tracer's bounces and radiance cache, 2 the rays' GI and AO, 4 the volumetric light's sun (blurs the shafts through the crowns); 0 the exact tests |
| `d3d12_RTFaceNormal` | `1` |  | The side of a surface the rays start from by the triangle's face (2 that face alone) |
| `d3d12_RTPbrScreen` | `1` |  | What the metals' rays hit takes its light from the last frame's picture |
| `d3d12_RTShadows` | `1` | yes | The sun's shadow at the rays' hits |
| `d3d12_SkyFogBand` | `1` | yes | With a near fog (Swamp, Volcano), the fog's colour over the sky box's lower part, as the original draws it |
| `d3d12_SpriteDistFactor` | `1` |  | The far vegetation sprites end by their distance times the engine's factor, as the original's (River's at 304 m, not 400: no fogged trees round the sky in a gap of the far mountains) |
| `d3d12_SpriteSort` | `1` |  | The particles drawn back to front, as the original sorts them (Volcano's smoke over the lava without steps) |
| `d3d12_StaticCasters` | `1` |  | The palms and the other static casters shadow the terrain everywhere |
| `d3d12_UnderwaterBodies` | `1` |  | The fish under the ocean drawn last without the depth: no shore foam in their shape over the water |
| `d3d12_UpscaleHdr` | `1` | yes | With HDR, the upscaler gets the scene's light before the tone map |
| `d3d12_WaterAsync` | `1` |  | The FFT ocean's waves made on the compute queue beside the frame |
| `d3d12_WaterDeferred` | `1` |  | The ocean's grid shaded once a pixel |
| `d3d12_WaterGeometry` | `1` |  | The ocean drawn as a grid raised to the waves' height |
| `d3d12_WaterSSR` | `1` |  | The water's screen space reflection (0: none, to measure it) |
| `d3d12_WaterUnder` | `1` |  | The light in the water with the camera under the ocean's surface |
| `d3d12_WaterUnderClear` | `1` |  | Under the water, the light it scatters from the sun and the sky |
| `d3d12_WaterUnderLine` | `1` |  | A camera just over the water whose near plane dips under it sees the water's light there |
| `d3d12_WaterUnderShare` | `1` |  | Under the water, the sun's light shared over a 4 x 4 window (faster) |
| `d3d12_WaterVolume` | `1` |  | The ocean's water attenuates each colour by the level's water colour |
| `d3d12_WaterVolumeColor` | `1` |  | The water of a `WaterVolume` material (pools, cave water) shows the scene behind it times the material's colour, as the original's |
| `d3d12_FogVolumeDepth` | `1` |  | A fog volume's fog over a point under its surface at least that of the point's depth: a dense one (Training's bunker pool) hides its floor |
| `d3d12_VolumetricLightRaysHalf` | `1` |  | The volumetric light's sun by rays from every other froxel a frame, the others keep their state for a frame (half the rays) |
| `d3d12_WaterCaustics` | `1` |  | The caustics of the FFT ocean's waves on the sea floor |
| `r_ParallaxMaterials` | `1` |  | `r_ParallaxDepth` scaled by each texture's physical material |

### Limits of the rays

Details: [Ray tracing](mod/XRenderD3D12/README.md#ray-tracing).

| Variable | Default | Saved | What it does |
|---|---|---|---|
| `d3d12_RTDistance` | `150` | yes | The rays' length (m) |
| `d3d12_RTMask` | `31` |  | The kinds of geometry the rays see, bits: 1 static, 2 alpha tested, 4 changing every frame (a draw is seen when the mask has all of its kinds) |
| `d3d12_RTMemory` | `384` | yes | The memory of the static acceleration structures (MB) beyond which the least recently used go |
| `d3d12_RTOffscreen` | `1` | yes | What out of the view the scene for rays holds: 1 for the rays from the view's surfaces, 2 always, 0 never |
| `d3d12_RTPbrQuad` | `0` |  | 1 one reflection ray for each 2 x 2 pixels (faster, moire under TAA) |

### Tuning of the rays and the ambient occlusion

Details: [Ray tracing](mod/XRenderD3D12/README.md#ray-tracing), [Ambient occlusion](mod/XRenderD3D12/README.md#ambient-occlusion).

| Variable | Default | Saved | What it does |
|---|---|---|---|
| `d3d12_AOApply` | `1` | yes | What the ambient occlusion darkens: 1 the ambient light alone, 0 the whole picture |
| `d3d12_AOBakedShare` | `0.5` | yes | The ambient light's share of the light baked into the terrain and the vegetation |
| `d3d12_AODenoise` | `2` | yes | The ambient occlusion's denoising passes |
| `d3d12_RTGIBounce` | `1` | yes | `d3d12_RTGI`: how much light what the rays find gives back |
| `d3d12_RTGIDistance` | `4` | yes | `d3d12_RTGI`: how far its rays look (m) |
| `d3d12_RTLightShadowGap` | `0.1` | yes | The lamps' shadows: how far short of a lamp its rays end (m) |
| `d3d12_RTLampSize` | `0.05` | yes | The lamps' shadows: a lamp's radius as a share of its light's (at most 0.5, as `d3d12_PTLampSize`): each pixel's ray goes to a point of that disc, soft edges, a caster hiding part of a big lamp takes part of its light; 0 a point, hard shadows |
| `d3d12_RTLampRays` | `4` | yes | With `d3d12_RTLampSize`, the rays a pixel traces towards points of a lamp's disc (1, 2, 4 or 8): the share that reaches it is its light; fewer, a noisier edge where the frames are not gathered |
| `d3d12_RTPbrMetalDiffuse` | `0.5` |  | The share of a metal's diffuse light beside its reflection |
| `d3d12_RTProjectorShadowGap` | `0.5` | yes | How far short of a projected light the rays towards it end (m) |
| `d3d12_RTSunAngle` | `0.53` | yes | The sun's disc in degrees across, the shadows' soft edges; 0 hard |
| `d3d12_RTSunCrease` | `1` |  | The sun's rays from a crease (a floor meeting a wall) off the pixel's own face and from 0 m, not 1 cm: no bright seam line in the path tracer's sun |
| `d3d12_RTSunDenoise` | `1` |  | The sun's shadow by rays: 1 the frames before and a filter, 2 AMD FidelityFX Denoiser, 0 none |

### Tuning of the path tracer

Details: [Path tracing](mod/XRenderD3D12/README.md#path-tracing).

| Variable | Default | Saved | What it does |
|---|---|---|---|
| `d3d12_PTAmbient` | `1` | yes | The game's ambient light the paths fill in with |
| `d3d12_PTAmbientRange` | `2` | yes | How far a ray finds nothing before it brings that ambient light (m) |
| `d3d12_PTBakedFalloff` | `0` | yes | The falloff of the lamps baked into the light maps |
| `d3d12_PTBehind` | `40` | yes | How far behind the camera the scene for rays reaches (m) |
| `d3d12_PTCache` | `1` | yes | The radiance cache of the paths' last bounce; 0 none |
| `d3d12_PTEmission` | `1` | yes | The light of what glows |
| `d3d12_PTFaceNormal` | `1` |  | The side of a triangle the camera sees (a trunk's normal) from 1 / w's plane, not the world positions' differences |
| `d3d12_PTShadeFloor` | `1` |  | Where the sun is hidden (all of a frame without the sun) no darker than the raster's shade: the terrain's baked light (times the tint in a live caster's shadow), a light map's light and the ambient |
| `d3d12_PTLampSize` | `0.05` | yes | A lamp's radius as a share of its light's (at most 0.5): its shadow's rays go to points of a disc of that size, soft edges; 0 a point, hard shadows |
| `d3d12_PTDecalLight` | `1` | yes | A draw lit by its light map after the paths (a decal blended over the hull it lies on: Carrier's ship's name) takes the paths' light of the surface under it in the raster's ratio of their lights, not the raster's light alone (dark letters over a hull the paths lit brighter); 0 the raster's light |
| `d3d12_PTLeafLight` | `1` |  | The leaves no darker than the raster lights them (its bake: twice the ambient, the sun times \|N.L\|), not near black under a crown |
| `d3d12_PTFadeBlend` | `1` |  | The objects that fade with the distance (Carrier's deck crates) blended by their alpha after the trace, as without the paths, not whole from the start of their fade |
| `d3d12_PTFillLights` | `0` | yes | The characters' fill lights: 0 in their ambient light, 1 lamps with a ray |
| `d3d12_PTLamps` | `1` | yes | The lamps' light |
| `d3d12_PTLightDistance` | `60` | yes | The lights within this distance of the camera (m) |
| `d3d12_PTLightMaps` | `1` | yes | Indoors, the light maps' light beyond the path tracer's own |
| `d3d12_PTSky` | `1` | yes | The sky's light |
| `d3d12_PTSkyPieces` | `1` |  | The rays pass the pieces of the sky (Training's bunker's windows): a path that reaches one has left the scene |
| `d3d12_PTSpecular` | `1` | yes | The highlights of the sun and the lamps |
| `d3d12_PTSun` | `1` | yes | The sun's light |
| `d3d12_PTSunHistory` | `64` | yes | The frames the sun's shadow by rays holds at most |
| `d3d12_PTTerrainDelight` | `2` | yes | The light baked into the terrain's texture taken off for its albedo |
| `d3d12_PTVegetationGain` | `1` |  | The plants the paths do not light (blended, reflected) take the light baked into their colours |
| `d3d12_PTWaterPlants` | `1` |  | The plants in the water blended after it, as without the paths (no foam round their leaves) |

### Tuning of HDR and the exposure

Details: [HDR](mod/XRenderD3D12/README.md#hdr).

| Variable | Default | Saved | What it does |
|---|---|---|---|
| `d3d12_BloomThreshold` | `1` | yes | The light above which the bloom starts |
| `d3d12_ExposureHigh` | `0.95` | yes | The automatic exposure's meter: the share of the bright spots it leaves out |
| `d3d12_ExposureHighlights` | `0.9` | yes | The meter: the percentile of the sky kept at the tone map's knee |
| `d3d12_ExposureLow` | `0.05` | yes | The meter: the share of the dark corners it leaves out |
| `d3d12_ExposureMax` | `1.5` | yes | The most EV of the automatic exposure |
| `d3d12_ExposureMeter` | `1` | yes | The meter: 1 by a histogram, 0 the mean |
| `d3d12_ExposureMin` | `0` | yes | The least EV of the automatic exposure |
| `d3d12_ExposureSpeed` | `1.5` | yes | The automatic exposure's rate per second |
| `d3d12_HDRGlow` | `3` | yes | The light of what glows (fire, flashes, coronas) times this |
| `d3d12_HDRGlowHue` | `1` | yes | A lamp's glow map and the lights' coronas brighter only as a whole colour: their brightest channel's light after the exposure at most this (times the peak over paper white with the HDR output), the hue kept through the tone map (a cyan tube not a white disc); 0 each channel boosted alone |
| `d3d12_HDRHighlights` | `2` | yes | With the HDR output, the light from which the scene goes on to the peak |
| `d3d12_HDRSky` | `2` | yes | The sky's light towards the sun times up to this |
| `d3d12_HDRSkyAngle` | `4` | yes | The angle from the sun of the sky's glow at half way (degrees) |
| `d3d12_HDRSkyPieces` | `1` |  | A piece of the sky with a glow map (the opening of Training's cave's shaft, the bunker's windows) at most the game's white, not a lamp: no blue halo of the bloom round it |
| `d3d12_HDRSun` | `8` | yes | The light of the sun's flare times this |
| `d3d12_ToneMapDesaturation` | `3` | yes | `d3d12_ToneMap 4`: how fast the light far above white loses its hue |
| `d3d12_ToneMapKnee` | `0.85` | yes | Where the tone map's shoulder starts |
| `d3d12_ToneMapToe` | `0.02` | yes | `d3d12_ToneMap 2`: the ACES fit's soft toe, from black linear up to this light and smoothly into the curve, not black below 0.00325; 0 the fit as it is |

### Tuning of the light in the air and in the leaves

Details: [Volumetric light](mod/XRenderD3D12/README.md#volumetric-light), [Foliage translucency](mod/XRenderD3D12/README.md#foliage-translucency).

| Variable | Default | Saved | What it does |
|---|---|---|---|
| `d3d12_VolumetricLightAmbient` | `1` | yes | The light from all round the air scatters |
| `d3d12_VolumetricLightAnisotropy` | `0.3` | yes | How much more of the sun's light the air scatters forwards |
| `d3d12_VolumetricLightDensity` | `0.002` | yes | The air's extinction near the water (1 / m) |
| `d3d12_VolumetricLightDistance` | `60` | yes | How far the volumetric light reaches (m) |
| `d3d12_VolumetricLightHeight` | `40` | yes | The height over which the air thins to half (m) |
| `d3d12_VolumetricLightIndoors` | `2.5` | yes | With the sun by rays, the air indoors this much denser |
| `d3d12_VolumetricLightShafts` | `4` | yes | The shafts of the sun alone: the air lit as much as the air around it (this many m across the sun) is in shadow; 0 all the air |
| `d3d12_VolumetricLightShaftsPower` | `3` | yes | How narrow the shafts are |
| `d3d12_VolumetricLightShaftsSun` | `16` | yes | The sun's light of the shafts |
| `d3d12_VolumetricLightSun` | `1` | yes | The sun's light the air scatters |
| `d3d12_FoliageTranslucencyDistance` | `60` | yes | Where the light through the leaves ends (m) |
| `d3d12_FoliageTranslucencyPower` | `4` | yes | How narrow it is around the sun |

### Tuning of the water

Details: [Water](mod/XRenderD3D12/README.md#water).

| Variable | Default | Saved | What it does |
|---|---|---|---|
| `d3d12_WaterChop` | `2.5` |  | How sharp the FFT ocean's crests are |
| `d3d12_WaterCrestLight` | `1` |  | The sun's light through the thin crests |
| `d3d12_WaterGlint` | `1` |  | The sun's glint on the water |
| `d3d12_WaterGlintRoughness` | `0.004` |  | How wide the glint is |
| `d3d12_WaterGlintSky` | `1` | yes | The glint only where the sky box shows a sun towards it (its brightness there, from 0.12 none to 0.24 all): none under Rebellion's storm cloud, in Volcano's smoke or under Steam's night sky (its moon is painted elsewhere; the water mirrors it with the sky); 0 everywhere |
| `d3d12_WaterReflectionBlur` | `0.1` |  | How much the fine ripple blurs the ocean's reflection |
| `d3d12_WaterReflectionFlags` | `4184` |  | What the water's reflection shows (`DrawLowDetail` flags) |
| `d3d12_WaterSlope` | `0.0446` |  | The steepness of the FFT ocean's waves |
| `d3d12_WaterSurf` | `0.8` |  | The height of the surf breaking on the shores (m) |
| `d3d12_WaterSurfNight` | `0.3` | yes | The share of the surf's foam shown under a moon (the engine's sun's light up to 0.36, all from 0.42): Steam's night shore without a white line far brighter than its islands; 1 as by day |
| `d3d12_WaterPondRipple` | `0.05` | yes | On the ponds and lakes the level's terrain cuts off from the sea, the original's ripple in place of the FFT waves (its terrain water's 32 frames at a tile of 8 m): this is its slopes' RMS; 0 the waves |
| `d3d12_WaterUnderAdapt` | `1` |  | Under the water, the eye's white balance |
| `d3d12_WaterUnderExposure` | `1` |  | Under the water, the eye's exposure (EV more) |
| `d3d12_WaterWind` | `3.5` |  | The wind of the FFT ocean's sea (m/s) |

### Tuning of the materials and the far vegetation

Details: [Parallax and generated normals (RayMaster)](mod/XRenderD3D12/README.md#parallax-and-generated-normals-raymaster), [Vegetation sprites and terrain detail](mod/XRenderD3D12/README.md#vegetation-sprites-and-terrain-detail).

| Variable | Default | Saved | What it does |
|---|---|---|---|
| `r_ParallaxDepth` | `0.03` |  | The parallax's depth, a fraction of the texture's side |
| `r_ParallaxFilter` | `4` |  | The parallax's height averaged over this many texels |
| `r_SkinRoughness` | `0.55` |  | The matte skin's roughness |
| `r_SkinRoughnessOily` | `0.35` |  | The oily skin's roughness |
| `r_SkinScatter` | `1` |  | How far the light under the skin reaches |
| `d3d12_SpriteBlend` | `0.2` | yes | The far vegetation sprites' blend between their pictures |
| `r_TexUpscaledLight` | `0` |  | 1 the textures of the templates that add light (glows, flashes, the bunker's shafts) take their upscaled pictures too; 0 their own |
| `r_TexUpscaledSky` | `0` |  | 1 the sky boxes take their upscaled pictures too (compressed: the smooth gradients in steps); 0 the game's own |

### Read only: what the renderer found

| Variable | Default | Saved | What it does |
|---|---|---|---|
| `d3d12_HDROutputState` | `SDR` |  | The HDR output now: SDR, HDR10 or scRGB (and why SDR) |
| `d3d12_HDRDisplayOn` | `-1` |  | The window's display: 1 in HDR mode, 0 in SDR, -1 not found |
| `d3d12_HDRDisplayPeak` | `0` |  | Its peak in nits |
| `d3d12_HDRDisplayFullFrame` | `0` |  | Its full frame light in nits |
| `d3d12_HDRDisplayWhite` | `0` |  | Windows' SDR content brightness in nits |

### The original renderer's

The renderer registers the original renderer's other variables too, so that the game's modules find them (`mod/XRenderD3D12/ConsoleVariables.cpp`); of those it reads only the ones in these tables.

| Variable | Default | Saved | What it does |
|---|---|---|---|
| `r_DetailDefault` | `Textures/Detail/rock` |  | Registered for the game, not read |
| `r_DetailDistance` | `4` | yes | Where the terrain's detail textures fade out (m) |
| `r_DetailScale` | `8` |  | Metres per repeat of the detail texture |
| `r_DisableSfx` | `0` | yes | 1 no screen effects |
| `r_EnvCMResolution` | `2` | yes | The environment cubes' size, 0 to 2 |
| `r_EnvCMupdateInterval` | `0.1` | yes | Seconds between the environment cubes' faces |
| `r_NoDrawNear` | `0` |  | 1 the first person weapon not drawn |
| `r_ResetScreenFx` | `0` |  | Registered for the game, not read |
| `r_TexSkyResolution` | `0` | yes | Registered for the game, not read |
| `r_WaterRefractions` | `0` | yes | Registered for the game, not read |
| `r_WaterUpdateDeltaAngle` | `5` |  | The water's reflection redrawn when the camera turns this much (degrees) |
| `r_WaterUpdateDistance` | `2` |  | The same when it moves this far (m) |
| `r_WaterUpdateFactor` | `0.01` | yes | The same after this many seconds per metre to the water |
| `r_WaterUpdateMinInterval` | `0.00833` |  | But by time not sooner than this many seconds (1/120: above 120 FPS not every frame; the water reads it through the matrix it was drawn with). `0` as the original |
