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

void CD3D12Renderer::MakeFrameConstants(SFrameConstants &fc) const
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
  const float w = (float)std::max(m_Viewport[2], 1), h = (float)std::max(m_Viewport[3], 1);
  Set4(fc.vScreen, w, h, 1 / w, 1 / h);
  Set4(fc.vTime, iTimer ? iTimer->GetCurrTime() : 0.0f, 0, 0, 0);
  Set4(fc.vWorldColor, m_WorldColor[0], m_WorldColor[1], m_WorldColor[2], m_WorldColor[3]);
  Set4(fc.vDepthParams, m_ProjMatrix.data[10], m_ProjMatrix.data[14], 0, 0);
}

void CD3D12Renderer::DrawGeometry(const SPipelineKey &Key, const SDrawConstants &dc, D3D_PRIMITIVE_TOPOLOGY eTopology, const D3D12_VERTEX_BUFFER_VIEW &VB,
  const D3D12_INDEX_BUFFER_VIEW *pIB, uint32 nCount, uint32 nFirst, int nBaseVertex, const D3D12_VERTEX_BUFFER_VIEW *pTexCoord1,
  const D3D12_VERTEX_BUFFER_VIEW *pTangents)
{
  if (!nCount || !m_pPipeline || !m_pUpload || !EnsureFrame())
    return;
  SPipelineKey FullKey = Key;
  FullKey.nTopology = (uint8)GetTopologyType(eTopology);
  FullKey.nStreams = (pTexCoord1 ? PIPELINE_STREAM_TEXCOORD1 : 0) | (pTangents ? PIPELINE_STREAM_TANGENTS : 0);
  ID3D12PipelineState *pPSO = m_pPipeline->GetPSO(FullKey);
  if (!pPSO)
    return;

  if (!m_nFrameCB)
  {
    SFrameConstants fc;
    MakeFrameConstants(fc);
    m_nFrameCB = UploadConstants(*m_pUpload, fc);
  }
  ID3D12GraphicsCommandList *pList = m_pDevice->GetCmdList();
  pList->SetPipelineState(pPSO);
  pList->SetGraphicsRootConstantBufferView(0, m_nFrameCB);
  pList->SetGraphicsRootConstantBufferView(1, UploadConstants(*m_pUpload, dc));
  pList->IASetVertexBuffers(0, 1, &VB);
  m_pPipeline->BindDefaultStream(pList);
  if (pTexCoord1)
    pList->IASetVertexBuffers(2, 1, pTexCoord1);
  if (pTangents)
    pList->IASetVertexBuffers(3, 1, pTangents);
  pList->IASetPrimitiveTopology(eTopology);

  // The viewport inside the back buffer (an off screen target: all of it, or m_TargetRect); the
  // scissor rectangle inside the viewport (the scissors are the back buffer's).
  const bool bOffscreen = m_pDevice->IsOffscreen();
  const int nBufW = m_pDevice->GetTargetWidth(), nBufH = m_pDevice->GetTargetHeight();
  int vx = std::clamp(m_Viewport[0], 0, nBufW), vy = std::clamp(m_Viewport[1], 0, nBufH);
  int vw = std::clamp(m_Viewport[2], 0, nBufW - vx), vh = std::clamp(m_Viewport[3], 0, nBufH - vy);
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
  pList->RSSetViewports(1, &Viewport);
  RECT Scissor = { vx, vy, vx + vw, vy + vh };
  if (m_bItemScissor && !bOffscreen)
  {
    Scissor.left = std::max(Scissor.left, m_ItemScissor.left);
    Scissor.top = std::max(Scissor.top, m_ItemScissor.top);
    Scissor.right = std::min(Scissor.right, m_ItemScissor.right);
    Scissor.bottom = std::min(Scissor.bottom, m_ItemScissor.bottom);
  }
  if (m_bScissor && !bOffscreen)
  {
    Scissor.left = std::max<LONG>(Scissor.left, m_Scissor[0]);
    Scissor.top = std::max<LONG>(Scissor.top, m_Scissor[1]);
    Scissor.right = std::min<LONG>(Scissor.right, m_Scissor[0] + m_Scissor[2]);
    Scissor.bottom = std::min<LONG>(Scissor.bottom, m_Scissor[1] + m_Scissor[3]);
  }
  if (Scissor.right <= Scissor.left || Scissor.bottom <= Scissor.top)
    return;
  pList->RSSetScissorRects(1, &Scissor);

  if (pIB)
  {
    pList->IASetIndexBuffer(pIB);
    pList->DrawIndexedInstanced(nCount, 1, nFirst, nBaseVertex, 0);
  }
  else
    pList->DrawInstanced(nCount, 1, nFirst, 0);
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
