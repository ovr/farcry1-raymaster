// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: HdrOutput.cpp
//	Description: The HDR output to the display (d3d12_HDROutput: 1 HDR10,
//	2 scRGB), with r_HDRRendering on a display in HDR mode.
//
//	The frame is drawn as without it, into an 8-bit back buffer: the
//	screen effects, the 2D and its masks through the back buffer's alpha,
//	the screen copies and every read of the frame stay as they are. The
//	back buffer is then the device's frame target, and with it goes the
//	highlight layer (D3D12Device.h, Common.hlsli): the pass that puts the
//	scene into the back buffer (the HDR resolve, or the upscaler's picture)
//	also writes, per pixel, the ratio of the scene's light on the HDR
//	display to the light of its SDR colour, and a fingerprint of that
//	colour. The tone map is continued above its knee up to the display's
//	peak instead of to white (HighlightRatio, Hdr.hlsl); below the knee
//	the ratio is 1. Before the frame is presented, the output pass
//	(Hdr.hlsl, HDR_OUTPUT) gives each pixel its light: the SDR colour's
//	(HdrDecode), times the ratio where the fingerprint still matches, i.e.
//	where nothing was drawn over the scene since, times paper white; then
//	PQ of Rec. 2020 for HDR10, or linear Rec. 709 at 80 nits per 1 for
//	scRGB. So the HUD, the menu and the screen effects show at paper white
//	as SDR content does under Windows HDR, the scene as in SDR up to the
//	knee, and its highlights (the sun, fire, lamps) up to the peak. Where
//	a screen effect changes the scene's pixels (night vision, the blur, the
//	flash, motion blur) they show without highlights.
//
//	Paper white is Windows' SDR white level of the display (the SDR content
//	brightness slider), or d3d12_HDRPaperWhite; the peak is the display's
//	(DXGI_OUTPUT_DESC1), or d3d12_HDRPeak.
//
//	The frame average limit (d3d12_HDRFrameAverage, Shaders/FrameAverage.hlsl,
//	HdrFrameAverage.hlsli): a display holds its peak in a small part of the
//	screen only, an OLED dims the whole picture by itself when a frame's
//	average is above its full frame light (DXGI_OUTPUT_DESC1, or
//	d3d12_HDRFullFrame). Before the output pass two kernels add up the
//	frame's light (of each pixel's brightest channel, as MaxFALL), the SDR
//	picture's and what the highlights add, and give the factor of the
//	highlights that keeps the average at the limit: down within
//	FALL_DOWN_SECONDS, back within FALL_UP_SECONDS. The SDR picture stays;
//	the HDR10 metadata's MaxFALL is the limit (paper white at least).
//
//	The output follows the display:
//	the HDR mode switched off in Windows, or the window on a display in
//	SDR, and it is SDR again (the log says why).
//
//	XRenderD3D12_ReadFrameHdr10 (for the demos' HDR10 videos) runs the
//	output pass in HDR10 into a target of its own and reads it back; from
//	its first call on the highlight layer is kept whatever the display
//	(the levels then 203 and 1000 nits, unless the cvars set them).
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "D3D12Device.h"
#include "D3D12Upload.h"
#include "D3D12Descriptors.h"
#include "D3D12Pipeline.h"
#include "D3D12Compute.h"

namespace
{

void Set4(float v[4], float x, float y, float z, float w)
{
  v[0] = x; v[1] = y; v[2] = z; v[3] = w;
}

//! Paper white when neither the display nor d3d12_HDRPaperWhite gives one: the reference white of
//! HDR video (ITU-R BT.2408).
constexpr float DEFAULT_PAPER_WHITE = 203;
//! The peak when the display reports none.
constexpr float DEFAULT_PEAK = 1000;

//! The frame average limit's samples of a frame (about; a sample every so many pixels across and down).
constexpr float FALL_SAMPLES = 65536.0f;
//! The time its factor of the highlights takes from 1 to 0 at most (a frame getting brighter), and back.
constexpr float FALL_DOWN_SECONDS = 0.1f;
constexpr float FALL_UP_SECONDS = 0.5f;
constexpr UINT FALL_WORDS_BYTES = XR_FALL_WORDS * sizeof(uint32);
constexpr int KERNEL_TABLE = CD3D12Compute::COMPUTE_SRVS + CD3D12Compute::COMPUTE_UAVS;

const char *OutputName(EDisplayOutput Output)
{
  return Output == EDisplayOutput::Hdr10 ? "HDR10 (PQ, Rec. 2020)" : Output == EDisplayOutput::ScRgb ? "scRGB (linear, Rec. 709)" : "SDR";
}

}

