// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Renderer2D.cpp
//	Description: Render states, 2D, lines, text and immediate drawing.
//
//	The immediate calls draw at once (copies of their vertices in per frame
//	memory, Draw.cpp) with the render state set by SetState, SetCullMode,
//	SetTexture, SetColorOp and the texture matrix.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "D3D12Device.h"
#include "D3D12Descriptors.h"
#include "Texture.h"
#include <IFont.h>
#include <cstdarg>

namespace
{

using SVertex = struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F;
constexpr int VERTEX_FORMAT = VERTEX_FORMAT_P3F_COL4UB_TEX2F;

uint8 ToByte(float f)
{
  return (uint8)std::clamp((int)(f * 255.0f + 0.5f), 0, 255);
}

//! Vertex colours are bytes B, G, R, A.
UCol MakeColor(float r, float g, float b, float a)
{
  UCol c;
  c.bcolor[0] = ToByte(b);
  c.bcolor[1] = ToByte(g);
  c.bcolor[2] = ToByte(r);
  c.bcolor[3] = ToByte(a);
  return c;
}

UCol MakeColor(const CFColor &c)
{
  return MakeColor(c.r, c.g, c.b, c.a);
}

SVertex MakeVertex(float x, float y, float z, UCol Color, float s = 0, float t = 0)
{
  SVertex v;
  v.xyz = Vec3(x, y, z);
  v.color = Color;
  v.st[0] = s;
  v.st[1] = t;
  return v;
}

SVertex MakeVertex(const Vec3 &p, UCol Color, float s = 0, float t = 0)
{
  return MakeVertex(p.x, p.y, p.z, Color, s, t);
}

void SetMatrix(float Dst[16], const Matrix44 &m)
{
  memcpy(Dst, m.data, 16 * sizeof(float));
}

//! A triangle list of a fan (pInds NULL: the vertices in order).
std::vector<ushort> FanToList(const ushort *pInds, int nCount)
{
  std::vector<ushort> List;
  for (int i = 1; i + 1 < nCount; i++)
  {
    List.push_back(pInds ? pInds[0] : 0);
    List.push_back(pInds ? pInds[i] : (ushort)i);
    List.push_back(pInds ? pInds[i + 1] : (ushort)(i + 1));
  }
  return List;
}

//! A triangle list of quads (4 vertices each).
std::vector<ushort> QuadsToList(const ushort *pInds, int nCount)
{
  std::vector<ushort> List;
  for (int i = 0; i + 3 < nCount; i += 4)
  {
    for (int k : { 0, 1, 2, 0, 2, 3 })
      List.push_back(pInds ? pInds[i + k] : (ushort)(i + k));
  }
  return List;
}

//! The engine's default font, or NULL.
IFFont *GetDefaultFont()
{
  ICryFont *pCryFont = iSystem ? iSystem->GetICryFont() : nullptr;
  return pCryFont ? pCryFont->GetFont("default") : nullptr;
}

std::string FormatV(const char *szFormat, va_list Args)
{
  char szBuf[2048];
  if (!szFormat)
    return std::string();
  vsnprintf(szBuf, sizeof(szBuf), szFormat, Args);
  szBuf[sizeof(szBuf) - 1] = 0;
  return szBuf;
}

}

//////////////////////////////////////////////////////////////////////////
// Render state

