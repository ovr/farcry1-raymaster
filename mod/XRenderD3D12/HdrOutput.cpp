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
//	(DXGI_OUTPUT_DESC1), or d3d12_HDRPeak. The output follows the display:
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

void CD3D12Renderer::UpdateHdrOutput()
{
  if (!m_pDevice)
    return;
  const int nWanted = std::clamp(CV_d3d12_HDROutput, 0, 2);
  if (!nWanted && !m_HdrOut.bCapture && m_pDevice->GetOutput() == EDisplayOutput::Sdr && !m_pDevice->HasLayer())
  {
    m_HdrOut.Reason.clear();
    return;
  }
  EDisplayOutput Output = EDisplayOutput::Sdr;
  std::string Reason;
  const SDisplayInfo &Info = m_pDevice->GetDisplayInfo();
  GetHdrLevels(Info, m_HdrOut.fPaperWhite, m_HdrOut.fPeak);
  if (nWanted && !IsHdrOn())
    Reason = "r_HDRRendering is 0";
  else if (nWanted && !Info.szName[0])
    Reason = "the window's display was not found";
  else if (nWanted && !Info.bHdr)
    Reason = "the display is not in HDR mode (Windows: Use HDR)";
  else if (nWanted)
    Output = nWanted == 1 ? EDisplayOutput::Hdr10 : EDisplayOutput::ScRgb;
  // The metadata's frame average: paper white, what most of a frame is at most.
  if (!m_pDevice->SetOutput(Output, Output != EDisplayOutput::Sdr || m_HdrOut.bCapture, m_HdrOut.fPeak, m_HdrOut.fPaperWhite) &&
      Output != EDisplayOutput::Sdr)
    Reason = "the swap chain can't show its colour space";

  const EDisplayOutput Actual = m_pDevice->GetOutput();
  if ((int)Actual == m_HdrOut.nLogged && Reason == m_HdrOut.Reason)
    return;
  if (Actual != EDisplayOutput::Sdr)
    iLog->Log("XRenderD3D12: HDR output %s, paper white %g nits, peak %g nits (display %s: peak %g nits, SDR white %g nits)", OutputName(Actual),
              m_HdrOut.fPaperWhite, m_HdrOut.fPeak, Info.szName, Info.fMaxNits, Info.fSdrWhiteNits);
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
  // The peak in paper whites, and its display value (HdrEncode, Common.hlsli).
  const float fPeak = m_HdrOut.fPeak / m_HdrOut.fPaperWhite;
  Set4(dc.vParams3, fPeak > 1 ? 1 + (fPeak - 1) / 2.2f : 1.0f, fPeak, 0, 0);
}

SDrawConstants CD3D12Renderer::HdrOutputConstants(EDisplayOutput Output) const
{
  SDrawConstants dc = DefaultDrawConstants();
  dc.nFlags = HDR_OUTPUT;
  dc.nTex0 = (uint32)m_HdrOut.nFrameSRV;
  dc.nTex1 = (uint32)m_HdrOut.nHighlightSRV;
  Set4(dc.vParams0, m_HdrOut.fPaperWhite, m_HdrOut.fPeak, Output == EDisplayOutput::Hdr10 ? 1.0f : 2.0f, (float)std::clamp(CV_d3d12_HDROutputDebug, 0, 2));
  Set4(dc.vParams1, 1.0f / m_pDevice->GetWidth(), 1.0f / m_pDevice->GetHeight(), 0, 0);
  return dc;
}

void CD3D12Renderer::DrawHdrOutput()
{
  const EDisplayOutput Output = m_pDevice->GetOutput();
  if (Output == EDisplayOutput::Sdr || !m_pDevice->HasLayer() || !UpdateLayerSRVs())
    return;
  GpuSection("hdr output");
  m_pDevice->BeginOutput();
  DrawHdrFullScreen(HdrOutputConstants(Output), 1, Output == EDisplayOutput::Hdr10 ? PIPELINE_COLOR_HDR10 : PIPELINE_COLOR_HDR);
}

void CD3D12Renderer::DumpHdrOutput()
{
  if (!m_pFrameDump || !m_pDevice || (!CV_d3d12_HDROutput && !m_pDevice->HasLayer()))
    return;
  const SDisplayInfo &Info = m_pDevice->GetDisplayInfo();
  fprintf(m_pFrameDump.get(), "hdr output: %s (d3d12_HDROutput %d%s%s), paper white %g nits, peak %g nits; display %s: %s, peak %g nits, full frame %g nits, "
          "black %g nits, SDR white %g nits; highlight layer %s\n", OutputName(m_pDevice->GetOutput()), CV_d3d12_HDROutput,
          m_HdrOut.Reason.empty() ? "" : ": ", m_HdrOut.Reason.c_str(), m_HdrOut.fPaperWhite, m_HdrOut.fPeak, Info.szName[0] ? Info.szName : "?",
          Info.bHdr ? "HDR" : "SDR", Info.fMaxNits, Info.fMaxFullFrameNits, Info.fMinNits, Info.fSdrWhiteNits, m_pDevice->HasLayer() ? "on" : "off");
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
  m_pDevice->SetFrameReadable(true);
  DrawHdrPass(m_HdrOut.pCapture, HdrOutputConstants(EDisplayOutput::Hdr10));
  m_pDevice->SetFrameReadable(false);
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
