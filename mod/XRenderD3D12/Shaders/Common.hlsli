//////////////////////////////////////////////////////////////////////
//
//	File: Common.hlsli
//	Description: Bindings, vertex input and helpers shared by the shader
//	programs.
//
//	The bindings follow the root signature (D3D12Pipeline.h, Constants.h):
//	b0 the frame constants, b1 the draw constants, the texture table seen
//	as Texture2D (space1) and as TextureCube (space2), static samplers
//	s0..s3 (XR_SAMPLER_*). A texture index comes from the draw constants,
//	so it is the same for the whole draw.
//
//	Vertex input: every program reads all six attributes. The input
//	layouts (D3D12Pipeline.cpp) take what a vertex format lacks from the
//	default stream: normal (0, 0, 1), colours white, texture coordinates 0.
//	POSITION is a float4: formats with x, y, z get w = 1, the
//	pre-transformed format gives x, y, z, rhw.
//	Vertex colours are the engine's UCol (a DWORD 0xAARRGGBB), so the bytes
//	in memory are B, G, R, A. They are read as R8G8B8A8_UNORM: the shader
//	sees (B, G, R, A) and puts them back in order with .bgra
//	(DecodeVertexColor).
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_COMMON_HLSLI
#define XRENDERD3D12_COMMON_HLSLI

#include "Constants.h"

ConstantBuffer<SFrameConstants> g_Frame : register(b0);
ConstantBuffer<SDrawConstants> g_Draw : register(b1);

Texture2D g_Textures[] : register(t0, space1);
TextureCube g_Cubes[] : register(t0, space2);

SamplerState g_SamplerLinearWrap : register(s0);    // anisotropic
SamplerState g_SamplerLinearClamp : register(s1);
SamplerState g_SamplerPointWrap : register(s2);
SamplerState g_SamplerPointClamp : register(s3);

//! Pixel coordinates (DF_SCREEN_POS) are those of Direct3D 9's pre-transformed vertices: the
//! centre of pixel (i, j) is at (i, j), where Direct3D 12 puts it at (i + 0.5, j + 0.5). So a
//! position moves by this much before it becomes a clip position.
static const float XR_SCREEN_PIXEL_OFFSET = 0.5;

struct SVertexInput
{
  float4 vPos : POSITION;       //!< x, y, z, w = 1 (or rhw for the pre-transformed format)
  float3 vNormal : NORMAL;
  float4 vColor0 : COLOR0;      //!< B, G, R, A: DecodeVertexColor
  float4 vColor1 : COLOR1;      //!< B, G, R, A: DecodeVertexColor
  float2 vTex0 : TEXCOORD0;
  float2 vTex1 : TEXCOORD1;     //!< from the vertex or from the second stream (lightmap coordinates)
};

//! A vertex colour as read (B, G, R, A) to R, G, B, A.
float4 DecodeVertexColor(float4 vColor)
{
  return vColor.bgra;
}

bool HasFlag(uint nFlags)
{
  return (g_Draw.nFlags & nFlags) != 0;
}

//! A normalised vector, (0, 0, 0) for a zero vector.
float3 SafeNormalize(float3 v)
{
  float fLen2 = dot(v, v);
  return fLen2 > 1e-12 ? v * rsqrt(fLen2) : float3(0, 0, 0);
}

//! A position in pixels of the viewport (x, y from the top left, z depth, w rhw) to a clip
//! position. vScreen holds the viewport size. With rhw the clip position keeps 1 / rhw as w, so
//! that the attributes are interpolated with perspective, as for Direct3D 9's pre-transformed
//! vertices.
float4 ScreenToClip(float4 vPos)
{
  float2 vNDC;
  vNDC.x = (vPos.x + XR_SCREEN_PIXEL_OFFSET) * 2 * g_Frame.vScreen.z - 1;
  vNDC.y = 1 - (vPos.y + XR_SCREEN_PIXEL_OFFSET) * 2 * g_Frame.vScreen.w;
  float fW = vPos.w > 0 ? 1 / vPos.w : 1;
  return float4(vNDC * fW, vPos.z * fW, fW);
}

//! Samples g_Textures[nIndex] with sampler nSampler (XR_SAMPLER_*).
float4 SampleTexture(uint nIndex, uint nSampler, float2 vUV)
{
  switch (nSampler)
  {
  case XR_SAMPLER_LINEAR_CLAMP:
    return g_Textures[nIndex].Sample(g_SamplerLinearClamp, vUV);
  case XR_SAMPLER_POINT_WRAP:
    return g_Textures[nIndex].Sample(g_SamplerPointWrap, vUV);
  case XR_SAMPLER_POINT_CLAMP:
    return g_Textures[nIndex].Sample(g_SamplerPointClamp, vUV);
  default:
    return g_Textures[nIndex].Sample(g_SamplerLinearWrap, vUV);
  }
}

//! Samples g_Cubes[nIndex] in direction vDir with sampler nSampler (XR_SAMPLER_*).
float4 SampleCube(uint nIndex, uint nSampler, float3 vDir)
{
  switch (nSampler)
  {
  case XR_SAMPLER_LINEAR_CLAMP:
    return g_Cubes[nIndex].Sample(g_SamplerLinearClamp, vDir);
  case XR_SAMPLER_POINT_WRAP:
    return g_Cubes[nIndex].Sample(g_SamplerPointWrap, vDir);
  case XR_SAMPLER_POINT_CLAMP:
    return g_Cubes[nIndex].Sample(g_SamplerPointClamp, vDir);
  default:
    return g_Cubes[nIndex].Sample(g_SamplerLinearWrap, vDir);
  }
}

#endif
