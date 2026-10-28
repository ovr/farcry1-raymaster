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
//	      doubles its rgb, DF_VALPHA_ONLY keeps only its alpha), times the
//	      vertex lighting (DF_LIGHTING, rgb only):
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
//	      sampler from bits 4-7): DF_TEX1_LIGHTMAP rgb *= 4 * tex1 + vAmbient
//	      (DF_LIGHTMAP_DIR, a dot3 light map: tex1 * saturate(D.z) * D.a, D =
//	      tex2 at uv1, rgb * 2 - 1), DF_TEX1_DETAIL rgb = 2 * rgb + tex1 - 0.5,
//	      else *= tex1
//	    DF_TERRAIN_CLIFF (terrain with cliffs): rgb = 2 * rgb * lerp(1, 2 * tex2,
//	      w.x) * lerp(1, 2 * tex3, w.y), w = vertex colour rg * vTexGen1V.xy, uv
//	      of tex2 from the world position with vParams0 / 1, of tex3 with 2 / 3
//	    DF_DECAL_FADE (decals): a *= 1 - f(r), r = 2 * |uv0 - 0.5| (1 on the circle
//	      inside the decal's square), f(r) = (r + smoothstep(0, 1, r)) / 2
//	    DF_ALPHATEST: discard when a < fAlphaRef (DF_ALPHATEST_LESS: when a >= fAlphaRef);
//	      then DF_ALPHA_ONE: a = 1
//	    DF_DETAIL_LAYER (a terrain detail layer, multiplied in twice): rgb =
//	      lerp(0.5, tex0, a * the fade by the distance), a = 1; DF_DETAIL_BUMP
//	      (bump mapped layer) first scales tex0 by 1 + (sun N'.L - sun N.L) /
//	      (vParams1.x + saturate(N.L)), clamped to 0..2: N the vertex normal, N'
//	      the normal map (tex1 at uv0) in its frame (tangent along vTexGenU made
//	      orthogonal to N, binormal along vTexGenV, the map's y times vParams1.y)
//	    DF_SHADOWS: the sun's visibility through the draw's shadow maps
//	      (SunVisibility, Common.hlsli) v: DF_LIGHTING colour = lerp(the vertex
//	      light without the sun, with it, v) * texture; DF_TEX1_LIGHTMAP rgb *=
//	      4 * v * tex1 + vAmbient; DF_SHADOW_PASS rgb = colour * tex0, a = 1 - v
//	    DF_PIXEL_LIGHTING: DF_LIGHTING is lit here instead of per vertex
//	      (DrawLighting: the same sum, but the lights' colours clamped to 1 and
//	      point lights falling off as saturate(1 - d / radius)), with the
//	      vertex normal or, with DF_BUMP (bump mapping), N' = the normal map
//	      (tex3 at uv0, its y times vParams1.y) in the vertex's tangent frame;
//	      the dot3 light map takes N'.D for D.z
//	    DF_CAUSTICS (the terrain under the ocean, multiplied in twice): rgb =
//	      0.5 + 0.5 * k * (I - 1), I = min of two layers of the caustics texture
//	      tex0 drifting apart, sampled where the sun's refracted ray crossed
//	      the surface (vParams0: x water level, y 1 / pattern size, z time,
//	      w k; vParams1: xy the ray's shift per metre of depth, z the depth
//	      where the caustics are gone, w the texture's scale to a mean of 1),
//	      fading in over the first metre of depth
//	    DF_FOG (with vFogParams.w set): linear fog by the distance to the
//	      camera, per pixel (towards 0.5 with DF_DETAIL_LAYER, DF_CAUSTICS)
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
  float4 vColorNoSun : TEXCOORD5; //!< DF_SHADOWS with DF_LIGHTING: vColor lit without the sun
  float3 vTangent : TEXCOORD6;  //!< DF_BUMP: the tangent frame in world space, not normalised
  float3 vBinormal : TEXCOORD7;
  float3 vTNormal : TEXCOORD8;
};

