// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: CascadeCull.h
//	Description: What of the sun's cascades' casters the renderer can
//	leave out early (d3d12_CascadeCull, Cascades.cpp). Reaches: whether a
//	part's sphere reaches a cascade's slice, where its shadow falls on the
//	slice's receivers (DrawCascadeItems draws it there; without rays
//	CScene::AddEf leaves out the parts that reach none). CasterFar: how far
//	from the view, along the sun's direction, a caster can stand and still
//	shade a receiver. A caster P shades a receiver R only along the sun,
//	P = R + t * Z (Z towards the sun, t >= 0), so P.z - R.z = t * Z.z: with
//	P no higher than the highest caster and R no lower than the lowest
//	receiver, P's depth along Z is at most R's plus their heights'
//	difference over Z.z. The camera towards the sun ends there instead of
//	at the main view's far plane. The unit tests (CascadeCullTests.cpp)
//	hold both to that.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_CASCADECULL_H
#define XRENDERD3D12_CASCADECULL_H

#include <algorithm>
#include <cmath>

namespace CascadeCull
{
  constexpr float LOW_SUN_Z = 0.17364818f;  //!< sin 10 degrees: a sun lower than this keeps the view's far plane
  constexpr float FAR_MARGIN_M = 10;        //!< beyond the bound (m): the camera's direction and the engine's boxes
  constexpr float OBJECT_RISE_M = 100;      //!< how far above the level's highest terrain a caster may reach (m)

  //! Whether a part's sphere (centre x, y, z along the sun's axes, radius r; r < 0: no box, it reaches all) reaches
  //! the cascade Cas: across its square, not all of it farther from the sun than the slice's range, and its shadow
  //! (the sphere drawn out away from the sun) over the outline of the slice's receivers with their margin.
  template <class TCascade>
  inline bool Reaches(const TCascade &Cas, float x, float y, float z, float r)
  {
    if (r < 0)
      return true;
    if (fabsf(x - Cas.fCenter[0]) > Cas.fHalfSize + r || fabsf(y - Cas.fCenter[1]) > Cas.fHalfSize + r || z + r < Cas.fZMin)
      return false;
    for (int e = 0; e < Cas.nEdges; e++)
      if (Cas.Edges[e][0] * x + Cas.Edges[e][1] * y - Cas.Edges[e][2] > r + Cas.fMargin)
        return false;
    return true;
  }

  //! The far plane of the camera towards the sun: fViewFar (the main view's) for a low sun (fSunZ, the sun's
  //! direction's z, below LOW_SUN_Z), else the depth along it beyond which no caster no higher than fCasterTop
  //! shades a receiver no lower than fReceiverLow whose own depth is at most fReceiverDepth (all m; depths from the
  //! camera along the sun's direction), and FAR_MARGIN_M; at least 1, at most fViewFar.
  inline float CasterFar(float fViewFar, float fSunZ, float fReceiverDepth, float fReceiverLow, float fCasterTop)
  {
    const float fView = std::max(fViewFar, 1.0f);
    if (!(fSunZ >= LOW_SUN_Z))
      return fView;
    const float fFar = fReceiverDepth + std::max(fCasterTop - fReceiverLow, 0.0f) / fSunZ + FAR_MARGIN_M;
    return std::clamp(fFar, 1.0f, fView);
  }
}

#endif
