// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: LayoutTests.cpp
//	Description: The structures C++ and the shaders share
//	(Shaders/RayConstants.h) laid out alike on both sides: C++ fills two
//	elements of SRayInstance and SPTLight (structured buffers) and one
//	SPathTraceConstants (a constant buffer) with a different word in each
//	place, CSLayout (TestPathTrace.hlsl) reads them member by member as
//	HLSL lays them out and writes the words back in the members' order.
//	(AbiChecks.cpp checks the SDK classes against the engine, not these.)
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "GpuHarness.h"
#include "TestMath.h"

#include "Test_Layout.h"

using namespace XrTest;

namespace
{
  //! The vectors (16 bytes; a matrix 4) CSLayout reads of each structure: a member added to RayConstants.h shows here first.
  constexpr size_t INSTANCE_VECTORS = 14, LIGHT_VECTORS = 8, PATH_TRACE_VECTORS = 27;

  //! Word w of element k of a structure: a pattern no two places share (a normal float's bits, though they go as words).
  uint32 Pattern(uint32 nStruct, uint32 k, uint32 w) { return 0x40000000u | (nStruct << 20) | (k << 12) | w; }

  template <class T> void Fill(T *p, size_t n, uint32 nStruct)
  {
    for (size_t k = 0; k < n; k++)
    {
      uint32 *pWords = (uint32 *)&p[k];
      for (uint32 w = 0; w < sizeof(T) / 4; w++)
        pWords[w] = Pattern(nStruct, (uint32)k, w);
    }
  }
}

//! The sizes: whole vectors, as many as CSLayout reads. Why: a C++ size that differs from HLSL's stride shifts every
//! instance after the first in g_RayInstances, and every light in g_PTLights.
XR_TEST(Layout_Sizes)
{
  CHECK(sizeof(SRayInstance) == INSTANCE_VECTORS * 16);
  CHECK(sizeof(SPTLight) == LIGHT_VECTORS * 16);
  CHECK(sizeof(SPathTraceConstants) == PATH_TRACE_VECTORS * 16);
}

//! Every word of element 1 of SRayInstance and SPTLight and of SPathTraceConstants where HLSL reads it (the matrices by rows:
//! -Zpr, row major, as C++ copies Matrix44). Why: the instances' materials, the lamps and the path tracer's constants all
//! cross this boundary every frame; a word out of place is a wrong texture, light or switch, with no error anywhere.
XR_TEST(Layout_SharedStructures)
{
  if (!Gpu().HasShaderModel65())
    XR_SKIP("the device has no Shader Model 6.5 (PathTrace.hlsl's kernels)");
  SRayInstance Instances[2];
  SPTLight Lights[2];
  SPathTraceConstants pt;
  Fill(Instances, 2, 1);
  Fill(Lights, 2, 2);
  Fill(&pt, 1, 3);
  SDispatch d;
  d.Shader = XR_BYTECODE(Test_Layout);
  d.pInstances = Instances;
  d.nInstanceBytes = sizeof(Instances);
  d.pLights = Lights;
  d.nLightBytes = sizeof(Lights);
  d.pPathTrace = &pt;
  d.nOut = INSTANCE_VECTORS + LIGHT_VECTORS + PATH_TRACE_VECTORS;
  const std::vector<uint32> Out = Gpu().Run(d);
  const uint32 *pInstance = (const uint32 *)&Instances[1], *pLight = (const uint32 *)&Lights[1], *pPT = (const uint32 *)&pt;
  size_t o = 0;
  for (size_t w = 0; w < INSTANCE_VECTORS * 4; w++, o++)
    CHECK_MSG(Out[o] == pInstance[w], "SRayInstance word " + std::to_string(w));
  for (size_t w = 0; w < LIGHT_VECTORS * 4; w++, o++)
    CHECK_MSG(Out[o] == pLight[w], "SPTLight word " + std::to_string(w));
  for (size_t w = 0; w < PATH_TRACE_VECTORS * 4; w++, o++)
    CHECK_MSG(Out[o] == pPT[w], "SPathTraceConstants word " + std::to_string(w));
}
