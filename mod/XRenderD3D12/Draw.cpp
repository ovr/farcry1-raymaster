// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Draw.cpp
//	Description: The draw call every part of the renderer goes through:
//	pipeline state, constants, vertex and index buffers, viewport and
//	scissor.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "D3D12Device.h"
#include "D3D12Upload.h"
#include "D3D12Descriptors.h"
#include "RayTracing.h"

namespace
{

D3D12_PRIMITIVE_TOPOLOGY_TYPE GetTopologyType(D3D_PRIMITIVE_TOPOLOGY eTopology)
{
  switch (eTopology)
  {
  case D3D_PRIMITIVE_TOPOLOGY_POINTLIST:
    return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
  case D3D_PRIMITIVE_TOPOLOGY_LINELIST:
  case D3D_PRIMITIVE_TOPOLOGY_LINESTRIP:
    return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
  default:
    return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  }
}

//! The triangles a draw of nCount vertices or indices makes (GetPolyCount).
int CountTriangles(D3D_PRIMITIVE_TOPOLOGY eTopology, uint32 nCount)
{
  switch (eTopology)
  {
  case D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
    return (int)(nCount / 3);
  case D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
    return nCount > 2 ? (int)(nCount - 2) : 0;
  default:
    return 0;
  }
}

//! A copy of the constants in this frame's memory.
template<class T> D3D12_GPU_VIRTUAL_ADDRESS UploadConstants(CD3D12Upload &Upload, const T &Data)
{
  const uint32 nSize = (sizeof(T) + 255) & ~255u;
  const SD3D12FrameAlloc Mem = Upload.AllocFrame(nSize, 256);
  memcpy(Mem.pCPU, &Data, sizeof(T));
  return Mem.nGPU;
}

void SetIdentity(float m[16])
{
  memset(m, 0, 16 * sizeof(float));
  m[0] = m[5] = m[10] = m[15] = 1;
}

void Set4(float v[4], float x, float y, float z, float w)
{
  v[0] = x; v[1] = y; v[2] = z; v[3] = w;
}

}

// HLSL packs a matrix of a constant buffer from a 16 byte boundary.
static_assert(offsetof(SDrawConstants, mParams0) % 16 == 0, "SDrawConstants::mParams0 must match the HLSL layout");

SDrawConstants CD3D12Renderer::DefaultDrawConstants()
{
  SDrawConstants dc = {};
  SetIdentity(dc.mWorld);
  SetIdentity(dc.mWorldViewProj);
  Set4(dc.vColor, 1, 1, 1, 1);
  Set4(dc.vTexMat0, 1, 0, 0, 0);
  Set4(dc.vTexMat1, 0, 1, 0, 0);
  dc.nTex0 = dc.nTex1 = D3D12_SRV_WHITE;
  return dc;
}

Matrix44 CD3D12Renderer::MakeOrtho(float w, float h)
{
  // x: 0..w -> -1..1, y: 0..h -> 1..-1, z kept (0..1).
  Matrix44 m;
  memset(m.data, 0, sizeof(m.data));
  m.data[0] = 2.0f / std::max(w, 1.0f);
  m.data[5] = -2.0f / std::max(h, 1.0f);
  m.data[10] = 1;
  m.data[12] = -1;
  m.data[13] = 1;
  m.data[15] = 1;
  return m;
}

