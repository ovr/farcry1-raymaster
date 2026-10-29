// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: DemoStates.cpp
//	Description: A table of render states in one frame.
//
//	24 cells, 6 across and 4 down the virtual 800 x 600 screen, each with
//	its name above it and a background of horizontal black and white
//	stripes (Draw2dImage; mid grey in the last row, so that the helpers'
//	own colours show, dark or light). Each cell draws in a viewport of its own
//	(SetViewport, as the SDK's menu draws its models) with a camera of its
//	own and one state; after it the default state comes back
//	(GS_DEPTHWRITE, R_CULL_BACK, R_SOLID_MODE). Nothing moves: frames 1
//	and 2 are the same.
//	  opaque to alpha test >=64: the test picture (TestTexture.h) on a quad,
//	    a red triangle in front of it and a blue one in front of both, whose
//	    alpha rises from 0 at the cell's left to 1 at its right (0.5 in the
//	    middle); all three with the cell's SetState: GS_DEPTHWRITE (opaque),
//	    (SRCALPHA, ONEMINUSSRCALPHA), (ONE, ONE), (SRCALPHA, ONE), (DSTCOL,
//	    ZERO), (DSTCOL, SRCCOL) and GS_DEPTHWRITE with GS_ALPHATEST_GREATER0,
//	    _LESS128, _GEQUAL128 and _GEQUAL64.
//	  depth <=, depth ==, depth >, no depth test, depth write off: the test
//	    picture on a quad that writes depth (but in depth write off), then
//	    a green half transparent copy of it (the same vertices, no depth
//	    write), a red triangle 0.3 m in front of it and a blue one 0.3 m
//	    behind it that overlap; copy and triangles with the default depth
//	    test (less or equal), GS_DEPTHFUNC_EQUAL, GS_DEPTHFUNC_GREAT and
//	    GS_NODEPTHTEST, with depth writes.
//	  cull back, cull front, cull none: a box without a lid seen from above,
//	    the test picture on its five sides (each upright and counter-clockwise
//	    seen from outside), drawn with SetCullMode.
//	  wireframe: the content of opaque with SetPolygonMode(R_WIREFRAME_MODE).
//	  DrawBall, DrawPoint (sizes 0, 1, 2, 4, 8 and a grid of size 0) and
//	    DrawLine (a fan, with DrawLineColor axes): the helper primitives in
//	    the default state, seen from above.
//	  Draw2dImage: the test picture at (100, 75, 600, 450) and a yellow bar
//	    along the bottom of the virtual screen, which is the cell's viewport.
//	  DrawTriStrip: the test picture on a wavy band seen straight on, one
//	    triangle strip (CVertexBuffer of P3F_COL4UB_TEX2F vertices).
//
//	Scene file keys (Demos\DemoStates.ini):
//	  font = <name>   the font of the names (default: Default)
//
//////////////////////////////////////////////////////////////////////

#include "DemoHost.h"
#include <IFont.h>
#include <VertexFormats.h>

typedef struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F SVertex;

static const int CELL_COLUMNS = 6;
static const int CELL_ROWS = 4;
static const int CELL_STRIPES = 8;
//! The cells from this one on (the last row) have a grey background instead of the stripes.
static const int CELL_GREY = 18;
//! Field of view of the cells' cameras, degrees.
static const float CELL_FOV = 45.0f;
//! Height of the names above the cells, on the virtual screen.
static const float CELL_NAME = 16.0f;

enum ECellKind
{
  eCK_Flat,
  eCK_Depth,
  eCK_Cull,
  eCK_Wireframe,
  eCK_Ball,
  eCK_Point,
  eCK_Image,
  eCK_TriStrip,
  eCK_Line,
};

struct SCell
{
  const char *szName;
  ECellKind eKind;
  //! Flat: SetState of everything; depth: of the quad; cull: SetCullMode.
  int nState;
  //! Depth: SetState of the copy and the triangles.
  int nState2;
};

