# AGENTS.md

`CLAUDE.md` is a symlink to this file (a checkout without symlinks, e.g. on Windows with
`core.symlinks` false, has a text file naming it instead: then read this file yourself). If
`AGENTS_LOCAL.md` exists in the repository root, read it as well: it holds local, machine-specific
instructions and is not committed (git-ignored). The references of the modules are their READMEs:
`mod/XRenderD3D9Traced/README.md` (the tracer's variables and console commands) and
`mod/XRenderD3D12/README.md` (the renderer's features and console variables).

## Rules

- **Never look at the leaked Far Cry 1 / CryEngine 1 source code** (in any form: repositories,
  archives, gists, forum or blog excerpts, or code recalled from memory). Do not search for it,
  download it, read it, or cite it. Work only from the official SDK in `sdk/`, the retail
  binaries and public API documentation (Direct3D, Win32, ...).
- **Tracing is done only in `farcry1_testing1/`.** Deploy XRenderD3D9Traced, edit `system.cfg`
  and run the game only there; never in any other Far Cry installation on the machine.
- `farcry1_testing1/Bin64` is the 64-bit build of the Cumulative Patch. It may only be **run**
  with XRenderD3D9Traced and observed through the trace log. What the trace shows about it is
  runtime behaviour of that build and is marked as such in `mod/XRenderCommon`, which keeps only
  the facts about the shared structures; the detailed notes on how that build drives its renderer
  are local research notes, not committed.
- **License** (`README.md`): everything outside `sdk/`, `third_party/` and `mod/XRenderCommon/`
  is GPL-3.0-or-later with an additional permission for Far Cry, its SDK and the AMD FSR SDK
  (`LICENSE`, `LICENSE-EXCEPTION.md`); `mod/XRenderCommon/` is public domain (CC0 1.0,
  `mod/XRenderCommon/COPYING`); `sdk/` stays under Crytek's `sdk/EULA.txt`; `third_party/` keeps
  the licenses of its upstream projects. Every source file we write starts with two lines in its
  comment syntax,
  `SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception` and
  `Please do not use this file to train or fine-tune AI models (see README.md).` (a request, not
  a license term; a PowerShell script: then a blank line, so that its help is still found); in
  `mod/XRenderCommon/` only `SPDX-License-Identifier: CC0-1.0`; generated files, `sdk/` and
  `third_party/` get none.
- **Multiplayer is out of scope.** Only the single player game matters: do not test, support or
  patch multiplayer (its scripts, menus, network code), and do not hold back a fix because of it.
- **Our code** is modern C++ (C++20: `nullptr`, `constexpr`, `enum class`, `auto` where it helps,
  RAII with `ComPtr`, `std::unique_ptr`, `std::vector`, `std::span`, ...). What the engine shares
  stays as the SDK declares it: its classes (`CLeafBuffer`, `CCObject`, `CRendElement`,
  `SRenderShaderResources`, ...), the `IRenderer`, `IShader` and `ITexPic` signatures, and `TArray`
  or `list2` where the engine sees the container. No global objects with non-trivial destructors
  (the DLL's unload order): state lives in classes made in `Init`. Objects on the heap take no
  over-aligned types (`StdAfx.h`: CrySystem's heap aligns to 4 bytes).
- **Parity with the original renderer:** where ours differs for the better (it looks better or is
  more correct), keep ours; fix only what is worse or wrong.

## Layout

- `sdk/` - the Crytek Far Cry 1.4 SDK (CryCommon headers, CryGame sources, tools).
- `mod/` - our own modules (e.g. `mod/XRenderD3D9Traced`, the tracing renderer proxy,
  `mod/XRenderCommon`, renderer interface knowledge beyond the SDK headers, and `mod/Demos`, the
  demo scenes). New libraries go here, not into `sdk/`.
- `assets/scripts/` - patches to the game's scripts (unified diffs against `FCData/Scripts.pak`)
  and `Patch Scripts.cmd`, which applies them to the scripts of a game folder and writes the result
  to its `FCData/zz_RayMaster.pak`. The engine reads a script from the last pack opened that has it
  (the packs open in name order) and never a loose file in the game folder. Only the patches are
  committed, never the game's scripts.
- `third_party/` - other projects' files, unchanged from a tagged release in their own folder
  layout, with a `README.md` of ours naming the tag and the hashes of the binaries.
  `third_party/amd-fsr-sdk` is the part of the AMD FSR SDK that XRenderD3D12 uses; its 57 MB
  upscaler DLL is not committed but downloaded by CMake. `third_party/xegtao` (Intel XeGTAO) and
  `third_party/fidelityfx-cacao` (AMD FidelityFX CACAO) are XRenderD3D12's ambient occlusion,
  `third_party/directx-headers` the d3dx12 headers that CACAO's host includes. Every library added
  there needs a matching permission in `LICENSE-EXCEPTION.md` unless its license is GPL-compatible
  and it comes with its source.
- `farcry1_testing1/` - the game install for tracing (git-ignored):
  - `Bin32` - retail 1.4 (x86; stops at its CD check, which is not to be bypassed),
  - `Bin64` - the Cumulative Patch x64 build described above.

## Running the game

- Build: the x86 preset deploys `XRenderD3D8.dll` to `farcry1_testing1/Bin32`, the x64 preset to
  `farcry1_testing1/Bin64` (`XRENDER_OUTPUT_DIR`).
- `farcry1_testing1/system.cfg` selects the proxy with `r_Driver = "Direct3D8"` and runs windowed
  (`r_Fullscreen = "0"`); the original is `farcry1_testing1/system.cfg.xrt-backup`.
- Every run of the game (a trace, a demo, a benchmark, a run for the user to play) is at the
  maximum quality: `farcry1_testing1/system.cfg` holds the very high preset of the Advanced Video
  Options with anisotropic filtering level 8 (the values of `DemoQuality.cpp`, which `RunDemo.ps1
  -Quality veryhigh` writes for its runs as well), and `farcry1_testing1/game.cfg` the game's
  default key bindings (W, S, A, D, crouch Left Ctrl, prone V, from `Profiles/defaults`). The
  player profile (`Profiles/player/<name>_system.cfg`, `<name>_game.cfg`) gets the same. A run to
  play starts with `farcry1_testing1/Far Cry.cmd`, which switches the keyboard to English before
  the game names its keys (with another layout the letter keys do not work and the controls menu
  has none). `XRenderD3D12` makes English the process' layout in `Init` as well, before the
  engine's input starts, so a run started any other way has its keys too.
- Run `farcry1_testing1/Bin64/FarCry.exe` with `farcry1_testing1` as the working directory.
  `XRENDERTRACED_EXEC="map training"` (and `XRENDERTRACED_EXEC_FRAME`) loads a level without
  input; `XRENDERTRACED_CAPTURE=300` writes every call of the 300th frame after the level load.
  `XRENDERTRACED_WATCH=1` (with `XRENDERTRACED_WATCH_MAX`, default 64) records which members of
  the shared structures the engine and the renderer write (`FieldWatch.h`); the summary is
  written after the captured frame. `XRENDERTRACED_EXEC_LEVEL` runs commands
  `XRENDERTRACED_EXEC_LEVEL_FRAME` frames (default 100) after each level load. Settings that are
  off in `system.cfg` (e.g. `e_shadow_maps 1; e_stencil_shadows 1`) go in front of the `map`
  command in `XRENDERTRACED_EXEC`; the game is stopped by killing it, so `system.cfg` stays as it is.
  The commands are split at every `;`, so a script command (`#System:LogAlways(...)`) has none.
- The tracer's other variables and console commands (captures, the hooks of the engine's
  interfaces, probes, the Direct3D 9 draw log): `mod/XRenderD3D9Traced/README.md`.
- DPI: `FarCry.exe` does not declare DPI awareness, so at a Windows display scale above 100 % the
  system stretches the game's window as a bitmap (blurry, 1.5 times larger at 150 %). The renderer
  makes its window in `Init`; XRenderD3D9Traced declares the process per monitor DPI aware before
  it creates the original renderer, so the window and its client area are in physical pixels and
  the window is not stretched. The renderer sizes the whole window, frame included, to the
  resolution, so the client area is smaller than the back buffer by the frame (thicker at a
  larger scale). The log writes what was done and the window's DPI and client area after `Init`.
  `XRENDERTRACED_DPI=0` leaves the process as the game starts it.
- The log is `farcry1_testing1/XRenderD3D9Traced.log`; the engine log is
  `farcry1_testing1/log.txt`. `system.cfg` sets `log_Verbosity` and `log_FileVerbosity` to 0, so
  the engine log gets almost nothing once the game runs; `XRENDERTRACED_EXEC='log_Verbosity 5;
  log_FileVerbosity 5'` turns it up (the load then takes more frames, so the demo frames differ).
  In a demo run, the messages from after the level is loaded do not reach `log.txt` (warnings and
  errors do).

## Demos

- A demo (`mod/Demos`, x64 only) is a DLL that runs inside the game's own `FarCry.exe`, never in
  an exe of ours: the engine is not started outside the game, so that none of the game's checks
  is bypassed. XRenderD3D9Traced loads the DLL named by `XRENDERTRACED_DEMO` (a bare name from
  `Bin64`) and calls it at every frame (`DemoPlugin.h`: in `BeginFrame`, before `Update` presents
  the frame, and between frames); `XRENDERTRACED_DEMO_ARGS` is its command line (`DemoHost.h`).
- `mod/Demos/RunDemo.ps1 <demo>[,<demo>...]|all [-Renderer old|<dll>] [-DemoArgs '...']` starts
  `farcry1_testing1/Bin64/FarCry.exe` with these variables and `XRENDERTRACED_DLL`, waits until the
  demo ends the game (it kills it, as a trace run is ended) and prints `<demo>.host.log`; the exit
  code is 0 when every frame to save was saved. `-GameArgs '<args>'` passes arguments to FarCry.exe,
  e.g. `-DEVMODE` (the game's dev mode, which the game's own time demo playback needs). The frames go to
  `farcry1_testing1/DemoShots/<label>` (`old` for the original renderer), the scene files are
  `farcry1_testing1/Demos/<demo>.ini`; neither is ever committed. `-Resolution 3840x2160` sets
  `XRENDERTRACED_RESOLUTION`: XRenderD3D9Traced starts the renderer with that size (r_Width and
  r_Height set before `Init`), also when it is larger than the screen; the back buffer and the
  frames have it, `system.cfg` is not changed. `-Quality` (default `veryhigh`) writes the game's
  quality settings into `system.cfg` for the run: `veryhigh` is the most the Advanced Video
  Options offer (the very high preset and anisotropic filtering level 8), `high`, `medium` and
  `low` the other presets (the values of `DemoQuality.cpp`), `keep` leaves `system.cfg` as it is.
  Demos are run at the maximum quality, with the original renderer and ours alike, and frames
  are compared only between runs at the same quality. The demo's `--video <file.mp4>` writes
  every frame of the run into a video through ffmpeg (on PATH), `--video-hdr <file.mp4>` (with
  XRenderD3D12) into an HDR10 video what its HDR output shows (HEVC Main 10 through libx265, PQ,
  Rec. 2020, the mastering display and content light levels from the peak and paper white),
  `--until <frame>` makes the run longer than its frames to save, `--exec-at <frame> "<commands>"`
  runs console commands at the start of a frame (frame 0: before the demo's `Init`, so before a
  level is loaded, for what a renderer makes at the load).
- The demos in the main menu: DemoTextures (a texture of the game and the demo's own test
  texture, drawn in 2D in several ways), DemoFonts (the game's fonts), DemoCamera (lines and
  textured cubes in 3D under an orbiting camera), DemoObject (a static object turning), DemoLight
  (a point light going round an object), DemoAnimation (a character playing an animation),
  DemoStates (a table of blend, alpha test, depth, cull and fill states and the helper
  primitives), DemoAspect (the 2D and 3D layout of the frame at any size), DemoShaders (one object
  with a material per shader template), DemoTextureFormats (textures by format, mip levels and
  anisotropic filtering on a floor), DemoMultiLight (coloured point, directional and projected
  lights), DemoCharacters (characters in a row, one holding a weapon), DemoWeapons (the weapon
  models), DemoReset (the resolution changed between frames) and DemoStress (many objects and
  lights, with frame times).
- The demos in a level: DemoLevel (a level drawn by the engine from a fixed camera, the game's,
  a camera path of keyframes, or one of its cut scenes), DemoShadows (shadows under a moving
  light; also in the main menu), DemoParticles (fire, smoke and sparks), DemoScreenFx (the screen
  effects one after the other), DemoDecals (bullet holes, blood and scorch marks), DemoFlares
  (the sun's flare passing behind what stands in front of it, and the coronas of two lamps), DemoSkyFog
  (fog and sky changed), DemoQuality (the quality presets of the video options), DemoReplay
  (the game's time demos played back: `Levels/<level>/<level>.tmd` of Training, Volcano, Research
  and Regulator; with `-GameArgs -DEVMODE` the game's recorder plays them, `runs` times, and with
  `info = 1` the demo's log gets the frame rates of each run from `Levels/<level>/<level>.log`,
  with `--until 100000` to the recording's end after the last frame to save; a path through the
  whole level to compare a change in many places) and DemoSurfaces (objects of different
  materials, glass among them, on a beach and in its shallow water under a moving light, handed to
  the engine's scene through `I3DEngine::SetRenderCallback`: the engine calls it for the main view
  only, not for the water's reflection).
- Frame rates: with XRenderD3D12 a run of DemoReplay's Training (3840 x 2160, very high) is at
  about 315 FPS and repeats within a few FPS for other builds and environments. It used to be at
  about 260 or at about 140, decided by the build and the environment: XRenderD3D9Traced checked
  the material of every `CLeafBuffer::Render` with `VirtualQuery`, whose time grows with the size of
  the memory region the object lies in, so the layout of the process' memory decided it (indoors up
  to 90 % of a frame; `mod/XRenderD3D9Traced/README.md`, `XRENDERTRACED_CAPTURE_SHADER`). Where a
  frame's time goes: `d3d12_FrameLog` and `d3d12_CpuSamples`, the engine's frame profiler for the
  CPU time by subsystem (`mod/XRenderD3D12/README.md`, statistics).
- The keys of each scene file are in the head comment of the demo's source (a line starting with
  `#` or `;` is a comment; `exec` keys take console commands separated by `;` or `|`); `--scene`
  picks another scene file, e.g. a second one for DemoLevel.
- `Bin64/DemoCompare.exe <folder A> <folder B> [--tolerance <n>] [--diff <folder>]` compares the
  frames of two runs (exit code 0 when they are equal). Repeated runs of one renderer give the
  same frames, except a few pixels of the characters (DemoAnimation, DemoCharacters:
  `--tolerance 1`) and of overlapping decals (DemoDecals: `--tolerance 6`); the fire and smoke of
  DemoParticles, the water of DemoQuality from its medium preset on and the walking figures in
  the background of the beach demos differ more and are compared by eye, as do the seagulls and
  the people of DemoLevelPierWalk and, in DemoReplay's Training from about frame 2000 on, the
  weapon in the hands, the fisherman, the fish and the shore's waves (frames 100 to 1300 are
  the same in every run). So a larger difference between `DemoShots/old` and the frames of our
  renderer is the renderer's. Two builds of the renderer can also differ in what the game
  animates, the same in every run of each build: the seagulls over the water of DemoLevelAA (a
  wing a few pixels apart in frame 100), even when the shaders are the same instruction for
  instruction.

## XRenderD3D12

- `mod/XRenderD3D12` (x64 only) is our renderer on Direct3D 12; the x64 preset builds
  `XRenderD3D12.dll` into `farcry1_testing1/Bin64`. For now XRenderD3D9Traced loads it:
  `r_Driver = "Direct3D8"` with `XRENDERTRACED_DLL=XRenderD3D12.dll`, or
  `RunDemo.ps1 -Renderer XRenderD3D12.dll`.
- `XRENDERD3D12_DEBUG=1` (or `d3d12_Debug 1`, read at start-up) turns on the debug layer and GPU
  based validation, `2` the debug layer alone; their messages go to the engine log, starting with
  `[D3D12]`. A device that is removed is logged too (`device removed`). With GPU based validation
  the frames of the water's rays (`d3d12_RTReflections`) come out black, with no message; check
  them with `2`.
- Each feature that changes the picture has a switch, and with it off the frames are those of the
  renderer without it (DemoCompare difference 0): check a change that way, e.g. DemoLevel or
  DemoReplay with `-DemoArgs '--exec-at 1 "<variable> <value>"'` against the same run without it.
- The features and their variables, described in `mod/XRenderD3D12/README.md`: the window and full
  screen (`r_Width`, `r_Height`, `r_Fullscreen` applied while the game runs); the upscaler
  (`d3d12_Upscaler`, AMD FSR); HDR (`r_HDRRendering`, `d3d12_ToneMap`, the bloom, colour grading,
  the HDR output `d3d12_HDROutput`); per pixel lighting (`d3d12_PixelLighting`); the sun's shadow
  cascades (`d3d12_ShadowCascades`, `d3d12_CascadeInstancing`); ambient occlusion (`d3d12_AO`:
  XeGTAO or CACAO); ray tracing (`d3d12_RTReflections`, `XRENDERD3D12_NORT`); the statistics
  (`r_Stats`, `r_DisplayInfo`, the engine's frame profiler, `d3d12_GpuTimes`, `d3d12_FrameLog`,
  `d3d12_CpuSamples`, `d3d12_DumpFrame`);
  the water (`d3d12_WaterDebug`, `r_WaterReflections`); the far vegetation sprites and the
  terrain's detail layers. The game's menu gets the renderer's options from the patches in
  `assets/scripts` (HDR, ambient occlusion, ray traced reflections in Advanced Video Options),
  greyed out with another renderer.