void CD3D12Renderer::MakeFrameConstants(SFrameConstants &fc, bool bScene) const
{
  fc = {};
  const Matrix44 ViewProj = m_CameraView * m_ProjMatrix;
  memcpy(fc.mViewProj, ViewProj.data, sizeof(fc.mViewProj));
  memcpy(fc.mView, m_CameraView.data, sizeof(fc.mView));
  const Vec3 vPos = m_Camera.GetPos();
  Set4(fc.vCameraPos, vPos.x, vPos.y, vPos.z, 1);
  float fLen = sqrtf(m_SunDir[0] * m_SunDir[0] + m_SunDir[1] * m_SunDir[1] + m_SunDir[2] * m_SunDir[2]);
  if (fLen <= 0)
    fLen = 1;
  Set4(fc.vSunDir, m_SunDir[0] / fLen, m_SunDir[1] / fLen, m_SunDir[2] / fLen, 0);
  Set4(fc.vSunColor, m_SunColor[0], m_SunColor[1], m_SunColor[2], 1);
  Set4(fc.vSunSpec, m_SunSpec[0], m_SunSpec[1], m_SunSpec[2], 1);
  Set4(fc.vFogColor, m_FogColor[0], m_FogColor[1], m_FogColor[2], 1);
  const float fRange = m_fFogEnd - m_fFogStart;
  Set4(fc.vFogParams, m_fFogStart, m_fFogEnd, fRange > 0 ? 1.0f / fRange : 0.0f, m_bFog ? 1.0f : 0.0f);
  // In an upscaler's scene: its pixels, the jitter of the frame (FSR's pixels to clip space) and
  // the mip bias for the frames it gathers.
  float sx = 1, sy = 1;
  if (bScene)
    GetSceneScale(sx, sy);
  const float w = (float)std::max(m_Viewport[2], 1) * sx, h = (float)std::max(m_Viewport[3], 1) * sy;
  Set4(fc.vScreen, w, h, 1 / w, 1 / h);
  Set4(fc.vJitter, bScene ? 2 * m_UpscaleJitter[0] / w : 0.0f, bScene ? -2 * m_UpscaleJitter[1] / h : 0.0f, sx, sy);
  fc.vMipBias[0] = bScene ? log2f(std::max(sx, 1e-3f)) - 1 : 0.0f;
  memcpy(&fc.vMipBias[1], m_AOBakedShare, sizeof(m_AOBakedShare));
  Set4(fc.vTime, iTimer ? iTimer->GetCurrTime() : 0.0f, 0, 0, 0);
  Set4(fc.vWorldColor, m_WorldColor[0], m_WorldColor[1], m_WorldColor[2], m_WorldColor[3]);
  Set4(fc.vDepthParams, m_ProjMatrix.data[10], m_ProjMatrix.data[14], 0, 0);
  Set4(fc.vHdr, HdrSkyGlow(), 0, 0, 0);
  SetCascadeConstants(fc);
  const float fFoliageEnd = std::max(CV_d3d12_FoliageTranslucencyDistance, 1.0f);
  Set4(fc.vFoliage, std::max(CV_d3d12_FoliageTranslucency, 0.0f), std::max(CV_d3d12_FoliageTranslucencyPower, 0.1f), fFoliageEnd, 2 / fFoliageEnd);
}

