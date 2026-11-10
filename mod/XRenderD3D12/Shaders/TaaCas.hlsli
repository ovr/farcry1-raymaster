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
//	0 to 1 (TaaToCas, Taa.hlsli, included first); its result, back in the
//	picture's colours, is kept within the 3 x 3's range (TaaCas says why).
//	The includer defines first:
//	  float3 TaaCasPicture(int2 p)  pixel p of the picture (clamped into
//	                                it): the HDR light or the LDR colours
//	  float TaaCasExposure()        the picture's exposure (TaaToCas's:
//	                                0 for the LDR colours)
//	and with TAA_CAS_LOAD
//	  float3 TaaCasLoad(int2 p)     TaaToCas of TaaCasPicture(p) at that
//	                                exposure (Taa.hlsl's, from its tile)
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

float3 TaaCasPicture(int2 p);
float TaaCasExposure();

#if defined(TAA_CAS_LOAD)
float3 TaaCasLoad(int2 p);
#endif

AF3 CasLoad(ASU2 p)
{
#if defined(TAA_CAS_LOAD)
  return TaaCasLoad(p);
#else
  return TaaToCas(TaaCasPicture(p), TaaCasExposure());
#endif
}

void CasInput(inout AF1 r, inout AF1 g, inout AF1 b)
{
}

#include "ffx_cas.h"

//! Pixel p sharpened by CAS, with CasSetup's constants (vConst0, vConst1) of the sharpness, in the picture's colours,
//! each channel within its 3 x 3's range of them. CAS's lobe takes a pixel a little past its neighbours, which in colours
//! from 0 to 1 is a little; but TaaToCas compresses the HDR light, the brighter the more (c / (1 + max(r, g, b))), and
//! TaaFromCas makes of that little near the top the light's many times: the sky at a cave's opening, a window seen from
//! a dark room, became white with a bloom around it. Within the range the light sharpened is never brighter than its
//! brightest neighbour (as FSR's RCAS limits its lobe to the neighbours' range on the same compressed HDR light). The
//! range is the picture's own: in CAS's space each pixel is compressed by its own brightest channel, so that a channel's
//! range there is not its range in light.
float3 TaaCas(int2 p, uint4 vConst0, uint4 vConst1)
{
  AF3 c;
  CasFilter(c.r, c.g, c.b, AU2(p), vConst0, vConst1, true);
  float3 vMin = 1e30, vMax = 0;
  [unroll] for (int y = -1; y <= 1; y++)
  {
    [unroll] for (int x = -1; x <= 1; x++)
    {
      const float3 v = max(TaaCasPicture(p + int2(x, y)), 0);
      vMin = min(vMin, v);
      vMax = max(vMax, v);
    }
  }
  return clamp(TaaFromCas(c, TaaCasExposure()), vMin, vMax);
}

#endif
