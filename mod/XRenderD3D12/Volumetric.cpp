// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Volumetric.cpp
//	Description: The volumetric light of the main view
//	(d3d12_VolumetricLight, with r_HDRRendering).
//
//	The air between the camera and the scene scatters light towards the
//	camera and takes away some of the light behind it. Its froxels (cells
//	of the view's frustum, VOLUME_WIDTH across, as many down as the view's
//	shape gives, VOLUME_SLICES slices from VOLUME_NEAR to
//	d3d12_VolumetricLightDistance, each the same factor deeper than the one
//	before) are made each frame after the main scene, before the HDR
//	resolve (Shaders/Volumetric.hlsl):
//	  - the injection: at a point in each froxel that moves from frame to
//	    frame, the extinction (d3d12_VolumetricLightDensity up to the water's
//	    level, thinning to half over each d3d12_VolumetricLightHeight above)
//	    and the light the air there scatters towards the camera: the sun's,
//	    as much more forwards as d3d12_VolumetricLightAnisotropy says, where
//	    the full set of the sun's cascades shows the sun (Cascades.cpp), and
//	    the light from all round, the fog's colour (AMBIENT_IN_SHADOW of it
//	    where the sun is hidden: so is most of the sky under the leaves and
//	    the roofs); blended with the froxels
//	    of the frame before (HISTORY_WEIGHT of them), which smooths the
//	    moving points out;
//	  - the integration: along each column of froxels, the light scattered
//	    towards the camera up to each froxel's far end and the part of the
//	    light behind that reaches the camera.
//	The resolve (Hdr.hlsl, HDR_VOLUMETRIC) takes them at each pixel's depth
//	(the depth buffer, read only meanwhile): the scene's light times that
//	part plus the light scattered, before the exposure. The sky is behind
//	all the froxels. The shafts of light are where the sun's light gets
//	through the leaves and the beams of a building's windows; beyond the
//	cascades the sun reaches everywhere.
//	None with an upscaler, in heat vision or under water, and indoors where
//	the outdoor lights do not reach (the camera's vis area; it fades out and
//	in over FADE_TIME); off, nothing of it runs and the frame is the same as
//	without it.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "D3D12Device.h"
#include "D3D12Upload.h"
#include "D3D12Descriptors.h"
#include "D3D12Compute.h"
#include "Scene.h"

namespace
{

void Set4(float v[4], float x, float y, float z, float w)
{
  v[0] = x; v[1] = y; v[2] = z; v[3] = w;
}

//! The froxels across the view, whatever its size.
constexpr int VOLUME_WIDTH = 160;
//! Their slices, from VOLUME_NEAR (m) to d3d12_VolumetricLightDistance.
constexpr int VOLUME_SLICES = 128;
constexpr float VOLUME_NEAR = 0.5f;
constexpr DXGI_FORMAT VOLUME_FORMAT = DXGI_FORMAT_R16G16B16A16_FLOAT;
//! The part of a froxel that comes from the frame before.
constexpr float HISTORY_WEIGHT = 0.9f;
//! The seconds the volumetric light takes to fade out indoors and in outside.
constexpr float FADE_TIME = 0.5f;
//! The part of the light from all round the air scatters where the sun is hidden.
constexpr float AMBIENT_IN_SHADOW = 0.25f;
//! A table of a kernel: its SRVs and UAVs (D3D12Compute.h).
constexpr int KERNEL_TABLE = CD3D12Compute::COMPUTE_SRVS + CD3D12Compute::COMPUTE_UAVS;
constexpr D3D12_RESOURCE_STATES FROXELS_READ = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
constexpr D3D12_RESOURCE_STATES CASCADES_READ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
constexpr D3D12_RESOURCE_STATES DEPTH_READ = D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

//! The light of a colour of the game (Shaders/HdrLight.hlsli's HdrDecode).
float HdrDecode(float c)
{
  return c > 1 ? 1 + 2.2f * (c - 1) : powf(std::max(c, 0.0f), 2.2f);
}

//! Element i (from 1) of the Halton sequence of base b.
float Halton(int i, int b)
{
  float f = 1, r = 0;
  for (; i > 0; i /= b)
  {
    f /= (float)b;
    r += f * (float)(i % b);
  }
  return r;
}

D3D12_SHADER_RESOURCE_VIEW_DESC VolumeSRV()
{
  D3D12_SHADER_RESOURCE_VIEW_DESC SRV = {};
  SRV.Format = VOLUME_FORMAT;
  SRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
  SRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  SRV.Texture3D.MipLevels = 1;
  return SRV;
}

D3D12_UNORDERED_ACCESS_VIEW_DESC VolumeUAV()
{
  D3D12_UNORDERED_ACCESS_VIEW_DESC UAV = {};
  UAV.Format = VOLUME_FORMAT;
  UAV.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
  UAV.Texture3D.WSize = VOLUME_SLICES;
  return UAV;
}

}