void CD3D12Renderer::DrawGeometry(const SPipelineKey &Key, const SDrawConstants &dc, D3D_PRIMITIVE_TOPOLOGY eTopology, const D3D12_VERTEX_BUFFER_VIEW &VB,
  const D3D12_INDEX_BUFFER_VIEW *pIB, uint32 nCount, uint32 nFirst, int nBaseVertex, const D3D12_VERTEX_BUFFER_VIEW *pTexCoord1,
  const D3D12_VERTEX_BUFFER_VIEW *pTangents, uint32 nInstances)
{
  if (!nCount || !nInstances || !m_pPipeline || !m_pUpload || !EnsureFrame())
    return;
  // The main view's opaque list set up for the scene for rays before it is drawn (BuildRayScene).
  if (m_bRayCaptureWalk)
  {
    if (m_pRayScene && m_pRayScene->IsCapturing())
      m_pRayScene->CaptureDraw(Key, dc, eTopology, VB, pIB, nCount, nFirst, nBaseVertex, pTexCoord1);
    return;
  }
  // The depth prepass (DrawItems): the general program's draws that write the depth with LESS_EQUAL
  // and discard nothing.
  if (m_bDepthPrepass && (Key.nProgram != eD3D12P_General || Key.nTarget != 0 || Key.bWireframe || !(Key.nState & GS_DEPTHWRITE) ||
                          (Key.nState & (GS_BLSRC_MASK | GS_BLDST_MASK | GS_NODEPTHTEST | GS_DEPTHFUNC_EQUAL | GS_DEPTHFUNC_GREAT)) ||
                          (dc.nFlags & DF_ALPHATEST)))
    return;
  const bool bOffscreen = m_pDevice->IsOffscreen();
  const bool bScene = m_pDevice->IsSceneBound();
  // The water reflection's draws for the scene for rays (RayTracing.h).
  if (m_pRayScene && !m_bDepthPrepass && m_pRayScene->IsCapturingReflection())
    m_pRayScene->CaptureDraw(Key, dc, eTopology, VB, pIB, nCount, nFirst, nBaseVertex, pTexCoord1);
  // A program with rays needs this frame's scene (the list after its builds from here on); the water
  // goes without them when there is none.
  const bool bRays = Key.nProgram == eD3D12P_RayDebug || (Key.nProgram == eD3D12P_Water && Key.bRayTrace);
  const D3D12_GPU_VIRTUAL_ADDRESS nRayScene = bRays ? UseRayScene() : 0;
  if (Key.nProgram == eD3D12P_RayDebug && !nRayScene)
    return;
  SPipelineKey FullKey = Key;
  FullKey.bRayTrace = nRayScene ? Key.bRayTrace : 0;
  FullKey.nTopology = (uint8)GetTopologyType(eTopology);
  FullKey.nStreams = (pTexCoord1 ? PIPELINE_STREAM_TEXCOORD1 : 0) | (pTangents ? PIPELINE_STREAM_TANGENTS : 0);
  if (bScene && FullKey.nTarget == 0)
    FullKey.nTarget = 3;
  // The target in its format: the HDR target, an upscaler's HDR scene, the water's reflection and
  // the environment cubes with r_HDRRendering.
  if ((FullKey.nTarget == 0 || FullKey.nTarget == 3) && m_pDevice->GetTargetColorFormat() == D3D12_HDR_FORMAT)
    FullKey.nColorFormat = PIPELINE_COLOR_HDR;
  // The sun's cascades in the general program's pixel shader that has them; the leaves letting the sun
  // through in their own.
  FullKey.bCascades = 0;
  if (Key.nProgram == eD3D12P_General)
    FullKey.bCascades = (dc.nFlags2 & DF2_FOLIAGE) ? 2 : (dc.nFlags2 & (DF2_SUN_CASCADES | DF2_CASCADE_CASTER)) ? 1 : 0;
  // A shadow map's caster without the alpha test or the live cascades' comparison with the full set
  // (the general program's discards): without a pixel shader. One that can discard keeps the depth
  // test and write after it for all its pixels.
  // The depth prepass: the depth alone too.
  FullKey.bDepthOnly = (FullKey.nTarget == 2 && Key.nProgram == eD3D12P_General && !(dc.nFlags & DF_ALPHATEST) &&
                        !(dc.nFlags2 & (DF2_CASCADE_CASTER | DF2_SCOPE_LENS))) || m_bDepthPrepass ? 1 : 0;
  // The ambient occlusion's target bound to the main view (AmbientOcclusion.cpp).
  FullKey.bAmbient = (FullKey.nTarget == 0 || FullKey.nTarget == 3) && m_pDevice->IsAmbientBound() ? 1 : 0;
  ID3D12PipelineState *pPSO = m_pPipeline->GetPSO(FullKey);
  if (!pPSO)
    return;

  const bool bHdrTarget = FullKey.nColorFormat == PIPELINE_COLOR_HDR;
  if (!m_nFrameCB || m_bFrameCBScene != bScene || m_bFrameCBHdr != bHdrTarget)
  {
    SFrameConstants fc;
    MakeFrameConstants(fc, bScene);
    m_nFrameCB = UploadConstants(*m_pUpload, fc);
    m_bFrameCBScene = bScene;
    m_bFrameCBHdr = bHdrTarget;
  }
  // Heat vision: the main view's scene through HeatVision (Common.hlsli), not the passes multiplied
  // in or the fog volumes; an additive draw other than a light pass is hot (fire, muzzle flashes).
  const SDrawConstants *pDC = &dc;
  SDrawConstants LocalDC;
  if (m_bHeatPass && (Key.nProgram == eD3D12P_General || Key.nProgram == eD3D12P_Water) &&
      !(dc.nFlags & (DF_DETAIL_LAYER | DF_CAUSTICS | DF_SHADOW_PASS | DF_SCREEN_POS)) && !(dc.nFlags2 & DF2_FOG_VOLUME))
  {
    const bool bAdditive = (Key.nState & GS_BLDST_MASK) == GS_BLDST_ONE;
    LocalDC = dc;
    LocalDC.nFlags2 |= DF2_HEAT;
    Set4(LocalDC.vHeat, bAdditive && !(dc.nFlags2 & DF2_NO_SUN) ? 1.0f : m_fDrawHeat, bAdditive ? 1.0f : 0.0f, 0, 0);
    pDC = &LocalDC;
  }
  // The motion vectors of a draw that gave none: still on the screen.
  if (bScene && std::all_of(std::begin(pDC->mPrevWorldViewProj), std::end(pDC->mPrevWorldViewProj), [](float f) { return f == 0; }))
  {
    if (pDC != &LocalDC)
    {
      LocalDC = dc;
      pDC = &LocalDC;
    }
    memcpy(LocalDC.mPrevWorldViewProj, LocalDC.mWorldViewProj, sizeof(LocalDC.mPrevWorldViewProj));
  }
  // The state the draw before set stays while nothing else touched the command list.
  ID3D12GraphicsCommandList *pList = m_pDevice->GetDrawCmdList();
  SDrawState &Set = m_DrawState;
  if (Set.nEpoch != m_pDevice->GetStateEpoch())
  {
    Set = {};
    Set.nEpoch = m_pDevice->GetStateEpoch();
  }
  if (pPSO != Set.pPSO)
  {
    pList->SetPipelineState(pPSO);
    Set.pPSO = pPSO;
  }
  m_nFramePSOChanges += pPSO != m_pLastPSO;
  m_pLastPSO = pPSO;
  if (m_nFrameCB != Set.nFrameCB)
  {
    pList->SetGraphicsRootConstantBufferView(0, m_nFrameCB);
    Set.nFrameCB = m_nFrameCB;
  }
  pList->SetGraphicsRootConstantBufferView(1, UploadConstants(*m_pUpload, *pDC));
  if (nRayScene)
  {
    pList->SetGraphicsRootShaderResourceView(ROOT_RAY_SCENE, nRayScene);
    pList->SetGraphicsRootShaderResourceView(ROOT_RAY_INSTANCES, m_pRayScene->GetInstances());
  }
  const auto SetVB = [&](UINT nSlot, const D3D12_VERTEX_BUFFER_VIEW &View)
  {
    if (memcmp(&View, &Set.VB[nSlot], sizeof(View)))
    {
      pList->IASetVertexBuffers(nSlot, 1, &View);
      Set.VB[nSlot] = View;
    }
  };
  SetVB(0, VB);
  if (!Set.bDefaultStream)
  {
    m_pPipeline->BindDefaultStream(pList);
    Set.bDefaultStream = true;
  }
  if (pTexCoord1)
    SetVB(2, *pTexCoord1);
  if (pTangents)
    SetVB(3, *pTangents);
  if (eTopology != Set.eTopology)
  {
    pList->IASetPrimitiveTopology(eTopology);
    Set.eTopology = eTopology;
  }

  // The viewport inside the back buffer (an off screen target: all of it, or m_TargetRect); the
  // scissor rectangle inside the viewport (the scissors are the back buffer's). In an upscaler's
  // scene both are scaled from the back buffer's pixels to the scene's.
  const int nBufW = m_pDevice->GetTargetWidth(), nBufH = m_pDevice->GetTargetHeight();
  float sx = 1, sy = 1;
  if (bScene)
    GetSceneScale(sx, sy);
  const auto X = [sx](int x) { return sx == 1 ? x : (int)lroundf(x * sx); };
  const auto Y = [sy](int y) { return sy == 1 ? y : (int)lroundf(y * sy); };
  const int x0 = X(m_Viewport[0]), y0 = Y(m_Viewport[1]);
  int vx = std::clamp(x0, 0, nBufW), vy = std::clamp(y0, 0, nBufH);
  int vw = std::clamp(X(m_Viewport[0] + m_Viewport[2]) - x0, 0, nBufW - vx), vh = std::clamp(Y(m_Viewport[1] + m_Viewport[3]) - y0, 0, nBufH - vy);
  if (bOffscreen && m_bTargetRect)
  {
    vx = std::clamp((int)m_TargetRect.left, 0, nBufW);
    vy = std::clamp((int)m_TargetRect.top, 0, nBufH);
    vw = std::clamp((int)(m_TargetRect.right - m_TargetRect.left), 0, nBufW - vx);
    vh = std::clamp((int)(m_TargetRect.bottom - m_TargetRect.top), 0, nBufH - vy);
  }
  else if (bOffscreen)
  {
    vx = vy = 0;
    vw = nBufW;
    vh = nBufH;
  }
  if (vw <= 0 || vh <= 0)
    return;
  const D3D12_VIEWPORT Viewport = { (float)vx, (float)vy, (float)vw, (float)vh, m_fDepthMin, m_fDepthMax };
  if (memcmp(&Viewport, &Set.Viewport, sizeof(Viewport)))
  {
    pList->RSSetViewports(1, &Viewport);
    Set.Viewport = Viewport;
  }
  RECT Scissor = { vx, vy, vx + vw, vy + vh };
  if (m_bItemScissor && !bOffscreen)
  {
    Scissor.left = std::max<LONG>(Scissor.left, X(m_ItemScissor.left));
    Scissor.top = std::max<LONG>(Scissor.top, Y(m_ItemScissor.top));
    Scissor.right = std::min<LONG>(Scissor.right, X(m_ItemScissor.right));
    Scissor.bottom = std::min<LONG>(Scissor.bottom, Y(m_ItemScissor.bottom));
  }
  if (m_bScissor && !bOffscreen)
  {
    Scissor.left = std::max<LONG>(Scissor.left, X(m_Scissor[0]));
    Scissor.top = std::max<LONG>(Scissor.top, Y(m_Scissor[1]));
    Scissor.right = std::min<LONG>(Scissor.right, X(m_Scissor[0] + m_Scissor[2]));
    Scissor.bottom = std::min<LONG>(Scissor.bottom, Y(m_Scissor[1] + m_Scissor[3]));
  }
  if (Scissor.right <= Scissor.left || Scissor.bottom <= Scissor.top)
    return;
  if (memcmp(&Scissor, &Set.Scissor, sizeof(Scissor)))
  {
    pList->RSSetScissorRects(1, &Scissor);
    Set.Scissor = Scissor;
  }

  m_nFramePolygons += CountTriangles(eTopology, nCount) * nInstances;
  m_nFrameDraws++;
  if (pIB)
  {
    if (memcmp(pIB, &Set.IB, sizeof(*pIB)))
    {
      pList->IASetIndexBuffer(pIB);
      Set.IB = *pIB;
    }
    pList->DrawIndexedInstanced(nCount, nInstances, nFirst, nBaseVertex, 0);
  }
  else
    pList->DrawInstanced(nCount, nInstances, nFirst, 0);
}

