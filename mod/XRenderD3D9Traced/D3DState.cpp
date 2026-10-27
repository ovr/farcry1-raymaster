//////////////////////////////////////////////////////////////////////
//
//	File: D3DState.cpp
//	Description: Direct3D 9 state of the original renderer after a call.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include <d3d9.h>
#include "RenderTrace.h"
#include "D3DState.h"

IDirect3DDevice9 *D3DState_GetDevice(IRenderer *pRenderer)
{
  static IDirect3DDevice9 *s_pDevice = NULL;
  static bool s_bQueried = false;
  if (s_bQueried)
    return s_pDevice;
  void *p = pRenderer->EF_Query(EFQ_D3DDevice);
  // The vtable of a device created by d3d9.dll is in d3d9.dll.
  HMODULE hD3D9 = GetModuleHandleA("d3d9.dll");
  HMODULE hModule = NULL;
  if (p && hD3D9 && Trace_IsReadable(p, sizeof(void *)) && Trace_IsReadable(*(void **)p, sizeof(void *))
    && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)**(void ***)p, &hModule)
    && hModule == hD3D9)
    s_pDevice = (IDirect3DDevice9 *)p;
  // The renderer only knows a device after Init; ask again on the next call until it has one.
  s_bQueried = s_pDevice != NULL;
  if (!s_pDevice)
    Trace_Write("    d3d: EF_Query(EFQ_D3DDevice) -> %p, not a Direct3D 9 device", p);
  return s_pDevice;
}

static void Append(char *szBuf, size_t nSize, const char *szFormat, ...)
{
  size_t nLen = strlen(szBuf);
  if (nLen >= nSize - 1)
    return;
  va_list args;
  va_start(args, szFormat);
  _vsnprintf(szBuf + nLen, nSize - nLen - 1, szFormat, args);
  va_end(args);
  szBuf[nSize - 1] = 0;
}

void D3DState_Write(IRenderer *pRenderer)
{
  IDirect3DDevice9 *pDev = D3DState_GetDevice(pRenderer);
  if (pDev)
    D3DState_WriteDevice(pDev);
}

