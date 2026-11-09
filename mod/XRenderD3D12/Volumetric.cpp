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
//	(the depth buffer, read only meanwhile; with an upscaler the scene's,
//	at the render size, whose jitter is a small part of a froxel): the
//	scene's light times that
//	part plus the light scattered, before the exposure. The sky is behind
//	all the froxels. The shafts of light are where the sun's light gets
//	through the leaves and the beams of a building's windows; beyond the
//	cascades the sun reaches everywhere.
//	None in heat vision or under water, and indoors where
//	the outdoor lights do not reach (the camera's vis area; it fades out and
//	in over FADE_TIME); off, nothing of it runs and the frame is the same as
//	without it.
//	The sun by rays (d3d12_VolumetricLightRays, with the sun's shadow by
//	rays or the path tracer: WantsVolumetricRays): before the injection a
//	ray from each froxel's point towards the sun through the scene for rays
//	(Shaders/VolumetricRays.hlsl) gives the sun's state there, which the
//	injection takes in place of the cascades (XR_VOL_RAYS): its shadows to
//	the rays' length, not the cascades' range, and indoors too. What keeps
//	the sun out of a room is in the scene for rays (its walls and roof the
//	view has, the rest from the 3D engine's cameras round the camera,
//	UpdateRayBehind), so the air is lit only where the sun comes through
//	the room's openings: Training's bunker's shafts of sunlight, whose
//	planes (TemplMuzzleFlash_Auto) are then not drawn (HasVolumetricShafts).
//	Indoors the frame has no sun: the 3D engine's (its position raised as
//	the scene's sun outdoors, its colour), and the air there is
//	d3d12_VolumetricLightIndoors times as dense. The planes of the sky box
//	that close a room's openings (Training's bunker) are pieces of the sky
//	the rays pass (XR_RAY_SKY).
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "D3D12Device.h"
#include "D3D12Upload.h"
#include "D3D12Descriptors.h"
#include "D3D12Compute.h"
#include "Scene.h"
#include "RayTracing.h"

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
//! The sun's states at the froxels' points (d3d12_VolumetricLightRays): 1 lit, 0 in a shadow, -1 not air.
constexpr DXGI_FORMAT SUN_STATES_FORMAT = DXGI_FORMAT_R8_SNORM;
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

D3D12_SHADER_RESOURCE_VIEW_DESC VolumeSRV(DXGI_FORMAT Format = VOLUME_FORMAT)
{
  D3D12_SHADER_RESOURCE_VIEW_DESC SRV = {};
  SRV.Format = Format;
  SRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
  SRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  SRV.Texture3D.MipLevels = 1;
  return SRV;
}

D3D12_UNORDERED_ACCESS_VIEW_DESC VolumeUAV(DXGI_FORMAT Format = VOLUME_FORMAT)
{
  D3D12_UNORDERED_ACCESS_VIEW_DESC UAV = {};
  UAV.Format = Format;
  UAV.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
  UAV.Texture3D.WSize = VOLUME_SLICES;
  return UAV;
}

}

bool CD3D12Renderer::WantsVolumetricRays() const
{
  return CV_d3d12_VolumetricLight && CV_d3d12_VolumetricLightRays > 0 && WantsRaySun();
}

bool CD3D12Renderer::HasVolumetricShafts() const
{
  return m_Volumetric.bRays && m_Volumetric.nFrame >= m_nFrames - 1 && WantsVolumetricRays() && !m_bHeatVision;
}

