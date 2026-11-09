// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: RaySun.cpp
//	Description: The sun's shadow by rays (d3d12_RTSunShadows, on a device
//	with DXR 1.1).
//
//	In the main view's opaque list after the depth prepass, before its
//	surfaces are shaded (DrawItems): the mask of the sun's visibility at
//	the surfaces of the depth buffer (Shaders/RaySun.hlsl): a ray for each
//	2 x 2 pixels, each of them in turn (d3d12_RTSunShadows 1), or for each
//	pixel (2), towards a point of the sun's disc (d3d12_RTSunAngle wide)
//	among this frame's scene for rays (RayTracing.h; with what casts from
//	out of the view: d3d12_RTOffscreen), the frames before blended in where
//	the surface was, spread over the soft edges of the shadow
//	(d3d12_RTSunDenoise 1). With d3d12_RTSunDenoise 2 a ray for each pixel,
//	denoised by AMD FidelityFX Denoiser's shadows denoiser instead
//	(Shaders/RaySunFfx.hlsl, third_party/fidelityfx-denoiser; where it is
//	built in, else 1): its tile classification with its history, its three
//	filter passes, for all the casters and for the live ones.
//	The general program takes it in place of the sun's cascades where its
//	pixel is the mask's surface (XR_RAY_SUN, General.hlsl): the receivers of
//	the cascades (DF2_SUN_CASCADES) that the depth prepass draws, all the
//	casters for the lit ones, the live casters for the light maps and the
//	terrain (XR_RAY_LIVE, as the live set of the cascades). The others (the
//	alpha tested surfaces, the leaves, what is blended, the weapon in the
//	hands) keep the cascades. The mask reaches as far as the cascades and
//	gives way to them over the last MASK_FADE of that.
//	Off, or without the depth prepass, the cascades or a scene for rays,
//	nothing of it runs and the frame is the same as without it.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "D3D12Device.h"
#include "D3D12Upload.h"
#include "D3D12Descriptors.h"
#include "D3D12Compute.h"
#include "RayTracing.h"

namespace
{

void Set4(float v[4], float x, float y, float z, float w)
{
  v[0] = x; v[1] = y; v[2] = z; v[3] = w;
}

//! The part of the mask's range over which it gives way to the cascades.
constexpr float MASK_FADE = 0.1f;
//! CSTrace's rays: the distances to what hides the sun, of all the casters and of the live ones (-1: no
//! surface), and the view depth of the pixel traced.
constexpr DXGI_FORMAT RAYS_FORMAT = DXGI_FORMAT_R16G16B16A16_FLOAT;
//! The history: the visibility of both, the frames in it, the view depth.
constexpr DXGI_FORMAT HISTORY_FORMAT = DXGI_FORMAT_R16G16B16A16_FLOAT;
//! The mask: the view depth, the visibilities and the mask's weight packed (RaySun.hlsl).
constexpr DXGI_FORMAT MASK_FORMAT = DXGI_FORMAT_R32G32_FLOAT;
constexpr D3D12_RESOURCE_STATES SHADER_READ = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
constexpr D3D12_RESOURCE_STATES DEPTH_READ = D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
//! The kernels' UAV tables (D3D12Compute.h): the rays', the history's of each texture, the mask's.
enum { TABLE_TRACE, TABLE_TEMPORAL0, TABLE_TEMPORAL1, TABLE_FILTER, TABLE_COUNT };
//! With AMD FidelityFX Denoiser (SRaySunState::SFfx): CSInputs' for each depth, CSPrepare's, CSClassify's for each set
//! and moments, CSFilter's first pass's, its second's and third's for each set.
enum { FFX_INPUTS, FFX_PREPARE = FFX_INPUTS + 2, FFX_CLASSIFY, FFX_FILTER0 = FFX_CLASSIFY + 4, FFX_FILTER1, FFX_FILTER2 = FFX_FILTER1 + 2,
       FFX_TABLES = FFX_FILTER2 + 2 };
static_assert(FFX_TABLES == sizeof(CD3D12Renderer::SRaySunState::SFfx::nTables) / sizeof(int), "SFfx::nTables");
//! Its kernels.
constexpr ED3D12Kernel FFX_KERNELS[] = { eD3D12K_RaySunFfxInputs, eD3D12K_RaySunFfxPrepare, eD3D12K_RaySunFfxClassify, eD3D12K_RaySunFfxFilter,
                                         eD3D12K_RaySunFfxPack };

D3D12_SHADER_RESOURCE_VIEW_DESC TextureSRV(DXGI_FORMAT Format)
{
  D3D12_SHADER_RESOURCE_VIEW_DESC SRV = {};
  SRV.Format = Format;
  SRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  SRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  SRV.Texture2D.MipLevels = 1;
  return SRV;
}

}