void CD3D12Renderer::DrawImmediate(const SPipelineKey &Key, const SDrawConstants &dc, D3D_PRIMITIVE_TOPOLOGY eTopology, int nVertexFormat,
  const void *pVerts, int nVerts, const ushort *pInds, int nInds)
{
  if (!pVerts || nVerts <= 0 || !m_pUpload || !EnsureFrame())
    return;
  const int nStride = CD3D12Pipeline::GetVertexSize(nVertexFormat);
  if (nStride <= 0)
    return;
  const uint32 nVBSize = (uint32)nStride * nVerts;
  const SD3D12FrameAlloc VBMem = m_pUpload->AllocFrame(nVBSize, 16);
  memcpy(VBMem.pCPU, pVerts, nVBSize);
  const D3D12_VERTEX_BUFFER_VIEW VB = { VBMem.nGPU, nVBSize, (UINT)nStride };

  SPipelineKey FullKey = Key;
  FullKey.nVertexFormat = (uint8)nVertexFormat;
  if (pInds && nInds > 0)
  {
    const uint32 nIBSize = (uint32)nInds * sizeof(ushort);
    const SD3D12FrameAlloc IBMem = m_pUpload->AllocFrame((nIBSize + 3) & ~3u, 4);
    memcpy(IBMem.pCPU, pInds, nIBSize);
    const D3D12_INDEX_BUFFER_VIEW IB = { IBMem.nGPU, nIBSize, DXGI_FORMAT_R16_UINT };
    DrawGeometry(FullKey, dc, eTopology, VB, &IB, nInds);
  }
  else
    DrawGeometry(FullKey, dc, eTopology, VB, nullptr, nVerts);
}
