// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Smaa.hlsli
//	Description: The first two passes of SMAA 1x (d3d12_AA smaa,
//	PostAA.cpp): SMAA.hlsl of third_party/smaa as it is, with the
//	renderer's samplers, run by compute kernels (AntiAliasSmaa.hlsl):
//	  - SmaaEdges: SMAA's luma edge detection of a pixel of the picture
//	    (gamma encoded, as the back buffer holds it): its left and top
//	    edges, 0 or 1 each;
//	  - SmaaWeights: SMAA's blending weights of a pixel from the edges,
//	    the area and the search textures (Textures/AreaTex.h,
//	    Textures/SearchTex.h), the edges read through the bilinear
//	    sampler as SMAA reads them (two at a time).
//	The third, the neighbourhood blending, is SmaaBlend.hlsli's.
//	A compute kernel has no implicit derivatives: SMAA's Sample is
//	SampleLevel 0 (its textures have one level). Its edge detection
//	discards a pixel without edges, which a kernel cannot: there it
//	returns no edges (0, 0), what the cleared target of a pixel shader
//	would keep.
//	The includer defines, before it includes this:
//	  SMAA_RT_METRICS        float4(1 / width, 1 / height, width, height)
//	  SMAA_LINEAR_SAMPLER    a linear clamp sampler
//	  SMAA_POINT_SAMPLER     a point clamp sampler
//	  SMAA_PRESET_LOW, _MEDIUM, _HIGH or _ULTRA (d3d12_AAQuality 0 to 3)
//	The coordinates are those of the pixel's centre, (p + 0.5) / size.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_SMAA_HLSLI
#define XRENDERD3D12_SMAA_HLSLI

#define SMAA_CUSTOM_SL 1
#define SMAATexture2D(tex) Texture2D tex
#define SMAATexturePass2D(tex) tex
#define SMAASampleLevelZero(tex, coord) tex.SampleLevel(SMAA_LINEAR_SAMPLER, coord, 0)
#define SMAASampleLevelZeroPoint(tex, coord) tex.SampleLevel(SMAA_POINT_SAMPLER, coord, 0)
#define SMAASampleLevelZeroOffset(tex, coord, offset) tex.SampleLevel(SMAA_LINEAR_SAMPLER, coord, 0, offset)
#define SMAASample(tex, coord) tex.SampleLevel(SMAA_LINEAR_SAMPLER, coord, 0)
#define SMAASamplePoint(tex, coord) tex.SampleLevel(SMAA_POINT_SAMPLER, coord, 0)
#define SMAASampleOffset(tex, coord, offset) tex.SampleLevel(SMAA_LINEAR_SAMPLER, coord, 0, offset)
#define SMAA_FLATTEN [flatten]
#define SMAA_BRANCH [branch]
#define SMAAGather(tex, coord) tex.Gather(SMAA_LINEAR_SAMPLER, coord, 0)

// The edge detection's discard (the functions return the edges, float2) as no edges.
#define discard return float2(0.0, 0.0)
#include "SMAA.hlsl"
#undef discard

//! SMAA's edges of the pixel at vTexcoord of the picture tColor: x its left edge, y its top edge (0 or 1).
float2 SmaaEdges(float2 vTexcoord, Texture2D tColor)
{
  float4 vOffset[3];
  SMAAEdgeDetectionVS(vTexcoord, vOffset);
  return SMAALumaEdgeDetectionPS(vTexcoord, vOffset, tColor);
}

//! SMAA's blending weights of the pixel at vTexcoord from the edges tEdges (SmaaEdges, R8G8_UNORM), the area texture
//! tArea and the search texture tSearch: what SmaaBlend.hlsli reads (R8G8B8A8_UNORM).
float4 SmaaWeights(float2 vTexcoord, Texture2D tEdges, Texture2D tArea, Texture2D tSearch)
{
  float2 vPixcoord;
  float4 vOffset[3];
  SMAABlendingWeightCalculationVS(vTexcoord, vPixcoord, vOffset);
  // SMAA 1x: no subsamples.
  return SMAABlendingWeightCalculationPS(vTexcoord, vPixcoord, vOffset, tEdges, tArea, tSearch, float4(0, 0, 0, 0));
}

#endif
