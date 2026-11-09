// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: RayBehind.h
//	Description: What is behind, above and below the camera in the scene
//	for rays (UpdateRayBehind, d3d12_PTBehind): why a frame gathers it.
//	The view's own draws and the cascades' parts (what is towards the sun)
//	leave out what is behind the camera and over and under the view; the
//	3D engine's cameras looking back, up and down give it, for:
//	  - the path tracer: its paths go every way;
//	  - the volumetric light's sun by rays where the frame has no sun: what
//	    keeps it out of a room;
//	  - the lamps' shadows by rays (d3d12_RTLightBehind) while a draw traced
//	    rays towards its lamps the frame before: what stands between a lamp
//	    and the view's surfaces may be out of the view. In Training's
//	    opening cut scene the ceiling over Jack (8 m up) shades the lamp up
//	    the shaft (17 m up) from him; it came into the view as the camera
//	    tilted at frame 1067, and the lamp's light on him went out at once
//	    (the mean luma 60.0 -> 36.0) where the original's frame stays even.
//	Pure, for the unit tests.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_RAYBEHIND_H
#define XRENDERD3D12_RAYBEHIND_H

namespace RayBehind
{
enum : unsigned
{
  PATHS = 1,
  VOLUMETRIC = 2,
  LAMPS = 4
};

// bPaths the path tracer; bVolumetricRays the volumetric light's sun by rays, bSun the frame has a sun;
// bLampRays a draw traced rays towards its lamps the frame before, bLampBehind d3d12_RTLightBehind.
inline unsigned Reasons(bool bPaths, bool bVolumetricRays, bool bSun, bool bLampRays, bool bLampBehind)
{
  return (bPaths ? PATHS : 0u) | (bVolumetricRays && !bSun ? VOLUMETRIC : 0u) | (bLampRays && bLampBehind ? LAMPS : 0u);
}
} // namespace RayBehind

#endif
