// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: TaaTests.cpp
//	Description: Unit tests of TAA (d3d12_AA taa, UpscalerTaa.cpp) on the
//	GPU: Shaders/Taa.hlsli as Taa.hlsl's kernels use it (TestTaa.hlsl), on
//	small pictures, frame after frame with each frame's picture the next
//	one's history as the renderer's history targets keep it, and its
//	jitter (Upscaler.h's Halton).
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "GpuHarness.h"
#include "TestMath.h"
#include "Upscaler.h"

#include "Test_TaaResolve.h"
#include "Test_TaaDilate.h"
#include "Test_TaaSharpen.h"
#include "Test_TaaWorking.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

using namespace XrTest;

namespace
{
  constexpr uint32 NO_HISTORY = 1, HDR = 2;
  //! TAA's least shares at d3d12_AAQuality 2 (UpscalerTaa.cpp's s_LeastShare, s_MovingShare).
  constexpr float HIGH = 1 / 10.0f, HIGH_MOVING = 1.5f / 10;

  //! A picture of w x h texels of four floats, row by row.
  struct SImage
  {
    int w = 0, h = 0;
    std::vector<float> v;
    SImage() = default;
    SImage(int nw, int nh, float r = 0, float g = 0, float b = 0, float a = 0) : w(nw), h(nh)
    {
      for (int i = 0; i < w * h; i++)
        v.insert(v.end(), { r, g, b, a });
    }
    float *At(int x, int y) { return &v[((size_t)y * w + x) * 4]; }
    const float *At(int x, int y) const { return &v[((size_t)y * w + x) * 4]; }
  };

  STestTexture Texture(const SImage &Image)
  {
    STestTexture t;
    t.nWidth = (uint32)Image.w;
    t.nHeight = (uint32)Image.h;
    t.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    t.Data.resize(Image.v.size() * sizeof(float));
    memcpy(t.Data.data(), Image.v.data(), t.Data.size());
    return t;
  }

  //! What TaaResolve gave a pixel (CSTaaResolve).
  struct STaa
  {
    SImage Picture;               //!< rgb the colour, a the new history's alpha: the next frame's history
    std::vector<float> Share, Clip;
  };

  //! The frame's resolve over the whole picture.
  STaa Resolve(const SImage &Frame, const SImage &Depth, const SImage &Motion, const SImage &History, uint32 nFlags,
               float fLeastShare = HIGH, float fExposure = 0, float fMovingShare = HIGH_MOVING)
  {
    SDispatch d;
    d.Shader = XR_BYTECODE(Test_TaaResolve);
    const size_t n = (size_t)Frame.w * Frame.h;
    d.Test.nTest[0] = (uint32)n;
    d.Test.nTest[1] = (uint32)Frame.w;
    d.Test.nTest[2] = (uint32)Frame.h;
    d.Test.nTest[3] = nFlags;
    d.Test.vTest[0] = fLeastShare;
    d.Test.vTest[1] = fExposure;
    d.Test.vTest[3] = fMovingShare;
    d.nGroups[0] = Groups64(n);
    d.Textures = { Texture(Frame), Texture(Depth), Texture(Motion), Texture(History) };
    d.nOut = 2 * n;
    const std::vector<uint32> Out = Gpu().Run(d);
    STaa r;
    r.Picture = SImage(Frame.w, Frame.h);
    for (size_t i = 0; i < n; i++)
    {
      for (int k = 0; k < 4; k++)
        r.Picture.v[i * 4 + k] = At(Out, i, k);
      r.Share.push_back(At(Out, n + i, 0));
      r.Clip.push_back(At(Out, n + i, 1));
    }
    return r;
  }

  //! The share of the square x..x+1, y..y+1 on the white side (bWhite of a point), by 32 x 32 samples.
  float Coverage(int x, int y, const std::function<bool(double, double)> &bWhite)
  {
    int n = 0;
    for (int j = 0; j < 32; j++)
      for (int i = 0; i < 32; i++)
        n += bWhite(x + (i + 0.5) / 32, y + (j + 0.5) / 32) ? 1 : 0;
    return n / 1024.0f;
  }