void D3DState_WriteDevice(IDirect3DDevice9 *pDev, bool bShaderConstants)
{
  static const struct { D3DRENDERSTATETYPE m_eState; const char *m_szName; } s_States[] =
  {
    { D3DRS_ZENABLE, "z" }, { D3DRS_ZWRITEENABLE, "zwrite" }, { D3DRS_ZFUNC, "zfunc" },
    { D3DRS_ALPHATESTENABLE, "atest" }, { D3DRS_ALPHAFUNC, "afunc" }, { D3DRS_ALPHAREF, "aref" },
    { D3DRS_ALPHABLENDENABLE, "blend" }, { D3DRS_SRCBLEND, "src" }, { D3DRS_DESTBLEND, "dst" },
    { D3DRS_BLENDOP, "op" }, { D3DRS_CULLMODE, "cull" }, { D3DRS_FILLMODE, "fill" },
    { D3DRS_FOGENABLE, "fog" }, { D3DRS_STENCILENABLE, "stencil" }, { D3DRS_COLORWRITEENABLE, "cwrite" },
    { D3DRS_LIGHTING, "lighting" }, { D3DRS_TEXTUREFACTOR, "tfactor" }, { D3DRS_DEPTHBIAS, "zbias" },
  };
  char szLine[1024] = "";
  for (int i = 0; i < (int)(sizeof(s_States) / sizeof(s_States[0])); i++)
  {
    DWORD nValue = 0;
    if (SUCCEEDED(pDev->GetRenderState(s_States[i].m_eState, &nValue)))
      Append(szLine, sizeof(szLine), " %s %u", s_States[i].m_szName, nValue);
    else
      Append(szLine, sizeof(szLine), " %s ?", s_States[i].m_szName);
  }
  Trace_Write("    d3d rs:%s", szLine);
  DWORD nStencil = 0;
  if (SUCCEEDED(pDev->GetRenderState(D3DRS_STENCILENABLE, &nStencil)) && nStencil)
  {
    static const struct { D3DRENDERSTATETYPE m_eState; const char *m_szName; } s_StencilStates[] =
    {
      { D3DRS_STENCILFUNC, "func" }, { D3DRS_STENCILREF, "ref" }, { D3DRS_STENCILMASK, "mask" },
      { D3DRS_STENCILWRITEMASK, "writemask" }, { D3DRS_STENCILFAIL, "fail" }, { D3DRS_STENCILZFAIL, "zfail" },
      { D3DRS_STENCILPASS, "pass" }, { D3DRS_TWOSIDEDSTENCILMODE, "twosided" }, { D3DRS_CCW_STENCILFUNC, "ccwfunc" },
      { D3DRS_CCW_STENCILFAIL, "ccwfail" }, { D3DRS_CCW_STENCILZFAIL, "ccwzfail" }, { D3DRS_CCW_STENCILPASS, "ccwpass" },
    };
    szLine[0] = 0;
    for (int i = 0; i < (int)(sizeof(s_StencilStates) / sizeof(s_StencilStates[0])); i++)
    {
      DWORD nValue = 0;
      pDev->GetRenderState(s_StencilStates[i].m_eState, &nValue);
      Append(szLine, sizeof(szLine), " %s %u", s_StencilStates[i].m_szName, nValue);
    }
    Trace_Write("    d3d stencil:%s", szLine);
  }
  DWORD nScissor = 0;
  RECT Scissor = {};
  if (SUCCEEDED(pDev->GetRenderState(D3DRS_SCISSORTESTENABLE, &nScissor)) && nScissor && SUCCEEDED(pDev->GetScissorRect(&Scissor)))
    Trace_Write("    d3d scissor: (%ld %ld)-(%ld %ld)", Scissor.left, Scissor.top, Scissor.right, Scissor.bottom);

  // Texture stages up to the first disabled one.
  for (DWORD nStage = 0; nStage < 4; nStage++)
  {
    DWORD nColorOp = 0, nColor1 = 0, nColor2 = 0, nAlphaOp = 0, nAlpha1 = 0, nAlpha2 = 0, nTexCoord = 0;
    pDev->GetTextureStageState(nStage, D3DTSS_COLOROP, &nColorOp);
    pDev->GetTextureStageState(nStage, D3DTSS_COLORARG1, &nColor1);
    pDev->GetTextureStageState(nStage, D3DTSS_COLORARG2, &nColor2);
    pDev->GetTextureStageState(nStage, D3DTSS_ALPHAOP, &nAlphaOp);
    pDev->GetTextureStageState(nStage, D3DTSS_ALPHAARG1, &nAlpha1);
    pDev->GetTextureStageState(nStage, D3DTSS_ALPHAARG2, &nAlpha2);
    pDev->GetTextureStageState(nStage, D3DTSS_TEXCOORDINDEX, &nTexCoord);
    szLine[0] = 0;
    Append(szLine, sizeof(szLine), "color %u(%u,%u) alpha %u(%u,%u) tc %u", nColorOp, nColor1, nColor2, nAlphaOp, nAlpha1, nAlpha2, nTexCoord);
    IDirect3DBaseTexture9 *pTex = NULL;
    if (SUCCEEDED(pDev->GetTexture(nStage, &pTex)) && pTex)
    {
      D3DSURFACE_DESC Desc;
      if (pTex->GetType() == D3DRTYPE_TEXTURE && SUCCEEDED(((IDirect3DTexture9 *)pTex)->GetLevelDesc(0, &Desc)))
      {
        Append(szLine, sizeof(szLine), " tex %p %ux%u format %u levels %u pool %u", pTex, Desc.Width, Desc.Height, Desc.Format,
          pTex->GetLevelCount(), Desc.Pool);
        // An alpha-only texture (the font texture): its texels not 0 and their sum.
        D3DLOCKED_RECT Rect;
        if (Desc.Format == D3DFMT_A8 && Desc.Pool == D3DPOOL_MANAGED
          && SUCCEEDED(((IDirect3DTexture9 *)pTex)->LockRect(0, &Rect, NULL, D3DLOCK_READONLY)))
        {
          int nNonZero = 0;
          unsigned __int64 nSum = 0;
          for (UINT y = 0; y < Desc.Height; y++)
          {
            const BYTE *pRow = (const BYTE *)Rect.pBits + y * Rect.Pitch;
            for (UINT x = 0; x < Desc.Width; x++)
            {
              nNonZero += pRow[x] != 0;
              nSum += pRow[x];
            }
          }
          // The middle row and the diagonal, every 8th texel (once per texture): the profile of a
          // generated texture such as the particle glow.
          static IDirect3DBaseTexture9 *s_pProfiled[16];
          static int s_nProfiled;
          bool bProfiled = false;
          for (int i = 0; i < s_nProfiled; i++)
            bProfiled |= s_pProfiled[i] == pTex;
          if (!bProfiled && s_nProfiled < 16 && Desc.Width == Desc.Height && Desc.Width <= 256)
          {
            s_pProfiled[s_nProfiled++] = pTex;
            char szRow[1024] = "", szDiag[1024] = "";
            for (UINT x = 0; x < Desc.Width; x += 8)
            {
              Append(szRow, sizeof(szRow), " %u", ((const BYTE *)Rect.pBits + (Desc.Height / 2) * Rect.Pitch)[x]);
              Append(szDiag, sizeof(szDiag), " %u", ((const BYTE *)Rect.pBits + x * Rect.Pitch)[x]);
            }
            Trace_Write("    d3d tex %p row %u:%s", pTex, Desc.Height / 2, szRow);
            Trace_Write("    d3d tex %p diagonal:%s", pTex, szDiag);
          }
          ((IDirect3DTexture9 *)pTex)->UnlockRect(0);
          Append(szLine, sizeof(szLine), " (texels not 0 %d, sum %llu)", nNonZero, nSum);
        }
      }
      else
        Append(szLine, sizeof(szLine), " tex %p type %d", pTex, pTex->GetType());
      pTex->Release();
    }
    else
      Append(szLine, sizeof(szLine), " no tex");
    DWORD nMin = 0, nMag = 0, nMip = 0, nU = 0, nV = 0;
    pDev->GetSamplerState(nStage, D3DSAMP_MINFILTER, &nMin);
    pDev->GetSamplerState(nStage, D3DSAMP_MAGFILTER, &nMag);
    pDev->GetSamplerState(nStage, D3DSAMP_MIPFILTER, &nMip);
    pDev->GetSamplerState(nStage, D3DSAMP_ADDRESSU, &nU);
    pDev->GetSamplerState(nStage, D3DSAMP_ADDRESSV, &nV);
    Append(szLine, sizeof(szLine), " filter %u/%u/%u address %u/%u", nMin, nMag, nMip, nU, nV);
    DWORD nTexTransform = 0;
    pDev->GetTextureStageState(nStage, D3DTSS_TEXTURETRANSFORMFLAGS, &nTexTransform);
    D3DMATRIX T;
    if (nTexTransform && SUCCEEDED(pDev->GetTransform((D3DTRANSFORMSTATETYPE)(D3DTS_TEXTURE0 + nStage), &T)))
      Append(szLine, sizeof(szLine), " texture transform %u [%g %g %g %g] [%g %g %g %g] [%g %g %g %g] [%g %g %g %g]", nTexTransform,
        T._11, T._12, T._13, T._14, T._21, T._22, T._23, T._24, T._31, T._32, T._33, T._34, T._41, T._42, T._43, T._44);
    Trace_Write("    d3d ts%u: %s", nStage, szLine);
    if (nColorOp == D3DTOP_DISABLE)
      break;
  }

  IDirect3DVertexShader9 *pVS = NULL;
  IDirect3DPixelShader9 *pPS = NULL;
  DWORD nFVF = 0;
  pDev->GetVertexShader(&pVS);
  pDev->GetPixelShader(&pPS);
  pDev->GetFVF(&nFVF);
  D3DVIEWPORT9 VP = {};
  pDev->GetViewport(&VP);
  Trace_Write("    d3d vs %p ps %p fvf 0x%x viewport %u %u %ux%u z %g..%g", pVS, pPS, nFVF, VP.X, VP.Y, VP.Width, VP.Height, VP.MinZ, VP.MaxZ);
  static const struct { D3DTRANSFORMSTATETYPE m_eState; const char *m_szName; } s_Transforms[] =
  {
    { D3DTS_WORLD, "world" }, { D3DTS_VIEW, "view" }, { D3DTS_PROJECTION, "proj" },
  };
  for (int i = 0; i < (int)(sizeof(s_Transforms) / sizeof(s_Transforms[0])); i++)
  {
    D3DMATRIX M;
    if (SUCCEEDED(pDev->GetTransform(s_Transforms[i].m_eState, &M)))
      Trace_Write("    d3d %s [%g %g %g %g] [%g %g %g %g] [%g %g %g %g] [%g %g %g %g]", s_Transforms[i].m_szName,
        M._11, M._12, M._13, M._14, M._21, M._22, M._23, M._24, M._31, M._32, M._33, M._34, M._41, M._42, M._43, M._44);
  }

  // Fixed function lighting: the material, the global ambient and the enabled lights.
  DWORD nLighting = 0;
  if (SUCCEEDED(pDev->GetRenderState(D3DRS_LIGHTING, &nLighting)) && nLighting)
  {
    DWORD nAmbient = 0, nColorVertex = 0, nDiffuseSrc = 0, nAmbientSrc = 0, nSpecular = 0, nNormalize = 0;
    pDev->GetRenderState(D3DRS_AMBIENT, &nAmbient);
    pDev->GetRenderState(D3DRS_COLORVERTEX, &nColorVertex);
    pDev->GetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, &nDiffuseSrc);
    pDev->GetRenderState(D3DRS_AMBIENTMATERIALSOURCE, &nAmbientSrc);
    pDev->GetRenderState(D3DRS_SPECULARENABLE, &nSpecular);
    pDev->GetRenderState(D3DRS_NORMALIZENORMALS, &nNormalize);
    D3DMATERIAL9 Mtl = {};
    pDev->GetMaterial(&Mtl);
    Trace_Write("    d3d lighting: ambient 0x%08x colorvertex %u diffuse from %u ambient from %u specular %u normalize %u"
      " material diffuse (%g %g %g %g) ambient (%g %g %g %g) specular (%g %g %g %g) power %g emissive (%g %g %g %g)",
      nAmbient, nColorVertex, nDiffuseSrc, nAmbientSrc, nSpecular, nNormalize,
      Mtl.Diffuse.r, Mtl.Diffuse.g, Mtl.Diffuse.b, Mtl.Diffuse.a, Mtl.Ambient.r, Mtl.Ambient.g, Mtl.Ambient.b, Mtl.Ambient.a,
      Mtl.Specular.r, Mtl.Specular.g, Mtl.Specular.b, Mtl.Specular.a, Mtl.Power, Mtl.Emissive.r, Mtl.Emissive.g, Mtl.Emissive.b, Mtl.Emissive.a);
    for (DWORD nLight = 0; nLight < 8; nLight++)
    {
      BOOL bEnabled = FALSE;
      D3DLIGHT9 L = {};
      if (FAILED(pDev->GetLightEnable(nLight, &bEnabled)) || !bEnabled || FAILED(pDev->GetLight(nLight, &L)))
        continue;
      Trace_Write("    d3d light %u: type %d diffuse (%g %g %g) specular (%g %g %g) ambient (%g %g %g) pos (%g %g %g) dir (%g %g %g)"
        " range %g att (%g %g %g)", nLight, L.Type, L.Diffuse.r, L.Diffuse.g, L.Diffuse.b, L.Specular.r, L.Specular.g, L.Specular.b,
        L.Ambient.r, L.Ambient.g, L.Ambient.b, L.Position.x, L.Position.y, L.Position.z, L.Direction.x, L.Direction.y, L.Direction.z,
        L.Range, L.Attenuation0, L.Attenuation1, L.Attenuation2);
    }
  }

  // The constants of the bound shaders (vs_1_1 has 96 registers, ps_2_0 32); registers that are
  // all zero are left out.
  if (bShaderConstants && (pVS || pPS))
  {
    for (int nKind = 0; nKind < 2; nKind++)
    {
      if (!(nKind ? (void *)pPS : (void *)pVS))
        continue;
      const UINT nCount = nKind ? 32 : 96;
      float fConsts[96 * 4] = {};
      HRESULT hr = nKind ? pDev->GetPixelShaderConstantF(0, fConsts, nCount) : pDev->GetVertexShaderConstantF(0, fConsts, nCount);
      if (FAILED(hr))
        continue;
      for (UINT i = 0; i < nCount; i++)
      {
        const float *c = &fConsts[i * 4];
        if (c[0] || c[1] || c[2] || c[3])
          Trace_Write("    d3d %s c%u (%g %g %g %g)", nKind ? "ps" : "vs", i, c[0], c[1], c[2], c[3]);
      }
    }
  }
  if (pVS)
    pVS->Release();
  if (pPS)
    pPS->Release();
}