void CD3D12Renderer::GetHdrLevels(const SDisplayInfo &Info, float &fPaperWhite, float &fPeak) const
{
  fPaperWhite = CV_d3d12_HDRPaperWhite > 0 ? CV_d3d12_HDRPaperWhite : Info.bHdr && Info.fSdrWhiteNits > 0 ? Info.fSdrWhiteNits : DEFAULT_PAPER_WHITE;
  fPeak = CV_d3d12_HDRPeak > 0 ? CV_d3d12_HDRPeak : Info.bHdr && Info.fMaxNits > 0 ? Info.fMaxNits : DEFAULT_PEAK;
  fPaperWhite = std::clamp(fPaperWhite, 1.0f, 10000.0f);
  fPeak = std::clamp(fPeak, fPaperWhite, 10000.0f);
}

float CD3D12Renderer::GetFrameAverageLimit(const SDisplayInfo &Info, float fPeak) const
{
  const float fFullFrame = CV_d3d12_HDRFullFrame > 0 ? CV_d3d12_HDRFullFrame : Info.bHdr ? Info.fMaxFullFrameNits : 0;
  const float fLimit = std::max(CV_d3d12_HDRFrameAverage, 0.0f) * fFullFrame;
  return fLimit > 0 && fLimit < fPeak ? fLimit : 0;
}

void CD3D12Renderer::PublishHdrDisplay(const SDisplayInfo &Info)
{
  // Set through the console, so that it and the scripts show the same.
  const float fValues[4] = { !Info.szName[0] ? -1.0f : Info.bHdr ? 1.0f : 0.0f, Info.fMaxNits, Info.fMaxFullFrameNits, Info.bHdr ? Info.fSdrWhiteNits : 0 };
  for (int i = 0; i < 4; i++)
  {
    if (fValues[i] == m_HdrOut.fDisplay[i] || !m_HdrOut.pDisplay[i])
      continue;
    m_HdrOut.fDisplay[i] = fValues[i];
    char szValue[32];
    snprintf(szValue, sizeof(szValue), "%g", fValues[i]);
    m_HdrOut.pDisplay[i]->ForceSet(szValue);
  }
}

void CD3D12Renderer::PublishHdrState(EDisplayOutput Output, const std::string &Reason)
{
  const std::string State = Output == EDisplayOutput::Hdr10 ? "HDR10" : Output == EDisplayOutput::ScRgb ? "scRGB" : Reason.empty() ? "SDR" : "SDR: " + Reason;
  if (State == m_HdrOut.State)
    return;
  m_HdrOut.State = State;
  if (m_HdrOut.pState)
    m_HdrOut.pState->ForceSet(State.c_str());
}

