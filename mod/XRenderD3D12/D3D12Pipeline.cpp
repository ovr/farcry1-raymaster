//////////////////////////////////////////////////////////////////////
//
//	File: D3D12Pipeline.cpp
//	Description: The root signature, the input layouts and the pipeline state cache.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include <cstddef>
#include "Renderer.h"
#include "D3D12Device.h"
#include "D3D12Pipeline.h"
#include "Shaders/Constants.h"

// The programs, compiled by dxc at build time (Shaders/Shaders.cmake).
#include "General_VS.h"
#include "General_PS.h"
#include "Water_VS.h"
#include "Water_PS.h"

using Microsoft::WRL::ComPtr;

namespace
{
  //! Which winding is the front of a triangle. The engine's front faces are counter-clockwise as the
  //! viewer sees them: R_CULL_BACK culls the clockwise triangles.
  constexpr BOOL FRONT_COUNTER_CLOCKWISE = TRUE;

  //! Polygon offset (SPipelineKey::bDepthBias): towards the viewer, in units of the depth format's
  //! resolution plus a part that grows with the slope of the triangle. Depth grows away from the
  //! viewer (LESS_EQUAL test), so the bias is negative.
  constexpr INT DEPTH_BIAS = -16;
  constexpr float SLOPE_SCALED_DEPTH_BIAS = -1.0f;
  //! The shadow maps' bias: the casters' depths away from the sun, by the slope of the triangle as
  //! the sun sees it (a constant part is in the receivers' comparison, SDrawConstants::vShadowParams).
  constexpr INT SHADOW_DEPTH_BIAS = 0;
  constexpr float SHADOW_SLOPE_SCALED_DEPTH_BIAS = 2.0f;

  //! The shader programs by ED3D12Program.
  struct SProgram
  {
    const char *szName;
    const void *pVS;
    size_t nVS;
    const void *pPS;
    size_t nPS;
  };
  const SProgram s_Programs[eD3D12P_Num] =
  {
    { "General", g_General_VS, sizeof(g_General_VS), g_General_PS, sizeof(g_General_PS) },
    { "Water", g_Water_VS, sizeof(g_Water_VS), g_Water_PS, sizeof(g_Water_PS) },
  };

  //! Input slot 1: what a vertex format lacks, read with stride 0 by every vertex.
  struct SDefaultStream
  {
    float vNormal[3];
    uint32 nColor0;               //!< UCol bytes B, G, R, A
    uint32 nColor1;
    float vTex0[2];
    float vTex1[2];
    float vTangent[3];            //!< the tangent frame without a tangent stream
    float vBinormal[3];
    float vTNormal[3];
  };
  constexpr UINT DEFAULT_STREAM_SLOT = 1;
  constexpr UINT TEXCOORD1_STREAM_SLOT = 2;   //!< VERTEX_FORMAT_TEX2F
  constexpr UINT TANGENT_STREAM_SLOT = 3;     //!< SPipTangents

