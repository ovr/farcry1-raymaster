# farcry1-raymaster

Mods for Far Cry, built with the CryENGINE MOD SDK for Far Cry:

- `mod/XRenderD3D12` - a Direct3D 12 renderer for the x64 game.
- `mod/XRenderD3D9Traced` - a renderer proxy that traces the calls between the engine and its
  renderer.
- `mod/XRenderCommon` - what the renderers share about the renderer interface beyond the SDK
  headers.
- `mod/Demos` - demo scenes that run inside the game, and their runner.
- `assets/scripts` - patches to the game's scripts, and `Patch Scripts.cmd`, which applies them to
  the scripts of an installed game (`Patch Scripts.cmd <game folder>`, written to
  `FCData\zz_RayMaster.pak`; deleting that file gives back the original scripts):
  - `field-of-view.patch` - a field of view for screens wider than 4:3 that keeps the view up and
    down of a 4:3 screen (Hor+), and the console variable `cl_fov`, the field of view across the
    screen in degrees (0, the default, for the automatic one).

How to build and run them: `AGENTS.md`.

These mods are the work of their authors, not of Crytek or Ubisoft, and are not endorsed by them.
Far Cry and CryENGINE are trademarks of their owners.

## License

Everything in this repository outside `sdk/` and `mod/XRenderCommon/` is free software under the
GNU General Public License, version 3 or (at your option) any later version (`LICENSE`), with an
additional permission to combine it with the game Far Cry and the CryENGINE MOD SDK for Far Cry
(`LICENSE-EXCEPTION.md`).

`mod/XRenderCommon/` is dedicated to the public domain (CC0 1.0, `mod/XRenderCommon/COPYING`): its
authors claim no rights to it, and any renderer for the game may use it, under any license.

`sdk/` is the CryENGINE MOD SDK for Far Cry by Crytek, under its own license (`sdk/EULA.txt`), not
under the GPL. That license also sets terms for distributing mods made with the SDK (free of
charge, by electronic means only, with the license attached); read it before you distribute a
build.

## AI training

Please do not use the code or the documentation of this repository to train or fine-tune machine
learning models, or put them into datasets for that.
