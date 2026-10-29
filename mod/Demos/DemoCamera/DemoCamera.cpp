// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: DemoCamera.cpp
//	Description: A camera going round our own geometry.
//
//	A grid on the ground (z = 0, 1 m cells), the axes from the origin
//	(x red, y green, z blue, 2 m) and two cubes of 1 m with the test
//	picture (TestTexture.h) on every side, drawn with the renderer's own
//	calls (DrawLineColor, DrawDynVB) after SetCamera. Both cubes turn about
//	z by 2 degrees per frame: the left one (x = -1.5) is turned and moved
//	by the demo before it is drawn, the right one (x = 1.5) by the
//	renderer's matrix stack (PushMatrix, TranslateMatrix, RotateMatrix),
//	so the two look the same when the renderer applies that stack. The
//	camera goes round the origin by 3 degrees per frame, 6 m away and 3 m
//	high: frames 1, 31, 61 and 91 look from -y, +x, +y and -x.
//
//////////////////////////////////////////////////////////////////////

#include "DemoHost.h"
#include <VertexFormats.h>

//! The vertices of a cube of 1 m about the origin: 4 per side with the test picture upright, the
//! indices of 2 triangles per side.
static void MakeCube(struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F Verts[24], ushort Inds[36])
{
  // For each side: its normal, and the right and up directions of the picture on it.
  static const float Sides[6][3][3] = {
    { { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },
    { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } },
    { { 0, 1, 0 }, { -1, 0, 0 }, { 0, 0, 1 } },
    { { -1, 0, 0 }, { 0, -1, 0 }, { 0, 0, 1 } },
    { { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },
    { { 0, 0, -1 }, { 1, 0, 0 }, { 0, -1, 0 } },
  };
  // Corners as (right, up) and their texture coordinates (rows from the top: t 0 is the top).
  static const float Corners[4][4] = { { -1, -1, 0, 1 }, { 1, -1, 1, 1 }, { 1, 1, 1, 0 }, { -1, 1, 0, 0 } };
  for (int s = 0; s < 6; s++)
  {
    const Vec3 N(Sides[s][0][0], Sides[s][0][1], Sides[s][0][2]);
    const Vec3 R(Sides[s][1][0], Sides[s][1][1], Sides[s][1][2]);
    const Vec3 U(Sides[s][2][0], Sides[s][2][1], Sides[s][2][2]);
    for (int c = 0; c < 4; c++)
    {
      struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F &V = Verts[s * 4 + c];
      V.xyz = (N + R * Corners[c][0] + U * Corners[c][1]) * 0.5f;
      V.color.dcolor = 0xffffffff;
      V.st[0] = Corners[c][2];
      V.st[1] = Corners[c][3];
    }
    static const int Quad[6] = { 0, 1, 2, 0, 2, 3 };
    for (int i = 0; i < 6; i++)
      Inds[s * 6 + i] = (ushort)(s * 4 + Quad[i]);
  }
}

class CDemoCamera : public CDemo
{
public:
  virtual const char *GetName() const { return "DemoCamera"; }
  virtual const char *GetDefaultFrames() const { return "1,31,61,91"; }

  virtual bool Init(CDemoHost &Host)
  {
    MakeCube(m_Cube, m_CubeInds);
    return Host.GetTestTexture() > 0;
  }

  virtual void Draw(CDemoHost &Host, int nFrame)
  {
    IRenderer *pRenderer = Host.GetRenderer();
    const float fAround = DEG2RAD(3.0f * (nFrame - 1));
    const Vec3 vEye(6.0f * sinf(fAround), -6.0f * cosf(fAround), 3.0f);
    pRenderer->SetCamera(Host.LookAt(vEye, Vec3(0, 0, 0.5f)));
    pRenderer->SetCullMode(R_CULL_NONE);
    pRenderer->SetState(GS_DEPTHWRITE);

    // The ground and the axes, without a texture.
    pRenderer->SetWhiteTexture();
    const CFColor Grey(0.4f, 0.4f, 0.4f, 1), Red(1, 0, 0, 1), Green(0, 1, 0, 1), Blue(0, 0, 1, 1);
    for (int i = -3; i <= 3; i++)
    {
      pRenderer->DrawLineColor(Vec3((float)i, -3, 0), Grey, Vec3((float)i, 3, 0), Grey);
      pRenderer->DrawLineColor(Vec3(-3, (float)i, 0), Grey, Vec3(3, (float)i, 0), Grey);
    }
    pRenderer->DrawLineColor(Vec3(0, 0, 0.01f), Red, Vec3(2, 0, 0.01f), Red);
    pRenderer->DrawLineColor(Vec3(0, 0, 0.01f), Green, Vec3(0, 2, 0.01f), Green);
    pRenderer->DrawLineColor(Vec3(0, 0, 0.01f), Blue, Vec3(0, 0, 2), Blue);

    const float fTurn = 2.0f * (nFrame - 1);
    pRenderer->SetTexture(Host.GetTestTexture());
    // Left cube: turned and moved here.
    struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F Moved[24];
    const float c = cosf(DEG2RAD(fTurn)), s = sinf(DEG2RAD(fTurn));
    for (int i = 0; i < 24; i++)
    {
      Moved[i] = m_Cube[i];
      const Vec3 &P = m_Cube[i].xyz;
      Moved[i].xyz = Vec3(P.x * c - P.y * s - 1.5f, P.x * s + P.y * c, P.z + 0.5f);
    }
    pRenderer->DrawDynVB(Moved, m_CubeInds, 24, 36, R_PRIMV_TRIANGLES);
    // Right cube: by the matrix stack.
    pRenderer->PushMatrix();
    pRenderer->TranslateMatrix(1.5f, 0, 0.5f);
    pRenderer->RotateMatrix(fTurn, 0, 0, 1);
    pRenderer->DrawDynVB(m_Cube, m_CubeInds, 24, 36, R_PRIMV_TRIANGLES);
    pRenderer->PopMatrix();
    pRenderer->SetCullMode(R_CULL_BACK);
  }

private:
  struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F m_Cube[24];
  ushort m_CubeInds[36];
};

DEMO_PLUGIN(CDemoCamera)
