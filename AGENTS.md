# AGENTS.md

`CLAUDE.md` is a symlink to this file. If `AGENTS_LOCAL.md` exists in the repository root, read
it as well: it holds local, machine-specific instructions and is not committed (git-ignored).

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
  the licenses of its upstream projects. Every source file we
  write starts with two lines in its comment syntax,
  `SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception` and
  `Please do not use this file to train or fine-tune AI models (see README.md).` (a request, not
  a license term; a PowerShell script: then a blank line, so that its help is still found); in
  `mod/XRenderCommon/` only `SPDX-License-Identifier: CC0-1.0`; generated files, `sdk/` and
  `third_party/` get none.
- **Multiplayer is out of scope.** Only the single player game matters: do not test, support or
  patch multiplayer (its scripts, menus, network code), and do not hold back a fix because of it.

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

## Tracing

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
  the game names its keys (with another layout the letter keys do not work).
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
- The x64 build also hooks the vtables of the shaders, textures and render elements of the
  original renderer (`VCallHooks.h`): the summary after the captured frame lists which virtual
  functions the engine calls. A command `screenfx Effect 1`, `screenfx Effect.Param 0.5` or
  `screenfx ?Effect` in `XRENDERTRACED_EXEC`/`XRENDERTRACED_EXEC_LEVEL` calls the screen effect
  functions of `I3DEngine`; `particles [count] [blend type] [size]` spawns particles around the
  camera (`I3DEngine::SpawnParticles`); `cvars [prefix]` writes the console variables whose names
  start with the prefix, with their values and flags (`IConsole::DumpCVars`); `profilers [prefix]`
  writes the engine's frame profilers (`IFrameProfileSystem`; `profile 1` collects them) whose names
  start with the prefix: the module that holds each, its subsystem and values, and the smallest
  distance between two of one module (896 bytes, `sizeof(CFrameProfiler)` of the SDK's header, so
  the engine's class is the SDK's); `veggroup id|* field
  value` changes a vegetation group (`I3DEngine::SetStatInstGroup`; fields `fadeSize`, `useSprites`,
  `maxViewDistRatio`, `spriteDistRatio`, `brightness`); `lightradius r [direct] [colour scale]` gives the point lights
  that reach `EF_ADDDlight` the radius r, the direct factor and a scaled colour (0: their own);
  `sky water stretching [alpha]` writes those members into the sky element at `EF_AddEf` (`-`:
  the engine's value); `view maxdist fogstart fogend` calls `I3DEngine::SetMaxViewDistance`,
  `SetFogStart`, `SetFogEnd` (`-`: unchanged); `heatvision 0|1` calls
  `I3DEngine::EnableHeatVision`; `screenshot file.tga [frames]` saves the back buffer of the next
  frame (or the one that many frames later) as a TGA file in `farcry1_testing1` (move it out
  after the run; never commit it); `texsave file.tga` saves the pixels of the next
  `DownLoadToVideoMemory` or `UpdateTextureInVideoMemory` call with `eTF_0888` or `eTF_8888` (the
  start-up videos, the loading screen) the same way, 4 bytes per pixel as passed; `after frames
  command` runs the command that many frames
  later (counted from when it is read; 32 may wait at once); `decal size texture [x y]` calls `I3DEngine::CreateDecal` where the ray through that point
  of the screen (fractions, the centre by default) meets terrain or a static object, with the owner
  taken as the SDK game code does, and `decal size texture terrain x y` puts one on the terrain at
  world (x, y) (decals need `e_decals 1` before the level loads); `mouse down [right]`, `mouse up
  [right]`, `mouse move dx dy` and `key down|up <letter or digit>` send the left (right) button, a
  relative move or a key (as a scan code) with `SendInput`, only while the game window is in front
  (the run's log says whether it was); `screenshader <shader>|-` calls
  `I3DEngine::SetScreenShader` (`-`: ""), `screenmask <texture>|-` and `blurmask <texture>|-`
  `SetScreenMask` / `SetBlurMask` with that texture (`LoadTexture`) or NULL; `inframe n command`
  runs a console command at the end of each of the next n frames, inside the frame (e.g.
  `inframe 300 #System:DrawRectShader("ScreenDistort", 0, 0, 800, 600, 1, 1, 1, 1)`; the other
  commands run between two frames); `goto x y z [ax ay az]` puts the player (the first entity of class `Player`)
  and the view camera at that point (a scripted camera, as at the start of Archive, overrides
  it); `entities [part of a class name]` lists the entities with class, name and position, to
  find a place for `goto`; `playerevent item n` and `playerevent zoom n` send the player the
  script event the SDK's client sends for an item key (`ScriptEvent_ItemActivated`, e.g. 0 for the
  binoculars) and for the zoom key (`ScriptEvent_ZoomToggle`: 1 pressed, 0 released), without
  input; `capture` writes every call from then on until the renderer
  is released
  (e.g. `capture; quit` in `XRENDERTRACED_EXEC_LEVEL` for the shutdown; in the menu it never ends
  and the log grows by gigabytes), `frame` only the next frame, as `XRENDERTRACED_CAPTURE` does
  (e.g. the start-up videos with `XRENDERTRACED_EXEC_FRAME=20`).
- The screen effect calls (`CREScreenProcess::mfSetParameter`, `mfGetParameter`) are written as
  `[screenfx]` lines (the first 8 of each parameter, then every 50th); `XRENDERTRACED_SCREENFX_ALL=1`
  writes all of them, with the timer's time. `XRENDERTRACED_SCREENFX_WATCH=8.24,8.30` reads those
  parameters (process.param) from the ScreenProcess element after every frame and writes
  `[screenfx watch]` lines with the frame, the time and the frame time when a value changes.
- `XRENDERTRACED_TRACE_METHODS` (names as in the log, separated by `,`, e.g.
  `DownLoadToVideoMemory,CLeafBuffer::CreateBufferFromSource`) writes every call of those methods,
  not only the first `xrt_TraceCalls`; for the hooked virtual functions (e.g. `CCObject::AddWaves`)
  the first 4000 (or `XRENDERTRACED_TRACE_METHODS_MAX`) instead of 3, counted again after the
  command `vcalls` (e.g. in `XRENDERTRACED_EXEC_LEVEL`). A written `CCObject::AddWaves` call also
  writes the two waves the engine filled after the call before it.
- The engine's `PrepareBufferCallback` of a leaf buffer (the detail grass) reaches the original
  renderer wrapped: the calls appear as `PrepareBufferCallback(...)` with the engine's calls inside
  them and the leaf buffer, its video buffer and index stream dumped before and after.
- Whole leaf buffers: `XRENDERTRACED_MESH_FULL=n` writes a mesh of at most n faces whole before
  `CLeafBuffer::CreateBufferFromMesh` (faces, positions, texture coordinates, colours, normals) and
  the buffer after it (vertices, tangents, indices, the strip map).
  `XRENDERTRACED_VERTS=n` writes at least n vertices wherever vertices are dumped (e.g. the
  skinned vertices the engine passes to `UpdateBuffer` in a captured frame), positions with 5
  decimals.
  `XRENDERTRACED_UCL_FULL=n` makes `CLeafBuffer::UpdateCustomLighting` of a buffer of at most n
  vertices (with colours in its vertex format) write every vertex (position, temp normal, loaded
  colour, the colour before and after the call), the buffer with its chunks, its indices and the
  sun colour. `XRENDERTRACED_LB_RAW=1` adds the raw words of the leaf buffer, of its first chunk
  and of that chunk's render element to every leaf buffer dump.
- The engine's `I3DEngine` is hooked as well (from the first `BeginFrame`): the calls the original
  renderer makes into it are written as `[vcall] I3DEngine::...` lines (with the camera of
  `SetCamera` and the flags of `DrawLowDetail`) and counted in the renderer column of the virtual
  call summary; `XRENDERTRACED_TRACE_METHODS` takes them as e.g. `I3DEngine::DrawLowDetail`.
- The console (`IConsole`) is hooked before the original renderer is constructed: every call the
  renderer makes into it (the variables it registers) is a `[console]` line with its arguments,
  and the first `GetCVar` of each name by each module (the engine's modules too) a
  `[console] GetCVar(...)` line with the variable found; the summary counts the lookups.
- The engine's services are hooked the same way (`ISystem`, `ILog`, `ITimer`, `ICryPak`,
  `IStreamEngine`, `IPhysicalWorld`, `ICryFont`): the calls the renderer makes into them are
  `[engine] ICryPak::FOpen (slot 13) ...` lines with their arguments (as text where they point to
  one) and counted in the summary; e.g. `XRENDERTRACED_TRACE_METHODS=ICryPak::FOpen` with
  `XRENDERTRACED_TRACE_METHODS_MAX=30000` writes every file the renderer opens. A traced
  `GetAnimTexInfoFromId` or `RemoveAnimatedTexture` writes the `AnimTexInfo` (name, frames,
  references, texture IDs). The engine's materials (`IMatInfo`) handed to `CLeafBuffer::Render`
  and `AddRenderElements` are hooked too (`IMatInfo::GetSubMtl` lines, e.g. with
  `XRENDERTRACED_TRACE_METHODS=IMatInfo::GetSubMtl`; the counter starts again at the `vcalls`
  command), and the dump under a `Render` call with a material lists its sub materials and the
  buffer's chunks with their `cgfId`. A material pointer without a valid vtable is not called but
  written as raw bytes.
- `XRENDERTRACED_FEATURES_CLEAR` and `XRENDERTRACED_FEATURES_SET` (hexadecimal `RFT_*` masks)
  change the value of `GetFeatures` before the engine gets it, to see what the engine does with
  each feature; the traced call writes the caller and both values.
- After the captured frame and at `Release` the log lists how often each traced method was called
  from each module (`==== callers`); the command `callers` writes the list at any time. A call
  that an engine function makes as its last step is counted at the module that called that
  function.
- A call into the renderer from another thread than the one that loaded it (an IRenderer or
  CLeafBuffer method, a hooked virtual function, or a call of the renderer into the engine) writes
  a `[thread]` line, once per method.
- `XRENDERTRACED_D3D_STATE` (names as in the log, e.g. `DrawDynVB,Draw2dImage`) writes the
  Direct3D 9 state of the original renderer after those calls (`D3DState.h`; read only, through
  `EF_Query(EFQ_D3DDevice)`). `XRENDERTRACED_PROBE=FontSetBlending` passes (i, 12 - i) in the first
  13 calls of `FontSetBlending` instead of the engine's arguments;
  `XRENDERTRACED_PROBE=ProjectorFrustum` gives a projected light (`DLF_PROJECT`) the frustum angles
  15, 30, ..., 90 in turn for each `EF_UpdateDLight` (restored after the call) and writes the
  texture matrix the renderer makes from it.
  `XRENDERTRACED_TEMPLATE=TemplDecalModulate` gives every material that names a template
  (`Templ...`) that template instead, to see how the renderer draws templates that are not in
  view; `XRENDERTRACED_TEMPLATE_ONLY=name,name` limits it to the materials of those names (as
  passed to `EF_LoadShaderItem`) or with those templates, and then also replaces templates that
  are not `Templ...` (the level's material library loads every material as `MaterialContainer`,
  so e.g. `XRENDERTRACED_TEMPLATE_ONLY=WaterVolume` picks its water materials);
  `XRENDERTRACED_TEMPLATE_OPACITY=0.5` then gives them that opacity too.
  `XRENDERTRACED_TEXTURE=<name>` gives every material with a diffuse texture this texture instead
  (`XRENDERTRACED_TEXTURE_ONLY=name,name` only the materials of those names), e.g. an animated
  one made of test pictures, to see in view how the renderer uses it. The original renderer cuts
  the folder part of an animated texture name at 64 characters, so keep such test pictures in a
  short folder under `farcry1_testing1` (and delete them after the run).
  `XRENDERTRACED_SKIP_SPRITES=particlelight_fp,...` holds back the `EF_AddSpriteToScene` calls
  of these shaders, to tell which Direct3D draw belongs to which sprite.
  `XRENDERTRACED_PROBE=FlareInView` moves the object of every flare (`EF_AddEf` with a Flare
  element) onto the camera's line of sight at its own distance or `XRENDERTRACED_FLARE_DIST`, so
  that the renderer tests and draws it; `XRENDERTRACED_FLARE_DIR="dx dy"` turns that line by dx
  times the view's right and dy times its up vector (e.g. `0.6 0` towards the right edge of the
  screen). `XRENDERTRACED_PROBE=CoronaInView` moves every light with a corona
  (`m_fCoronaScale > 0`) in `EF_ADDDlight` onto that line, `XRENDERTRACED_CORONA_DIST` (5) in front
  of the camera, and `XRENDERTRACED_CORONA_SCALE` replaces its `m_fCoronaScale`. These three take
  one number or several (`"0.75 1.5 3"`), used in turn, one per frame.
  `XRENDERTRACED_PROBE=UserTemplParams` gives every object added with a user template
  (`EF_AddEf` with `nTempl` >= 30) the proxy's own `m_ShaderParams`: one float
  `XRENDERTRACED_PARAM_NAME` (default `refraction`) = `XRENDERTRACED_PARAM_VALUE` (one number per
  frame in turn), or none for a negative value.
  `XRENDERTRACED_PROBE=Nearest` changes the elements added with sort `eS_Nearest` or an object
  with `FOB_NEAREST` (the first person weapon): `XRENDERTRACED_NEAREST_SORT` replaces the sort and
  `XRENDERTRACED_NEAREST_FLAG=0` clears `FOB_NEAREST`.
  `XRENDERTRACED_PROBE=LightAt` puts every point light (not the sun) at a world position of
  `XRENDERTRACED_LIGHT_POS` (`"x y z x y z ..."`, one position per frame in turn), to see how the
  renderer lights an object from known places (the console command `lightradius` sets the radius).
  For the projected lights (`DLF_PROJECT`), `XRENDERTRACED_PROJ_ANGLES` (`"x y z"`, one group per
  frame in turn) and `XRENDERTRACED_PROJ_POS` replace `m_ProjAngles` and `m_Origin` at
  `EF_UpdateDLight`, and `XRENDERTRACED_PROJ_IMAGE` (a picture file, an absolute path works)
  replaces `m_pLightImage` with that picture loaded as the engine loads light images, to see which
  way up the picture is cast (no `XRENDERTRACED_PROBE` needed).
  `XRENDERTRACED_FLARES=1` writes the object of the flare as the engine passes it and again after
  `EF_EndEf3D` as `[flare]` lines (colour, `m_AmbColor`, `m_TempVars`, `m_Trans2`, `m_Angs2`,
  `m_RE` and the distance to the camera; up to 40000 lines), for the state the renderer keeps
  in it, and in the same way (`[flare corona]`) the object the renderer keeps for a light's corona
  (`CDLight::m_pObject`, written in the light's dump as well). Flares and coronas are off in
  farcry1_testing1's settings: put `r_Coronas 1` in front of `map` in `XRENDERTRACED_EXEC`.
  The render elements a shader holds (`GetREs`, e.g. the flares) are hooked when they reach
  `EF_AddEf`, so their draws carry the element in the Direct3D draw log.
- `XRENDERTRACED_D3D_DRAWS=1` writes the Direct3D 9 draw calls of the original renderer in the
  captured frame (`D3DDraws.h`; the header line gives `ITimer::GetCurrTime()`, for animated
  values): buffers, vertex declaration, shaders, textures and the render
  element being drawn (x64; for a leaf buffer the shader and template of its chunk, while an
  object rendered with `SRendParams::pMaterial` is drawn with that material, whose shader,
  template and resources the `IMatInfo` dump under its `CLeafBuffer::Render` line gives), the
  first vertices of the non-indexed draws (8, or
  `XRENDERTRACED_D3D_VERTS`), the `Clear` calls, the render target and depth buffer changes
  (`SetRenderTarget`, `SetDepthStencilSurface`), the surface copies and fills (`StretchRect`,
  `UpdateSurface`, `ColorFill`), the read backs (`GetRenderTargetData`, `GetFrontBufferData`,
  `LockRect` of the depth buffer or the back buffer), `GetDepthStencilSurface`, `CreateQuery`
  and `Present`. The dynamic vertex buffers seen in a captured frame are watched from the start
  of the next captured frame, so the vertices of its first draws are known too (capture two
  frames in a row, e.g. `after 1000 frame; after 1001 frame`). `2` adds the device state after
  each (with
  the stencil state when stencil is on and the scissor rectangle when the scissor test is on) and
  the box of the positions an indexed draw uses (when
  its vertex buffer can be read back), `3` also the shader constants. The `CCObject` dumps give
  its scissor rectangle, and the camera dump of `SetCamera` the camera's portal and scissor
  rectangles when they are set.
  `XRENDERTRACED_D3D_CUBESAVE=<path prefix>` saves level 0 of the six faces of every cube texture
  a draw uses as TGA files (DXT decoded; keep them in the scratchpad, never commit them).
  `XRENDERTRACED_D3D_TEXSAVE=<path prefix>` does the same for level 0 of every managed 2D texture
  a draw uses that is at most `XRENDERTRACED_D3D_TEXSAVE_MAX` (default 256) texels wide and high.
  `XRENDERTRACED_D3D_RT=1` writes the first `SetRenderTarget` of every frame to a surface other
  than the back buffer (to find the frames that render shadow maps; 64 lines, counted again after
  each level load); `XRENDERTRACED_CAPTURE_RT=100` captures the rest of the first frame that sets
  such a render target at least 100 frames after each level load (every call, and the draws with
  `XRENDERTRACED_D3D_DRAWS`), for passes into textures at irregular frames (water reflections);
  `XRENDERTRACED_D3D_VSREG=7` writes vertex shader constant c7 at every
  indexed draw where it changed, with the frame and the time (60000 lines, counted again after
  each level load); `XRENDERTRACED_CAPTURE=1`
  captures the first frame after the load.
  `XRENDERTRACED_CAPTURE_SHADER=s_m_fa,templmuzzleflash_auto_fp` (names of shaders or of their
  templates) captures the rest of the first frame in which an object with one of them is handed
  over (`EF_AddEf`, `CLeafBuffer::Render` / `AddRenderElements`), `XRENDERTRACED_CAPTURE_SHADER_MAX`
  times (default 1). Templates are shared: `templmuzzleflash` is also the flares' template.
  `XRENDERTRACED_DISPLAY=1` writes a line `[display] frame N: ...` (device, back buffer, window
  client area and DPI, `GetWidth` / `GetHeight`) whenever one of them changes, e.g. after
  `r_Width 1280`.
  A device reset after a frame captured with `XRENDERTRACED_D3D_DRAWS` fails (the tracer keeps
  references to the dynamic vertex buffers), so do not capture between resolution changes.
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
  `info = 1` the demo's log gets the frame rates of each run from `Levels/<level>/<level>.log`
  (`--until 100000` keeps the run going to the recording's end after the last frame to save;
  with XRenderD3D12 a run of Training is at about 260 FPS or at about 140, where some frames
  take 20 ms and more while the renderer waits for nothing; which one repeats for one renderer DLL
  with the same arguments and environment and changes with any of them, for a build before a
  change as for the one after it, so frame rates are compared over several runs and launches;
  with `profile 1` too, at about 225 or about 130);
  a path through the whole level to compare a change in many places) and DemoSurfaces (objects
  of different materials, glass among them, on a beach and in its shallow water under a moving
  light, handed to the engine's
  scene through `I3DEngine::SetRenderCallback`: the engine calls it for the main view only, not
  for the water's reflection).
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
    (`HdrDecode` in `Shaders/Common.hlsli`): the game adds its colours in gamma space, and what it
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
  drawn slice by slice, each compared with the full set's.

  Lit objects take the sun from the full set. Light maps and the terrain, whose light has the
  static world's shadows already, take it from the live set; the terrain is darkened to the
  colour of the engine's shadows on it (`EntityShadowOnTerrain`, which fade out where the
  cascades reach). Beyond the cascades the objects' shadow maps work as before.
  `d3d12_ShadowSoftness` (0.05 m) is the filter's radius (at least a texel and a half).
  `d3d12_ShadowDebug` 1 also shows the slices, 3 colours the cascades, 4 shows the full set's
  shadow in red and the live set's in green. With `d3d12_ShadowCascades 0` (from the start,
  `--exec-at 0`) or `e_shadow_maps 0` the frames are those of the renderer without them
  (DemoCompare difference 0 for the former). The cascades' code is in pixel shaders of its own
  (`XR_CASCADES`, for the draws with their flags): in the general one, with dxc's code for the old
  paths unchanged, the GPU's compiler still changed a few pixels by a level.
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
  resolve makes the light taken, `(1 - visibility ^ strength) * ambient` (`R16G16B16A16_FLOAT`,
  the strength not divided by 2.2: the game adds its light in gamma space), and a blend of `ONE,
  ONE` with `REV_SUBTRACT` (`SPipelineKey::nBlendOp`) takes it from the scene. A light map's light
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
- Ray tracing (`RayTracing.h`, DXR 1.1 with inline `RayQuery`, Shader Model 6.5): the main view's
  opaque draws and the static ones of the water's planar reflection become acceleration
  structures every frame (the `ray scene` section of the GPU times, a `ray scene:` line in the
  frame dump), once a frame and only while something wants rays (else their memory goes).
  `XRENDERD3D12_NORT=1` (or a device without tier 1.1) turns them off; the log says why.
  `Shaders/RayShade.hlsli` traces (`TraceScene`, alpha tested triangles tested) and shades the
  hits as the general program does without the draw's lights:
  textures, vertex colour, light maps (dot3 ones too), the sun with its shadow by a ray
  (`d3d12_RTShadows`, default 1). `d3d12_RTReflections` (0 by default, read every frame): 1 the
  water's reflection by rays in place of the screen space reflection, 2 hybrid (the screen space
  reflection where it is sure, the rays elsewhere); not registered without rays.
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
- The engine's statistics: `GetPolyCount` gives the triangles drawn in the frame the last `Update`
  ended (every draw into every target; no shadow volumes), which the game asks for after the frame
  is shown: the time demo's `Tri/Frame`, `Tri/Sec` and ratio to the recording in
  `Levels/<level>/<level>.log`, and the `Polygons` and `P/Sec` of the engine's `r_DisplayInfo` block
  (the engine draws that block at the top right itself).
  The renderer's work is in the engine's frame profiler under the subsystem Renderer
  (`FUNCTION_PROFILER(iSystem, PROFILE_RENDERER)`, `FrameProfiler.h`; `profile 1` shows it, the
  tracer's `profilers CD3D12` writes it): `CD3D12Renderer::BeginFrame` with
  `CD3D12Device::BeginFrame:WaitForGpu` (the CPU waiting for the GPU), the passes inside the 3D
  engine's own `Renderer::EF_EndEf3D` (`UpdateSunCascades`, `UpdateWaterReflection`,
  `DrawItems:Opaque`, `:Nearest`, `:Blended`, `BuildRayScene`, `DrawScreenEffects`, ...),
  `CD3D12Renderer::Update` with `CD3D12Device::EndFrame:Present`, `FlushFrame`, and the loading
  (`EF_LoadTexture`, `DownLoadToVideoMemory`, `EF_LoadShaderItem`, `CD3D12Pipeline::CreatePSO`, ...).
  They are CPU times; the GPU's are `d3d12_GpuTimes` (a section `flush` is the GPU waiting for
  the CPU after a `FlushFrame`, e.g. a demo reading every frame for its clip).
- `r_Stats` shows the renderer's statistics at the top left, below the time demo's lines, averaged
  over half a second (`Stats.cpp`): `1` the GPU time of each pass in the order they run (the
  sections of `d3d12_GpuTimes`, with a bar of each one's share of the GPU's frame), the time
  between frames, the CPU's wait for the GPU at the start of a frame and in `Present`, the draws,
  how many of them changed the pipeline state, and the triangles; `2` (and more) also the memory:
  the process' video memory and its budget (DXGI), the textures and buffers, the static
  geometry's acceleration structures, the frame memory and the pipeline states made. A frame read
  before `Update` (a demo's frames and clips) has them too. It turns the GPU's timestamps on, as
  `d3d12_GpuTimes` does; both get the same times.
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
  blurred one where the blur is wider than 4 to 8 pixels. `d3d12_WaterReflectionBlur` scales that
  variance (0.1; 0.25 matches the halved slopes of the calmer normal the reflection is looked up
  along, but already washes the reflections of the bay in PierWalk out; 0 gives exactly the frames
  of the mirror before it; not saved).
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
