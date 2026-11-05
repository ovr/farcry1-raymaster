// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Underwater.cpp
//	Description: The view from under the ocean's surface (d3d12_WaterUnder):
//	with the camera under the ocean's level and the light in its water on
//	(d3d12_WaterVolume), the main view's lists leave out the fog passes of
//	the ocean's fog volume (a uniform colour laid over each surface by its
//	distance), the ocean seen from below gives its surface's light alone
//	(Snell's window and the water it mirrors), and after the water
//	surfaces the blended list copies the scene and draws a triangle over
//	the view (Shaders/Underwater.hlsl): each pixel's scene attenuated in
//	each channel along its path through the water and the light's way down
//	to it, and the light the water scatters towards the camera, its sun's
//	share in the sun's shadow and the FFT ocean's caustics at points along
//	the view (the shafts under the waves); with d3d12_WaterUnderShare that
//	sun's light from a pass before it at one point of each pixel's ray
//	(PSTaps into m_pUnderTaps), the sixteen of the 4 x 4 window round each
//	pixel the segments of its ray, the segments' weights in closed form. The blended objects after it
//	(the plants in the water) take that light over their own colour
//	(SetUnderwaterFog: General.hlsl with Underwater.hlsli's UnderFog, the
//	sun's light its mean along the way); the sprites come after it.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "D3D12Device.h"
#include "D3D12Descriptors.h"
#include "D3D12Upload.h"
#include "Scene.h"

namespace
{
  void Set4(float v[4], float x, float y, float z, float w)
  {
    v[0] = x;
    v[1] = y;
    v[2] = z;
    v[3] = w;
  }

  //! The ocean (one of the terrain's water shaders, as Water.cpp's IsOcean), not a water volume.
  bool IsOceanItem(const SRenderItem &it)
  {
    return it.eDrawClass == eDC_Water && it.pShader && _strnicmp(it.pShader->GetName(), "terrain", 7) == 0;
  }

  //! The sun's share of the light the water scatters (Water.cpp's WATER_VOLUME_SUN_SHARE: as of the light on the sea floor,
  //! the caustics').
  constexpr float UNDER_SUN_SHARE = 0.6f;
  //! How far over the level the surface may be where the depth copy has none of it (the ocean's grid's crests: its
  //! depth is in the copy where it is drawn).
  constexpr float UNDER_GRID_CAP = 4.0f;

  //! The density of the water's fog volume as the water's (SetupWaterDraw).
  float UnderDensity(const SFogVolume &FV)
  {
    return logf(100.0f) / (2 * std::max(FV.fMaxFogDist, 1.0f));
  }

  //! The sun's light in the water, less at a low sun.
  float UnderSun(float fSunZ)
  {
    return std::clamp(fSunZ * 4, 0.0f, 1.0f);
  }
}

float CD3D12Renderer::UnderCap() const
{
  return CV_d3d12_WaterGeometry > 0 && CV_d3d12_WaterFFT > 0 ? UNDER_GRID_CAP : 0.0f;
}

void CD3D12Renderer::SetUnderwaterFog(SDrawConstants &dc, uint32 nState) const
{
  const SFogVolume *pFV = m_pScene ? m_pScene->GetOceanFogVolume() : nullptr;
  if (!pFV)
    return;
  // Over what the blend leaves of the water behind: by the source's alpha or replacing it (the colour attenuated and the
  // light in front added), adding to it (the light it adds attenuated alone); not those that multiply the frame.
  const uint32 nSrc = nState & GS_BLSRC_MASK, nDst = nState & GS_BLDST_MASK;
  const float fMode = !(nState & GS_BLEND_MASK) || (nSrc == GS_BLSRC_SRCALPHA && nDst == GS_BLDST_ONEMINUSSRCALPHA) ? 1.0f
                      : nDst == GS_BLDST_ONE                                                                       ? 2.0f
                                                                                                                   : 0.0f;
  if (fMode == 0)
    return;
  // The view's debug views (d3d12_WaterDebug 31-33) by tens: the object shows the same term.
  const int nDebug = CV_d3d12_WaterDebug;
  const float fDebug = nDebug >= 31 && nDebug <= 33 ? 10.0f * (float)(nDebug - 30) : 0.0f;
  Set4(dc.vUnderColor, pFV->Color.r, pFV->Color.g, pFV->Color.b, UnderDensity(*pFV));
  Set4(dc.vUnder, pFV->fFogLayerZ, UnderCap(), UNDER_SUN_SHARE * UnderSun(m_SunDir[2]), fMode + fDebug);
}