  //! Where a vertex format keeps its attributes in slot 0; -1: from the default stream. nPos -1:
  //! the format is not drawn on its own.
  struct SFormatLayout
  {
    int nPos;
    DXGI_FORMAT PosFormat;
    int nNormal;
    int nColor0;
    int nColor1;
    int nTex0;
    int nTex1;
  };

#define XR_OFS(Struct, Member) (int)offsetof(Struct, Member)
  const SFormatLayout s_Layouts[VERTEX_FORMAT_NUMS] =
  {
    // 0: no format
    { -1, DXGI_FORMAT_UNKNOWN, -1, -1, -1, -1, -1 },
    // VERTEX_FORMAT_P3F
    { 0, DXGI_FORMAT_R32G32B32_FLOAT, -1, -1, -1, -1, -1 },
    // VERTEX_FORMAT_P3F_COL4UB
    { 0, DXGI_FORMAT_R32G32B32_FLOAT, -1, XR_OFS(struct_VERTEX_FORMAT_P3F_COL4UB, color), -1, -1, -1 },
    // VERTEX_FORMAT_P3F_TEX2F
    { 0, DXGI_FORMAT_R32G32B32_FLOAT, -1, -1, -1, XR_OFS(struct_VERTEX_FORMAT_P3F_TEX2F, st), -1 },
    // VERTEX_FORMAT_P3F_COL4UB_TEX2F
    { 0, DXGI_FORMAT_R32G32B32_FLOAT, -1, XR_OFS(struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F, color), -1,
      XR_OFS(struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F, st), -1 },
    // VERTEX_FORMAT_TRP3F_COL4UB_TEX2F: x, y, z, rhw
    { XR_OFS(struct_VERTEX_FORMAT_TRP3F_COL4UB_TEX2F, x), DXGI_FORMAT_R32G32B32A32_FLOAT, -1,
      XR_OFS(struct_VERTEX_FORMAT_TRP3F_COL4UB_TEX2F, color), -1, XR_OFS(struct_VERTEX_FORMAT_TRP3F_COL4UB_TEX2F, st), -1 },
    // VERTEX_FORMAT_P3F_COL4UB_COL4UB
    { 0, DXGI_FORMAT_R32G32B32_FLOAT, -1, XR_OFS(struct_VERTEX_FORMAT_P3F_COL4UB_COL4UB, color),
      XR_OFS(struct_VERTEX_FORMAT_P3F_COL4UB_COL4UB, seccolor), -1, -1 },
    // VERTEX_FORMAT_P3F_N
    { 0, DXGI_FORMAT_R32G32B32_FLOAT, XR_OFS(struct_VERTEX_FORMAT_P3F_N, normal), -1, -1, -1, -1 },
    // VERTEX_FORMAT_P3F_N_COL4UB
    { 0, DXGI_FORMAT_R32G32B32_FLOAT, XR_OFS(struct_VERTEX_FORMAT_P3F_N_COL4UB, normal),
      XR_OFS(struct_VERTEX_FORMAT_P3F_N_COL4UB, color), -1, -1, -1 },
    // VERTEX_FORMAT_P3F_N_TEX2F
    { 0, DXGI_FORMAT_R32G32B32_FLOAT, XR_OFS(struct_VERTEX_FORMAT_P3F_N_TEX2F, normal), -1, -1,
      XR_OFS(struct_VERTEX_FORMAT_P3F_N_TEX2F, st), -1 },
    // VERTEX_FORMAT_P3F_N_COL4UB_TEX2F
    { 0, DXGI_FORMAT_R32G32B32_FLOAT, XR_OFS(struct_VERTEX_FORMAT_P3F_N_COL4UB_TEX2F, normal),
      XR_OFS(struct_VERTEX_FORMAT_P3F_N_COL4UB_TEX2F, color), -1, XR_OFS(struct_VERTEX_FORMAT_P3F_N_COL4UB_TEX2F, st), -1 },
    // VERTEX_FORMAT_P3F_N_COL4UB_COL4UB
    { 0, DXGI_FORMAT_R32G32B32_FLOAT, XR_OFS(struct_VERTEX_FORMAT_P3F_N_COL4UB_COL4UB, normal),
      XR_OFS(struct_VERTEX_FORMAT_P3F_N_COL4UB_COL4UB, color), XR_OFS(struct_VERTEX_FORMAT_P3F_N_COL4UB_COL4UB, seccolor), -1, -1 },
    // VERTEX_FORMAT_P3F_COL4UB_COL4UB_TEX2F
    { 0, DXGI_FORMAT_R32G32B32_FLOAT, -1, XR_OFS(struct_VERTEX_FORMAT_P3F_COL4UB_COL4UB_TEX2F, color),
      XR_OFS(struct_VERTEX_FORMAT_P3F_COL4UB_COL4UB_TEX2F, seccolor), XR_OFS(struct_VERTEX_FORMAT_P3F_COL4UB_COL4UB_TEX2F, st), -1 },
    // VERTEX_FORMAT_P3F_N_COL4UB_COL4UB_TEX2F
    { 0, DXGI_FORMAT_R32G32B32_FLOAT, XR_OFS(struct_VERTEX_FORMAT_P3F_N_COL4UB_COL4UB_TEX2F, normal),
      XR_OFS(struct_VERTEX_FORMAT_P3F_N_COL4UB_COL4UB_TEX2F, color), XR_OFS(struct_VERTEX_FORMAT_P3F_N_COL4UB_COL4UB_TEX2F, seccolor),
      XR_OFS(struct_VERTEX_FORMAT_P3F_N_COL4UB_COL4UB_TEX2F, st), -1 },
    // VERTEX_FORMAT_T3F_B3F_N3F: tangents, an extra stream only
    { -1, DXGI_FORMAT_UNKNOWN, -1, -1, -1, -1, -1 },
    // VERTEX_FORMAT_TEX2F: lightmap coordinates, an extra stream only
    { -1, DXGI_FORMAT_UNKNOWN, -1, -1, -1, -1, -1 },
    // VERTEX_FORMAT_P3F_COL4UB_TEX2F_TEX2F
    { 0, DXGI_FORMAT_R32G32B32_FLOAT, -1, XR_OFS(struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F_TEX2F, color), -1,
      XR_OFS(struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F_TEX2F, st0), XR_OFS(struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F_TEX2F, st1) },
  };
#undef XR_OFS

