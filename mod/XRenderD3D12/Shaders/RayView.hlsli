// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: RayView.hlsli
//	Description: What the compute passes with rays over the main view's
//	depth buffer share (RaySun.hlsl, RayAO.hlsl): where a pixel is in the
//	world, its face's normal, the grid of their rays (a cell of 1 or 2 x 2
//	pixels, a pixel of it traced in each frame) and the history of the
//	frame before.
//	RAY_VIEW is the pass's constant buffer (RayConstants.h), with the view's
//	fields: mPrevViewProj, vCameraPos, vRayX, vRayY, vRayO, vSize, vJitter,
//	nTextures (x the depth buffer, y the rays, z the history of the frame
//	before), nTrace (x the pixels a side of a cell, y z the pixel traced).
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_RAYVIEW_HLSLI
#define XRENDERD3D12_RAYVIEW_HLSLI

//! How far apart two view depths of a surface may be, relative to them.
static const float SAME_DEPTH = 0.02;

//! The depth buffer at pixel p, clamped to the view.
float DepthAt(int2 p)
{
  p = clamp(p, 0, (int2)RAY_VIEW.vSize.xy - 1);
  return g_Textures[RAY_VIEW.nTextures.x].Load(int3(p, 0)).r;
}

//! The view depth of a value of the depth buffer.
float ViewDepthOf(float z)
{
  return g_Frame.vDepthParams.y / (z + g_Frame.vDepthParams.x);
}

//! The pixels a side of a cell of the rays' grid (1 or 2).
int TraceStep()
{
  return (int)RAY_VIEW.nTrace.x;
}

//! The cells of the rays' grid across and down.
int2 RaysSize()
{
  return ((int2)RAY_VIEW.vSize.xy + TraceStep() - 1) / TraceStep();
}

//! The pixel cell q's ray is from in this frame.
int2 RayPixel(int2 q)
{
  return q * TraceStep() + (int2)RAY_VIEW.nTrace.yz;
}

//! The ray of cell q (clamped to the grid) in the pass's rays (nTextures.y): x y what it found (x < 0: no
//! surface), z the view depth of its pixel.
float3 RaysAt(int2 q)
{
  q = clamp(q, 0, RaysSize() - 1);
  return g_Textures[RAY_VIEW.nTextures.y].Load(int3(q, 0)).xyz;
}

//! The world position at vPixel (pixels, from the view's top left) and view depth d, the jitter taken off.
float3 PixelPos(float2 vPixel, float d)
{
  const float2 v = float2(vPixel.x * RAY_VIEW.vSize.z * 2 - 1, 1 - vPixel.y * RAY_VIEW.vSize.w * 2) - RAY_VIEW.vJitter.xy;
  return RAY_VIEW.vCameraPos.xyz + d * (RAY_VIEW.vRayO.xyz + v.x * RAY_VIEW.vRayX.xyz + v.y * RAY_VIEW.vRayY.xyz);
}

//! The face's normal at pixel p (at vPos, view depth d), towards the camera.
float3 FaceNormal(int2 p, float3 vPos, float d)
{
  const float dl = ViewDepthOf(DepthAt(p + int2(-1, 0))), dr = ViewDepthOf(DepthAt(p + int2(1, 0)));
  const float du = ViewDepthOf(DepthAt(p + int2(0, -1))), dd = ViewDepthOf(DepthAt(p + int2(0, 1)));
  const float2 c = p + 0.5;
  const float3 vX = abs(dr - d) < abs(dl - d) ? PixelPos(c + float2(1, 0), dr) - vPos : vPos - PixelPos(c - float2(1, 0), dl);
  const float3 vY = abs(dd - d) < abs(du - d) ? PixelPos(c + float2(0, 1), dd) - vPos : vPos - PixelPos(c - float2(0, 1), du);
  const float3 vToCamera = RAY_VIEW.vCameraPos.xyz - vPos;
  float3 n = cross(vX, vY);
  const float fLen = length(n);
  n = fLen > 1e-12 ? n / fLen : normalize(vToCamera);
  return dot(n, vToCamera) < 0 ? -n : n;
}

