# farcry1-raymaster

Mods for Far Cry, built with the CryENGINE MOD SDK for Far Cry:

- `mod/XRenderD3D12` - a Direct3D 12 renderer for the x64 game. Its options, for players and the
  system ones: [`CONFIGURATION.md`](CONFIGURATION.md).
- `mod/XRenderD3D9Traced` - a renderer proxy that traces the calls between the engine and its
  renderer.
- `mod/XRenderCommon` - what the renderers share about the renderer interface beyond the SDK
  headers.
- `mod/Demos` - demo scenes that run inside the game, and their runner.
- `third_party/amd-fsr-sdk` - the parts of the AMD FSR SDK that XRenderD3D12 uses for AMD FSR
  Upscaling.
- `third_party/xegtao`, `third_party/fidelityfx-cacao` - Intel XeGTAO and AMD FidelityFX CACAO,
  the ambient occlusion of XRenderD3D12; `third_party/directx-headers` - Microsoft's d3dx12
  headers, which CACAO needs.
- `third_party/smaa` - SMAA (Enhanced Subpixel Morphological Antialiasing), the SMAA 1x of
  XRenderD3D12's anti-aliasing.
- `third_party/cmaa2` - Intel's CMAA2 (Conservative Morphological Anti-Aliasing 2.0), the CMAA2 of
  XRenderD3D12's anti-aliasing.
- `third_party/fidelityfx-cas` - AMD FidelityFX CAS (Contrast Adaptive Sharpening), the sharpening
  of XRenderD3D12's TAA.
- `third_party/nrd`, `third_party/shadermake`, `third_party/mathlib` - git submodules: NVIDIA's NRD,
  the denoiser of XRenderD3D12's path tracer, with the tool that builds its shaders and its math
  library. The repository has their links, not their files: clone it with `--recurse-submodules`
  (or run `git submodule update --init`); without them XRenderD3D12 is built without NRD.
- `third_party/fidelityfx-denoiser` - a git submodule: AMD FidelityFX Denoiser, whose shadows
  denoiser XRenderD3D12 can take for the sun's shadow by rays; without it that denoiser is left out.
- `assets/levels` - `Patch Levels.cmd`, which moves pieces of the levels where the sun of the ray
  traced lighting needs them, in the levels of an installed game (`Patch Levels.cmd <game
  folder>`; each pak it changes is kept as `<name>.orig`, copying that back gives back the original
  level): in Training the two concrete roof slabs over the north edge of the bunker's roof opening
  go under its floor, so the sun's shafts come in through both of the roof's openings, not one.
- `assets/scripts` - patches to the game's scripts, and `Patch Scripts.cmd`, which applies them in
  the order of their numbers to the scripts of an installed game (`Patch Scripts.cmd <game
  folder>`, written to `FCData\zz_RayMaster.pak`; deleting that file gives back the original
  scripts):
  - `01-field-of-view.patch` - a field of view for screens wider than 4:3 that keeps the view up and
    down of a 4:3 screen (Hor+), and the console variable `cl_fov`, the field of view across the
    screen in degrees (0, the default, for the automatic one).
  - `02-hdr-options.patch` - HDR entries in Advanced Video Options, where the game hides its own:
    HDR, None, Soft, ACES or AgX, which sets `r_HDRRendering` and `d3d12_ToneMap`, the HDR
    pipeline of XRenderD3D12 and its tone map; and HDR output, SDR, HDR10 or scRGB, which sets
    `d3d12_HDROutput`: on a display in HDR mode (Windows: Use HDR) the sun, fire and lamps go up to
    the display's peak brightness, while the rest of the picture and the menu stay as bright as SDR
    content.
  - `03-video-ambient-occlusion.patch` - Ambient Occlusion in Advanced Video Options: None, GTAO or
    CACAO, which sets `d3d12_AO`, the screen space ambient occlusion of XRenderD3D12 (Intel
    XeGTAO or AMD FidelityFX CACAO); the Basic Video Options button moves to the bar of Apply.
  - `04-video-ray-tracing.patch` - Ray Traced Reflections in Advanced Video Options: None, Rays or
    Hybrid, which sets `d3d12_RTReflections`, the water's reflection by rays of XRenderD3D12 (a
    graphics card with DXR 1.1; greyed out without one); the rows of the page move closer together.
  - `05-video-upscaler.patch` - Upscaler in Video Options, under Resolution: None, FSR (auto),
    FSR 3.1 or FSR 4, which sets `d3d12_Upscaler` and `d3d12_UpscaleVersion`, the AMD FSR
    Upscaling of XRenderD3D12 (auto: the version FSR picks for the graphics card); and Upscaler
    Mode, Native AA, Quality, Balanced, Performance or Ultra Performance, which sets
    `d3d12_UpscaleMode`, the size the 3D scene is rendered at.
  - `06-video-vsync.patch` - Vertical Sync in Video Options stays on in a game, and Apply sets it
    without a relaunch: XRenderD3D12 takes `r_VSync` every frame (the game greys it out there, as
    its Direct3D 9 renderer had to reset its device for it; with another renderer it still does).
  - `07-video-path-tracing.patch` - Path Tracing in Advanced Video Options: None, Real Time or
    Reference, which sets `d3d12_PathTracing`, the lighting of XRenderD3D12 by paths in place of the
    light maps (a graphics card with DXR 1.1; greyed out without one); choosing it while HDR is None
    sets HDR to Soft.
  - `08-hdr-page.patch` - an HDR page of the video options: HDR output in Advanced Video Options
    becomes a button that opens it. On it the output (SDR, HDR10 or scRGB, with what the display
    is), SDR Brightness (Windows' or 80 to 480 nits), Peak Brightness (the display's or 250 to 4000
    nits) and Frame Average Limit (Display or Off: the frame's average at most the display's full
    frame light, so that an OLED does not dim the picture itself), applied at once; while it is
    open, on a display in HDR mode, a test under them: a square at 10000 nits round a smaller one
    at the peak, which disappears once the peak is the display's, and patches at the SDR
    brightness, twice and four times that and at the peak.
  - `09-video-anti-aliasing.patch` - Anti-Aliasing in Video Options with XRenderD3D12: None,
    FXAA, SMAA 1x, CMAA2, TAA or MSAA 2x / 4x / 8x, which sets `d3d12_AA`, its anti-aliasing of the 3D scene without an upscaler, in place
    of the game's FSAA modes; on in a game and applied without a relaunch, greyed out while the
    Upscaler is not None (FSR anti-aliases then).

