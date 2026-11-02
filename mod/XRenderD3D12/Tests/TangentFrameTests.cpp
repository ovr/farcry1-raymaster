// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: TangentFrameTests.cpp
//	Description: Unit tests of the tangent frames of bump mapping
//	(TangentFrame.h, CLeafBuffer::CreateTangBuffer), on the CPU: the
//	frame's normal is the vertex's own while it fits its triangles and
//	theirs where it does not (a box side whose normals are another
//	side's, as on Training's shelves), the frames are unit, at right
//	angles and follow the texture's u and v.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "TestMath.h"

#include "TangentFrame.h"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace XrTest;

namespace
{
  //! A vertex as the game's P3F_N_TEX2F has it: the position, the normal, the texture coordinates.
  struct SVert
  {
    float p[3], n[3], tc[2];
  };

  //! The frames of the vertices and triangles; bNormals false: the mesh has no normals of its own.
  std::vector<TangentFrame::SFrame> Frames(const std::vector<SVert> &Verts, const std::vector<uint16_t> &Tris, bool bNormals = true)
  {
    TangentFrame::SMesh m;
    m.pVerts = (const uint8_t *)Verts.data();
    m.nStride = sizeof(SVert);
    m.nPos = offsetof(SVert, p);
    m.nTC = offsetof(SVert, tc);
    if (bNormals)
    {
      m.pNormals = (const uint8_t *)Verts.data() + offsetof(SVert, n);
      m.nNormalStride = sizeof(SVert);
      m.nNormals = (int)Verts.size();
    }
    std::vector<TangentFrame::SFrame> Out(Verts.size());
    TangentFrame::Build(m, (int)Verts.size(), Tris.data(), Tris.size(), Out.data());
    return Out;
  }

  SVec3 V(const float *f) { return { f[0], f[1], f[2] }; }

  //! A box's side as Training's shelves have it (shelve_indoor_*.cgf, 2 m long, its front at y = -45 cm facing -y):
  //! one quad, u along x and v along z, every vertex with the normal n (the shelves': the bottom's, (0, 0, -1)).
  std::vector<SVert> BoxSide(SVec3 n)
  {
    return { { { 0, -45, 5 }, { n.x, n.y, n.z }, { 0, 0 } }, { { 200, -45, 5 }, { n.x, n.y, n.z }, { 1, 0 } },
             { { 200, -45, 60 }, { n.x, n.y, n.z }, { 1, 0.5f } }, { { 0, -45, 60 }, { n.x, n.y, n.z }, { 0, 0.5f } } };
  }
  const std::vector<uint16_t> QUAD = { 0, 1, 2, 0, 2, 3 };
  const SVec3 FRONT = { 0, -1, 0 };

  //! Every frame unit and at right angles, its tangent along the texture's u (dPos/du) and its binormal along v.
  void CheckFrames(const std::vector<TangentFrame::SFrame> &F, SVec3 vU, SVec3 vV, const char *szWhat)
  {
    for (size_t i = 0; i < F.size(); i++)
    {
      const SVec3 T = V(F[i].T), B = V(F[i].B), N = V(F[i].N);
      const std::string s = std::string(szWhat) + " vertex " + std::to_string(i);
      CHECK_MSG(std::fabs(Length(T) - 1) < 1e-5f && std::fabs(Length(B) - 1) < 1e-5f && std::fabs(Length(N) - 1) < 1e-5f, s + ": not unit");
      CHECK_MSG(std::fabs(Dot(T, N)) < 1e-5f && std::fabs(Dot(B, N)) < 1e-5f && std::fabs(Dot(T, B)) < 1e-5f, s + ": not at right angles");
      CHECK_MSG(Dot(T, vU) > 0 && Dot(B, vV) > 0, s + ": not along the texture's u and v");
    }
  }
}

//! The shelves of Training's store: the front's vertices carry the bottom's normal, at right angles to the front. The
//! frames' normal is the front's, and a normal map's normal tilted by up to 60 degrees stays on the front's side (above
//! half of it towards the camera). Why: with the vertices' normal the map's normal lay in the front's plane and the path
//! tracer turned it up and down from a pixel to the next (PathGBufferNormal), noise all over the shelves (2026-11-02).
XR_TEST(TangentFrame_NormalsAcrossTheFace)
{
  const auto F = Frames(BoxSide({ 0, 0, -1 }), QUAD);
  for (size_t i = 0; i < F.size(); i++)
  {
    CHECK_NEAR(F[i].N[0], FRONT.x, 1e-6);
    CHECK_NEAR(F[i].N[1], FRONT.y, 1e-6);
    CHECK_NEAR(F[i].N[2], FRONT.z, 1e-6);
    // The normal map's normal in the frame (General.hlsl: m.x T + m.y B + m.z N), tilted 60 degrees four ways.
    for (int k = 0; k < 4; k++)
    {
      const float a = 1.0471976f, c = std::cos(k * 1.5707963f), s = std::sin(k * 1.5707963f);
      const SVec3 m = { std::sin(a) * c, std::sin(a) * s, std::cos(a) };
      const SVec3 n = Normalize(V(F[i].T) * m.x + V(F[i].B) * m.y + V(F[i].N) * m.z);
      CHECK_MSG(Dot(n, FRONT) >= 0.5f - 1e-5f, "the bumped normal leaves the front's side at vertex " + std::to_string(i));
    }
  }
  CheckFrames(F, { 1, 0, 0 }, { 0, 0, 1 }, "normals across the face");
  // The same with the top's normal and a side's (the shelves' other corners).
  for (const SVec3 n : { SVec3{ 0, 0, 1 }, SVec3{ 1, 0, 0 }, SVec3{ -1, 0, 0 } })
  {
    const auto G = Frames(BoxSide(n), QUAD);
    for (size_t i = 0; i < G.size(); i++)
      CHECK_MSG(Distance(V(G[i].N), FRONT) < 1e-5f, "normal (" + std::to_string(n.x) + " " + std::to_string(n.z) + ") not replaced");
  }
}

