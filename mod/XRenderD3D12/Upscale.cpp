// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Upscale.cpp
//	Description: The upscaler of the main view (d3d12_Upscaler, Upscaler.h).
//
//	With one, the main view's 3D scene (BeginScene3D .. the end of its
//	EF_EndEf3D) draws into the device's scene targets of the render size of
//	d3d12_UpscaleMode: the engine's viewport and scissors are scaled to it
//	(DrawGeometry), every clip position is jittered (SFrameConstants::
//	vJitter), the textures are sampled sharper (vMipBias), and the general
//	program's opaque draws write motion vectors from mPrevWorldViewProj:
//	the camera of the frame before (static geometry), or where a character
//	or a permanent object was then. UpscaleScene makes the picture of the
//	back buffer's size from it, before the screen effects and the 2D, which
//	stay at the display's size.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "D3D12Device.h"
#include "D3D12Descriptors.h"
#include "Scene.h"

namespace
{

void Set4(float v[4], float x, float y, float z, float w)
{
  v[0] = x; v[1] = y; v[2] = z; v[3] = w;
}

//! A triangle over the whole target (clip space).
const float s_FullScreen[3][3] = { { -1, -1, 0 }, { -1, 3, 0 }, { 3, -1, 0 } };

const char *const s_ModeNames[eUM_Num] = { "Native AA", "Quality", "Balanced", "Performance", "Ultra Performance" };

//! The upscaler of a d3d12_Upscaler value; nullptr for "none" and unknown names.
std::unique_ptr<IUpscaler> CreateUpscaler(const char *szName)
{
  if (!_stricmp(szName, "fsr"))
    return CreateUpscalerFsr();
  return nullptr;
}

//! The inverse of a 4x4 matrix (Gauss-Jordan with partial pivoting); false when it is singular.
bool Invert(const double m[16], double inv[16])
{
  double a[4][8];
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++)
    {
      a[r][c] = m[r * 4 + c];
      a[r][c + 4] = r == c ? 1 : 0;
    }
  for (int c = 0; c < 4; c++)
  {
    int nPivot = c;
    for (int r = c + 1; r < 4; r++)
      if (fabs(a[r][c]) > fabs(a[nPivot][c]))
        nPivot = r;
    if (fabs(a[nPivot][c]) < 1e-300)
      return false;
    if (nPivot != c)
      for (int k = 0; k < 8; k++)
        std::swap(a[c][k], a[nPivot][k]);
    const double f = 1 / a[c][c];
    for (int k = 0; k < 8; k++)
      a[c][k] *= f;
    for (int r = 0; r < 4; r++)
    {
      if (r == c || a[r][c] == 0)
        continue;
      const double g = a[r][c];
      for (int k = 0; k < 8; k++)
        a[r][k] -= g * a[c][k];
    }
  }
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++)
      inv[r * 4 + c] = a[r][c + 4];
  return true;
}

}

