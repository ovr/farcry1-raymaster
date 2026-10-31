// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: UpscalerFsr.cpp
//	Description: AMD FSR Upscaling (d3d12_Upscaler fsr) through the AMD
//	FSR SDK (third_party/amd-fsr-sdk): its loader DLL, loaded at run time
//	from the folder of this renderer, finds the upscaler DLL next to it
//	(FSR 4 on the GPUs it supports, else FSR 3.1).
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Upscaler.h"
#include "api/include/ffx_api.h"
#include "api/include/ffx_api_loader.h"
#include "api/include/dx12/ffx_api_dx12.h"
#include "upscalers/include/ffx_upscale.h"

namespace
{

//! FSR's messages to the engine's log.
void LogMessage(uint32_t nType, const wchar_t *szMessage)
{
  char sz[1024];
  if (!WideCharToMultiByte(CP_UTF8, 0, szMessage ? szMessage : L"", -1, sz, sizeof(sz), nullptr, nullptr))
    sz[0] = 0;
  if (nType == FFX_API_MESSAGE_TYPE_ERROR)
    iLog->LogError("[FSR] %s", sz);
  else
    iLog->LogWarning("[FSR] %s", sz);
}

//! The folder of this DLL, with the separator at the end.
std::wstring GetModuleFolder()
{
  HMODULE hModule = nullptr;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&GetModuleFolder, &hModule);
  wchar_t szPath[MAX_PATH];
  const DWORD n = GetModuleFileNameW(hModule, szPath, MAX_PATH);
  const std::wstring Path(szPath, n < MAX_PATH ? n : 0);
  const size_t nSlash = Path.find_last_of(L"\\/");
  return nSlash == std::wstring::npos ? std::wstring() : Path.substr(0, nSlash + 1);
}

class CUpscalerFsr final : public IUpscaler
{
public:
  ~CUpscalerFsr() override
  {
    if (m_Context && m_Ffx.DestroyContext)
      m_Ffx.DestroyContext(&m_Context, nullptr);
    if (m_hLoader)
      FreeLibrary(m_hLoader);
  }

  bool Create(ID3D12Device *pDevice, int nWidth, int nHeight, bool bDebug, int nVersion) override
  {
    const std::wstring Loader = GetModuleFolder() + L"amd_fidelityfx_loader_dx12.dll";
    m_hLoader = LoadLibraryExW(Loader.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!m_hLoader)
    {
      iLog->LogWarning("XRenderD3D12: AMD FSR: amd_fidelityfx_loader_dx12.dll is not next to the renderer (error %u)", (unsigned)GetLastError());
      return false;
    }
    ffxLoadFunctions(&m_Ffx, m_hLoader);
    if (!m_Ffx.CreateContext || !m_Ffx.DestroyContext || !m_Ffx.Query || !m_Ffx.Dispatch)
    {
      iLog->LogWarning("XRenderD3D12: AMD FSR: amd_fidelityfx_loader_dx12.dll lacks the ffx API");
      return false;
    }

    ffxCreateContextDescUpscale Desc = {};
    Desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    // The scene's colours are those of the back buffer (LDR, gamma encoded); no exposure of ours.
    Desc.flags = FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE | FFX_UPSCALE_ENABLE_AUTO_EXPOSURE | FFX_UPSCALE_ENABLE_DEBUG_VISUALIZATION |
                 (bDebug ? FFX_UPSCALE_ENABLE_DEBUG_CHECKING : 0);
    Desc.maxRenderSize = { (uint32_t)nWidth, (uint32_t)nHeight };
    Desc.maxUpscaleSize = { (uint32_t)nWidth, (uint32_t)nHeight };
    Desc.fpMessage = LogMessage;
    ffxCreateBackendDX12Desc Backend = {};
    Backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
    Backend.device = pDevice;
    ffxCreateContextDescUpscaleVersion Version = {};
    Version.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION;
    Version.version = FFX_UPSCALER_VERSION;
    Desc.header.pNext = &Backend.header;
    Backend.header.pNext = &Version.header;
    // A major version asked for: the newest of it among the upscaler's versions (their names are
    // "4.1.2", "3.1.5", ...).
    ffxOverrideVersion Override = {};
    if (nVersion > 0)
    {
      uint64_t nCount = 0;
      ffxQueryDescGetVersions Versions = {};
      Versions.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
      Versions.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
      Versions.device = pDevice;
      Versions.outputCount = &nCount;
      std::vector<uint64_t> Ids;
      std::vector<const char *> Names;
      if (m_Ffx.Query(nullptr, &Versions.header) == FFX_API_RETURN_OK && nCount > 0)
      {
        Ids.resize((size_t)nCount);
        Names.resize((size_t)nCount);
        Versions.versionIds = Ids.data();
        Versions.versionNames = Names.data();
        if (m_Ffx.Query(nullptr, &Versions.header) != FFX_API_RETURN_OK)
          nCount = 0;
      }
      std::string List;
      int nBest[2] = { -1, -1 };
      for (size_t i = 0; i < (size_t)nCount; i++)
      {
        const char *szName = Names[i] ? Names[i] : "";
        List += (i ? ", " : "") + std::string(szName);
        int nMajor = 0, nMinor = 0, nPatch = 0;
        if (sscanf_s(szName, "%d.%d.%d", &nMajor, &nMinor, &nPatch) < 1 || nMajor != nVersion)
          continue;
        if (nMinor > nBest[0] || (nMinor == nBest[0] && nPatch > nBest[1]))
        {
          nBest[0] = nMinor;
          nBest[1] = nPatch;
          Override.versionId = Ids[i];
        }
      }
      if (Override.versionId)
      {
        Override.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION;
        Version.header.pNext = &Override.header;
      }
      else
        iLog->LogWarning("XRenderD3D12: AMD FSR: no version %d (%s); the default", nVersion, List.empty() ? "none listed" : List.c_str());
    }
    if (const ffxReturnCode_t nCode = m_Ffx.CreateContext(&m_Context, &Desc.header, nullptr); nCode != FFX_API_RETURN_OK)
    {
      iLog->LogWarning("XRenderD3D12: AMD FSR: no upscaling context (error %u)", (unsigned)nCode);
      m_Context = nullptr;
      return false;
    }
    m_nWidth = nWidth;
    m_nHeight = nHeight;

    ffxQueryGetProviderVersion Provider = {};
    Provider.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
    m_Name = "AMD FSR Upscaling";
    if (m_Ffx.Query(&m_Context, &Provider.header) == FFX_API_RETURN_OK && Provider.versionName)
      m_Name += std::string(" ") + Provider.versionName;
    return true;
  }

