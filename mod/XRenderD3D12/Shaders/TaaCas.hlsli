// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: TaaCas.hlsli
//	Description: TAA's sharpening (d3d12_AA taa, d3d12_UpscaleSharpness;
//	Taa.hlsl's last kernel): AMD FidelityFX CAS (Contrast Adaptive
//	Sharpening, third_party/fidelityfx-cas, ffx_cas.h as it is) in its
//	sharpen-only form, 32-bit, with the 3 x 3's diagonals in its soft
//	minimum and maximum (CAS_BETTER_DIAGONALS) and the exact reciprocals
//	and square root (CAS_GO_SLOWER). Its kernel's negative lobe around the
//	pixel, -1/8 to -1/5 by the sharpness (CasSetup, UpscalerTaa.cpp), is
//	scaled by the 3 x 3's distance to black and to white over its maximum:
//	the textures' small contrasts get it all, a silhouette against the sky
//	and the brightest and darkest parts less. CAS takes linear light from
//	0 to 1 (TaaToCas). The includer defines first:
//	  float3 TaaCasLoad(int2 p)  pixel p of the picture (clamped into it)
//	                             in CAS's space
//
//////////////////////////////////////////////////////////////////////

#ifndef XR_TAA_CAS_HLSLI
#define XR_TAA_CAS_HLSLI

#define A_GPU 1
#define A_HLSL 1
// ffx_a.h's bit field helpers shift a literal 1 (dxc's -Wambig-lit-shift); CAS does not use them.
#pragma dxc diagnostic push
#pragma dxc diagnostic ignored "-Wambig-lit-shift"
#include "ffx_a.h"
#pragma dxc diagnostic pop

#define CAS_BETTER_DIAGONALS 1
#define CAS_GO_SLOWER 1

float3 TaaCasLoad(int2 p);

AF3 CasLoad(ASU2 p)
{
  return TaaCasLoad(p);
}

void CasInput(inout AF1 r, inout AF1 g, inout AF1 b)
{
}

#include "ffx_cas.h"

//! Pixel p sharpened by CAS, with CasSetup's constants (vConst0, vConst1) of the sharpness: in CAS's space, 0 to 1.
float3 TaaCas(int2 p, uint4 vConst0, uint4 vConst1)
{
  AF3 c;
  CasFilter(c.r, c.g, c.b, AU2(p), vConst0, vConst1, true);
  return c;
}

#endif