void CD3D12Renderer::UpdateHdrOutput()
{
  if (!m_pDevice)
    return;
  const int nWanted = std::clamp(CV_d3d12_HDROutput, 0, 2);
  if (!nWanted && !m_HdrOut.bCapture && m_pDevice->GetOutput() == EDisplayOutput::Sdr && !m_pDevice->HasLayer())
  {
    m_HdrOut.Reason.clear();
    PublishHdrState(EDisplayOutput::Sdr, m_HdrOut.Reason);
    // The display's variables for the menu's HDR page, a few times a second (its state is read at most once a second).
    if (--m_HdrOut.nDisplayPoll <= 0)
    {
      m_HdrOut.nDisplayPoll = 30;
      PublishHdrDisplay(m_pDevice->GetDisplayInfo());
    }
    return;
  }
  EDisplayOutput Output = EDisplayOutput::Sdr;
  std::string Reason;
  const SDisplayInfo &Info = m_pDevice->GetDisplayInfo();
  PublishHdrDisplay(Info);
  GetHdrLevels(Info, m_HdrOut.fPaperWhite, m_HdrOut.fPeak);
  m_HdrOut.fFrameAverage = GetFrameAverageLimit(Info, m_HdrOut.fPeak);
  if (nWanted && !IsHdrOn())
    Reason = "r_HDRRendering is 0";
  else if (nWanted && !Info.szName[0])
    Reason = "the window's display was not found";
  else if (nWanted && !Info.bHdr)
    Reason = "the display is not in HDR mode (Windows: Use HDR)";
  else if (nWanted)
    Output = nWanted == 1 ? EDisplayOutput::Hdr10 : EDisplayOutput::ScRgb;
  // The metadata's frame average: the limit's, paper white at least (the SDR picture is not limited); without it paper
  // white, what most of a frame is at most.
  const float fMaxFall = m_HdrOut.fFrameAverage > 0 ? std::max(m_HdrOut.fFrameAverage, m_HdrOut.fPaperWhite) : m_HdrOut.fPaperWhite;
  if (!m_pDevice->SetOutput(Output, Output != EDisplayOutput::Sdr || m_HdrOut.bCapture, m_HdrOut.fPeak, fMaxFall) &&
      Output != EDisplayOutput::Sdr)
    Reason = "the swap chain can't show its colour space";

  const EDisplayOutput Actual = m_pDevice->GetOutput();
  PublishHdrState(Actual, Reason);
  if ((int)Actual == m_HdrOut.nLogged && Reason == m_HdrOut.Reason)
    return;
  if (Actual != EDisplayOutput::Sdr)
    iLog->Log("XRenderD3D12: HDR output %s, paper white %g nits, peak %g nits, frame average at most %g nits (display %s: peak %g nits, full "
              "frame %g nits, SDR white %g nits)", OutputName(Actual), m_HdrOut.fPaperWhite, m_HdrOut.fPeak, m_HdrOut.fFrameAverage, Info.szName,
              Info.fMaxNits, Info.fMaxFullFrameNits, Info.fSdrWhiteNits);
  else if (!Reason.empty())
    iLog->LogWarning("XRenderD3D12: no HDR output: %s", Reason.c_str());
  else if (m_HdrOut.nLogged > 0)
    iLog->Log("XRenderD3D12: HDR output off");
  m_HdrOut.nLogged = (int)Actual;
  m_HdrOut.Reason = Reason;
}

bool CD3D12Renderer::UpdateLayerSRVs()
{
  ID3D12Resource *pFrame = m_pDevice->GetFrameTarget(), *pHighlight = m_pDevice->GetHighlight();
  if (pFrame != m_HdrOut.pFrame.Get() || pHighlight != m_HdrOut.pHighlight.Get())
  {
    // Kept until their SRVs are freed: a new target is never at their address.
    for (int *pSRV : { &m_HdrOut.nFrameSRV, &m_HdrOut.nHighlightSRV })
    {
      if (*pSRV >= 0)
        m_pDescriptors->FreeSRV(*pSRV);
      *pSRV = -1;
    }
    m_HdrOut.pFrame = pFrame;
    m_HdrOut.pHighlight = pHighlight;
    if (pFrame && pHighlight)
    {
      m_HdrOut.nFrameSRV = m_pDescriptors->AllocSRV(pFrame, nullptr);
      m_HdrOut.nHighlightSRV = m_pDescriptors->AllocSRV(pHighlight, nullptr);
    }
  }
  return m_HdrOut.nFrameSRV >= 0 && m_HdrOut.nHighlightSRV >= 0;
}

void CD3D12Renderer::SetHighlightConstants(SDrawConstants &dc) const
{
  if (!m_pDevice->HasLayer())
    return;
  // The peak in paper whites, and its display value (HdrEncode, HdrLight.hlsli); the light from which
  // the neutral tone map's continuation takes over (HdrOutputToneMap, HdrToneMap.hlsli).
  const float fPeak = m_HdrOut.fPeak / m_HdrOut.fPaperWhite;
  Set4(dc.vParams3, fPeak > 1 ? 1 + (fPeak - 1) / 2.2f : 1.0f, fPeak, 0, std::max(CV_d3d12_HDRHighlights, 0.0f));
}

