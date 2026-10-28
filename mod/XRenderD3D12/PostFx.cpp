//////////////////////////////////////////////////////////////////////
//
//	File: PostFx.cpp
//	Description: The screen effects of the screen effect elements
//	(CREScreenProcess, CScene::GetScreenFx), drawn over the main view's 3D
//	scene when it is finished, before the 2D of the frame (the HUD stays as
//	it is). The post program (Shaders/Post.hlsl) does the work:
//	  - ScreenFade: the scene darkened by the fade's alpha.
//	  - ScreenBlur: the scene loses sharpness for the luminance of its blur
//	    tinted in the effect's colour, by the effect's amount.
//	  - NightVision: the luminance of the scene and the glow of its blur,
//	    with grain, from dark cyan to white.
//	  - FlashBang: white fading with the part of the flash still to come
//	    over an afterimage (the blurred scene at the start of the flash) and
//	    a bright spot at the flash's position (FlashPos, FlashSize: the
//	    virtual 800 x 600 screen) that shrinks as it fades.
//	The blurs: the copy of the back buffer reduced by a whole factor, then a
//	gaussian across and down. The glow (night vision, the afterimage) is
//	reduced to about POST_BLUR_ROWS rows and blurred by 2 of its texels, so
//	it looks the same at every resolution; the screen blur's radius grows
//	slower than the screen (ScreenBlurSigma).
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "D3D12Device.h"
#include "D3D12Upload.h"
#include "D3D12Pipeline.h"
#include "Scene.h"

namespace
{

//! The rows of the glow blur's targets: the back buffer's height over a whole factor, about this much.
constexpr int POST_BLUR_ROWS = 192;
//! The glow blur's gaussian, in its texels.
constexpr float POST_GLOW_SIGMA = 2;

void Set4(float v[4], float x, float y, float z, float w)
{
  v[0] = x; v[1] = y; v[2] = z; v[3] = w;
}

//! A triangle over the whole target (clip space).
const float s_FullScreen[3][3] = { { -1, -1, 0 }, { -1, 3, 0 }, { 3, -1, 0 } };

//! The screen blur's gaussian in pixels of a back buffer nHeight rows high: a part fixed in
//! pixels and a part of the screen, 5.5 at 768 rows, 10 at 2160.
float ScreenBlurSigma(int nHeight)
{
  const float fScreen = nHeight / 242.0f;
  return sqrtf(4.49f * 4.49f + fScreen * fScreen);
}

//! The flash bang's look by t, the part of the flash still to come (1 to 0): the scene's share,
//! the afterimage's, the white added and the spot's strength. White to t 0.74; the spot is
//! saturated far from its centre at first and shrinks as it fades.
struct SFlashLook
{
  float fScene, fGhost, fWhite, fSpot;
};

SFlashLook FlashLook(float t)
{
  t = std::clamp(t, 0.0f, 1.0f);
  SFlashLook Look;
  Look.fScene = 1 - 0.5f * t;
  Look.fGhost = 2.9f * t + 1.3f * t * t;
  Look.fWhite = 0.95f * t * t + std::clamp((t - 0.68f) * 10, 0.0f, 1.0f);
  Look.fSpot = 70 * powf(std::clamp((t - 0.1f) * 2.5f, 0.0f, 1.0f), 1.5f);
  return Look;
}

}

