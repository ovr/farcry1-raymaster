//////////////////////////////////////////////////////////////////////
//
//	File: DemoAspect.cpp
//	Description: How the frame is laid out at a resolution and aspect
//	ratio.
//
//	2D, on the virtual 800 x 600 screen of Draw2dImage and the fonts (the
//	SDK's menu code, UISystem.cpp, scales it to GetWidth() x GetHeight()):
//	yellow L marks in the four corners with their coordinates, yellow
//	ticks along the edges every 50 (longer every 100), a magenta cross at
//	the centre (400, 300) and, top left over a dark box, GetWidth() x
//	GetHeight(), GetViewport, ScaleCoordX(800) x ScaleCoordY(600) and the
//	camera.
//	3D: a camera at the origin looks level along +y with a field of view
//	of fov degrees (CCamera::Init; CCamera::Update makes the vertical
//	angle of its frustum fov * height / width). Around it, 10 m
//	away, stand posts every 10 degrees from -90 (left, orange) through 0
//	(white) to +90 (right, cyan), each with its angle above it in digits
//	made of quads; a grey horizon arc has a tick every degree, longer every
//	5. Straight ahead, green ticks every 5 degrees of elevation from -45 to
//	+45 are labelled every 10. The posts and ticks cut by the frame's
//	edges give the visible angles; the text gives the half angles of
//	CCamera's frustum. The centre cross lies on the horizon at the 0 post
//	when the 2D centre and the 3D view direction agree. Nothing moves.
//
//	Scene file keys (Demos\DemoAspect.ini):
//	  fov = <degrees>   the camera's field of view (default 96)
//	  font = <name>     the font of the text (default: Default)
//
//////////////////////////////////////////////////////////////////////

#include "DemoHost.h"
#include <IFont.h>
#include <VertexFormats.h>

typedef struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F SVertex;

//! Distance of the posts and the elevation ticks from the camera, metres.
static const float RING_RADIUS = 10.0f;
//! Height of the digits, metres.
static const float DIGIT_HEIGHT = 0.44f;

//! The segments of the digits 0 to 9 as bits 0 to 6: top, top right, bottom right, bottom, bottom
//! left, top left, middle.
static const int s_DigitSegments[10] = { 0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f };
//! The ends of the segments in a digit 1 wide and 2 high, from its bottom left.
static const float s_Segments[7][4] = {
  { 0, 2, 1, 2 }, { 1, 2, 1, 1 }, { 1, 1, 1, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 }, { 0, 1, 0, 2 }, { 0, 1, 1, 1 },
};

//! Coloured quads drawn with DrawDynVB, in batches.
class CQuadBatch
{
public:
  CQuadBatch(IRenderer *pRenderer) : m_pRenderer(pRenderer) {}

  //! The quad A, B, C, D (in order round it).
  void Add(const Vec3 &A, const Vec3 &B, const Vec3 &C, const Vec3 &D, DWORD dwColor)
  {
    if (m_Verts.size() + 4 > 1024)
      Flush();
    const ushort n = (ushort)m_Verts.size();
    const Vec3 *Corners[4] = { &A, &B, &C, &D };
    for (int i = 0; i < 4; i++)
    {
      SVertex V;
      V.xyz = *Corners[i];
      V.color.dcolor = dwColor;
      V.st[0] = V.st[1] = 0;
      m_Verts.push_back(V);
    }
    static const ushort Quad[6] = { 0, 1, 2, 0, 2, 3 };
    for (int i = 0; i < 6; i++)
      m_Inds.push_back(n + Quad[i]);
  }

  //! A bar fWidth wide along vSide from P0 to P1, longer by half its width at both ends.
  void AddBar(const Vec3 &P0, const Vec3 &P1, const Vec3 &vSide, float fWidth, DWORD dwColor)
  {
    const Vec3 vEnd = GetNormalized(P1 - P0) * (fWidth * 0.5f), vHalf = vSide * (fWidth * 0.5f);
    const Vec3 A = P0 - vEnd, B = P1 + vEnd;
    Add(A - vHalf, B - vHalf, B + vHalf, A + vHalf, dwColor);
  }

