// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: RayFaceTests.cpp
//	Description: Unit tests of the face the main view's rays start from
//	(d3d12_RTFaceNormal, RayFace.hlsli's FaceFromRhw, TestHeaders.hlsl's
//	CSRayFace): a camera some hundreds of metres from the origin as in the
//	game's levels, a 3840 x 2160 view, planes 0.4 m away (a face in a cut
//	scene) at angles from head on to nearly edge on and 50 m away; 1 / w
//	at three pixels as the GPU's float differences take it. Its normal
//	is the plane's within a fraction of a degree, while the cross product
//	of the float world positions' differences (the frames before) is off
//	by degrees there; an orthographic view gives none.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "GpuHarness.h"
#include "TestMath.h"

#include "Test_RayFace.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

using namespace XrTest;

namespace
{
  constexpr double PI = 3.14159265358979;
  constexpr int W = 3840, H = 2160;

  struct SD3
  {
    double x = 0, y = 0, z = 0;
  };
  SD3 operator+(SD3 a, SD3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
  SD3 operator-(SD3 a, SD3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
  SD3 operator*(SD3 a, double s) { return { a.x * s, a.y * s, a.z * s }; }
  double Dot(SD3 a, SD3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
  SD3 Cross(SD3 a, SD3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
  SD3 Normalize(SD3 a) { return a * (1 / std::sqrt(Dot(a, a))); }

  //! A view: the camera, its axes, the world to clip matrix (row vectors, as the renderer's mViewProj).
  struct SView
  {
    SD3 vPos, vRight, vUp, vFwd;
    double fFocal = 0, fAspect = 0;
    double m[4][4] = {};
    bool bOrtho = false;

    //! The ray's direction through the NDC x, y (its view depth 1).
    SD3 Dir(double x, double y) const { return vFwd + vRight * (x * fAspect / fFocal) + vUp * (y / fFocal); }
    //! The NDC of pixel (px, py)'s centre.
    static double NdcX(double px) { return 2 * (px + 0.5) / W - 1; }
    static double NdcY(double py) { return 1 - 2 * (py + 0.5) / H; }
  };

  //! A perspective view (or an orthographic one) of the camera vPos looking along vFwd, 60 degrees across vertically.
  SView MakeView(SD3 vPos, SD3 vFwd, bool bOrtho = false)
  {
    SView v;
    v.vPos = vPos;
    v.vFwd = Normalize(vFwd);
    v.vRight = Normalize(Cross({ 0, 0, 1 }, v.vFwd));
    v.vUp = Cross(v.vFwd, v.vRight);
    v.fFocal = 1 / std::tan(PI / 6);
    v.fAspect = (double)W / H;
    v.bOrtho = bOrtho;
    double View[4][4] = {}, Proj[4][4] = {};
    const SD3 Axes[3] = { v.vRight, v.vUp, v.vFwd };
    for (int c = 0; c < 3; c++)
    {
      View[0][c] = Axes[c].x;
      View[1][c] = Axes[c].y;
      View[2][c] = Axes[c].z;
      View[3][c] = -Dot(vPos, Axes[c]);
    }
    View[3][3] = 1;
    const double n = 0.25, f = 1000;
    if (bOrtho)
    {
      Proj[0][0] = 2 / 4.0;
      Proj[1][1] = 2 / (4.0 / v.fAspect);
      Proj[2][2] = 1 / (f - n);
      Proj[3][2] = -n / (f - n);
      Proj[3][3] = 1;
    }
    else
    {
      Proj[0][0] = v.fFocal / v.fAspect;
      Proj[1][1] = v.fFocal;
      Proj[2][2] = f / (f - n);
      Proj[3][2] = -n * f / (f - n);
      Proj[2][3] = 1;
    }
    for (int r = 0; r < 4; r++)
      for (int c = 0; c < 4; c++)
      {
        v.m[r][c] = 0;
        for (int k = 0; k < 4; k++)
          v.m[r][c] += View[r][k] * Proj[k][c];
      }
    return v;
  }

  //! A plane through vPoint with the normal vNormal.
  struct SPlane
  {
    SD3 vPoint, vNormal;
  };

  //! Where pixel (px, py)'s ray meets the plane, and 1 / w there (w its view depth).
  SD3 Hit(const SView &v, const SPlane &p, double px, double py, double &fRhw)
  {
    const SD3 d = v.Dir(SView::NdcX(px), SView::NdcY(py));
    const double t = Dot(p.vPoint - v.vPos, p.vNormal) / Dot(d, p.vNormal);
    fRhw = 1 / t;
    return v.vPos + d * t;
  }

  //! A case of CSRayFace: pixel (px, py) of the view on the plane.
  struct SFaceCase
  {
    SView View;
    SPlane Plane;
    int px = 0, py = 0;
  };

  //! FaceFromRhw of each case, 1 / w and its differences as the GPU's floats take them.
  std::vector<SVec3> RunFace(const std::vector<SFaceCase> &In)
  {
    SDispatch d;
    d.Shader = XR_BYTECODE(Test_RayFace);
    d.Test.nTest[0] = (uint32)In.size();
    d.nGroups[0] = Groups64(In.size());
    for (const SFaceCase &c : In)
    {
      double q0 = 1, qx = 1, qy = 1;
      if (c.View.bOrtho)
        q0 = qx = qy = 1;
      else
      {
        Hit(c.View, c.Plane, c.px, c.py, q0);
        Hit(c.View, c.Plane, c.px + 1, c.py, qx);
        Hit(c.View, c.Plane, c.px, c.py + 1, qy);
      }
      const float f0 = (float)q0;
      Push(d.In, f0, (float)qx - f0, (float)qy - f0, 0);
      Push(d.In, (float)SView::NdcX(c.px), (float)SView::NdcY(c.py), 2.0f / W, -2.0f / H);
      for (int r = 0; r < 4; r++)
        Push(d.In, (float)c.View.m[r][0], (float)c.View.m[r][1], (float)c.View.m[r][2], (float)c.View.m[r][3]);
    }
    d.nOut = In.size();
    const std::vector<uint32> Out = Gpu().Run(d);
    std::vector<SVec3> R;
    for (size_t i = 0; i < In.size(); i++)
      R.push_back(Vec(Out, i));
    return R;
  }

  //! The angle (degrees) between the plane's normal and a, either side.
  double AngleTo(SD3 n, SD3 a)
  {
    const double l = std::sqrt(Dot(a, a));
    if (!(l > 0))
      return 180;
    return std::acos(std::min(1.0, std::abs(Dot(n, a)) / l)) * 180 / PI;
  }

  //! The face the frames before took: the cross product of the float world positions' differences a pixel right and down.
  SD3 FloatCrossFace(const SFaceCase &c)
  {
    double q;
    const SD3 p0 = Hit(c.View, c.Plane, c.px, c.py, q), p1 = Hit(c.View, c.Plane, c.px + 1, c.py, q),
              p2 = Hit(c.View, c.Plane, c.px, c.py + 1, q);
    auto F = [](SD3 a) { return SD3{ (double)(float)a.x, (double)(float)a.y, (double)(float)a.z }; };
    const SD3 a = F(p1), b = F(p0), e = F(p2);
    const SD3 dx = { (double)((float)a.x - (float)b.x), (double)((float)a.y - (float)b.y), (double)((float)a.z - (float)b.z) };
    const SD3 dy = { (double)((float)e.x - (float)b.x), (double)((float)e.y - (float)b.y), (double)((float)e.z - (float)b.z) };
    return Cross(dx, dy);
  }

  //! Planes through pixel (px, py)'s ray fDistance away, at the angles fTheta from facing the camera (degrees) towards
  //! the directions fPhi round the ray.
  std::vector<SFaceCase> Cases(const SView &v, double fDistance, std::initializer_list<double> Theta, int px, int py)
  {
    std::vector<SFaceCase> R;
    const SD3 d = Normalize(v.Dir(SView::NdcX(px), SView::NdcY(py)));
    const SD3 e1 = Normalize(Cross(d, v.vUp)), e2 = Cross(e1, d);
    for (double t : Theta)
      for (double ph : { 0.0, 90.0, 200.0 })
      {
        const double th = t * PI / 180, f = ph * PI / 180;
        SFaceCase c;
        c.View = v;
        c.Plane.vPoint = v.vPos + d * fDistance;
        c.Plane.vNormal = Normalize(d * -std::cos(th) + e1 * (std::sin(th) * std::cos(f)) + e2 * (std::sin(th) * std::sin(f)));
        c.px = px;
        c.py = py;
        R.push_back(c);
      }
    return R;
  }

  //! The camera of Training's opening cut scene's kind: some hundreds of metres from the origin.
  SView GameView(bool bOrtho = false) { return MakeView({ 412.37, 287.91, 31.6 }, { 0.3, 0.9, -0.2 }, bOrtho); }
}

XR_TEST(RayFace_CloseUp)
{
  // A face 0.4 m away (a pixel 0.2 mm across) at the view's centre and its corners, from head on to nearly edge on.
  const SView v = GameView();
  std::vector<SFaceCase> In;
  const std::array<int, 2> Pixels[] = { { W / 2, H / 2 }, { 40, 30 }, { W - 60, H - 50 }, { 300, H - 400 } };
  for (const std::array<int, 2> &p : Pixels)
  {
    const std::vector<SFaceCase> c = Cases(v, 0.4, { 0, 30, 60, 75, 85 }, p[0], p[1]);
    In.insert(In.end(), c.begin(), c.end());
  }
  const std::vector<SVec3> Out = RunFace(In);
  double fWorst = 0, fWorstOld = 0;
  for (size_t i = 0; i < In.size(); i++)
  {
    const double a = AngleTo(In[i].Plane.vNormal, { Out[i].x, Out[i].y, Out[i].z });
    fWorst = std::max(fWorst, a);
    fWorstOld = std::max(fWorstOld, AngleTo(In[i].Plane.vNormal, FloatCrossFace(In[i])));
    CHECK_MSG(a < 0.25, "case " + std::to_string(i) + ": " + std::to_string(a) + " degrees off");
  }
  // The frames before's face is degrees off there: the rounding this fixes.
  CHECK_MSG(fWorstOld > 3, "the float positions' cross product only " + std::to_string(fWorstOld) + " degrees off");
  CHECK(fWorst < 0.25);
}

XR_TEST(RayFace_Far)
{
  // 50 m away the same, within half a degree (1 / w's changes are small there, the world positions' are not).
  const SView v = GameView();
  std::vector<SFaceCase> In = Cases(v, 50, { 0, 45, 80 }, W / 2 + 7, H / 2 - 11);
  const std::vector<SFaceCase> c = Cases(v, 50, { 0, 45, 80 }, 200, 150);
  In.insert(In.end(), c.begin(), c.end());
  const std::vector<SVec3> Out = RunFace(In);
  for (size_t i = 0; i < In.size(); i++)
  {
    const double a = AngleTo(In[i].Plane.vNormal, { Out[i].x, Out[i].y, Out[i].z });
    CHECK_MSG(a < 0.5, "case " + std::to_string(i) + ": " + std::to_string(a) + " degrees off");
  }
}

XR_TEST(RayFace_Orthographic)
{
  // 1 / w the same everywhere: no face (the caller keeps the world positions' one).
  const SView v = GameView(true);
  const std::vector<SFaceCase> In = Cases(v, 3, { 0, 60 }, W / 2, H / 2);
  const std::vector<SVec3> Out = RunFace(In);
  for (size_t i = 0; i < In.size(); i++)
    CHECK(Out[i].x * Out[i].x + Out[i].y * Out[i].y + Out[i].z * Out[i].z <= 1e-20f);
}
