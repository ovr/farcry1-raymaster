// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: AOCacao.cpp
//	Description: AMD FidelityFX CACAO (third_party/fidelityfx-cacao) as a
//	backend of the ambient occlusion (AmbientOcclusion.h), through its own
//	Direct3D 12 host (ffx_cacao_impl.cpp, with a descriptor heap, root
//	signatures and pipeline states of its own), at the full size of the
//	depth buffer, the normals made from the depth. d3d12_AOQuality 0 to 3
//	are its LOW, MEDIUM, HIGH and HIGHEST, d3d12_AODenoise its blur passes
//	(1 to 8); the rest of its settings are its defaults.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
// The host's Direct3D 12 part, as its own files are built (CMakeLists.txt).
#define FFX_CACAO_ENABLE_D3D12
#include "ffx_cacao_impl.h"

namespace
{

class CAOCacao : public IAmbientOcclusion
{
public:
  ~CAOCacao() override { Release(); }
  bool Create(ID3D12Device *pDevice, const SAOTargets &Targets, bool bDebug) override;
  bool Dispatch(const SAOFrame &Frame) override;
  const char *GetName() const override { return "AMD FidelityFX CACAO 1.2"; }

private:
  void Release();
  FFX_CACAO_D3D12Context *Context() const { return reinterpret_cast<FFX_CACAO_D3D12Context *>(m_Memory.get()); }

  std::unique_ptr<uint8[]> m_Memory;      //!< the context (FFX_CACAO_D3D12GetContextSize, it aligns it itself)
  bool m_bContext = false;                //!< FFX_CACAO_D3D12InitContext done
  bool m_bScreen = false;                 //!< FFX_CACAO_D3D12InitScreenSizeDependentResources done
  ID3D12Resource *m_pOutput = nullptr;
  bool m_bFailedLogged = false;
};

void CAOCacao::Release()
{
  if (m_bScreen)
    FFX_CACAO_D3D12DestroyScreenSizeDependentResources(Context());
  if (m_bContext)
    FFX_CACAO_D3D12DestroyContext(Context());
  m_bScreen = m_bContext = false;
  m_Memory.reset();
  m_pOutput = nullptr;
}

bool CAOCacao::Create(ID3D12Device *pDevice, const SAOTargets &Targets, bool bDebug)
{
  Release();
  if (!pDevice || !Targets.pDepth || !Targets.pOutput || Targets.nWidth <= 0 || Targets.nHeight <= 0)
    return false;
  m_Memory = std::make_unique<uint8[]>(FFX_CACAO_D3D12GetContextSize());
  FFX_CACAO_Status Status = FFX_CACAO_D3D12InitContext(Context(), pDevice);
  if (Status != FFX_CACAO_STATUS_OK)
  {
    iLog->LogError("XRenderD3D12: FFX_CACAO_D3D12InitContext failed (%d)", (int)Status);
    Release();
    return false;
  }
  m_bContext = true;

  FFX_CACAO_D3D12ScreenSizeInfo Info = {};
  Info.width = (uint32_t)Targets.nWidth;
  Info.height = (uint32_t)Targets.nHeight;
  Info.depthBufferResource = Targets.pDepth;
  Info.depthBufferSrvDesc.Format = Targets.DepthFormat;
  Info.depthBufferSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  Info.depthBufferSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  Info.depthBufferSrvDesc.Texture2D.MipLevels = 1;
  Info.outputResource = Targets.pOutput;
  Info.outputUavDesc.Format = DXGI_FORMAT_R8_UNORM;
  Info.outputUavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  Info.useDownsampledSsao = FFX_CACAO_FALSE;
  Status = FFX_CACAO_D3D12InitScreenSizeDependentResources(Context(), &Info);
  if (Status != FFX_CACAO_STATUS_OK)
  {
    iLog->LogError("XRenderD3D12: FFX_CACAO_D3D12InitScreenSizeDependentResources (%d x %d) failed (%d)", Targets.nWidth, Targets.nHeight,
                   (int)Status);
    Release();
    return false;
  }
  m_bScreen = true;
  m_pOutput = Targets.pOutput;
  return true;
}

bool CAOCacao::Dispatch(const SAOFrame &Frame)
{
  if (!m_bScreen || !Frame.pCmdList)
    return false;
  FFX_CACAO_Settings Settings = FFX_CACAO_DEFAULT_SETTINGS;
  Settings.radius = std::max(Frame.fRadius, 0.01f);
  Settings.qualityLevel = (FFX_CACAO_Quality)(FFX_CACAO_QUALITY_LOW + std::clamp(Frame.nQuality, 0, 3));
  Settings.blurPassCount = (uint32_t)std::clamp(Frame.nDenoise, 1, 8);
  Settings.fadeOutFrom = Frame.fFadeStart;
  Settings.fadeOutTo = Frame.fFadeEnd;
  Settings.generateNormals = FFX_CACAO_TRUE;
  // While an upscaler gathers the frames: its kernel turned and scaled in a cycle of three, as AMD
  // suggests for temporal anti-aliasing.
  if (Frame.nNoiseIndex)
  {
    const int n = Frame.nNoiseIndex % 3;
    Settings.temporalSupersamplingAngleOffset = n / 3.0f * 3.14159265f;
    Settings.temporalSupersamplingRadiusOffset = 1.0f + (n - 1) / 3.0f * 0.1f;
  }
  FFX_CACAO_D3D12UpdateSettings(Context(), &Settings);

  // Row major as Matrix44, which CACAO reads (MATRIX_ROW_MAJOR_ORDER); the normals it makes are in
  // view space already.
  FFX_CACAO_Matrix4x4 Proj, Identity = {};
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++)
      Proj.elements[r][c] = Frame.Proj[r * 4 + c];
  for (int i = 0; i < 4; i++)
    Identity.elements[i][i] = 1;
  const FFX_CACAO_Status Status = FFX_CACAO_D3D12Draw(Context(), Frame.pCmdList, &Proj, &Identity);
  if (Status != FFX_CACAO_STATUS_OK)
  {
    if (!m_bFailedLogged)
      iLog->LogError("XRenderD3D12: FFX_CACAO_D3D12Draw failed (%d)", (int)Status);
    m_bFailedLogged = true;
    return false;
  }
  // It leaves the output in GENERIC_READ.
  D3D12_RESOURCE_BARRIER b = {};
  b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b.Transition.pResource = m_pOutput;
  b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  b.Transition.StateBefore = D3D12_RESOURCE_STATE_GENERIC_READ;
  b.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  Frame.pCmdList->ResourceBarrier(1, &b);
  return true;
}

}

std::unique_ptr<IAmbientOcclusion> CreateAOCacao()
{
  return std::make_unique<CAOCacao>();
}