static const SCell s_Cells[CELL_COLUMNS * CELL_ROWS] = {
  { "opaque", eCK_Flat, GS_DEPTHWRITE, 0 },
  { "alpha blend", eCK_Flat, GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA, 0 },
  { "additive", eCK_Flat, GS_BLSRC_ONE | GS_BLDST_ONE, 0 },
  { "alpha additive", eCK_Flat, GS_BLSRC_SRCALPHA | GS_BLDST_ONE, 0 },
  { "multiply", eCK_Flat, GS_BLSRC_DSTCOL | GS_BLDST_ZERO, 0 },
  { "multiply 2x", eCK_Flat, GS_BLSRC_DSTCOL | GS_BLDST_SRCCOL, 0 },
  { "alpha test >0", eCK_Flat, GS_DEPTHWRITE | GS_ALPHATEST_GREATER0, 0 },
  { "alpha test <128", eCK_Flat, GS_DEPTHWRITE | GS_ALPHATEST_LESS128, 0 },
  { "alpha test >=128", eCK_Flat, GS_DEPTHWRITE | GS_ALPHATEST_GEQUAL128, 0 },
  { "alpha test >=64", eCK_Flat, (int)(GS_DEPTHWRITE | GS_ALPHATEST_GEQUAL64), 0 },
  { "depth <=", eCK_Depth, GS_DEPTHWRITE, GS_DEPTHWRITE },
  { "depth ==", eCK_Depth, GS_DEPTHWRITE, GS_DEPTHWRITE | GS_DEPTHFUNC_EQUAL },
  { "depth >", eCK_Depth, GS_DEPTHWRITE, GS_DEPTHWRITE | GS_DEPTHFUNC_GREAT },
  { "no depth test", eCK_Depth, GS_DEPTHWRITE, GS_NODEPTHTEST },
  { "depth write off", eCK_Depth, 0, GS_DEPTHWRITE },
  { "cull back", eCK_Cull, R_CULL_BACK, 0 },
  { "cull front", eCK_Cull, R_CULL_FRONT, 0 },
  { "cull none", eCK_Cull, R_CULL_NONE, 0 },
  { "wireframe", eCK_Wireframe, GS_DEPTHWRITE, 0 },
  { "DrawBall", eCK_Ball, 0, 0 },
  { "DrawPoint", eCK_Point, 0, 0 },
  { "Draw2dImage", eCK_Image, 0, 0 },
  { "DrawTriStrip", eCK_TriStrip, 0, 0 },
  { "DrawLine", eCK_Line, 0, 0 },
};

//! The rectangle of cell n on the virtual 800 x 600 screen, below its name.
static void GetCellRect(int n, float &x, float &y, float &w, float &h)
{
  const float fMargin = 6.0f, fGap = 6.0f;
  const float fCellW = (800.0f - 2 * fMargin - (CELL_COLUMNS - 1) * fGap) / CELL_COLUMNS;
  const float fCellH = (600.0f - 2 * fMargin - (CELL_ROWS - 1) * fGap) / CELL_ROWS;
  x = fMargin + (n % CELL_COLUMNS) * (fCellW + fGap);
  y = fMargin + (n / CELL_COLUMNS) * (fCellH + fGap) + CELL_NAME;
  w = fCellW;
  h = fCellH - CELL_NAME;
}

//! A camera of an nW x nH viewport looking at the origin from fYaw degrees about z (0: from -y) and
//! fPitch degrees above, so near that a ball of fRadius about the origin fills the narrower side.
static CCamera CellCamera(int nW, int nH, float fYaw, float fPitch, float fRadius)
{
  CCamera Cam;
  Cam.Init(nW, nH, DEG2RAD(CELL_FOV));
  // CCamera: the vertical angle of view is the fov times height / width.
  const float fRatio = (float)nH / max(nW, 1);
  const float fHalfV = DEG2RAD(CELL_FOV) * fRatio * 0.5f;
  const float fHalf = min(fHalfV, atanf(tanf(fHalfV) / fRatio));
  const float fYawRad = DEG2RAD(fYaw), fPitchRad = DEG2RAD(fPitch);
  const Vec3 vDir(sinf(fYawRad) * cosf(fPitchRad), -cosf(fYawRad) * cosf(fPitchRad), sinf(fPitchRad));
  Cam.SetPos(vDir * (fRadius / sinf(fHalf)));
  Cam.SetAngle(ConvertVectorToCameraAngles(-vDir));
  Cam.Update();
  return Cam;
}