bool CD3D12Renderer::EnsureFrameAverage()
{
  SHdrOutputState &o = m_HdrOut;
  if (o.pFallWords)
    return true;
  if (o.bFallFailed || !m_pCompute || !m_pUpload || !m_pDescriptors)
    return false;
  ID3D12Device *pDevice = m_pDevice->GetDevice();
  const D3D12_HEAP_PROPERTIES Default = { D3D12_HEAP_TYPE_DEFAULT };
  D3D12_RESOURCE_DESC Desc = {};
  Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  Desc.Width = FALL_WORDS_BYTES;
  Desc.Height = 1;
  Desc.DepthOrArraySize = 1;
  Desc.MipLevels = 1;
  Desc.SampleDesc.Count = 1;
  Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  bool bOk = m_pCompute->HasKernel(eD3D12K_FrameAverageSum) && m_pCompute->HasKernel(eD3D12K_FrameAverageLimit) &&
             SUCCEEDED(pDevice->CreateCommittedResource(&Default, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                        IID_PPV_ARGS(&o.pFallWords)));
  o.pFall = bOk ? m_pUpload->CreateRenderTarget(1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, true) : nullptr;
  bOk = bOk && o.pFall && o.pFall->m_nSRV >= 0;
  // The tables: t0 the frame target and t1 the highlight layer each frame, u0 the words, u1 the target; the views the
  // kernels do not use null.
  D3D12_SHADER_RESOURCE_VIEW_DESC NullSRV = {};
  NullSRV.Format = D3D12_HDR_FORMAT;
  NullSRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  NullSRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  NullSRV.Texture2D.MipLevels = 1;
  D3D12_UNORDERED_ACCESS_VIEW_DESC NullUAV = {};
  NullUAV.Format = D3D12_HDR_FORMAT;
  NullUAV.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  D3D12_UNORDERED_ACCESS_VIEW_DESC WordsUAV = {};
  WordsUAV.Format = DXGI_FORMAT_R32_TYPELESS;
  WordsUAV.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  WordsUAV.Buffer.NumElements = XR_FALL_WORDS;
  WordsUAV.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
  D3D12_UNORDERED_ACCESS_VIEW_DESC TargetUAV = {};
  TargetUAV.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
  TargetUAV.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  o.FallTables.assign(D3D12_FRAMES, -1);
  for (int &nTable : o.FallTables)
  {
    if (!bOk || (nTable = m_pDescriptors->AllocRange(KERNEL_TABLE)) < 0)
    {
      bOk = false;
      continue;
    }
    for (int i = 0; i < CD3D12Compute::COMPUTE_SRVS; i++)
      m_pDescriptors->WriteSRV(nTable + i, nullptr, &NullSRV);
    m_pDescriptors->WriteUAV(nTable + CD3D12Compute::COMPUTE_SRVS, o.pFallWords.Get(), &WordsUAV);
    m_pDescriptors->WriteUAV(nTable + CD3D12Compute::COMPUTE_SRVS + 1, o.pFall->m_pResource, &TargetUAV);
    for (int i = 2; i < CD3D12Compute::COMPUTE_UAVS; i++)
      m_pDescriptors->WriteUAV(nTable + CD3D12Compute::COMPUTE_SRVS + i, nullptr, &NullUAV);
  }
  // The words zero (a copy of zeros from the frame memory: a buffer is created common, the copy promotes it), then the
  // kernels' for good.
  const SD3D12FrameAlloc Zeros = bOk ? m_pUpload->AllocFrame(FALL_WORDS_BYTES, 256) : SD3D12FrameAlloc{};
  if (!bOk || !Zeros.pCPU)
  {
    iLog->LogWarning("XRenderD3D12: no frame average limit (d3d12_HDRFrameAverage): no buffer, target or kernels");
    ReleaseFrameAverage();
    o.bFallFailed = true;
    return false;
  }
  o.pFallWords->SetName(L"XRenderD3D12 frame average limit");
  memset(Zeros.pCPU, 0, FALL_WORDS_BYTES);
  m_pDevice->CopyBufferRegion(o.pFallWords.Get(), 0, Zeros.pResource, Zeros.nOffset, FALL_WORDS_BYTES);
  m_pDevice->Transition(o.pFallWords.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  return true;
}

bool CD3D12Renderer::LimitFrameAverage(const char *szSection)
{
  SHdrOutputState &o = m_HdrOut;
  if (o.fFrameAverage <= 0 || !m_pDevice || !m_pDevice->HasLayer() || !m_pDevice->IsInFrame())
    return false;
  if (o.nFallFrame == m_nFrames)
    return o.pFall != nullptr;
  ID3D12Resource *pFrame = m_pDevice->GetFrameTarget(), *pHighlight = m_pDevice->GetHighlight();
  if (!pFrame || !pHighlight || !EnsureFrameAverage())
    return false;
  const int w = m_pDevice->GetWidth(), h = m_pDevice->GetHeight();
  const int nTable = o.FallTables[m_pDevice->GetFrameSlot()];
  m_pDescriptors->WriteSRV(nTable, pFrame, nullptr);
  m_pDescriptors->WriteSRV(nTable + 1, pHighlight, nullptr);

  // Without the frame just before (the first, the output off), the factor at once.
  SFrameAverageConstants fc = {};
  Set4(fc.vSize, (float)w, (float)h, 1.0f / w, 1.0f / h);
  Set4(fc.vParams, o.fPaperWhite, o.fPeak, o.fFrameAverage / o.fPaperWhite, o.nFallFrame != m_nFrames - 1 ? 1.0f : 0.0f);
  const float fFrameTime = iTimer ? std::clamp(iTimer->GetFrameTime(), 0.0f, 1.0f) : 0.02f;
  Set4(fc.vSteps, fFrameTime / FALL_DOWN_SECONDS, fFrameTime / FALL_UP_SECONDS, 0, 0);
  const UINT nStride = (UINT)std::max((long)ceilf(sqrtf((float)w * h / FALL_SAMPLES)), 1L);
  fc.vStride[0] = nStride;
  const SD3D12FrameAlloc CB = m_pUpload->AllocFrame(sizeof(fc), 256);
  if (!CB.pCPU)
    return false;
  memcpy(CB.pCPU, &fc, sizeof(fc));

  constexpr D3D12_RESOURCE_STATES READ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  ID3D12GraphicsCommandList *pList = m_pDevice->GetCmdList();
  GpuSection("hdr frame average");
  // The frame and the highlights read by the kernel, then by the output's pixels as they are (the device keeps their
  // states).
  m_pDevice->Require(pFrame, READ);
  m_pDevice->Require(pHighlight, READ);
  m_pDevice->Transition(o.pFall->m_pResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  // 16 x 16 samples a group, then one thread.
  const UINT nGroupPixels = 16 * nStride;
  m_pCompute->Dispatch(pList, eD3D12K_FrameAverageSum, CB.nGPU, nTable, nTable + CD3D12Compute::COMPUTE_SRVS,
                       (UINT)(w + nGroupPixels - 1) / nGroupPixels, (UINT)(h + nGroupPixels - 1) / nGroupPixels);
  m_pDevice->UavBarrier(o.pFallWords.Get());
  m_pCompute->Dispatch(pList, eD3D12K_FrameAverageLimit, CB.nGPU, nTable, nTable + CD3D12Compute::COMPUTE_SRVS, 1, 1);
  m_pDevice->UavBarrier(o.pFallWords.Get());
  m_pDevice->Transition(o.pFall->m_pResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  GpuSection(szSection);
  o.nFallFrame = m_nFrames;
  return true;
}

CD3D12Texture *CD3D12Renderer::FrameAverageTarget() const
{
  const SHdrOutputState &o = m_HdrOut;
  return o.fFrameAverage > 0 && o.nFallFrame == m_nFrames && o.pFall && o.pFall->m_nSRV >= 0 ? o.pFall : nullptr;
}

void CD3D12Renderer::ReleaseFrameAverage()
{
  SHdrOutputState &o = m_HdrOut;
  if (m_pDescriptors)
  {
    for (int nTable : o.FallTables)
    {
      if (nTable >= 0)
        m_pDescriptors->FreeRange(nTable, KERNEL_TABLE);
    }
  }
  o.FallTables.clear();
  if (m_pDevice && o.pFallWords)
    m_pDevice->DeferredRelease(o.pFallWords);
  o.pFallWords.Reset();
  if (m_pUpload)
    m_pUpload->ReleaseTexture(o.pFall);
  o.pFall = nullptr;
  o.nFallFrame = -1;
}

SDrawConstants CD3D12Renderer::HdrOutputConstants(EDisplayOutput Output) const
{
  SDrawConstants dc = DefaultDrawConstants();
  dc.nFlags = HDR_OUTPUT;
  dc.nTex0 = (uint32)m_HdrOut.nFrameSRV;
  dc.nTex1 = (uint32)m_HdrOut.nHighlightSRV;
  // The frame average limit's factor of the highlights (white: none).
  const CD3D12Texture *pFall = FrameAverageTarget();
  dc.nTex2 = pFall ? (uint32)pFall->m_nSRV : D3D12_SRV_WHITE;
  const int nDebug = std::clamp(CV_d3d12_HDROutputDebug, 0, 3);
  Set4(dc.vParams0, m_HdrOut.fPaperWhite, m_HdrOut.fPeak, Output == EDisplayOutput::Hdr10 ? 1.0f : 2.0f, (float)nDebug);
  Set4(dc.vParams1, 1.0f / m_pDevice->GetWidth(), 1.0f / m_pDevice->GetHeight(), 0, 0);
  // The HDR page's test: where it is (left, top, right, bottom in 0..1).
  if (nDebug == 3)
  {
    float r[4] = {};
    if (!m_HdrOut.pTestRect || sscanf(m_HdrOut.pTestRect->GetString(), "%f %f %f %f", &r[0], &r[1], &r[2], &r[3]) != 4)
      sscanf(HDR_TEST_RECT, "%f %f %f %f", &r[0], &r[1], &r[2], &r[3]);
    Set4(dc.vParams2, r[0], r[1], r[2], r[3]);
  }
  return dc;
}

void CD3D12Renderer::DrawHdrOutput()
{
  FUNCTION_PROFILER(iSystem, PROFILE_RENDERER);
  const EDisplayOutput Output = m_pDevice->GetOutput();
  if (Output == EDisplayOutput::Sdr || !m_pDevice->HasLayer() || !UpdateLayerSRVs())
    return;
  GpuSection("hdr output");
  m_pDevice->BeginOutput();
  // The frame average limit's factor of this frame, then the HDR statistics' nits of the output (HdrStats.cpp).
  LimitFrameAverage("hdr output");
  if (WantHdrStats())
    DispatchHdrOutputStats(false);
  DrawHdrFullScreen(HdrOutputConstants(Output), 1, Output == EDisplayOutput::Hdr10 ? PIPELINE_COLOR_HDR10 : PIPELINE_COLOR_HDR);
}

void CD3D12Renderer::DumpHdrOutput()
{
  if (!m_pFrameDump || !m_pDevice || (!CV_d3d12_HDROutput && !m_pDevice->HasLayer()))
    return;
  const SDisplayInfo &Info = m_pDevice->GetDisplayInfo();
  fprintf(m_pFrameDump.get(), "hdr output: %s (d3d12_HDROutput %d%s%s), paper white %g nits, peak %g nits; display %s: %s, peak %g nits, full frame %g nits, "
          "black %g nits, SDR white %g nits; highlight layer %s, highlights from %g; frame average at most %g nits (d3d12_HDRFrameAverage %g, "
          "d3d12_HDRFullFrame %g)\n", OutputName(m_pDevice->GetOutput()), CV_d3d12_HDROutput, m_HdrOut.Reason.empty() ? "" : ": ",
          m_HdrOut.Reason.c_str(), m_HdrOut.fPaperWhite, m_HdrOut.fPeak, Info.szName[0] ? Info.szName : "?", Info.bHdr ? "HDR" : "SDR",
          Info.fMaxNits, Info.fMaxFullFrameNits, Info.fMinNits, Info.fSdrWhiteNits, m_pDevice->HasLayer() ? "on" : "off", CV_d3d12_HDRHighlights,
          m_HdrOut.fFrameAverage, CV_d3d12_HDRFrameAverage, CV_d3d12_HDRFullFrame);
  if (!m_pDevice->HasLayer())
    return;
  // Where the layer has the scene (the fingerprint as the output pass checks it) and its highlights.
  const int w = m_pDevice->GetWidth(), h = m_pDevice->GetHeight();
  std::vector<uint8> Frame((size_t)w * h * 4);
  std::vector<uint16> Layer((size_t)w * h * 4);
  if (!m_pDevice->ReadBackBuffer(Frame.data(), w, h) ||
      !m_pDevice->ReadBackTexture(m_pDevice->GetHighlight(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, (uint8 *)Layer.data(), w, h))
    return;
  size_t nScene = 0, nHighlight = 0;
  float fMost = 1;
  for (size_t i = 0; i < (size_t)w * h; i++)
  {
    const uint8 *p = &Frame[i * 4];   // B, G, R, A
    const float fFingerprint = (0.3f * p[2] + 0.45f * p[1] + 0.25f * p[0]) / 255;
    if (fabsf(fFingerprint - HalfToFloat(Layer[i * 4 + 3])) > 0.6f / 255)
      continue;
    nScene++;
    const float r = std::max({ HalfToFloat(Layer[i * 4]), HalfToFloat(Layer[i * 4 + 1]), HalfToFloat(Layer[i * 4 + 2]) });
    if (r > 1.001f)
    {
      nHighlight++;
      fMost = std::max(fMost, r);
    }
  }
  const double fPixels = (double)w * h;
  fprintf(m_pFrameDump.get(), "hdr output: the layer has the scene in %.1f%% of the frame, highlights in %.2f%% (up to %.2f times the SDR light)\n",
          100.0 * nScene / fPixels, 100.0 * nHighlight / fPixels, fMost);
}

bool CD3D12Renderer::ReadFrameHdr10(void *pDst, int nPitch, int *pWidth, int *pHeight, float *pPaperWhite, float *pPeak)
{
  // Also before the device is there: the layer comes with the frame targets (UpdateHdrOutput).
  m_HdrOut.bCapture = true;
  if (!m_pDevice)
    return false;
  const int w = m_pDevice->GetWidth(), h = m_pDevice->GetHeight();
  if (pWidth)
    *pWidth = w;
  if (pHeight)
    *pHeight = h;
  if (pPaperWhite)
    *pPaperWhite = m_HdrOut.fPaperWhite;
  if (pPeak)
    *pPeak = m_HdrOut.fPeak;
  if (!pDst)
    return true;
  if (!m_pDevice->IsInFrame() || !m_pDevice->HasLayer() || nPitch < w * 4 || !m_pUpload || !UpdateLayerSRVs())
    return false;
  if (m_HdrOut.pCapture && (m_HdrOut.pCapture->m_nWidth != w || m_HdrOut.pCapture->m_nHeight != h))
  {
    m_pUpload->ReleaseTexture(m_HdrOut.pCapture);
    m_HdrOut.pCapture = nullptr;
  }
  if (!m_HdrOut.pCapture)
    m_HdrOut.pCapture = m_pUpload->CreateRenderTarget(w, h, D3D12_HDR10_FORMAT);
  if (!m_HdrOut.pCapture)
    return false;
  // The frame and the highlights read (the capture bound instead of them; drawn on again after: the bound targets'
  // states, the device's).
  m_pDevice->Require(m_pDevice->GetFrameTarget(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  m_pDevice->Require(m_pDevice->GetHighlight(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  LimitFrameAverage("hdr capture");
  if (WantHdrStats())
    DispatchHdrOutputStats(true);
  DrawHdrPass(m_HdrOut.pCapture, HdrOutputConstants(EDisplayOutput::Hdr10));
  m_pDevice->SetBackBufferTargets();
  m_HdrOut.Pixels.resize((size_t)w * h * 4);
  if (!m_pDevice->ReadBackTexture(m_HdrOut.pCapture->m_pResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, m_HdrOut.Pixels.data(), w, h))
    return false;
  for (int y = 0; y < h; y++)
    memcpy((uint8 *)pDst + (size_t)y * nPitch, &m_HdrOut.Pixels[(size_t)y * w * 4], (size_t)w * 4);
  return true;
}

void CD3D12Renderer::ReleaseHdrOutput()
{
  ReleaseFrameAverage();
  if (m_pDescriptors)
  {
    for (int *pSRV : { &m_HdrOut.nFrameSRV, &m_HdrOut.nHighlightSRV })
    {
      if (*pSRV >= 0)
        m_pDescriptors->FreeSRV(*pSRV);
      *pSRV = -1;
    }
  }
  m_HdrOut.pFrame.Reset();
  m_HdrOut.pHighlight.Reset();
  if (m_pUpload)
    m_pUpload->ReleaseTexture(m_HdrOut.pCapture);
  m_HdrOut.pCapture = nullptr;
  m_HdrOut.Pixels.clear();
}

//! For the demos' HDR10 videos (mod/Demos, DemoHost --video-hdr): CD3D12Renderer::ReadFrameHdr10.
//! Called in a frame, before ISystem::RenderEnd presents it.
extern "C" __declspec(dllexport) bool XRenderD3D12_ReadFrameHdr10(void *pDst, int nPitch, int *pWidth, int *pHeight, float *pPaperWhite, float *pPeak)
{
  return g_pRenderer && g_pRenderer->ReadFrameHdr10(pDst, nPitch, pWidth, pHeight, pPaperWhite, pPeak);
}
