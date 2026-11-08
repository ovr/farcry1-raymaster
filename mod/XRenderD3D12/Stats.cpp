// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Stats.cpp
//	Description: r_Stats, the renderer's statistics at the top left of the
//	screen, and r_DisplayInfo at the top right. The engine's frame profiler
//	stays as it is (profile 1, the CPU time of the renderer's
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
//	the pipeline states made. The panel is below the text at the top left
//	(the time demo's lines), or at the top.
//
//	r_DisplayInfo is drawn here too, at the top right in the same way, in
//	place of the engine's own block: the console's variable stays 0 (the
//	engine draws its block when it is not), its value is the renderer's
//	(TakeDisplayInfo). The game's version and level, the view's camera,
//	angles and distance, the frame rate (half a second's, and of its
//	slowest and fastest frames), the triangles and the lights of the frame,
//	the anti-aliasing, the upscaler and the 3D scene's size before and
//	after it. d3d12_DisplayHDR's panel (HdrStats.cpp) is below it: the
//	histogram of the scene's light (log2, before the exposure; the bars
//	grey below the tone map's knee, orange in its shoulder, red at white
//	and above; the marks: white yellow, the knee orange, the meter's mean
//	green), the exposure, its target and range (d3d12_ExposureMeter 1: the
//	histogram's window, the sky's share and ceiling), percentiles of the exposed
//	light, the tone map, how much of the frame is in its shoulder and at
//	white, the bloom and the volumetric light, the output, the display's
//	levels and the output's measured nits.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "D3D12Device.h"
#include "D3D12Upload.h"
#include "D3D12Pipeline.h"
#include "RayTracing.h"
#include "Texture.h"
#include "CpuSampler.h"
#include "HdrStatsMath.h"
#include <CryVersion.h>

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

//! The columns of a panel: the bars, the right edges of the two texts at the right, the width.
struct SPanelColumns
{
  float fBarX, fBarW, fValueRight, fShareRight, fWidth;
};

}

void SGpuTimes::Add(const CD3D12Device &Device, int nTimedFrames)
{
  const auto AddTo = [](std::vector<std::pair<const char *, double>> &Sum, const std::vector<std::pair<const char *, double>> &Times)
  {
    for (const auto &[szSection, fMs] : Times)
    {
      auto It = std::find_if(Sum.begin(), Sum.end(), [szSection](const auto &e) { return strcmp(e.first, szSection) == 0; });
      if (It == Sum.end())
        Sum.emplace_back(szSection, fMs);
      else
        It->second += fMs;
    }
  };
  AddTo(Sections, Device.GetGpuTimes());
  AddTo(Compute, Device.GetComputeGpuTimes());
  nFrames += nTimedFrames;
}

bool CD3D12Renderer::SDisplayInfoSink::OnBeforeVarChange(ICVar *pChanged, const char *szNewValue)
{
  if (pChanged != pVar)
    return true;
  nValue = szNewValue ? atoi(szNewValue) : 0;
  return false;
}

void CD3D12Renderer::TakeDisplayInfo()
{
  if (m_DisplayInfo.pVar || !iConsole)
    return;
  ICVar *pVar = iConsole->GetCVar("r_DisplayInfo");
  if (!pVar)
    return;
  // A value it already has (system.cfg) is taken over.
  m_DisplayInfo.nValue = pVar->GetIVal();
  if (m_DisplayInfo.nValue)
    pVar->Set(0);
  m_DisplayInfo.pVar = pVar;
  iConsole->AddConsoleVarSink(&m_DisplayInfo);
}

double CD3D12Renderer::CpuMs()
{
  return NowMs();
}