static void SetVertex(SVertex &V, const Vec3 &vPos, DWORD dwColor, float s = 0, float t = 0)
{
  V.xyz = vPos;
  V.color.dcolor = dwColor;
  V.st[0] = s;
  V.st[1] = t;
}

//! A quad in the plane y facing -y, (x0, z0) bottom left, with the picture upright (t 0 at the top).
static void MakeQuad(SVertex Verts[4], float x0, float z0, float x1, float z1, float y, DWORD dwColor)
{
  SetVertex(Verts[0], Vec3(x0, y, z0), dwColor, 0, 1);
  SetVertex(Verts[1], Vec3(x1, y, z0), dwColor, 1, 1);
  SetVertex(Verts[2], Vec3(x1, y, z1), dwColor, 1, 0);
  SetVertex(Verts[3], Vec3(x0, y, z1), dwColor, 0, 0);
}

//! Alpha of the triangles at x: 0 at the cell's left, 1 at its right.
static float AlphaAt(float x)
{
  return min(max((x + 1.1f) / 2.2f, 0.0f), 1.0f);
}

class CDemoStates : public CDemo
{
public:
  virtual const char *GetName() const { return "DemoStates"; }
  virtual const char *GetDefaultFrames() const { return "1,2"; }

  virtual bool Init(CDemoHost &Host)
  {
    std::string Font = Host.GetScene().Get("font", "Default");
    ICryFont *pCryFont = Host.GetSystem()->GetICryFont();
    m_pFont = pCryFont ? pCryFont->GetFont(Font.c_str()) : NULL;
    if (!m_pFont)
      Host.Log("DemoStates: font '%s' not found, the cells have no names", Font.c_str());
    m_bRGBA = (Host.GetRenderer()->GetFeatures() & RFT_RGBA) != 0;
    return Host.GetTestTexture() > 0;
  }

  virtual void Draw(CDemoHost &Host, int nFrame)
  {
    IRenderer *pRenderer = Host.GetRenderer();
    const int nCells = CELL_COLUMNS * CELL_ROWS;

    // The white stripes (the screen is black), or the grey background.
    pRenderer->SetState(GS_NODEPTHTEST);
    for (int i = 0; i < nCells; i++)
    {
      float x, y, w, h;
      GetCellRect(i, x, y, w, h);
      if (i >= CELL_GREY)
        pRenderer->Draw2dImage(x, y, w, h, -1, 0, 0, 0, 0, 0, 0.5f, 0.5f, 0.5f, 1);
      else
        for (int s = 1; s < CELL_STRIPES; s += 2)
          pRenderer->Draw2dImage(x, y + h * s / CELL_STRIPES, w, h / CELL_STRIPES, -1, 0, 0, 0, 0, 0, 1, 1, 1, 1);
    }
    pRenderer->SetState(GS_DEPTHWRITE);
    pRenderer->ClearDepthBuffer();

    int nVpX, nVpY, nVpW, nVpH;
    pRenderer->GetViewport(&nVpX, &nVpY, &nVpW, &nVpH);
    for (int i = 0; i < nCells; i++)
    {
      float x, y, w, h;
      GetCellRect(i, x, y, w, h);
      const int nX = (int)(pRenderer->ScaleCoordX(x) + 0.5f), nY = (int)(pRenderer->ScaleCoordY(y) + 0.5f);
      const int nW = (int)(pRenderer->ScaleCoordX(w) + 0.5f), nH = (int)(pRenderer->ScaleCoordY(h) + 0.5f);
      pRenderer->SetViewport(nX, nY, nW, nH);
      DrawCell(Host, s_Cells[i], nW, nH);
      pRenderer->SetState(GS_DEPTHWRITE);
      pRenderer->SetCullMode(R_CULL_BACK);
      pRenderer->SetPolygonMode(R_SOLID_MODE);
    }
    pRenderer->SetViewport(nVpX, nVpY, nVpW, nVpH);

    if (m_pFont)
    {
      m_pFont->Reset();
      m_pFont->UseRealPixels(false);
      m_pFont->SetEffect("default");
      m_pFont->SetSameSize(false);
      m_pFont->SetCharWidthScale(1.0f);
      m_pFont->SetColor(color4f(1, 1, 1, 1));
      for (int i = 0; i < nCells; i++)
      {
        float x, y, w, h;
        GetCellRect(i, x, y, w, h);
        // GetTextSize is in pixels (as for the SDK's menu): smaller where the name is wider than the cell.
        m_pFont->SetSize(vector2f(12, 12));
        const float fWidth = m_pFont->GetTextSize(s_Cells[i].szName).x * 800.0f / max(pRenderer->GetWidth(), 1);
        const float fSize = fWidth > w - 4 ? 12 * (w - 4) / fWidth : 12;
        m_pFont->SetSize(vector2f(fSize, fSize));
        m_pFont->DrawString(x + 2, y - CELL_NAME + 1, s_Cells[i].szName);
      }
    }
    pRenderer->SetState(GS_DEPTHWRITE);
  }

private:
  //! A vertex colour: the byte order is R, G, B, A with RFT_RGBA, else B, G, R, A (as in the SDK's
  //! ScriptObjectRenderer).
  DWORD Color(float r, float g, float b, float a) const
  {
    const DWORD R = (DWORD)(r * 255 + 0.5f), G = (DWORD)(g * 255 + 0.5f), B = (DWORD)(b * 255 + 0.5f), A = (DWORD)(a * 255 + 0.5f);
    return m_bRGBA ? (A << 24) | (B << 16) | (G << 8) | R : (A << 24) | (R << 16) | (G << 8) | B;
  }

