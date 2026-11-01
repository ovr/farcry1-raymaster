// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: TestCommon.hlsli
//	Description: The bindings of the unit tests' kernels (GpuHarness.h),
//	in space9 so that they never meet the registers of the renderer's
//	headers the tests include:
//	  b0 space9  STestConstants g_Test (the test's parameters)
//	  t0 space9  StructuredBuffer<uint4> g_TestIn (the test's input)
//	  u0 space9  RWStructuredBuffer<uint4> g_TestOut (what it found)
//	Both are words as they are: floats go through asuint / asfloat, so
//	that no bit of a key, a NaN or a denormal is changed on the way.
//	The renderer's own bindings the tested code reads are bound where
//	its headers declare them: b2 (g_PT, SPathTraceConstants), u3
//	(g_PathCache), t1 and t2 space9 (the layout test's SRayInstance and
//	SPTLight).
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_TESTS_TESTCOMMON_HLSLI
#define XRENDERD3D12_TESTS_TESTCOMMON_HLSLI

struct STestConstants
{
  uint4 nTest;    //!< x the number of items, y z w the test's own
  float4 vTest;   //!< the test's own
  float4 vTest2;  //!< the test's own
};

ConstantBuffer<STestConstants> g_Test : register(b0, space9);
StructuredBuffer<uint4> g_TestIn : register(t0, space9);
RWStructuredBuffer<uint4> g_TestOut : register(u0, space9);

//! Item i of the input as floats.
float4 TestIn(uint i)
{
  return asfloat(g_TestIn[i]);
}

//! Item i of the output, floats.
void TestOut(uint i, float4 v)
{
  g_TestOut[i] = asuint(v);
}

//! The 10 bits a channel of DXGI_FORMAT_R10G10B10A2_UNORM keep of x (0..1, rounded to the nearest step) and the 2 of
//! its alpha: what the G-buffer's normal target stores of a texel.
float4 QuantizeR10G10B10A2(float4 x)
{
  return float4(round(saturate(x.rgb) * 1023) / 1023, round(saturate(x.a) * 3) / 3);
}

#endif
