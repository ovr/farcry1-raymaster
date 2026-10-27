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
  runtime behaviour of that build and is marked as such in `mod/XRenderCommon`.

## Layout

- `sdk/` - the Crytek Far Cry 1.4 SDK (CryCommon headers, CryGame sources, tools).
- `mod/` - our own modules (e.g. `mod/XRenderD3D9Traced`, the tracing renderer proxy, and
  `mod/XRenderCommon`, renderer interface knowledge beyond the SDK headers). New libraries go
  here, not into `sdk/`.
- `farcry1_testing1/` - the game install for tracing (git-ignored):
  - `Bin32` - retail 1.4 (x86; stops at its CD check, which is not to be bypassed),
  - `Bin64` - the Cumulative Patch x64 build described above.

## Tracing

- Build: the x86 preset deploys `XRenderD3D8.dll` to `farcry1_testing1/Bin32`, the x64 preset to
  `farcry1_testing1/Bin64` (`XRENDER_OUTPUT_DIR`).
- `farcry1_testing1/system.cfg` selects the proxy with `r_Driver = "Direct3D8"` and runs windowed
  (`r_Fullscreen = "0"`); the original is `farcry1_testing1/system.cfg.xrt-backup`.
- Run `farcry1_testing1/Bin64/FarCry.exe` with `farcry1_testing1` as the working directory.
  `XRENDERTRACED_EXEC="map training"` (and `XRENDERTRACED_EXEC_FRAME`) loads a level without
  input; `XRENDERTRACED_CAPTURE=300` writes every call of the 300th frame after the level load.
  `XRENDERTRACED_WATCH=1` (with `XRENDERTRACED_WATCH_MAX`, default 64) records which members of
  the shared structures the engine and the renderer write (`FieldWatch.h`); the summary is
  written after the captured frame. `XRENDERTRACED_EXEC_LEVEL` runs commands
  `XRENDERTRACED_EXEC_LEVEL_FRAME` frames (default 100) after each level load. Settings that are
  off in `system.cfg` (e.g. `e_shadow_maps 1; e_stencil_shadows 1`) go in front of the `map`
  command in `XRENDERTRACED_EXEC`; the game is stopped by killing it, so `system.cfg` stays as it is.
- The x64 build also hooks the vtables of the shaders, textures and render elements of the
  original renderer (`VCallHooks.h`): the summary after the captured frame lists which virtual
  functions the engine calls. A command `screenfx Effect 1`, `screenfx Effect.Param 0.5` or
  `screenfx ?Effect` in `XRENDERTRACED_EXEC`/`XRENDERTRACED_EXEC_LEVEL` calls the screen effect
  functions of `I3DEngine`; `particles [count] [blend type] [size]` spawns particles around the
  camera (`I3DEngine::SpawnParticles`); `cvars [prefix]` writes the console variables whose names
  start with the prefix, with their values and flags (`IConsole::DumpCVars`); `veggroup id|* field
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
  world (x, y) (decals need `e_decals 1` before the level loads); `mouse down`, `mouse up` and
  `mouse move dx dy` send the left button or a relative move with `SendInput`, only while the game
  window is in front; `goto x y z [ax ay az]` puts the player (the first entity of class `Player`)
  and the view camera at that point (a scripted camera, as at the start of Archive, overrides
  it); `entities [part of a class name]` lists the entities with class, name and position, to
  find a place for `goto`; `capture` writes every call from then on until the renderer
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
  references, texture IDs).
- `XRENDERTRACED_FEATURES_CLEAR` and `XRENDERTRACED_FEATURES_SET` (hexadecimal `RFT_*` masks)
  change the value of `GetFeatures` before the engine gets it, to see what the engine does with
  each feature; the traced call writes the caller and both values.
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
  passed to `EF_LoadShaderItem`), and `XRENDERTRACED_TEMPLATE_OPACITY=0.5` then gives them that
  opacity too.
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
  element being drawn (x64), the first vertices of the non-indexed draws (8, or
  `XRENDERTRACED_D3D_VERTS`), the `Clear` calls, the render target and depth buffer changes
  (`SetRenderTarget`, `SetDepthStencilSurface`), the surface copies and fills (`StretchRect`,
  `UpdateSurface`, `ColorFill`), the read backs (`GetRenderTargetData`, `GetFrontBufferData`,
  `LockRect` of the depth buffer or the back buffer), `GetDepthStencilSurface`, `CreateQuery`
  and `Present`. The dynamic vertex buffers seen in a captured frame are watched from the start
  of the next captured frame, so the vertices of its first draws are known too (capture two
  frames in a row, e.g. `after 1000 frame; after 1001 frame`). `2` adds the device state after
  each (with
  the stencil state when stencil is on) and the box of the positions an indexed draw uses (when
  its vertex buffer can be read back), `3` also the shader constants.
  `XRENDERTRACED_D3D_CUBESAVE=<path prefix>` saves level 0 of the six faces of every cube texture
  a draw uses as TGA files (DXT decoded; keep them in the scratchpad, never commit them).
  `XRENDERTRACED_D3D_TEXSAVE=<path prefix>` does the same for level 0 of every managed 2D texture
  a draw uses that is at most `XRENDERTRACED_D3D_TEXSAVE_MAX` (default 64) texels wide and high.
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
  client area, `GetWidth` / `GetHeight`) whenever one of them changes, e.g. after `r_Width 1280`.
  A device reset after a frame captured with `XRENDERTRACED_D3D_DRAWS` fails (the tracer keeps
  references to the dynamic vertex buffers), so do not capture between resolution changes.
- The log is `farcry1_testing1/XRenderD3D9Traced.log`; the engine log is
  `farcry1_testing1/log.txt`.