bool CD3D12Renderer::UpdateVolumetricLight(ID3D12Resource *pDepth, DXGI_FORMAT DepthFormat)
{
  if (!CV_d3d12_VolumetricLight)
  {
    if (m_Volumetric.pIntegrated)
      ReleaseVolumetricLight();
    return false;
  }
  // The sun's states of the frame before, where its update had the sun by rays (the same texture still: not a new size's).
  const CD3D12Texture *pStatesBefore = m_Volumetric.bRays && m_Volumetric.nFrame == m_nFrames - 1 ? m_Volumetric.pSunStates : nullptr;
  m_Volumetric.bRays = false;
  if (m_bHeatVision || !m_pCompute || !m_pUpload || !m_pDescriptors || !m_pDevice)
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
  // The sun by rays: this frame's scene for rays built, the kernels there.
  const bool bRays = WantsVolumetricRays() && m_pRayScene && m_pRayScene->GetBuiltFrame() == m_nFrames && m_pRayScene->GetTLAS() &&
                     m_pCompute->HasKernel(eD3D12K_VolumetricSun) && m_pCompute->HasKernel(eD3D12K_VolumetricInjectRays) && EnsureVolumetricSunStates();
  // Indoors, in a vis area that the outdoor lights do not reach, the air has none of the sun's
  // light nor the fog's: it fades out over FADE_TIME, and in again outside; at once after a frame
  // without it. With the sun by rays it stays: the scene for rays keeps the sun out of the room
  // but where it comes in, its air the denser by d3d12_VolumetricLightIndoors (fades in and out the same way).
  IVisArea *pArea = p3D ? p3D->GetVisAreaFromPos(vCamera) : nullptr;
  const bool bIndoors = pArea && !pArea->IsAfectedByOutLights();
  const float fOutdoor = bIndoors && !bRays ? 0.0f : 1.0f;
  const float fIndoors = bIndoors && bRays ? 1.0f : 0.0f;
  if (m_Volumetric.nOutdoorFrame == m_nFrames - 1)
  {
    const float fStep = (iTimer ? std::clamp(iTimer->GetFrameTime(), 0.0f, 1.0f) : 0.02f) / FADE_TIME;
    m_Volumetric.fOutdoor = std::clamp(fOutdoor, m_Volumetric.fOutdoor - fStep, m_Volumetric.fOutdoor + fStep);
    m_Volumetric.fIndoors = std::clamp(fIndoors, m_Volumetric.fIndoors - fStep, m_Volumetric.fIndoors + fStep);
  }
  else
  {
    m_Volumetric.fOutdoor = fOutdoor;
    m_Volumetric.fIndoors = fIndoors;
  }
  m_Volumetric.nOutdoorFrame = m_nFrames;
  if (m_Volumetric.fOutdoor <= 0)
    return false;
  // The depth buffer's view (a new one for the upscaler's scene depth and back).
  if (pDepth != m_Volumetric.pDepth.Get())
  {
    if (m_Volumetric.nDepthSRV >= 0)
      m_pDescriptors->FreeSRV(m_Volumetric.nDepthSRV);
    m_Volumetric.pDepth = pDepth;
    D3D12_SHADER_RESOURCE_VIEW_DESC SRV = {};
    SRV.Format = DepthFormat;
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
  // Half the froxels' rays a frame where the others have the frame before's states (not the first frame with them, nor
  // after a frame without, nor with a new size: all of them then); the points' sequence then in an order that gives
  // each froxel points all over it (VolSunPointIndex).
  const bool bHalf = CV_d3d12_VolumetricLightRaysHalf != 0 && bRays && pStatesBefore && pStatesBefore == m_Volumetric.pSunStates;
  const int n = (CV_d3d12_VolumetricLightRaysHalf != 0 && bRays ? (int)((((uint32)m_nFrames >> 1) + ((m_nFrames & 1) ? 16 : 0)) & 31) : m_nFrames % 32) + 1;
  Set4(vc.vJitter, Halton(n, 2), Halton(n, 3), Halton(n, 5), 0);

  // The light: the sun's (with a sun in the scene) and the light from all round. Indoors the scene has
  // none (the outdoor lights do not reach the camera's area): with the sun by rays the 3D engine's, raised as that
  // of the scene outdoors (the light the 3D engine adds there: its shadows and light maps).
  Vec3 vSunDir(m_SunDir[0], m_SunDir[1], m_SunDir[2]);
  float SunColor[3] = { m_SunColor[0], m_SunColor[1], m_SunColor[2] };
  bool bSun = m_pScene && m_pScene->GetSun();
  bool bEngineSun = false;
  if (!bSun && bRays && p3D)
  {
    const Vec3 vTo = p3D->GetSunPosition(true) - vCamera;
    const Vec3 Color = p3D->GetSunColor();
    if (vTo.GetLengthSquared() > 1e-6f)
    {
      vSunDir = vTo;
      SunColor[0] = Color.x;
      SunColor[1] = Color.y;
      SunColor[2] = Color.z;
      bSun = bEngineSun = true;
    }
  }
  float fLen = vSunDir.GetLength();
  if (fLen <= 0)
    fLen = 1;
  Set4(vc.vSunDir, vSunDir.x / fLen, vSunDir.y / fLen, vSunDir.z / fLen, std::clamp(CV_d3d12_VolumetricLightAnisotropy, -0.95f, 0.95f));
  const float fShafts = std::max(CV_d3d12_VolumetricLightShafts, 0.0f);
  const float fSun =
    bSun ? std::max(CV_d3d12_VolumetricLightSun, 0.0f) * (fShafts > 0 ? std::max(CV_d3d12_VolumetricLightShaftsSun, 0.0f) : 1.0f) : 0.0f;
  Set4(vc.vSunLight, HdrDecode(SunColor[0]) * fSun, HdrDecode(SunColor[1]) * fSun, HdrDecode(SunColor[2]) * fSun, 0);
  Set4(vc.vShafts, fShafts, std::max(CV_d3d12_VolumetricLightShaftsPower, 0.0f), 0, 0);
  const float fAmbient = fShafts > 0 ? 0.0f : std::max(CV_d3d12_VolumetricLightAmbient, 0.0f);
  Set4(vc.vAmbientLight, HdrDecode(m_FogColor[0]) * fAmbient, HdrDecode(m_FogColor[1]) * fAmbient, HdrDecode(m_FogColor[2]) * fAmbient, AMBIENT_IN_SHADOW);
  const bool bHistory = m_Volumetric.nFrame == m_nFrames - 1 && !IsCameraCut();  // none after a cut (d3d12_CutReset)
  const float fDensity = std::max(CV_d3d12_VolumetricLightDensity, 0.0f) * m_Volumetric.fOutdoor *
                         (1 + (std::max(CV_d3d12_VolumetricLightIndoors, 0.0f) - 1) * m_Volumetric.fIndoors);
  Set4(vc.vDensity, fDensity, fWaterZ > -1e6f ? fWaterZ : 0.0f,
       CV_d3d12_VolumetricLightHeight > 0 ? 1 / CV_d3d12_VolumetricLightHeight : 0.0f, bHistory ? HISTORY_WEIGHT : 0.0f);
  memcpy(vc.mPrevViewProj, m_Volumetric.PrevViewProj, sizeof(vc.mPrevViewProj));
  const Matrix44 ViewProj = m_CameraView * m_ProjMatrix;
  // The sun by rays: this frame's view (the froxel of a point), the rays' length, the sun's disc and a froxel's height
  // over its depth (the rays' cone), the instances (not the pieces of the sky: the sun is behind them) and the frame.
  memcpy(vc.mViewProj, ViewProj.data, sizeof(vc.mViewProj));
  Set4(vc.vRays, std::max(CV_d3d12_RTDistance, 1.0f), tanf(DEG2RAD(std::clamp(CV_d3d12_RTSunAngle, 0.0f, 10.0f)) * 0.5f),
       2 / (fabsf(P[5]) * m_Volumetric.nHeight), CV_d3d12_VolumetricLightRays >= 2 ? 1.0f : 0.0f);
  vc.nRays[0] = (RayMask() & 0xff & ~(uint32)XR_RAY_SKY) | ((CV_d3d12_RTAlphaCoverage & 4) ? XR_RAY_COVERAGE_RAYS : 0);
  vc.nRays[1] = (uint32)m_nFrames;
  vc.nRays[2] = bHalf ? 1 : 0;
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
  // (t1: the cascades, or with the sun by rays its states: below)
  m_pDescriptors->WriteUAV(nInject + CD3D12Compute::COMPUTE_SRVS, pFroxels->m_pResource, &UAV);
  // The integration: this frame's froxels; the integrated ones.
  m_pDescriptors->WriteSRV(nIntegrate, pFroxels->m_pResource, &SRV);
  m_pDescriptors->WriteUAV(nIntegrate + CD3D12Compute::COMPUTE_SRVS, m_Volumetric.pIntegrated->m_pResource, &UAV);

  ID3D12GraphicsCommandList *pList = m_pDevice->GetCmdList();
  const UINT nGroupsX = (UINT)(m_Volumetric.nWidth + XR_VOLUMETRIC_GROUP - 1) / XR_VOLUMETRIC_GROUP;
  const UINT nGroupsY = (UINT)(m_Volumetric.nHeight + XR_VOLUMETRIC_GROUP - 1) / XR_VOLUMETRIC_GROUP;
  m_pDevice->EndOffscreen();
  // The sun by rays: its states at this frame's points first, read by the injection in place of the cascades.
  const bool bSunRays = bRays && DispatchVolumetricSun(CB.nGPU, bHalf ? (UINT)((m_Volumetric.nWidth + 1) / 2 + XR_VOLUMETRIC_GROUP - 1) / XR_VOLUMETRIC_GROUP : nGroupsX, nGroupsY);
  ID3D12Resource *pMaps = nCascades > 0 && !bSunRays ? m_CascadeMaps[0].pDepth.Get() : nullptr;
  if (bSunRays)
  {
    const D3D12_SHADER_RESOURCE_VIEW_DESC States = VolumeSRV(SUN_STATES_FORMAT);
    m_pDescriptors->WriteSRV(nInject + 1, m_Volumetric.pSunStates->m_pResource, &States);
  }
  else
    m_pDescriptors->WriteSRV(nInject + 1, pMaps, &Maps);
  // Each kernel's inputs and outputs in the states it wants (the device keeps them): the history and the cascades read
  // (the cascades by the opaque list's pixels too), this frame's froxels written, then read by the integration; the
  // integrated froxels left for the resolve's pixels.
  m_pDevice->Require(pHistory->m_pResource, FROXELS_READ);
  m_pDevice->Require(pFroxels->m_pResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  if (pMaps)
    m_pDevice->Require(pMaps, CASCADES_READ);
  m_pCompute->Dispatch(pList, bSunRays ? eD3D12K_VolumetricInjectRays : eD3D12K_VolumetricInject, CB.nGPU, nInject, nInject + CD3D12Compute::COMPUTE_SRVS,
                       nGroupsX, nGroupsY, VOLUME_SLICES);
  m_pDevice->Require(pFroxels->m_pResource, FROXELS_READ);
  m_pDevice->Require(m_Volumetric.pIntegrated->m_pResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  m_pCompute->Dispatch(pList, eD3D12K_VolumetricIntegrate, CB.nGPU, nIntegrate, nIntegrate + CD3D12Compute::COMPUTE_SRVS, nGroupsX, nGroupsY);
  m_pDevice->Require(m_Volumetric.pIntegrated->m_pResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

  memcpy(m_Volumetric.PrevViewProj, ViewProj.data, sizeof(m_Volumetric.PrevViewProj));
  m_Volumetric.nFrame = m_nFrames;
  m_Volumetric.bRays = bSunRays;
  m_Volumetric.fNear = fNear;
  m_Volumetric.fFar = fFar;
  m_Volumetric.fM22 = P[10];
  m_Volumetric.fM32 = P[14];
  if (m_pFrameDump)
    fprintf(m_pFrameDump.get(), "volumetric light: %d x %d x %d froxels, %g to %g m, extinction %g / m from %.2f m (half over %g m), sun (%.3f %.3f %.3f) g %g, "
            "shafts %g m (power %g), ambient (%.3f %.3f %.3f), outdoors %.2f, indoors %.2f, %d cascades, history %d\n", m_Volumetric.nWidth, m_Volumetric.nHeight, VOLUME_SLICES, fNear,
            fFar, vc.vDensity[0], vc.vDensity[1], CV_d3d12_VolumetricLightHeight, vc.vSunLight[0], vc.vSunLight[1], vc.vSunLight[2], vc.vSunDir[3], fShafts, vc.vShafts[1],
            vc.vAmbientLight[0], vc.vAmbientLight[1], vc.vAmbientLight[2], m_Volumetric.fOutdoor, m_Volumetric.fIndoors, nCascades, bHistory ? 1 : 0);
  if (m_pFrameDump)
  {
    const Vec3 vEngine = p3D ? (p3D->GetSunPosition(true) - vCamera).GetNormalized() : Vec3(0, 0, 0);
    const Vec3 EngineColor = p3D ? p3D->GetSunColor() : Vec3(0, 0, 0);
    fprintf(m_pFrameDump.get(), "volumetric light's sun: %s, direction (%.3f %.3f %.3f) colour (%.2f %.2f %.2f)%s; the 3D engine's (%.3f %.3f %.3f) "
            "colour (%.2f %.2f %.2f); rays to %g m, mask 0x%02x%s%s\n", bSunRays ? "by rays" : bRays ? "by the cascades (no rays)" : "by the cascades",
            vc.vSunDir[0], vc.vSunDir[1], vc.vSunDir[2], SunColor[0], SunColor[1], SunColor[2], bEngineSun ? " (the 3D engine's: the frame has no sun)" : "",
            vEngine.x, vEngine.y, vEngine.z, EngineColor.x, EngineColor.y, EngineColor.z, vc.vRays[0], vc.nRays[0], vc.vRays[3] > 0 ? ", the states in colours" : "",
            bSunRays ? (bHalf ? ", half the froxels' rays (d3d12_VolumetricLightRaysHalf)" : ", all the froxels' rays") : "");
  }
  return true;
}

bool CD3D12Renderer::EnsureVolumetricSunStates()
{
  if (m_Volumetric.pSunStates)
    return m_Volumetric.nSunStatesSRV >= 0 && m_Volumetric.nSunTable >= 0;
  if (!m_Volumetric.pIntegrated)
    return false;
  // Made in UNORDERED_ACCESS, its state kept by the device; released with the froxels (a new size makes them again).
  CD3D12Texture *pTex = m_pUpload->CreateComputeVolume(m_Volumetric.nWidth, m_Volumetric.nHeight, VOLUME_SLICES, SUN_STATES_FORMAT,
                                                       L"XRenderD3D12 volumetric light's sun states");
  m_Volumetric.pSunStates = pTex;
  if (!pTex)
    return false;
  m_pDevice->TrackState(pTex->m_pResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  const D3D12_SHADER_RESOURCE_VIEW_DESC SRV = VolumeSRV(SUN_STATES_FORMAT);
  m_Volumetric.nSunStatesSRV = m_pDescriptors->AllocSRV(pTex->m_pResource, &SRV);
  if ((m_Volumetric.nSunTable = m_pDescriptors->AllocRange(CD3D12Compute::COMPUTE_UAVS)) >= 0)
  {
    const D3D12_UNORDERED_ACCESS_VIEW_DESC UAV = VolumeUAV(SUN_STATES_FORMAT);
    for (int i = 0; i < CD3D12Compute::COMPUTE_UAVS; i++)
      m_pDescriptors->WriteUAV(m_Volumetric.nSunTable + i, i == 0 ? pTex->m_pResource : nullptr, &UAV);
  }
  if (m_Volumetric.nSunStatesSRV < 0 || m_Volumetric.nSunTable < 0)
  {
    iLog->LogError("XRenderD3D12: no views for the volumetric light's sun by rays");
    return false;
  }
  return true;
}

bool CD3D12Renderer::DispatchVolumetricSun(D3D12_GPU_VIRTUAL_ADDRESS CB, UINT nGroupsX, UINT nGroupsY)
{
  GpuSection("volumetric light's sun by rays");
  // The builds of the scene done (the frame's list goes on after them).
  const D3D12_GPU_VIRTUAL_ADDRESS nTlas = UseRayScene();
  if (!nTlas)
    return false;
  SFrameConstants fc;
  MakeFrameConstants(fc, false);
  const SD3D12FrameAlloc FrameCB = m_pUpload->AllocFrame(sizeof(fc), 256);
  memcpy(FrameCB.pCPU, &fc, sizeof(fc));
  m_pDevice->Require(m_Volumetric.pSunStates->m_pResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  m_pCompute->DispatchRays(m_pDevice->GetCmdList(), eD3D12K_VolumetricSun, FrameCB.nGPU, CB, nTlas, m_pRayScene->GetInstances(), m_Volumetric.nSunTable,
                           nGroupsX, nGroupsY, 0, VOLUME_SLICES);
  m_pDevice->Require(m_Volumetric.pSunStates->m_pResource, FROXELS_READ);
  GpuSection("volumetric light");
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
  SetDepthRead(m_Volumetric.pDepth.Get(), bRead);
}

void CD3D12Renderer::SetDepthRead(ID3D12Resource *pDepth, bool bRead)
{
  if (!pDepth)
    return;
  // Read with the depth buffer bound read only; bound writable again, the device gives it back for the draws.
  if (bRead)
    m_pDevice->Require(pDepth, DEPTH_READ);
  m_pDevice->SetDepthReadOnly(bRead);
}

bool CD3D12Renderer::EnsureVolumetricTargets(int nWidth, int nHeight)
{
  if (nWidth == m_Volumetric.nWidth && nHeight == m_Volumetric.nHeight && m_Volumetric.pIntegrated)
    return true;
  ReleaseVolumetricLight();
  ID3D12GraphicsCommandList *pList = m_pDevice->GetCmdList();
  // Made in UNORDERED_ACCESS, their states kept by the device (UpdateVolumetricLight's kernels ask for theirs).
  for (CD3D12Texture *&pTex : m_Volumetric.pFroxels)
  {
    pTex = m_pUpload->CreateComputeVolume(nWidth, nHeight, VOLUME_SLICES, VOLUME_FORMAT, L"XRenderD3D12 volumetric light froxels");
    if (pTex)
      m_pDevice->TrackState(pTex->m_pResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  }
  m_Volumetric.pIntegrated = m_pUpload->CreateComputeVolume(nWidth, nHeight, VOLUME_SLICES, VOLUME_FORMAT, L"XRenderD3D12 volumetric light integrated");
  if (m_Volumetric.pIntegrated && pList)
  {
    m_pDevice->TrackState(m_Volumetric.pIntegrated->m_pResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
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
    m_pUpload->ReleaseTexture(m_Volumetric.pSunStates);
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
    if (m_Volumetric.nSunStatesSRV >= 0)
      m_pDescriptors->FreeSRV(m_Volumetric.nSunStatesSRV);
    if (m_Volumetric.nSunTable >= 0)
      m_pDescriptors->FreeRange(m_Volumetric.nSunTable, CD3D12Compute::COMPUTE_UAVS);
  }
  m_Volumetric = SVolumetricState();
}