void CD3D12Renderer::UpdateUnderwater(std::span<const SRenderItem> Items)
{
  m_bUnderwater = false;
  if (CV_d3d12_WaterUnder <= 0 || CV_d3d12_WaterVolume <= 0 || m_bHeatVision || !m_pScene)
    return;
  const SFogVolume *pFV = m_pScene->GetOceanFogVolume();
  if (!pFV || m_Camera.GetPos().z >= pFV->fFogLayerZ)
    return;
  m_bUnderwater = std::any_of(Items.begin(), Items.end(), IsOceanItem);
}

bool CD3D12Renderer::UnderwaterOn() const
{
  return m_bUnderwater && m_nRecurseLevel <= 1 && (m_nSceneFlags & SHDF_ALLOWHDR) && !m_bReflectionPass && !m_bEnvCubePass &&
         !m_bShadowMapPass && !m_bSpritePass && !m_bCascadePass && !m_bRayBehindPass;
}

void CD3D12Renderer::DrawUnderwater()
{
  // The water's debug views but its own: the surfaces' terms as they are.
  const int nDebug = CV_d3d12_WaterDebug;
  const SFogVolume *pFV = m_pScene ? m_pScene->GetOceanFogVolume() : nullptr;
  if (!pFV || !m_pDevice || (nDebug != 0 && (nDebug < 31 || nDebug > 33)))
    return;
  GpuSection("underwater");
  // The scene as drawn up to here (the water surfaces in it, their depth with the ocean's grid) in a copy of its own: the
  // flares read the list's first copy's depth.
  if (!EnsureSceneCopy(m_UnderCopy))
    return;
  ResolveMsaaDepth();
  m_pDevice->CopyTargets(m_UnderCopy.pColor.Get(), m_UnderCopy.pDepth.Get());
  m_bUnderDrawn = true;

  SPipelineKey Key = {};
  Key.nProgram = eD3D12P_Underwater;
  Key.nState = GS_NODEPTHTEST | GS_COLMASKONLYRGB;
  Key.nCull = R_CULL_NONE;
  SDrawConstants dc = DefaultDrawConstants();
  dc.nTex0 = (uint32)m_UnderCopy.nColorSRV;
  dc.nTex2 = (uint32)m_UnderCopy.nDepthSRV;
  // The water's colour and density as the water's (SetupWaterDraw), the sun's share of the light it scatters less at a
  // low sun; the FFT ocean's caustics made this frame (UpdateOcean), else none.
  Set4(dc.vColor, pFV->Color.r, pFV->Color.g, pFV->Color.b, UnderDensity(*pFV));
  const float fSun = UnderSun(m_SunDir[2]);
  Set4(dc.vParams1, UNDER_SUN_SHARE * fSun, 0, 1, 1);
  const bool bCaustics = SetOceanCaustics(dc, fSun) && m_Ocean.nCausticsFrame == m_nFrames;
  Set4(dc.vParams0, pFV->fFogLayerZ, UnderCap(), (float)nDebug, bCaustics && fSun > 0 ? 1.0f : 0.0f);
  float vCamera[4];
  GetRayView(vCamera, dc.vLightPos[2], dc.vLightPos[3], dc.vLightPos[4]);

  // Over the whole view (the viewport in the back buffer's pixels, scaled to the scene's by the draw).
  int Viewport[4];
  memcpy(Viewport, m_Viewport, sizeof(Viewport));
  const bool bScissor = m_bScissor, bItemScissor = m_bItemScissor;
  m_Viewport[0] = m_Viewport[1] = 0;
  m_Viewport[2] = m_pDevice->GetWidth();
  m_Viewport[3] = m_pDevice->GetHeight();
  m_bScissor = m_bItemScissor = false;
  static const float s_Triangle[3][3] = { { -1, -1, 0 }, { -1, 3, 0 }, { 3, -1, 0 } };
  // d3d12_WaterUnderShare: the segments' weights in closed form (vParams3.w 1), and with the sun's share the taps pass first
  // (2): the sun's light at one point of each pixel's ray into m_pUnderTaps, the main view's jitter and its noise's frame
  // given it (an off screen target's frame constants have neither).
  if (CV_d3d12_WaterUnderShare > 0)
  {
    Key.nProgram = eD3D12P_UnderwaterShare;
    dc.vParams3[3] = 1;
    if (dc.vParams1[0] > 0 && EnsureUnderTaps())
    {
      SFrameConstants fc;
      MakeFrameConstants(fc, m_pDevice->IsSceneBound());
      SDrawConstants tc = dc;
      tc.vParams2[2] = fc.vJitter[0];
      tc.vParams2[3] = fc.vJitter[1];
      tc.vParams3[2] = fc.vTime[1];
      SPipelineKey TapsKey = {};
      TapsKey.nProgram = eD3D12P_UnderwaterTaps;
      TapsKey.nState = GS_NODEPTHTEST;
      TapsKey.nCull = R_CULL_NONE;
      TapsKey.nTarget = 1;
      TapsKey.nColorFormat = PIPELINE_COLOR_R32F;
      // (Its own GPU section; the copy and the view's pass the section "underwater".)
      GpuSection("underwater taps");
      m_pDevice->SetOffscreenTargets(m_pUnderTaps->m_pResource, m_pUnderTaps->m_RTV, {}, m_pUnderTaps->m_nWidth, m_pUnderTaps->m_nHeight);
      DrawImmediate(TapsKey, tc, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, VERTEX_FORMAT_P3F, s_Triangle, 3);
      m_pDevice->SetBackBufferTargets();
      GpuSection("underwater");
      dc.nTex3 = (uint32)m_pUnderTaps->m_nSRV;
      dc.vParams3[3] = 2;
    }
  }
  DrawImmediate(Key, dc, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, VERTEX_FORMAT_P3F, s_Triangle, 3);
  memcpy(m_Viewport, Viewport, sizeof(Viewport));
  m_bScissor = bScissor;
  m_bItemScissor = bItemScissor;
}

bool CD3D12Renderer::EnsureUnderTaps()
{
  if (!m_pDevice || !m_pUpload || !m_pDescriptors)
    return false;
  const int w = m_pDevice->GetMainWidth(), h = m_pDevice->GetMainHeight();
  if (m_pUnderTaps && m_pUnderTaps->m_nWidth == w && m_pUnderTaps->m_nHeight == h)
    return true;
  if (m_pUnderTaps)
    m_pUpload->ReleaseTexture(m_pUnderTaps);
  m_pUnderTaps = m_pUpload->CreateRenderTarget(w, h, DXGI_FORMAT_R32_FLOAT);
  if (m_pUnderTaps && m_pUnderTaps->m_nSRV < 0)
  {
    m_pUpload->ReleaseTexture(m_pUnderTaps);
    m_pUnderTaps = nullptr;
  }
  return m_pUnderTaps != nullptr;
}

void CD3D12Renderer::ReleaseUnderwater()
{
  FreeSceneCopy(m_UnderCopy);
  if (m_pUnderTaps && m_pUpload)
    m_pUpload->ReleaseTexture(m_pUnderTaps);
  m_pUnderTaps = nullptr;
  m_bUnderwater = false;
}
