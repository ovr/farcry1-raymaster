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
- The log is `farcry1_testing1/XRenderD3D9Traced.log`; the engine log is
  `farcry1_testing1/log.txt`.
