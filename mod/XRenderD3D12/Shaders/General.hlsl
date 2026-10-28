//////////////////////////////////////////////////////////////////////
//
//	File: General.hlsl
//	Description: The general program: one vertex and one pixel shader for
//	everything the DF_* flags of Constants.h describe.
//
//	Each flag is a branch on SDrawConstants::nFlags, the same for the
//	whole draw. In order:
//	  vertex shader
//	    position: DF_SCREEN_POS pixels (ScreenToClip), else mWorldViewProj;
//	      the world position (mWorld) for texgen, cube reflections and fog
//	    colour: vColor, times the vertex colour (DF_VCOLOR; DF_VCOLOR_X2 also
//	      doubles its rgb), times the vertex lighting (DF_LIGHTING, rgb only):
//	      2 * (saturate(ambient / 2 + sun N.L + light 0) + the sum of
//	      saturate(light) of the draw's other lights), light = colour * N.L,
//	      point lights times 1 / (a0 + a1 d): each light is clamped on its own,
//	      as in a pass of its own; the light of a texture modulated by 2
//	    uv0: DF_TEXGEN_WORLD (dot(float4(world pos, 1), vTexGenU / V)), then
//	      DF_TEXMATRIX
//	  pixel shader
//	    tex0: DF_CUBE0 (g_Cubes, reflected view vector), else DF_TEX0_ARRAY
//	      (g_TextureArrays, slice floor(u / 2) at (u - 2 * slice, v)), else DF_TEX0
//	      (g_Textures), else white; sampler from nSamplers bits 0-3
//	    colour * tex0; DF_TEX0_ALPHA_ONLY: rgb = colour, a = colour a * tex a;
//	      DF_ALPHA_FROM_TEX: a = tex a
//	    uv1: DF_TEXGEN1_WORLD (dot(float4(world pos, 1), vTexGen1U / V)), else
//	      the vertex's second texture coordinates
//	    tex1 with uv1 (any of DF_TEX1, DF_TEX1_DETAIL, DF_TEX1_LIGHTMAP;
//	      sampler from bits 4-7): DF_TEX1_LIGHTMAP rgb *= 4 * tex1 + vAmbient,
//	      DF_TEX1_DETAIL rgb = 2 * rgb + tex1 - 0.5, else *= tex1
//	    DF_TERRAIN_CLIFF (terrain with cliffs): rgb = 2 * rgb * lerp(1, 2 * tex2,
//	      w.x) * lerp(1, 2 * tex3, w.y), w = vertex colour rg * vTexGen1V.xy, uv
//	      of tex2 from the world position with vParams0 / 1, of tex3 with 2 / 3
//	    DF_ALPHATEST: discard when a < fAlphaRef (DF_ALPHATEST_LESS: when a >= fAlphaRef);
//	      then DF_ALPHA_ONE: a = 1
//	    DF_DETAIL_LAYER (a terrain detail layer, multiplied in twice): rgb =
//	      lerp(0.5, tex0, a * the fade by the distance), a = 1
//	    DF_FOG (with vFogParams.w set): linear fog by the distance to the
//	      camera, per pixel (towards 0.5 with DF_DETAIL_LAYER)
//
//////////////////////////////////////////////////////////////////////

#include "Common.hlsli"

struct SVertexOutput
{
  float4 vPos : SV_Position;
  float4 vColor : COLOR0;       //!< vColor * vertex colour * lighting
  float2 vTex0 : TEXCOORD0;
  float2 vTex1 : TEXCOORD1;
  float3 vWorldPos : TEXCOORD2;
  float3 vNormal : TEXCOORD3;   //!< world space, not normalised
  float2 vCliff : TEXCOORD4;    //!< DF_TERRAIN_CLIFF: the weights of the two cliff textures
};

SVertexOutput VSMain(SVertexInput In)
{
  SVertexOutput Out;

  float3 vWorldPos;
  if (HasFlag(DF_SCREEN_POS))
  {
    Out.vPos = ScreenToClip(In.vPos);
    vWorldPos = In.vPos.xyz;
  }
  else
  {
    float4 vPos = float4(In.vPos.xyz, 1);
    Out.vPos = mul(vPos, g_Draw.mWorldViewProj);
    vWorldPos = mul(vPos, g_Draw.mWorld).xyz;
  }
  float3 vNormal = mul(float4(In.vNormal, 0), g_Draw.mWorld).xyz;

  float4 vColor = g_Draw.vColor;
  if (HasFlag(DF_VCOLOR | DF_VCOLOR_X2))
  {
    float4 vVertexColor = DecodeVertexColor(In.vColor0);
    if (HasFlag(DF_VCOLOR_X2))
      vVertexColor.rgb *= 2;
    vColor *= vVertexColor;
  }
  if (HasFlag(DF_LIGHTING))
  {
    float3 vN = SafeNormalize(vNormal);
    float3 vLight = 0.5 * g_Draw.vAmbient.rgb + g_Frame.vSunColor.rgb * saturate(dot(vN, g_Frame.vSunDir.xyz));
    float3 vOthers = 0;
    for (uint i = 0; i < min(g_Draw.nLights, XR_MAX_DRAW_LIGHTS); i++)
    {
      float4 vLightPos = g_Draw.vLightPos[i];
      float3 vToLight = vLightPos.xyz;
      float fAtten = 1;
      if (vLightPos.w == 1)
      {
        vToLight = vLightPos.xyz - vWorldPos;
        float fDist = length(vToLight);
        float4 vAtten = g_Draw.vLightAtten[i];
        fAtten = fDist < vAtten.z ? 1 / (vAtten.x + vAtten.y * fDist) : 0;
        vToLight /= max(fDist, 1e-4);
      }
      float3 vThis = g_Draw.vLightColor[i].rgb * saturate(dot(vN, vToLight)) * fAtten;
      if (i == 0)
        vLight += vThis;
      else
        vOthers += saturate(vThis);
    }
    vColor.rgb *= 2 * (saturate(vLight) + vOthers);
  }

  float2 vTex0 = In.vTex0;
  if (HasFlag(DF_TEXGEN_WORLD))
  {
    float4 vPos = float4(vWorldPos, 1);
    vTex0 = float2(dot(vPos, g_Draw.vTexGenU), dot(vPos, g_Draw.vTexGenV));
  }
  if (HasFlag(DF_TEXMATRIX))
  {
    float3 vUV = float3(vTex0, 1);
    vTex0 = float2(dot(vUV, g_Draw.vTexMat0.xyz), dot(vUV, g_Draw.vTexMat1.xyz));
  }

  Out.vColor = vColor;
  Out.vTex0 = vTex0;
  Out.vTex1 = In.vTex1;
  if (HasFlag(DF_TEXGEN1_WORLD))
  {
    float4 vPos = float4(vWorldPos, 1);
    Out.vTex1 = float2(dot(vPos, g_Draw.vTexGen1U), dot(vPos, g_Draw.vTexGen1V));
  }
  Out.vWorldPos = vWorldPos;
  Out.vNormal = vNormal;
  Out.vCliff = HasFlag(DF_TERRAIN_CLIFF) ? DecodeVertexColor(In.vColor0).rg : 0;
  return Out;
}