  void DrawCell(CDemoHost &Host, const SCell &Cell, int nW, int nH)
  {
    IRenderer *pRenderer = Host.GetRenderer();
    if (Cell.eKind == eCK_Flat || Cell.eKind == eCK_Depth || Cell.eKind == eCK_Wireframe || Cell.eKind == eCK_TriStrip)
      pRenderer->SetCamera(CellCamera(nW, nH, 0, 0, 1.2f));
    else if (Cell.eKind == eCK_Cull)
      pRenderer->SetCamera(CellCamera(nW, nH, 30, 40, 1.25f));
    else
      pRenderer->SetCamera(CellCamera(nW, nH, 30, 25, 1.3f));
    pRenderer->SetColorOp(eCO_MODULATE, eCO_MODULATE, DEF_TEXARG0, DEF_TEXARG0);

    switch (Cell.eKind)
    {
    case eCK_Flat:
      DrawFlat(Host, Cell.nState);
      break;
    case eCK_Wireframe:
      pRenderer->SetPolygonMode(R_WIREFRAME_MODE);
      DrawFlat(Host, Cell.nState);
      break;
    case eCK_Depth:
      DrawDepth(Host, Cell.nState, Cell.nState2);
      break;
    case eCK_Cull:
      DrawOpenBox(Host, Cell.nState);
      break;
    case eCK_Ball:
      pRenderer->DrawBall(Vec3(-0.55f, 0, -0.3f), 0.35f);
      pRenderer->DrawBall(0.45f, 0.2f, 0.3f, 0.5f);
      pRenderer->DrawBall(Vec3(0.6f, -0.4f, -0.75f), 0.2f);
      break;
    case eCK_Point:
    {
      static const float Sizes[5] = { 0, 1, 2, 4, 8 };
      for (int i = 0; i < 5; i++)
        pRenderer->DrawPoint(-0.8f + 0.4f * i, 0, 0.5f, Sizes[i]);
      for (int i = 0; i < 15; i++)
        pRenderer->DrawPoint(-0.8f + 0.4f * (i % 5), 0, -0.2f - 0.3f * (i / 5));
      break;
    }
    case eCK_Image:
      // The virtual 800 x 600 screen of Draw2dImage is the viewport: the cell.
      pRenderer->SetState(GS_NODEPTHTEST);
      pRenderer->Draw2dImage(100, 75, 600, 450, Host.GetTestTexture(), 0, 1, 1, 0, 0, 1, 1, 1, 1);
      pRenderer->Draw2dImage(0, 560, 800, 40, -1, 0, 0, 0, 0, 0, 1, 1, 0, 1);
      break;
    case eCK_TriStrip:
      DrawWave(Host);
      break;
    case eCK_Line:
    {
      const Vec3 vCenter(-0.35f, 0, 0.25f);
      for (int i = 0; i < 12; i++)
      {
        const float fAngle = DEG2RAD(30.0f * i);
        pRenderer->DrawLine(vCenter, vCenter + Vec3(cosf(fAngle), 0, sinf(fAngle)) * 0.6f);
      }
      const Vec3 vOrigin(0.4f, 0, -0.7f);
      const CFColor Red(1, 0, 0, 1), Green(0, 1, 0, 1), Blue(0, 0, 1, 1);
      pRenderer->DrawLineColor(vOrigin, Red, vOrigin + Vec3(0.6f, 0, 0), Red);
      pRenderer->DrawLineColor(vOrigin, Green, vOrigin + Vec3(0, 0.6f, 0), Green);
      pRenderer->DrawLineColor(vOrigin, Blue, vOrigin + Vec3(0, 0, 0.6f), Blue);
      break;
    }
    }
  }

