// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Stats.cpp
//	Description: r_Stats, the renderer's statistics at the top left of the
//	screen. The engine's own overlays stay as they are: r_DisplayInfo (the
//	engine's block at the top right, its polygons from GetPolyCount) and
//	the frame profiler (profile 1, the CPU time of the renderer's
//	FUNCTION_PROFILER sections among the others).
//
//	The numbers are averages of half a second: the GPU time of each pass
//	(the device's timestamps between GpuSection calls, as d3d12_GpuTimes
//	logs them) in the order the passes run, with a bar of its share of the
//	GPU's frame; the time between frames, the CPU's wait for the GPU at
//	the start of a frame and in Present; the draws, how many changed the
//	pipeline state, the triangles. r_Stats 2 adds the memory: the process'
//	video memory and its budget (DXGI), the textures and buffers, the
//	acceleration structures of the static geometry, the frame memory and
//	the pipeline states made.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "D3D12Device.h"
#include "D3D12Upload.h"
#include "D3D12Pipeline.h"
#include "RayTracing.h"

namespace
{

constexpr double STATS_WINDOW_MS = 500;   //!< the numbers shown are averages of this long

//! The CPU's clock in milliseconds.
double NowMs()
{
  static const double fMsPerTick = []
  {
    LARGE_INTEGER Frequency;
    QueryPerformanceFrequency(&Frequency);
    return 1000.0 / (double)Frequency.QuadPart;
  }();
  LARGE_INTEGER Now;
  QueryPerformanceCounter(&Now);
  return (double)Now.QuadPart * fMsPerTick;
}

//! n with a comma between thousands (1,234,567).
std::string Thousands(uint64 n)
{
  std::string s = std::to_string(n);
  for (int i = (int)s.size() - 3; i > 0; i -= 3)
    s.insert((size_t)i, ",");
  return s;
}

std::string Format(const char *szFormat, ...)
{
  char szBuf[256];
  va_list Args;
  va_start(Args, szFormat);
  vsnprintf(szBuf, sizeof(szBuf), szFormat, Args);
  va_end(Args);
  szBuf[sizeof(szBuf) - 1] = 0;
  return szBuf;
}

//! One line of the table: a text at the left, a bar (fBar 0 to 1, < 0: none) and two texts
//! aligned at the right of their columns.
struct SStatsLine
{
  std::string Left;
  float fBar = -1;
  std::string Value;
  std::string Share;
  bool bDim = false;              //!< grey: a heading, or a pass that took no time
};

}

void SGpuTimes::Add(const std::vector<std::pair<const char *, double>> &Times, int nTimedFrames)
{
  for (const auto &[szSection, fMs] : Times)
  {
    auto It = std::find_if(Sections.begin(), Sections.end(), [szSection](const auto &e) { return strcmp(e.first, szSection) == 0; });
    if (It == Sections.end())
      Sections.emplace_back(szSection, fMs);
    else
      It->second += fMs;
  }
  nFrames += nTimedFrames;
}