  constexpr int s_VertexSizes[VERTEX_FORMAT_NUMS] =
  {
    0,
    sizeof(struct_VERTEX_FORMAT_P3F),
    sizeof(struct_VERTEX_FORMAT_P3F_COL4UB),
    sizeof(struct_VERTEX_FORMAT_P3F_TEX2F),
    sizeof(struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F),
    sizeof(struct_VERTEX_FORMAT_TRP3F_COL4UB_TEX2F),
    sizeof(struct_VERTEX_FORMAT_P3F_COL4UB_COL4UB),
    sizeof(struct_VERTEX_FORMAT_P3F_N),
    sizeof(struct_VERTEX_FORMAT_P3F_N_COL4UB),
    sizeof(struct_VERTEX_FORMAT_P3F_N_TEX2F),
    sizeof(struct_VERTEX_FORMAT_P3F_N_COL4UB_TEX2F),
    sizeof(struct_VERTEX_FORMAT_P3F_N_COL4UB_COL4UB),
    sizeof(struct_VERTEX_FORMAT_P3F_COL4UB_COL4UB_TEX2F),
    sizeof(struct_VERTEX_FORMAT_P3F_N_COL4UB_COL4UB_TEX2F),
    sizeof(SPipTangents),
    sizeof(struct_VERTEX_FORMAT_TEX2F),
    sizeof(struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F_TEX2F),
  };

  static_assert(VERTEX_FORMAT_NUMS == 17);
  static_assert(sizeof(struct_VERTEX_FORMAT_TRP3F_COL4UB_TEX2F) == 28);
  static_assert(sizeof(struct_VERTEX_FORMAT_P3F_N_COL4UB_COL4UB_TEX2F) == 40);
  static_assert(sizeof(SPipTangents) == 36);
  static_assert(sizeof(SDefaultStream) == 72);
  static_assert(sizeof(SPipelineKey) == 12, "SPipelineKey is hashed and compared as bytes");

  //! The most elements of an input layout.
  constexpr int MAX_INPUT_ELEMENTS = 9;

