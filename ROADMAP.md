# Roadmap

What is planned but not done yet.

## XRenderD3D12

- **More upscalers.** `d3d12_Upscaler` picks the upscaler; `fsr` (AMD FSR Upscaling) is the first.
  The same render-size scene, depth, motion vectors and jitter are to feed:
  - `xess` - Intel XeSS (XeSS SDK, `libxess.dll`);
  - `dlss` - NVIDIA DLSS Super Resolution and DLAA (DLSS SDK or Streamline).

  Each is a closed library: it needs its own permission in `LICENSE-EXCEPTION.md` (as the AMD FSR
  SDK got) and its own folder in `third_party/`.
- **Reactive and transparency masks for FSR.** FSR 4 does well without them, but its FSR 3.1
  fallback (non-AMD and older AMD GPUs) ghosts particles and other blended effects without them.
- **The upscaler in the game's options menu.** Today it is set from the console; an entry in the
  video options would be a patch in `assets/scripts`.