void CD3D12Renderer::DrawScreenEffects()
{
  // The main view of the game only (not the scenes drawn into textures).
  if (!m_pScene || !(m_nSceneFlags & SHDF_ALLOWHDR) || m_nRecurseLevel > 1 || m_bReflectionPass || m_bSpritePass ||
      m_bShadowMapPass || !EnsureFrame())
    return;
  const SScreenFxState fx = m_pScene->GetScreenFx();
  const bool bBlur = fx.bScreenBlur && fx.fBlurAmount > 0;
  const bool bFade = fx.bScreenFade && fx.fFadeAlpha > 0;
  if (!fx.bFlashBang)
    m_bFlashGhost = false;
  if (!fx.bNightVision && !bBlur && !fx.bFlashBang && !bFade)
    return;
  if (m_pFrameDump)
  {
    fprintf(m_pFrameDump.get(), "screen effects: night vision %d, blur %g (%.2f %.2f %.2f), flash bang %d (%g), fade %g\n",
            fx.bNightVision, bBlur ? fx.fBlurAmount : 0.0f, fx.fBlurColor[0], fx.fBlurColor[1], fx.fBlurColor[2], fx.bFlashBang,
            fx.fFlashBang, bFade ? fx.fFadeAlpha : 0.0f);
  }
  GpuSection("screen effects");
  if (!CopySceneForWater())
    return;
  const int w = m_pDevice->GetWidth(), h = m_pDevice->GetHeight();

  // The glow blur; at the start of a flash, it becomes the afterimage.
  const bool bCapture = fx.bFlashBang && !m_bFlashGhost;
  bool bGlow = false, bSoft = false;
  if ((fx.bNightVision || bCapture) && EnsurePostBlur(m_PostGlow, std::max((h + POST_BLUR_ROWS / 2) / POST_BLUR_ROWS, 1)))
  {
    DrawPostBlur(m_PostGlow, POST_GLOW_SIGMA);
    bGlow = true;
    if (bCapture)
    {
      CD3D12Texture *pGlow = m_PostGlow.pTex[0];
      if (m_pPostGhost && (m_pPostGhost->m_nWidth != pGlow->m_nWidth || m_pPostGhost->m_nHeight != pGlow->m_nHeight))
      {
        m_pUpload->ReleaseTexture(m_pPostGhost);
        m_pPostGhost = nullptr;
      }
      if (!m_pPostGhost)
        m_pPostGhost = m_pUpload->CreateRenderTarget(pGlow->m_nWidth, pGlow->m_nHeight, DXGI_FORMAT_R8G8B8A8_UNORM);
      if (m_pPostGhost && m_pPostGhost->m_nSRV >= 0)
      {
        SDrawConstants dc = DefaultDrawConstants();
        dc.nFlags = POST_DOWNSAMPLE;
        dc.nTex0 = (uint32)pGlow->m_nSRV;
        Set4(dc.vParams0, 1.0f / pGlow->m_nWidth, 1.0f / pGlow->m_nHeight, 1, 0);
        DrawPostPass(m_pPostGhost, dc);
        m_bFlashGhost = true;
      }
    }
  }
  // The screen blur's own: a factor that leaves about 2 texels of its gaussian.
  if (bBlur)
  {
    const float fSigma = ScreenBlurSigma(h);
    const int nFactor = std::max((int)lroundf(fSigma / 2), 1);
    if (EnsurePostBlur(m_PostSoft, nFactor))
    {
      // the reduction is a box of nFactor pixels: its variance is part of the gaussian's
      const float fBox = (nFactor * nFactor - 1) / 12.0f;
      DrawPostBlur(m_PostSoft, sqrtf(std::max(fSigma * fSigma - fBox, 0.25f)) / nFactor);
      bSoft = true;
    }
  }
  if (bGlow || bSoft || bCapture)
    m_pDevice->SetBackBufferTargets();

  SPipelineKey Key = {};
  Key.nProgram = eD3D12P_Post;
  Key.nState = GS_NODEPTHTEST;
  Key.nCull = R_CULL_NONE;
  SDrawConstants dc = DefaultDrawConstants();
  dc.nFlags = POST_EFFECTS;
  dc.nTex0 = (uint32)m_nSceneColorSRV;
  dc.nTex1 = bGlow ? (uint32)m_PostGlow.pTex[0]->m_nSRV : (uint32)m_nSceneColorSRV;
  dc.nTex2 = m_pPostGhost && m_bFlashGhost ? (uint32)m_pPostGhost->m_nSRV : dc.nTex1;
  dc.nTex3 = bSoft ? (uint32)m_PostSoft.pTex[0]->m_nSRV : (uint32)m_nSceneColorSRV;
  Set4(dc.vParams0, 1.0f / w, 1.0f / h, bFade ? fx.fFadeAlpha : 0.0f, (float)(m_nFrames & 0xFFFF));
  if (fx.bNightVision && bGlow)
    dc.nFlags |= POST_NIGHTVISION;
  if (bSoft)
  {
    dc.nFlags |= POST_SCREENBLUR;
    Set4(dc.vParams1, fx.fBlurColor[0], fx.fBlurColor[1], fx.fBlurColor[2], std::min(fx.fBlurAmount, 1.0f));
  }
  if (fx.bFlashBang && m_bFlashGhost)
  {
    dc.nFlags |= POST_FLASHBANG;
    const SFlashLook Look = FlashLook(fx.fFlashBang);
    Set4(dc.vParams2, Look.fScene, Look.fGhost, Look.fWhite, Look.fSpot);
    // The spot: centred on FlashPos, FlashSize across (virtual 800 x 600 screen).
    const float rx = std::max(fx.fFlashSize[0], 1.0f) * 0.5f / 800, ry = std::max(fx.fFlashSize[1], 1.0f) * 0.5f / 600;
    Set4(dc.vParams3, fx.fFlashPos[0] / 800, fx.fFlashPos[1] / 600, 1 / rx, 1 / ry);
  }
  if (bFade)
    dc.nFlags |= POST_FADE;
  DrawImmediate(Key, dc, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, VERTEX_FORMAT_P3F, s_FullScreen, 3);
}