void CD3D12Renderer::LogFrame()
{
  CpuSampler::Update(CV_d3d12_CpuSamples, m_nFrameID, "XRenderD3D12_samples.txt");
  const double fNow = NowMs();
  IFrameProfileSystem *pProfiles = iSystem ? iSystem->GetIProfileSystem() : nullptr;
  if (pProfiles && m_bFrameLogProfiles != (CV_d3d12_FrameLog >= 2))
  {
    m_bFrameLogProfiles = CV_d3d12_FrameLog >= 2;
    pProfiles->Enable(m_bFrameLogProfiles, false);
  }
  if (CV_d3d12_FrameLog <= 0)
  {
    m_pFrameLog.reset();
    m_fFrameLogLast = 0;
  }
  else
  {
    if (!m_pFrameLog)
    {
      m_pFrameLog.reset(fopen("XRenderD3D12_frames.txt", "w"));
      if (m_pFrameLog)
        fprintf(m_pFrameLog.get(),
                "# frame, ms since the last frame, the CPU's wait for the GPU, Present, the main view's EF_EndEf3D, of it the engine's "
                "DrawLowDetail for the water's reflection, the sun's cascades, the environment cubes; the GPU's ms (of the frame %d "
                "earlier), draws, PSO changes, triangles, dynamic lights, camera x y z, angles x y z, the GPU's sections; with 2 the self ms of the "
                "engine's last frame by subsystem, and its profilers that took the most; then the frame's barriers (d3d12_BarrierBatch): "
                "ResourceBarrier calls, of them after work, transitions, UAV barriers, UAV barriers of all; last the bytes of the "
                "draws' constants (b1 and b3) and the draws by why they uploaded the rest (b3, DrawExt.h): none (the default "
                "block), d3d12_DrawExt 0, their program, a depth-only draw's vertices, other bytes than the default; the display's "
                "lookups since the last line (the first line: since the start), their ms (GetDisplayInfo, DisplayPoll.h) and "
                "whether the game has the focus\n",
                D3D12_FRAMES);
    }
    if (m_pFrameLog && m_fFrameLogLast > 0)
    {
      const int nGpuFrames = m_pDevice->GetGpuTimedFrames();
      double fGpuMs = 0;
      std::string Sections;
      for (const auto &[szSection, fMs] : m_pDevice->GetGpuTimes())
      {
        fGpuMs += fMs;
        Sections += Format("%s%s=%.3f", Sections.empty() ? "" : ";", szSection, nGpuFrames ? fMs / nGpuFrames : 0.0);
      }
      // The compute queue's beside the frame (not in its time).
      for (const auto &[szSection, fMs] : m_pDevice->GetComputeGpuTimes())
        Sections += Format("%s%s=%.3f", Sections.empty() ? "" : ";", szSection, nGpuFrames ? fMs / nGpuFrames : 0.0);
      // The engine's frame profiler: the self time of its last frame by subsystem, then the
      // profilers that took the most.
      std::string Profiles;
      if (pProfiles && m_bFrameLogProfiles)
      {
        static const char *const szSubsystems[] = { "any", "renderer", "3dengine", "ai", "animation", "movie", "entity", "font",
                                                    "network", "physics", "script", "sound", "editor", "system", "game" };
        static_assert(std::size(szSubsystems) == PROFILE_NETWORK_TRAFFIC);
        float fSubsystem[PROFILE_NETWORK_TRAFFIC] = {};
        std::vector<std::pair<float, const char *>> Self;
        for (int i = 0; i < pProfiles->GetProfilerCount(); i++)
        {
          CFrameProfiler *pProfiler = pProfiles->GetProfiler(i);
          if (!pProfiler || pProfiler->m_subsystem < 0 || pProfiler->m_subsystem >= PROFILE_NETWORK_TRAFFIC)
            continue;
          const float fMs = pProfiler->m_selfTimeHistory.GetLast();
          fSubsystem[pProfiler->m_subsystem] += fMs;
          Self.emplace_back(fMs, pProfiler->m_name);
        }
        for (int i = 0; i < PROFILE_NETWORK_TRAFFIC; i++)
          Profiles += Format("%s%s=%.3f", i ? ";" : ",", szSubsystems[i], fSubsystem[i]);
        const size_t nTop = std::min<size_t>(Self.size(), 8);
        std::partial_sort(Self.begin(), Self.begin() + nTop, Self.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
        for (size_t i = 0; i < nTop; i++)
        {
          std::string Name = Self[i].second ? Self[i].second : "?";
          std::replace_if(Name.begin(), Name.end(), [](char c) { return c == ',' || c == ';' || c == '='; }, ' ');
          Profiles += Format("%s%s=%.3f", i ? ";" : ",", Name.c_str(), Self[i].first);
        }
      }
      const Barriers::SStats &BarrierStats = m_pDevice->GetBarrierStats();
      const std::string BarrierCounts = Format(",barrier_calls=%u;barrier_drains=%u;barrier_transitions=%u;barrier_uav=%u;barrier_uav_all=%u",
                                               BarrierStats.nCalls, BarrierStats.nDrains, BarrierStats.nTransitions, BarrierStats.nUav, BarrierStats.nUavAll);
      const std::string DrawCounts = Format(",draw_bytes=%u;ext_default=%d;ext_all=%d;ext_program=%d;ext_vertex=%d;ext_differs=%d",
                                            m_nFrameDrawBytes, m_nFrameDrawExt[DrawExt::REASON_DEFAULT], m_nFrameDrawExt[DrawExt::REASON_ALL],
                                            m_nFrameDrawExt[DrawExt::REASON_PROGRAM], m_nFrameDrawExt[DrawExt::REASON_VERTEX],
                                            m_nFrameDrawExt[DrawExt::REASON_DIFFERS]);
      const std::string DisplayCounts = Format(",display_lookups=%u;display_lookup_ms=%.3f;display_active=%d", m_pDevice->GetDisplayLookups() - m_nFrameLogDisplayLookups,
                                               m_pDevice->GetDisplayLookupMs() - m_fFrameLogDisplayMs, m_pDevice->IsAppActive() ? 1 : 0);
      const CCamera &Camera = iSystem->GetViewCamera();
      const Vec3 &vPos = Camera.GetPos();
      const Ang3 vAngles = Camera.GetAngles();
      fprintf(m_pFrameLog.get(), "%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d,%d,%d,%.1f,%.1f,%.1f,%.0f,%.0f,%.0f,%s%s%s%s%s\n", m_nFrameID,
              fNow - m_fFrameLogLast, m_pDevice->GetWaitMs(), m_pDevice->GetPresentMs(), m_fFrameEndEf3DMs, m_fFrameLowDetailMs[LD_WATER],
              m_fFrameLowDetailMs[LD_CASCADES], m_fFrameLowDetailMs[LD_ENVCUBES], nGpuFrames ? fGpuMs / nGpuFrames : -1.0, m_nFrameDraws,
              m_nFramePSOChanges, m_nPolygons, m_nFrameDynLights, vPos.x, vPos.y, vPos.z, vAngles.x, vAngles.y, vAngles.z, Sections.c_str(),
              Profiles.c_str(), BarrierCounts.c_str(), DrawCounts.c_str(), DisplayCounts.c_str());
      // Since the last line: the first line has the lookups since the start (the first one, when the swap chain came).
      m_nFrameLogDisplayLookups = m_pDevice->GetDisplayLookups();
      m_fFrameLogDisplayMs = m_pDevice->GetDisplayLookupMs();
    }
    m_fFrameLogLast = fNow;
  }
  m_fFrameEndEf3DMs = 0;
  for (double &fMs : m_fFrameLowDetailMs)
    fMs = 0;
}

void CD3D12Renderer::CollectStats()
{
  LogFrame();
  // The device's GPU times since the last frame (a frame's come D3D12_FRAMES frames later).
  if (const int n = m_pDevice->GetGpuTimedFrames())
  {
    if (CV_d3d12_GpuTimes > 0)
      m_GpuReport.Add(*m_pDevice, n);
    if (CV_r_Stats > 0)
      m_StatsSum.Gpu.Add(*m_pDevice, n);
    m_pDevice->ResetGpuTimes();
  }
  const int nDraws = m_nFrameDraws, nPSOChanges = m_nFramePSOChanges;
  const int nDrawExt = nDraws - m_nFrameDrawExt[DrawExt::REASON_DEFAULT];
  const uint32 nDrawBytes = m_nFrameDrawBytes;
  m_nFrameDraws = m_nFramePSOChanges = m_nFrameAlphaCoverage = 0;
  m_nFrameDrawBytes = 0;
  for (int &n : m_nFrameDrawExt)
    n = 0;
  if (CV_r_Stats <= 0 && m_DisplayInfo.nValue <= 0)
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
    if (m_pTextures)
      m_pTextures->UpscaledStats(Stats.nUpscaled, Stats.nTextures, Stats.nUpscaledBytes, Stats.nTextureBytes);
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
  const double fFrameMs = fNow - m_fStatsLastFrame;
  Sum.fMinFrameMs = Sum.nFrames ? std::min(Sum.fMinFrameMs, fFrameMs) : fFrameMs;
  Sum.fMaxFrameMs = Sum.nFrames ? std::max(Sum.fMaxFrameMs, fFrameMs) : fFrameMs;
  Sum.nFrames++;
  Sum.fFrameMs += fFrameMs;
  Sum.fWaitMs += m_pDevice->GetWaitMs();
  Sum.fPresentMs += m_pDevice->GetPresentMs();
  Sum.fDraws += nDraws;
  Sum.fPSOChanges += nPSOChanges;
  Sum.fDrawExt += nDrawExt;
  Sum.fDrawBytes += nDrawBytes;
  Sum.fTriangles += m_nPolygons;
  const Barriers::SStats &BarrierStats = m_pDevice->GetBarrierStats();
  Sum.fBarrierCalls += BarrierStats.nCalls;
  Sum.fBarrierDrains += BarrierStats.nDrains;
  Sum.fBarriers += BarrierStats.nTransitions + BarrierStats.nUav + BarrierStats.nUavAll + BarrierStats.nOther;
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
  const bool bStats = CV_r_Stats > 0, bInfo = m_DisplayInfo.nValue > 0, bHdr = CV_d3d12_DisplayHDR > 0;
  if ((!bStats && !bInfo && !bHdr) || !m_pDevice || !m_pDevice->IsInFrame() || m_nStatsDrawnFrame == m_nFrameID)
    return;
  m_nStatsDrawnFrame = m_nFrameID;
  ICryFont *pCryFont = iSystem ? iSystem->GetICryFont() : nullptr;
  IFFont *pFont = pCryFont ? pCryFont->GetFont("default") : nullptr;
  if (!pFont)
    return;

  // The panels, in the 800 x 600 virtual screen: the names, the bars, then the values right
  // aligned in their columns.
  constexpr float fMargin = 6, fPad = 4, fChar = 9.5f, fStep = 10.5f;
  pFont->UseRealPixels(false);
  pFont->SetSameSize(false);
  pFont->SetSize(vector2f(fChar, fChar));
  // GetTextSize is in the screen's pixels (as the game's dialogs scale it).
  const float fTextScale = 800.0f / (float)std::max(m_nWidth, 1);
  const auto TextWidth = [pFont, fTextScale](const std::string &Text) { return pFont->GetTextSize(Text.c_str()).x * fTextScale; };
  const auto PanelWidth = [&TextWidth](const std::vector<SStatsLine> &Lines, const SPanelColumns &Columns)
  {
    float fWidth = Columns.fWidth;
    for (const SStatsLine &Line : Lines)
    {
      if (Line.fBar < 0 && Line.Value.empty())
        fWidth = std::max(fWidth, TextWidth(Line.Left) + 2 * fPad);
    }
    return fWidth;
  };
  const auto DrawPanel = [&](const std::vector<SStatsLine> &Lines, float fLeft, float fTop, const SPanelColumns &Columns)
  {
    Draw2dImage(fLeft, fTop, PanelWidth(Lines, Columns), Lines.size() * fStep + 2 * fPad, -1, 0, 0, 1, 1, 0, 0, 0, 0, 0.6f, 0);
    float y = fTop + fPad;
    for (const SStatsLine &Line : Lines)
    {
      if (Line.fBar > 0)
        Draw2dImage(fLeft + Columns.fBarX, y + fStep * 0.2f, std::max(Columns.fBarW * std::min(Line.fBar, 1.0f), 0.5f), fStep * 0.6f, -1, 0, 0, 1,
                    1, 0, 1.0f, 0.6f, 0.15f, 0.9f, 0);
      y += fStep;
    }
    y = fTop + fPad;
    for (const SStatsLine &Line : Lines)
    {
      pFont->SetColor(Line.bDim ? color4f(0.65f, 0.65f, 0.65f, 1) : color4f(1, 1, 1, 1));
      pFont->DrawString(fLeft + fPad, y, Line.Left.c_str());
      if (!Line.Value.empty())
        pFont->DrawString(fLeft + Columns.fValueRight - TextWidth(Line.Value), y, Line.Value.c_str());
      if (!Line.Share.empty())
        pFont->DrawString(fLeft + Columns.fShareRight - TextWidth(Line.Share), y, Line.Share.c_str());
      y += fStep;
    }
  };

  int SavedViewport[4];
  memcpy(SavedViewport, m_Viewport, sizeof(SavedViewport));
  const bool bSavedScissor = m_bScissor;
  const int nSavedState = m_nState;
  SetViewport(0, 0, 0, 0);
  m_bScissor = false;
  m_nState = GS_NODEPTHTEST | GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA;

  // The frames so far until the first half second is over.
  const SFrameStats &S = m_Stats.nFrames ? m_Stats : m_StatsSum;
  float fInfoBottom = 0;                  // below r_DisplayInfo's panel
  const double fFrames = std::max(S.nFrames, 1);
  std::vector<SStatsLine> Lines;
  const auto Add = [&Lines](std::string Left, bool bDim = false) -> SStatsLine &
  {
    Lines.push_back({ std::move(Left) });
    Lines.back().bDim = bDim;
    return Lines.back();
  };
  if (bStats)
  {
    const double fGpuFrames = std::max(S.Gpu.nFrames, 1);
    double fGpuMs = 0;
    for (const auto &Section : S.Gpu.Sections)
      fGpuMs += Section.second / fGpuFrames;
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
      Add(Format("Draw constants %.2f MB, the rest uploaded by %s draws%s", S.fDrawBytes / fFrames / (1024.0 * 1024.0),
                 Thousands((uint64)(S.fDrawExt / fFrames + 0.5)).c_str(), CV_d3d12_DrawExt > 0 ? "" : " (all)"));
      Add(Format("Barrier calls %s (%s after work), barriers %s%s", Thousands((uint64)(S.fBarrierCalls / fFrames + 0.5)).c_str(),
                 Thousands((uint64)(S.fBarrierDrains / fFrames + 0.5)).c_str(), Thousands((uint64)(S.fBarriers / fFrames + 0.5)).c_str(),
                 CV_d3d12_BarrierBatch > 0 ? "" : ", each at once"));
      if (CV_r_Stats >= 2)
      {
        constexpr double MB = 1024.0 * 1024.0;
        if (S.nVideoBudget)
          Add(Format("Video memory %s of %s MB", Thousands((uint64)(S.nVideoUsage / MB)).c_str(), Thousands((uint64)(S.nVideoBudget / MB)).c_str()));
        Add(Format("Textures and buffers %s MB, rays %s MB", Thousands((uint64)(S.nResourceBytes / MB)).c_str(), Thousands((uint64)(S.nRayBytes / MB)).c_str()));
        Add(Format("Textures %s (%s MB), upscaled %s (%s MB)", Thousands((uint64)S.nTextures).c_str(), Thousands((uint64)(S.nTextureBytes / MB)).c_str(),
                   Thousands((uint64)S.nUpscaled).c_str(), Thousands((uint64)(S.nUpscaledBytes / MB)).c_str()));
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
      // Beside the frame, not in its time.
      for (const auto &[szSection, fSum] : S.Gpu.Compute)
      {
        const double fMs = fSum / fGpuFrames;
        SStatsLine &Line = Add(szSection, true);
        Line.fBar = fGpuMs > 0 ? (float)(fMs / fGpuMs) : 0.0f;
        Line.Value = Format("%.2f", fMs);
      }
    }
    // At the top left, below the time demo's lines (the game's, while it plays).
    const float fTop = std::max(fMargin, std::max(m_fTopLeftText[0], m_fTopLeftText[1]) + fPad);
    DrawPanel(Lines, fMargin, fTop, { 84, 110, 222, 244, 250 });
  }

  if (bInfo)
  {
    // r_DisplayInfo at the top right: the names, and their values right aligned after the longest.
    Lines.clear();
    const auto Value = [&Add](const char *szName, std::string Text) { Add(szName).Value = std::move(Text); };
    char szVersion[64] = "";
    iSystem->GetFileVersion().ToString(szVersion);
    // The level is the folder of its files (Levels/Training/).
    I3DEngine *p3DEngine = iSystem->GetI3DEngine();
    std::string Level = p3DEngine && p3DEngine->GetLevelFilePath("") ? p3DEngine->GetLevelFilePath("") : "";
    while (!Level.empty() && (Level.back() == '/' || Level.back() == '\\'))
      Level.pop_back();
    Level.erase(0, Level.find_last_of("/\\") + 1);
    Add(Format("Far Cry %s, level %s", szVersion, Level.empty() ? "none" : Level.c_str()), true);
    const CCamera &Camera = iSystem->GetViewCamera();
    const Vec3 &vPos = Camera.GetPos();
    const Ang3 vAngles = Camera.GetAngles();
    Value("Camera", Format("%.1f %.1f %.1f", vPos.x, vPos.y, vPos.z));
    Value("Angles", Format("%.0f %.0f %.0f", vAngles.x, vAngles.y, vAngles.z));
    Value("View distance", Format("%.0f m", Camera.GetZMax()));
    if (!S.nFrames || S.fFrameMs <= 0)
    {
      Value("FPS", "collecting");
    }
    else
    {
      // The average, and the slowest and the fastest frame's.
      Value("FPS", Format("%.0f (%.0f to %.0f)", 1000.0 * fFrames / S.fFrameMs, S.fMaxFrameMs > 0 ? 1000.0 / S.fMaxFrameMs : 0.0,
                          S.fMinFrameMs > 0 ? 1000.0 / S.fMinFrameMs : 0.0));
      Value("Triangles", Thousands((uint64)(S.fTriangles / fFrames + 0.5)));
      Value("Triangles per second", Thousands((uint64)(S.fTriangles * 1000.0 / S.fFrameMs + 0.5)));
    }
    // The main view's (none yet in a frame read before its EF_EndEf3D).
    Value("Lights", m_nFrameSunLights ? Format("sun, %d dynamic", m_nFrameDynLights) : Format("%d dynamic", m_nFrameDynLights));
    // The ambient occlusion's backend as it is made (UpdateAO), the rays' GI in its place.
    static const struct { const char *szCvar, *szName; } s_AONames[] = { { "gtao", "XeGTAO" }, { "cacao", "CACAO" }, { "rt", "by rays" } };
    std::string AO = "none";
    if (m_pAO && m_bAOGI)
      AO = CV_d3d12_RTGI >= 2 ? "GI by rays (a ray a pixel)" : "GI by rays";
    else if (m_pAO)
    {
      AO = m_AOName;
      for (const auto &n : s_AONames)
        if (!_stricmp(m_AOName.c_str(), n.szCvar))
          AO = n.szName;
    }
    // The path tracer lights the opaque surfaces itself, without it.
    const bool bPathTraced = CV_d3d12_PathTracing > 0 && WantsPathTracing() && m_PathTrace.nHistoryFrame >= m_nFrames - 1;
    if (bPathTraced)
      AO = "none (path tracing)";
    Value("Ambient occlusion", AO);
    // The effects by rays (as their variables ask; each needs the device's DXR 1.1) and the last scene for them.
    if (!m_pRayScene || !m_pRayScene->IsSupported())
      Value("Ray tracing", "not supported");
    else if (!WantsRays())
      Value("Ray tracing", "off");
    else
    {
      const bool bScene = m_pRayScene->GetBuiltFrame() >= m_nFrames - 1 && m_pRayScene->GetSceneInstances();
      Value("Ray tracing", bScene ? Format("%s instances, %s triangles", Thousands(m_pRayScene->GetSceneInstances()).c_str(),
                                           Thousands(m_pRayScene->GetSceneTriangles()).c_str())
                                  : std::string("on, no scene now"));
      std::string Shadows;
      if (WantsRaySun())
        Shadows = CV_d3d12_RTSunShadows >= 2 ? "sun (a ray a pixel)" : "sun";
      if (WantsRayLights())
        Shadows += Shadows.empty() ? "lamps" : ", lamps";
      if (!Shadows.empty())
        Value("RT shadows", Shadows);
      if (CV_d3d12_RTReflections > 0)
        Value("RT reflections", std::string(CV_d3d12_RTReflections >= 2 ? "water (hybrid)" : "water") + (WantsRaySurfaces() ? ", cube map surfaces" : ""));
      if (CV_d3d12_RTDebug != 0)
        Value("RT debug view", Format("%d", CV_d3d12_RTDebug));
    }
    // The path tracer as the last frame traced it (PathTrace.cpp; d3d12_PathTracing is there with DXR 1.1 alone).
    if (m_pRayScene && m_pRayScene->IsSupported() && CV_d3d12_PathTracing > 0)
    {
      const SPathTraceState &s = m_PathTrace;
      if (!IsHdrOn())
        Value("Path tracing", "waits for HDR");
      else if (!bPathTraced)
        Value("Path tracing", "on, no 3D scene now");
      else if (s.bReference)
        Value("Path tracing", Format("reference, %d bounces, %d frames", s.nBounces, s.nAccumulated));
      else
        Value("Path tracing", Format("%s, %d bounce%s%s%s", s.szDenoiser, s.nBounces, s.nBounces == 1 ? "" : "s",
                                     s.bCache ? ", cache" : "", s.bCheckerboard ? ", checkerboard" : ""));
    }
    // The anti-aliasing (the upscaler's, else d3d12_AA's: PostAA.cpp), the upscaler and the 3D scene's size before and
    // after it.
    const SUpscaleInfo Up = UpscaleInfo();
    const int nWidth = m_pDevice->GetWidth(), nHeight = m_pDevice->GetHeight();
    if (Up.szName)
    {
      const std::string PostAA = AntiAliasing() != EAntiAlias::None && m_pAAVar ? Format(", d3d12_AA %s not used", m_pAAVar->GetString()) : "";
      Value("Anti-aliasing", Format("FSR, temporal%s%s", Up.bThisFrame ? "" : " (no 3D scene now)", PostAA.c_str()));
      Value("Upscaler", Format("%s, %s%s", Up.szName, Up.szMode, Up.bHdr ? ", HDR input" : ""));
      Value("Resolution", Up.nRenderWidth == nWidth && Up.nRenderHeight == nHeight
                            ? Format("%dx%d, native", nWidth, nHeight)
                            : Format("%dx%d to %dx%d", Up.nRenderWidth, Up.nRenderHeight, nWidth, nHeight));
    }
    else
    {
      Value("Anti-aliasing", PostAAInfo());
      Value("Upscaler", _stricmp(Up.szWanted, "none") != 0 && Up.szWanted[0] ? Format("none (%s is not available)", Up.szWanted) : "none");
      Value("Resolution", Format("%dx%d", nWidth, nHeight));
    }
    float fNameWidth = 0, fValueWidth = 0;
    for (const SStatsLine &Line : Lines)
    {
      if (!Line.Value.empty())
      {
        fNameWidth = std::max(fNameWidth, TextWidth(Line.Left));
        fValueWidth = std::max(fValueWidth, TextWidth(Line.Value));
      }
    }
    const float fValueRight = fPad + fNameWidth + 12 + fValueWidth;
    const SPanelColumns Columns = { 0, 0, fValueRight, fValueRight, fValueRight + fPad };
    DrawPanel(Lines, 800 - fMargin - PanelWidth(Lines, Columns), fMargin, Columns);
    fInfoBottom = fMargin + Lines.size() * fStep + 2 * fPad;
  }

  if (bHdr)
  {
    // d3d12_DisplayHDR below it (HdrStats.cpp): the frame of D3D12_FRAMES before.
    Lines.clear();
    const auto Value = [&Add](const char *szName, std::string Text) { Add(szName).Value = std::move(Text); };
    const SHdrStatsFrame &f = m_HdrStats.Last;
    const bool bOn = IsHdrOn();
    const bool bFresh = bOn && f.nFrame >= 0 && m_nFrameID - f.nFrame <= D3D12_FRAMES + 2 && f.bScene;
    const uint32 *w = f.Words;
    const auto Float = [](uint32 n) { float x; memcpy(&x, &n, sizeof(x)); return x; };
    Add(!bOn ? std::string("HDR: r_HDRRendering is 0")
             : bFresh ? Format("HDR, frame %d (%d frames ago)", f.nFrame, m_nFrameID - f.nFrame) : std::string("HDR: no 3D scene now"), true);
    // The histogram's rows (drawn over them below), then its axis.
    constexpr int HIST_ROWS = 5;
    const size_t nHistLine = Lines.size();
    for (int i = 0; i < HIST_ROWS; i++)
      Add("");
    Add(Format("light (log2) %d", XR_HDR_HIST_MIN), true).Value = Format("%+d", XR_HDR_HIST_MAX);
    const float fEV = bFresh ? Float(w[XR_HDR_STAT_EV]) : 0, fMeter = bFresh ? Float(w[XR_HDR_STAT_METER]) : 0;
    const float fKneeLog = f.nToneMap == 0 ? 0.0f : 2.2f * log2f(std::max(f.fKnee, 1e-3f));
    if (bFresh)
    {
      const double fSamples = std::max<double>(w[XR_HDR_STAT_SAMPLES], 1);
      if (f.bAuto && f.bHistogram)
      {
        // d3d12_ExposureMeter 1: the target, the window's mean, the sky's ceiling.
        const float fTarget = std::clamp(Float(w[XR_HDR_STAT_TARGET]), f.fExposureMin, f.fExposureMax);
        const float fCeiling = Float(w[XR_HDR_STAT_CEILING]), fSky = Float(w[XR_HDR_STAT_SKY]);
        Value("Exposure", Format("%+.2f EV (target %+.2f, %g to %g)", fEV, fTarget, f.fExposureMin, f.fExposureMax));
        Value("Meter: histogram", Format("mean %.2f of %.0f-%.0f %% (key %.2f)", fMeter, 100 * f.fLow, 100 * f.fHigh, HDR_EXPOSURE_KEY_LOG2));
        Value("Sky", fCeiling < XR_EXPOSURE_NONE ? Format("%.1f %% of the frame, at the knee %+.2f EV", 100 * fSky, fCeiling)
                                                 : Format("%.1f %% of the frame", 100 * fSky));
      }
      else if (f.bAuto)
      {
        const float fTarget = std::clamp(HDR_EXPOSURE_KEY_LOG2 - fMeter, f.fExposureMin, f.fExposureMax);
        Value("Exposure", Format("%+.2f EV (target %+.2f, %g to %g)", fEV, fTarget, f.fExposureMin, f.fExposureMax));
        Value("Meter's mean", Format("%.2f (key %.2f)", fMeter, HDR_EXPOSURE_KEY_LOG2));
      }
      else
        Value("Exposure", Format("%+.2f EV (fixed)", fEV));
      // The scene's light exposed, in EV from the game's white.
      const uint32 *pLuma = &w[XR_HDR_STAT_LUMA];
      Value("Exposed p50 / p95 / p99", Format("%+.1f / %+.1f / %+.1f EV", HdrHistPercentile(pLuma, 0.5) + fEV, HdrHistPercentile(pLuma, 0.95) + fEV,
                                              HdrHistPercentile(pLuma, 0.99) + fEV));
      static const char *const szToneMaps[] = { "clamp", "soft shoulder", "ACES fitted", "AgX", "neutral" };
      Value("Tone map", f.nToneMap == 0 ? std::string(szToneMaps[0]) : Format("%s, knee %.2f", szToneMaps[f.nToneMap], f.fKnee));
      Value("In the shoulder / at white", Format("%.1f %% / %.1f %%", 100 * w[XR_HDR_STAT_SHOULDER] / fSamples, 100 * w[XR_HDR_STAT_WHITE] / fSamples));
      Value("Bloom, volumetric light", Format("%g, %s", f.fBloom, f.bVolumetric ? "on" : "off"));
    }
    // The output and the display.
    const SDisplayInfo &Display = m_pDevice->GetDisplayInfo();
    const EDisplayOutput Output = m_pDevice->GetOutput();
    Value("Output", Output == EDisplayOutput::Sdr ? std::string(m_pDevice->HasLayer() ? "SDR (HDR10 captured)" : "SDR")
                                                  : Format("%s, %g / %g nits", Output == EDisplayOutput::Hdr10 ? "HDR10" : "scRGB", m_HdrOut.fPaperWhite,
                                                           m_HdrOut.fPeak));
    if (m_HdrOut.fFrameAverage > 0 && m_pDevice->HasLayer())
      Value("Frame average limit", Format("%g nits", m_HdrOut.fFrameAverage));
    if (Display.szName[0])
      Value("Display", Display.bHdr ? Format("HDR, peak %.0f, full frame %.0f, SDR white %.0f", Display.fMaxNits, Display.fMaxFullFrameNits,
                                             Display.fSdrWhiteNits)
                                    : std::string("SDR"));
    if (bFresh && f.bOutput && w[XR_HDR_STAT_OUT_PIXELS])
    {
      const double fOut = w[XR_HDR_STAT_OUT_PIXELS];
      Value("Nits: average, MaxCLL", Format("%.0f, %.0f (of %g / %g)", HdrOutNitsSum(w[XR_HDR_STAT_OUT_SUM], w[XR_HDR_STAT_OUT_SUM_HIGH]) / fOut,
                                           Float(w[XR_HDR_STAT_OUT_MAX]), f.fPaperWhite, f.fPeak));
      Value("Above paper white / at peak", Format("%.1f %% / %.1f %%", 100 * w[XR_HDR_STAT_OUT_PAPER] / fOut, 100 * w[XR_HDR_STAT_OUT_PEAK] / fOut));
    }
    float fNameWidth = 0, fValueWidth = 0;
    for (const SStatsLine &Line : Lines)
    {
      if (!Line.Value.empty())
      {
        fNameWidth = std::max(fNameWidth, TextWidth(Line.Left));
        fValueWidth = std::max(fValueWidth, TextWidth(Line.Value));
      }
    }
    const float fValueRight = std::max(fPad + fNameWidth + 12 + fValueWidth, 200.0f);
    const SPanelColumns Columns = { 0, 0, fValueRight, fValueRight, fValueRight + fPad };
    const float fWidth = PanelWidth(Lines, Columns), fLeft = 800 - fMargin - fWidth, fTop = fInfoBottom + fMargin;
    DrawPanel(Lines, fLeft, fTop, Columns);
    if (bFresh)
    {
      // The bars: the samples of each bin (by the square root, so that small ones show), grey up to
      // the tone map's knee, orange in its shoulder, red at white and above; the marks: white
      // (yellow), the knee (orange), the meter's mean (green).
      const uint32 *pLuma = &w[XR_HDR_STAT_LUMA];
      uint32 nMost = 1;
      for (int i = 0; i < XR_HDR_HIST_BINS; i++)
        nMost = std::max(nMost, pLuma[i]);
      const float fX = fLeft + fPad, fW = fWidth - 2 * fPad, fBottom = fTop + fPad + (nHistLine + HIST_ROWS) * fStep - 1;
      const float fH = HIST_ROWS * fStep - 2, fBin = fW / XR_HDR_HIST_BINS;
      for (int i = 0; i < XR_HDR_HIST_BINS; i++)
      {
        if (!pLuma[i])
          continue;
        const float h = std::max(fH * sqrtf((float)pLuma[i] / nMost), 0.5f);
        const float fTopLog = HdrHistLog2(i + 1.0f) + fEV;
        const bool bWhite = fTopLog > 0, bShoulder = fTopLog > fKneeLog;
        Draw2dImage(fX + i * fBin, fBottom - h, std::max(fBin - 0.5f, 0.5f), h, -1, 0, 0, 1, 1, 0, 1, bWhite ? 0.25f : bShoulder ? 0.6f : 1.0f,
                    bShoulder ? 0.15f : 1.0f, bShoulder ? 0.9f : 0.55f, 0);
      }
      const auto Mark = [&](float fLog2, float r, float g, float b)
      {
        const float fAt = HdrHistBinOf(fLog2);
        if (fAt >= 0 && fAt <= XR_HDR_HIST_BINS)
          Draw2dImage(fX + fAt * fBin - 0.5f, fBottom - fH, 1, fH, -1, 0, 0, 1, 1, 0, r, g, b, 1, 0);
      };
      Mark(-fEV, 1, 0.95f, 0.2f);
      if (f.nToneMap != 0)
        Mark(fKneeLog - fEV, 1, 0.55f, 0.1f);
      if (f.bAuto)
        Mark(fMeter, 0.3f, 1, 0.3f);
    }
  }

  SetViewport(SavedViewport[0], SavedViewport[1], SavedViewport[2], SavedViewport[3]);
  m_bScissor = bSavedScissor;
  m_nState = nSavedState;
}
