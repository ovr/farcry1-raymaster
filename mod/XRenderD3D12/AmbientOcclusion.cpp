// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: AmbientOcclusion.cpp
//	Description: The ambient occlusion of the main view (d3d12_AO,
//	AmbientOcclusion.h). After the main view's opaque list (CScene::
//	EndEf3D), not in its reflection, sprites, shadow maps, cubes or heat
//	vision:
//	  1. the depth buffer goes to DEPTH_READ | NON_PIXEL_SHADER_RESOURCE
//	  2. the backend writes the visibility (m_pAOOutput)
//	  3. AoResolve.hlsl makes it the factor (m_pAOFactor): the visibility
//	     to the power d3d12_AOStrength / 2.2, faded out with the fog, 1 on
//	     the sky
//	  4. the scene drawn so far is multiplied by it (POST_BLEND, DESTCOLOR
//	     x 0, the colour only: the alpha the screen effects read stays)
//	With d3d12_AOApply 1 it darkens the ambient light alone, not the sun's
//	and the lamps' direct light. The opaque list draws with the ambient
//	light's target bound (BeginAOTarget, m_pAOAmbient): General.hlsl's
//	XR_AMBIENT writes there how much of each colour is the ambient light's,
//	fogged, and for the terrain and the vegetation, whose light is baked
//	into them, the share of it the shadow leaves (m_AOBakedShare: the
//	colour of the engine's shadows on the terrain in this frame). Then 3.
//	the resolve makes the light taken away (m_pAOTaken): that times 1 - the
//	visibility to the power d3d12_AOStrength, 0 on the sky, and 4. it is
//	subtracted from the scene (ONE x ONE, REV_SUBTRACT).
//	The first person objects, the water, the particles and the rest of the
//	blended list are drawn after it and get none; the water's refraction
//	copies the scene after it, so it shows the occlusion under the surface.
//	With an upscaler it works at the render size on the scene's depth, and
//	the noise moves every frame for the upscaler to gather.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "D3D12Device.h"
#include "D3D12Descriptors.h"
#include "D3D12Upload.h"
#include "D3D12Compute.h"
#include "Scene.h"

namespace
{

void Set4(float v[4], float x, float y, float z, float w)
{
  v[0] = x; v[1] = y; v[2] = z; v[3] = w;
}

//! A triangle over the whole target (clip space).
const float s_FullScreen[3][3] = { { -1, -1, 0 }, { -1, 3, 0 }, { 3, -1, 0 } };

constexpr int TABLE = CD3D12Compute::COMPUTE_SRVS + CD3D12Compute::COMPUTE_UAVS;  //!< the resolve's tables: its SRVs, then its UAVs
constexpr D3D12_RESOURCE_STATES DEPTH_READ = D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
constexpr D3D12_RESOURCE_STATES AMBIENT_READ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
constexpr DXGI_FORMAT TAKEN_FORMAT = DXGI_FORMAT_R16G16B16A16_FLOAT;

//! The backend of a d3d12_AO value; nullptr for "none" and unknown names.
std::unique_ptr<IAmbientOcclusion> CreateAO(const char *szName)
{
  if (!_stricmp(szName, "gtao") || !_stricmp(szName, "xegtao"))
    return CreateAOXeGtao();
  if (!_stricmp(szName, "cacao"))
    return CreateAOCacao();
  return nullptr;
}

}

bool CD3D12Renderer::IsAOView()
{
  return (m_nSceneFlags & SHDF_ALLOWHDR) && m_nRecurseLevel <= 1 && !m_bReflectionPass && !m_bSpritePass && !m_bShadowMapPass && !m_bEnvCubePass &&
         !m_bCascadePass && !m_bHeatVision && m_pDevice && EnsureFrame() && !m_pDevice->IsOffscreen();
}

