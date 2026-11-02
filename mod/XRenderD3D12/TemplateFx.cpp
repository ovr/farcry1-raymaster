// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: TemplateFx.cpp
//	Description: The templates of shaders of their own (SShaderProps::eFx,
//	eDC_ShaderFx and eDC_ShaderFxBlend; General.hlsl, ShaderFx): the
//	rainbow, the membrane, cartoon shading, fur, iridescence, the soap
//	bubble and the lava. Each but the lava is the model of one material of
//	Objects/matlib in the game; the lava is the rivers and pools of Volcano.
//
//	What the original draws (its D3D9 trace; research docs):
//	- TemplRainbow: its vertex program alone over a fixed function stage
//	  (its pixel shader CGRCRainbow is not used): Defaults/colors2 at
//	  (N.E, N.H), H = normalize(E + N), wrapped, opaque and unlit.
//	- TemplMembrane: membrane/yellowred (a row of colours) at E.N + 0.2 t,
//	  added (ONE, ONE) without depth writes.
//	- TemplCartoonShading: one opaque pass for each light of the object,
//	  each over the last: defaults/4shade (the steps of the light) at L.N
//	  times defaults/edgeimage (dark at the outline) at E.N times the
//	  colour 1 0.8 0.2, clamped. Here the sun's light, once.
//	- TemplFurRendering and TemplIridescence_PS20: never drawn. Here what
//	  their scripts set out: the model with fuzz lit by N.L, then 8 shells
//	  of it 1 to 8 cm out along the normal, alpha 0.9 down to 0.2; the
//	  wing lit from both sides, see-through by its alpha, with a gloss of
//	  the rainbow gradient at N.E.
//	- TemplBubble_PS20: the model wobbling along its normal by a noise
//	  moving with time (0.2 times it), the texture times the cube
//	  reflected, see-through but at its edges (SRCALPHA, INVSRCALPHA).
//	- LavaVolume: its vertex and pixel programs, opaque and unlit: the
//	  material's texture moving with time (DetailMoveSpeedX/Y times a
//	  hundredth of the time, then DetailTillingAmountX/Y) under its lava
//	  colour (ColorR/G/B), mixed by OverlayAmount and doubled (as the
//	  original's program; without it the lava was half as bright as the
//	  original's), the colour brighter where
//	  defaults/white_plain_d (a noise of greens) is greener; that one at an
//	  eighth of the position in the object's space times TillingAmountX/Y,
//	  moving with time (MoveSpeedX/Y, five hundredths of the time, back)
//	  and rippled by WaterBump (BumpAmountX/Y; it moves a hundredth, on)
//	  (General.hlsl, Lava). Its waves (WaveAmplitude) are 0 in the game's
//	  materials: left out. Without it (drawn as an opaque lit surface, its
//	  texture the terrain's rock) the lava was grey ground. Its colour comes
//	  from the material only through the template's public parameters
//	  (s_PublicParams, Shader.cpp).
//	Per pixel where the original's vertex programs take the dot products
//	at the vertices.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "D3D12Device.h"
#include "D3D12Descriptors.h"
#include "D3D12Pipeline.h"
#include "Texture.h"
#include "Shader.h"
#include "Scene.h"

namespace
{

void Set4(float v[4], float x, float y, float z, float w)
{
  v[0] = x; v[1] = y; v[2] = z; v[3] = w;
}

//! GetFxTexture's textures.
enum EFxTexture
{
  eFT_Colors2,
  eFT_Membrane,
  eFT_Shade,
  eFT_Edge,
  eFT_Fuzz,
  eFT_RainbowGradient,
  eFT_WhitePlain,
};

constexpr float MEMBRANE_SPEED = 0.2f;        //!< the membrane's colours per second (its TexCoordMod)
constexpr float BUBBLE_SPEED_X = 0.8f;        //!< the bubble's noise per second along x and y (its DeformWave)
constexpr float BUBBLE_SPEED_Y = 0.6f;
constexpr float BUBBLE_PERIOD = 32;           //!< the noise repeats every 32 units
constexpr float FUR_SHELL_STEP = 0.01f;       //!< a shell's distance from the last along the normal (its Modifiers)
constexpr float LAVA_TIME = 0.01f;            //!< the lava's flow: its texture's shift per second per DetailMoveSpeed (its ripple's per MoveSpeed)
constexpr float LAVA_GLOW_TIME = -0.05f;      //!< its glow map's shift per second per MoveSpeed
constexpr float LAVA_SCALE = 0.125f;          //!< the ripple's and the glow map's coordinates per unit of the object's space

//! fmod(x, 1): a texture shift that wraps.
float Wrap(float x)
{
  return x - floorf(x);
}

//! The object's world matrix as DrawLeafItem takes it: the whole matrix with FOB_TRANS_* flags, else its
//! translation only.
Matrix44 GetWorldMatrix(const CCObject *pObj)
{
  if (pObj->m_ObjFlags & FOB_TRANS_MASK)
    return pObj->m_Matrix;
  Matrix44 m;
  m.SetIdentity();
  m.data[12] = pObj->m_Matrix.data[12];
  m.data[13] = pObj->m_Matrix.data[13];
  m.data[14] = pObj->m_Matrix.data[14];
  return m;
}

}

