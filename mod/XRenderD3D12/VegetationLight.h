// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: VegetationLight.h
//	Description: The light baked into the vegetation's vertex colours
//	(CLeafBuffer::UpdateCustomLighting), without the renderer's types (the
//	unit tests): a vertex's colour times half the ambient light plus the
//	sun's (on one side the sun times N.L; on both sides, the leaves and the
//	grass drawn without culling, half the ambient and half the sun times
//	|N.L|) less the self shadow, the shaders taking it twice. With the path
//	tracer (Geometry::SetUnlitVegetation) the colours are baked unlit, half
//	the mesh's colour, and the paths light them; the draws they do not
//	light (the plants blended after the water, the water's reflection, the
//	cubes) take the light the colours had lit on the mean over the mesh
//	(SGain: in each channel the sum of its vertices' lit colours over the
//	sum of their unlit, the self shadow left out; d3d12_PTVegetationGain).
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_VEGETATIONLIGHT_H
#define XRENDERD3D12_VEGETATIONLIGHT_H

#include <algorithm>
#include <cmath>

namespace VegetationLight
{
  //! The light a vertex's colour is baked with in one channel (0..1 the vertex colour's units, the shaders' 2x not in it):
  //! fAmbient and fSun the ambient's and the sun's light, d its normal's N.L, bTwoSided the leaves and the grass drawn
  //! without culling, fShadow the self shadow taken off the sun's.
  inline float Light(float fAmbient, float fSun, float d, bool bTwoSided, float fShadow)
  {
    const float fSunLight = bTwoSided ? 0.5f * (fAmbient + fSun * std::fabs(d)) : fSun * std::max(d, 0.f);
    return 0.5f * fAmbient + std::max(fSunLight - fShadow, 0.f);
  }

  //! The light the raster lights a vertex of the bake with, the shaders' 2x in it, as the path tracer's G-buffer takes it
  //! for a leaf's least light (d3d12_PTLeafLight, Draw3D.cpp, General.hlsl): fAmbientTerm plus fSunTerm times N.L (|N.L|
  //! on both sides), 2 Light without the self shadow.
  inline void RasterTerms(float fAmbient, float fSun, bool bTwoSided, float &fAmbientTerm, float &fSunTerm)
  {
    fAmbientTerm = bTwoSided ? 2 * fAmbient : fAmbient;
    fSunTerm = bTwoSided ? fSun : 2 * fSun;
  }

  //! The light the colours had lit over unlit, in each channel the sum of the vertices' lit colours over the sum of their
  //! unlit (the stored colours, clamped to 1 as they are stored; the unlit half the mesh's colour).
  struct SGain
  {
    double vLit[3] = {};
    double vUnlit[3] = {};

    //! A vertex of the mesh's colour rgb (0..1), its N.L d, on both sides or one.
    void Add(const float rgb[3], const float vAmbient[3], const float vSun[3], float d, bool bTwoSided)
    {
      for (int c = 0; c < 3; c++)
      {
        vLit[c] += std::clamp(rgb[c] * Light(vAmbient[c], vSun[c], d, bTwoSided, 0), 0.f, 1.f);
        vUnlit[c] += std::clamp(rgb[c] * 0.5f, 0.f, 1.f);
      }
    }

    //! The factor of each channel (1 where the unlit colours sum to 0).
    void Get(float vGain[3]) const
    {
      for (int c = 0; c < 3; c++)
        vGain[c] = vUnlit[c] > 0 ? (float)(vLit[c] / vUnlit[c]) : 1.0f;
    }
  };
}

#endif
