// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: SkyFogBand.hlsli
//	Description: The fog over the lower part of the sky box
//	(d3d12_SkyFogBand), as SkyFogBand.h has it on the CPU (where
//	Draw3D.cpp's DrawSkyItem draws it): for the sky the water reflects or
//	sees through its surface (Water.hlsl's SkyBoxColor callers). The ring
//	of 12 segments taken as round.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_SKYFOGBAND_HLSLI
#define XRENDERD3D12_SKYFOGBAND_HLSLI

//! The z of the band's edge on its ring of radius 32 about a camera at height fCameraZ, the frame's fog ending at fFogEnd
//! (SkyFogBand.h's Edge); no band (-1e9) with the fog ending from 256 m on.
float SkyFogBandEdge(float fFogEnd, float fCameraZ)
{
  return fFogEnd < 256 ? (160 - max(fFogEnd, 64)) / 6 - max(fCameraZ, 0) / 35 : -1e9;
}

//! How much of the band, its edge at fEdge, lies over the sky box in the direction vDir: 1 from 32 below the edge up to
//! it, falling evenly to 0 at 16 above it (on the ring of radius 32: the height vDir reaches there), 0 straight up or
//! down.
float SkyFogBandAlpha(float3 vDir, float fEdge)
{
  const float r = length(vDir.xy);
  if (r < 1e-6)
    return 0;
  const float z = 32 * vDir.z / r;
  return z < fEdge - 32 ? 0 : saturate((fEdge + 16 - z) / 16);
}

#endif