  //! szText (digits and '-') with its bottom left at vOrigin, along vRight and vUp.
  void AddDigits(const char *szText, const Vec3 &vOrigin, const Vec3 &vRight, const Vec3 &vUp, DWORD dwColor)
  {
    const float fUnit = DIGIT_HEIGHT * 0.5f;
    Vec3 vPos = vOrigin;
    for (const char *p = szText; *p; p++)
    {
      const int nMask = *p == '-' ? 0x40 : *p >= '0' && *p <= '9' ? s_DigitSegments[*p - '0'] : 0;
      for (int s = 0; s < 7; s++)
      {
        if (!(nMask & (1 << s)))
          continue;
        const float *pSeg = s_Segments[s];
        const Vec3 P0 = vPos + vRight * (pSeg[0] * fUnit) + vUp * (pSeg[1] * fUnit);
        const Vec3 P1 = vPos + vRight * (pSeg[2] * fUnit) + vUp * (pSeg[3] * fUnit);
        AddBar(P0, P1, pSeg[1] == pSeg[3] ? vUp : vRight, DIGIT_HEIGHT * 0.12f, dwColor);
      }
      vPos = vPos + vRight * (fUnit * 1.6f);
    }
  }

  void Flush()
  {
    if (!m_Verts.empty())
      m_pRenderer->DrawDynVB(&m_Verts[0], &m_Inds[0], (int)m_Verts.size(), (int)m_Inds.size(), R_PRIMV_TRIANGLES);
    m_Verts.clear();
    m_Inds.clear();
  }

private:
  IRenderer *m_pRenderer;
  std::vector<SVertex> m_Verts;
  std::vector<ushort> m_Inds;
};

//! Width of szText drawn by CQuadBatch::AddDigits.
static float DigitsWidth(const char *szText)
{
  return DIGIT_HEIGHT * 0.5f * (1.6f * strlen(szText) - 0.6f);
}

class CDemoAspect : public CDemo
{
public:
  virtual const char *GetName() const { return "DemoAspect"; }
  virtual const char *GetDefaultFrames() const { return "1"; }

  virtual bool Init(CDemoHost &Host)
  {
    m_fFov = Host.GetScene().GetFloat("fov", 96.0f);
    std::string Font = Host.GetScene().Get("font", "Default");
    ICryFont *pCryFont = Host.GetSystem()->GetICryFont();
    m_pFont = pCryFont ? pCryFont->GetFont(Font.c_str()) : NULL;
    if (!m_pFont)
      Host.Log("DemoAspect: font '%s' not found, no text", Font.c_str());
    IRenderer *pRenderer = Host.GetRenderer();
    m_bRGBA = (pRenderer->GetFeatures() & RFT_RGBA) != 0;
    int nVpX, nVpY, nVpW, nVpH;
    pRenderer->GetViewport(&nVpX, &nVpY, &nVpW, &nVpH);
    Host.Log("DemoAspect: %d x %d, viewport %d %d %d %d, ScaleCoordX(800) %g, ScaleCoordY(600) %g, fov %g", pRenderer->GetWidth(),
      pRenderer->GetHeight(), nVpX, nVpY, nVpW, nVpH, pRenderer->ScaleCoordX(800), pRenderer->ScaleCoordY(600), m_fFov);
    return true;
  }

  virtual void Draw(CDemoHost &Host, int nFrame)
  {
    const CCamera Cam = Host.LookAt(Vec3(0, 0, 0), Vec3(0, 1, 0), m_fFov);
    Draw3D(Host, Cam);
    Draw2D(Host);
    DrawLabels(Host, Cam);
    Host.GetRenderer()->SetState(GS_DEPTHWRITE);
  }

private:
  //! A vertex colour: the byte order is R, G, B, A with RFT_RGBA, else B, G, R, A (as in the SDK's
  //! ScriptObjectRenderer).
  DWORD Color(float r, float g, float b) const
  {
    const DWORD R = (DWORD)(r * 255 + 0.5f), G = (DWORD)(g * 255 + 0.5f), B = (DWORD)(b * 255 + 0.5f);
    return m_bRGBA ? 0xff000000 | (B << 16) | (G << 8) | R : 0xff000000 | (R << 16) | (G << 8) | B;
  }