bool CD3D12Renderer::UpdateAO()
{
  if (!m_pDevice || !m_pDescriptors || !m_pUpload || !m_pCompute)
    return false;
  if (!m_pAOVar)
    m_pAOVar = iConsole->GetCVar("d3d12_AO");
  const char *szName = m_pAOVar && m_pAOVar->GetString() ? m_pAOVar->GetString() : "none";
  ID3D12Resource *pDepth = m_pDevice->GetMainDepth();
  const int w = m_pDevice->GetMainWidth(), h = m_pDevice->GetMainHeight();
  const int nApply = CV_d3d12_AOApply ? 1 : 0;
  const bool bNew = _stricmp(szName, m_AOName.c_str()) != 0;
  if (!bNew && pDepth == m_pAODepth.Get() && w == m_nAOWidth && h == m_nAOHeight && nApply == m_nAOApply)
    return m_pAO != nullptr;

  // Made again for the new name, depth buffer, size or application; kept as it is when that fails,
  // until one of them changes.
  ReleaseAO();
  m_AOName = szName;
  m_pAODepth = pDepth;
  m_nAOWidth = w;
  m_nAOHeight = h;
  m_nAOApply = nApply;
  if (!_stricmp(szName, "none"))
    return false;
  m_pAO = CreateAO(szName);
  if (!m_pAO)
  {
    if (bNew)
      iLog->LogWarning("XRenderD3D12: d3d12_AO %s: no such ambient occlusion (none, gtao, cacao); none", szName);
    return false;
  }
  m_pAOOutput = m_pUpload->CreateComputeTarget(w, h, 1, DXGI_FORMAT_R8_TYPELESS, L"AO visibility");
  m_pAOFactor = m_pUpload->CreateComputeTarget(w, h, 1, DXGI_FORMAT_R8G8B8A8_UNORM, L"AO factor");
  if (m_pAOFactor)
    m_pAOFactor->m_nSRV = m_pDescriptors->AllocSRV(m_pAOFactor->m_pResource, nullptr);
  if (nApply)
  {
    m_pAOAmbient = m_pUpload->CreateRenderTarget(w, h, D3D12_AMBIENT_FORMAT);
    m_pAOTaken = m_pUpload->CreateComputeTarget(w, h, 1, TAKEN_FORMAT, L"AO light taken");
    if (m_pAOTaken)
      m_pAOTaken->m_nSRV = m_pDescriptors->AllocSRV(m_pAOTaken->m_pResource, nullptr);
  }
  m_nAOResolveTable = m_pDescriptors->AllocRange(TABLE);
  SAOTargets Targets;
  Targets.pDepth = pDepth;
  Targets.DepthFormat = m_pDevice->GetMainDepthSRVFormat();
  Targets.pOutput = m_pAOOutput ? m_pAOOutput->m_pResource : nullptr;
  Targets.nWidth = w;
  Targets.nHeight = h;
  const bool bApply = !nApply || (m_pAOAmbient && m_pAOAmbient->m_nSRV >= 0 && m_pAOTaken && m_pAOTaken->m_nSRV >= 0);
  if (!pDepth || !m_pAOOutput || !m_pAOFactor || m_pAOFactor->m_nSRV < 0 || !bApply || m_nAOResolveTable < 0 ||
      !m_pAO->Create(m_pDevice->GetDevice(), Targets, CV_d3d12_debug != 0))
  {
    if (bNew)
      iLog->LogWarning("XRenderD3D12: d3d12_AO %s is not available at %d x %d; none", szName, w, h);
    ReleaseAO(false);
    return false;
  }

  // The resolve's tables: t0 the visibility, t1 the depth buffer, t2 the ambient light; u0 the
  // factor, u1 the light taken.
  D3D12_SHADER_RESOURCE_VIEW_DESC SRV = {};
  SRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  SRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  SRV.Texture2D.MipLevels = 1;
  SRV.Format = DXGI_FORMAT_R8_UNORM;
  m_pDescriptors->WriteSRV(m_nAOResolveTable, m_pAOOutput->m_pResource, &SRV);
  SRV.Format = Targets.DepthFormat;
  m_pDescriptors->WriteSRV(m_nAOResolveTable + 1, pDepth, &SRV);
  SRV.Format = D3D12_AMBIENT_FORMAT;
  m_pDescriptors->WriteSRV(m_nAOResolveTable + 2, m_pAOAmbient ? m_pAOAmbient->m_pResource : nullptr, &SRV);
  D3D12_UNORDERED_ACCESS_VIEW_DESC UAV = {};
  UAV.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  for (int i = 0; i < CD3D12Compute::COMPUTE_UAVS; i++)
  {
    UAV.Format = i == 1 ? TAKEN_FORMAT : DXGI_FORMAT_R8G8B8A8_UNORM;
    ID3D12Resource *pUAV = i == 0 ? m_pAOFactor->m_pResource : i == 1 && m_pAOTaken ? m_pAOTaken->m_pResource : nullptr;
    m_pDescriptors->WriteUAV(m_nAOResolveTable + CD3D12Compute::COMPUTE_SRVS + i, pUAV, &UAV);
  }
  // Made in PIXEL_SHADER_RESOURCE (called in a frame).
  if (m_pAOAmbient)
    m_pDevice->Transition(m_pAOAmbient->m_pResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, AMBIENT_READ);
  iLog->Log("XRenderD3D12: ambient occlusion %s, %d x %d%s", m_pAO->GetName(), w, h, nApply ? ", on the ambient light" : "");
  return true;
}