void CD3D12Renderer::ApplyRenderState(SPipelineKey &Key, SDrawConstants &dc, int nVertexFormat, bool bVertexColor)
{
  Key = {};
  Key.nState = (uint32)m_nState;
  Key.nProgram = eD3D12P_General;
  Key.nVertexFormat = (uint8)nVertexFormat;
  Key.nCull = (uint8)m_nCullMode;
  Key.bDepthBias = m_bDepthBias ? 1 : 0;
  Key.bWireframe = m_nPolygonMode == R_WIREFRAME_MODE ? 1 : 0;

  dc = DefaultDrawConstants();
  const byte eCo = m_ColorOp[0];
  CTexture *pTex = m_pTextures ? m_pTextures->GetByID(m_nCurTexture[0]) : nullptr;
  if (pTex && !pTex->IsCube() && eCo != eCO_ARG2 && eCo != eCO_DISABLE)
  {
    dc.nFlags |= DF_TEX0;
    dc.nTex0 = pTex->GetSRV();
    int nSampler = pTex->GetSampler();
    if (m_bTexClamp || (m_nState & GS_TEXPARAM_CLAMP))
      nSampler = (nSampler == XR_SAMPLER_POINT_WRAP || nSampler == XR_SAMPLER_POINT_CLAMP) ? XR_SAMPLER_POINT_CLAMP : XR_SAMPLER_LINEAR_CLAMP;
    dc.nSamplers = nSampler;
  }
  // The second argument of the colour or the alpha operation (bits 3-5) eCA_Constant: the texture
  // times the material colour (SetMaterialColor) instead of the vertex colour (the terrain decals).
  const bool bModulate = eCo == eCO_MODULATE || eCo == eCO_MODULATE2X;
  const bool bConstColor = bModulate && ((m_ColorOp[2] >> 3) & 7) == eCA_Constant;
  const bool bConstAlpha = m_ColorOp[1] == eCO_MODULATE && ((m_ColorOp[3] >> 3) & 7) == eCA_Constant;
  if (bVertexColor && eCo != eCO_REPLACE && eCo != eCO_DECAL && !bConstColor)
  {
    dc.nFlags |= DF_VCOLOR;
    if (eCo == eCO_MODULATE2X)
      dc.nFlags |= DF_VCOLOR_X2;
  }
  if (bConstColor)
  {
    const float s = eCo == eCO_MODULATE2X ? 2.0f : 1.0f;
    for (int i = 0; i < 3; i++)
      dc.vColor[i] = s * m_MaterialColor[i];
  }
  if (bConstAlpha)
    dc.vColor[3] = m_MaterialColor[3];

  switch ((uint32)m_nState & GS_ALPHATEST_MASK)
  {
  case GS_ALPHATEST_GREATER0:
    dc.nFlags |= DF_ALPHATEST;
    dc.fAlphaRef = 1.0f / 255.0f;
    break;
  case GS_ALPHATEST_GEQUAL128:
    dc.nFlags |= DF_ALPHATEST;
    dc.fAlphaRef = 128.0f / 255.0f;
    break;
  case GS_ALPHATEST_GEQUAL64:
    dc.nFlags |= DF_ALPHATEST;
    dc.fAlphaRef = 64.0f / 255.0f;
    break;
  case GS_ALPHATEST_LESS128:
    dc.nFlags |= DF_ALPHATEST | DF_ALPHATEST_LESS;
    dc.fAlphaRef = 128.0f / 255.0f;
    break;
  default:
    break;
  }

  if (m_bTexMatrix)
  {
    dc.nFlags |= DF_TEXMATRIX;
    dc.vTexMat0[0] = m_TexMatrix[0]; dc.vTexMat0[1] = m_TexMatrix[1]; dc.vTexMat0[2] = m_TexMatrix[2];
    dc.vTexMat1[0] = m_TexMatrix[3]; dc.vTexMat1[1] = m_TexMatrix[4]; dc.vTexMat1[2] = m_TexMatrix[5];
  }
  if (m_bTexGen && !m_b2DMode)
  {
    dc.nFlags |= DF_TEXGEN_WORLD;
    memcpy(dc.vTexGenU, &m_TexGen[0], sizeof(dc.vTexGenU));
    memcpy(dc.vTexGenV, &m_TexGen[4], sizeof(dc.vTexGenV));
  }
  if (m_bFog && !m_b2DMode)
    dc.nFlags |= DF_FOG;
}

Matrix44 CD3D12Renderer::GetImmediateViewProj() const
{
  if (m_b2DMode)
    return MakeOrtho(m_fOrthoWidth, m_fOrthoHeight);
  return m_ViewMatrix * m_ProjMatrix;
}

void CD3D12Renderer::SetState(int State)
{
  m_nState = State;
}

void CD3D12Renderer::SetCullMode(int mode)
{
  m_nCullMode = mode;
}

bool CD3D12Renderer::EnableFog(bool enable)
{
  const bool bOld = m_bFog;
  m_bFog = enable;
  InvalidateFrameConstants();
  return bOld;
}

void CD3D12Renderer::SetFog(float density,float fogstart,float fogend,const float *color,int fogmode)
{
  m_fFogStart = fogstart;
  m_fFogEnd = fogend;
  if (color)
    SetFogColor(const_cast<float *>(color));
  InvalidateFrameConstants();
}

void CD3D12Renderer::SetFogColor(float * color)
{
  if (!color)
    return;
  m_FogColor[0] = color[0];
  m_FogColor[1] = color[1];
  m_FogColor[2] = color[2];
  m_FogColor[3] = 1;
  InvalidateFrameConstants();
}

void CD3D12Renderer::EnableTexGen(bool enable)
{
  m_bTexGen = enable;
}

void CD3D12Renderer::SetTexgen(float scaleX,float scaleY,float translateX,float translateY)
{
  const float TexGen[8] = { scaleX, 0, 0, translateX, 0, scaleY, 0, translateY };
  memcpy(m_TexGen, TexGen, sizeof(m_TexGen));
}

void CD3D12Renderer::SetTexgen3D(float x1, float y1, float z1, float x2, float y2, float z2)
{
  const float TexGen[8] = { x1, y1, z1, 0, x2, y2, z2, 0 };
  memcpy(m_TexGen, TexGen, sizeof(m_TexGen));
}

