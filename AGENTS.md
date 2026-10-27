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
  - `Bin32` - retail 1.4 (x86; does not start on this machine),
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
  `SetFogStart`, `SetFogEnd` (`-`: unchanged).
- `XRENDERTRACED_TRACE_METHODS` (names as in the log, separated by `,`, e.g.
  `DownLoadToVideoMemory,CLeafBuffer::CreateBufferFromSource`) writes every call of those methods,
  not only the first `xrt_TraceCalls`.
- `XRENDERTRACED_D3D_STATE` (names as in the log, e.g. `DrawDynVB,Draw2dImage`) writes the
  Direct3D 9 state of the original renderer after those calls (`D3DState.h`; read only, through
  `EF_Query(EFQ_D3DDevice)`). `XRENDERTRACED_PROBE=FontSetBlending` passes (i, 12 - i) in the first
  13 calls of `FontSetBlending` instead of the engine's arguments.
- `XRENDERTRACED_D3D_DRAWS=1` writes the Direct3D 9 draw calls of the original renderer in the
  captured frame (`D3DDraws.h`): buffers, vertex declaration, shaders, textures and the render
  element being drawn (x64), the first vertices of the non-indexed draws (8, or
  `XRENDERTRACED_D3D_VERTS`), and the `Clear` calls; `2` adds the device state after each (with
  the stencil state when stencil is on), `3` also the shader constants.
  `XRENDERTRACED_D3D_RT=1` writes the first `SetRenderTarget` of every frame to a surface other
  than the back buffer (to find the frames that render shadow maps); `XRENDERTRACED_CAPTURE=1`
  captures the first frame after the load.
- The log is `farcry1_testing1/XRenderD3D9Traced.log`; the engine log is
  `farcry1_testing1/log.txt`.
