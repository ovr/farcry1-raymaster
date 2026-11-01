// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: RaySunFfx.hlsl
//	Description: The sun's shadow by rays denoised by AMD FidelityFX
//	Denoiser's shadows denoiser (d3d12_RTSunDenoise 2, RaySun.cpp; its
//	headers from third_party/fidelityfx-denoiser/ffx-shadows-dnsr, which the
//	build adds to the includes; compiled only where they are). Shader Model
//	6.5, the compute root signature with rays (D3D12Compute.h), no rays.
//
//	After RaySun.hlsl's CSTrace (a ray a pixel), for each set of casters
//	(all of them; the live ones where the scene has them):
//	CSInputs (once): each pixel's face normal (from the depths beside it,
//	as RaySun.hlsl's) and its depth, 1 where the mask has no surface (the
//	sky, beyond its range), for this frame and as the frame before's in
//	the next.
//	CSPrepare: the rays' hits as the denoiser's bit masks of 8 x 4 pixels.
//	CSClassify: its tile classification: tiles all in the sun or all in the
//	shadow (and round them) skipped, the others' history where the point
//	was in the frame before (the camera's move), clamped to the rays round
//	it, and the moments of the visibility.
//	CSFilter: its three edge-avoiding passes (steps 1, 2, 4), the second's
//	output the next frame's history.
//	CSPack: the two sets' visibility into the mask, as RaySun.hlsl's
//	CSFilter writes it (with the pixel's view depth and how much the mask
//	counts).
//	The denoiser's callbacks are below. Its depth is the depth buffer's
//	(1: none), its matrices the view's from the view depth (ViewDepthOf),
//	its motion the camera's.
//
//////////////////////////////////////////////////////////////////////

#include "RayShade.hlsli"

ConstantBuffer<SRaySunFfxConstants> g_Ffx : register(b2);
#define RAY_VIEW g_Ffx
#include "RayView.hlsli"

// The denoiser's half precision types as full ones (no 16 bit types in our build).
#define float16_t2 float2
#define float16_t3 float3
#define float16_t float

//! The view depth at pixel p, 0 where the mask has no surface (the sky, beyond its range), as RaySun.hlsl's.
float SurfaceDepth(int2 p)
{
  const float z = DepthAt(p);
  const float d = ViewDepthOf(z);
  return z < 1 && d > 0 && d <= g_Ffx.vParams.x ? d : 0;
}

int2 FFX_DNSR_Shadows_GetBufferDimensions()
{
  return (int2)g_Ffx.vSize.xy;
}

float2 FFX_DNSR_Shadows_GetInvBufferDimensions()
{
  return g_Ffx.vSize.zw;
}

//! A tile's texel in a texture of tiles of nTileWidth pixels (8) across the view, from its index.
uint2 TileTexel(uint nIndex, uint nTileWidth)
{
  const uint nAcross = ((uint)g_Ffx.vSize.x + nTileWidth - 1) / nTileWidth;
  return uint2(nIndex % nAcross, nIndex / nAcross);
}

#if defined(RAY_FFX_INPUTS)

RWTexture2D<float4> g_Normals : register(u0);
RWTexture2D<float> g_Depth : register(u1);

[numthreads(XR_RAY_SUN_GROUP, XR_RAY_SUN_GROUP, 1)]
void CSInputs(uint3 vId : SV_DispatchThreadID)
{
  const int2 p = (int2)vId.xy;
  if (any(p >= (int2)g_Ffx.vSize.xy))
    return;
  const float d = SurfaceDepth(p);
  if (d <= 0)
  {
    g_Normals[p] = float4(0.5, 0.5, 1, 0);
    g_Depth[p] = 1;
    return;
  }
  const float3 vPos = PixelPos(p + 0.5, d);
  g_Normals[p] = float4(FaceNormal(p, vPos, d) * 0.5 + 0.5, 0);
  g_Depth[p] = DepthAt(p);
}

#elif defined(RAY_FFX_PREPARE)

RWTexture2D<uint> g_Tiles : register(u0);

//! Whether the sun shows at pixel p for the pass's set of casters: CSTrace's ray found nothing (and the pixels
//! with no surface as if it did).
bool FFX_DNSR_Shadows_HitsLight(uint2 p, uint2 vThread, uint2 vGroup)
{
  if (any(p >= (uint2)g_Ffx.vSize.xy))
    return true;
  const float3 r = RaysAt((int2)p);
  return (g_Ffx.nSet.x ? r.y : r.x) <= 0;
}