void CD3D12Renderer::ReleaseAO(bool bForget)
{
  // A backend's own objects may be in use by the frames in flight.
  if (m_pAO && m_pDevice)
    m_pDevice->WaitForGPU();
  m_pAO.reset();
  if (m_pUpload)
  {
    for (CD3D12Texture *pTex : { m_pAOOutput, m_pAOFactor, m_pAOAmbient, m_pAOTaken })
      if (pTex)
        m_pUpload->ReleaseTexture(pTex);
  }
  m_pAOOutput = m_pAOFactor = m_pAOAmbient = m_pAOTaken = nullptr;
  if (m_nAOResolveTable >= 0 && m_pDescriptors)
    m_pDescriptors->FreeRange(m_nAOResolveTable, TABLE);
  m_nAOResolveTable = -1;
  m_nAODrawnFrame = -1;
  if (bForget)
  {
    m_AOName.clear();
    m_pAODepth.Reset();
    m_nAOWidth = m_nAOHeight = 0;
    m_nAOApply = 0;
  }
}

void CD3D12Renderer::BeginAOTarget(const SRenderItem *pItems, int nItems)
{
  if (!CV_d3d12_AOApply || m_bAOAmbientBound || !IsAOView() || !UpdateAO() || !m_pAOAmbient)
    return;
  // The share of the ambient light in a baked light: the terrain in the shadow over the terrain in
  // the light, the colour of the engine's shadows on it (the mean of their objects'), else the
  // console's.
  float Share[3] = {};
  int nShare = 0;
  for (int i = 0; i < nItems; i++)
  {
    if (pItems[i].eDrawClass != eDC_TerrainShadow || !pItems[i].pObj)
      continue;
    const CFColor &c = pItems[i].pObj->m_Color;
    Share[0] += std::clamp(c.r, 0.0f, 1.0f);
    Share[1] += std::clamp(c.g, 0.0f, 1.0f);
    Share[2] += std::clamp(c.b, 0.0f, 1.0f);
    nShare++;
  }
  for (int i = 0; i < 3; i++)
    m_AOBakedShare[i] = nShare ? Share[i] / nShare : std::clamp(CV_d3d12_AOBakedShare, 0.0f, 1.0f);
  InvalidateFrameConstants();

  static const float Zero[4] = {};
  m_pDevice->Transition(m_pAOAmbient->m_pResource, AMBIENT_READ, D3D12_RESOURCE_STATE_RENDER_TARGET);
  m_pDevice->GetCmdList()->ClearRenderTargetView(m_pAOAmbient->m_RTV, Zero, 0, nullptr);
  m_pDevice->BindAmbient(m_pAOAmbient->m_RTV);
  m_bAOAmbientBound = true;
}

