// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: TestDraw.hlsl
//	Description: The unit tests' kernel over the draw's constants of
//	Common.hlsli (Shaders/Constants.h): CSDrawLayout reads every member of
//	g_Draw (b1, SDrawHead) and g_DrawExt (b3, SDrawExt) in the order the
//	structures declare them and writes their words back, a vector a
//	member (a matrix by rows, -Zpr; the header's words four a vector);
//	LayoutTests.cpp's Layout_DrawConstants checks them against C++.
//
//////////////////////////////////////////////////////////////////////

#include "TestCommon.hlsli"
#include "Common.hlsli"

[numthreads(1, 1, 1)]
void CSDrawLayout()
{
  uint o = 0, r, i;
  [unroll] for (r = 0; r < 4; r++) g_TestOut[o++] = asuint(g_Draw.mWorld[r]);
  [unroll] for (r = 0; r < 4; r++) g_TestOut[o++] = asuint(g_Draw.mWorldViewProj[r]);
  [unroll] for (r = 0; r < 4; r++) g_TestOut[o++] = asuint(g_Draw.mPrevWorldViewProj[r]);
  g_TestOut[o++] = asuint(g_Draw.vColor);
  g_TestOut[o++] = uint4(g_Draw.nFlags, g_Draw.nTex0, g_Draw.nTex1, g_Draw.nSamplers);
  g_TestOut[o++] = uint4(asuint(g_Draw.fAlphaRef), g_Draw.nLights, g_Draw.nTex2, g_Draw.nTex3);
  g_TestOut[o++] = uint4(g_Draw.nTex4, g_Draw.nTex5, g_Draw.nTex6, g_Draw.nFlags2);
  g_TestOut[o++] = asuint(g_DrawExt.vAmbient);
  g_TestOut[o++] = asuint(g_DrawExt.vTexGenU);
  g_TestOut[o++] = asuint(g_DrawExt.vTexGenV);
  g_TestOut[o++] = asuint(g_DrawExt.vTexMat0);
  g_TestOut[o++] = asuint(g_DrawExt.vTexMat1);
  [unroll] for (i = 0; i < XR_MAX_DRAW_LIGHTS; i++) g_TestOut[o++] = asuint(g_DrawExt.vLightPos[i]);
  [unroll] for (i = 0; i < XR_MAX_DRAW_LIGHTS; i++) g_TestOut[o++] = asuint(g_DrawExt.vLightColor[i]);
  [unroll] for (i = 0; i < XR_MAX_DRAW_LIGHTS; i++) g_TestOut[o++] = asuint(g_DrawExt.vLightAtten[i]);
  g_TestOut[o++] = asuint(g_DrawExt.vTexGen1U);
  g_TestOut[o++] = asuint(g_DrawExt.vTexGen1V);
  g_TestOut[o++] = asuint(g_DrawExt.vParams0);
  g_TestOut[o++] = asuint(g_DrawExt.vParams1);
  g_TestOut[o++] = asuint(g_DrawExt.vParams2);
  g_TestOut[o++] = asuint(g_DrawExt.vParams3);
  [unroll] for (r = 0; r < 4; r++) g_TestOut[o++] = asuint(g_DrawExt.mParams0[r]);
  [unroll] for (i = 0; i < XR_MAX_DRAW_SHADOWS; i++) [unroll] for (r = 0; r < 4; r++) g_TestOut[o++] = asuint(g_DrawExt.mShadow[i][r]);
  g_TestOut[o++] = g_DrawExt.nShadowTex;
  g_TestOut[o++] = asuint(g_DrawExt.vShadowParams);
  [unroll] for (i = 0; i < XR_MAX_DRAW_LIGHTS; i++) g_TestOut[o++] = asuint(g_DrawExt.vLightSpec[i]);
  g_TestOut[o++] = asuint(g_DrawExt.vSpecular);
  g_TestOut[o++] = asuint(g_DrawExt.vAddColor);
  g_TestOut[o++] = asuint(g_DrawExt.vHeat);
  g_TestOut[o++] = asuint(g_DrawExt.vHdrCore);
  g_TestOut[o++] = asuint(g_DrawExt.vEdgeFade);
  g_TestOut[o++] = asuint(g_DrawExt.vUnderColor);
  g_TestOut[o++] = asuint(g_DrawExt.vUnder);
  g_TestOut[o++] = g_DrawExt.nParallax;
  g_TestOut[o++] = asuint(g_DrawExt.vParallax);
  g_TestOut[o++] = g_DrawExt.nPbr;
  g_TestOut[o++] = asuint(g_DrawExt.vSkin);
  g_TestOut[o++] = asuint(g_DrawExt.vLeafLight);
  g_TestOut[o++] = asuint(g_DrawExt.vLeafSun);
  g_TestOut[o++] = g_DrawExt.nPathDecal;
  g_TestOut[o++] = g_DrawExt.nPathDecalDepth;
  g_TestOut[o++] = asuint(g_DrawExt.vPathDecal);
}
