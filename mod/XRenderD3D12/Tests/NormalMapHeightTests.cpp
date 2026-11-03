// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: NormalMapHeightTests.cpp
//	Description: Unit tests of the height maps of bump mapping
//	(NormalMapHeight.h), on the CPU: which names are height maps, a
//	height map made a normal map, and a normal map with a height map's
//	slopes added as the original makes a material's bump and normal map
//	slots one texture (d3d12_NormalMapHeight): flat heights keep the
//	normal map, a slope tilts it the way and by the amount a height bump
//	map does, the height map repeats over the normal map, alpha the mean.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "TestMath.h"

#include "NormalMapHeight.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace XrTest;

namespace
{
  //! A w x h picture of B, G, R, A pixels, each from f(x, y).
  template <class F> std::vector<uint8_t> Picture(int w, int h, F f)
  {
    std::vector<uint8_t> p((size_t)w * h * 4);
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
        f(x, y, &p[((size_t)y * w + x) * 4]);
    return p;
  }

  //! A grey height map texel.
  void Grey(uint8_t *p, int v)
  {
    p[0] = p[1] = p[2] = (uint8_t)v;
    p[3] = 255;
  }

  //! A normal map texel of the unit vector (x, y, z).
  void Normal(uint8_t *p, float x, float y, float z)
  {
    p[0] = NormalMapHeight::Encode(z);
    p[1] = NormalMapHeight::Encode(y);
    p[2] = NormalMapHeight::Encode(x);
    p[3] = 255;
  }

  //! The vector of a normal map texel.
  void Decode(const uint8_t *p, float &x, float &y, float &z)
  {
    x = p[2] / 127.5f - 1;
    y = p[1] / 127.5f - 1;
    z = p[0] / 127.5f - 1;
  }
}

//! The normal maps' names (*_ddn*, *.ddn*) are no height maps, every other name is. Why: Research's cave rock names its
//! height map in the normal map slot ("cave_bump"); taken as a normal map, its grey made the bumps' normals noise.
XR_TEST(NormalMapHeight_Names)
{
  CHECK(!NormalMapHeight::IsHeightMapName("textures/glm/ww2style/cave/cave_wall12_ddn"));
  CHECK(!NormalMapHeight::IsHeightMapName("objects/x/rock.ddn.dds"));
  CHECK(!NormalMapHeight::IsHeightMapName("textures/white_ddn.dds"));
  CHECK(NormalMapHeight::IsHeightMapName("objects/natural/rocks/cave/cave_bump.dds"));
  CHECK(NormalMapHeight::IsHeightMapName("textures/fire_noise"));
}

//! A flat height map keeps the normal map (within a level of rounding) and its alpha is the mean of the normal map's and
//! the height. Why: only the slopes are the height map's; a height that moved the normals would tilt every such surface.
XR_TEST(NormalMapHeight_FlatKeepsNormals)
{
  const int w = 8, h = 8;
  const auto N = Picture(w, h, [](int x, int y, uint8_t *p) {
    const float a = 0.3f * x - 0.2f * y, s = std::sin(a) * 0.5f, c = std::cos(a) * 0.4f;
    const float z = std::sqrt(1 - s * s - c * c);
    Normal(p, s, c, z);
    p[3] = (uint8_t)(200 + x);
  });
  const auto H = Picture(4, 4, [](int, int, uint8_t *p) { Grey(p, 90); });
  std::vector<uint8_t> Out(N.size());
  NormalMapHeight::AddSlopes(N.data(), w, h, H.data(), 4, 4, Out.data());
  for (size_t i = 0; i < N.size(); i += 4)
  {
    for (int c = 0; c < 3; c++)
      CHECK(std::abs((int)Out[i + c] - (int)N[i + c]) <= 1);
    CHECK(Out[i + 3] == (N[i + 3] + 90 + 1) / 2);
  }
}

//! A height rising along x (6 levels a texel) and along y (8 levels) on a flat normal map tilts each normal to
//! normalize(-sx, -sy, 1), sx = 4 * 6 / 255 and sy = 4 * 8 / 255: the same normal a height bump map of it gets
//! (HeightToNormal), unit length. Why: the original's texture is the normal map tilted by the slopes its height bump
//! maps have; a slope of the wrong sign or amount moves the light on the rock to the other side of each bump.
XR_TEST(NormalMapHeight_Slopes)
{
  const int w = 16, h = 16;
  const auto H = Picture(w, h, [](int x, int y, uint8_t *p) { Grey(p, 10 + 6 * std::min(x, 14) + 8 * std::min(y, 14)); });
  const auto N = Picture(w, h, [](int, int, uint8_t *p) { Normal(p, 0, 0, 1); });
  std::vector<uint8_t> Out(N.size()), Bump(N.size());
  NormalMapHeight::AddSlopes(N.data(), w, h, H.data(), w, h, Out.data());
  NormalMapHeight::HeightToNormal(H.data(), w, h, Bump.data());
  const float sx = 4 * 6 / 255.0f, sy = 4 * 8 / 255.0f, l = std::sqrt(sx * sx + sy * sy + 1);
  for (int y = 2; y < 12; y++)
    for (int x = 2; x < 12; x++)
    {
      const uint8_t *p = &Out[((size_t)y * w + x) * 4];
      float nx, ny, nz;
      Decode(p, nx, ny, nz);
      CHECK_NEAR(nx, -sx / l, 0.012);
      CHECK_NEAR(ny, -sy / l, 0.012);
      CHECK_NEAR(nz, 1 / l, 0.012);
      CHECK_NEAR(std::sqrt(nx * nx + ny * ny + nz * nz), 1, 0.015);
      for (int c = 0; c < 3; c++)
        CHECK(std::abs((int)p[c] - (int)Bump[((size_t)y * w + x) * 4 + c]) <= 1);
    }
}

//! A height map smaller than the normal map repeats over it, texel by texel (not stretched): the result at (x + hw, y) and
//! (x, y + hh) is the one at (x, y) where the normal map is the same there. Why: the original's texture has the cave's
//! 256 x 256 height map twice in each direction over its 512 x 512 normal map (its alpha is that height map, repeated).
XR_TEST(NormalMapHeight_Repeats)
{
  const int w = 16, h = 16, hw = 4, hh = 4;
  const auto H = Picture(hw, hh, [](int x, int y, uint8_t *p) { Grey(p, (x * 37 + y * 91) % 200 + 20); });
  const auto N = Picture(w, h, [](int, int, uint8_t *p) { Normal(p, 0.1f, -0.2f, std::sqrt(1 - 0.05f)); });
  std::vector<uint8_t> Out(N.size());
  NormalMapHeight::AddSlopes(N.data(), w, h, H.data(), hw, hh, Out.data());
  for (int y = 0; y + hh < h; y++)
    for (int x = 0; x + hw < w; x++)
      for (int c = 0; c < 4; c++)
      {
        const uint8_t v = Out[((size_t)y * w + x) * 4 + c];
        CHECK(Out[((size_t)y * w + x + hw) * 4 + c] == v);
        CHECK(Out[((size_t)(y + hh) * w + x) * 4 + c] == v);
      }
  // Not flat: the slopes changed the normals somewhere.
  int nChanged = 0;
  for (size_t i = 0; i < N.size(); i += 4)
    nChanged += Out[i + 2] != N[i + 2] || Out[i + 1] != N[i + 1];
  CHECK(nChanged > 100);
}