void CD3D12Renderer::DrawAmbientOcclusion()
{
  FUNCTION_PROFILER(iSystem, PROFILE_RENDERER);
  // The ambient light's target unbound, whatever comes next; readable by the resolve, then a shader
  // resource again.
  ID3D12Resource *pAmbient = nullptr;
  if (m_bAOAmbientBound)
  {
    m_bAOAmbientBound = false;
    pAmbient = m_pAOAmbient->m_pResource;
    m_pDevice->BindAmbient({});
    m_pDevice->Transition(pAmbient, D3D12_RESOURCE_STATE_RENDER_TARGET, AMBIENT_READ);
  }
  // With d3d12_AOApply 1 only over an opaque list that wrote the ambient light (made for it already).
  if (!IsAOView() || (pAmbient ? !m_pAO : (!UpdateAO() || m_nAOApply)))
    return;
  GpuSection("ao");
  ID3D12GraphicsCommandList *pList = m_pDevice->GetCmdList();
  ID3D12Resource *pDepth = m_pAODepth.Get();
  const int w = m_nAOWidth, h = m_nAOHeight;
  m_pDevice->Transition(pDepth, D3D12_RESOURCE_STATE_DEPTH_WRITE, DEPTH_READ);

  SAOFrame Frame;
  Frame.pCmdList = pList;
  memcpy(Frame.Proj, m_ProjMatrix.data, sizeof(Frame.Proj));
  Frame.fRadius = std::max(CV_d3d12_AORadius, 0.01f);
  Frame.nQuality = std::clamp(CV_d3d12_AOQuality, 0, 3);
  Frame.nDenoise = CV_d3d12_AODenoise;
  Frame.nNoiseIndex = m_bUpscaling ? m_nAOFrames % 64 : 0;
  // Beyond the fog's end nothing is left to see of it; without fog, the far half of the view.
  const float *p = m_ProjMatrix.data;
  const float fFar = p[10] != -1 ? p[14] / (p[10] + 1) : 1000.0f;
  Frame.fFadeStart = m_bFog && m_fFogEnd > m_fFogStart ? m_fFogEnd : fFar * 0.5f;
  Frame.fFadeEnd = std::max(m_bFog && m_fFogEnd > m_fFogStart ? m_fFogEnd * 1.1f : fFar, Frame.fFadeStart + 1);
  m_nAOFrames++;
  const bool bOk = m_pAO->Dispatch(Frame);
  // A backend may have bound a heap of its own.
  m_pDevice->BindRootSignature();
  if (m_pFrameDump)
    fprintf(m_pFrameDump.get(), "ao: %s, %d x %d, quality %d, radius %g, strength %g, denoise %d, noise %d, apply %d, baked share %.3f %.3f %.3f%s\n",
            m_pAO->GetName(), w, h, Frame.nQuality, Frame.fRadius, CV_d3d12_AOStrength, Frame.nDenoise, Frame.nNoiseIndex, m_nAOApply,
            m_AOBakedShare[0], m_AOBakedShare[1], m_AOBakedShare[2], bOk ? "" : ", failed");
  CD3D12Texture *pApplied = m_nAOApply ? m_pAOTaken : m_pAOFactor;
  if (bOk)
  {
    m_pDevice->Transition(m_pAOOutput->m_pResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    SAoResolveConstants rc = {};
    Set4(rc.vSize, (float)w, (float)h, 1.0f / w, 1.0f / h);
    Set4(rc.vDepthParams, p[10], p[14], 0, 0);
    const float fRange = m_fFogEnd - m_fFogStart;
    Set4(rc.vFogParams, m_fFogStart, m_fFogEnd, fRange > 0 ? 1.0f / fRange : 0.0f, m_bFog ? 1.0f : 0.0f);
    // Taken from the ambient light: in the units the game adds its light in.
    const float fStrength = std::max(CV_d3d12_AOStrength, 0.0f);
    Set4(rc.vParams, m_nAOApply ? fStrength : fStrength / 2.2f, m_nAOApply ? 1.0f : 0.0f, m_nAOApply && CV_d3d12_AODebug == 1 ? 1.0f : 0.0f, 0);
    const SD3D12FrameAlloc CB = m_pUpload->AllocFrame(sizeof(rc), 256);
    memcpy(CB.pCPU, &rc, sizeof(rc));
    m_pCompute->Dispatch(pList, eD3D12K_AoResolve, CB.nGPU, m_nAOResolveTable, m_nAOResolveTable + CD3D12Compute::COMPUTE_SRVS,
                         (UINT)(w + 7) / 8, (UINT)(h + 7) / 8);
    m_pDevice->Transition(m_pAOOutput->m_pResource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  }
  m_pDevice->Transition(pDepth, DEPTH_READ, D3D12_RESOURCE_STATE_DEPTH_WRITE);
  InvalidateFrameConstants();
  if (!bOk)
    return;

  // The scene times the factor, or less the light taken, over the whole target (the engine's
  // viewport may be smaller).
  int Viewport[4];
  memcpy(Viewport, m_Viewport, sizeof(Viewport));
  const bool bScissor = m_bScissor, bItemScissor = m_bItemScissor;
  m_Viewport[0] = m_Viewport[1] = 0;
  m_Viewport[2] = m_pDevice->GetWidth();
  m_Viewport[3] = m_pDevice->GetHeight();
  m_bScissor = m_bItemScissor = false;
  m_pDevice->Transition(pApplied->m_pResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  SPipelineKey Key = {};
  Key.nProgram = eD3D12P_Post;
  Key.nState = GS_NODEPTHTEST | GS_COLMASKONLYRGB | (m_nAOApply ? GS_BLSRC_ONE | GS_BLDST_ONE : GS_BLSRC_DSTCOL | GS_BLDST_ZERO);
  Key.nCull = R_CULL_NONE;
  Key.nBlendOp = m_nAOApply ? 1 : 0;
  SDrawConstants dc = DefaultDrawConstants();
  dc.nFlags = POST_BLEND;
  dc.nTex0 = (uint32)pApplied->m_nSRV;
  Set4(dc.vParams0, 1.0f / w, 1.0f / h, 0, 0);
  Set4(dc.vParams1, 1, 1, 1, 1);
  DrawImmediate(Key, dc, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, VERTEX_FORMAT_P3F, s_FullScreen, 3);
  m_pDevice->Transition(pApplied->m_pResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  memcpy(m_Viewport, Viewport, sizeof(Viewport));
  m_bScissor = bScissor;
  m_bItemScissor = bItemScissor;
  m_nAODrawnFrame = m_nFrames;
}

void CD3D12Renderer::DrawAODebug()
{
  if (!CV_d3d12_AODebug || m_nAODrawnFrame != m_nFrames || !m_pAOFactor || m_pAOFactor->m_nSRV < 0 || !m_pDevice || m_pDevice->IsOffscreen())
    return;
  // The factor, the ambient light or the light taken, stretched over the whole back buffer.
  CD3D12Texture *pShow = m_nAOApply && CV_d3d12_AODebug == 2 ? m_pAOAmbient : m_nAOApply && CV_d3d12_AODebug == 3 ? m_pAOTaken : m_pAOFactor;
  const bool bCompute = pShow != m_pAOAmbient;
  const int w = m_pDevice->GetWidth(), h = m_pDevice->GetHeight();
  int Viewport[4];
  memcpy(Viewport, m_Viewport, sizeof(Viewport));
  const bool bScissor = m_bScissor, bItemScissor = m_bItemScissor;
  m_Viewport[0] = m_Viewport[1] = 0;
  m_Viewport[2] = w;
  m_Viewport[3] = h;
  m_bScissor = m_bItemScissor = false;
  if (bCompute)
    m_pDevice->Transition(pShow->m_pResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  SPipelineKey Key = {};
  Key.nProgram = eD3D12P_Post;
  Key.nState = GS_NODEPTHTEST;
  Key.nCull = R_CULL_NONE;
  SDrawConstants dc = DefaultDrawConstants();
  dc.nFlags = POST_BLEND;
  dc.nTex0 = (uint32)pShow->m_nSRV;
  Set4(dc.vParams0, 1.0f / w, 1.0f / h, 0, 0);
  Set4(dc.vParams1, 1, 1, 1, 1);
  DrawImmediate(Key, dc, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, VERTEX_FORMAT_P3F, s_FullScreen, 3);
  if (bCompute)
    m_pDevice->Transition(pShow->m_pResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  memcpy(m_Viewport, Viewport, sizeof(Viewport));
  m_bScissor = bScissor;
  m_bItemScissor = bItemScissor;
}
