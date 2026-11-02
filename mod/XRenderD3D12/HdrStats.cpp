// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: HdrStats.cpp
//	Description: The HDR statistics, while d3d12_DisplayHDR (the panel,
//	Stats.cpp) or d3d12_HDRLog (XRenderD3D12_hdr.txt) is on; without them
//	nothing of this runs.
//
//	The kernels (Shaders/HdrStats.hlsl) add up into a buffer of words,
//	cleared when a frame first needs it: the histogram of the scene's
//	light after the HDR pipeline's adaptation (ResolveHdr), the HDR
//	output's nits when the output pass runs (DrawHdrOutput) or a demo
//	captures HDR10 (ReadFrameHdr10). Before EndFrame the words are copied
//	to the frame slot's part of a readback buffer, which the CPU reads when
//	the slot comes again (the GPU is done with it then): the statistics
//	are D3D12_FRAMES frames old, the GPU never waited for.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "D3D12Device.h"
#include "D3D12Upload.h"
#include "D3D12Descriptors.h"
#include "D3D12Compute.h"
#include "HdrStatsMath.h"

namespace
{

constexpr int KERNEL_TABLE = CD3D12Compute::COMPUTE_SRVS + CD3D12Compute::COMPUTE_UAVS;
constexpr UINT WORDS_BYTES = XR_HDR_STAT_WORDS * sizeof(uint32);
//! A group of the kernels covers this many pixels a side (16 threads, a sample each).
constexpr int GROUP_PIXELS = 16 * XR_HDR_STRIDE;

void Set4(float v[4], float x, float y, float z, float w)
{
  v[0] = x; v[1] = y; v[2] = z; v[3] = w;
}

D3D12_RESOURCE_DESC BufferDesc(UINT64 nBytes, D3D12_RESOURCE_FLAGS Flags)
{
  D3D12_RESOURCE_DESC Desc = {};
  Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  Desc.Width = nBytes;
  Desc.Height = 1;
  Desc.DepthOrArraySize = 1;
  Desc.MipLevels = 1;
  Desc.SampleDesc.Count = 1;
  Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  Desc.Flags = Flags;
  return Desc;
}

float AsFloat(uint32 n)
{
  float f;
  memcpy(&f, &n, sizeof(f));
  return f;
}

}

bool CD3D12Renderer::WantHdrStats() const
{
  return (CV_d3d12_DisplayHDR > 0 || CV_d3d12_HDRLog > 0) && m_pCompute && m_pDevice && !m_HdrStats.bFailed;
}