void CD3D12Renderer::CollectStats()
{
  // The device's GPU times since the last frame (a frame's come D3D12_FRAMES frames later).
  if (const int n = m_pDevice->GetGpuTimedFrames())
  {
    if (CV_d3d12_GpuTimes > 0)
      m_GpuReport.Add(m_pDevice->GetGpuTimes(), n);
    if (CV_r_Stats > 0)
      m_StatsSum.Gpu.Add(m_pDevice->GetGpuTimes(), n);
    m_pDevice->ResetGpuTimes();
  }
  const int nDraws = m_nFrameDraws, nPSOChanges = m_nFramePSOChanges;
  m_nFrameDraws = m_nFramePSOChanges = 0;
  if (CV_r_Stats <= 0)
  {
    m_StatsSum = {};
    m_Stats = {};
    m_fStatsStart = 0;
    return;
  }
  const double fNow = NowMs();
  SFrameStats &Sum = m_StatsSum;
  const auto TakeMemory = [this](SFrameStats &Stats)
  {
    m_pDevice->GetVideoMemory(Stats.nVideoUsage, Stats.nVideoBudget);
    Stats.nResourceBytes = m_pUpload ? m_pUpload->GetVideoMemory() : 0;
    Stats.nRayBytes = m_pRayScene ? m_pRayScene->GetStaticBytes() : 0;
    Stats.nPSOs = m_pPipeline ? m_pPipeline->GetPSOCount() : 0;
  };
  if (!m_fStatsStart)
  {
    // The first frame: no time between frames yet. Until the first half second is over the
    // frames so far are shown, with the memory of now.
    m_fStatsStart = m_fStatsLastFrame = fNow;
    TakeMemory(Sum);
    return;
  }
  Sum.nFrames++;
  Sum.fFrameMs += fNow - m_fStatsLastFrame;
  Sum.fWaitMs += m_pDevice->GetWaitMs();
  Sum.fPresentMs += m_pDevice->GetPresentMs();
  Sum.fDraws += nDraws;
  Sum.fPSOChanges += nPSOChanges;
  Sum.fTriangles += m_nPolygons;
  Sum.fFrameMemory += m_pUpload ? (double)m_pUpload->GetFrameMemory() : 0.0;
  m_fStatsLastFrame = fNow;
  if (fNow - m_fStatsStart < STATS_WINDOW_MS)
    return;
  TakeMemory(Sum);
  m_Stats = std::move(Sum);
  Sum = {};
  m_fStatsStart = fNow;
}