float4 PSMain(SVertexOutput In) : SV_Target
{
  uint nSampler0 = g_Draw.nSamplers & 15;
  uint nSampler1 = (g_Draw.nSamplers >> 4) & 15;

  float4 vTex = float4(1, 1, 1, 1);
  if (HasFlag(DF_CUBE0))
  {
    float3 vView = In.vWorldPos - g_Frame.vCameraPos.xyz;
    vTex = SampleCube(g_Draw.nTex0, nSampler0, reflect(vView, SafeNormalize(In.vNormal)));
  }
  else if (HasFlag(DF_TEX0_ARRAY))
  {
    float fSlice = floor(In.vTex0.x * 0.5);
    vTex = SampleTextureArray(g_Draw.nTex0, nSampler0, float3(In.vTex0.x - 2 * fSlice, In.vTex0.y, fSlice));
  }
  else if (HasFlag(DF_TEX0))
    vTex = SampleTexture(g_Draw.nTex0, nSampler0, In.vTex0);

  float4 vColor;
  if (HasFlag(DF_TEX0_ALPHA_ONLY))
    vColor = float4(In.vColor.rgb, In.vColor.a * vTex.a);
  else
    vColor = In.vColor * vTex;
  if (HasFlag(DF_ALPHA_FROM_TEX))
    vColor.a = vTex.a;

  if (HasFlag(DF_TEX1 | DF_TEX1_DETAIL | DF_TEX1_LIGHTMAP))
  {
    float4 vTex1 = SampleTexture(g_Draw.nTex1, nSampler1, In.vTex1);
    if (HasFlag(DF_TEX1_LIGHTMAP))
      vColor.rgb *= 4 * vTex1.rgb + g_Draw.vAmbient.rgb;
    else if (HasFlag(DF_TEX1_DETAIL))
      vColor.rgb = 2 * vColor.rgb + vTex1.rgb - 0.5;
    else
      vColor *= vTex1;
  }

  if (HasFlag(DF_TERRAIN_CLIFF))
  {
    float4 vPos = float4(In.vWorldPos, 1);
    float2 vW = In.vCliff * g_Draw.vTexGen1V.xy;
    vColor.rgb *= 2;
    if (vW.x > 0)
      vColor.rgb *= lerp(1, 2 * SampleTexture(g_Draw.nTex2, XR_SAMPLER_LINEAR_WRAP, float2(dot(vPos, g_Draw.vParams0), dot(vPos, g_Draw.vParams1))).rgb, vW.x);
    if (vW.y > 0)
      vColor.rgb *= lerp(1, 2 * SampleTexture(g_Draw.nTex3, XR_SAMPLER_LINEAR_WRAP, float2(dot(vPos, g_Draw.vParams2), dot(vPos, g_Draw.vParams3))).rgb, vW.y);
  }

  if (HasFlag(DF_ALPHATEST))
  {
    if (HasFlag(DF_ALPHATEST_LESS))
      clip(g_Draw.fAlphaRef - vColor.a - 1.0 / 512);
    else
      clip(vColor.a - g_Draw.fAlphaRef);
  }
  if (HasFlag(DF_ALPHA_ONE))
    vColor.a = 1;

  float3 vFogColor = g_Frame.vFogColor.rgb;
  if (HasFlag(DF_DETAIL_LAYER))
  {
    float fDist = distance(In.vWorldPos, g_Frame.vCameraPos.xyz);
    float fWeight = vColor.a * saturate((g_Draw.vParams0.y - fDist) * g_Draw.vParams0.z);
    vColor = float4(lerp(0.5, vTex.rgb, fWeight), 1);
    vFogColor = 0.5;
  }

  if (HasFlag(DF_FOG) && g_Frame.vFogParams.w != 0)
  {
    float fDist = distance(In.vWorldPos, g_Frame.vCameraPos.xyz);
    float fFog = saturate((g_Frame.vFogParams.y - fDist) * g_Frame.vFogParams.z);
    vColor.rgb = lerp(vFogColor, vColor.rgb, fFog);
  }

  return vColor;
}
