//////////////////////////////////////////////////////////////////////
//
//	File: DemoReset.cpp
//	Description: The resolution changed between frames, so that the
//	renderer resets its device.
//
//	A still scene of our own geometry, drawn like DemoCamera: a grid on
//	the ground (z = 0, 1 m cells), the axes (x red, y green, z blue, 2 m)
//	and a cube of 1 m with the test picture (TestTexture.h) on every side,
//	turned by 30 degrees about z, from a fixed camera. Over it in 2D (the
//	virtual 800 x 600 screen): the test picture upright in the bottom right
//	corner, a yellow frame along the edges of the screen and the size the
//	renderer reports (GetWidth x GetHeight) with the console variables.
//	After every 20 frames the demo sets r_Width and r_Height to the next
//	size, between two frames, as the game's video options do: 1280 x 720,
//	800 x 600 and back to the size the run started with. Frames 10, 30, 50
//	and 70 show each size settled, frames 21, 41 and 61 the first frame
//	after each change; frame 70 asks for the start size again, as frame 10.
//	The log has the size the renderer reports and the window's client area
//	whenever the reported size changes.
//
//	Scene file keys (Demos\DemoReset.ini):
//	  sizes = WxH, WxH     the sizes in between (default 1280x720, 800x600);
//	                       the last phase goes back to the start size
//	  phase = <frames>     frames per size (default 20; the default frames
//	                       fit 20 frames and two sizes, else give --frames)
//	  fullscreen = 0|1     1 also switches r_Fullscreen: the sizes in between
//	                       are drawn in the other mode, the last phase in
//	                       the start mode again (default 0)
//
//////////////////////////////////////////////////////////////////////

#include "DemoHost.h"
#include <VertexFormats.h>
#include <IFont.h>

//! The vertices of a cube of 1 m about the origin: 4 per side with the test picture upright, the
//! indices of 2 triangles per side (as in DemoCamera).
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

class CDemoReset : public CDemo
{
public:
  virtual const char *GetName() const { return "DemoReset"; }
  virtual const char *GetDefaultFrames() const { return "10,21,30,41,50,61,70"; }

  virtual bool Init(CDemoHost &Host)
  {
    IRenderer *pRenderer = Host.GetRenderer();
    MakeCube(m_Cube, m_CubeInds);
    // The cube turned by 30 degrees about z and standing on the ground.
    const float c = cosf(DEG2RAD(30.0f)), s = sinf(DEG2RAD(30.0f));
    for (int i = 0; i < 24; i++)
    {
      const Vec3 P = m_Cube[i].xyz;
      m_Cube[i].xyz = Vec3(P.x * c - P.y * s, P.x * s + P.y * c, P.z + 0.5f);
    }

    m_nStartWidth = atoi(Host.GetCVarString("r_Width").c_str());
    m_nStartHeight = atoi(Host.GetCVarString("r_Height").c_str());
    if (m_nStartWidth <= 0 || m_nStartHeight <= 0)
    {
      m_nStartWidth = pRenderer->GetWidth();
      m_nStartHeight = pRenderer->GetHeight();
    }
    m_nStartFullscreen = atoi(Host.GetCVarString("r_Fullscreen").c_str());
    m_bFullscreen = Host.GetScene().GetInt("fullscreen", 0) != 0;
    m_nPhase = max(Host.GetScene().GetInt("phase", 20), 1);

    std::vector<float> Sizes = Host.GetScene().GetFloats("sizes");
    if (Sizes.size() < 2)
    {
      static const float Default[4] = { 1280, 720, 800, 600 };
      Sizes.assign(Default, Default + 4);
    }
    for (size_t i = 0; i + 1 < Sizes.size(); i += 2)
      m_Sizes.push_back(std::make_pair((int)Sizes[i], (int)Sizes[i + 1]));
    m_Sizes.push_back(std::make_pair(m_nStartWidth, m_nStartHeight));
    Host.Log("DemoReset: start %d x %d (renderer %d x %d), r_Fullscreen %d, %d sizes, %d frames each%s", m_nStartWidth,
      m_nStartHeight, pRenderer->GetWidth(), pRenderer->GetHeight(), m_nStartFullscreen, (int)m_Sizes.size(), m_nPhase,
      m_bFullscreen ? ", r_Fullscreen switched" : "");

    ICryFont *pCryFont = Host.GetSystem()->GetICryFont();
    m_pFont = pCryFont ? pCryFont->GetFont("Default") : NULL;
    if (!m_pFont)
      Host.Log("DemoReset: no font Default, the size is not written");
    return Host.GetTestTexture() > 0;
  }

