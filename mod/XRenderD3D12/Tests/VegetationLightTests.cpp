// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: VegetationLightTests.cpp
//	Description: Unit tests of the light baked into the vegetation's vertex
//	colours (VegetationLight.h): a face towards the sun, away from it, the
//	leaves lit on both sides, the self shadow; and the mean light its draws
//	the path tracer does not light take (d3d12_PTVegetationGain): the lit
//	colours over the unlit, weighted by the colours, clamped as stored.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "VegetationLight.h"

#include <cmath>
#include <string>

// Why: the bake as CLeafBuffer::UpdateCustomLighting had it (the vertex colours of every vegetation mesh: what the raster
// and the rays show): a face towards the sun half the ambient and the sun, away from it half the ambient alone; the leaves
// lit on both sides by half the ambient and half the sun whichever side faces it; the self shadow off the sun's light, never
// below half the ambient.
XR_TEST(Vegetation_Light)
{
  CHECK_NEAR(VegetationLight::Light(0.5f, 1, 1, false, 0), 0.25f + 1, 1e-6);
  CHECK_NEAR(VegetationLight::Light(0.5f, 1, 0.5f, false, 0), 0.25f + 0.5f, 1e-6);
  CHECK_NEAR(VegetationLight::Light(0.5f, 1, -0.7f, false, 0), 0.25f, 1e-6);
  CHECK_NEAR(VegetationLight::Light(0.5f, 1, 0.6f, true, 0), 0.25f + 0.5f * (0.5f + 0.6f), 1e-6);
  CHECK_NEAR(VegetationLight::Light(0.5f, 1, -0.6f, true, 0), VegetationLight::Light(0.5f, 1, 0.6f, true, 0), 1e-6);
  CHECK_NEAR(VegetationLight::Light(0.5f, 1, 1, false, 0.3f), 0.25f + 0.7f, 1e-6);
  CHECK_NEAR(VegetationLight::Light(0.5f, 1, 0.2f, false, 0.45f), 0.25f, 1e-6);
}

// Why: with the path tracer the vegetation is baked unlit (half the mesh's colour) and the paths light it; the draws they do
// not light (Carrier's water plants blended after the water, the water's reflection of the jungle) were unlit, dark beside the
// raster's. They take the light the colours had lit on the mean over the mesh: one vertex towards the sun ambient plus twice
// the sun over its unlit colour; the leaves twice the ambient plus the sun's |N.L|; each channel its own; the vertices weighted
// by their colour (a dark vertex counts less); the lit colours clamped at 1 as stored (a bright one saturates); none 1.
XR_TEST(Vegetation_Gain)
{
  const float Amb[3] = { 0.45f, 0.52f, 0.57f }, Sun[3] = { 1.0f, 0.95f, 0.8f }, Grey[3] = { 0.5f, 0.5f, 0.5f };
  float g[3];
  {
    VegetationLight::SGain G;
    G.Add(Grey, Amb, Sun, 1, false);
    G.Get(g);
    for (int c = 0; c < 3; c++)
      CHECK_MSG(std::fabs(g[c] - (Amb[c] + 2 * Sun[c])) < 1e-5f, std::to_string(c) + ": " + std::to_string(g[c]));
  }
  {
    VegetationLight::SGain G;
    G.Add(Grey, Amb, Sun, -0.4f, true);
    G.Get(g);
    for (int c = 0; c < 3; c++)
      CHECK_MSG(std::fabs(g[c] - (2 * Amb[c] + Sun[c] * 0.4f)) < 1e-5f, std::to_string(c) + ": " + std::to_string(g[c]));
  }
  {
    // Two vertices, one towards the sun and one away: weighted by their colours (0.8 and 0.2).
    const float Bright[3] = { 0.8f, 0.8f, 0.8f }, Dark[3] = { 0.2f, 0.2f, 0.2f };
    VegetationLight::SGain G;
    G.Add(Bright, Amb, Sun, 1, false);
    G.Add(Dark, Amb, Sun, -1, false);
    G.Get(g);
    for (int c = 0; c < 3; c++)
    {
      const double fLit = std::min(0.8 * (0.5 * Amb[c] + Sun[c]), 1.0) + 0.2 * 0.5 * Amb[c];
      CHECK_MSG(std::fabs(g[c] - fLit / 0.5) < 1e-5f, std::to_string(c) + ": " + std::to_string(g[c]));
    }
    CHECK(g[0] < 2 * (0.5f * Amb[0] + Sun[0]));
  }
  {
    // A white vertex towards the sun: its lit colour stored as 1 (1.08-1.23 before the clamp), the gain 2.
    const float White[3] = { 1, 1, 1 };
    VegetationLight::SGain G;
    G.Add(White, Amb, Sun, 1, false);
    G.Get(g);
    for (int c = 0; c < 3; c++)
      CHECK_NEAR(g[c], 2, 1e-6);
  }
  {
    VegetationLight::SGain G;
    G.Get(g);
    for (int c = 0; c < 3; c++)
      CHECK_NEAR(g[c], 1, 1e-9);
  }
}

// Why: with the path tracer a leaf under a crown that hid the sky was near black, a third of the raster's (Swamp's path, River's
// temple: the bake lights the leaves on both sides with twice the ambient, unshadowed). d3d12_PTLeafLight lights it no darker
// than the raster: General.hlsl takes the ambient term plus the sun term times N.L (|N.L| on both sides) for its least light.
// These terms are twice the bake's light without the self shadow for every N.L, on both sides and on one.
XR_TEST(Vegetation_RasterTerms)
{
  for (const bool bTwoSided : { true, false })
    for (const float fAmbient : { 0.0f, 0.3f, 0.52f, 1.0f })
      for (const float fSun : { 0.0f, 0.45f, 1.2f })
        for (const float d : { -1.0f, -0.6f, -0.1f, 0.0f, 0.25f, 0.8f, 1.0f })
        {
          float fAmbientTerm, fSunTerm;
          VegetationLight::RasterTerms(fAmbient, fSun, bTwoSided, fAmbientTerm, fSunTerm);
          const float fRaster = fAmbientTerm + fSunTerm * (bTwoSided ? std::fabs(d) : std::max(d, 0.0f));
          CHECK_MSG(std::fabs(fRaster - 2 * VegetationLight::Light(fAmbient, fSun, d, bTwoSided, 0)) < 1e-5f,
                    std::to_string(bTwoSided) + " " + std::to_string(fAmbient) + " " + std::to_string(fSun) + " " + std::to_string(d));
        }
  // A leaf in the shade on both sides: twice the ambient, as the raster's (the path tracer's paths alone lit it ~0.33 of that).
  float a, s;
  VegetationLight::RasterTerms(0.45f, 1, true, a, s);
  CHECK_NEAR(a, 0.9f, 1e-6);
  CHECK_NEAR(s, 1, 1e-6);
}