  void Draw3D(CDemoHost &Host, const CCamera &Cam)
  {
    IRenderer *pRenderer = Host.GetRenderer();
    pRenderer->SetCamera(Cam);
    // Nothing is hidden: the later quads are drawn over the earlier ones.
    pRenderer->SetState(GS_NODEPTHTEST);
    pRenderer->SetCullMode(R_CULL_NONE);
    pRenderer->SetWhiteTexture();
    pRenderer->SetColorOp(eCO_MODULATE, eCO_MODULATE, DEF_TEXARG0, DEF_TEXARG0);

    CQuadBatch Batch(pRenderer);
    const Vec3 vUp(0, 0, 1);
    const DWORD dwGrey = Color(0.6f, 0.6f, 0.6f), dwLeft = Color(1, 0.55f, 0.1f), dwRight = Color(0.2f, 0.75f, 1);
    const DWORD dwAhead = Color(1, 1, 1), dwGreen = Color(0.3f, 1, 0.3f);
    char szLabel[16];
    // Horizontal angle a: positive to the right (+x).
    for (int a = -90; a <= 90; a++)
    {
      const float fRad = DEG2RAD((float)a);
      const Vec3 vBase = Vec3(sinf(fRad), cosf(fRad), 0) * RING_RADIUS, vRight(cosf(fRad), -sinf(fRad), 0);
      if (a < 90)
      {
        const float fNext = DEG2RAD(a + 1.0f);
        const Vec3 vNext = Vec3(sinf(fNext), cosf(fNext), 0) * RING_RADIUS;
        Batch.Add(vBase - vUp * 0.015f, vNext - vUp * 0.015f, vNext + vUp * 0.015f, vBase + vUp * 0.015f, dwGrey);
      }
      if (a % 10)
      {
        const float fLen = a % 5 ? 0.1f : 0.2f;
        Batch.AddBar(vBase - vUp * fLen, vBase + vUp * fLen, vRight, 0.03f, dwGrey);
        continue;
      }
      const DWORD dwColor = a < 0 ? dwLeft : a > 0 ? dwRight : dwAhead;
      Batch.AddBar(vBase - vUp * 0.8f, vBase + vUp * 0.8f, vRight, 0.1f, dwColor);
      sprintf(szLabel, "%d", a);
      Batch.AddDigits(szLabel, vBase - vRight * (DigitsWidth(szLabel) * 0.5f) + vUp * 1.0f, vRight, vUp, dwColor);
    }
    // Elevation e straight ahead, on the plane y = RING_RADIUS.
    for (int e = -45; e <= 45; e += 5)
    {
      const float z = RING_RADIUS * tanf(DEG2RAD((float)e));
      const float fLen = e % 10 ? 0.15f : 0.3f;
      Batch.AddBar(Vec3(-fLen, RING_RADIUS, z), Vec3(fLen, RING_RADIUS, z), vUp, 0.03f, dwGreen);
      if (e % 10 || !e)
        continue;
      sprintf(szLabel, "%d", e);
      Batch.AddDigits(szLabel, Vec3(0.4f, RING_RADIUS, z - DIGIT_HEIGHT * 0.5f), Vec3(1, 0, 0), vUp, dwGreen);
    }
    Batch.Flush();
    pRenderer->SetCullMode(R_CULL_BACK);
  }

  //! A rectangle of the virtual 800 x 600 screen in one colour.
  static void Rect(IRenderer *pRenderer, float x, float y, float w, float h, float r, float g, float b, float a = 1)
  {
    pRenderer->Draw2dImage(x, y, w, h, -1, 0, 0, 0, 0, 0, r, g, b, a);
  }

  void Draw2D(CDemoHost &Host)
  {
    IRenderer *pRenderer = Host.GetRenderer();
    pRenderer->SetState(GS_NODEPTHTEST);
    for (int x = 50; x < 800; x += 50)
    {
      const float fLen = x % 100 ? 8.0f : 16.0f;
      Rect(pRenderer, x - 1.0f, 0, 2, fLen, 1, 1, 0);
      Rect(pRenderer, x - 1.0f, 600 - fLen, 2, fLen, 1, 1, 0);
    }
    for (int y = 50; y < 600; y += 50)
    {
      const float fLen = y % 100 ? 8.0f : 16.0f;
      Rect(pRenderer, 0, y - 1.0f, fLen, 2, 1, 1, 0);
      Rect(pRenderer, 800 - fLen, y - 1.0f, fLen, 2, 1, 1, 0);
    }
    // The corners: L marks 40 long and 4 wide.
    Rect(pRenderer, 0, 0, 40, 4, 1, 1, 0);
    Rect(pRenderer, 0, 0, 4, 40, 1, 1, 0);
    Rect(pRenderer, 760, 0, 40, 4, 1, 1, 0);
    Rect(pRenderer, 796, 0, 4, 40, 1, 1, 0);
    Rect(pRenderer, 0, 596, 40, 4, 1, 1, 0);
    Rect(pRenderer, 0, 560, 4, 40, 1, 1, 0);
    Rect(pRenderer, 760, 596, 40, 4, 1, 1, 0);
    Rect(pRenderer, 796, 560, 4, 40, 1, 1, 0);
    // The centre.
    Rect(pRenderer, 380, 299, 40, 2, 1, 0.2f, 1);
    Rect(pRenderer, 399, 280, 2, 40, 1, 0.2f, 1);
  }