//! Normals that fit their faces stay as the mesh has them, bit for bit (as before TangentFrame.h): a smooth surface's
//! (tilted 30 degrees off the face) and a hard edge's mean of two sides (45 degrees off each). Why: the frames of the
//! game's other bump mapped meshes, and so their pictures, must not change.
XR_TEST(TangentFrame_OwnNormalsKept)
{
  for (const SVec3 n : { Normalize(SVec3{ 0, -1, 0.57735f }), Normalize(SVec3{ 0, -1, -1 }), Normalize(SVec3{ 0.3f, -1, 0.2f }) })
  {
    const auto F = Frames(BoxSide(n), QUAD);
    float e[3] = { n.x, n.y, n.z };
    TangentFrame::Normalize3(e);
    for (size_t i = 0; i < F.size(); i++)
      CHECK_MSG(std::memcmp(F[i].N, e, sizeof(e)) == 0, "the vertex's own normal changed at vertex " + std::to_string(i));
    CheckFrames(F, { 1, 0, 0 }, { 0, 0, 1 }, "own normals");
  }
}

//! A normal against its faces (a mesh exported inside out) is replaced by the faces' as well. Why: it does not fit them
//! (the cosine is -1); the SDK's tangent space calculation also takes the normal from the faces.
XR_TEST(TangentFrame_NormalAgainstItsFaces)
{
  const auto F = Frames(BoxSide({ 0, 1, 0 }), QUAD);
  for (size_t i = 0; i < F.size(); i++)
    CHECK_MSG(Distance(V(F[i].N), FRONT) < 1e-5f, "vertex " + std::to_string(i));
  CheckFrames(F, { 1, 0, 0 }, { 0, 0, 1 }, "normal against its faces");
}

//! Without normals of its own (a format without them) the frame's normal is the faces'.
XR_TEST(TangentFrame_NoOwnNormals)
{
  const auto F = Frames(BoxSide({ 0, 0, 0 }), QUAD, false);
  for (size_t i = 0; i < F.size(); i++)
    CHECK_MSG(Distance(V(F[i].N), FRONT) < 1e-5f, "vertex " + std::to_string(i));
  CheckFrames(F, { 1, 0, 0 }, { 0, 0, 1 }, "no own normals");
}

//! A sheet whose two sides share the vertices (the same quad wound both ways): the faces' normals cancel out, so they
//! tell no side, and the vertices keep their own normal, even at right angles. Why: there is nothing to repair it with.
XR_TEST(TangentFrame_SheetSharedBySides)
{
  const std::vector<uint16_t> Both = { 0, 1, 2, 0, 2, 3, 0, 2, 1, 0, 3, 2 };
  const auto F = Frames(BoxSide({ 0, 0, -1 }), Both);
  for (size_t i = 0; i < F.size(); i++)
    CHECK_MSG(Distance(V(F[i].N), SVec3{ 0, 0, -1 }) < 1e-6f, "vertex " + std::to_string(i));
}

//! A vertex no triangle uses keeps its own normal; one without any normal gets (0, 0, 1). Why: CreateTangBuffer fills the
//! whole buffer, and a vertex outside the lists must still have a unit frame.
XR_TEST(TangentFrame_UnusedVertex)
{
  auto Verts = BoxSide({ 0, 0, -1 });
  Verts.push_back({ { 50, 50, 50 }, { 0, 2, 0 }, { 0, 0 } });
  const auto F = Frames(Verts, QUAD);
  CHECK(Distance(V(F[4].N), SVec3{ 0, 1, 0 }) < 1e-6f);
  const auto G = Frames(Verts, QUAD, false);
  CHECK(Distance(V(G[4].N), SVec3{ 0, 0, 1 }) < 1e-6f);
  CHECK(std::fabs(Length(V(G[4].T)) - 1) < 1e-5f && std::fabs(Dot(V(G[4].T), V(G[4].N))) < 1e-5f);
}