void CD3D12Renderer::SetLodBias(float value)
{
}

void CD3D12Renderer::SetColorOp(byte eCo, byte eAo, byte eCa, byte eAa)
{
  m_ColorOp[0] = eCo;
  m_ColorOp[1] = eAo;
  m_ColorOp[2] = eCa;
  m_ColorOp[3] = eAa;
}

void CD3D12Renderer::EnableTMU(bool enable)
{
  if (!enable)
    m_nCurTexture[m_nCurTMU] = -1;
}

void CD3D12Renderer::SelectTMU(int tnum)
{
  m_nCurTMU = std::clamp(tnum, 0, 1);
}

void CD3D12Renderer::SetTexture(int tnum, ETexType Type)
{
  // SetTexClampMode applies to the texture bound until the next one.
  m_nCurTexture[m_nCurTMU] = tnum;
  m_bTexClamp = false;
}

void CD3D12Renderer::SetWhiteTexture()
{
  CTexture *pWhite = m_pTextures ? m_pTextures->GetWhite() : nullptr;
  m_nCurTexture[m_nCurTMU] = pWhite ? pWhite->GetTextureID() : -1;
}

void CD3D12Renderer::SetMaterialColor(float r, float g, float b, float a)
{
  m_MaterialColor[0] = r;
  m_MaterialColor[1] = g;
  m_MaterialColor[2] = b;
  m_MaterialColor[3] = a;
}

void CD3D12Renderer::SetLineWidth(float fWidth)
{
  m_fLineWidth = fWidth;
}

void CD3D12Renderer::TransformTextureMatrix(float x, float y, float angle, float scale)
{
  // About the texture's centre: scale 1 gives (u + x, v + y), scale -1 (1 + x - u, 1 + y - v).
  const float a = angle * gf_PI / 180.0f, c = cosf(a) * scale, s = sinf(a) * scale;
  m_bTexMatrix = true;
  m_TexMatrix[0] = c;
  m_TexMatrix[1] = -s;
  m_TexMatrix[2] = 0.5f - 0.5f * c + 0.5f * s + x;
  m_TexMatrix[3] = s;
  m_TexMatrix[4] = c;
  m_TexMatrix[5] = 0.5f - 0.5f * s - 0.5f * c + y;
}

void CD3D12Renderer::ResetTextureMatrix()
{
  m_bTexMatrix = false;
}

void CD3D12Renderer::Set2DMode(bool enable, int ortox, int ortoy)
{
  m_b2DMode = enable;
  if (enable && ortox > 0 && ortoy > 0)
  {
    m_fOrthoWidth = (float)ortox;
    m_fOrthoHeight = (float)ortoy;
  }
}

void CD3D12Renderer::SetTexClampMode(bool clamp)
{
  m_bTexClamp = clamp;
}

// The screen (GetWidth / GetHeight), not the viewport: callers place viewports with it.

float CD3D12Renderer::ScaleCoordX(float value)
{
  return value * (float)m_nWidth / 800.0f;
}

float CD3D12Renderer::ScaleCoordY(float value)
{
  return value * (float)m_nHeight / 600.0f;
}

//////////////////////////////////////////////////////////////////////////
// 2D images