  //! Fills pOut (MAX_INPUT_ELEMENTS at most) with the input layout of a vertex format and the extra
  //! streams nStreams (PIPELINE_STREAM_*); 0 when the format is not drawn on its own.
  int BuildInputLayout(int nFormat, uint8 nStreams, D3D12_INPUT_ELEMENT_DESC *pOut)
  {
    if (nFormat <= 0 || nFormat >= VERTEX_FORMAT_NUMS)
      return 0;
    const SFormatLayout &Layout = s_Layouts[nFormat];
    if (Layout.nPos < 0)
      return 0;

    int n = 0;
    auto Add = [&](const char *szName, UINT nIndex, DXGI_FORMAT Format, UINT nSlot, int nOffset)
    {
      pOut[n++] = { szName, nIndex, Format, nSlot, (UINT)nOffset, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
    };
    // An attribute from slot 0 or, when the format lacks it, from the default stream.
    auto AddOr = [&](const char *szName, UINT nIndex, DXGI_FORMAT Format, int nOffset, int nDefaultOffset)
    {
      if (nOffset >= 0)
        Add(szName, nIndex, Format, 0, nOffset);
      else
        Add(szName, nIndex, Format, DEFAULT_STREAM_SLOT, nDefaultOffset);
    };

    Add("POSITION", 0, Layout.PosFormat, 0, Layout.nPos);
    AddOr("NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, Layout.nNormal, (int)offsetof(SDefaultStream, vNormal));
    // Vertex colours: bytes B, G, R, A, swapped back in the shader (Common.hlsli).
    AddOr("COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, Layout.nColor0, (int)offsetof(SDefaultStream, nColor0));
    AddOr("COLOR", 1, DXGI_FORMAT_R8G8B8A8_UNORM, Layout.nColor1, (int)offsetof(SDefaultStream, nColor1));
    AddOr("TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, Layout.nTex0, (int)offsetof(SDefaultStream, vTex0));
    if (nStreams & PIPELINE_STREAM_TEXCOORD1)
      Add("TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, TEXCOORD1_STREAM_SLOT, 0);
    else
      AddOr("TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, Layout.nTex1, (int)offsetof(SDefaultStream, vTex1));
    if (nStreams & PIPELINE_STREAM_TANGENTS)
    {
      Add("TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, TANGENT_STREAM_SLOT, (int)offsetof(SPipTangents, m_Tangent));
      Add("BINORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, TANGENT_STREAM_SLOT, (int)offsetof(SPipTangents, m_Binormal));
      Add("TNORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, TANGENT_STREAM_SLOT, (int)offsetof(SPipTangents, m_TNormal));
    }
    else
    {
      Add("TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, DEFAULT_STREAM_SLOT, (int)offsetof(SDefaultStream, vTangent));
      Add("BINORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, DEFAULT_STREAM_SLOT, (int)offsetof(SDefaultStream, vBinormal));
      Add("TNORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, DEFAULT_STREAM_SLOT, (int)offsetof(SDefaultStream, vTNormal));
    }
    return n;
  }

  D3D12_BLEND SrcBlend(uint32 nState)
  {
    switch (nState & GS_BLSRC_MASK)
    {
    case GS_BLSRC_ZERO:             return D3D12_BLEND_ZERO;
    case GS_BLSRC_ONE:              return D3D12_BLEND_ONE;
    case GS_BLSRC_DSTCOL:           return D3D12_BLEND_DEST_COLOR;
    case GS_BLSRC_ONEMINUSDSTCOL:   return D3D12_BLEND_INV_DEST_COLOR;
    case GS_BLSRC_SRCALPHA:         return D3D12_BLEND_SRC_ALPHA;
    case GS_BLSRC_ONEMINUSSRCALPHA: return D3D12_BLEND_INV_SRC_ALPHA;
    case GS_BLSRC_DSTALPHA:         return D3D12_BLEND_DEST_ALPHA;
    case GS_BLSRC_ONEMINUSDSTALPHA: return D3D12_BLEND_INV_DEST_ALPHA;
    case GS_BLSRC_ALPHASATURATE:    return D3D12_BLEND_SRC_ALPHA_SAT;
    default:                        return D3D12_BLEND_ONE;
    }
  }

  D3D12_BLEND DstBlend(uint32 nState)
  {
    switch (nState & GS_BLDST_MASK)
    {
    case GS_BLDST_ZERO:             return D3D12_BLEND_ZERO;
    case GS_BLDST_ONE:              return D3D12_BLEND_ONE;
    case GS_BLDST_SRCCOL:           return D3D12_BLEND_SRC_COLOR;
    case GS_BLDST_ONEMINUSSRCCOL:   return D3D12_BLEND_INV_SRC_COLOR;
    case GS_BLDST_SRCALPHA:         return D3D12_BLEND_SRC_ALPHA;
    case GS_BLDST_ONEMINUSSRCALPHA: return D3D12_BLEND_INV_SRC_ALPHA;
    case GS_BLDST_DSTALPHA:         return D3D12_BLEND_DEST_ALPHA;
    case GS_BLDST_ONEMINUSDSTALPHA: return D3D12_BLEND_INV_DEST_ALPHA;
    default:                        return D3D12_BLEND_ZERO;
    }
  }

  //! The factor of the alpha channel for a colour factor: the alpha channel can't use colour
  //! factors, their alpha is the matching alpha; alpha saturate is (f, f, f, 1).
  D3D12_BLEND AlphaBlend(D3D12_BLEND Blend)
  {
    switch (Blend)
    {
    case D3D12_BLEND_SRC_COLOR:      return D3D12_BLEND_SRC_ALPHA;
    case D3D12_BLEND_INV_SRC_COLOR:  return D3D12_BLEND_INV_SRC_ALPHA;
    case D3D12_BLEND_DEST_COLOR:     return D3D12_BLEND_DEST_ALPHA;
    case D3D12_BLEND_INV_DEST_COLOR: return D3D12_BLEND_INV_DEST_ALPHA;
    case D3D12_BLEND_SRC_ALPHA_SAT:  return D3D12_BLEND_ONE;
    default:                         return Blend;
    }
  }

  D3D12_STATIC_SAMPLER_DESC StaticSampler(UINT nRegister, D3D12_FILTER Filter, D3D12_TEXTURE_ADDRESS_MODE Address)
  {
    D3D12_STATIC_SAMPLER_DESC Desc = {};
    Desc.Filter = Filter;
    Desc.AddressU = Address;
    Desc.AddressV = Address;
    Desc.AddressW = Address;
    Desc.MipLODBias = 0;
    Desc.MaxAnisotropy = Filter == D3D12_FILTER_ANISOTROPIC ? 8 : 1;
    Desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    Desc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    Desc.MinLOD = 0;
    Desc.MaxLOD = D3D12_FLOAT32_MAX;
    Desc.ShaderRegister = nRegister;
    Desc.RegisterSpace = 0;
    Desc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    return Desc;
  }
}

size_t CD3D12Pipeline::SKeyHash::operator()(const SPipelineKey &Key) const
{
  // FNV-1a over the bytes of the key (callers zero-initialise it).
  const uint8 *p = (const uint8 *)&Key;
  uint64 nHash = 14695981039346656037ull;
  for (size_t i = 0; i < sizeof(Key); i++)
  {
    nHash ^= p[i];
    nHash *= 1099511628211ull;
  }
  return (size_t)nHash;
}

bool CD3D12Pipeline::SKeyEqual::operator()(const SPipelineKey &a, const SPipelineKey &b) const
{
  return memcmp(&a, &b, sizeof(SPipelineKey)) == 0;
}

CD3D12Pipeline::CD3D12Pipeline() = default;

CD3D12Pipeline::~CD3D12Pipeline()
{
  Shutdown();
}

bool CD3D12Pipeline::Init(ID3D12Device *pDevice)
{
  Shutdown();
  m_pDevice = pDevice;
  return CreateRootSignature() && CreateDefaultStream();
}

void CD3D12Pipeline::Shutdown()
{
  m_PSOs.clear();
  m_pDefaultStream.Reset();
  m_DefaultStreamView = {};
  m_pRootSignature.Reset();
  m_pDevice = nullptr;
}

bool CD3D12Pipeline::CreateRootSignature()
{
  // The texture table is bound as unbounded arrays, which needs resource binding tier 2.
  D3D12_FEATURE_DATA_D3D12_OPTIONS Options = {};
  if (FAILED(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &Options, sizeof(Options))) ||
      Options.ResourceBindingTier < D3D12_RESOURCE_BINDING_TIER_2)
    iLog->LogError("XRenderD3D12: the device has resource binding tier %d; the texture table needs tier 2",
      (int)Options.ResourceBindingTier);

  // All views of the texture table start at its first slot.
  D3D12_DESCRIPTOR_RANGE Ranges[3] = {};
  Ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  Ranges[0].NumDescriptors = UINT_MAX;          // unbounded
  Ranges[0].BaseShaderRegister = 0;
  Ranges[0].RegisterSpace = 1;                  // Texture2D g_Textures[]
  Ranges[0].OffsetInDescriptorsFromTableStart = 0;
  Ranges[1] = Ranges[0];
  Ranges[1].RegisterSpace = 2;                  // TextureCube g_Cubes[]
  Ranges[2] = Ranges[0];
  Ranges[2].RegisterSpace = 3;                  // Texture2DArray g_TextureArrays[]

  D3D12_ROOT_PARAMETER Params[3] = {};
  Params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;      // b0 SFrameConstants
  Params[0].Descriptor.ShaderRegister = 0;
  Params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  Params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;      // b1 SDrawConstants
  Params[1].Descriptor.ShaderRegister = 1;
  Params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  Params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  Params[2].DescriptorTable.NumDescriptorRanges = 3;
  Params[2].DescriptorTable.pDescriptorRanges = Ranges;
  Params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  D3D12_STATIC_SAMPLER_DESC Samplers[5];
  Samplers[XR_SAMPLER_LINEAR_WRAP] = StaticSampler(XR_SAMPLER_LINEAR_WRAP, D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_WRAP);
  Samplers[XR_SAMPLER_LINEAR_CLAMP] = StaticSampler(XR_SAMPLER_LINEAR_CLAMP, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
  Samplers[XR_SAMPLER_POINT_WRAP] = StaticSampler(XR_SAMPLER_POINT_WRAP, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_WRAP);
  Samplers[XR_SAMPLER_POINT_CLAMP] = StaticSampler(XR_SAMPLER_POINT_CLAMP, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
  // The shadow maps: a depth comparison, lit beyond the map.
  Samplers[4] = StaticSampler(4, D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_BORDER);
  Samplers[4].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

  D3D12_ROOT_SIGNATURE_DESC Desc = {};
  Desc.NumParameters = 3;
  Desc.pParameters = Params;
  Desc.NumStaticSamplers = 5;
  Desc.pStaticSamplers = Samplers;
  Desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
    D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
    D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
    D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

  ComPtr<ID3DBlob> pBlob, pErrors;
  HRESULT hr = D3D12SerializeRootSignature(&Desc, D3D_ROOT_SIGNATURE_VERSION_1, &pBlob, &pErrors);
  if (FAILED(hr))
  {
    iLog->LogError("XRenderD3D12: D3D12SerializeRootSignature failed (0x%08X): %s", (unsigned)hr,
      pErrors ? (const char *)pErrors->GetBufferPointer() : "");
    return false;
  }
  hr = m_pDevice->CreateRootSignature(0, pBlob->GetBufferPointer(), pBlob->GetBufferSize(), IID_PPV_ARGS(&m_pRootSignature));
  if (FAILED(hr))
  {
    iLog->LogError("XRenderD3D12: CreateRootSignature failed (0x%08X)", (unsigned)hr);
    return false;
  }
  m_pRootSignature->SetName(L"XRenderD3D12 root signature");
  return true;
}

bool CD3D12Pipeline::CreateDefaultStream()
{
  // An upload heap buffer: tiny, written once, and every vertex reads the same bytes.
  D3D12_HEAP_PROPERTIES Heap = {};
  Heap.Type = D3D12_HEAP_TYPE_UPLOAD;
  D3D12_RESOURCE_DESC Desc = {};
  Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  Desc.Width = 256;
  Desc.Height = 1;
  Desc.DepthOrArraySize = 1;
  Desc.MipLevels = 1;
  Desc.Format = DXGI_FORMAT_UNKNOWN;
  Desc.SampleDesc.Count = 1;
  Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  HRESULT hr = m_pDevice->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_GENERIC_READ,
    nullptr, IID_PPV_ARGS(&m_pDefaultStream));
  if (FAILED(hr))
  {
    iLog->LogError("XRenderD3D12: CreateCommittedResource (default vertex stream) failed (0x%08X)", (unsigned)hr);
    return false;
  }
  m_pDefaultStream->SetName(L"XRenderD3D12 default vertex stream");

  SDefaultStream Defaults = {};
  Defaults.vNormal[2] = 1.0f;
  Defaults.nColor0 = 0xffffffff;
  Defaults.nColor1 = 0xffffffff;
  Defaults.vTangent[0] = 1.0f;
  Defaults.vBinormal[1] = 1.0f;
  Defaults.vTNormal[2] = 1.0f;
  void *pCPU = nullptr;
  const D3D12_RANGE NoRead = { 0, 0 };
  hr = m_pDefaultStream->Map(0, &NoRead, &pCPU);
  if (FAILED(hr))
  {
    iLog->LogError("XRenderD3D12: Map (default vertex stream) failed (0x%08X)", (unsigned)hr);
    return false;
  }
  memcpy(pCPU, &Defaults, sizeof(Defaults));
  m_pDefaultStream->Unmap(0, nullptr);

  m_DefaultStreamView.BufferLocation = m_pDefaultStream->GetGPUVirtualAddress();
  m_DefaultStreamView.SizeInBytes = sizeof(SDefaultStream);
  m_DefaultStreamView.StrideInBytes = 0;
  return true;
}

ID3D12RootSignature *CD3D12Pipeline::GetRootSignature() const
{
  return m_pRootSignature.Get();
}

void CD3D12Pipeline::BindDefaultStream(ID3D12GraphicsCommandList *pList)
{
  if (pList && m_pDefaultStream)
    pList->IASetVertexBuffers(DEFAULT_STREAM_SLOT, 1, &m_DefaultStreamView);
}

int CD3D12Pipeline::GetVertexSize(int nVertexFormat)
{
  if (nVertexFormat <= 0 || nVertexFormat >= VERTEX_FORMAT_NUMS)
    return 0;
  return s_VertexSizes[nVertexFormat];
}

ID3D12PipelineState *CD3D12Pipeline::GetPSO(const SPipelineKey &Key)
{
  if (!m_pRootSignature)
    return nullptr;
  auto it = m_PSOs.find(Key);
  if (it != m_PSOs.end())
    return it->second.Get();
  ComPtr<ID3D12PipelineState> pPSO = CreatePSO(Key);
  ID3D12PipelineState *pResult = pPSO.Get();
  m_PSOs.emplace(Key, std::move(pPSO));
  return pResult;
}

ComPtr<ID3D12PipelineState> CD3D12Pipeline::CreatePSO(const SPipelineKey &Key)
{
  if (Key.nProgram >= eD3D12P_Num)
  {
    iLog->LogError("XRenderD3D12: no shader program %d", (int)Key.nProgram);
    return nullptr;
  }
  D3D12_INPUT_ELEMENT_DESC Elements[MAX_INPUT_ELEMENTS];
  const int nElements = BuildInputLayout(Key.nVertexFormat, Key.nStreams, Elements);
  if (!nElements)
    return nullptr;

  const SProgram &Program = s_Programs[Key.nProgram];
  const bool bDepth = Key.nTarget == 0 || Key.nTarget == 2;
  const bool bShadowMap = Key.nTarget == 2;
  const uint32 nState = Key.nState;

  D3D12_GRAPHICS_PIPELINE_STATE_DESC Desc = {};
  Desc.pRootSignature = m_pRootSignature.Get();
  Desc.VS = { Program.pVS, Program.nVS };
  Desc.PS = { Program.pPS, Program.nPS };

  // Blending: off when neither factor is set.
  D3D12_RENDER_TARGET_BLEND_DESC &Blend = Desc.BlendState.RenderTarget[0];
  if (nState & (GS_BLSRC_MASK | GS_BLDST_MASK))
  {
    Blend.BlendEnable = TRUE;
    Blend.SrcBlend = SrcBlend(nState);
    Blend.DestBlend = DstBlend(nState);
    Blend.BlendOp = D3D12_BLEND_OP_ADD;
    Blend.SrcBlendAlpha = AlphaBlend(Blend.SrcBlend);
    Blend.DestBlendAlpha = AlphaBlend(Blend.DestBlend);
    Blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
  }
  else
  {
    Blend.BlendEnable = FALSE;
    Blend.SrcBlend = D3D12_BLEND_ONE;
    Blend.DestBlend = D3D12_BLEND_ZERO;
    Blend.BlendOp = D3D12_BLEND_OP_ADD;
    Blend.SrcBlendAlpha = D3D12_BLEND_ONE;
    Blend.DestBlendAlpha = D3D12_BLEND_ZERO;
    Blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
  }
  Blend.LogicOpEnable = FALSE;
  Blend.LogicOp = D3D12_LOGIC_OP_NOOP;
  if (nState & GS_NOCOLMASK)
    Blend.RenderTargetWriteMask = 0;
  else if (nState & GS_COLMASKONLYALPHA)
    Blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALPHA;
  else if (nState & GS_COLMASKONLYRGB)
    Blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN | D3D12_COLOR_WRITE_ENABLE_BLUE;
  else
    Blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  Desc.SampleMask = UINT_MAX;

  D3D12_RASTERIZER_DESC &Raster = Desc.RasterizerState;
  Raster.FillMode = Key.bWireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
  switch (Key.nCull)
  {
  case R_CULL_FRONT: Raster.CullMode = D3D12_CULL_MODE_FRONT; break;
  case R_CULL_BACK:  Raster.CullMode = D3D12_CULL_MODE_BACK; break;
  default:           Raster.CullMode = D3D12_CULL_MODE_NONE; break;
  }
  Raster.FrontCounterClockwise = FRONT_COUNTER_CLOCKWISE;
  if (Key.bDepthBias && bShadowMap)
  {
    Raster.DepthBias = SHADOW_DEPTH_BIAS;
    Raster.SlopeScaledDepthBias = SHADOW_SLOPE_SCALED_DEPTH_BIAS;
  }
  else if (Key.bDepthBias && bDepth)
  {
    Raster.DepthBias = DEPTH_BIAS;
    Raster.SlopeScaledDepthBias = SLOPE_SCALED_DEPTH_BIAS;
  }
  Raster.DepthBiasClamp = 0.0f;
  Raster.DepthClipEnable = TRUE;
  Raster.MultisampleEnable = FALSE;
  Raster.AntialiasedLineEnable = FALSE;
  Raster.ForcedSampleCount = 0;
  Raster.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

  // Depth: GS_NODEPTHTEST turns off the test and the writes.
  D3D12_DEPTH_STENCIL_DESC &Depth = Desc.DepthStencilState;
  if (bDepth && !(nState & GS_NODEPTHTEST))
  {
    Depth.DepthEnable = TRUE;
    Depth.DepthWriteMask = (nState & GS_DEPTHWRITE) ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    if (nState & GS_DEPTHFUNC_EQUAL)
      Depth.DepthFunc = D3D12_COMPARISON_FUNC_EQUAL;
    else if (nState & GS_DEPTHFUNC_GREAT)
      Depth.DepthFunc = D3D12_COMPARISON_FUNC_GREATER;
    else
      Depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
  }
  else
  {
    Depth.DepthEnable = FALSE;
    Depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    Depth.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
  }
  Depth.StencilEnable = FALSE;
  Depth.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
  Depth.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
  const D3D12_DEPTH_STENCILOP_DESC StencilOp = { D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS };
  Depth.FrontFace = StencilOp;
  Depth.BackFace = StencilOp;

  Desc.InputLayout = { Elements, (UINT)nElements };
  Desc.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
  const int nTopology = Key.nTopology;
  Desc.PrimitiveTopologyType = nTopology >= D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT && nTopology <= D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE ?
    (D3D12_PRIMITIVE_TOPOLOGY_TYPE)nTopology : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  Desc.NumRenderTargets = bShadowMap ? 0 : 1;
  Desc.RTVFormats[0] = bShadowMap ? DXGI_FORMAT_UNKNOWN : bDepth ? D3D12_BACKBUFFER_FORMAT : DXGI_FORMAT_R8G8B8A8_UNORM;
  Desc.DSVFormat = bShadowMap ? D3D12_SHADOW_MAP_FORMAT : bDepth ? D3D12_DEPTH_FORMAT : DXGI_FORMAT_UNKNOWN;
  Desc.SampleDesc.Count = 1;
  Desc.SampleDesc.Quality = 0;
  Desc.NodeMask = 0;
  Desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

  ComPtr<ID3D12PipelineState> pPSO;
  HRESULT hr = m_pDevice->CreateGraphicsPipelineState(&Desc, IID_PPV_ARGS(&pPSO));
  if (FAILED(hr))
  {
    iLog->LogError("XRenderD3D12: CreateGraphicsPipelineState failed (0x%08X): program %s, vertex format %d, state 0x%08X, "
      "cull %d, topology %d, target %d, depth bias %d, streams %d, wireframe %d", (unsigned)hr, Program.szName,
      (int)Key.nVertexFormat, nState, (int)Key.nCull, nTopology, (int)Key.nTarget, (int)Key.bDepthBias,
      (int)Key.nStreams, (int)Key.bWireframe);
    return nullptr;
  }
  return pPSO;
}
