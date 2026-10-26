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

void D3DState_WriteDevice(IDirect3DDevice9 *pDev)
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
  if (pVS)
    pVS->Release();
  if (pPS)
    pPS->Release();
}
