# farcry1-raymaster

Mods for Far Cry, built with the CryENGINE MOD SDK for Far Cry:

- `mod/XRenderD3D12` - a Direct3D 12 renderer for the x64 game. It can draw the 3D scene with
  AMD FSR Upscaling, set from the console:
  - `d3d12_Upscaler fsr` turns it on (`none`, the default, turns it off).
  - `d3d12_UpscaleMode` picks the mode: 0 Native AA (anti-aliasing at the display's resolution),
    1 Quality, 2 Balanced, 3 Performance or 4 Ultra Performance.
  - `d3d12_UpscaleSharpness` sets the sharpening, 0 to 1.
- `mod/XRenderD3D9Traced` - a renderer proxy that traces the calls between the engine and its
  renderer.
- `mod/XRenderCommon` - what the renderers share about the renderer interface beyond the SDK
  headers.
- `mod/Demos` - demo scenes that run inside the game, and their runner.
- `third_party/amd-fsr-sdk` - the parts of the AMD FSR SDK that XRenderD3D12 uses for AMD FSR
  Upscaling.
- `assets/scripts` - patches to the game's scripts, and `Patch Scripts.cmd`, which applies them to
  the scripts of an installed game (`Patch Scripts.cmd <game folder>`, written to
  `FCData\zz_RayMaster.pak`; deleting that file gives back the original scripts):
  - `field-of-view.patch` - a field of view for screens wider than 4:3 that keeps the view up and
    down of a 4:3 screen (Hor+), and the console variable `cl_fov`, the field of view across the
    screen in degrees (0, the default, for the automatic one).
  - `hdr-options.patch` - HDR entries in Advanced Video Options, where the game hides its own:
    HDR, None, Soft, ACES or AgX, which sets `r_HDRRendering` and `d3d12_ToneMap`, the HDR
    pipeline of XRenderD3D12 and its tone map; and HDR output, SDR, HDR10 or scRGB, which sets
    `d3d12_HDROutput`: on a display in HDR mode (Windows: Use HDR) the sun, fire and lamps go up to
    the display's peak brightness, while the rest of the picture and the menu stay as bright as SDR
    content.

How to build and run them: `AGENTS.md`.

These mods are the work of their authors, not of Crytek or Ubisoft, and are not endorsed by them.
Far Cry and CryENGINE are trademarks of their owners.

## License

Everything in this repository outside `sdk/`, `third_party/` and `mod/XRenderCommon/` is free
software under the GNU General Public License, version 3 or (at your option) any later version
(`LICENSE`), with an additional permission to combine it with the game Far Cry, the CryENGINE MOD
SDK for Far Cry and the AMD FSR SDK (`LICENSE-EXCEPTION.md`).

`third_party/amd-fsr-sdk/` holds parts of the AMD FSR SDK by Advanced Micro Devices, Inc., under
AMD's MIT license (`third_party/amd-fsr-sdk/docs/license.md`); the build downloads its upscaler
DLL (`third_party/amd-fsr-sdk/README.md`).

`mod/XRenderCommon/` is dedicated to the public domain (CC0 1.0, `mod/XRenderCommon/COPYING`): its
authors claim no rights to it, and any renderer for the game may use it, under any license.

`sdk/` is the CryENGINE MOD SDK for Far Cry by Crytek, under its own license (`sdk/EULA.txt`), not
under the GPL. That license also sets terms for distributing mods made with the SDK (free of
charge, by electronic means only, with the license attached); read it before you distribute a
build.

## AI training

Please do not use the code or the documentation of this repository to train or fine-tune machine
learning models, or put them into datasets for that.