void CD3D12Renderer::DrawStats()
{
  if (CV_r_Stats <= 0 || !m_pDevice || !m_pDevice->IsInFrame() || m_nStatsDrawnFrame == m_nFrameID)
    return;
  m_nStatsDrawnFrame = m_nFrameID;
  ICryFont *pCryFont = iSystem ? iSystem->GetICryFont() : nullptr;
  IFFont *pFont = pCryFont ? pCryFont->GetFont("default") : nullptr;
  if (!pFont)
    return;

  // The lines, in the 800 x 600 virtual screen; the frames so far until the first half second is over.
  const SFrameStats &S = m_Stats.nFrames ? m_Stats : m_StatsSum;
  const double fFrames = std::max(S.nFrames, 1), fGpuFrames = std::max(S.Gpu.nFrames, 1);
  double fGpuMs = 0;
  for (const auto &Section : S.Gpu.Sections)
    fGpuMs += Section.second / fGpuFrames;
  std::vector<SStatsLine> Lines;
  const auto Add = [&Lines](std::string Left, bool bDim = false) -> SStatsLine &
  {
    Lines.push_back({ std::move(Left) });
    Lines.back().bDim = bDim;
    return Lines.back();
  };
  std::string Size = Format("%dx%d", m_pDevice->GetWidth(), m_pDevice->GetHeight());
  if (m_pDevice->GetSceneWidth() > 0 && (m_pDevice->GetSceneWidth() != m_pDevice->GetWidth() || m_pDevice->GetSceneHeight() != m_pDevice->GetHeight()))
    Size += Format(", render %dx%d", m_pDevice->GetSceneWidth(), m_pDevice->GetSceneHeight());
  Add(Format("Direct3D 12, %s, %s", m_pDevice->GetAdapterName(), Size.c_str()), true);
  if (!S.nFrames)
  {
    Add("r_Stats: collecting");
  }
  else
  {
    const double fFrameMs = S.fFrameMs / fFrames;
    Add(Format("Frame %.2f ms (%.0f fps), GPU %.2f ms", fFrameMs, fFrameMs > 0 ? 1000.0 / fFrameMs : 0.0, fGpuMs));
    Add(Format("CPU waits for the GPU %.2f ms, in Present %.2f ms", S.fWaitMs / fFrames, S.fPresentMs / fFrames));
    Add(Format("Draws %s, PSO changes %s, triangles %s", Thousands((uint64)(S.fDraws / fFrames + 0.5)).c_str(),
               Thousands((uint64)(S.fPSOChanges / fFrames + 0.5)).c_str(), Thousands((uint64)(S.fTriangles / fFrames + 0.5)).c_str()));
    if (CV_r_Stats >= 2)
    {
      constexpr double MB = 1024.0 * 1024.0;
      if (S.nVideoBudget)
        Add(Format("Video memory %s of %s MB", Thousands((uint64)(S.nVideoUsage / MB)).c_str(), Thousands((uint64)(S.nVideoBudget / MB)).c_str()));
      Add(Format("Textures and buffers %s MB, rays %s MB", Thousands((uint64)(S.nResourceBytes / MB)).c_str(), Thousands((uint64)(S.nRayBytes / MB)).c_str()));
      Add(Format("Frame memory %s KB, PSOs %s", Thousands((uint64)(S.fFrameMemory / fFrames / 1024.0)).c_str(), Thousands(S.nPSOs).c_str()));
    }
    SStatsLine &Heading = Add("GPU per pass", true);
    Heading.Value = "ms";
    Heading.Share = "%";
    if (S.Gpu.Sections.empty())
      Add("(no GPU timestamps)", true);
    for (const auto &[szSection, fSum] : S.Gpu.Sections)
    {
      const double fMs = fSum / fGpuFrames;
      SStatsLine &Line = Add(szSection, fMs < 0.005);
      Line.fBar = fGpuMs > 0 ? (float)(fMs / fGpuMs) : 0.0f;
      Line.Value = Format("%.2f", fMs);
      Line.Share = Format("%.0f", fGpuMs > 0 ? fMs * 100.0 / fGpuMs : 0.0);
    }
  }

  // The panel: the names, the bars, then the milliseconds and the share right aligned.
  // Below the time demo's lines (the game's, at the top left while it plays).
  constexpr float fLeft = 6, fTop = 44, fPad = 4, fChar = 9.5f, fStep = 10.5f;
  constexpr float fBarX = 84, fBarW = 110, fValueRight = 222, fShareRight = 244, fWidth = 250;
  pFont->UseRealPixels(false);
  pFont->SetSameSize(false);
  pFont->SetSize(vector2f(fChar, fChar));
  // GetTextSize is in the screen's pixels (as the game's dialogs scale it).
  const float fTextScale = 800.0f / (float)std::max(m_nWidth, 1);
  const auto TextWidth = [pFont, fTextScale](const std::string &Text) { return pFont->GetTextSize(Text.c_str()).x * fTextScale; };
  float fPanelW = fWidth;
  for (const SStatsLine &Line : Lines)
  {
    if (Line.fBar < 0 && Line.Value.empty())
      fPanelW = std::max(fPanelW, TextWidth(Line.Left) + 2 * fPad);
  }
  const float fPanelH = Lines.size() * fStep + 2 * fPad;

  int SavedViewport[4];
  memcpy(SavedViewport, m_Viewport, sizeof(SavedViewport));
  const bool bSavedScissor = m_bScissor;
  const int nSavedState = m_nState;
  SetViewport(0, 0, 0, 0);
  m_bScissor = false;
  m_nState = GS_NODEPTHTEST | GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA;
  Draw2dImage(fLeft, fTop, fPanelW, fPanelH, -1, 0, 0, 1, 1, 0, 0, 0, 0, 0.6f, 0);
  float y = fTop + fPad;
  for (const SStatsLine &Line : Lines)
  {
    if (Line.fBar > 0)
      Draw2dImage(fLeft + fBarX, y + fStep * 0.2f, std::max(fBarW * std::min(Line.fBar, 1.0f), 0.5f), fStep * 0.6f, -1, 0, 0, 1, 1, 0,
                  1.0f, 0.6f, 0.15f, 0.9f, 0);
    y += fStep;
  }
  y = fTop + fPad;
  for (const SStatsLine &Line : Lines)
  {
    pFont->SetColor(Line.bDim ? color4f(0.65f, 0.65f, 0.65f, 1) : color4f(1, 1, 1, 1));
    pFont->DrawString(fLeft + fPad, y, Line.Left.c_str());
    if (!Line.Value.empty())
      pFont->DrawString(fLeft + fValueRight - TextWidth(Line.Value), y, Line.Value.c_str());
    if (!Line.Share.empty())
      pFont->DrawString(fLeft + fShareRight - TextWidth(Line.Share), y, Line.Share.c_str());
    y += fStep;
  }
  SetViewport(SavedViewport[0], SavedViewport[1], SavedViewport[2], SavedViewport[3]);
  m_bScissor = bSavedScissor;
  m_nState = nSavedState;
}
