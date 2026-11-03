// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: NormalMapHeight.h
//	Description: Height maps in the normal maps of bump mapping
//	(Texture.cpp; the unit tests). A bump map whose name is no normal
//	map's (*_ddn*, *.ddn*) is a height map: it is made a normal map,
//	n = normalize(-sx, -sy, 1) with the height in alpha, the slopes 4
//	times the height's forward differences (along x the mean of rows y
//	and y + 1), wrapping at the edges. A material may have a normal map
//	in its bump slot (EFTT_BUMP) and a height map in its normal map slot
//	(EFTT_NORMALMAP, the material's displacement map; Research's cave
//	rock): the original makes them one texture, which its frames show
//	(traced: the same to the 8-bit level): the height map's slopes added
//	to the normal map's x and y, n = normalize(N.x - sx, N.y - sy, N.z),
//	the height map repeating over the normal map's texels, alpha the mean
//	of the normal map's and the height. Pixels are B, G, R, A bytes, rows
//	from the top; without the renderer's types.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_NORMALMAPHEIGHT_H
#define XRENDERD3D12_NORMALMAPHEIGHT_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace NormalMapHeight
{
  //! A texture name (lower case, '/' separated) that is no normal map's: a bump map of that name is a height map.
  inline bool IsHeightMapName(std::string_view Name)
  {
    return Name.find("_ddn") == std::string_view::npos && Name.find(".ddn") == std::string_view::npos;
  }

  //! A w x h height map of B, G, R, A pixels: the height of texel (x, y) (wrapping) as a byte, the mean of b, g and r.
  inline int HeightByte(const uint8_t *pHeight, int w, int h, int x, int y)
  {
    const uint8_t *p = pHeight + ((size_t)(y % h) * w + x % w) * 4;
    return (p[0] + p[1] + p[2]) / 3;
  }

  //! The slopes of the height map at texel (x, y): 4 times the forward differences of the height (0 to 1), along x the
  //! mean over rows y and y + 1.
  inline void Slopes(const uint8_t *pHeight, int w, int h, int x, int y, float &sx, float &sy)
  {
    auto At = [&](int u, int v) { return HeightByte(pHeight, w, h, u, v) / 255.0f; };
    sx = 4 * 0.5f * ((At(x + 1, y) - At(x, y)) + (At(x + 1, y + 1) - At(x, y + 1)));
    sy = 4 * (At(x, y + 1) - At(x, y));
  }

  //! A unit vector's component as a byte (-1 to 0, 1 to 255).
  inline uint8_t Encode(float v)
  {
    return (uint8_t)std::clamp(std::lround(v * 127.5f + 127.5f), 0L, 255L);
  }

  //! A w x h height map made a normal map: pOut w x h pixels.
  inline void HeightToNormal(const uint8_t *pHeight, int w, int h, uint8_t *pOut)
  {
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
      {
        float sx, sy;
        Slopes(pHeight, w, h, x, y, sx, sy);
        const float fInvLen = 1 / sqrtf(sx * sx + sy * sy + 1);
        uint8_t *p = pOut + ((size_t)y * w + x) * 4;
        p[0] = Encode(fInvLen);
        p[1] = Encode(-sy * fInvLen);
        p[2] = Encode(-sx * fInvLen);
        p[3] = (uint8_t)HeightByte(pHeight, w, h, x, y);
      }
  }

  //! The w x h normal map pNormal with the slopes of the hw x hh height map pHeight added, the height map repeating over
  //! the normal map's texels: pOut w x h pixels (may not be pNormal).
  inline void AddSlopes(const uint8_t *pNormal, int w, int h, const uint8_t *pHeight, int hw, int hh, uint8_t *pOut)
  {
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
      {
        float sx, sy;
        Slopes(pHeight, hw, hh, x, y, sx, sy);
        const size_t i = ((size_t)y * w + x) * 4;
        const float nx = pNormal[i + 2] / 127.5f - 1 - sx, ny = pNormal[i + 1] / 127.5f - 1 - sy, nz = pNormal[i] / 127.5f - 1;
        const float fLen = sqrtf(nx * nx + ny * ny + nz * nz);
        if (fLen > 1e-6f)
        {
          pOut[i] = Encode(nz / fLen);
          pOut[i + 1] = Encode(ny / fLen);
          pOut[i + 2] = Encode(nx / fLen);
        }
        else
        {
          pOut[i] = 255;
          pOut[i + 1] = pOut[i + 2] = 128;
        }
        pOut[i + 3] = (uint8_t)((pNormal[i + 3] + HeightByte(pHeight, hw, hh, x, y) + 1) / 2);
      }
  }
}

#endif