bool CD3D12Renderer::WantsRaySun() const
{
  // (the path tracer takes its visibility at the surfaces of the depth buffer)
  return (CV_d3d12_RTSunShadows > 0 || WantsPathTracing()) && m_pRayScene && m_pRayScene->IsSupported() && CV_d3d12_DepthPrepass &&
         CV_d3d12_ShadowCascades > 0;
}

bool CD3D12Renderer::EnsureRaySunTargets(int nWidth, int nHeight)
{
  if (m_RaySun.pMask && nWidth == m_RaySun.nWidth && nHeight == m_RaySun.nHeight)
    return true;
  ReleaseRaySun();
  ID3D12GraphicsCommandList *pList = m_pDevice->GetCmdList();
  m_RaySun.pRays = m_pUpload->CreateComputeTarget(nWidth, nHeight, 1, RAYS_FORMAT, L"XRenderD3D12 sun's shadow rays");
  for (CD3D12Texture *&pTex : m_RaySun.pHistory)
    pTex = m_pUpload->CreateComputeTarget(nWidth, nHeight, 1, HISTORY_FORMAT, L"XRenderD3D12 sun's shadow history");
  m_RaySun.pMask = m_pUpload->CreateComputeTarget(nWidth, nHeight, 1, MASK_FORMAT, L"XRenderD3D12 sun's shadow mask");
  m_RaySun.nWidth = nWidth;
  m_RaySun.nHeight = nHeight;
  m_RaySun.nFrame = -1;
  if (!pList || !m_RaySun.pRays || !m_RaySun.pHistory[0] || !m_RaySun.pHistory[1] || !m_RaySun.pMask)
  {
    iLog->LogError("XRenderD3D12: no targets for the sun's shadow by rays at %d x %d", nWidth, nHeight);
    ReleaseRaySun();
    return false;
  }
  // Made in UNORDERED_ACCESS; their states kept by the device (each kernel asks for its own, nothing moved back after).
  for (CD3D12Texture *pTex : { m_RaySun.pRays, m_RaySun.pHistory[0], m_RaySun.pHistory[1], m_RaySun.pMask })
    m_pDevice->TrackState(pTex->m_pResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  // The views in the texture table, the kernels' UAV tables (the slots they do not use null).
  D3D12_SHADER_RESOURCE_VIEW_DESC SRV = TextureSRV(RAYS_FORMAT);
  m_RaySun.pRays->m_nSRV = m_pDescriptors->AllocSRV(m_RaySun.pRays->m_pResource, &SRV);
  SRV = TextureSRV(HISTORY_FORMAT);
  for (CD3D12Texture *pTex : m_RaySun.pHistory)
    pTex->m_nSRV = m_pDescriptors->AllocSRV(pTex->m_pResource, &SRV);
  SRV = TextureSRV(MASK_FORMAT);
  m_RaySun.pMask->m_nSRV = m_pDescriptors->AllocSRV(m_RaySun.pMask->m_pResource, &SRV);
  CD3D12Texture *const Outputs[TABLE_COUNT] = { m_RaySun.pRays, m_RaySun.pHistory[0], m_RaySun.pHistory[1], m_RaySun.pMask };
  for (int t = 0; t < TABLE_COUNT; t++)
  {
    int &nTable = m_RaySun.nTables[t];
    if ((nTable = m_pDescriptors->AllocRange(CD3D12Compute::COMPUTE_UAVS)) < 0)
      break;
    D3D12_UNORDERED_ACCESS_VIEW_DESC UAV = {};
    UAV.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    for (int i = 0; i < CD3D12Compute::COMPUTE_UAVS; i++)
    {
      UAV.Format = i == 0 ? Outputs[t]->m_Format : RAYS_FORMAT;
      m_pDescriptors->WriteUAV(nTable + i, i == 0 ? Outputs[t]->m_pResource : nullptr, &UAV);
    }
  }
  if (m_RaySun.pRays->m_nSRV < 0 || m_RaySun.pHistory[0]->m_nSRV < 0 || m_RaySun.pHistory[1]->m_nSRV < 0 || m_RaySun.pMask->m_nSRV < 0 ||
      std::any_of(std::begin(m_RaySun.nTables), std::end(m_RaySun.nTables), [](int n) { return n < 0; }))
  {
    iLog->LogError("XRenderD3D12: no views for the sun's shadow by rays");
    ReleaseRaySun();
    return false;
  }
  iLog->Log("XRenderD3D12: the sun's shadow by rays: %d x %d", nWidth, nHeight);
  return true;
}

bool CD3D12Renderer::EnsureRaySunFfx()
{
  SRaySunState::SFfx &f = m_RaySun.Ffx;
  if (f.pNormals)
    return true;
  if (f.bFailed || !m_pCompute || !m_pUpload || !m_pDescriptors || !m_pDevice || !m_pDevice->GetCmdList())
    return false;
  f.bFailed = true;
  for (ED3D12Kernel eKernel : FFX_KERNELS)
    if (!m_pCompute->HasKernel(eKernel))
    {
      iLog->LogWarning("XRenderD3D12: AMD FidelityFX Denoiser is not built in or the device refuses its kernels: the sun's shadow by rays "
                       "is denoised by the renderer's own (d3d12_RTSunDenoise 1)");
      return false;
    }
  const int w = m_RaySun.nWidth, h = m_RaySun.nHeight, nTilesX = (w + 7) / 8;
  const auto Make = [&](int nW, int nH, DXGI_FORMAT Format, const wchar_t *szName) -> CD3D12Texture *
  {
    CD3D12Texture *pTex = m_pUpload->CreateComputeTarget(nW, nH, 1, Format, szName);
    if (pTex)
    {
      const D3D12_SHADER_RESOURCE_VIEW_DESC SRV = TextureSRV(Format);
      pTex->m_nSRV = m_pDescriptors->AllocSRV(pTex->m_pResource, &SRV);
    }
    return pTex;
  };
  f.pNormals = Make(w, h, DXGI_FORMAT_R10G10B10A2_UNORM, L"XRenderD3D12 sun's shadow FidelityFX normals");
  for (CD3D12Texture *&pTex : f.pDepth)
    pTex = Make(w, h, DXGI_FORMAT_R32_FLOAT, L"XRenderD3D12 sun's shadow FidelityFX depth");
  f.pTiles = Make(nTilesX, (h + 3) / 4, DXGI_FORMAT_R32_UINT, L"XRenderD3D12 sun's shadow FidelityFX masks");
  f.pMeta = Make(nTilesX, (h + 7) / 8, DXGI_FORMAT_R32_UINT, L"XRenderD3D12 sun's shadow FidelityFX tiles");
  f.pReprojected = Make(w, h, DXGI_FORMAT_R16G16_FLOAT, L"XRenderD3D12 sun's shadow FidelityFX reprojected");
  f.pFiltered = Make(w, h, DXGI_FORMAT_R16G16_FLOAT, L"XRenderD3D12 sun's shadow FidelityFX filtered");
  for (int s = 0; s < 2; s++)
  {
    for (CD3D12Texture *&pTex : f.pMoments[s])
      pTex = Make(w, h, DXGI_FORMAT_R11G11B10_FLOAT, L"XRenderD3D12 sun's shadow FidelityFX moments");
    f.pHistory[s] = Make(w, h, DXGI_FORMAT_R16G16_FLOAT, L"XRenderD3D12 sun's shadow FidelityFX history");
    f.pDenoised[s] = Make(w, h, DXGI_FORMAT_R16G16_FLOAT, L"XRenderD3D12 sun's shadow FidelityFX denoised");
  }
  CD3D12Texture *const All[] = { f.pNormals, f.pDepth[0], f.pDepth[1], f.pTiles, f.pMeta, f.pReprojected, f.pFiltered, f.pMoments[0][0],
                                 f.pMoments[0][1], f.pMoments[1][0], f.pMoments[1][1], f.pHistory[0], f.pHistory[1], f.pDenoised[0], f.pDenoised[1] };
  bool bOk = std::all_of(std::begin(All), std::end(All), [](CD3D12Texture *pTex) { return pTex && pTex->m_nSRV >= 0; });
  // The kernels' UAV tables (as Shaders/RaySunFfx.hlsl's registers; the slots they do not use null).
  CD3D12Texture *const Tables[FFX_TABLES][4] = {
    { f.pNormals, f.pDepth[0] }, { f.pNormals, f.pDepth[1] }, { f.pTiles },
    { f.pReprojected, f.pMoments[0][0], f.pMeta, f.pTiles }, { f.pReprojected, f.pMoments[0][1], f.pMeta, f.pTiles },
    { f.pReprojected, f.pMoments[1][0], f.pMeta, f.pTiles }, { f.pReprojected, f.pMoments[1][1], f.pMeta, f.pTiles },
    { f.pFiltered, f.pMeta }, { f.pHistory[0], f.pMeta }, { f.pHistory[1], f.pMeta }, { f.pDenoised[0], f.pMeta }, { f.pDenoised[1], f.pMeta } };
  for (int t = 0; t < FFX_TABLES && bOk; t++)
  {
    if ((f.nTables[t] = m_pDescriptors->AllocRange(CD3D12Compute::COMPUTE_UAVS)) < 0)
    {
      bOk = false;
      break;
    }
    D3D12_UNORDERED_ACCESS_VIEW_DESC UAV = {};
    UAV.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    for (int i = 0; i < CD3D12Compute::COMPUTE_UAVS; i++)
    {
      CD3D12Texture *pTex = i < 4 ? Tables[t][i] : nullptr;
      UAV.Format = pTex ? pTex->m_Format : RAYS_FORMAT;
      m_pDescriptors->WriteUAV(f.nTables[t] + i, pTex ? pTex->m_pResource : nullptr, &UAV);
    }
  }
  if (!bOk)
  {
    iLog->LogError("XRenderD3D12: no targets for AMD FidelityFX Denoiser at %d x %d: the sun's shadow by rays is denoised by the renderer's "
                   "own (d3d12_RTSunDenoise 1)", w, h);
    ReleaseRaySunFfx();
    return false;
  }
  // Made in UNORDERED_ACCESS; the states of those a kernel reads kept by the device (DenoiseRaySunFfx's kernels ask for
  // theirs; the masks and tiles are written and read as UAVs only).
  for (CD3D12Texture *pTex : All)
    if (pTex != f.pTiles && pTex != f.pMeta)
      m_pDevice->TrackState(pTex->m_pResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  f.bFailed = false;
  f.nFrame = -1;
  iLog->Log("XRenderD3D12: the sun's shadow by rays denoised by AMD FidelityFX Denoiser 1.2 (shadows) at %d x %d", w, h);
  return true;
}

void CD3D12Renderer::ReleaseRaySunFfx()
{
  SRaySunState::SFfx &f = m_RaySun.Ffx;
  CD3D12Texture *const All[] = { f.pNormals, f.pDepth[0], f.pDepth[1], f.pTiles, f.pMeta, f.pReprojected, f.pFiltered, f.pMoments[0][0],
                                 f.pMoments[0][1], f.pMoments[1][0], f.pMoments[1][1], f.pHistory[0], f.pHistory[1], f.pDenoised[0], f.pDenoised[1] };
  for (CD3D12Texture *pTex : All)
  {
    if (!pTex)
      continue;
    if (m_pDescriptors && pTex->m_nSRV >= 0)
      m_pDescriptors->FreeSRV(pTex->m_nSRV);
    pTex->m_nSRV = -1;
    if (m_pUpload)
      m_pUpload->ReleaseTexture(pTex);
  }
  if (m_pDescriptors)
    for (int nTable : f.nTables)
      if (nTable >= 0)
        m_pDescriptors->FreeRange(nTable, CD3D12Compute::COMPUTE_UAVS);
  const bool bFailed = f.bFailed;
  f = SRaySunState::SFfx();
  f.bFailed = bFailed;
}

void CD3D12Renderer::ReleaseRaySun()
{
  if (m_RaySun.bActive)
    InvalidateFrameConstants();
  ReleaseRaySunFfx();
  if (m_pDescriptors)
  {
    for (CD3D12Texture *pTex : { m_RaySun.pRays, m_RaySun.pHistory[0], m_RaySun.pHistory[1], m_RaySun.pMask })
      if (pTex && pTex->m_nSRV >= 0)
      {
        m_pDescriptors->FreeSRV(pTex->m_nSRV);
        pTex->m_nSRV = -1;
      }
    for (int nTable : m_RaySun.nTables)
      if (nTable >= 0)
        m_pDescriptors->FreeRange(nTable, CD3D12Compute::COMPUTE_UAVS);
    if (m_RaySun.nDepthSRV >= 0)
      m_pDescriptors->FreeSRV(m_RaySun.nDepthSRV);
  }
  if (m_pUpload)
  {
    for (CD3D12Texture *pTex : { m_RaySun.pRays, m_RaySun.pHistory[0], m_RaySun.pHistory[1], m_RaySun.pMask })
      if (pTex)
        m_pUpload->ReleaseTexture(pTex);
  }
  m_RaySun = SRaySunState();
}

void CD3D12Renderer::DrawRaySunMask()
{
  m_RaySun.bActive = false;
  if (!WantsRaySun())
  {
    if (m_RaySun.pMask)
      ReleaseRaySun();
    return;
  }
  // The scene for rays of this frame and the cascades of the main view, whose receivers take the mask.
  if (!m_pCompute || !m_pUpload || !m_pDescriptors || !m_pDevice || !m_pRayScene || m_pRayScene->GetBuiltFrame() != m_nFrames ||
      !m_pRayScene->GetTLAS() || !m_nCascades || m_nCascadeFrame != m_nFrames || !CascadeReceiverFlags(false) || m_pDevice->IsOffscreen())
    return;
  // With MSAA its depth buffer's nearest samples (Msaa.cpp).
  ResolveMsaaDepth();
  ID3D12Resource *pDepth = m_pDevice->GetMainDepth();
  const int w = m_pDevice->GetMainWidth(), h = m_pDevice->GetMainHeight();
  // The view over the whole depth buffer (the pixels' positions from it).
  const bool bScene = m_pDevice->IsSceneBound();
  float sx = 1, sy = 1;
  if (bScene)
    GetSceneScale(sx, sy);
  if (!pDepth || w <= 0 || h <= 0 || m_Viewport[0] != 0 || m_Viewport[1] != 0 || fabsf(m_Viewport[2] * sx - w) > 1 || fabsf(m_Viewport[3] * sy - h) > 1 ||
      !EnsureRaySunTargets(w, h))
    return;
  if (pDepth != m_RaySun.pDepth.Get())
  {
    if (m_RaySun.nDepthSRV >= 0)
      m_pDescriptors->FreeSRV(m_RaySun.nDepthSRV);
    m_RaySun.pDepth = pDepth;
    const D3D12_SHADER_RESOURCE_VIEW_DESC SRV = TextureSRV(m_pDevice->GetMainDepthSRVFormat());
    m_RaySun.nDepthSRV = m_pDescriptors->AllocSRV(pDepth, &SRV);
    m_RaySun.nFrame = -1;
  }
  if (m_RaySun.nDepthSRV < 0)
    return;

  GpuSection("rt sun shadows");
  // The builds of the scene done (the frame's list goes on after them).
  const D3D12_GPU_VIRTUAL_ADDRESS nTlas = UseRayScene();
  if (!nTlas)
    return;
  SFrameConstants fc;
  MakeFrameConstants(fc, bScene);
  const SD3D12FrameAlloc FrameCB = m_pUpload->AllocFrame(sizeof(fc), 256);
  memcpy(FrameCB.pCPU, &fc, sizeof(fc));

  SRaySunConstants rc = {};
  memcpy(rc.mPrevViewProj, m_RaySun.PrevViewProj, sizeof(rc.mPrevViewProj));
  GetRayView(rc.vCameraPos, rc.vRayX, rc.vRayY, rc.vRayO);
  const float *P = m_ProjMatrix.data;
  Set4(rc.vSize, (float)w, (float)h, 1.0f / w, 1.0f / h);
  Set4(rc.vJitter, fc.vJitter[0], fc.vJitter[1], m_RaySun.PrevJitter[0], m_RaySun.PrevJitter[1]);
  // The sun's disc: its direction and two across it.
  Vec3 vSun(fc.vSunDir[0], fc.vSunDir[1], fc.vSunDir[2]);
  Vec3 vSunX = Vec3(0, 0, 1).Cross(vSun);
  if (vSunX.GetLength() < 1e-4f)
    vSunX = Vec3(0, 1, 0).Cross(vSun);
  vSunX.Normalize();
  const Vec3 vSunY = vSun.Cross(vSunX);
  const float fHalfAngle = DEG2RAD(std::clamp(CV_d3d12_RTSunAngle, 0.0f, 10.0f)) * 0.5f;
  Set4(rc.vSun, vSun.x, vSun.y, vSun.z, tanf(fHalfAngle));
  Set4(rc.vSunX, vSunX.x, vSunX.y, vSunX.z, 0);
  Set4(rc.vSunY, vSunY.x, vSunY.y, vSunY.z, 0);
  const float fRange = m_Cascades[m_nCascades - 1].fEnd;
  Set4(rc.vParams, fRange, fRange * (1 - MASK_FADE), std::max(CV_d3d12_RTDistance, 1.0f), 2 / (fabsf(P[5]) * h));
  // AMD FidelityFX Denoiser's (2, where it can be), else the renderer's own denoise (1).
  const bool bFfx = CV_d3d12_RTSunDenoise >= 2 && EnsureRaySunFfx();
  if (!bFfx && m_RaySun.Ffx.pNormals)
    ReleaseRaySunFfx();
  const bool bDenoise = CV_d3d12_RTSunDenoise != 0 && !bFfx;
  // None after a frame without the rays or a cut to another shot (d3d12_CutReset).
  const bool bFfxHistory = bFfx && m_RaySun.Ffx.nFrame == m_nFrames - 1 && !IsCameraCut();
  const bool bHistory = bDenoise && m_RaySun.nFrame == m_nFrames - 1 && !IsCameraCut();
  const int nCur = m_RaySun.nHistory ^= 1;
  CD3D12Texture *pHistory = m_RaySun.pHistory[nCur], *pPrevHistory = m_RaySun.pHistory[nCur ^ 1];
  rc.nTextures[0] = (uint32)m_RaySun.nDepthSRV;
  rc.nTextures[1] = (uint32)m_RaySun.pRays->m_nSRV;
  rc.nTextures[2] = (uint32)pPrevHistory->m_nSRV;
  rc.nTextures[3] = (uint32)pHistory->m_nSRV;
  // The frames the history holds at most; with the path tracer (d3d12_PTSunHistory) more, and where the shadow moves a
  // part of them kept (0x10000), not none: its sun has no other light to hide the rays' noise in.
  const bool bLong = WantsPathTracing() && CV_d3d12_PTSunHistory > 0;
  const uint32 nFrames = bLong ? (uint32)std::clamp(CV_d3d12_PTSunHistory, 1, 1024) : 24;
  rc.nFlags[0] = bHistory ? nFrames | (bLong ? 0x10000 : 0) : 0;
  rc.nFlags[1] = (uint32)m_nFrames;
  rc.nFlags[2] = (bDenoise ? 1 : 0) | (CV_d3d12_RTSunCrease > 0 ? 2 : 0);
  rc.nFlags[3] = (RayMask() & 0xff) | (m_pRayScene->GetLiveInstances() ? 0x100 : 0);
  // A ray for each 2 x 2 pixels, from each of them in turn over 4 frames (the history has them all), or for each pixel
  // (FidelityFX Denoiser's too).
  const int nStep = CV_d3d12_RTSunShadows >= 2 || bFfx ? 1 : 2;
  static constexpr uint32 CellPixels[4][2] = { { 0, 0 }, { 1, 1 }, { 1, 0 }, { 0, 1 } };
  rc.nTrace[0] = (uint32)nStep;
  rc.nTrace[1] = nStep == 2 ? CellPixels[m_nFrames & 3][0] : 0;
  rc.nTrace[2] = nStep == 2 ? CellPixels[m_nFrames & 3][1] : 0;
  const SD3D12FrameAlloc PassCB = m_pUpload->AllocFrame(sizeof(rc), 256);
  memcpy(PassCB.pCPU, &rc, sizeof(rc));

  ID3D12GraphicsCommandList *pList = m_pDevice->GetCmdList();
  const D3D12_GPU_VIRTUAL_ADDRESS nInstances = m_pRayScene->GetInstances();
  const UINT nGroupsX = (UINT)(w + XR_RAY_SUN_GROUP - 1) / XR_RAY_SUN_GROUP, nGroupsY = (UINT)(h + XR_RAY_SUN_GROUP - 1) / XR_RAY_SUN_GROUP;
  const int nCellsX = (w + nStep - 1) / nStep, nCellsY = (h + nStep - 1) / nStep;
  constexpr D3D12_RESOURCE_STATES UAV = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  // The depth read (the device gives it back for the draws: the bound depth buffer's state), the targets' states kept by
  // the device too (EnsureRaySunTargets): each kernel asks for its own, nothing moved back after.
  m_pDevice->Require(pDepth, DEPTH_READ);
  m_pDevice->Require(m_RaySun.pRays->m_pResource, UAV);
  m_pCompute->DispatchRays(pList, eD3D12K_RaySunTrace, FrameCB.nGPU, PassCB.nGPU, nTlas, nInstances, m_RaySun.nTables[TABLE_TRACE],
                           (UINT)(nCellsX + XR_RAY_SUN_GROUP - 1) / XR_RAY_SUN_GROUP, (UINT)(nCellsY + XR_RAY_SUN_GROUP - 1) / XR_RAY_SUN_GROUP);
  m_pDevice->Require(m_RaySun.pRays->m_pResource, SHADER_READ);
  if (bFfx)
    DenoiseRaySunFfx(FrameCB.nGPU, rc, w, h);
  else
  {
    // Each pixel's ray (and with the denoise its history), then the mask.
    GpuSection("rt sun shadows history");
    m_pDevice->Require(pPrevHistory->m_pResource, SHADER_READ);
    m_pDevice->Require(pHistory->m_pResource, UAV);
    m_pCompute->DispatchRays(pList, eD3D12K_RaySunTemporal, FrameCB.nGPU, PassCB.nGPU, 0, 0, m_RaySun.nTables[TABLE_TEMPORAL0 + nCur], nGroupsX,
                             nGroupsY);
    m_pDevice->Require(pHistory->m_pResource, SHADER_READ);
    GpuSection("rt sun shadows filter");
    m_pDevice->Require(m_RaySun.pMask->m_pResource, UAV);
    m_pCompute->DispatchRays(pList, eD3D12K_RaySunFilter, FrameCB.nGPU, PassCB.nGPU, 0, 0, m_RaySun.nTables[TABLE_FILTER], nGroupsX, nGroupsY);
    // Read by the opaque list's pixels.
    m_pDevice->Require(m_RaySun.pMask->m_pResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  }

  // The camera of this frame for the next one's history.
  const Matrix44 ViewProj = m_CameraView * m_ProjMatrix;
  memcpy(m_RaySun.PrevViewProj, ViewProj.data, sizeof(m_RaySun.PrevViewProj));
  m_RaySun.PrevJitter[0] = fc.vJitter[0];
  m_RaySun.PrevJitter[1] = fc.vJitter[1];
  m_RaySun.nFrame = bDenoise ? m_nFrames : -1;
  m_RaySun.Ffx.nFrame = bFfx ? m_nFrames : -1;
  // The opaque list's receivers read it from here on (the frame constants' nRaySun).
  m_RaySun.bActive = true;
  InvalidateFrameConstants();
  if (m_pFrameDump)
    fprintf(m_pFrameDump.get(), "rt sun shadows: %d x %d, a ray for each %d x %d pixels (%d %d of them), sun's disc %g degrees, to %.1f m (giving way from %.1f m), "
            "rays %g m, %s live casters, denoise %s, history %d\n", w, h, nStep, nStep, (int)rc.nTrace[1], (int)rc.nTrace[2], 2 * RAD2DEG(fHalfAngle),
            rc.vParams[0], rc.vParams[1], rc.vParams[2], (rc.nFlags[3] & 0x100) ? "with" : "without",
            bFfx ? "AMD FidelityFX Denoiser 1.2 (shadows)" : bDenoise ? "the renderer's" : "none", bHistory || bFfxHistory ? 1 : 0);
}

void CD3D12Renderer::DenoiseRaySunFfx(D3D12_GPU_VIRTUAL_ADDRESS FrameCB, const SRaySunConstants &rc, int w, int h)
{
  SRaySunState::SFfx &f = m_RaySun.Ffx;
  ID3D12GraphicsCommandList *pList = m_pDevice->GetCmdList();
  const bool bFirst = f.nFrame != m_nFrames - 1 || IsCameraCut();
  const int c = f.nCur ^= 1, p = c ^ 1;
  // The sets of casters: all of them, and the live ones where the scene has them (else none hides the sun).
  const int nSets = (rc.nFlags[3] & 0x100) ? 2 : 1;
  SRaySunFfxConstants xc = {};
  memcpy(xc.mPrevViewProj, rc.mPrevViewProj, sizeof(xc.mPrevViewProj));
  memcpy(xc.vCameraPos, rc.vCameraPos, sizeof(xc.vCameraPos));
  memcpy(xc.vRayX, rc.vRayX, sizeof(xc.vRayX));
  memcpy(xc.vRayY, rc.vRayY, sizeof(xc.vRayY));
  memcpy(xc.vRayO, rc.vRayO, sizeof(xc.vRayO));
  memcpy(xc.vSize, rc.vSize, sizeof(xc.vSize));
  memcpy(xc.vJitter, rc.vJitter, sizeof(xc.vJitter));
  memcpy(xc.vParams, rc.vParams, sizeof(xc.vParams));
  xc.nTextures[0] = rc.nTextures[0];
  xc.nTextures[1] = rc.nTextures[1];
  xc.nTrace[0] = 1;
  xc.nSet[1] = bFirst ? 1 : 0;
  xc.nInputs[0] = (uint32)f.pNormals->m_nSRV;
  xc.nInputs[1] = (uint32)f.pDepth[c]->m_nSRV;
  xc.nInputs[2] = (uint32)f.pDepth[p]->m_nSRV;
  const auto Constants = [&]() -> D3D12_GPU_VIRTUAL_ADDRESS
  {
    const SD3D12FrameAlloc CB = m_pUpload->AllocFrame(sizeof(xc), 256);
    memcpy(CB.pCPU, &xc, sizeof(xc));
    return CB.nGPU;
  };
  // The textures' states kept by the device (EnsureRaySunFfx): each kernel asks for what it reads and writes, nothing moved
  // back after (the frame before's depth, read by this one, stays readable).
  const auto Read = [&](CD3D12Texture *pTex) { m_pDevice->Require(pTex->m_pResource, SHADER_READ); };
  const auto Write = [&](CD3D12Texture *pTex) { m_pDevice->Require(pTex->m_pResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS); };
  const UINT gx = (UINT)(w + 7) / 8, gy = (UINT)(h + 7) / 8;
  const UINT nPrepareX = (UINT)((w + 7) / 8 + 3) / 4, nPrepareY = (UINT)((h + 3) / 4 + 3) / 4;

  GpuSection("rt sun shadows FidelityFX Denoiser");
  Write(f.pNormals);
  Write(f.pDepth[c]);
  m_pCompute->DispatchRays(pList, eD3D12K_RaySunFfxInputs, FrameCB, Constants(), 0, 0, f.nTables[FFX_INPUTS + c], gx, gy);
  Read(f.pNormals);
  Read(f.pDepth[c]);
  Read(f.pDepth[p]);
  for (int s = 0; s < nSets; s++)
  {
    xc.nSet[0] = (uint32)s;
    // The live set's history is of no use when the frame before had no live casters.
    xc.nSet[1] = bFirst || s >= f.nSets ? 1 : 0;
    xc.nInputs[3] = (uint32)f.pHistory[s]->m_nSRV;
    xc.nInputs2[0] = (uint32)f.pMoments[s][p]->m_nSRV;
    if (s)
    {
      // The set before's masks and tiles read until now.
      m_pDevice->UavBarrier(f.pTiles->m_pResource);
      m_pDevice->UavBarrier(f.pMeta->m_pResource);
    }
    m_pCompute->DispatchRays(pList, eD3D12K_RaySunFfxPrepare, FrameCB, Constants(), 0, 0, f.nTables[FFX_PREPARE], nPrepareX, nPrepareY);
    m_pDevice->UavBarrier(f.pTiles->m_pResource);
    Read(f.pHistory[s]);
    Read(f.pMoments[s][p]);
    Write(f.pReprojected);
    Write(f.pMoments[s][c]);
    m_pCompute->DispatchRays(pList, eD3D12K_RaySunFfxClassify, FrameCB, Constants(), 0, 0, f.nTables[FFX_CLASSIFY + 2 * s + c], gx, gy);
    Read(f.pReprojected);
    m_pDevice->UavBarrier(f.pMeta->m_pResource);
    // The filter's passes: steps 1, 2 (into the next frame's history), 4.
    xc.nSet[2] = 0;
    xc.nSet[3] = 1;
    xc.nInputs2[1] = (uint32)f.pReprojected->m_nSRV;
    Write(f.pFiltered);
    m_pCompute->DispatchRays(pList, eD3D12K_RaySunFfxFilter, FrameCB, Constants(), 0, 0, f.nTables[FFX_FILTER0], gx, gy);
    Read(f.pFiltered);
    xc.nSet[2] = 1;
    xc.nSet[3] = 2;
    xc.nInputs2[1] = (uint32)f.pFiltered->m_nSRV;
    Write(f.pHistory[s]);
    m_pCompute->DispatchRays(pList, eD3D12K_RaySunFfxFilter, FrameCB, Constants(), 0, 0, f.nTables[FFX_FILTER1 + s], gx, gy);
    Read(f.pHistory[s]);
    xc.nSet[2] = 2;
    xc.nSet[3] = 4;
    xc.nInputs2[1] = (uint32)f.pHistory[s]->m_nSRV;
    Write(f.pDenoised[s]);
    m_pCompute->DispatchRays(pList, eD3D12K_RaySunFfxFilter, FrameCB, Constants(), 0, 0, f.nTables[FFX_FILTER2 + s], gx, gy);
    Read(f.pDenoised[s]);
  }
  // Both sets into the mask.
  xc.nInputs2[2] = (uint32)f.pDenoised[0]->m_nSRV;
  xc.nInputs2[3] = nSets > 1 ? (uint32)f.pDenoised[1]->m_nSRV : XR_RAY_NONE;
  Write(m_RaySun.pMask);
  m_pCompute->DispatchRays(pList, eD3D12K_RaySunFfxPack, FrameCB, Constants(), 0, 0, m_RaySun.nTables[TABLE_FILTER], gx, gy);
  m_pDevice->Require(m_RaySun.pMask->m_pResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  f.nSets = nSets;
}

void CD3D12Renderer::EndRaySunMask()
{
  if (!m_RaySun.bActive)
    return;
  m_RaySun.bActive = false;
  InvalidateFrameConstants();
}
