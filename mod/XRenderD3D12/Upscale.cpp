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
//	stay at the display's size. An HDR scene (r_HDRRendering) gets there as
//	its light (d3d12_UpscaleHdr 1: Hdr.cpp's HdrSceneLight, through the
//	volumetric light, with the exposure), the bloom and the tone map after
//	it at the display's size; or tone mapped at its size (0).
//	The path tracer (d3d12_PathTracing) wants the motion vectors without an
//	upscaler too: with d3d12_Upscaler none it gets the scene targets of the
//	display's size, without jitter, and the picture is the scene's as it is
//	(CNativeScene). With d3d12_Upscaler none and d3d12_AA taa the upscaler
//	is TAA (UpscalerTaa.cpp), of the display's size, the path tracer's too.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "D3D12Device.h"
#include "D3D12Descriptors.h"
#include "D3D12Upload.h"
#include "Scene.h"
#include "CameraCut.h"

namespace
{

void Set4(float v[4], float x, float y, float z, float w)
{
  v[0] = x; v[1] = y; v[2] = z; v[3] = w;
}

//! A triangle over the whole target (clip space).
const float s_FullScreen[3][3] = { { -1, -1, 0 }, { -1, 3, 0 }, { 3, -1, 0 } };

const char *const s_ModeNames[eUM_Num] = { "Native AA", "Quality", "Balanced", "Performance", "Ultra Performance" };

//! The scene targets of the display's size for the path tracer without an upscaler: no jitter, and the
//! scene's picture as it is (Dispatch upscales nothing).
class CNativeScene final : public IUpscaler
{
public:
  bool Create(ID3D12Device *, int nWidth, int nHeight, bool, int, bool) override
  {
    m_nWidth = nWidth;
    m_nHeight = nHeight;
    return nWidth > 0 && nHeight > 0;
  }
  void GetRenderSize(int, int &nWidth, int &nHeight) override
  {
    nWidth = m_nWidth;
    nHeight = m_nHeight;
  }
  void GetJitter(int, int, float &x, float &y) override { x = y = 0; }
  bool Dispatch(const SUpscaleInputs &) override { return false; }
  const char *GetName() const override { return "none (the path tracer's scene targets)"; }
  bool GathersFrames() const override { return false; }

private:
  int m_nWidth = 0, m_nHeight = 0;
};

//! The d3d12_Upscaler the path tracer gets in place of none (CNativeScene).
const char *const NATIVE_SCENE = "native";
//! The one d3d12_AA taa gets in place of none (UpscalerTaa.cpp).
const char *const TAA_SCENE = "taa";

//! The upscaler of a d3d12_Upscaler value; nullptr for "none" and unknown names.
std::unique_ptr<IUpscaler> CreateUpscaler(const char *szName)
{
  if (!_stricmp(szName, "fsr"))
    return CreateUpscalerFsr();
  if (!_stricmp(szName, NATIVE_SCENE))
    return std::make_unique<CNativeScene>();
  if (!_stricmp(szName, TAA_SCENE))
    return CreateUpscalerTaa();
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
  // TAA without an upscaler (the path tracer gets its scene targets, jittered); else the path tracer's motion vectors.
  if (!_stricmp(szName, "none") && AntiAliasing() == EAntiAlias::Taa)
    szName = TAA_SCENE;
  if (!_stricmp(szName, "none") && WantsPathTracing())
    szName = NATIVE_SCENE;
  const int w = m_pDevice->GetWidth(), h = m_pDevice->GetHeight();
  const int nVersion = std::max(CV_d3d12_UpscaleVersion, 0);
  // An upscaler of the HDR scene's light; the path tracer's scene targets show the scene tone mapped as before. Its context is made
  // for its input (FSR's FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE): a change of r_HDRRendering or d3d12_UpscaleHdr makes it anew, its
  // history reset, as a change of the upscaler or of the display's size.
  const bool bHdrInput = m_bHdrScene && CV_d3d12_UpscaleHdr > 0 && _stricmp(szName, "none") != 0 && _stricmp(szName, NATIVE_SCENE) != 0;
  if (_stricmp(szName, m_UpscalerName.c_str()) != 0 || w != m_nUpscalerWidth || h != m_nUpscalerHeight || nVersion != m_nUpscalerVersion ||
      bHdrInput != m_bUpscalerHdrInput)
  {
    const bool bNew = _stricmp(szName, m_UpscalerName.c_str()) != 0 || nVersion != m_nUpscalerVersion;
    ReleaseUpscaler();
    m_UpscalerName = szName;
    m_nUpscalerWidth = w;
    m_nUpscalerHeight = h;
    m_nUpscalerVersion = nVersion;
    m_bUpscalerHdrInput = bHdrInput;
    m_pUpscaler = CreateUpscaler(szName);
    if (!m_pUpscaler)
    {
      if (bNew && _stricmp(szName, "none") != 0)
        iLog->LogWarning("XRenderD3D12: d3d12_Upscaler %s: no such upscaler (none, fsr); none", szName);
      return false;
    }
    if (!m_pUpscaler->Create(m_pDevice->GetDevice(), w, h, CV_d3d12_debug != 0, nVersion, bHdrInput))
    {
      m_pUpscaler.reset();
      if (bNew)
        iLog->LogWarning("XRenderD3D12: d3d12_Upscaler %s is not available; none", szName);
      return false;
    }
  }
  if (!m_pUpscaler)
    return false;

  // The scene targets of the mode, their colour HDR or not (the path tracer's and TAA's of the display's size).
  const bool bDisplaySize = !_stricmp(m_UpscalerName.c_str(), NATIVE_SCENE) || !_stricmp(m_UpscalerName.c_str(), TAA_SCENE);
  const int nMode = bDisplaySize ? (int)eUM_NativeAA : std::clamp(CV_d3d12_UpscaleMode, 0, eUM_Num - 1);
  if (nMode != m_nUpscaleMode || m_bHdrScene != m_bUpscaleHdr)
  {
    if (m_nUpscaleOutputSRV >= 0)
      m_pDescriptors->FreeSRV(m_nUpscaleOutputSRV);
    if (m_nUpscaleColorSRV >= 0)
      m_pDescriptors->FreeSRV(m_nUpscaleColorSRV);
    if (m_nUpscaleDepthSRV >= 0)
      m_pDescriptors->FreeSRV(m_nUpscaleDepthSRV);
    m_nUpscaleOutputSRV = m_nUpscaleColorSRV = m_nUpscaleDepthSRV = -1;
    int rw = w, rh = h;
    m_pUpscaler->GetRenderSize(nMode, rw, rh);
    if (!m_pDevice->SetSceneSize(rw, rh, m_bHdrScene ? D3D12_HDR_FORMAT : D3D12_BACKBUFFER_FORMAT,
                                 m_bUpscalerHdrInput ? D3D12_UPSCALE_HDR_FORMAT : D3D12_UPSCALE_FORMAT) ||
        (m_nUpscaleOutputSRV = m_pDescriptors->AllocSRV(m_pDevice->GetSceneOutput(), nullptr)) < 0 ||
        (m_nUpscaleColorSRV = m_pDescriptors->AllocSRV(m_pDevice->GetSceneColor(), nullptr)) < 0 ||
        (m_nUpscaleDepthSRV = m_pDescriptors->AllocSRV(m_pDevice->GetUpscaleDepth(), nullptr)) < 0)
    {
      iLog->LogError("XRenderD3D12: no scene targets of %d x %d for the upscaler; none", rw, rh);
      ReleaseUpscaler();
      return false;
    }
    // A new mode starts without history; the same with the colour HDR or not keeps it (the upscaler gets LDR colours both ways;
    // for its HDR light it is made anew, without history).
    if (nMode != m_nUpscaleMode)
      m_bUpscaleReset = true;
    m_nUpscaleMode = nMode;
    m_bUpscaleHdr = m_bHdrScene;
    iLog->Log("XRenderD3D12: %s, %s: %d x %d to %d x %d%s", m_pUpscaler->GetName(), s_ModeNames[nMode], rw, rh, w, h,
              m_bUpscalerHdrInput ? ", HDR: the scene's light, tone mapped after the upscale" : m_bHdrScene ? ", HDR: tone mapped before the upscale" : "");
  }
  return true;
}

CD3D12Renderer::SUpscaleInfo CD3D12Renderer::UpscaleInfo() const
{
  SUpscaleInfo Info;
  ICVar *pVar = m_pUpscalerVar ? m_pUpscalerVar : (iConsole ? iConsole->GetCVar("d3d12_Upscaler") : nullptr);
  if (pVar && pVar->GetString())
    Info.szWanted = pVar->GetString();
  // The path tracer's scene targets (CNativeScene) and TAA (d3d12_AA's) are not an upscaler for the statistics.
  if (m_pUpscaler && m_pUpscaler->GathersFrames() && !m_pUpscaler->IsAntiAliasing() && m_nUpscaleMode >= 0 && m_pDevice)
  {
    Info.szName = m_pUpscaler->GetName();
    Info.szMode = s_ModeNames[m_nUpscaleMode];
    Info.nRenderWidth = m_pDevice->GetSceneWidth();
    Info.nRenderHeight = m_pDevice->GetSceneHeight();
    // DrawStats runs before Update counts the frame.
    Info.bThisFrame = m_nUpscaledFrame == m_nFrames;
    Info.bHdr = m_bUpscalerHdrInput;
  }
  return Info;
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
    if (m_nUpscaleDepthSRV >= 0)
      m_pDescriptors->FreeSRV(m_nUpscaleDepthSRV);
  }
  m_nUpscaleOutputSRV = m_nUpscaleColorSRV = m_nUpscaleDepthSRV = -1;
  if (m_pDevice)
    m_pDevice->SetSceneSize(0, 0, D3D12_BACKBUFFER_FORMAT);
  m_pUpscaler.reset();
  ReleaseSceneLightTargets();
  m_nUpscaleMode = -1;
  m_bUpscaleHdr = false;
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

void CD3D12Renderer::NoteCameraCut()
{
  if (m_nCutCheckFrame == m_nFrames)
    return;
  // The direction the camera looks along: the view matrix's third column (the same sign every frame).
  const Vec3 vPos = m_Camera.GetPos();
  const float vP[3] = { vPos.x, vPos.y, vPos.z };
  float vD[3] = { m_CameraView.data[2], m_CameraView.data[6], m_CameraView.data[10] };
  const float fLength = sqrtf(vD[0] * vD[0] + vD[1] * vD[1] + vD[2] * vD[2]);
  for (float &f : vD)
    f /= std::max(fLength, 1e-6f);
  if (CV_d3d12_CutReset > 0 && m_nCutCheckFrame >= 0 && CameraCut::IsCut(m_vCutPos, m_vCutDir, vP, vD))
  {
    m_nCutFrame = m_nFrames;
    if (m_pFrameDump)
      fprintf(m_pFrameDump.get(), "camera cut (d3d12_CutReset): from (%.2f %.2f %.2f) looking (%.2f %.2f %.2f) to (%.2f %.2f %.2f) looking "
              "(%.2f %.2f %.2f); the histories start again\n", m_vCutPos[0], m_vCutPos[1], m_vCutPos[2], m_vCutDir[0], m_vCutDir[1],
              m_vCutDir[2], vP[0], vP[1], vP[2], vD[0], vD[1], vD[2]);
  }
  m_nCutCheckFrame = m_nFrames;
  memcpy(m_vCutPos, vP, sizeof(vP));
  memcpy(m_vCutDir, vD, sizeof(vD));
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
  // or of its field of view, a cut to another shot (d3d12_CutReset), a new mode.
  const Vec3 vPos = m_Camera.GetPos();
  const float fFovY = 2 * atanf(1 / std::max(m_ProjMatrix.data[5], 1e-6f));
  m_bUpscaleHistoryReset = m_bUpscaleReset || m_nUpscaledFrame != m_nFrames - 1 || (vPos - m_vPrevCameraPos).GetLength() > 5 ||
                           fabsf(fFovY - m_fPrevFovY) > DEG2RAD(10.0f) || IsCameraCut();
  if (m_bUpscaleHistoryReset)
  {
    m_PrevViewProj = m_CameraView * m_ProjMatrix;
    m_PrevSkyViewProj = GetSkyViewProj();
  }
  m_bUpscaling = true;
  UpdateReprojection();
  InvalidateFrameConstants();
  if (m_pFrameDump)
    fprintf(m_pFrameDump.get(), "upscaler: %s, %d x %d, jitter (%.3f %.3f)%s%s\n", m_pUpscaler->GetName(), m_pDevice->GetSceneWidth(),
            m_pDevice->GetSceneHeight(), m_UpscaleJitter[0], m_UpscaleJitter[1], m_bUpscaleHistoryReset ? ", reset" : "",
            m_bUpscalerHdrInput ? ", HDR input" : "");
}

void CD3D12Renderer::UpscaleScene()
{
  FUNCTION_PROFILER(iSystem, PROFILE_RENDERER);
  if (!m_bUpscaling)
    return;
  m_bUpscaling = false;
  InvalidateFrameConstants();
  m_pDevice->EndScene();

  // An HDR scene: its light for an upscaler made for it (d3d12_UpscaleHdr), through the volumetric light at the scene's depth (its
  // froxels are of the view without the jitter, many pixels each) with the exposure's factor, the meter from the scene as without
  // an upscaler; else tone mapped at its size first, the upscaler getting the LDR colours it is made for.
  const int rw = m_pDevice->GetSceneWidth(), rh = m_pDevice->GetSceneHeight();
  CD3D12Texture *pLdr = nullptr, *pLight = nullptr;
  SUpscaleInputs In;
  if (m_bHdrScene)
  {
    GpuSection("hdr");
    if (UpdateVolumetricLight(m_pDevice->GetSceneDepth(), DXGI_FORMAT_R32_FLOAT))
      GpuSection("hdr");
    // TAA's resolve makes the light itself (d3d12_AASceneLight): HdrSceneLight leaves it to it.
    if (m_bUpscalerHdrInput && EnsureSceneLightTargets(rw, rh) &&
        MeterHdr(m_nUpscaleColorSRV, m_pDevice->GetSceneColor(), rw, rh, m_pDevice->GetSceneDepth(), DXGI_FORMAT_R32_FLOAT, true))
      pLight = HdrSceneLight(m_nUpscaleColorSRV, rw, rh, m_pDevice->GetSceneDepth(), DXGI_FORMAT_R32_FLOAT,
                             m_pUpscaler->MakesSceneLight() ? &In : nullptr);
    if (!pLight)
      pLdr = ResolveHdr(m_nUpscaleColorSRV, m_pDevice->GetSceneColor(), rw, rh, true, m_pDevice->GetSceneDepth(), DXGI_FORMAT_R32_FLOAT);
    InvalidateFrameConstants();
  }
  // The pass by what runs in it: an upscaler's (FSR) upscale; TAA (d3d12_AA's, with d3d12_Upscaler none) the anti-aliasing at
  // the display's size, as DrawPostAA's methods; the path tracer's scene targets without either (CNativeScene) only their
  // picture drawn over the back buffer.
  GpuSection(m_pUpscaler->IsAntiAliasing() ? "anti-aliasing" : m_pUpscaler->GathersFrames() ? "upscale" : "scene copy");
  // The upscaler reads its inputs in a compute shader (FSR in exactly the state it is told: both shaders' reads) and writes
  // its output; the device keeps their states (the inputs stay readable by the pixels after).
  constexpr D3D12_RESOURCE_STATES READ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  // The light the resolve makes: from the scene, the volumetric light's froxels and depth as the pass would read them.
  CD3D12Texture *pIn = In.bSceneLight ? nullptr : pLight ? pLight : pLdr;
  if (pIn)
    m_pDevice->Require(pIn->m_pResource, READ);
  if (pLight)
    m_pDevice->Require(m_Hdr.pExposureScale->m_pResource, READ);
  if (In.bVolumetric && m_Volumetric.pIntegrated && m_Volumetric.pDepth)
  {
    m_pDevice->Require(m_Volumetric.pIntegrated->m_pResource, READ);
    m_pDevice->Require(m_Volumetric.pDepth.Get(), READ);
  }

  In.pCmdList = m_pDevice->GetCmdList();
  In.pColor = pIn ? pIn->m_pResource : m_pDevice->GetSceneColor();
  In.pExposure = pLight ? m_Hdr.pExposureScale->m_pResource : nullptr;
  In.pDepth = m_pDevice->GetUpscaleDepth();
  In.pVelocity = m_pDevice->GetSceneVelocity();
  In.pOutput = m_pDevice->GetSceneOutput();
  for (ID3D12Resource *pRes : { In.pColor, In.pDepth, In.pVelocity })
    m_pDevice->Require(pRes, READ);
  m_pDevice->Require(In.pOutput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  In.nRenderWidth = rw;
  In.nRenderHeight = rh;
  In.fJitterX = m_UpscaleJitter[0];
  In.fJitterY = m_UpscaleJitter[1];
  // The planes and the field of view of the projection (MakeProjection).
  const float *p = m_ProjMatrix.data;
  In.fNear = p[10] != 0 ? p[14] / p[10] : 0.25f;
  In.fFar = p[10] != -1 ? p[14] / (p[10] + 1) : 1000.0f;
  In.fFovY = 2 * atanf(1 / std::max(p[5], 1e-6f));
  In.fFrameMs = iTimer ? std::clamp(iTimer->GetFrameTime() * 1000.0f, 0.1f, 1000.0f) : 16.7f;
  In.fSharpness = std::clamp(CV_d3d12_UpscaleSharpness, 0.0f, 1.0f);
  In.nQuality = std::clamp(CV_d3d12_AAQuality, 0, 3);
  In.bReset = m_bUpscaleHistoryReset;
  // TAA's debug view is the anti-aliasing's (d3d12_AADebug).
  In.bDebugView = m_pUpscaler->IsAntiAliasing() ? CV_d3d12_AADebug != 0 : CV_d3d12_UpscaleDebug == 1;
  // A library's passes (FSR) after what the device queued.
  m_pDevice->FlushBarriers();
  const bool bUpscaled = m_pUpscaler->Dispatch(In);
  m_pDevice->BindRootSignature();
  // Not upscaled: the light tone mapped at the scene's size, shown stretched; still readable by the bloom's kernel
  // (DispatchBloomLevel reads its first level's source so).
  if (pLight && !bUpscaled)
  {
    // The light left to the resolve drawn after all.
    if (In.bSceneLight)
      DrawSceneLight();
    pLdr = ResolveHdr(pLight->m_nSRV, pLight->m_pResource, rw, rh, true, nullptr, DXGI_FORMAT_UNKNOWN, true);
  }

  const int w = m_pDevice->GetWidth(), h = m_pDevice->GetHeight();
  if (pLight && bUpscaled && CV_d3d12_UpscaleDebug != 2)
  {
    // The light upscaled: the bloom, the tone map, the colour grading and the highlight layer at the display's size, into the
    // back buffer. The upscaler's debug view (d3d12_UpscaleDebug 1) too: with the light its picture is of the light, exposed and
    // tone mapped as the frame.
    GpuSection("hdr");
    m_pDevice->Require(In.pOutput, READ);
    ResolveHdr(m_nUpscaleOutputSRV, In.pOutput, w, h, false, nullptr, DXGI_FORMAT_UNKNOWN, true);
  }
  else
  {
    // Its picture over the whole back buffer (else the scene stretched; with d3d12_UpscaleDebug 2
    // the depth it got).
    int Viewport[4];
    memcpy(Viewport, m_Viewport, sizeof(Viewport));
    const bool bScissor = m_bScissor;
    m_Viewport[0] = m_Viewport[1] = 0;
    m_Viewport[2] = w;
    m_Viewport[3] = h;
    m_bScissor = m_bItemScissor = false;
    if (bUpscaled)
      m_pDevice->Require(In.pOutput, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    SPipelineKey Key = {};
    Key.nProgram = eD3D12P_Post;
    Key.nState = GS_NODEPTHTEST;
    Key.nCull = R_CULL_NONE;
    SDrawConstants dc = DefaultDrawConstants();
    dc.nFlags = POST_BLEND;
    dc.nTex0 = (uint32)(bUpscaled ? m_nUpscaleOutputSRV : pLdr ? pLdr->m_nSRV : m_nUpscaleColorSRV);
    Set4(dc.vParams0, 1.0f / w, 1.0f / h, 0, 0);
    Set4(dc.vParams1, 1, 1, 1, 1);
    if (CV_d3d12_UpscaleDebug == 2)
    {
      dc.nFlags = POST_DEPTH_VIEW;
      dc.nTex0 = (uint32)m_nUpscaleDepthSRV;
      Set4(dc.vParams1, In.fNear, In.fFar, 0, 0);
    }
    // With the highlight layer (HdrOutput.cpp) it gets the resolve's ratio, upscaled bilinearly (none
    // for the depth view).
    const bool bHighlight = m_pDevice->HasLayer();
    if (bHighlight)
    {
      dc.nTex1 = pLdr && m_Hdr.pRatio && CV_d3d12_UpscaleDebug != 2 ? (uint32)m_Hdr.pRatio->m_nSRV : D3D12_SRV_WHITE;
      Set4(dc.vParams2, std::clamp(CV_d3d12_ToneMapKnee, 0.0f, 0.99f), 0, 0, 0);
      Key.nTarget = 4;
      m_pDevice->BindHighlight(true);
    }
    DrawImmediate(Key, dc, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, VERTEX_FORMAT_P3F, s_FullScreen, 3);
    if (bHighlight)
      m_pDevice->BindHighlight(false);
    memcpy(m_Viewport, Viewport, sizeof(Viewport));
    m_bScissor = bScissor;
  }
  // The depth buffer cleared for what is drawn in 3D after the scene.
  m_pDevice->ClearDepth(1.0f, 0, true, true);
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