//! The light of the sun, the ambient colour and the draw's lights at world position vPos with the
//! normalised normal vN, which multiplies the colour (DF_LIGHTING): 2 * (saturate(ambient / 2 +
//! sun N.L + light 0) + the sum of saturate(light) of the other lights), light = colour * N.L *
//! attenuation, with the sun; vNoSunLight the same without it. Per vertex a point light attenuates
//! by 1 / (a0 + a1 d) up to its range; per pixel (bPerPixel, DF_BUMP) by saturate(1 - d / radius),
//! and each light's colour is clamped to 1.
float3 DrawLighting(float3 vPos, float3 vN, bool bPerPixel, out float3 vNoSunLight)
{
  float3 vLight = 0.5 * g_Draw.vAmbient.rgb + g_Frame.vSunColor.rgb * saturate(dot(vN, g_Frame.vSunDir.xyz));
  float3 vNoSun = 0.5 * g_Draw.vAmbient.rgb;
  float3 vOthers = 0;
  for (uint i = 0; i < min(g_Draw.nLights, XR_MAX_DRAW_LIGHTS); i++)
  {
    float4 vLightPos = g_Draw.vLightPos[i];
    float4 vAtten = g_Draw.vLightAtten[i];
    float3 vToLight = vLightPos.xyz;
    float fAtten = 1;
    if (vLightPos.w == 1)
    {
      vToLight = vLightPos.xyz - vPos;
      float fDist = length(vToLight);
      if (bPerPixel)
        fAtten = saturate(1 - fDist * vAtten.w);
      else
        fAtten = fDist < vAtten.z ? 1 / (vAtten.x + vAtten.y * fDist) : 0;
      vToLight /= max(fDist, 1e-4);
    }
    float3 vColor = g_Draw.vLightColor[i].rgb;
    if (bPerPixel)
      vColor = saturate(vColor);
    float3 vThis = vColor * saturate(dot(vN, vToLight)) * fAtten;
    if (i == 0)
    {
      vLight += vThis;
      vNoSun += vThis;
    }
    else
      vOthers += saturate(vThis);
  }
  vNoSunLight = 2 * (saturate(vNoSun) + vOthers);
  return 2 * (saturate(vLight) + vOthers);
}

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
    if (HasFlag(DF_VALPHA_ONLY))
      vVertexColor.rgb = 1;
    vColor *= vVertexColor;
  }
  float4 vColorNoSun = vColor;
  if (HasFlag(DF_LIGHTING) && !HasFlag(DF_PIXEL_LIGHTING))
  {
    float3 vNoSunLight;
    vColor.rgb *= DrawLighting(vWorldPos, SafeNormalize(vNormal), false, vNoSunLight);
    vColorNoSun.rgb *= vNoSunLight;
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
  Out.vColorNoSun = vColorNoSun;
  Out.vTangent = mul(float4(In.vTangent, 0), g_Draw.mWorld).xyz;
  Out.vBinormal = mul(float4(In.vBinormal, 0), g_Draw.mWorld).xyz;
  Out.vTNormal = mul(float4(In.vTNormal, 0), g_Draw.mWorld).xyz;
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

  // Bump mapping: the normal map in tangent space (vMap) and in world space (vBumpN).
  float3 vMap = float3(0, 0, 1);
  float3 vBumpN = 0;
  if (HasFlag(DF_BUMP))
  {
    vMap = SafeNormalize(SampleTexture(g_Draw.nTex3, XR_SAMPLER_LINEAR_WRAP, In.vTex0).rgb * 2 - 1);
    vMap.y *= g_Draw.vParams1.y;
    vBumpN = SafeNormalize(vMap.x * In.vTangent + vMap.y * In.vBinormal + vMap.z * In.vTNormal);
  }

  float fSun = HasFlag(DF_SHADOWS) ? SunVisibility(In.vWorldPos, In.vNormal, In.vPos.xy) : 1;
  float4 vVertexColor = In.vColor;
  if (HasFlag(DF_LIGHTING) && HasFlag(DF_PIXEL_LIGHTING))
  {
    float3 vNoSunLight;
    float3 vN = HasFlag(DF_BUMP) ? vBumpN : SafeNormalize(In.vNormal);
    float3 vLight = DrawLighting(In.vWorldPos, vN, true, vNoSunLight);
    vVertexColor.rgb *= lerp(vNoSunLight, vLight, fSun);
  }
  else if (HasFlag(DF_SHADOWS) && HasFlag(DF_LIGHTING))
    vVertexColor.rgb = lerp(In.vColorNoSun.rgb, In.vColor.rgb, fSun);

  float4 vColor;
  if (HasFlag(DF_TEX0_ALPHA_ONLY))
    vColor = float4(vVertexColor.rgb, vVertexColor.a * vTex.a);
  else
    vColor = vVertexColor * vTex;
  if (HasFlag(DF_SHADOW_PASS))
    vColor.a = 1 - fSun;
  if (HasFlag(DF_ALPHA_FROM_TEX))
    vColor.a = vTex.a;

  if (HasFlag(DF_TEX1 | DF_TEX1_DETAIL | DF_TEX1_LIGHTMAP))
  {
    float4 vTex1 = SampleTexture(g_Draw.nTex1, nSampler1, In.vTex1);
    if (HasFlag(DF_TEX1_LIGHTMAP))
    {
      float3 vLight = vTex1.rgb;
      if (HasFlag(DF_LIGHTMAP_DIR))
      {
        float4 vDir = SampleTexture(g_Draw.nTex2, nSampler1, In.vTex1);
        vLight *= saturate(dot(vMap, vDir.xyz * 2 - 1)) * vDir.a;
      }
      vColor.rgb *= 4 * fSun * vLight + g_Draw.vAmbient.rgb;
    }
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

  if (HasFlag(DF_DECAL_FADE))
  {
    float r = saturate(2 * length(In.vTex0 - 0.5));
    vColor.a *= 1 - 0.5 * (r + smoothstep(0, 1, r));
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
    float3 vDetail = vTex.rgb;
    if (HasFlag(DF_DETAIL_BUMP))
    {
      float3 vN = SafeNormalize(In.vNormal);
      float3 vT = SafeNormalize(g_Draw.vTexGenU.xyz - vN * dot(g_Draw.vTexGenU.xyz, vN));
      float3 vB = cross(vN, vT);
      if (dot(vB, g_Draw.vTexGenV.xyz) < 0)
        vB = -vB;
      float3 vMap = SampleTexture(g_Draw.nTex1, nSampler1, In.vTex0).rgb * 2 - 1;
      float3 vBump = SafeNormalize(vMap.x * vT + vMap.y * g_Draw.vParams1.y * vB + vMap.z * vN);
      float3 vL = g_Frame.vSunDir.xyz;
      float fN = dot(vN, vL);
      vDetail *= clamp(1 + (dot(vBump, vL) - fN) / (g_Draw.vParams1.x + saturate(fN)), 0, 2);
    }
    vColor = float4(lerp(0.5, vDetail, fWeight), 1);
    vFogColor = 0.5;
  }

  if (HasFlag(DF_CAUSTICS))
  {
    float fDepth = g_Draw.vParams0.x - In.vWorldPos.z;
    float2 vP = (In.vWorldPos.xy + g_Draw.vParams1.xy * fDepth) * g_Draw.vParams0.y;
    float t = g_Draw.vParams0.z;
    float c1 = SampleTexture(g_Draw.nTex0, XR_SAMPLER_LINEAR_WRAP, vP + float2(0.031, 0.017) * t).r;
    float c2 = SampleTexture(g_Draw.nTex0, XR_SAMPLER_LINEAR_WRAP, vP * 1.37 + float2(-0.023, 0.029) * t).r;
    float fLight = min(c1, c2) * g_Draw.vParams1.w;
    float fFade = saturate(fDepth) * saturate(1 - fDepth / g_Draw.vParams1.z);
    vColor = float4(saturate(0.5 + 0.5 * g_Draw.vParams0.w * fFade * (fLight - 1)).xxx, 1);
    vFogColor = 0.5;
  }

  if (HasFlag(DF_FOG) && g_Frame.vFogParams.w != 0)
  {
    float fDist = distance(In.vWorldPos, g_Frame.vCameraPos.xyz);
    float fFog = saturate((g_Frame.vFogParams.y - fDist) * g_Frame.vFogParams.z);
    vColor.rgb = lerp(vFogColor, vColor.rgb, fFog);
  }

  if (HasFlag(DF_SHADOW_DEBUG))
    vColor = float4(fSun, fSun, fSun, 1);
  return vColor;
}
