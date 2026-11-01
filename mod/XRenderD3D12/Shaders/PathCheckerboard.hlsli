// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: PathCheckerboard.hlsli
//	Description: CSTrace's paths as the kernels after it take them
//	(CSTemporal, NRD's CSPack and CSResolve): with the checkerboard
//	(d3d12_PTCheckerboard, g_PT.nCheckerboard) CSTrace traces half the
//	pixels each frame, those of ((x ^ y) & 1) == nCheckerboard.y; a pixel
//	it left out takes the light of its four neighbours, which it traced,
//	weighed by how near their points are to its plane and how alike their
//	normals are (as CSFilter weighs). After PathGBuffer.hlsli and
//	RayView.hlsli with RAY_VIEW g_PT (SPathTraceConstants).
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_PATHCHECKERBOARD_HLSLI
#define XRENDERD3D12_PATHCHECKERBOARD_HLSLI

//! Whether CSTrace traced pixel p this frame.
bool PathTraced(int2 p)
{
  return !g_PT.nCheckerboard.x || ((uint)(p.x ^ p.y) & 1) == g_PT.nCheckerboard.y;
}

//! The G-buffer's normal at pixel p: on the side of its triangle the camera sees (the leaves' two sides), as
//! General.hlsl wrote it.
float3 PathNormalAt(int2 p)
{
  return PathOctDecode(g_Textures[g_PT.nTextures2.x].Load(int3(p, 0)).rg);
}

//! The view depth of a pixel CSTrace left out, as it finds a surface (0: none the path tracer lights).
float PathLeftOutDepth(int2 p)
{
  const float z = DepthAt(p);
  const float d = ViewDepthOf(z);
  return PathGBufferMaterial(g_Textures[g_PT.nTextures2.x].Load(int3(p, 0))) == 0 || z >= 1 || d <= 0 ? 0 : d;
}

//! The view depth of the surface at pixel p the path tracer lights (0: none).
float PathSurfaceDepth(int2 p)
{
  return PathTraced(p) ? g_Textures[g_PT.nTextures.y].Load(int3(p, 0)).w : PathLeftOutDepth(p);
}

//! The paths' light at pixel p: rgb, w the view depth (0 where the path tracer lights no surface there); fHit the first
//! bounce's hit distance where bHit (nTextures3.w).
float4 PathRaysAt(int2 p, bool bHit, out float fHit)
{
  fHit = 0;
  if (PathTraced(p))
  {
    if (bHit)
      fHit = g_Textures[g_PT.nTextures3.w].Load(int3(p, 0)).r;
    return g_Textures[g_PT.nTextures.y].Load(int3(p, 0));
  }
  // Left out: the surface as CSTrace finds it, the light of the neighbours on it.
  const float d = PathLeftOutDepth(p);
  if (d <= 0)
    return 0;
  const float3 vPos = PixelPos(p + 0.5, d);
  const float3 vN = PathNormalAt(p);
  const float3 vFace = FaceNormal(p, vPos, d);
  const float fPlane = 1 / (0.02 + 2 * d * g_PT.vParams.y);
  float4 vSum = 0, vAny = 0;
  float fHitSum = 0, fHitAny = 0;
  [unroll]
  for (int k = 0; k < 4; k++)
  {
    const int2 q = p + int2(k == 0 ? -1 : k == 1 ? 1 : 0, k == 2 ? -1 : k == 3 ? 1 : 0);
    if (any(q < 0) || any(q >= (int2)g_PT.vSize.xy))
      continue;
    const float4 h = g_Textures[g_PT.nTextures.y].Load(int3(q, 0));
    if (h.w <= 0)
      continue;
    const float fQHit = bHit ? g_Textures[g_PT.nTextures3.w].Load(int3(q, 0)).r : 0;
    const float3 vQ = PixelPos(q + 0.5, h.w);
    const float fNormal = saturate(dot(PathNormalAt(q), vN));
    const float w = saturate(1 - abs(dot(vQ - vPos, vFace)) * fPlane) * pow(fNormal, 8);
    vSum += w * float4(h.rgb, 1);
    fHitSum += w * fQHit;
    vAny += float4(h.rgb, 1);
    fHitAny += fQHit;
  }
  // None on its plane (an edge, a thin thing): all of them alike.
  if (vSum.w < 1e-3)
  {
    vSum = vAny;
    fHitSum = fHitAny;
  }
  if (vSum.w <= 0)
    return float4(0, 0, 0, d);
  fHit = fHitSum / vSum.w;
  return float4(vSum.rgb / vSum.w, d);
}

#endif