CTexture *CD3D12Renderer::GetFxTexture(int nIndex, const char *szName, int nFlags)
{
  if (nIndex < 0 || nIndex >= (int)std::size(m_pFxTextures) || !m_pTextures)
    return nullptr;
  if (!m_pFxTextures[nIndex])
    m_pFxTextures[nIndex] = m_pTextures->LoadTexture(szName, nFlags, 0, eTT_Base, -1, -1, -1, 0);
  CTexture *pTex = m_pFxTextures[nIndex];
  return pTex && !(pTex->GetFlags() & FT_NOTFOUND) && pTex->IsTextureLoaded() ? pTex : nullptr;
}

bool CD3D12Renderer::SetupTemplateFxDraw(SPipelineKey &Key, SDrawConstants &dc, const SRenderItem &it, const SShaderProps &Props, CTexture *pTex)
{
  if (m_bSpritePass)
    return false;
  const float t = iTimer ? iTimer->GetCurrTime() : 0.0f;
  CTexture *pWhite = m_pTextures ? m_pTextures->GetWhite() : nullptr;
  const uint32 nWhite = pWhite ? (uint32)pWhite->GetSRV() : 0;
  dc.nFlags = 0;
  dc.nFlags2 = DF2_SHADER_FX;
  dc.nSamplers = pTex ? pTex->GetSampler() : XR_SAMPLER_LINEAR_WRAP;
  Set4(dc.vColor, 1, 1, 1, 1);
  Set4(dc.vParams0, 0, 0, 0, 0);
  Set4(dc.vParams1, 0, 0, 0, 0);
  switch (Props.eFx)
  {
  case ETemplateFx::Rainbow:
  {
    CTexture *pColors = GetFxTexture(eFT_Colors2, "textures/defaults/colors2", 0);
    if (!pColors)
      return false;
    dc.vParams3[3] = XR_FX_RAINBOW;
    dc.nTex0 = pColors->GetSRV();
    Key.nState = GS_DEPTHWRITE;
    return true;
  }
  case ETemplateFx::Membrane:
  {
    CTexture *pColors = GetFxTexture(eFT_Membrane, "textures/defaults/membrane/yellowred", 0);
    if (!pColors)
      return false;
    dc.vParams3[3] = XR_FX_MEMBRANE;
    dc.nFlags2 |= DF2_FOG_BLACK;
    dc.nTex0 = pColors->GetSRV();
    dc.vParams0[0] = fmodf(MEMBRANE_SPEED * t, 1.0f);
    Key.nState = GS_BLSRC_ONE | GS_BLDST_ONE;
    return true;
  }
  case ETemplateFx::Cartoon:
  {
    CTexture *pShade = GetFxTexture(eFT_Shade, "textures/defaults/4shade", FT_CLAMP);
    CTexture *pEdge = GetFxTexture(eFT_Edge, "textures/defaults/edgeimage", FT_CLAMP);
    if (!pShade || !pEdge)
      return false;
    dc.vParams3[3] = XR_FX_CARTOON;
    dc.nTex0 = pShade->GetSRV();
    dc.nTex1 = pEdge->GetSRV();
    Set4(dc.vColor, 1, 0.8f, 0.2f, 1);
    Key.nState = GS_DEPTHWRITE;
    return true;
  }
  case ETemplateFx::Fur:
  {
    // The model itself, opaque; its shells follow (SetupFurShell).
    CTexture *pFuzz = GetFxTexture(eFT_Fuzz, "textures/defaults/fuzz", 0);
    if (!pFuzz)
      return false;
    dc.vParams3[3] = XR_FX_FUR;
    dc.nTex0 = pFuzz->GetSRV();
    Key.nState = GS_DEPTHWRITE;
    return true;
  }
  case ETemplateFx::Iridescence:
  {
    // The script's parameters, the material's public ones over them.
    CTexture *pRainbow = GetFxTexture(eFT_RainbowGradient, "textures/defaults/rainbowgradient", 0);
    if (!pRainbow)
      return false;
    CTexture *pBump = GetNormalMap(it.pRes);
    CTexture *pGloss = GetGlowMap(it.pRes);
    dc.vParams3[3] = XR_FX_IRIDESCENCE;
    dc.nTex0 = pTex ? (uint32)pTex->GetSRV() : nWhite;
    dc.nTex1 = pBump ? (uint32)pBump->GetSRV() : nWhite;
    dc.nTex2 = pGloss ? (uint32)pGloss->GetSRV() : nWhite;
    dc.nTex3 = pRainbow->GetSRV();
    Set4(dc.vParams0, GetResourceFloat(it.pRes, "IridescenceScale", 0.2f), GetResourceFloat(it.pRes, "IridescenceBias", 0.2f),
         GetResourceFloat(it.pRes, "IridescenceSpeedScale", 8), GetResourceFloat(it.pRes, "IridescenceSpeedBias", 0.5f));
    Set4(dc.vParams1, GetResourceFloat(it.pRes, "TranslucencyCoeff", 4), pBump ? 1.0f : 0.0f, 0, 0);
    Key.nState = GS_BLSRC_ONE | GS_BLDST_ONEMINUSSRCALPHA;
    return true;
  }
  case ETemplateFx::Bubble:
  {
    CTexture *pCube = GetEnvCubeMap(it.pRes);
    if (!pCube)
      return false;
    dc.vParams3[3] = XR_FX_BUBBLE;
    dc.nTex0 = pTex ? (uint32)pTex->GetSRV() : nWhite;
    dc.nTex1 = pCube->GetSRV();
    Set4(dc.vParams0, GetResourceFloat(it.pRes, "AlphaScale", 0.6f), GetResourceFloat(it.pRes, "AlphaBias", 0.1f), 0, 0);
    Set4(dc.vParams1, fmodf(BUBBLE_SPEED_X * t, BUBBLE_PERIOD), fmodf(BUBBLE_SPEED_Y * t, BUBBLE_PERIOD), 0,
         GetResourceFloat(it.pRes, "WaveAmplitude", 0.2f));
    Key.nState = GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA;
    return true;
  }
  case ETemplateFx::Lava:
  {
    // The script's parameters, the material's public ones over them (s_PublicParams lists them, Shader.cpp).
    CTexture *pGlow = GetFxTexture(eFT_WhitePlain, "textures/defaults/white_plain_d", 0);
    if (!m_pHazeBump && m_pTextures)
      m_pHazeBump = m_pTextures->LoadTexture("textures/waterbump", 0, 0, eTT_Bumpmap, -1, -1, -1, 0);
    if (!pGlow || !m_pHazeBump)
      return false;
    dc.vParams3[3] = XR_FX_LAVA;
    dc.nTex0 = pTex ? (uint32)pTex->GetSRV() : nWhite;
    dc.nTex1 = m_pHazeBump->GetSRV();
    dc.nTex2 = pGlow->GetSRV();
    // The planes of the object's x and y (an eighth, times TillingAmount) in world space.
    const float fTile[2] = { GetResourceFloat(it.pRes, "TillingAmountX", 2), GetResourceFloat(it.pRes, "TillingAmountY", 2) };
    const float fMove[2] = { GetResourceFloat(it.pRes, "MoveSpeedX", 0.2f), GetResourceFloat(it.pRes, "MoveSpeedY", 0.2f) };
    const Matrix44 Inv = GetInverted44(GetWorldMatrix(it.pObj));
    float *pRows[2] = { dc.vTexGenU, dc.vTexGenV };
    for (int r = 0; r < 2; r++)
      for (int i = 0; i < 4; i++)
        pRows[r][i] = Inv(i, r) * LAVA_SCALE * fTile[r];
    Set4(dc.vParams2, Wrap(LAVA_TIME * t * fMove[0] * fTile[0]), Wrap(LAVA_TIME * t * fMove[1] * fTile[1]),
         Wrap(LAVA_GLOW_TIME * t * fMove[0] * fTile[0]), Wrap(LAVA_GLOW_TIME * t * fMove[1] * fTile[1]));
    dc.vParams3[0] = GetResourceFloat(it.pRes, "BumpAmountX", 0.02f);
    dc.vParams3[1] = GetResourceFloat(it.pRes, "BumpAmountY", 0.02f);
    // The shift s before the tiling k: (uv + s) k; s as fmod(s k, 1) / k (the same picture, the time kept small).
    const float k[2] = { GetResourceFloat(it.pRes, "DetailTillingAmountX", 1), GetResourceFloat(it.pRes, "DetailTillingAmountY", 1) };
    const float fSpeed[2] = { GetResourceFloat(it.pRes, "DetailMoveSpeedX", 0.2f), GetResourceFloat(it.pRes, "DetailMoveSpeedY", 0.2f) };
    float fShift[2];
    for (int i = 0; i < 2; i++)
      fShift[i] = k[i] != 0 ? fmodf(fSpeed[i] * LAVA_TIME * t * k[i], 1.0f) / k[i] : 0.0f;
    Set4(dc.vParams0, k[0], k[1], fShift[0], fShift[1]);
    Set4(dc.vParams1, GetResourceFloat(it.pRes, "ColorR", 0.8f), GetResourceFloat(it.pRes, "ColorG", 0.55f), GetResourceFloat(it.pRes, "ColorB", 0),
         GetResourceFloat(it.pRes, "OverlayAmount", 0.32f));
    Key.nState = GS_DEPTHWRITE;
    return true;
  }
  default:
    return false;
  }
}

void CD3D12Renderer::SetupFurShell(SPipelineKey &Key, SDrawConstants &dc, int n)
{
  dc.vParams0[0] = FUR_SHELL_STEP * n;
  dc.vColor[3] = 1 - 0.1f * n;
  Key.nState = GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA;
}