  void DrawLabels(CDemoHost &Host, const CCamera &Cam)
  {
    if (!m_pFont)
      return;
    IRenderer *pRenderer = Host.GetRenderer();
    const int nWidth = pRenderer->GetWidth(), nHeight = pRenderer->GetHeight();
    m_pFont->Reset();
    m_pFont->UseRealPixels(false);
    m_pFont->SetEffect("default");
    m_pFont->SetSameSize(false);
    m_pFont->SetCharWidthScale(1.0f);
    m_pFont->SetSize(vector2f(12, 12));

    // The corners' coordinates; right aligned by GetTextSize, which is in pixels (as for the SDK's menu).
    m_pFont->SetColor(color4f(1, 1, 0, 1));
    const float fToVirtual = 800.0f / max(nWidth, 1);
    m_pFont->DrawString(8, 8, "(0, 0)");
    m_pFont->DrawString(792 - m_pFont->GetTextSize("(800, 0)").x * fToVirtual, 8, "(800, 0)");
    m_pFont->DrawString(8, 578, "(0, 600)");
    m_pFont->DrawString(792 - m_pFont->GetTextSize("(800, 600)").x * fToVirtual, 578, "(800, 600)");
    m_pFont->SetColor(color4f(1, 0.2f, 1, 1));
    m_pFont->DrawString(322, 356, "(400, 300)");

    // CCamera: the vertical angle of view is the fov times height / width; the horizontal one follows.
    const float fRatio = Cam.GetProjRatio();
    const float fHalfV = Cam.GetFov() * fRatio * 0.5f;
    const float fHalfH = atanf(tanf(fHalfV) / fRatio);
    int nVpX, nVpY, nVpW, nVpH;
    pRenderer->GetViewport(&nVpX, &nVpY, &nVpW, &nVpH);
    char szLines[6][128];
    sprintf(szLines[0], "GetWidth x GetHeight: %d x %d (%.3f)", nWidth, nHeight, (float)nWidth / max(nHeight, 1));
    sprintf(szLines[1], "GetViewport: %d %d %d %d", nVpX, nVpY, nVpW, nVpH);
    sprintf(szLines[2], "ScaleCoordX/Y(800, 600): %g x %g", pRenderer->ScaleCoordX(800), pRenderer->ScaleCoordY(600));
    sprintf(szLines[3], "camera: fov %g, CCamera ratio %.4f", RAD2DEG(Cam.GetFov()), fRatio);
    sprintf(szLines[4], "frustum: horizontal +-%.1f, vertical +-%.1f", RAD2DEG(fHalfH), RAD2DEG(fHalfV));
    sprintf(szLines[5], "posts every 10 degrees, + to the right");

    // A dark box behind the lines.
    float fBox = 0;
    for (int i = 0; i < 6; i++)
      fBox = max(fBox, m_pFont->GetTextSize(szLines[i]).x * fToVirtual);
    pRenderer->SetState(GS_NODEPTHTEST | GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA);
    Rect(pRenderer, 44, 44, fBox + 12, 6 * 16 + 10, 0, 0, 0, 0.7f);
    pRenderer->SetState(GS_NODEPTHTEST);
    m_pFont->SetColor(color4f(1, 1, 1, 1));
    for (int i = 0; i < 6; i++)
      m_pFont->DrawString(50, 50 + 16.0f * i, szLines[i]);
  }

  float m_fFov = 96.0f;
  IFFont *m_pFont = NULL;
  bool m_bRGBA = false;
};

DEMO_PLUGIN(CDemoAspect)