void CD3D12Renderer::Draw2dImage(float xpos,float ypos,float w,float h,int texture_id,float s0,float t0,float s1,float t1,float angle,float r,float g,float b,float a,float z)
{
  if (m_Viewport[2] <= 0 || m_Viewport[3] <= 0)
    return;
  // The 800 x 600 virtual screen on the viewport, whatever Set2DMode says.
  const float fScaleX = m_Viewport[2] / 800.0f, fScaleY = m_Viewport[3] / 600.0f;
  float x[4] = { xpos, xpos + w, xpos + w, xpos };
  float y[4] = { ypos, ypos, ypos + h, ypos + h };
  for (int i = 0; i < 4; i++)
  {
    x[i] *= fScaleX;
    y[i] *= fScaleY;
  }
  if (angle != 0)
  {
    // About the centre; positive angles turn clockwise on the screen (y points down).
    const float cx = (x[0] + x[2]) * 0.5f, cy = (y[0] + y[2]) * 0.5f;
    const float fA = angle * gf_PI / 180.0f, c = cosf(fA), s = sinf(fA);
    for (int i = 0; i < 4; i++)
    {
      const float dx = x[i] - cx, dy = y[i] - cy;
      x[i] = cx + dx * c - dy * s;
      y[i] = cy + dx * s + dy * c;
    }
  }
  const float u[4] = { s0, s1, s1, s0 };
  const float v[4] = { 1 - t0, 1 - t0, 1 - t1, 1 - t1 };
  const UCol Color = MakeColor(r, g, b, a);
  const float fZ = std::clamp(z, 0.0f, 1.0f);
  SVertex Verts[4];
  for (int i = 0; i < 4; i++)
    Verts[i] = MakeVertex(x[i], y[i], fZ, Color, u[i], v[i]);
  static const ushort Inds[6] = { 0, 1, 2, 0, 2, 3 };

  // The image's texture (-1: the vertex colour alone) modulated by the colour, whatever SetColorOp
  // and SetMaterialColor say: the terrain decals leave the material colour as the second argument
  // of the colour and the alpha operation, which turned the menu's panels into its colour.
  const int nSavedTexture = m_nCurTexture[0];
  byte SavedOps[4];
  memcpy(SavedOps, m_ColorOp, sizeof(SavedOps));
  const bool bSavedClamp = m_bTexClamp;
  m_nCurTexture[0] = texture_id;
  m_ColorOp[0] = m_ColorOp[1] = eCO_MODULATE;
  m_ColorOp[2] = m_ColorOp[3] = DEF_TEXARG0;
  m_bTexClamp = false;
  SPipelineKey Key;
  SDrawConstants dc;
  ApplyRenderState(Key, dc, VERTEX_FORMAT, true);
  m_nCurTexture[0] = nSavedTexture;
  memcpy(m_ColorOp, SavedOps, sizeof(SavedOps));
  m_bTexClamp = bSavedClamp;
  Key.nCull = R_CULL_NONE;
  dc.nFlags &= ~(DF_TEXGEN_WORLD | DF_FOG);
  SetMatrix(dc.mWorldViewProj, MakeOrtho((float)m_Viewport[2], (float)m_Viewport[3]));
  DrawImmediate(Key, dc, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, VERTEX_FORMAT, Verts, 4, Inds, 6);
}

void CD3D12Renderer::DrawImage(float xpos,float ypos,float w,float h,int texture_id,float s0,float t0,float s1,float t1,float r,float g,float b,float a)
{
  Draw2dImage(xpos, ypos, w, h, texture_id, s0, t0, s1, t1, 0, r, g, b, a, 1);
}

//////////////////////////////////////////////////////////////////////////
// Dynamic vertices and text

void *CD3D12Renderer::GetDynVBPtr(int nVerts, int &nOffs, int Pool)
{
  // Pool 0 holds P3F_COL4UB_TEX2F vertices; every draw takes the vertices from the start.
  nOffs = 0;
  if (nVerts <= 0)
    return nullptr;
  m_DynVB.resize(nVerts);
  return m_DynVB.data();
}

void CD3D12Renderer::DrawDynVB(int nOffs, int Pool, int nVerts)
{
  if (nVerts <= 0 || nOffs < 0 || nOffs + nVerts > (int)m_DynVB.size() || !m_pDevice)
    return;
  const SVertex *pVerts = m_DynVB.data() + nOffs;
  SPipelineKey Key;
  SDrawConstants dc;
  if (!m_bFontState)
  {
    ApplyRenderState(Key, dc, VERTEX_FORMAT, true);
    SetMatrix(dc.mWorldViewProj, GetImmediateViewProj());
    DrawImmediate(Key, dc, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, VERTEX_FORMAT, pVerts, nVerts);
    return;
  }

  // Text: positions in back buffer pixels, alpha = coverage * vertex alpha, blended, no depth.
  Key = {};
  Key.nState = GS_NODEPTHTEST | GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA;
  Key.nProgram = eD3D12P_General;
  Key.nVertexFormat = VERTEX_FORMAT;
  Key.nCull = R_CULL_NONE;
  dc = DefaultDrawConstants();
  dc.nFlags = DF_TEX0 | DF_VCOLOR | DF_TEX0_ALPHA_ONLY | DF_ALPHATEST;
  dc.fAlphaRef = 1.0f / 255.0f;
  dc.nTex0 = m_pTextures ? m_pTextures->GetSRV(m_nFontTexture) : D3D12_SRV_WHITE;
  dc.nSamplers = XR_SAMPLER_LINEAR_CLAMP;
  const int nBufW = m_pDevice->GetWidth(), nBufH = m_pDevice->GetHeight();
  SetMatrix(dc.mWorldViewProj, MakeOrtho((float)nBufW, (float)nBufH));
  int SavedViewport[4];
  memcpy(SavedViewport, m_Viewport, sizeof(m_Viewport));
  m_Viewport[0] = m_Viewport[1] = 0;
  m_Viewport[2] = nBufW;
  m_Viewport[3] = nBufH;
  DrawImmediate(Key, dc, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, VERTEX_FORMAT, pVerts, nVerts);
  memcpy(m_Viewport, SavedViewport, sizeof(m_Viewport));
}