How to build and run them: `AGENTS.md`.

These mods are the work of their authors, not of Crytek or Ubisoft, and are not endorsed by them.
Far Cry and CryENGINE are trademarks of their owners.

## License

Everything in this repository outside `sdk/`, `third_party/` and `mod/XRenderCommon/` is free
software under the GNU General Public License, version 3 or (at your option) any later version
(`LICENSE`), with an additional permission to combine it with the game Far Cry, the CryENGINE MOD
SDK for Far Cry, the AMD FSR SDK and NVIDIA NRD (`LICENSE-EXCEPTION.md`).

`third_party/amd-fsr-sdk/` holds parts of the AMD FSR SDK by Advanced Micro Devices, Inc., under
AMD's MIT license (`third_party/amd-fsr-sdk/docs/license.md`); the build downloads its upscaler
DLL (`third_party/amd-fsr-sdk/README.md`). `third_party/xegtao/` holds parts of XeGTAO by Intel
Corporation, `third_party/fidelityfx-cacao/` parts of FidelityFX CACAO by Advanced Micro Devices,
Inc., and `third_party/directx-headers/` parts of DirectX-Headers by Microsoft Corporation, each under
its MIT license (`LICENSE`, `license.txt`, `LICENSE` in its folder). `third_party/smaa/` holds SMAA's
shader and precomputed textures by Jorge Jimenez, Jose I. Echevarria, Belen Masia, Fernando Navarro
and Diego Gutierrez, under its MIT license (`LICENSE.txt` in its folder). `third_party/cmaa2/`
holds CMAA2's shader by Intel Corporation, under the Apache License 2.0 (`license.txt` in its
folder). `third_party/fidelityfx-cas/` holds FidelityFX CAS's headers by Advanced Micro Devices,
Inc., under its MIT license (`LICENSE.txt` in its folder).

`third_party/nrd`, `third_party/shadermake` and `third_party/mathlib` are git submodules: links to
NVIDIA's repositories of NRD (NVIDIA Real-time Denoisers) at the tag v4.18.0 and of ShaderMake and
MathLib at the commits it names, whose files are not part of this repository. NRD is under the
NVIDIA RTX SDKs License (`LICENSE.txt` in its repository), which allows distributing it only built
into an application; ShaderMake and MathLib are under NVIDIA's MIT licenses.
`third_party/fidelityfx-denoiser` is a git submodule too: a link to AMD's repository of FidelityFX
Denoiser at the tag v1.2, under AMD's MIT license (`LICENSE.txt` in its repository).

`mod/XRenderCommon/` is dedicated to the public domain (CC0 1.0, `mod/XRenderCommon/COPYING`): its
authors claim no rights to it, and any renderer for the game may use it, under any license.

`sdk/` is the CryENGINE MOD SDK for Far Cry by Crytek, under its own license (`sdk/EULA.txt`), not
under the GPL. That license also sets terms for distributing mods made with the SDK (free of
charge, by electronic means only, with the license attached); read it before you distribute a
build.

## AI training

Please do not use the code or the documentation of this repository to train or fine-tune machine
learning models, or put them into datasets for that.