  void GetRenderSize(int nMode, int &nWidth, int &nHeight) override
  {
    uint32_t w = (uint32_t)m_nWidth, h = (uint32_t)m_nHeight;
    ffxQueryDescUpscaleGetRenderResolutionFromQualityMode Query = {};
    Query.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETRENDERRESOLUTIONFROMQUALITYMODE;
    Query.displayWidth = (uint32_t)m_nWidth;
    Query.displayHeight = (uint32_t)m_nHeight;
    Query.qualityMode = (uint32_t)std::clamp(nMode, 0, eUM_Num - 1);
    Query.pOutRenderWidth = &w;
    Query.pOutRenderHeight = &h;
    if (m_Ffx.Query(&m_Context, &Query.header) != FFX_API_RETURN_OK || !w || !h)
      w = (uint32_t)m_nWidth, h = (uint32_t)m_nHeight;
    nWidth = (int)w;
    nHeight = (int)h;
  }

  void GetJitter(int nFrame, int nRenderWidth, float &x, float &y) override
  {
    int32_t nPhases = 0;
    ffxQueryDescUpscaleGetJitterPhaseCount Count = {};
    Count.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTERPHASECOUNT;
    Count.renderWidth = (uint32_t)nRenderWidth;
    Count.displayWidth = (uint32_t)m_nWidth;
    Count.pOutPhaseCount = &nPhases;
    x = y = 0;
    if (m_Ffx.Query(&m_Context, &Count.header) != FFX_API_RETURN_OK || nPhases <= 0)
      return;
    ffxQueryDescUpscaleGetJitterOffset Offset = {};
    Offset.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTEROFFSET;
    Offset.index = nFrame % nPhases;
    Offset.phaseCount = nPhases;
    Offset.pOutX = &x;
    Offset.pOutY = &y;
    if (m_Ffx.Query(&m_Context, &Offset.header) != FFX_API_RETURN_OK)
      x = y = 0;
  }

  bool Dispatch(const SUpscaleInputs &In) override
  {
    constexpr uint32_t READ = FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ;
    ffxDispatchDescUpscale Desc = {};
    Desc.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
    Desc.commandList = In.pCmdList;
    Desc.color = ffxApiGetResourceDX12(In.pColor, READ);
    Desc.depth = ffxApiGetResourceDX12(In.pDepth, READ);
    Desc.motionVectors = ffxApiGetResourceDX12(In.pVelocity, READ);
    Desc.output = ffxApiGetResourceDX12(In.pOutput, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    Desc.jitterOffset = { In.fJitterX, In.fJitterY };
    Desc.motionVectorScale = { (float)In.nRenderWidth, (float)In.nRenderHeight };
    Desc.renderSize = { (uint32_t)In.nRenderWidth, (uint32_t)In.nRenderHeight };
    Desc.upscaleSize = { (uint32_t)m_nWidth, (uint32_t)m_nHeight };
    Desc.enableSharpening = In.fSharpness > 0;
    Desc.sharpness = std::clamp(In.fSharpness, 0.0f, 1.0f);
    Desc.frameTimeDelta = In.fFrameMs;
    Desc.preExposure = 1;
    Desc.reset = In.bReset;
    Desc.cameraNear = In.fNear;
    Desc.cameraFar = In.fFar;
    Desc.cameraFovAngleVertical = In.fFovY;
    Desc.viewSpaceToMetersFactor = 1;     // the engine's units are metres
    Desc.flags = FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB | (In.bDebugView ? FFX_UPSCALE_FLAG_DRAW_DEBUG_VIEW : 0);
    const ffxReturnCode_t nCode = m_Ffx.Dispatch(&m_Context, &Desc.header);
    if (nCode != FFX_API_RETURN_OK && !m_bDispatchFailed)
    {
      m_bDispatchFailed = true;
      iLog->LogError("XRenderD3D12: AMD FSR: the upscale failed (error %u)", (unsigned)nCode);
    }
    return nCode == FFX_API_RETURN_OK;
  }

  const char *GetName() const override { return m_Name.c_str(); }

private:
  HMODULE m_hLoader = nullptr;
  ffxFunctions m_Ffx = {};
  ffxContext m_Context = nullptr;
  int m_nWidth = 0, m_nHeight = 0;    //!< the display size of the context
  std::string m_Name;
  bool m_bDispatchFailed = false;
};

}

std::unique_ptr<IUpscaler> CreateUpscalerFsr()
{
  return std::make_unique<CUpscalerFsr>();
}
