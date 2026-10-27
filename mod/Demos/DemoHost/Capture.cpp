//////////////////////////////////////////////////////////////////////
//
//	File: Capture.cpp
//	Description: Reads and saves the back buffer of the current frame.
//
//	Called before ISystem::RenderEnd, which presents the frame. The
//	Direct3D 9 device comes from the renderer (EF_Query(EFQ_D3DDevice));
//	a renderer without one is read with IRenderer::ReadFrameBuffer.
//
//////////////////////////////////////////////////////////////////////

#include "DemoHost.h"
#include "Capture.h"
#include "Tga.h"
#include <d3d9.h>

//! The device behind the renderer, if its vtable lies in d3d9.dll.
static IDirect3DDevice9 *GetDevice(IRenderer *pRenderer)
{
  void *p = pRenderer->EF_Query(EFQ_D3DDevice);
  HMODULE hD3D9 = GetModuleHandleA("d3d9.dll");
  HMODULE hModule = NULL;
  if (!p || !hD3D9 || IsBadReadPtr(p, sizeof(void *)) || IsBadReadPtr(*(void **)p, sizeof(void *)))
    return NULL;
  if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
    (LPCSTR)**(void ***)p, &hModule) || hModule != hD3D9)
    return NULL;
  return (IDirect3DDevice9 *)p;
}

static bool ReadFromDevice(IDirect3DDevice9 *pDev, int &nWidth, int &nHeight, std::vector<unsigned char> &Pixels)
{
  IDirect3DSurface9 *pBack = NULL, *pCopy = NULL;
  if (FAILED(pDev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pBack)))
    return false;
  D3DSURFACE_DESC Desc = {};
  pBack->GetDesc(&Desc);
  D3DLOCKED_RECT Rect = {};
  bool bOk = false;
  if ((Desc.Format == D3DFMT_X8R8G8B8 || Desc.Format == D3DFMT_A8R8G8B8) && Desc.MultiSampleType == D3DMULTISAMPLE_NONE
    && SUCCEEDED(pDev->CreateOffscreenPlainSurface(Desc.Width, Desc.Height, Desc.Format, D3DPOOL_SYSTEMMEM, &pCopy, NULL))
    && SUCCEEDED(pDev->GetRenderTargetData(pBack, pCopy)) && SUCCEEDED(pCopy->LockRect(&Rect, NULL, D3DLOCK_READONLY)))
  {
    // X8R8G8B8 has no alpha: the frames get 255, so that two renderers compare equal.
    nWidth = Desc.Width;
    nHeight = Desc.Height;
    Pixels.resize(Desc.Width * Desc.Height * 4);
    for (UINT y = 0; y < Desc.Height; y++)
    {
      const unsigned char *pSrc = (const unsigned char *)Rect.pBits + y * Rect.Pitch;
      unsigned char *pDst = &Pixels[y * Desc.Width * 4];
      memcpy(pDst, pSrc, Desc.Width * 4);
      for (UINT x = 0; x < Desc.Width; x++)
        pDst[x * 4 + 3] = 255;
    }
    pCopy->UnlockRect();
    bOk = true;
  }
  if (pCopy)
    pCopy->Release();
  pBack->Release();
  return bOk;
}

static bool ReadFromRenderer(IRenderer *pRenderer, int &nWidth, int &nHeight, std::vector<unsigned char> &Pixels)
{
  nWidth = pRenderer->GetWidth();
  nHeight = pRenderer->GetHeight();
  if (nWidth <= 0 || nHeight <= 0)
    return false;
  Pixels.resize(nWidth * nHeight * 4);
  pRenderer->ReadFrameBuffer(Pixels.data(), nWidth, nHeight, true, true);
  for (size_t i = 0; i < Pixels.size(); i += 4)
  {
    unsigned char r = Pixels[i];
    Pixels[i] = Pixels[i + 2];
    Pixels[i + 2] = r;
    Pixels[i + 3] = 255;
  }
  return true;
}

bool Capture_ReadFrame(IRenderer *pRenderer, int &nWidth, int &nHeight, std::vector<unsigned char> &Pixels)
{
  IDirect3DDevice9 *pDev = GetDevice(pRenderer);
  return pDev ? ReadFromDevice(pDev, nWidth, nHeight, Pixels) : ReadFromRenderer(pRenderer, nWidth, nHeight, Pixels);
}

bool Capture_SaveFrame(IRenderer *pRenderer, const char *szFile)
{
  int nWidth = 0, nHeight = 0;
  std::vector<unsigned char> Pixels;
  return Capture_ReadFrame(pRenderer, nWidth, nHeight, Pixels) && Tga_Write(szFile, nWidth, nHeight, Pixels.data(), nWidth * 4);
}