  //! The frame of a scene drawn without anti-aliasing, moved by the jitter (jx, jy) as the renderer moves it (right and
  //! down): pixel (x, y) shows the scene at its centre minus the jitter.
  SImage Draw(int w, int h, double jx, double jy, const std::function<bool(double, double)> &bWhite)
  {
    SImage f(w, h);
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
      {
        const float v = bWhite(x + 0.5 - jx, y + 0.5 - jy) ? 1.0f : 0.0f;
        float *p = f.At(x, y);
        p[0] = p[1] = p[2] = v;
        p[3] = 1;
      }
    return f;
  }

  //! TAA's jitter of the frame n (UpscalerTaa.cpp's GetJitter).
  void Jitter(int n, double &x, double &y)
  {
    x = Halton(n % 8 + 1, 2) - 0.5;
    y = Halton(n % 8 + 1, 3) - 0.5;
  }

  //! The mean difference of the red from the exact coverage away from the borders.
  double Error(const SImage &Picture, const std::function<bool(double, double)> &bWhite)
  {
    double fSum = 0;
    int n = 0;
    for (int y = 2; y < Picture.h - 2; y++)
      for (int x = 2; x < Picture.w - 2; x++)
      {
        fSum += fabs(Picture.At(x, y)[0] - Coverage(x, y, bWhite));
        n++;
      }
    return fSum / n;
  }

  //! nFrames of a still scene through TAA, the first without history: the last picture.
  SImage Gather(int w, int h, int nFrames, float fLeastShare, const std::function<bool(double, double)> &bWhite)
  {
    const SImage Depth(w, h, 0.5f), Still(w, h);
    SImage History(w, h);
    for (int n = 0; n < nFrames; n++)
    {
      double jx, jy;
      Jitter(n, jx, jy);
      History = Resolve(Draw(w, h, jx, jy, bWhite), Depth, Still, History, n == 0 ? NO_HISTORY : 0, fLeastShare).Picture;
    }
    return History;
  }

  //! A picture with detail everywhere (no two neighbours alike), shifted by dx pixels: its pixel x shows x - dx.
  SImage Pattern(int w, int h, int dx)
  {
    SImage f(w, h);
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
      {
        const int u = x - dx;
        float *p = f.At(x, y);
        p[0] = 0.5f + 0.4f * sinf(1.7f * u + 0.9f * y);
        p[1] = 0.5f + 0.4f * cosf(1.1f * u - 1.3f * y);
        p[2] = ((u * 7 + y * 3) & 7) / 8.0f;
        p[3] = 1;
      }
    return f;
  }

  double MaxDifference(const SImage &a, const SImage &b, int nFromX = 0)
  {
    double d = 0;
    for (int y = 0; y < a.h; y++)
      for (int x = nFromX; x < a.w; x++)
        for (int k = 0; k < 3; k++)
          d = std::max(d, (double)fabs(a.At(x, y)[k] - b.At(x, y)[k]));
    return d;
  }
}

//! The eight jitter phases are spread over the pixel: all of them inside it, no two alike, their mean near its centre.
//! Why: the picture gathers the pixel's area only if the frames sample all of it; a phase outside it would blur the
//! picture, two alike would waste a frame.
XR_TEST(Taa_JitterCoversThePixel)
{
  double fMeanX = 0, fMeanY = 0;
  for (int i = 0; i < 8; i++)
  {
    double x, y;
    Jitter(i, x, y);
    CHECK(x >= -0.5 && x < 0.5 && y >= -0.5 && y < 0.5);
    fMeanX += x / 8;
    fMeanY += y / 8;
    for (int j = 0; j < i; j++)
    {
      double u, v;
      Jitter(j, u, v);
      CHECK(fabs(u - x) + fabs(v - y) > 0.05);
    }
  }
  CHECK(fabs(fMeanX) < 0.06 && fabs(fMeanY) < 0.06);
  CHECK_NEAR(Halton(1, 2), 0.5, 1e-6);
  CHECK_NEAR(Halton(3, 2), 0.75, 1e-6);
  CHECK_NEAR(Halton(4, 3), 4 / 9.0, 1e-6);
}