bool CD3D12Renderer::EnsurePostBlur(SPostBlur &Blur, int nFactor)
{
  if (!m_pDevice || !m_pUpload)
    return false;
  const int w = m_pDevice->GetWidth(), h = m_pDevice->GetHeight();
  const int bw = std::max(w / nFactor, 1), bh = std::max(h / nFactor, 1);
  if (Blur.pTex[0] && bw == Blur.nWidth && bh == Blur.nHeight)
    return true;
  ReleasePostBlur(Blur);
  for (CD3D12Texture *&pTex : Blur.pTex)
    pTex = m_pUpload->CreateRenderTarget(bw, bh, DXGI_FORMAT_R8G8B8A8_UNORM);
  if (!Blur.pTex[0] || !Blur.pTex[1] || Blur.pTex[0]->m_nSRV < 0 || Blur.pTex[1]->m_nSRV < 0)
  {
    ReleasePostBlur(Blur);
    return false;
  }
  Blur.nWidth = bw;
  Blur.nHeight = bh;
  Blur.nFactor = nFactor;
  return true;
}

void CD3D12Renderer::ReleasePostBlur(SPostBlur &Blur)
{
  if (m_pUpload)
  {
    for (CD3D12Texture *pTex : Blur.pTex)
    {
      if (pTex)
        m_pUpload->ReleaseTexture(pTex);
    }
  }
  Blur = SPostBlur();
}

void CD3D12Renderer::DrawPostBlur(SPostBlur &Blur, float fSigma)
{
  SDrawConstants dc = DefaultDrawConstants();
  dc.nFlags = POST_DOWNSAMPLE;
  dc.nTex0 = (uint32)m_nSceneColorSRV;
  Set4(dc.vParams0, 1.0f / Blur.nWidth, 1.0f / Blur.nHeight, (float)((Blur.nFactor + 1) / 2), 0);
  DrawPostPass(Blur.pTex[0], dc);
  dc.nFlags = POST_BLUR;
  dc.nTex0 = (uint32)Blur.pTex[0]->m_nSRV;
  Set4(dc.vParams1, 1.0f / Blur.nWidth, 0, fSigma, 0);
  DrawPostPass(Blur.pTex[1], dc);
  dc.nTex0 = (uint32)Blur.pTex[1]->m_nSRV;
  Set4(dc.vParams1, 0, 1.0f / Blur.nHeight, fSigma, 0);
  DrawPostPass(Blur.pTex[0], dc);
}

void CD3D12Renderer::DrawPostPass(CD3D12Texture *pTarget, const SDrawConstants &dc)
{
  m_pDevice->SetOffscreenTargets(pTarget->m_pResource, pTarget->m_RTV, {}, pTarget->m_nWidth, pTarget->m_nHeight);
  SPipelineKey Key = {};
  Key.nProgram = eD3D12P_Post;
  Key.nState = GS_NODEPTHTEST;
  Key.nCull = R_CULL_NONE;
  Key.nTarget = 1;
  DrawImmediate(Key, dc, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, VERTEX_FORMAT_P3F, s_FullScreen, 3);
}

void CD3D12Renderer::ReleaseScreenEffects()
{
  ReleasePostBlur(m_PostGlow);
  ReleasePostBlur(m_PostSoft);
  if (m_pUpload)
    m_pUpload->ReleaseTexture(m_pPostGhost);
  m_pPostGhost = nullptr;
  m_bFlashGhost = false;
}