  //! The test picture on a quad, a red triangle in front of it and a blue one in front of both,
  //! with their alpha rising from left to right; all with nState.
  void DrawFlat(CDemoHost &Host, int nState)
  {
    IRenderer *pRenderer = Host.GetRenderer();
    static const float Red[3][2] = { { -0.85f, -1.05f }, { 1.05f, -1.05f }, { 0.1f, 0.65f } };
    static const float Blue[3][2] = { { -0.35f, 0.95f }, { 1.05f, 0.95f }, { 0.35f, -0.75f } };
    static ushort QuadInds[6] = { 0, 1, 2, 0, 2, 3 };
    static ushort TriInds[6] = { 0, 1, 2, 3, 4, 5 };
    SVertex Quad[4], Tris[6];
    MakeQuad(Quad, -1.05f, -0.25f, 0.25f, 1.05f, 0, Color(1, 1, 1, 1));
    for (int i = 0; i < 3; i++)
    {
      SetVertex(Tris[i], Vec3(Red[i][0], -0.05f, Red[i][1]), Color(1, 0.1f, 0.1f, AlphaAt(Red[i][0])));
      SetVertex(Tris[3 + i], Vec3(Blue[i][0], -0.1f, Blue[i][1]), Color(0.1f, 0.3f, 1, AlphaAt(Blue[i][0])));
    }
    pRenderer->SetCullMode(R_CULL_NONE);
    pRenderer->SetState(nState);
    pRenderer->SetTexture(Host.GetTestTexture());
    pRenderer->DrawDynVB(Quad, QuadInds, 4, 6, R_PRIMV_TRIANGLES);
    pRenderer->SetWhiteTexture();
    pRenderer->DrawDynVB(Tris, TriInds, 6, 6, R_PRIMV_TRIANGLES);
  }

  //! The test picture on a quad with nQuadState, then with nState a green half transparent copy of
  //! it (never writing depth), a red triangle in front of it and a blue one behind it.
  void DrawDepth(CDemoHost &Host, int nQuadState, int nState)
  {
    IRenderer *pRenderer = Host.GetRenderer();
    static const float Red[3][2] = { { -1.05f, -1.05f }, { 0.5f, -1.05f }, { -1.05f, 0.5f } };
    static const float Blue[3][2] = { { 1.05f, 0.95f }, { -0.95f, -0.15f }, { 0.1f, -1.0f } };
    static ushort QuadInds[6] = { 0, 1, 2, 0, 2, 3 };
    static ushort TriInds[3] = { 0, 1, 2 };
    SVertex Quad[4], Copy[4], RedTri[3], BlueTri[3];
    MakeQuad(Quad, -0.6f, -0.6f, 0.6f, 0.6f, 0, Color(1, 1, 1, 1));
    for (int i = 0; i < 4; i++)
    {
      Copy[i] = Quad[i];
      Copy[i].color.dcolor = Color(0, 1, 0, 0.5f);
    }
    for (int i = 0; i < 3; i++)
    {
      SetVertex(RedTri[i], Vec3(Red[i][0], -0.3f, Red[i][1]), Color(1, 0.1f, 0.1f, 1));
      SetVertex(BlueTri[i], Vec3(Blue[i][0], 0.3f, Blue[i][1]), Color(0.1f, 0.3f, 1, 1));
    }
    pRenderer->SetCullMode(R_CULL_NONE);
    pRenderer->SetState(nQuadState);
    pRenderer->SetTexture(Host.GetTestTexture());
    pRenderer->DrawDynVB(Quad, QuadInds, 4, 6, R_PRIMV_TRIANGLES);
    pRenderer->SetWhiteTexture();
    pRenderer->SetState((nState & ~GS_DEPTHWRITE) | GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA);
    pRenderer->DrawDynVB(Copy, QuadInds, 4, 6, R_PRIMV_TRIANGLES);
    pRenderer->SetState(nState);
    pRenderer->DrawDynVB(RedTri, TriInds, 3, 3, R_PRIMV_TRIANGLES);
    pRenderer->DrawDynVB(BlueTri, TriInds, 3, 3, R_PRIMV_TRIANGLES);
  }