//! A still edge, jittered frame after frame, comes out near its exact coverage, at every quality: much nearer than one
//! frame drawn without anti-aliasing. Why: this is what TAA is for; the frames sample the pixel at its eight phases and
//! the history averages them, as a picture supersampled would.
XR_TEST(Taa_StillEdgeGathersTheFrames)
{
  const auto bWhite = [](double x, double y) { return y < 0.37 * x + 4.3; };
  const int w = 32, h = 24;
  const double fAliased = Error(Draw(w, h, 0, 0, bWhite), bWhite);
  const float Shares[4] = { 1 / 5.0f, 1 / 7.0f, 1 / 10.0f, 1 / 14.0f };
  for (int q = 0; q < 4; q++)
  {
    const double fTaa = Error(Gather(w, h, 48, Shares[q], bWhite), bWhite);
    CHECK_MSG(fTaa < 0.5 * fAliased, "quality " + std::to_string(q) + ": " + std::to_string(fTaa) + " against " + std::to_string(fAliased));
  }
}

//! Without history the picture is the frame as it is, its history's alpha one frame, the frame's share 1. Why: after a
//! jump of the camera, a new level or the menu the history shows something else; nothing of it may stay.
XR_TEST(Taa_ResetTakesTheFrame)
{
  const int w = 12, h = 10;
  const SImage Frame = Pattern(w, h, 0), Depth(w, h, 0.5f), Still(w, h), Stale(w, h, 1, 0, 1, 1);
  const STaa r = Resolve(Frame, Depth, Still, Stale, NO_HISTORY);
  CHECK(MaxDifference(r.Picture, Frame) < 1e-6);
  for (size_t i = 0; i < r.Share.size(); i++)
  {
    CHECK_NEAR(r.Picture.v[i * 4 + 3], 1 / 32.0, 1e-6);
    CHECK_NEAR(r.Share[i], 1, 1e-6);
  }
}

//! A picture moving 2 pixels a frame keeps its history with its motion vectors: the history from where each pixel was
//! agrees with the frame, so the picture is the frame (no blur, no ghost), but not without them. Why: the camera and the
//! objects move all the time; the history must be fetched from where the pixel was.
XR_TEST(Taa_HistoryFollowsTheMotion)
{
  const int w = 24, h = 12;
  const SImage Before = Pattern(w, h, 4), Now = Pattern(w, h, 6), Depth(w, h, 0.5f), Still(w, h);
  SImage History = Before;
  for (size_t i = 3; i < History.v.size(); i += 4)
    History.v[i] = 1;
  // Where each pixel was in the frame before minus where it is: 2 pixels to the left.
  const SImage Moving(w, h, -2.0f / w, 0);
  const double fWith = MaxDifference(Resolve(Now, Depth, Moving, History, 0).Picture, Now);
  const double fWithout = MaxDifference(Resolve(Now, Depth, Still, History, 0).Picture, Now, 2);
  CHECK_MSG(fWith < 1e-3, std::to_string(fWith));
  CHECK_MSG(fWithout > 20 * std::max(fWith, 1e-4), std::to_string(fWithout) + " against " + std::to_string(fWith));
}

//! A history the frame's neighbourhood does not have goes: white history over a black frame comes out black (clipped),
//! a history that agrees is kept (not clipped). Why: an uncovered background, a light switched off must not leave a
//! ghost of what was there.
XR_TEST(Taa_StaleHistoryIsClipped)
{
  const int w = 8, h = 8;
  const SImage Depth(w, h, 0.5f), Still(w, h);
  const STaa Black = Resolve(SImage(w, h, 0, 0, 0, 1), Depth, Still, SImage(w, h, 1, 1, 1, 1), 0);
  const STaa Grey = Resolve(SImage(w, h, 0.5f, 0.5f, 0.5f, 1), Depth, Still, SImage(w, h, 0.5f, 0.5f, 0.5f, 1), 0);
  for (size_t i = 0; i < Black.Share.size(); i++)
  {
    CHECK(Black.Picture.v[i * 4] < 1e-4f);
    CHECK(Black.Clip[i] > 0.9f);
    CHECK_NEAR(Grey.Picture.v[i * 4], 0.5, 1e-4);
    CHECK(Grey.Clip[i] < 1e-4f);
    // The history holds 32 frames: the frame's share the least.
    CHECK_NEAR(Grey.Share[i], HIGH, 1e-4);
  }
}