void CD3D12Renderer::DrawDynVB(struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F *pBuf, ushort *pInds, int nVerts, int nInds, int nPrimType)
{
  if (!pBuf || nVerts <= 0)
    return;
  SPipelineKey Key;
  SDrawConstants dc;
  ApplyRenderState(Key, dc, VERTEX_FORMAT, true);
  SetMatrix(dc.mWorldViewProj, GetImmediateViewProj());
  const int nCount = pInds && nInds > 0 ? nInds : nVerts;
  const ushort *pIndices = pInds && nInds > 0 ? pInds : nullptr;
  switch (nPrimType)
  {
  case R_PRIMV_TRIANGLE_STRIP:
    DrawImmediate(Key, dc, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, VERTEX_FORMAT, pBuf, nVerts, pIndices, pIndices ? nCount : 0);
    break;
  case R_PRIMV_TRIANGLE_FAN:
  case R_PRIMV_QUADS:
  {
    const std::vector<ushort> List = nPrimType == R_PRIMV_QUADS ? QuadsToList(pIndices, nCount) : FanToList(pIndices, nCount);
    DrawImmediate(Key, dc, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, VERTEX_FORMAT, pBuf, nVerts, List.data(), (int)List.size());
    break;
  }
  default:
    DrawImmediate(Key, dc, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, VERTEX_FORMAT, pBuf, nVerts, pIndices, pIndices ? nCount : 0);
    break;
  }
}

void CD3D12Renderer::DrawTriStrip(CVertexBuffer *src, int vert_num)
{
  if (!src || vert_num < 3 || !src->m_VS[VSF_GENERAL].m_VData)
    return;
  SPipelineKey Key;
  SDrawConstants dc;
  ApplyRenderState(Key, dc, src->m_vertexformat, true);
  SetMatrix(dc.mWorldViewProj, GetImmediateViewProj());
  DrawImmediate(Key, dc, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, src->m_vertexformat, src->m_VS[VSF_GENERAL].m_VData, vert_num);
}

bool CD3D12Renderer::FontUploadTexture(class CFBitmap*, ETEX_Format eTF)
{
  return false;
}

void CD3D12Renderer::FontReleaseTexture(class CFBitmap *pBmp)
{
}

void CD3D12Renderer::FontSetTexture(class CFBitmap*, int nFilterMode)
{
}

void CD3D12Renderer::FontSetTexture(int nTexId, int nFilterMode)
{
  m_nFontTexture = nTexId;
}

void CD3D12Renderer::FontSetRenderingState(unsigned long nVirtualScreenWidth, unsigned long nVirtualScreenHeight)
{
  m_bFontState = true;
  m_fFontScreenW = (float)nVirtualScreenWidth;
  m_fFontScreenH = (float)nVirtualScreenHeight;
}

void CD3D12Renderer::FontSetBlending(int src, int dst)
{
}

void CD3D12Renderer::FontRestoreRenderingState()
{
  m_bFontState = false;
}

void CD3D12Renderer::DrawText800(float x, float y, float fSize, const float *pRGBA, bool bCenter, const char *szText)
{
  IFFont *pFont = GetDefaultFont();
  if (!pFont || !szText || !*szText)
    return;
  const float fPixels = 12.0f * (fSize > 0 ? fSize : 1.0f);
  pFont->UseRealPixels(false);
  pFont->SetSameSize(false);
  pFont->SetSize(vector2f(fPixels, fPixels));
  pFont->SetColor(pRGBA ? color4f(pRGBA[0], pRGBA[1], pRGBA[2], pRGBA[3]) : color4f(1, 1, 1, 1));
  if (bCenter)
    x -= pFont->GetTextSize(szText).x * 0.5f;
  pFont->DrawString(x, y, szText);
}

void CD3D12Renderer::WriteXY(CXFont *currfont,int x,int y, float xscale,float yscale,float r,float g,float b,float a,const char *message, ...)
{
  va_list Args;
  va_start(Args, message);
  const std::string Text = FormatV(message, Args);
  va_end(Args);
  const float Color[4] = { r, g, b, a };
  DrawText800((float)x, (float)y, xscale, Color, false, Text.c_str());
}

void CD3D12Renderer::Draw2dText(float posX,float posY,const char *szText,SDrawTextInfo &info)
{
  DrawText800(posX, posY, info.xscale, info.color, false, szText);
}

void CD3D12Renderer::TextToScreen(float x, float y, const char * format, ...)
{
  // x, y in percent of the screen.
  va_list Args;
  va_start(Args, format);
  const std::string Text = FormatV(format, Args);
  va_end(Args);
  DrawText800(x * 8.0f, y * 6.0f, 1, nullptr, false, Text.c_str());
}