bool CD3D12Renderer::UpdateUpscaler()
{
  if (!m_pDevice || !m_pDescriptors)
    return false;
  if (!m_pUpscalerVar)
    m_pUpscalerVar = iConsole->GetCVar("d3d12_Upscaler");
  const char *szName = m_pUpscalerVar && m_pUpscalerVar->GetString() ? m_pUpscalerVar->GetString() : "none";
  const int w = m_pDevice->GetWidth(), h = m_pDevice->GetHeight();
  if (_stricmp(szName, m_UpscalerName.c_str()) != 0 || w != m_nUpscalerWidth || h != m_nUpscalerHeight)
  {
    const bool bNew = _stricmp(szName, m_UpscalerName.c_str()) != 0;
    ReleaseUpscaler();
    m_UpscalerName = szName;
    m_nUpscalerWidth = w;
    m_nUpscalerHeight = h;
    m_pUpscaler = CreateUpscaler(szName);
    if (!m_pUpscaler)
    {
      if (bNew && _stricmp(szName, "none") != 0)
        iLog->LogWarning("XRenderD3D12: d3d12_Upscaler %s: no such upscaler (none, fsr); none", szName);
      return false;
    }
    if (!m_pUpscaler->Create(m_pDevice->GetDevice(), w, h, CV_d3d12_debug != 0))
    {
      m_pUpscaler.reset();
      if (bNew)
        iLog->LogWarning("XRenderD3D12: d3d12_Upscaler %s is not available; none", szName);
      return false;
    }
  }
  if (!m_pUpscaler)
    return false;

  // The scene targets of the mode.
  const int nMode = std::clamp(CV_d3d12_UpscaleMode, 0, eUM_Num - 1);
  if (nMode != m_nUpscaleMode)
  {
    if (m_nUpscaleOutputSRV >= 0)
      m_pDescriptors->FreeSRV(m_nUpscaleOutputSRV);
    if (m_nUpscaleColorSRV >= 0)
      m_pDescriptors->FreeSRV(m_nUpscaleColorSRV);
    m_nUpscaleOutputSRV = m_nUpscaleColorSRV = -1;
    int rw = w, rh = h;
    m_pUpscaler->GetRenderSize(nMode, rw, rh);
    if (!m_pDevice->SetSceneSize(rw, rh) || (m_nUpscaleOutputSRV = m_pDescriptors->AllocSRV(m_pDevice->GetSceneOutput(), nullptr)) < 0 ||
        (m_nUpscaleColorSRV = m_pDescriptors->AllocSRV(m_pDevice->GetSceneColor(), nullptr)) < 0)
    {
      iLog->LogError("XRenderD3D12: no scene targets of %d x %d for the upscaler; none", rw, rh);
      ReleaseUpscaler();
      return false;
    }
    m_nUpscaleMode = nMode;
    m_bUpscaleReset = true;
    iLog->Log("XRenderD3D12: %s, %s: %d x %d to %d x %d", m_pUpscaler->GetName(), s_ModeNames[nMode], rw, rh, w, h);
  }
  return true;
}

void CD3D12Renderer::ReleaseUpscaler()
{
  // The context may be in use by the frames in flight.
  if (m_pUpscaler && m_pDevice)
    m_pDevice->WaitForGPU();
  if (m_pDescriptors)
  {
    if (m_nUpscaleOutputSRV >= 0)
      m_pDescriptors->FreeSRV(m_nUpscaleOutputSRV);
    if (m_nUpscaleColorSRV >= 0)
      m_pDescriptors->FreeSRV(m_nUpscaleColorSRV);
  }
  m_nUpscaleOutputSRV = m_nUpscaleColorSRV = -1;
  if (m_pDevice)
    m_pDevice->SetSceneSize(0, 0);
  m_pUpscaler.reset();
  m_nUpscaleMode = -1;
  m_bUpscaling = false;
  m_bUpscaleReset = true;
  m_ObjectMotion.clear();
}

Matrix44 CD3D12Renderer::GetSkyViewProj() const
{
  Matrix44 View = m_CameraView;
  View.data[12] = View.data[13] = View.data[14] = 0;
  return View * m_SkyProjMatrix;
}

void CD3D12Renderer::UpdateReprojection()
{
  // The inverse of this frame's view * projection, then the frame before's: in double, as the
  // projection's far plane makes the inverse lose precision in float.
  const Matrix44 ViewProj = m_CameraView * m_ProjMatrix;
  double m[16], inv[16];
  for (int i = 0; i < 16; i++)
    m[i] = ViewProj.data[i];
  if (!Invert(m, inv))
  {
    m_Reproject.SetIdentity();
    return;
  }
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++)
    {
      double d = 0;
      for (int k = 0; k < 4; k++)
        d += inv[r * 4 + k] * m_PrevViewProj.data[k * 4 + c];
      m_Reproject.data[r * 4 + c] = (float)d;
    }
}

