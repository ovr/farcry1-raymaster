// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: TestPathTrace.hlsl
//	Description: The unit tests' kernels over the functions of the path
//	tracer's kernels (Shader Model 6.5, as the renderer compiles them).
//	PathTrace.hlsl is included as it is: its own kernels and the
//	resources only they use are left out of a compile of one of these
//	entry points, so the tests run its real functions (PathLeafSide,
//	CosineDirection, PathCheckerboard.hlsli's PathTraced with its real
//	g_PT) without the scene for rays. The layout kernel reads the
//	structures C++ shares with HLSL (RayConstants.h) as HLSL lays them out.
//
//////////////////////////////////////////////////////////////////////

#include "TestCommon.hlsli"
#include "PathTrace.hlsl"

StructuredBuffer<SRayInstance> g_TestInstances : register(t1, space9);
StructuredBuffer<SPTLight> g_TestLights : register(t2, space9);

//! PathLeafSide(face g_TestIn[2i].xyz, direction g_TestIn[2i + 1].xyz) into out[i].
[numthreads(64, 1, 1)]
void CSLeafSide(uint i : SV_DispatchThreadID)
{
  if (i < g_Test.nTest.x)
    TestOut(i, float4(PathLeafSide(TestIn(i * 2).xyz, TestIn(i * 2 + 1).xyz), 0));
}

//! CosineDirection into out[i]: about the normal g_TestIn[i / n].xyz (n = g_Test.nTest.y samples a normal) at the centres of
//! an s x s grid of u, v (s = g_Test.nTest.z; sample j = i % n at u = (j % s + 0.5) / s, v = (j / s + 0.5) / s); with
//! g_Test.nTest.z 0, about g_TestIn[2i].xyz at the u, v of g_TestIn[2i + 1].xy.
[numthreads(64, 1, 1)]
void CSCosine(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  float3 n;
  float u, v;
  if (g_Test.nTest.z)
  {
    const uint s = g_Test.nTest.z, j = i % g_Test.nTest.y;
    n = TestIn(i / g_Test.nTest.y).xyz;
    u = (j % s + 0.5) / s;
    v = (j / s + 0.5) / s;
  }
  else
  {
    n = TestIn(i * 2).xyz;
    u = TestIn(i * 2 + 1).x;
    v = TestIn(i * 2 + 1).y;
  }
  TestOut(i, float4(CosineDirection(n, u, v), 0));
}

//! PathTraced over a g_Test.nTest.x x g_Test.nTest.y grid of pixels, with g_PT as the test binds it: out[x + y * w].x 1 where traced.
[numthreads(8, 8, 1)]
void CSCheckerboard(uint2 p : SV_DispatchThreadID)
{
  if (all(p < g_Test.nTest.xy))
    g_TestOut[p.x + p.y * g_Test.nTest.x] = uint4(PathTraced((int2)p) ? 1 : 0, 0, 0, 0);
}

//! The structures as HLSL reads them: element 1 of g_TestInstances (14 vectors), of g_TestLights (8: the matrix by rows) and the
//! whole g_PT (26), in the order of their members, each vector's words as they are. A member moved or a
//! size that differs from C++ shows as words out of place (LayoutTests.cpp).
[numthreads(1, 1, 1)]
void CSLayout()
{
  uint o = 0;
  const SRayInstance r = g_TestInstances[1];
  g_TestOut[o++] = r.nGeom;
  g_TestOut[o++] = r.nLayout;
  g_TestOut[o++] = r.nMaterial;
  g_TestOut[o++] = r.nMaterial2;
  g_TestOut[o++] = asuint(r.vColor);
  g_TestOut[o++] = asuint(r.vAmbient);
  g_TestOut[o++] = asuint(r.vTexGenU);
  g_TestOut[o++] = asuint(r.vTexGenV);
  g_TestOut[o++] = asuint(r.vTexMat0);
  g_TestOut[o++] = asuint(r.vTexMat1);
  g_TestOut[o++] = asuint(r.vAddColor);
  g_TestOut[o++] = asuint(r.vAlbedo);
  g_TestOut[o++] = asuint(r.vSpecular);
  g_TestOut[o++] = r.nPath;
  const SPTLight l = g_TestLights[1];
  g_TestOut[o++] = asuint(l.vPos);
  g_TestOut[o++] = asuint(l.vColor);
  g_TestOut[o++] = asuint(l.vSpecular);
  g_TestOut[o++] = l.nParams;
  [unroll]
  for (uint k = 0; k < 4; k++)
    g_TestOut[o++] = asuint(l.mProject[k]);
  [unroll]
  for (uint m = 0; m < 4; m++)
    g_TestOut[o++] = asuint(g_PT.mPrevViewProj[m]);
  g_TestOut[o++] = asuint(g_PT.vCameraPos);
  g_TestOut[o++] = asuint(g_PT.vRayX);
  g_TestOut[o++] = asuint(g_PT.vRayY);
  g_TestOut[o++] = asuint(g_PT.vRayO);
  g_TestOut[o++] = asuint(g_PT.vSize);
  g_TestOut[o++] = asuint(g_PT.vJitter);
  g_TestOut[o++] = asuint(g_PT.vParams);
  g_TestOut[o++] = asuint(g_PT.vSun);
  g_TestOut[o++] = asuint(g_PT.vSky);
  g_TestOut[o++] = asuint(g_PT.vShade);
  g_TestOut[o++] = asuint(g_PT.vFill);
  g_TestOut[o++] = asuint(g_PT.vSpecular);
  g_TestOut[o++] = g_PT.nTextures;
  g_TestOut[o++] = g_PT.nTextures2;
  g_TestOut[o++] = g_PT.nTextures3;
  g_TestOut[o++] = g_PT.nTrace;
  g_TestOut[o++] = g_PT.nFlags;
  g_TestOut[o++] = g_PT.nPath;
  g_TestOut[o++] = asuint(g_PT.vNrd);
  g_TestOut[o++] = asuint(g_PT.vCache);
  g_TestOut[o++] = g_PT.nCache;
  g_TestOut[o++] = g_PT.nCheckerboard;
}