void CD3D12Renderer::TextToScreenColor(int x, int y, float r, float g, float b, float a, const char * format, ...)
{
  va_list Args;
  va_start(Args, format);
  const std::string Text = FormatV(format, Args);
  va_end(Args);
  const float Color[4] = { r, g, b, a };
  DrawText800(x * 8.0f, y * 6.0f, 1, Color, false, Text.c_str());
}

void CD3D12Renderer::DrawLabel(Vec3 pos, float font_size, const char * label_text, ...)
{
  va_list Args;
  va_start(Args, label_text);
  const std::string Text = FormatV(label_text, Args);
  va_end(Args);
  static const float White[4] = { 1, 1, 1, 1 };
  DrawLabelEx(pos, font_size, const_cast<float *>(White), true, true, "%s", Text.c_str());
}

void CD3D12Renderer::DrawLabelEx(Vec3 pos, float font_size, float * pfColor, bool bFixedSize, bool bCenter, const char * label_text, ...)
{
  va_list Args;
  va_start(Args, label_text);
  const std::string Text = FormatV(label_text, Args);
  va_end(Args);
  float sx, sy, sz;
  ProjectToScreen(pos.x, pos.y, pos.z, &sx, &sy, &sz);
  if (sz < 0 || sz > 1)
    return;
  DrawText800(sx * 8.0f, sy * 6.0f, font_size, pfColor, bCenter, Text.c_str());
}

void CD3D12Renderer::Draw2dLabel(float x,float y, float font_size, float * pfColor, bool bCenter, const char * label_text, ...)
{
  // x, y in pixels of the screen.
  va_list Args;
  va_start(Args, label_text);
  const std::string Text = FormatV(label_text, Args);
  va_end(Args);
  const float fW = (float)std::max(m_nWidth, 1), fH = (float)std::max(m_nHeight, 1);
  DrawText800(x * 800.0f / fW, y * 600.0f / fH, font_size, pfColor, bCenter, Text.c_str());
}

void CD3D12Renderer::FlushTextMessages()
{
}

//////////////////////////////////////////////////////////////////////////
// Shader items in 2D (not drawn)

void CD3D12Renderer::EF_AddPolyToScene2D(int Ef, int numPts, SColorVert2D *verts)
{
}

void CD3D12Renderer::EF_AddPolyToScene2D(SShaderItem si, int nTempl, int numPts, SColorVert2D *verts)
{
}

void CD3D12Renderer::EF_EndEf2D(bool bSort)
{
}

bool CD3D12Renderer::EF_DrawEfForName(char *name, float x, float y, float width, float height, CFColor& col, int nTempl)
{
  return false;
}

bool CD3D12Renderer::EF_DrawEfForNum(int num, float x, float y, float width, float height, CFColor& col, int nTempl)
{
  return false;
}

bool CD3D12Renderer::EF_DrawEf(IShader *ef, float x, float y, float width, float height, CFColor& col, int nTempl)
{
  return false;
}

bool CD3D12Renderer::EF_DrawEf(SShaderItem si, float x, float y, float width, float height, CFColor& col, int nTempl)
{
  return false;
}

bool CD3D12Renderer::EF_DrawPartialEfForName(char *name, SVrect *vr, SVrect *pr, CFColor& col)
{
  return false;
}

bool CD3D12Renderer::EF_DrawPartialEfForNum(int num, SVrect *vr, SVrect *pr, CFColor& col)
{
  return false;
}

bool CD3D12Renderer::EF_DrawPartialEf(IShader *ef, SVrect *vr, SVrect *pr, CFColor& col, float iwdt, float ihgt)
{
  return false;
}

void CD3D12Renderer::DrawLabelImage(const Vec3 &vPos,float fSize,int nTextureId)
{
}

void CD3D12Renderer::Graph(byte *g, int x, int y, int wdt, int hgt, int nC, int type, char *text, CFColor& color, float fScale)
{
}

//////////////////////////////////////////////////////////////////////////
// Lines, boxes, points, balls, quads

void CD3D12Renderer::DrawLineColor(const Vec3 & vPos1, const CFColor & vColor1, const Vec3 & vPos2, const CFColor & vColor2)
{
  const SVertex Verts[2] = { MakeVertex(vPos1, MakeColor(vColor1)), MakeVertex(vPos2, MakeColor(vColor2)) };
  SPipelineKey Key;
  SDrawConstants dc;
  ApplyRenderState(Key, dc, VERTEX_FORMAT, true);
  dc.nFlags &= ~(DF_TEX0 | DF_TEXGEN_WORLD | DF_TEXMATRIX);
  Key.nCull = R_CULL_NONE;
  SetMatrix(dc.mWorldViewProj, GetImmediateViewProj());
  DrawImmediate(Key, dc, D3D_PRIMITIVE_TOPOLOGY_LINELIST, VERTEX_FORMAT, Verts, 2);
}