//! The motion of a pixel is that of the nearest of its 3 x 3: the pixels around a near one move with it. Why: an
//! object's anti-aliased edge covers pixels whose own vector is the background's; they must move with the object, or
//! its edge smears.
XR_TEST(Taa_MotionOfTheNearest)
{
  const int w = 10, h = 9;
  SImage Depth(w, h, 0.9f), Velocity(w, h, 0.01f, 0.02f);
  Depth.At(5, 4)[0] = 0.1f;
  Velocity.At(5, 4)[0] = 0.3f;
  Velocity.At(5, 4)[1] = -0.2f;
  SDispatch d;
  d.Shader = XR_BYTECODE(Test_TaaDilate);
  const size_t n = (size_t)w * h;
  d.Test.nTest[0] = (uint32)n;
  d.Test.nTest[1] = (uint32)w;
  d.Test.nTest[2] = (uint32)h;
  d.nGroups[0] = Groups64(n);
  d.Textures = { Texture(SImage(w, h)), Texture(Depth), Texture(Velocity), Texture(SImage(w, h)) };
  d.nOut = n;
  const std::vector<uint32> Out = Gpu().Run(d);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++)
    {
      const bool bNear = abs(x - 5) <= 1 && abs(y - 4) <= 1;
      const size_t i = (size_t)y * w + x;
      CHECK_NEAR(At(Out, i, 0), bNear ? 0.3 : 0.01, 1e-6);
      CHECK_NEAR(At(Out, i, 1), bNear ? -0.2 : 0.02, 1e-6);
    }
}

//! A pixel that was off the picture in the frame before has no history: the frame alone. Why: the clamped history there
//! is the border's, not the pixel's.
XR_TEST(Taa_HistoryOffThePictureIsIgnored)
{
  const int w = 8, h = 8;
  const SImage Frame = Pattern(w, h, 0), Depth(w, h, 0.5f), Away(w, h, -1.5f, 0);
  const STaa r = Resolve(Frame, Depth, Away, SImage(w, h, 1, 1, 1, 1), 0);
  CHECK(MaxDifference(r.Picture, Frame) < 1e-6);
  for (float s : r.Share)
    CHECK_NEAR(s, 1, 1e-6);
}

//! The HDR light goes into TAA's space and back unchanged at any exposure, into 0..1, and LDR colours stay as they are;
//! a still light over its own history stays that light. Why: the light TAA gives is tone mapped after it; a change of
//! its level would change the picture's brightness.
XR_TEST(Taa_HdrLightRoundTrip)
{
  const float Values[] = { 0, 0.001f, 0.25f, 1, 3, 10, 100, 1000 };
  SDispatch d;
  d.Shader = XR_BYTECODE(Test_TaaWorking);
  const size_t n = sizeof(Values) / sizeof(Values[0]);
  for (float v : Values)
    Push(d.In, v, 0.5f * v, 0.1f * v, 0);
  d.Test.nTest[0] = (uint32)n;
  d.nGroups[0] = Groups64(n);
  d.nOut = 2 * n;
  for (const float e : { 0.0f, 0.7f, 4.0f })
  {
    d.Test.nTest[3] = e > 0 ? HDR : 0;
    d.Test.vTest[1] = e;
    const std::vector<uint32> Out = Gpu().Run(d);
    for (size_t i = 0; i < n; i++)
    {
      const float v = Values[i];
      if (e > 0)
        CHECK(At(Out, i, 0) >= 0 && At(Out, i, 0) < 1);
      else
        CHECK_NEAR(At(Out, i, 0), v, 1e-6);
      CHECK_NEAR(At(Out, n + i, 0), v, 1e-3 * std::max(v, 1.0f));
      CHECK_NEAR(At(Out, n + i, 1), 0.5f * v, 1e-3 * std::max(v, 1.0f));
    }
  }
  const int w = 6, h = 6;
  const STaa r = Resolve(SImage(w, h, 3, 2, 1, 1), SImage(w, h, 0.5f), SImage(w, h), SImage(w, h, 3, 2, 1, 1), HDR, HIGH, 0.7f);
  for (size_t i = 0; i < r.Share.size(); i++)
  {
    CHECK_NEAR(r.Picture.v[i * 4], 3, 1e-3);
    CHECK_NEAR(r.Picture.v[i * 4 + 2], 1, 1e-3);
  }
}