void CD3D12Renderer::BeginUpscale()
{
  if (m_bUpscaling || !UpdateUpscaler())
    return;
  m_pDevice->BeginScene();
  if (!m_pDevice->IsInScene())
    return;
  m_pDevice->ClearVelocity();
  m_pUpscaler->GetJitter(m_nUpscaleFrames++, m_pDevice->GetSceneWidth(), m_UpscaleJitter[0], m_UpscaleJitter[1]);

  // No history after a frame without this view (the menu, a level load), a jump of the camera
  // or of its field of view, a new mode.
  const Vec3 vPos = m_Camera.GetPos();
  const float fFovY = 2 * atanf(1 / std::max(m_ProjMatrix.data[5], 1e-6f));
  m_bUpscaleHistoryReset = m_bUpscaleReset || m_nUpscaledFrame != m_nFrames - 1 || (vPos - m_vPrevCameraPos).GetLength() > 5 ||
                           fabsf(fFovY - m_fPrevFovY) > DEG2RAD(10.0f);
  if (m_bUpscaleHistoryReset)
  {
    m_PrevViewProj = m_CameraView * m_ProjMatrix;
    m_PrevSkyViewProj = GetSkyViewProj();
  }
  m_bUpscaling = true;
  UpdateReprojection();
  InvalidateFrameConstants();
  if (m_pFrameDump)
    fprintf(m_pFrameDump.get(), "upscaler: %s, %d x %d, jitter (%.3f %.3f)%s\n", m_pUpscaler->GetName(), m_pDevice->GetSceneWidth(),
            m_pDevice->GetSceneHeight(), m_UpscaleJitter[0], m_UpscaleJitter[1], m_bUpscaleHistoryReset ? ", reset" : "");
}