void CD3D12Renderer::DrawLine(const Vec3 & vPos1, const Vec3 & vPos2)
{
  const CFColor White(1, 1, 1, 1);
  DrawLineColor(vPos1, White, vPos2, White);
}

void CD3D12Renderer::Draw2dLine(float x1,float y1,float x2,float y2)
{
  // The 800 x 600 virtual screen.
  const bool b2DMode = m_b2DMode;
  const float fW = m_fOrthoWidth, fH = m_fOrthoHeight;
  Set2DMode(true, 800, 600);
  DrawLine(Vec3(x1, y1, 0), Vec3(x2, y2, 0));
  m_b2DMode = b2DMode;
  m_fOrthoWidth = fW;
  m_fOrthoHeight = fH;
}

void CD3D12Renderer::Draw3dBBox(const Vec3 &mins,const Vec3 &maxs, int nPrimType)
{
  m_QueuedBoxes.push_back({ mins, maxs, nPrimType });
}

void CD3D12Renderer::Draw3dPrim(const Vec3 &mins,const Vec3 &maxs, int nPrimType, const float* fRGBA)
{
  DrawBox(mins, maxs, nPrimType, fRGBA);
}

void CD3D12Renderer::DrawQueuedBoxes()
{
  for (const SQueuedBox &Box : m_QueuedBoxes)
    DrawBox(Box.vMin, Box.vMax, Box.nPrimType, nullptr);
  m_QueuedBoxes.clear();
}

void CD3D12Renderer::DrawBox(const Vec3 &vMin, const Vec3 &vMax, int nPrimType, const float *pRGBA)
{
  // Untextured, in the colour (the material colour by default), depth tested and written, no
  // culling or blending, with the camera of the moment.
  SPipelineKey Key = {};
  Key.nState = GS_DEPTHWRITE;
  Key.nProgram = eD3D12P_General;
  Key.nVertexFormat = VERTEX_FORMAT;
  Key.nCull = R_CULL_NONE;
  Key.bWireframe = m_nPolygonMode == R_WIREFRAME_MODE ? 1 : 0;
  SDrawConstants dc = DefaultDrawConstants();
  const float *pColor = pRGBA ? pRGBA : m_MaterialColor;
  memcpy(dc.vColor, pColor, sizeof(dc.vColor));
  SetMatrix(dc.mWorldViewProj, m_ViewMatrix * m_ProjMatrix);

  const UCol White = MakeColor(1, 1, 1, 1);
  if (nPrimType == DPRIM_LINE)
  {
    const SVertex Verts[2] = { MakeVertex(vMin, White), MakeVertex(vMax, White) };
    DrawImmediate(Key, dc, D3D_PRIMITIVE_TOPOLOGY_LINELIST, VERTEX_FORMAT, Verts, 2);
    return;
  }
  // Corner i: x from bit 0, y from bit 1, z from bit 2.
  SVertex Verts[8];
  for (int i = 0; i < 8; i++)
    Verts[i] = MakeVertex((i & 1) ? vMax.x : vMin.x, (i & 2) ? vMax.y : vMin.y, (i & 4) ? vMax.z : vMin.z, White);
  if (nPrimType == DPRIM_SOLID_BOX || nPrimType == DPRIM_SOLID_SPHERE)
  {
    static const ushort Faces[36] =
    {
      0, 2, 3, 0, 3, 1,   4, 5, 7, 4, 7, 6,   0, 1, 5, 0, 5, 4,
      2, 6, 7, 2, 7, 3,   0, 4, 6, 0, 6, 2,   1, 3, 7, 1, 7, 5,
    };
    DrawImmediate(Key, dc, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, VERTEX_FORMAT, Verts, 8, Faces, 36);
    return;
  }
  static const ushort Edges[24] =
  {
    0, 1, 2, 3, 4, 5, 6, 7,   0, 2, 1, 3, 4, 6, 5, 7,   0, 4, 1, 5, 2, 6, 3, 7,
  };
  DrawImmediate(Key, dc, D3D_PRIMITIVE_TOPOLOGY_LINELIST, VERTEX_FORMAT, Verts, 8, Edges, 24);
}

void CD3D12Renderer::DrawBall(float x, float y, float z, float radius)
{
  DrawBall(Vec3(x, y, z), radius);
}