bool CD3D12Renderer::UpdateVolumetricLight()
{
  if (!CV_d3d12_VolumetricLight)
  {
    if (m_Volumetric.pIntegrated)
      ReleaseVolumetricLight();
    return false;
  }
  if (m_bUpscaling || m_bHeatVision || !m_pCompute || !m_pUpload || !m_pDescriptors || !m_pDevice)
    return false;
  I3DEngine *p3D = iSystem ? iSystem->GetI3DEngine() : nullptr;
  const float fWaterZ = p3D ? p3D->GetWaterLevel() : -FLT_MAX;
  const Vec3 vCamera = m_Camera.GetPos();
  if (vCamera.z < fWaterZ)
    return false;
  // The froxels in the view's shape.
  const int w = m_pDevice->GetWidth(), h = m_pDevice->GetHeight();
  const int nHeight = std::clamp((int)lroundf((float)VOLUME_WIDTH * h / std::max(w, 1)), 1, 4 * VOLUME_WIDTH);
  if (!EnsureVolumetricTargets(VOLUME_WIDTH, nHeight))
    return false;
  // Indoors, in a vis area that the outdoor lights do not reach, the air has none of the sun's
  // light nor the fog's: it fades out over FADE_TIME, and in again outside; at once after a frame
  // without it.
  IVisArea *pArea = p3D ? p3D->GetVisAreaFromPos(vCamera) : nullptr;
  const float fOutdoor = pArea && !pArea->IsAfectedByOutLights() ? 0.0f : 1.0f;
  if (m_Volumetric.nOutdoorFrame == m_nFrames - 1)
  {
    const float fStep = (iTimer ? std::clamp(iTimer->GetFrameTime(), 0.0f, 1.0f) : 0.02f) / FADE_TIME;
    m_Volumetric.fOutdoor = std::clamp(fOutdoor, m_Volumetric.fOutdoor - fStep, m_Volumetric.fOutdoor + fStep);
  }
  else
    m_Volumetric.fOutdoor = fOutdoor;
  m_Volumetric.nOutdoorFrame = m_nFrames;
  if (m_Volumetric.fOutdoor <= 0)
    return false;
  ID3D12Resource *pDepth = m_pDevice->GetMainDepth();
  if (pDepth != m_Volumetric.pDepth.Get())
  {
    if (m_Volumetric.nDepthSRV >= 0)
      m_pDescriptors->FreeSRV(m_Volumetric.nDepthSRV);
    m_Volumetric.pDepth = pDepth;
    D3D12_SHADER_RESOURCE_VIEW_DESC SRV = {};
    SRV.Format = m_pDevice->GetMainDepthSRVFormat();
    SRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    SRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    SRV.Texture2D.MipLevels = 1;
    m_Volumetric.nDepthSRV = pDepth ? m_pDescriptors->AllocSRV(pDepth, &SRV) : -1;
  }
  const int nSlot = m_pDevice->GetFrameSlot();
  const int nInject = m_Volumetric.Tables[nSlot], nIntegrate = m_Volumetric.Tables[D3D12_FRAMES + nSlot];
  if (m_Volumetric.nDepthSRV < 0 || nInject < 0 || nIntegrate < 0)
    return false;
  GpuSection("volumetric light");

  SVolumetricConstants vc = {};
  // The sun's cascades where the main view's receivers have them.
  SFrameConstants fc = {};
  SetCascadeConstants(fc);
  const int nCascades = (int)fc.vCascadeParams[0];
  memcpy(vc.mCascade, fc.mCascade, sizeof(vc.mCascade));
  memcpy(vc.vCascadeEnd, fc.vCascadeEnd, sizeof(vc.vCascadeEnd));
  memcpy(vc.vCascadeDepth, fc.vCascadeDepth, sizeof(vc.vCascadeDepth));
  Set4(vc.vCascadeParams, (float)nCascades, 0, 0, fc.vCascadeParams[3]);

  // The froxels' world positions from the view: its axes in the world (the columns of the view
  // matrix) and the projection's terms of x, y and w.
  const float *V = m_CameraView.data, *P = m_ProjMatrix.data;
  const Vec3 vRight(V[0], V[4], V[8]), vUp(V[1], V[5], V[9]), vAxis(V[2], V[6], V[10]);
  const float fW = P[11] != 0 ? P[11] : -1.0f;
  const Vec3 vRayX = vRight / P[0], vRayY = vUp / P[5];
  const Vec3 vRayO = vAxis / fW - vRayX * (P[8] / fW) - vRayY * (P[9] / fW);
  Set4(vc.vCameraPos, vCamera.x, vCamera.y, vCamera.z, 1);
  Set4(vc.vRayX, vRayX.x, vRayX.y, vRayX.z, 0);
  Set4(vc.vRayY, vRayY.x, vRayY.y, vRayY.z, 0);
  Set4(vc.vRayO, vRayO.x, vRayO.y, vRayO.z, 0);
  const float fNear = VOLUME_NEAR, fFar = std::max(CV_d3d12_VolumetricLightDistance, 2 * VOLUME_NEAR);
  const float fLog = log2f(fFar / fNear);
  Set4(vc.vSize, (float)m_Volumetric.nWidth, (float)m_Volumetric.nHeight, (float)VOLUME_SLICES, 1.0f / VOLUME_SLICES);
  Set4(vc.vSlices, fNear, fLog, 1 / fLog, fFar);
  const int n = m_nFrames % 32 + 1;
  Set4(vc.vJitter, Halton(n, 2), Halton(n, 3), Halton(n, 5), 0);

  // The light: the sun's (with a sun in the scene) and the light from all round.
  float fLen = sqrtf(m_SunDir[0] * m_SunDir[0] + m_SunDir[1] * m_SunDir[1] + m_SunDir[2] * m_SunDir[2]);
  if (fLen <= 0)
    fLen = 1;
  Set4(vc.vSunDir, m_SunDir[0] / fLen, m_SunDir[1] / fLen, m_SunDir[2] / fLen, std::clamp(CV_d3d12_VolumetricLightAnisotropy, -0.95f, 0.95f));
  const float fSun = m_pScene && m_pScene->GetSun() ? std::max(CV_d3d12_VolumetricLightSun, 0.0f) : 0.0f;
  Set4(vc.vSunLight, HdrDecode(m_SunColor[0]) * fSun, HdrDecode(m_SunColor[1]) * fSun, HdrDecode(m_SunColor[2]) * fSun, 0);
  const float fAmbient = std::max(CV_d3d12_VolumetricLightAmbient, 0.0f);
  Set4(vc.vAmbientLight, HdrDecode(m_FogColor[0]) * fAmbient, HdrDecode(m_FogColor[1]) * fAmbient, HdrDecode(m_FogColor[2]) * fAmbient, AMBIENT_IN_SHADOW);
  const bool bHistory = m_Volumetric.nFrame == m_nFrames - 1;
  Set4(vc.vDensity, std::max(CV_d3d12_VolumetricLightDensity, 0.0f) * m_Volumetric.fOutdoor, fWaterZ > -1e6f ? fWaterZ : 0.0f,
       CV_d3d12_VolumetricLightHeight > 0 ? 1 / CV_d3d12_VolumetricLightHeight : 0.0f, bHistory ? HISTORY_WEIGHT : 0.0f);
  memcpy(vc.mPrevViewProj, m_Volumetric.PrevViewProj, sizeof(vc.mPrevViewProj));
  const SD3D12FrameAlloc CB = m_pUpload->AllocFrame(sizeof(vc), 256);
  memcpy(CB.pCPU, &vc, sizeof(vc));

  // The injection: the history, the full set of the cascades; this frame's froxels.
  const int nCur = m_Volumetric.nFroxels ^= 1;
  CD3D12Texture *pFroxels = m_Volumetric.pFroxels[nCur], *pHistory = m_Volumetric.pFroxels[nCur ^ 1];
  const D3D12_SHADER_RESOURCE_VIEW_DESC SRV = VolumeSRV();
  const D3D12_UNORDERED_ACCESS_VIEW_DESC UAV = VolumeUAV();
  m_pDescriptors->WriteSRV(nInject, pHistory->m_pResource, &SRV);
  D3D12_SHADER_RESOURCE_VIEW_DESC Maps = {};
  Maps.Format = DXGI_FORMAT_R32_FLOAT;
  Maps.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
  Maps.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  Maps.Texture2DArray.MipLevels = 1;
  Maps.Texture2DArray.ArraySize = XR_MAX_CASCADES;
  ID3D12Resource *pMaps = nCascades > 0 ? m_CascadeMaps[0].pDepth.Get() : nullptr;
  m_pDescriptors->WriteSRV(nInject + 1, pMaps, &Maps);
  m_pDescriptors->WriteUAV(nInject + CD3D12Compute::COMPUTE_SRVS, pFroxels->m_pResource, &UAV);
  // The integration: this frame's froxels; the integrated ones.
  m_pDescriptors->WriteSRV(nIntegrate, pFroxels->m_pResource, &SRV);
  m_pDescriptors->WriteUAV(nIntegrate + CD3D12Compute::COMPUTE_SRVS, m_Volumetric.pIntegrated->m_pResource, &UAV);

  ID3D12GraphicsCommandList *pList = m_pDevice->GetCmdList();
  const UINT nGroupsX = (UINT)(m_Volumetric.nWidth + XR_VOLUMETRIC_GROUP - 1) / XR_VOLUMETRIC_GROUP;
  const UINT nGroupsY = (UINT)(m_Volumetric.nHeight + XR_VOLUMETRIC_GROUP - 1) / XR_VOLUMETRIC_GROUP;
  m_pDevice->EndOffscreen();
  m_pDevice->Transition(pFroxels->m_pResource, FROXELS_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  if (pMaps)
    m_pDevice->Transition(pMaps, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, CASCADES_READ);
  m_pCompute->Dispatch(pList, eD3D12K_VolumetricInject, CB.nGPU, nInject, nInject + CD3D12Compute::COMPUTE_SRVS, nGroupsX, nGroupsY, VOLUME_SLICES);
  if (pMaps)
    m_pDevice->Transition(pMaps, CASCADES_READ, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  m_pDevice->Transition(pFroxels->m_pResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, FROXELS_READ);
  m_pDevice->Transition(m_Volumetric.pIntegrated->m_pResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  m_pCompute->Dispatch(pList, eD3D12K_VolumetricIntegrate, CB.nGPU, nIntegrate, nIntegrate + CD3D12Compute::COMPUTE_SRVS, nGroupsX, nGroupsY);
  m_pDevice->Transition(m_Volumetric.pIntegrated->m_pResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

  const Matrix44 ViewProj = m_CameraView * m_ProjMatrix;
  memcpy(m_Volumetric.PrevViewProj, ViewProj.data, sizeof(m_Volumetric.PrevViewProj));
  m_Volumetric.nFrame = m_nFrames;
  m_Volumetric.fNear = fNear;
  m_Volumetric.fFar = fFar;
  m_Volumetric.fM22 = P[10];
  m_Volumetric.fM32 = P[14];
  if (m_pFrameDump)
    fprintf(m_pFrameDump.get(), "volumetric light: %d x %d x %d froxels, %g to %g m, extinction %g / m from %.2f m (half over %g m), sun (%.3f %.3f %.3f) g %g, "
            "ambient (%.3f %.3f %.3f), outdoors %.2f, %d cascades, history %d\n", m_Volumetric.nWidth, m_Volumetric.nHeight, VOLUME_SLICES, fNear, fFar,
            vc.vDensity[0], vc.vDensity[1], CV_d3d12_VolumetricLightHeight, vc.vSunLight[0], vc.vSunLight[1], vc.vSunLight[2], vc.vSunDir[3],
            vc.vAmbientLight[0], vc.vAmbientLight[1], vc.vAmbientLight[2], m_Volumetric.fOutdoor, nCascades, bHistory ? 1 : 0);
  return true;
}

bool CD3D12Renderer::SetVolumetricConstants(SDrawConstants &dc) const
{
  if (m_Volumetric.nFrame != m_nFrames || m_Volumetric.nIntegratedSRV < 0 || m_Volumetric.nDepthSRV < 0)
    return false;
  dc.nFlags |= HDR_VOLUMETRIC;
  dc.nTex4 = (uint32)m_Volumetric.nIntegratedSRV;
  dc.nTex5 = (uint32)m_Volumetric.nDepthSRV;
  Set4(&dc.mParams0[0], m_Volumetric.fNear, 1 / log2f(m_Volumetric.fFar / m_Volumetric.fNear), 0.5f / VOLUME_SLICES, m_Volumetric.fM22);
  Set4(&dc.mParams0[4], m_Volumetric.fM32, CV_d3d12_VolumetricLight == 2 ? 1.0f : 0.0f, 0, 0);
  return true;
}

void CD3D12Renderer::SetVolumetricDepthRead(bool bRead)
{
  ID3D12Resource *pDepth = m_Volumetric.pDepth.Get();
  if (!pDepth)
    return;
  if (bRead)
  {
    m_pDevice->Transition(pDepth, D3D12_RESOURCE_STATE_DEPTH_WRITE, DEPTH_READ);
    m_pDevice->SetDepthReadOnly(true);
  }
  else
  {
    m_pDevice->SetDepthReadOnly(false);
    m_pDevice->Transition(pDepth, DEPTH_READ, D3D12_RESOURCE_STATE_DEPTH_WRITE);
  }
}

bool CD3D12Renderer::EnsureVolumetricTargets(int nWidth, int nHeight)
{
  if (nWidth == m_Volumetric.nWidth && nHeight == m_Volumetric.nHeight && m_Volumetric.pIntegrated)
    return true;
  ReleaseVolumetricLight();
  ID3D12GraphicsCommandList *pList = m_pDevice->GetCmdList();
  for (CD3D12Texture *&pTex : m_Volumetric.pFroxels)
  {
    pTex = m_pUpload->CreateComputeVolume(nWidth, nHeight, VOLUME_SLICES, VOLUME_FORMAT, L"XRenderD3D12 volumetric light froxels");
    if (pTex && pList)
      m_pDevice->Transition(pTex->m_pResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, FROXELS_READ);
  }
  m_Volumetric.pIntegrated = m_pUpload->CreateComputeVolume(nWidth, nHeight, VOLUME_SLICES, VOLUME_FORMAT, L"XRenderD3D12 volumetric light integrated");
  if (m_Volumetric.pIntegrated && pList)
  {
    m_pDevice->Transition(m_Volumetric.pIntegrated->m_pResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    const D3D12_SHADER_RESOURCE_VIEW_DESC SRV = VolumeSRV();
    m_Volumetric.nIntegratedSRV = m_pDescriptors->AllocSRV(m_Volumetric.pIntegrated->m_pResource, &SRV);
  }
  // The kernels' tables, D3D12_FRAMES a kernel; the views they do not use null.
  D3D12_SHADER_RESOURCE_VIEW_DESC NullSRV = VolumeSRV();
  D3D12_UNORDERED_ACCESS_VIEW_DESC NullUAV = VolumeUAV();
  m_Volumetric.Tables.assign(2 * D3D12_FRAMES, -1);
  for (int &nTable : m_Volumetric.Tables)
  {
    if ((nTable = m_pDescriptors->AllocRange(KERNEL_TABLE)) < 0)
      continue;
    for (int i = 0; i < CD3D12Compute::COMPUTE_SRVS; i++)
      m_pDescriptors->WriteSRV(nTable + i, nullptr, &NullSRV);
    for (int i = 0; i < CD3D12Compute::COMPUTE_UAVS; i++)
      m_pDescriptors->WriteUAV(nTable + CD3D12Compute::COMPUTE_SRVS + i, nullptr, &NullUAV);
  }
  m_Volumetric.nWidth = nWidth;
  m_Volumetric.nHeight = nHeight;
  m_Volumetric.nFrame = -1;
  if (!pList || !m_Volumetric.pFroxels[0] || !m_Volumetric.pFroxels[1] || !m_Volumetric.pIntegrated || m_Volumetric.nIntegratedSRV < 0)
  {
    iLog->LogError("XRenderD3D12: no volumetric light froxels for %d x %d x %d", nWidth, nHeight, VOLUME_SLICES);
    ReleaseVolumetricLight();
    return false;
  }
  if (iLog)
    iLog->Log("XRenderD3D12: the volumetric light: %d x %d x %d froxels", nWidth, nHeight, VOLUME_SLICES);
  return true;
}

void CD3D12Renderer::ReleaseVolumetricLight()
{
  if (m_pUpload)
  {
    for (CD3D12Texture *&pTex : m_Volumetric.pFroxels)
      m_pUpload->ReleaseTexture(pTex);
    m_pUpload->ReleaseTexture(m_Volumetric.pIntegrated);
  }
  if (m_pDescriptors)
  {
    for (int nTable : m_Volumetric.Tables)
    {
      if (nTable >= 0)
        m_pDescriptors->FreeRange(nTable, KERNEL_TABLE);
    }
    if (m_Volumetric.nIntegratedSRV >= 0)
      m_pDescriptors->FreeSRV(m_Volumetric.nIntegratedSRV);
    if (m_Volumetric.nDepthSRV >= 0)
      m_pDescriptors->FreeSRV(m_Volumetric.nDepthSRV);
  }
  m_Volumetric = SVolumetricState();
}
