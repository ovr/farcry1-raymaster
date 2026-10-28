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
//	Vertex input: every program reads all the attributes. The input
//	layouts (D3D12Pipeline.cpp) take what a vertex format lacks from the
//	default stream: normal (0, 0, 1), colours white, texture coordinates 0,
//	the tangent frame (1, 0, 0), (0, 1, 0), (0, 0, 1) when there is no
//	tangent stream.
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
Texture2DArray g_TextureArrays[] : register(t0, space3);

SamplerState g_SamplerLinearWrap : register(s0);    // anisotropic
SamplerState g_SamplerLinearClamp : register(s1);
SamplerState g_SamplerPointWrap : register(s2);
SamplerState g_SamplerPointClamp : register(s3);
SamplerComparisonState g_SamplerShadow : register(s4);

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
  float3 vTangent : TANGENT;    //!< the tangent frame (SPipTangents) from the tangent stream
  float3 vBinormal : BINORMAL;
  float3 vTNormal : TNORMAL;
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

bool HasFlag2(uint nFlags2)
{
  return (g_Draw.nFlags2 & nFlags2) != 0;
}

//! A normalised vector, (0, 0, 0) for a zero vector.
float3 SafeNormalize(float3 v)
{
  float fLen2 = dot(v, v);
  return fLen2 > 1e-12 ? v * rsqrt(fLen2) : float3(0, 0, 0);
}

//! Heat vision (DF2_HEAT): a cold surface shows the luminance L of its colour in a light blue
//! over a dark floor, a hot one (vHeat.x towards 1) glows from red to pale yellow whatever its
//! texture, less where it turns away from the camera (fFacing: |N.V|); an additive draw (vHeat.y)
//! adds the same colours times its L, without the floor.
float3 HeatVision(float3 vColor, float fFacing)
{
  static const float3 COLD = float3(0.5, 0.7, 1);
  float L = saturate(dot(vColor, float3(0.299, 0.587, 0.114)));
  float fHeat = saturate(g_Draw.vHeat.x) * lerp(0.55, 1, saturate(fFacing));
  float3 vHot = lerp(float3(0.9, 0.3, 0.06), float3(1, 0.95, 0.65), saturate(0.3 + L));
  if (g_Draw.vHeat.y > 0)
    return lerp(COLD * 0.5, vHot, fHeat) * L;
  return lerp(COLD * (0.08 + 0.42 * L), vHot, fHeat);
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

//! Samples slice vUVW.z of g_TextureArrays[nIndex] with sampler nSampler (XR_SAMPLER_*).
float4 SampleTextureArray(uint nIndex, uint nSampler, float3 vUVW)
{
  switch (nSampler)
  {
  case XR_SAMPLER_LINEAR_CLAMP:
    return g_TextureArrays[nIndex].Sample(g_SamplerLinearClamp, vUVW);
  case XR_SAMPLER_POINT_WRAP:
    return g_TextureArrays[nIndex].Sample(g_SamplerPointWrap, vUVW);
  case XR_SAMPLER_POINT_CLAMP:
    return g_TextureArrays[nIndex].Sample(g_SamplerPointClamp, vUVW);
  default:
    return g_TextureArrays[nIndex].Sample(g_SamplerLinearWrap, vUVW);
  }
}

//! How much of the sun reaches world position vPos (normal vNormal, may be zero) through the
//! draw's shadow maps (DF_SHADOWS): 1 lit, 0 in shadow. Each map is a percentage closer filter of
//! 16 bilinear comparisons on a rotated disc (vShadowParams.z texels), its shadow fading out
//! towards the edge of the map's circle and, with vShadowParams.y, with the depth behind its
//! casters (times saturate(1 - y * (depth - the mean depth of the casters seen))); the maps
//! multiply.
float SunVisibility(float3 vPos, float3 vNormal, float2 vScreenPos)
{
  static const float2 Disc[8] =
  {
    float2(-0.326, -0.406), float2(-0.840, -0.074), float2(-0.696, 0.457), float2(-0.203, 0.621),
    float2(0.962, -0.195), float2(0.473, -0.480), float2(0.519, 0.767), float2(0.185, -0.893),
  };
  float3 vP = vPos + SafeNormalize(vNormal) * g_Draw.vShadowParams.w;
  // A rotation of the disc per pixel (interleaved gradient noise) turns banding into fine noise.
  float fAngle = 6.2831853 * frac(52.9829189 * frac(dot(vScreenPos, float2(0.06711056, 0.00583715))));
  float2 vRot = float2(cos(fAngle), sin(fAngle));
  float fLit = 1;
  uint nMaps = min((uint)g_Draw.vShadowParams.x, XR_MAX_DRAW_SHADOWS);
  for (uint i = 0; i < nMaps; i++)
  {
    float4 v = mul(float4(vP, 1), g_Draw.mShadow[i]);
    float fEdge = saturate((1 - length(v.xy)) * 6);
    // In front of the casters: lit; behind them (beyond the map's depth range): as far as they go.
    if (fEdge <= 0 || v.z <= 0)
      continue;
    Texture2D Map = g_Textures[g_Draw.nShadowTex[i]];
    float2 vSize;
    Map.GetDimensions(vSize.x, vSize.y);
    float2 vUV = float2(0.5 + 0.5 * v.x, 0.5 - 0.5 * v.y);
    float2 vStep = g_Draw.vShadowParams.z / vSize;
    float fDepth = min(v.z, 1);
    float fSum = 0, fCasters = 0, fCasterDepth = 0;
    [unroll]
    for (uint k = 0; k < 16; k++)
    {
      float2 d = float2(Disc[k / 2].x * vRot.x - Disc[k / 2].y * vRot.y, Disc[k / 2].x * vRot.y + Disc[k / 2].y * vRot.x);
      float2 vUVk = vUV + d * vStep * ((k & 1) ? 0.5 : 1);
      fSum += Map.SampleCmpLevelZero(g_SamplerShadow, vUVk, fDepth);
      if (g_Draw.vShadowParams.y > 0)
      {
        float fMap = Map.SampleLevel(g_SamplerPointClamp, vUVk, 0).r;
        if (fMap < fDepth)
        {
          fCasters += 1;
          fCasterDepth += fMap;
        }
      }
    }
    float fShadow = (1 - fSum / 16) * fEdge;
    if (fCasters > 0)
      fShadow *= saturate(1 - g_Draw.vShadowParams.y * (fDepth - fCasterDepth / fCasters));
    fLit *= 1 - fShadow;
  }
  return fLit;
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
