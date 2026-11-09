// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: SpriteOrder.h
//	Description: The order the scene's sprites (the particles,
//	Draw3D.cpp's sprite pass) are drawn in (d3d12_SpriteSort): back to
//	front by the distance of the centre of each one's vertices from the
//	camera, those at the same distance as the engine gave them; off, as
//	the engine gave them. The original draws its particles so (its trace
//	of Volcano's lava: the runs of smoke and of heat haze one after the
//	other from 92 m to 40 m). Their order matters where the heat haze
//	lays the scene copied before the particles over what is behind it:
//	in the engine's order a haze drawn after a nearer smoke wiped it out,
//	and each burst of new particles (every 5 frames) changed how much of
//	the smoke was left, a step brighter each time. Unit tests:
//	SpriteOrderTests.cpp.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_SPRITEORDER_H
#define XRENDERD3D12_SPRITEORDER_H

#include <algorithm>
#include <numeric>
#include <span>
#include <vector>

namespace SpriteOrder
{
//! A sprite's distance for the order: the centre of its vertices' positions (x, y, z each nStride floats apart
//! from pPos) from the camera (fCamX, fCamY, fCamZ), squared; 0 without vertices.
inline float CentreDistance2(const float *pPos, int nVerts, int nStride, float fCamX, float fCamY, float fCamZ)
{
  if (!pPos || nVerts <= 0)
    return 0;
  double x = 0, y = 0, z = 0;
  for (int i = 0; i < nVerts; i++)
  {
    const float *p = pPos + (size_t)i * nStride;
    x += p[0];
    y += p[1];
    z += p[2];
  }
  const double dx = x / nVerts - fCamX, dy = y / nVerts - fCamY, dz = z / nVerts - fCamZ;
  return (float)(dx * dx + dy * dy + dz * dz);
}

//! The order to draw the sprites in, their indices into Order: bSort back to front by Dist (the farthest first,
//! equal ones as given), otherwise as given.
inline void BackToFront(std::span<const float> Dist, bool bSort, std::vector<int> &Order)
{
  Order.resize(Dist.size());
  std::iota(Order.begin(), Order.end(), 0);
  if (bSort)
    std::stable_sort(Order.begin(), Order.end(), [&](int a, int b) { return Dist[a] > Dist[b]; });
}
}

#endif
