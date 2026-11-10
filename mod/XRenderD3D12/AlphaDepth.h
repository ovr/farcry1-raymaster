// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: AlphaDepth.h
//	Description: The draws of the leaves' and the grass's depth pass
//	(d3d12_FoliagePrepass; Draw3D.cpp's DrawItems, Draw.cpp's
//	DrawGeometry): an alpha tested draw whose alpha at its test is its
//	vertices' times its texture's at uv0, no flag of General.hlsl's
//	ShadePixel changing it after the texture is read. The pass writes
//	its depth where that alpha passes the test (General.hlsl's
//	AlphaDepthMain, the product compared as ShadePixel's), the pixels
//	its colour pass keeps; the colour then tests EQUAL, writing none.
//	Header only (the unit tests, Tests/AlphaDepthTests.cpp).
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_ALPHADEPTH_H
#define XRENDERD3D12_ALPHADEPTH_H

#include "Shaders/Constants.h"

namespace AlphaDepth
{
  //! The flags that change the alpha after the texture is read, or the texture's alpha itself (a cube, an array, the
  //! offset by a bump map, a second layer, the parallax's uv; General.hlsl's ShadePixel), or that test another.
  constexpr uint32 OTHER_ALPHA = DF_ALPHATEST_LESS | DF_SHADOW_PASS | DF_ALPHA_FROM_TEX | DF_DECAL_FADE | DF_CUBE0 | DF_TEX0_ARRAY;
  constexpr uint32 OTHER_ALPHA2 = DF2_MODULATE | DF2_PROJECTOR | DF2_GLASS | DF2_SHADER_FX | DF2_CASCADE_CASTER | DF2_BUMP_OFFSET |
                                  DF2_TEX4_LAYER | DF2_FOG_VOLUME;

  //! The draw's alpha at its test is In.vColor.a times its texture's at uv0 (DF_TEX1 without the light map or the
  //! detail multiplies in the second texture's).
  inline bool Covers(uint32 nFlags, uint32 nFlags2, float fAlphaRef, uint32 nParallax)
  {
    return (nFlags & (DF_ALPHATEST | DF_TEX0)) == (DF_ALPHATEST | DF_TEX0) && !(nFlags & OTHER_ALPHA) && !(nFlags2 & OTHER_ALPHA2) &&
           fAlphaRef > 0 && nParallax == 0 && (!(nFlags & DF_TEX1) || (nFlags & (DF_TEX1_LIGHTMAP | DF_TEX1_DETAIL)));
  }
}

#endif