//! A highlight of one frame in one pixel (a spark, a sun glint) weighs as a white in the HDR light: the picture there
//! stays near its history, far below the frame's linear share of it, and its neighbours stay as they were. Why: a
//! single bright sample would otherwise flash through the history for frames.
XR_TEST(Taa_HighlightOfOneFrameWeighsLess)
{
  const int w = 7, h = 7;
  SImage Frame(w, h, 0.5f, 0.5f, 0.5f, 1);
  float *c = Frame.At(3, 3);
  c[0] = c[1] = c[2] = 50;
  const STaa r = Resolve(Frame, SImage(w, h, 0.5f), SImage(w, h), SImage(w, h, 0.5f, 0.5f, 0.5f, 1), HDR, HIGH, 1);
  const float fLinear = HIGH * 50 + (1 - HIGH) * 0.5f;
  CHECK_MSG(r.Picture.At(3, 3)[0] < 0.2f * fLinear, std::to_string(r.Picture.At(3, 3)[0]) + " against " + std::to_string(fLinear));
  CHECK(r.Picture.At(3, 3)[0] > 0.5f);
  CHECK_NEAR(r.Picture.At(0, 0)[0], 0.5, 1e-4);
}

//! The sharpening pushes a soft low-contrast step's pixels apart (a texture's detail) but never beyond their
//! neighbours' range (no halo), leaves a silhouette's high-contrast step as it is, and 0 leaves the picture as it is.
//! Why: TAA's picture is a little softer than one frame; the sharpening gives the textures that back without the bright
//! and dark rims of an unsharp mask, and without making the edges it has just anti-aliased jagged again.
XR_TEST(Taa_SharpeningStaysInRange)
{
  // Two steps blurred, S-shaped runs: a texture's from 0.4 to 0.55 and a silhouette's from 0.2 to 0.8.
  const float Texture[] = { 0.4f, 0.4f, 0.4f, 0.41f, 0.43f, 0.475f, 0.52f, 0.54f, 0.55f, 0.55f, 0.55f, 0.55f };
  const float Silhouette[] = { 0.2f, 0.2f, 0.2f, 0.2f, 0.25f, 0.5f, 0.75f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f };
  const int w = 12, h = 5;
  for (const float *pProfile : { Texture, Silhouette })
  {
    SImage Soft(w, h);
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
      {
        float *p = Soft.At(x, y);
        p[0] = p[1] = p[2] = pProfile[x];
        p[3] = 1;
      }
    for (const float k : { 0.0f, 0.5f, 1.0f })
    {
      SDispatch d;
      d.Shader = XR_BYTECODE(Test_TaaSharpen);
      const size_t n = (size_t)w * h;
      d.Test.nTest[0] = (uint32)n;
      d.Test.nTest[1] = (uint32)w;
      d.Test.nTest[2] = (uint32)h;
      d.Test.vTest[2] = k;
      d.nGroups[0] = Groups64(n);
      d.Textures = { ::Texture(Soft) };
      d.nOut = n;
      const std::vector<uint32> Out = Gpu().Run(d);
      double fMoved = 0;
      for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
        {
          const float v = At(Out, (size_t)y * w + x, 0), s = Soft.At(x, y)[0];
          const float l = Soft.At(std::max(x - 1, 0), y)[0], r = Soft.At(std::min(x + 1, w - 1), y)[0];
          CHECK(v >= std::min(l, s) - 1e-6f && v <= std::max(r, s) + 1e-6f);
          fMoved = std::max(fMoved, (double)fabs(v - s));
        }
      if (k == 0 || pProfile == Silhouette)
        CHECK_MSG(fMoved < 1e-6, std::to_string(fMoved));
      else
      {
        CHECK_MSG(fMoved > 0.006 * k, std::to_string(fMoved));
        // Apart: the pixels below the middle darker, those above it brighter.
        CHECK(At(Out, 4, 0) <= Soft.At(4, 0)[0] && At(Out, 6, 0) >= Soft.At(6, 0)[0]);
      }
    }
  }
}