  //! The test picture on a wavy band 2 m wide, drawn as one triangle strip with DrawTriStrip.
  void DrawWave(CDemoHost &Host)
  {
    IRenderer *pRenderer = Host.GetRenderer();
    const int nSteps = 8;
    SVertex Verts[2 * (nSteps + 1)];
    for (int i = 0; i <= nSteps; i++)
    {
      const float s = (float)i / nSteps, x = -1 + 2 * s, z = 0.3f * sinf(s * 2 * gf_PI);
      SetVertex(Verts[2 * i], Vec3(x, 0, z - 0.35f), Color(1, 1, 1, 1), s, 1);
      SetVertex(Verts[2 * i + 1], Vec3(x, 0, z + 0.35f), Color(1, 1, 1, 1), s, 0);
    }
    CVertexBuffer Buffer(Verts, VERTEX_FORMAT_P3F_COL4UB_TEX2F, 2 * (nSteps + 1));
    pRenderer->SetCullMode(R_CULL_NONE);
    pRenderer->SetTexture(Host.GetTestTexture());
    pRenderer->DrawTriStrip(&Buffer, 2 * (nSteps + 1));
    pRenderer->SetWhiteTexture();
  }

  //! A box of 1.4 m about the origin without its top, the test picture on its sides, with nCullMode.
  void DrawOpenBox(CDemoHost &Host, int nCullMode)
  {
    IRenderer *pRenderer = Host.GetRenderer();
    // For each side: its normal, and the right and up directions of the picture on it seen from
    // outside (right x up = normal: the corners below go counter-clockwise seen from outside).
    static const float Sides[5][3][3] = {
      { { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },
      { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } },
      { { 0, 1, 0 }, { -1, 0, 0 }, { 0, 0, 1 } },
      { { -1, 0, 0 }, { 0, -1, 0 }, { 0, 0, 1 } },
      { { 0, 0, -1 }, { 1, 0, 0 }, { 0, -1, 0 } },
    };
    static const float Corners[4][4] = { { -1, -1, 0, 1 }, { 1, -1, 1, 1 }, { 1, 1, 1, 0 }, { -1, 1, 0, 0 } };
    SVertex Verts[20];
    ushort Inds[30];
    for (int s = 0; s < 5; s++)
    {
      const Vec3 N(Sides[s][0][0], Sides[s][0][1], Sides[s][0][2]);
      const Vec3 R(Sides[s][1][0], Sides[s][1][1], Sides[s][1][2]);
      const Vec3 U(Sides[s][2][0], Sides[s][2][1], Sides[s][2][2]);
      for (int c = 0; c < 4; c++)
        SetVertex(Verts[s * 4 + c], (N + R * Corners[c][0] + U * Corners[c][1]) * 0.7f, Color(1, 1, 1, 1), Corners[c][2], Corners[c][3]);
      static const int Quad[6] = { 0, 1, 2, 0, 2, 3 };
      for (int i = 0; i < 6; i++)
        Inds[s * 6 + i] = (ushort)(s * 4 + Quad[i]);
    }
    pRenderer->SetState(GS_DEPTHWRITE);
    pRenderer->SetCullMode(nCullMode);
    pRenderer->SetTexture(Host.GetTestTexture());
    pRenderer->DrawDynVB(Verts, Inds, 20, 30, R_PRIMV_TRIANGLES);
    pRenderer->SetWhiteTexture();
  }

  IFFont *m_pFont = NULL;
  bool m_bRGBA = false;
};

DEMO_PLUGIN(CDemoStates)
