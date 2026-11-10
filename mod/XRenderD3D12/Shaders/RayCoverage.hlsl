// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: RayCoverage.hlsl
//	Description: d3d12_RTAlphaCoverage: the shares of the triangles of the
//	scene's alpha tested instances their alpha test passes on
//	(RayShade.hlsli's RayTriangleCoverage), a byte a triangle, into the
//	buffer the rays read them from (RayCandidatePasses). Made once a mesh
//	and material (RayTracing.cpp); a thread for each 4 triangles of a job.
//	  b2 SRayCoverageConstants, u0 the shares (RWByteAddressBuffer)
//
//////////////////////////////////////////////////////////////////////

#include "RayShade.hlsli"

ConstantBuffer<SRayCoverageConstants> g_Coverage : register(b2);
RWByteAddressBuffer g_Out : register(u0);

[numthreads(XR_RAY_COVERAGE_GROUP, 1, 1)]
void CSCoverage(uint3 vId : SV_DispatchThreadID)
{
  const uint nJobs = g_Coverage.nParams.x;
  if (nJobs == 0)
    return;
  // The job of the thread: the last whose first thread is not after it.
  uint nLo = 0, nHi = nJobs;
  while (nHi - nLo > 1)
  {
    const uint nMid = (nLo + nHi) / 2;
    if (g_Coverage.nJobs[nMid].w <= vId.x)
      nLo = nMid;
    else
      nHi = nMid;
  }
  const uint4 J = g_Coverage.nJobs[nLo];
  const uint nWord = vId.x - J.w, nFirst = nWord * 4;
  if (nFirst >= J.z)
    return;
  uint nOut = 0;
  for (uint k = 0; k < 4 && nFirst + k < J.z; k++)
    nOut |= RayTriangleCoverage(J.x, nFirst + k) << (8 * k);
  g_Out.Store(J.y + nWord * 4, nOut);
}