void CD3D12Renderer::UpscaleScene()
{
  if (!m_bUpscaling)
    return;
  m_bUpscaling = false;
  InvalidateFrameConstants();
  GpuSection("upscale");
  m_pDevice->EndScene();

  SUpscaleInputs In;
  In.pCmdList = m_pDevice->GetCmdList();
  In.pColor = m_pDevice->GetSceneColor();
  In.pDepth = m_pDevice->GetSceneDepth();
  In.pVelocity = m_pDevice->GetSceneVelocity();
  In.pOutput = m_pDevice->GetSceneOutput();
  In.nRenderWidth = m_pDevice->GetSceneWidth();
  In.nRenderHeight = m_pDevice->GetSceneHeight();
  In.fJitterX = m_UpscaleJitter[0];
  In.fJitterY = m_UpscaleJitter[1];
  // The planes and the field of view of the projection (MakeProjection).
  const float *p = m_ProjMatrix.data;
  In.fNear = p[10] != 0 ? p[14] / p[10] : 0.25f;
  In.fFar = p[10] != -1 ? p[14] / (p[10] + 1) : 1000.0f;
  In.fFovY = 2 * atanf(1 / std::max(p[5], 1e-6f));
  In.fFrameMs = iTimer ? std::clamp(iTimer->GetFrameTime() * 1000.0f, 0.1f, 1000.0f) : 16.7f;
  In.fSharpness = std::clamp(CV_d3d12_UpscaleSharpness, 0.0f, 1.0f);
  In.bReset = m_bUpscaleHistoryReset;
  In.bDebugView = CV_d3d12_UpscaleDebug != 0;
  const bool bUpscaled = m_pUpscaler->Dispatch(In);
  m_pDevice->BindRootSignature();

  // Its picture over the whole back buffer (else the scene stretched), then the depth buffer
  // cleared for what is drawn in 3D after the scene.
  const int w = m_pDevice->GetWidth(), h = m_pDevice->GetHeight();
  int Viewport[4];
  memcpy(Viewport, m_Viewport, sizeof(Viewport));
  const bool bScissor = m_bScissor;
  m_Viewport[0] = m_Viewport[1] = 0;
  m_Viewport[2] = w;
  m_Viewport[3] = h;
  m_bScissor = m_bItemScissor = false;
  if (bUpscaled)
    m_pDevice->Transition(In.pOutput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  SPipelineKey Key = {};
  Key.nProgram = eD3D12P_Post;
  Key.nState = GS_NODEPTHTEST;
  Key.nCull = R_CULL_NONE;
  SDrawConstants dc = DefaultDrawConstants();
  dc.nFlags = POST_BLEND;
  dc.nTex0 = (uint32)(bUpscaled ? m_nUpscaleOutputSRV : m_nUpscaleColorSRV);
  Set4(dc.vParams0, 1.0f / w, 1.0f / h, 0, 0);
  Set4(dc.vParams1, 1, 1, 1, 1);
  DrawImmediate(Key, dc, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, VERTEX_FORMAT_P3F, s_FullScreen, 3);
  if (bUpscaled)
    m_pDevice->Transition(In.pOutput, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  m_pDevice->ClearDepth(1.0f, 0, true, true);
  memcpy(m_Viewport, Viewport, sizeof(Viewport));
  m_bScissor = bScissor;
  InvalidateFrameConstants();

  // This frame's camera for the next one; the objects not seen for a while go.
  m_PrevViewProj = m_CameraView * m_ProjMatrix;
  m_PrevSkyViewProj = GetSkyViewProj();
  m_vPrevCameraPos = m_Camera.GetPos();
  m_fPrevFovY = In.fFovY;
  m_nUpscaledFrame = m_nFrames;
  m_bUpscaleReset = false;
  if ((m_nFrames & 255) == 0)
    std::erase_if(m_ObjectMotion, [this](const auto &e) { return e.second.nFrame < m_nFrames - 2; });
}

void CD3D12Renderer::GetSceneScale(float &sx, float &sy) const
{
  sx = sy = 1;
  if (m_pDevice && m_pDevice->IsSceneBound())
  {
    sx = (float)m_pDevice->GetSceneWidth() / std::max(m_pDevice->GetWidth(), 1);
    sy = (float)m_pDevice->GetSceneHeight() / std::max(m_pDevice->GetHeight(), 1);
  }
}

void CD3D12Renderer::SetStaticMotion(SDrawConstants &dc) const
{
  if (!m_bUpscaling)
    return;
  Matrix44 Cur;
  memcpy(Cur.data, dc.mWorldViewProj, sizeof(Cur.data));
  const Matrix44 Prev = Cur * m_Reproject;
  memcpy(dc.mPrevWorldViewProj, Prev.data, sizeof(dc.mPrevWorldViewProj));
}

void CD3D12Renderer::SetObjectMotion(SDrawConstants &dc, const CCObject *pObj, const Matrix44 &World, bool bNearest)
{
  if (!m_bUpscaling || !m_pDevice->IsSceneBound())
    return;
  if (bNearest)
  {
    memcpy(dc.mPrevWorldViewProj, dc.mWorldViewProj, sizeof(dc.mPrevWorldViewProj));
    return;
  }
  // A character is the same instance from frame to frame, a permanent object the same object;
  // the temporary ones are handed out anew every frame.
  const void *pKey = pObj->m_pCharInstance ? (const void *)pObj->m_pCharInstance : m_pScene->IsPermanent(pObj) ? (const void *)pObj : nullptr;
  if (pKey)
  {
    SObjectMotion &m = m_ObjectMotion[pKey];
    if (m.nFrame != m_nFrames)
    {
      m.bPrev = m.nFrame >= 0 && m.nFrame == m_nUpscaledFrame;
      m.Prev = m.Cur;
      m.Cur = World;
      m.nFrame = m_nFrames;
    }
    if (m.bPrev)
    {
      const Matrix44 Prev = m.Prev * m_PrevViewProj;
      memcpy(dc.mPrevWorldViewProj, Prev.data, sizeof(dc.mPrevWorldViewProj));
      return;
    }
  }
  SetStaticMotion(dc);
}

void CD3D12Renderer::ForgetObjectMotion(const CCObject *pObj)
{
  m_ObjectMotion.erase(pObj);
}