void FFX_DNSR_Shadows_WriteMask(uint nTile, uint nMask)
{
  g_Tiles[TileTexel(nTile, 8)] = nMask;
}

#include "ffx_denoiser_shadows_prepare.h"

[numthreads(8, 4, 1)]
void CSPrepare(uint3 vThread : SV_GroupThreadID, uint3 vGroup : SV_GroupID)
{
  FFX_DNSR_Shadows_PrepareShadowMask(vThread.xy, vGroup.xy);
}

#else

//! This frame's depth (CSInputs: the depth buffer's, 1 where the mask has no surface).
float FFX_DNSR_Shadows_ReadDepth(int2 p)
{
  return g_Textures[g_Ffx.nInputs.y].Load(int3(clamp(p, 0, (int2)g_Ffx.vSize.xy - 1), 0)).r;
}

float3 FFX_DNSR_Shadows_ReadNormals(int2 p)
{
  return normalize(g_Textures[g_Ffx.nInputs.x].Load(int3(clamp(p, 0, (int2)g_Ffx.vSize.xy - 1), 0)).xyz * 2 - 1);
}

bool FFX_DNSR_Shadows_IsShadowReciever(uint2 p)
{
  return all(p < (uint2)g_Ffx.vSize.xy) && FFX_DNSR_Shadows_ReadDepth((int2)p) < 1;
}

//! Clip (x, y -1..1 at a pixel's centre, y up, this frame's jitter in it; z the depth buffer's) to view: z / w
//! the view depth (ViewDepthOf), as the denoiser's mul(matrix, vector).
float4x4 FFX_DNSR_Shadows_GetProjectionInverse()
{
  const float2 b = g_Frame.vDepthParams.xy;
  return float4x4(1, 0, 0, 0,
                  0, 1, 0, 0,
                  0, 0, 0, b.y,
                  0, 0, 1, b.x);
}

//! The same clip to the world (homogeneous): the camera plus the view depth times the ray through the point, the
//! jitter taken off (PixelPos).
float4x4 FFX_DNSR_Shadows_GetViewProjectionInverse()
{
  const float2 b = g_Frame.vDepthParams.xy;
  const float3 c = g_Ffx.vCameraPos.xyz;
  const float3 o = g_Ffx.vRayO.xyz - g_Ffx.vJitter.x * g_Ffx.vRayX.xyz - g_Ffx.vJitter.y * g_Ffx.vRayY.xyz;
  const float3 x = g_Ffx.vRayX.xyz * b.y, y = g_Ffx.vRayY.xyz * b.y;
  return float4x4(x.x, y.x, c.x, c.x * b.x + o.x * b.y,
                  x.y, y.y, c.y, c.y * b.x + o.y * b.y,
                  x.z, y.z, c.z, c.z * b.x + o.z * b.y,
                  0, 0, 1, b.x);
}

//! This frame's clip to the frame before's (without its jitter).
float4x4 FFX_DNSR_Shadows_GetReprojectionMatrix()
{
  return mul(transpose(g_Ffx.mPrevViewProj), FFX_DNSR_Shadows_GetViewProjectionInverse());
}

float3 FFX_DNSR_Shadows_GetEye()
{
  return g_Ffx.vCameraPos.xyz;
}

float FFX_DNSR_Shadows_GetDepthSimilaritySigma()
{
  return 1;
}

#if defined(RAY_FFX_CLASSIFY)

RWTexture2D<float2> g_Reprojected : register(u0);
RWTexture2D<float4> g_Moments : register(u1);
RWTexture2D<uint> g_Meta : register(u2);
RWTexture2D<uint> g_Tiles : register(u3);

uint FFX_DNSR_Shadows_ReadRaytracedShadowMask(uint nTile)
{
  return g_Tiles[TileTexel(nTile, 8)];
}

float FFX_DNSR_Shadows_ReadPreviousDepth(int2 p)
{
  return g_Textures[g_Ffx.nInputs.z].Load(int3(clamp(p, 0, (int2)g_Ffx.vSize.xy - 1), 0)).r;
}