void CD3D12Renderer::DrawBall(const Vec3 & pos, float radius)
{
  // A unit sphere (z up) scaled and moved, unlit, with the bound texture; outer faces counter-
  // clockwise, back faces culled.
  constexpr int RINGS = 11, SEGMENTS = 22;
  const UCol White = MakeColor(1, 1, 1, 1);
  std::vector<SVertex> Verts;
  Verts.reserve((RINGS + 1) * (SEGMENTS + 1));
  for (int i = 0; i <= RINGS; i++)
  {
    const float fTheta = gf_PI * i / RINGS;
    for (int j = 0; j <= SEGMENTS; j++)
    {
      const float fPhi = 2 * gf_PI * j / SEGMENTS;
      const Vec3 n(sinf(fTheta) * cosf(fPhi), sinf(fTheta) * sinf(fPhi), cosf(fTheta));
      Verts.push_back(MakeVertex(pos + n * radius, White, (float)j / SEGMENTS, (float)i / RINGS));
    }
  }
  std::vector<ushort> Inds;
  Inds.reserve(RINGS * SEGMENTS * 6);
  for (int i = 0; i < RINGS; i++)
  {
    for (int j = 0; j < SEGMENTS; j++)
    {
      const ushort a = (ushort)(i * (SEGMENTS + 1) + j), b = (ushort)(a + 1);
      const ushort c = (ushort)(a + SEGMENTS + 1), d = (ushort)(c + 1);
      for (ushort k : { a, c, d, a, d, b })
        Inds.push_back(k);
    }
  }
  SPipelineKey Key;
  SDrawConstants dc;
  ApplyRenderState(Key, dc, VERTEX_FORMAT, false);
  Key.nCull = R_CULL_BACK;
  SetMatrix(dc.mWorldViewProj, GetImmediateViewProj());
  DrawImmediate(Key, dc, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, VERTEX_FORMAT, Verts.data(), (int)Verts.size(), Inds.data(), (int)Inds.size());
}

void CD3D12Renderer::DrawPoint(float x, float y, float z, float fSize)
{
  // A square of fSize pixels (at least 1) at the projected point, vertex colour 0.
  if (m_Viewport[2] <= 0 || m_Viewport[3] <= 0)
    return;
  const Matrix44 ViewProj = GetImmediateViewProj();
  float Clip[4];
  for (int j = 0; j < 4; j++)
    Clip[j] = x * ViewProj.data[j] + y * ViewProj.data[4 + j] + z * ViewProj.data[8 + j] + ViewProj.data[12 + j];
  if (Clip[3] <= 0)
    return;
  const float px = (Clip[0] / Clip[3] * 0.5f + 0.5f) * m_Viewport[2];
  const float py = (0.5f - Clip[1] / Clip[3] * 0.5f) * m_Viewport[3];
  const float pz = Clip[2] / Clip[3];
  if (pz < 0 || pz > 1)
    return;
  const float fHalf = std::max(fSize, 1.0f) * 0.5f;
  const UCol Black = MakeColor(0, 0, 0, 0);
  const SVertex Verts[4] =
  {
    MakeVertex(px - fHalf, py - fHalf, pz, Black), MakeVertex(px + fHalf, py - fHalf, pz, Black),
    MakeVertex(px + fHalf, py + fHalf, pz, Black), MakeVertex(px - fHalf, py + fHalf, pz, Black),
  };
  static const ushort Inds[6] = { 0, 1, 2, 0, 2, 3 };
  SPipelineKey Key;
  SDrawConstants dc;
  ApplyRenderState(Key, dc, VERTEX_FORMAT, true);
  dc.nFlags &= ~(DF_TEX0 | DF_TEXGEN_WORLD | DF_TEXMATRIX | DF_FOG);
  Key.nCull = R_CULL_NONE;
  Key.bWireframe = 0;
  SetMatrix(dc.mWorldViewProj, MakeOrtho((float)m_Viewport[2], (float)m_Viewport[3]));
  DrawImmediate(Key, dc, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, VERTEX_FORMAT, Verts, 4, Inds, 6);
}

void CD3D12Renderer::DrawQuad(const Vec3 &right, const Vec3 &up, const Vec3 &origin,int nFlipMode)
{
  // origin is the centre; texture row 0 at the top (origin + up).
  const UCol White = MakeColor(1, 1, 1, 1);
  const SVertex Verts[4] =
  {
    MakeVertex(origin - right + up, White, 0, 0), MakeVertex(origin + right + up, White, 1, 0),
    MakeVertex(origin + right - up, White, 1, 1), MakeVertex(origin - right - up, White, 0, 1),
  };
  static const ushort Inds[6] = { 0, 1, 2, 0, 2, 3 };
  SPipelineKey Key;
  SDrawConstants dc;
  ApplyRenderState(Key, dc, VERTEX_FORMAT, true);
  Key.nCull = R_CULL_NONE;
  SetMatrix(dc.mWorldViewProj, GetImmediateViewProj());
  DrawImmediate(Key, dc, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, VERTEX_FORMAT, Verts, 4, Inds, 6);
}

void CD3D12Renderer::DrawQuad(float dy,float dx, float dz, float x, float y, float z)
{
  // A quad about (x, y, z), spanning dx and dy, tilted by dz.
  DrawQuad(Vec3(dx, 0, 0), Vec3(0, dy, dz), Vec3(x, y, z));
}
