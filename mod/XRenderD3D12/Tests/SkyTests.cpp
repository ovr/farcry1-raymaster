// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: SkyTests.cpp
//	Description: Unit tests of the fog band over the sky box's lower part
//	(d3d12_SkyFogBand): SkyFogBand.h, which DrawSkyItem draws by, against
//	the heights of the band's edge the D3D9 renderer drew (its draws, seen
//	on Swamp and Volcano at several camera heights and with the fog's end
//	changed over a fixed camera), and Shaders/SkyFogBand.hlsli
//	(TestHeaders.hlsl's CSSkyFogBand) on the GPU, which the water's
//	reflected sky goes by, against SkyFogBand.h.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "GpuHarness.h"
#include "TestMath.h"
#include "SkyFogBand.h"

#include "Test_SkyFogBand.h"

#include <cmath>
#include <vector>

using namespace XrTest;

namespace
{
  //! A frame the D3D9 renderer drew the band in: the frame's fog's end, the camera's height, the z of the band's edge.
  struct SBandSeen
  {
    float fFogEnd, fCameraZ, fEdge;
  };

  //! A direction for CSSkyFogBand: the fog's end, the camera's height, the height the direction reaches on the ring, its
  //! turn about z (radians).
  struct SBandIn
  {
    float fFogEnd, fCameraZ, z, fTurn;
  };

  //! CSSkyFogBand: per input the edge and the band's alpha over the box in that direction.
  std::vector<std::pair<float, float>> Run(const std::vector<SBandIn> &In)
  {
    SDispatch d;
    d.Shader = XR_BYTECODE(Test_SkyFogBand);
    d.Test.nTest[0] = (uint32)In.size();
    d.nGroups[0] = Groups64(In.size());
    for (const SBandIn &b : In)
      Push(d.In, b.fFogEnd, b.fCameraZ, b.z, b.fTurn);
    d.nOut = In.size();
    const std::vector<uint32> Out = Gpu().Run(d);
    std::vector<std::pair<float, float>> R;
    for (size_t i = 0; i < In.size(); i++)
      R.push_back({ At(Out, i, 0), At(Out, i, 1) });
    return R;
  }
}

// Why: Swamp's sky box (and Volcano's) is cut into rectangles near the horizon without the band: its bottom and the
// edges between its sides show there, which the D3D9 renderer covers with the fog's colour. Where it puts the band's
// edge decides how much of the box shows; these are the heights it drew (its vertices' z, to 6 digits): Swamp's own fog
// (ending at 150 m) with the camera from 42 to 140 m, Volcano's (120 m) from 63 to 141 m, and over Swamp with the
// camera at 64 m (140 at 250 m) the fog's end from 40 to 255 m, by DemoSkyFog; and the mirrored camera of Swamp's water
// reflection, below z = 0 (taken as 0) and above.
XR_TEST(SkyFogBand_D3D9Edges)
{
  static const SBandSeen s_Seen[] =
  {
    { 150, 64.0f, -0.161905f }, { 150, 61.833f, -0.1f }, { 150, 88.834f, -0.871429f }, { 150, 140, -2.33333f }, { 150, 42.167f, 0.461905f },
    { 120, 64.4f, 4.82667f }, { 120, 63.07f, 4.8647f }, { 120, 88.799f, 4.12952f }, { 120, 140.833f, 2.64286f }, { 120, 66.433f, 4.76857f },
    { 40, 64, 14.1714f }, { 60, 64, 14.1714f }, { 70, 64, 13.1714f }, { 100, 64, 8.17143f }, { 140, 64, 1.50476f },
    { 180, 64, -5.16191f }, { 200, 64, -8.49524f }, { 220, 64, -11.8286f }, { 250, 64, -16.8286f }, { 255, 64, -17.6619f },
    { 250, 140, -19.0f },
    { 150, -14, 1.66667f }, { 150, 7.833f, 1.44286f },
  };
  for (const SBandSeen &s : s_Seen)
  {
    CHECK(SkyFogBand::Drawn(s.fFogEnd));
    CHECK_NEAR(SkyFogBand::Edge(s.fFogEnd, s.fCameraZ), s.fEdge, 2e-4);
  }
}

// Why: the D3D9 renderer draws the band only with the fog ending before 256 m: at 255 m it is there, at 256 m and on
// (and on Training at 700, Pier at 900, River at 300 m) the box alone. A band beyond would lie over the far ground's
// own fog and the sky's lower part where the original shows the box.
XR_TEST(SkyFogBand_D3D9Drawn)
{
  for (const float f : { 40.0f, 120.0f, 150.0f, 250.0f, 255.0f, 255.9f })
    CHECK(SkyFogBand::Drawn(f));
  for (const float f : { 256.0f, 257.0f, 260.0f, 270.0f, 280.0f, 290.0f, 300.0f, 700.0f, 900.0f })
    CHECK(!SkyFogBand::Drawn(f));
}

// Why: the water reflects the sky by SkyFogBand.hlsli, the sky itself is drawn by SkyFogBand.h: if the two put the
// band's edge in different places the reflected horizon shows a seam (or the box's edges again). And the band's alpha
// over the box must be the drawn band's: 1 from 32 below its edge up to it, falling evenly to 0 over the 16 above
// (its vertices' alpha, interpolated along z on the ring), the same all round; none with the fog ending from 256 m on.
XR_TEST(SkyFogBand_Shader)
{
  std::vector<SBandIn> In;
  for (const float fFogEnd : { 30.0f, 64.0f, 120.0f, 150.0f, 200.0f, 255.0f, 256.0f, 700.0f })
    for (const float fCameraZ : { -20.0f, 0.0f, 25.0f, 64.0f, 140.0f })
    {
      const float fEdge = SkyFogBand::Edge(fFogEnd, fCameraZ);
      for (const float dz : { -40.0f, -32.5f, -31.5f, -10.0f, 0.0f, 4.0f, 8.0f, 12.0f, 16.0f, 20.0f })
        for (const float fTurn : { 0.0f, 1.0f, 3.5f })
          In.push_back({ fFogEnd, fCameraZ, fEdge + dz, fTurn });
    }
  const auto R = Run(In);
  for (size_t i = 0; i < In.size(); i++)
  {
    const SBandIn &b = In[i];
    if (!SkyFogBand::Drawn(b.fFogEnd))
    {
      CHECK(R[i].first < -1e8f);
      CHECK_NEAR(R[i].second, 0, 0);
      continue;
    }
    const float fEdge = SkyFogBand::Edge(b.fFogEnd, b.fCameraZ);
    CHECK_NEAR(R[i].first, fEdge, 1e-4);
    const float dz = b.z - fEdge;
    const float fAlpha = dz < -SkyFogBand::BELOW ? 0 : std::clamp(1 - dz / SkyFogBand::FADE, 0.0f, 1.0f);
    CHECK_NEAR(R[i].second, fAlpha, 1e-4);
  }
}