//! The pixel's move since the frame before (clip x y, this frame's minus that one's), the camera's.
float2 FFX_DNSR_Shadows_ReadVelocity(int2 p)
{
  const float z = FFX_DNSR_Shadows_ReadDepth(p);
  if (z >= 1)
    return 0;
  const float4 c = mul(float4(PixelPos(p + 0.5, ViewDepthOf(z)), 1), g_Ffx.mPrevViewProj);
  if (c.w <= 1e-3)
    return 1e4;
  const float2 vBefore = c.xy / c.w + g_Ffx.vJitter.zw;
  const float2 vNow = float2((p.x + 0.5) * g_Ffx.vSize.z * 2 - 1, 1 - (p.y + 0.5) * g_Ffx.vSize.w * 2);
  return vNow - vBefore;
}

float FFX_DNSR_Shadows_ReadHistory(float2 vUV)
{
  return g_Textures[g_Ffx.nInputs.w].SampleLevel(g_SamplerLinearClamp, vUV, 0).r;
}

float3 FFX_DNSR_Shadows_ReadPreviousMomentsBuffer(int2 p)
{
  // A first frame: the moments before are not written yet.
  if (g_Ffx.nSet.y != 0)
    return 0;
  return g_Textures[g_Ffx.nInputs2.x].Load(int3(clamp(p, 0, (int2)g_Ffx.vSize.xy - 1), 0)).xyz;
}

bool FFX_DNSR_Shadows_IsFirstFrame()
{
  return g_Ffx.nSet.y != 0;
}

void FFX_DNSR_Shadows_WriteMetadata(uint nTile, uint nMask)
{
  g_Meta[TileTexel(nTile, 8)] = nMask;
}

void FFX_DNSR_Shadows_WriteReprojectionResults(uint2 p, float2 v)
{
  g_Reprojected[p] = v;
}

void FFX_DNSR_Shadows_WriteMoments(uint2 p, float3 v)
{
  g_Moments[p] = float4(v, 0);
}

#include "ffx_denoiser_shadows_tileclassification.h"

[numthreads(64, 1, 1)]
void CSClassify(uint nThread : SV_GroupIndex, uint3 vGroup : SV_GroupID)
{
  FFX_DNSR_Shadows_TileClassification(nThread, vGroup.xy);
}

#elif defined(RAY_FFX_FILTER)

RWTexture2D<float2> g_Filtered : register(u0);
RWTexture2D<uint> g_Meta : register(u1);

float2 FFX_DNSR_Shadows_ReadInput(int2 p)
{
  return g_Textures[g_Ffx.nInputs2.y].Load(int3(p, 0)).xy;
}

uint FFX_DNSR_Shadows_ReadTileMetaData(uint nTile)
{
  return g_Meta[TileTexel(nTile, 8)];
}

#include "ffx_denoiser_shadows_filter.h"

[numthreads(8, 8, 1)]
void CSFilter(uint3 vGroup : SV_GroupID, uint3 vThread : SV_GroupThreadID, uint3 vId : SV_DispatchThreadID)
{
  bool bWrite = false;
  const float2 v = FFX_DNSR_Shadows_FilterSoftShadowsPass(vGroup.xy, vThread.xy, vId.xy, bWrite, g_Ffx.nSet.z, g_Ffx.nSet.w);
  if (bWrite)
    g_Filtered[vId.xy] = v;
}

#elif defined(RAY_FFX_PACK)

RWTexture2D<float4> g_Out : register(u0);

[numthreads(XR_RAY_SUN_GROUP, XR_RAY_SUN_GROUP, 1)]
void CSPack(uint3 vId : SV_DispatchThreadID)
{
  const int2 p = (int2)vId.xy;
  if (any(p >= (int2)g_Ffx.vSize.xy))
    return;
  const float d = SurfaceDepth(p);
  if (d <= 0)
  {
    g_Out[p] = 0;   // view depth 0: no pixel's
    return;
  }
  // Without live casters in the scene none hides the sun (CSTrace's).
  const float fAll = g_Textures[g_Ffx.nInputs2.z].Load(int3(p, 0)).r;
  const float fLive = g_Ffx.nInputs2.w != XR_RAY_NONE ? g_Textures[g_Ffx.nInputs2.w].Load(int3(p, 0)).r : 1;
  // How much the mask counts: all of it, giving way to the cascades over the end of its range.
  const float fWeight = saturate((g_Ffx.vParams.x - d) / max(g_Ffx.vParams.x - g_Ffx.vParams.y, 1e-3));
  const uint n = (uint)round(saturate(fAll) * 255) | ((uint)round(saturate(fLive) * 255) << 8) | ((uint)round(fWeight * 255) << 16);
  g_Out[p] = float4(d, (float)n, 0, 0);
}

#endif
#endif