  virtual void Draw(CDemoHost &Host, int nFrame)
  {
    IRenderer *pRenderer = Host.GetRenderer();
    const int nWidth = pRenderer->GetWidth(), nHeight = pRenderer->GetHeight();
    if (nWidth != m_nLastWidth || nHeight != m_nLastHeight)
    {
      RECT Client = {};
      GetClientRect((HWND)pRenderer->GetHWND(), &Client);
      Host.Log("DemoReset: frame %d drawn at %d x %d (the window's client area %d x %d)", nFrame, nWidth, nHeight,
        (int)(Client.right - Client.left), (int)(Client.bottom - Client.top));
      m_nLastWidth = nWidth;
      m_nLastHeight = nHeight;
    }

    pRenderer->SetCamera(Host.LookAt(Vec3(3.5f, -5.0f, 3.0f), Vec3(0, 0, 0.5f)));
    pRenderer->SetCullMode(R_CULL_NONE);
    pRenderer->SetState(GS_DEPTHWRITE);
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
    pRenderer->SetTexture(Host.GetTestTexture());
    pRenderer->DrawDynVB(m_Cube, m_CubeInds, 24, 36, R_PRIMV_TRIANGLES);
    pRenderer->SetCullMode(R_CULL_BACK);

    // 2D on the virtual 800 x 600 screen: the picture, then a frame of 4 pixels along the edges.
    pRenderer->SetState(GS_NODEPTHTEST);
    pRenderer->Draw2dImage(580, 380, 200, 200, Host.GetTestTexture(), 0, 1, 1, 0);
    pRenderer->Draw2dImage(0, 0, 800, 4, -1, 0, 0, 0, 0, 0, 1, 1, 0, 1);
    pRenderer->Draw2dImage(0, 596, 800, 4, -1, 0, 0, 0, 0, 0, 1, 1, 0, 1);
    pRenderer->Draw2dImage(0, 0, 4, 600, -1, 0, 0, 0, 0, 0, 1, 1, 0, 1);
    pRenderer->Draw2dImage(796, 0, 4, 600, -1, 0, 0, 0, 0, 0, 1, 1, 0, 1);

    if (m_pFont)
    {
      char szLine[128];
      m_pFont->Reset();
      m_pFont->SetEffect("default");
      m_pFont->SetSameSize(false);
      m_pFont->SetCharWidthScale(1.0f);
      m_pFont->SetColor(color4f(1, 1, 1, 1));
      m_pFont->SetSize(vector2f(32, 32));
      _snprintf(szLine, sizeof(szLine) - 1, "%d x %d", nWidth, nHeight);
      szLine[sizeof(szLine) - 1] = 0;
      m_pFont->DrawString(20, 20, szLine);
      m_pFont->SetSize(vector2f(16, 16));
      _snprintf(szLine, sizeof(szLine) - 1, "r_Width %s, r_Height %s, r_Fullscreen %s", Host.GetCVarString("r_Width").c_str(),
        Host.GetCVarString("r_Height").c_str(), Host.GetCVarString("r_Fullscreen").c_str());
      szLine[sizeof(szLine) - 1] = 0;
      m_pFont->DrawString(20, 60, szLine);
    }
  }

  virtual void AfterFrame(CDemoHost &Host, int nFrame)
  {
    if (nFrame % m_nPhase)
      return;
    const size_t nSize = (size_t)(nFrame / m_nPhase - 1);
    if (nSize >= m_Sizes.size())
      return;
    char szCmd[64];
    sprintf(szCmd, "r_Width %d", m_Sizes[nSize].first);
    Host.Exec(szCmd);
    sprintf(szCmd, "r_Height %d", m_Sizes[nSize].second);
    Host.Exec(szCmd);
    if (m_bFullscreen)
    {
      const bool bLast = nSize + 1 == m_Sizes.size();
      sprintf(szCmd, "r_Fullscreen %d", bLast ? m_nStartFullscreen : !m_nStartFullscreen);
      Host.Exec(szCmd);
    }
    Host.Log("DemoReset: after frame %d: size %d of %d, %d x %d", nFrame, (int)nSize + 1, (int)m_Sizes.size(),
      m_Sizes[nSize].first, m_Sizes[nSize].second);
  }

private:
  struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F m_Cube[24];
  ushort m_CubeInds[36];
  //! The sizes after each phase, the start size last.
  std::vector<std::pair<int, int>> m_Sizes;
  int m_nPhase = 20;
  int m_nStartWidth = 0, m_nStartHeight = 0;
  int m_nStartFullscreen = 0;
  bool m_bFullscreen = false;
  int m_nLastWidth = 0, m_nLastHeight = 0;
  IFFont *m_pFont = NULL;
};

DEMO_PLUGIN(CDemoReset)