bool CD3D12Renderer::BeginHdrStats()
{
  SHdrStatsState &s = m_HdrStats;
  if (!WantHdrStats() || !m_pDevice->IsInFrame())
    return false;
  if (s.nFrame == m_nFrames)
    return true;
  if (!s.pWords)
  {
    ID3D12Device *pDevice = m_pDevice->GetDevice();
    const D3D12_HEAP_PROPERTIES Default = { D3D12_HEAP_TYPE_DEFAULT }, Readback = { D3D12_HEAP_TYPE_READBACK };
    const D3D12_RESOURCE_DESC Words = BufferDesc(WORDS_BYTES, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    const D3D12_RESOURCE_DESC Copies = BufferDesc((UINT64)WORDS_BYTES * D3D12_FRAMES, D3D12_RESOURCE_FLAG_NONE);
    s.Tables.assign(2 * D3D12_FRAMES, -1);
    bool bOk = SUCCEEDED(pDevice->CreateCommittedResource(&Default, D3D12_HEAP_FLAG_NONE, &Words, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                          IID_PPV_ARGS(&s.pWords))) &&
               SUCCEEDED(pDevice->CreateCommittedResource(&Readback, D3D12_HEAP_FLAG_NONE, &Copies, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                          IID_PPV_ARGS(&s.pReadback)));
    // The tables, their views the kernels do not use null; the words the UAV of each.
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
    WordsUAV.Buffer.NumElements = XR_HDR_STAT_WORDS;
    WordsUAV.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    for (int &nTable : s.Tables)
    {
      if (!bOk || (nTable = m_pDescriptors->AllocRange(KERNEL_TABLE)) < 0)
      {
        bOk = false;
        continue;
      }
      for (int i = 0; i < CD3D12Compute::COMPUTE_SRVS; i++)
        m_pDescriptors->WriteSRV(nTable + i, nullptr, &NullSRV);
      m_pDescriptors->WriteUAV(nTable + CD3D12Compute::COMPUTE_SRVS, s.pWords.Get(), &WordsUAV);
      for (int i = 1; i < CD3D12Compute::COMPUTE_UAVS; i++)
        m_pDescriptors->WriteUAV(nTable + CD3D12Compute::COMPUTE_SRVS + i, nullptr, &NullUAV);
    }
    if (!bOk || !m_pCompute->HasKernel(eD3D12K_HdrHistogram) || !m_pCompute->HasKernel(eD3D12K_HdrOutputStats))
    {
      iLog->LogWarning("XRenderD3D12: no HDR statistics (d3d12_DisplayHDR, d3d12_HDRLog): no buffers or kernels");
      ReleaseHdrStats();
      s.bFailed = true;
      return false;
    }
    s.pWords->SetName(L"XRenderD3D12 HDR statistics");
    s.pReadback->SetName(L"XRenderD3D12 HDR statistics (read back)");
    for (SHdrStatsFrame &f : s.Slots)
      f = {};
  }
  // The frame D3D12_FRAMES back is done: its words first, then this frame's cleared (a copy of
  // zeros from the frame memory) and writable by the kernels.
  ReadHdrStats();
  const SD3D12FrameAlloc Zeros = m_pUpload->AllocFrame(WORDS_BYTES, 256);
  if (!Zeros.pCPU)
    return false;
  memset(Zeros.pCPU, 0, WORDS_BYTES);
  m_pDevice->EndOffscreen();
  ID3D12GraphicsCommandList *pList = m_pDevice->GetCmdList();
  pList->CopyBufferRegion(s.pWords.Get(), 0, Zeros.pResource, Zeros.nOffset, WORDS_BYTES);
  m_pDevice->Transition(s.pWords.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  s.nFrame = m_nFrames;
  s.Cur = {};
  s.Cur.nFrame = m_nFrameID;
  return true;
}

void CD3D12Renderer::DispatchHdrHistogram(ID3D12Resource *pScene, int nWidth, int nHeight, float fCompensation, bool bAdapted, bool bMeter,
                                          bool bVolumetric)
{
  SHdrStatsState &s = m_HdrStats;
  if (!pScene || nWidth <= 0 || nHeight <= 0 || m_Hdr.Meter.empty() || !BeginHdrStats() || s.Cur.bScene)
    return;
  const int nTable = s.Tables[m_pDevice->GetFrameSlot()];
  ID3D12Resource *pExposure = m_Hdr.pExposure[m_Hdr.nExposure]->m_pResource, *pMeter = m_Hdr.Meter.back()->m_pResource;
  m_pDescriptors->WriteSRV(nTable, pScene, nullptr);
  m_pDescriptors->WriteSRV(nTable + 1, pExposure, nullptr);
  m_pDescriptors->WriteSRV(nTable + 2, pMeter, nullptr);

  SHdrStatsConstants hc = {};
  Set4(hc.vSize, (float)nWidth, (float)nHeight, 1.0f / nWidth, 1.0f / nHeight);
  // The light of the knee: the soft shoulder's display value as light; the clamp's is white.
  const int nToneMap = std::clamp(CV_d3d12_ToneMap, 0, 4);
  const float fKnee = std::clamp(CV_d3d12_ToneMapKnee, 0.0f, 0.99f);
  Set4(hc.vParams, fCompensation, bAdapted ? 1.0f : 0.0f, nToneMap == 0 ? 1.0f : powf(fKnee, 2.2f), bMeter ? 1.0f : 0.0f);
  const SD3D12FrameAlloc CB = m_pUpload->AllocFrame(sizeof(hc), 256);
  memcpy(CB.pCPU, &hc, sizeof(hc));

  // The EV and the meter were drawn into: readable by the kernel too.
  m_pDevice->EndOffscreen();
  constexpr D3D12_RESOURCE_STATES READ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  m_pDevice->Transition(pExposure, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, READ);
  m_pDevice->Transition(pMeter, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, READ);
  m_pCompute->Dispatch(m_pDevice->GetCmdList(), eD3D12K_HdrHistogram, CB.nGPU, nTable, nTable + CD3D12Compute::COMPUTE_SRVS,
                       (UINT)(nWidth + GROUP_PIXELS - 1) / GROUP_PIXELS, (UINT)(nHeight + GROUP_PIXELS - 1) / GROUP_PIXELS);
  m_pDevice->Transition(pMeter, READ, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  m_pDevice->Transition(pExposure, READ, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

  SHdrStatsFrame &f = s.Cur;
  f.bScene = true;
  f.nToneMap = nToneMap;
  f.fKnee = fKnee;
  f.bAuto = bMeter;
  f.fExposureMin = std::min(CV_d3d12_ExposureMin, CV_d3d12_ExposureMax);
  f.fExposureMax = std::max(CV_d3d12_ExposureMin, CV_d3d12_ExposureMax);
  f.fBloom = std::max(CV_d3d12_Bloom, 0.0f);
  f.bVolumetric = bVolumetric;
}

void CD3D12Renderer::DispatchHdrOutputStats(bool bCapture)
{
  SHdrStatsState &s = m_HdrStats;
  ID3D12Resource *pFrame = m_pDevice ? m_pDevice->GetFrameTarget() : nullptr, *pHighlight = m_pDevice ? m_pDevice->GetHighlight() : nullptr;
  if (!pFrame || !pHighlight || !BeginHdrStats() || s.Cur.bOutput)
    return;
  const int w = m_pDevice->GetWidth(), h = m_pDevice->GetHeight();
  const int nTable = s.Tables[D3D12_FRAMES + m_pDevice->GetFrameSlot()];
  m_pDescriptors->WriteSRV(nTable, pFrame, nullptr);
  m_pDescriptors->WriteSRV(nTable + 1, pHighlight, nullptr);

  SHdrStatsConstants hc = {};
  Set4(hc.vSize, (float)w, (float)h, 1.0f / w, 1.0f / h);
  Set4(hc.vParams, m_HdrOut.fPaperWhite, m_HdrOut.fPeak, 0, 0);
  const SD3D12FrameAlloc CB = m_pUpload->AllocFrame(sizeof(hc), 256);
  memcpy(CB.pCPU, &hc, sizeof(hc));

  constexpr D3D12_RESOURCE_STATES READ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  m_pDevice->Transition(pFrame, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, READ);
  m_pDevice->Transition(pHighlight, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, READ);
  // After the histogram's adds.
  m_pDevice->UavBarrier(s.pWords.Get());
  m_pCompute->Dispatch(m_pDevice->GetCmdList(), eD3D12K_HdrOutputStats, CB.nGPU, nTable, nTable + CD3D12Compute::COMPUTE_SRVS,
                       (UINT)(w + GROUP_PIXELS - 1) / GROUP_PIXELS, (UINT)(h + GROUP_PIXELS - 1) / GROUP_PIXELS);
  m_pDevice->Transition(pHighlight, READ, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  m_pDevice->Transition(pFrame, READ, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

  SHdrStatsFrame &f = s.Cur;
  f.bOutput = true;
  f.bCapture = bCapture;
  f.fPaperWhite = m_HdrOut.fPaperWhite;
  f.fPeak = m_HdrOut.fPeak;
}

void CD3D12Renderer::EndHdrStats()
{
  SHdrStatsState &s = m_HdrStats;
  if (s.nFrame != m_nFrames || !s.pWords)
    return;
  const int nSlot = m_pDevice->GetFrameSlot();
  m_pDevice->EndOffscreen();
  m_pDevice->Transition(s.pWords.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
  m_pDevice->GetCmdList()->CopyBufferRegion(s.pReadback.Get(), (UINT64)WORDS_BYTES * nSlot, s.pWords.Get(), 0, WORDS_BYTES);
  m_pDevice->Transition(s.pWords.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
  s.Cur.Output = m_pDevice->GetOutput();
  s.Slots[nSlot] = s.Cur;
  s.nFrame = -1;
}

void CD3D12Renderer::ReadHdrStats()
{
  SHdrStatsState &s = m_HdrStats;
  SHdrStatsFrame &f = s.Slots[m_pDevice->GetFrameSlot()];
  if (f.nFrame < 0 || !s.pReadback)
    return;
  const D3D12_RANGE Range = { (SIZE_T)WORDS_BYTES * m_pDevice->GetFrameSlot(), (SIZE_T)WORDS_BYTES * (m_pDevice->GetFrameSlot() + 1) };
  void *pData = nullptr;
  if (SUCCEEDED(s.pReadback->Map(0, &Range, &pData)) && pData)
  {
    memcpy(f.Words, (const uint8 *)pData + Range.Begin, WORDS_BYTES);
    const D3D12_RANGE None = { 0, 0 };
    s.pReadback->Unmap(0, &None);
    s.Last = f;
    LogHdrStats(f);
  }
  f.nFrame = -1;
}

void CD3D12Renderer::LogHdrStats(const SHdrStatsFrame &f)
{
  SHdrStatsState &s = m_HdrStats;
  if (CV_d3d12_HDRLog <= 0)
  {
    s.pLog.reset();
    return;
  }
  if (!s.pLog)
  {
    s.pLog.reset(fopen("XRenderD3D12_hdr.txt", "w"));
    if (!s.pLog)
      return;
    fprintf(s.pLog.get(), "# frame, EV (exposed with: the adapted and the compensation), the automatic exposure's target EV (none: nan), the meter's mean "
                          "log2 luminance, log2 of the scene's luminance (before the exposure) at the percentiles 1 5 25 50 75 95 99, %% of the "
                          "samples whose brightest channel exposed is in the tone map's shoulder and at white or above, the output's nits "
                          "(the brightest channel; none: nan): mean, most, %% above paper white, %% at the peak; output, paper white, peak; camera "
                          "x y z; the samples of the histogram's %d bins of log2 luminance from %d to %d, by spaces\n",
            XR_HDR_HIST_BINS, XR_HDR_HIST_MIN, XR_HDR_HIST_MAX);
  }
  if (!f.bScene)
    return;
  const uint32 *w = f.Words;
  const float fEV = AsFloat(w[XR_HDR_STAT_EV]), fMeter = AsFloat(w[XR_HDR_STAT_METER]);
  const float fTarget = f.bAuto ? std::clamp(HDR_EXPOSURE_KEY_LOG2 - fMeter, f.fExposureMin, f.fExposureMax) : NAN;
  const double fSamples = std::max<double>(w[XR_HDR_STAT_SAMPLES], 1);
  const double fOut = std::max<double>(w[XR_HDR_STAT_OUT_PIXELS], 1);
  const bool bOut = f.bOutput && w[XR_HDR_STAT_OUT_PIXELS];
  const Vec3 vPos = iSystem->GetViewCamera().GetPos();
  fprintf(s.pLog.get(), "%d,%.3f,%.3f,%.3f", f.nFrame, fEV, fTarget, f.bAuto ? fMeter : NAN);
  for (double p : { 0.01, 0.05, 0.25, 0.5, 0.75, 0.95, 0.99 })
    fprintf(s.pLog.get(), ",%.2f", HdrHistPercentile(&w[XR_HDR_STAT_LUMA], p));
  fprintf(s.pLog.get(), ",%.2f,%.2f,%.1f,%.1f,%.3f,%.3f,%s,%g,%g,%.1f,%.1f,%.1f,", 100 * w[XR_HDR_STAT_SHOULDER] / fSamples,
          100 * w[XR_HDR_STAT_WHITE] / fSamples, bOut ? w[XR_HDR_STAT_OUT_SUM] * (double)XR_HDR_NITS_UNIT / fOut : NAN,
          bOut ? AsFloat(w[XR_HDR_STAT_OUT_MAX]) : NAN, bOut ? 100 * w[XR_HDR_STAT_OUT_PAPER] / fOut : NAN,
          bOut ? 100 * w[XR_HDR_STAT_OUT_PEAK] / fOut : NAN,
          !bOut ? "none" : f.bCapture ? "capture" : f.Output == EDisplayOutput::Hdr10 ? "hdr10" : "scrgb", f.fPaperWhite, f.fPeak, vPos.x, vPos.y,
          vPos.z);
  for (int i = 0; i < XR_HDR_HIST_BINS; i++)
    fprintf(s.pLog.get(), i ? " %u" : "%u", (unsigned)w[XR_HDR_STAT_LUMA + i]);
  fputc('\n', s.pLog.get());
  // Whole lines, also when the game is ended from outside.
  fflush(s.pLog.get());
}

void CD3D12Renderer::ReleaseHdrStats()
{
  SHdrStatsState &s = m_HdrStats;
  if (m_pDescriptors)
  {
    for (int nTable : s.Tables)
    {
      if (nTable >= 0)
        m_pDescriptors->FreeRange(nTable, KERNEL_TABLE);
    }
  }
  s.Tables.clear();
  if (m_pDevice)
  {
    if (s.pWords)
      m_pDevice->DeferredRelease(s.pWords);
    if (s.pReadback)
      m_pDevice->DeferredRelease(s.pReadback);
  }
  s.pWords.Reset();
  s.pReadback.Reset();
  s.nFrame = -1;
  for (SHdrStatsFrame &f : s.Slots)
    f.nFrame = -1;
  s.bFailed = false;
}