//! FaceNormal, each axis's neighbour taken from the side whose two pixels, extended, meet the pixel's depth (the
//! depth buffer is affine across a face): at a crease, a floor meeting a wall, the pixel's own face, not a cross of
//! the two faces' slopes that may point along the crease (RaySun.hlsl, d3d12_RTSunCrease).
float3 FaceNormalCrease(int2 p, float3 vPos, float d)
{
  const float z = DepthAt(p);
  const float zl = DepthAt(p + int2(-1, 0)), zr = DepthAt(p + int2(1, 0)), zu = DepthAt(p + int2(0, -1)), zd = DepthAt(p + int2(0, 1));
  const float zl2 = DepthAt(p + int2(-2, 0)), zr2 = DepthAt(p + int2(2, 0));
  const float zu2 = DepthAt(p + int2(0, -2)), zd2 = DepthAt(p + int2(0, 2));
  const float2 c = p + 0.5;
  const float3 vX = abs(2 * zr - zr2 - z) < abs(2 * zl - zl2 - z) ? PixelPos(c + float2(1, 0), ViewDepthOf(zr)) - vPos
                                                                   : vPos - PixelPos(c - float2(1, 0), ViewDepthOf(zl));
  const float3 vY = abs(2 * zd - zd2 - z) < abs(2 * zu - zu2 - z) ? PixelPos(c + float2(0, 1), ViewDepthOf(zd)) - vPos
                                                                   : vPos - PixelPos(c - float2(0, 1), ViewDepthOf(zu));
  const float3 vToCamera = RAY_VIEW.vCameraPos.xyz - vPos;
  float3 n = cross(vX, vY);
  const float fLen = length(n);
  n = fLen > 1e-12 ? n / fLen : normalize(vToCamera);
  return dot(n, vToCamera) < 0 ? -n : n;
}

//! Interleaved gradient noise at pixel p, another pattern each frame.
float Noise(float2 p, uint nFrame)
{
  p += 5.588238 * (float)(nFrame & 63);
  return frac(52.9829189 * frac(dot(p, float2(0.06711056, 0.00583715))));
}

//! The history of the frame before (nTextures.z: x y the values, z the frames in it, w the view depth) at
//! the point of vPixel (pixels, view depth d), where it was then: x y the values, z the frames (0: none
//! there, another surface or off the view).
float3 HistoryAt(float2 vPixel, float d)
{
  float3 vHistory = 0;
  const float4 c = mul(float4(PixelPos(vPixel, d), 1), RAY_VIEW.mPrevViewProj);
  if (c.w > 1e-3)
  {
    const float2 vNDC = c.xy / c.w + RAY_VIEW.vJitter.zw;
    const float2 t = float2(0.5 + 0.5 * vNDC.x, 0.5 - 0.5 * vNDC.y) * RAY_VIEW.vSize.xy - 0.5;
    const int2 b = (int2)floor(t);
    const float2 f = t - b;
    float4 vSum = 0;
    [unroll]
    for (int k = 0; k < 4; k++)
    {
      const int2 q = b + int2(k & 1, k >> 1);
      if (any(q < 0) || any(q >= (int2)RAY_VIEW.vSize.xy))
        continue;
      const float4 h = g_Textures[RAY_VIEW.nTextures.z].Load(int3(q, 0));
      const float w = ((k & 1) ? f.x : 1 - f.x) * ((k >> 1) ? f.y : 1 - f.y);
      if (h.z > 0 && abs(h.w - c.w) <= SAME_DEPTH * c.w + 0.02)
        vSum += w * float4(h.xyz, 1);
    }
    if (vSum.w > 1e-3)
      vHistory = vSum.xyz / vSum.w;
  }
  return vHistory;
}

#endif
