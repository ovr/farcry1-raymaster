// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
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
//	      s * (saturate(ambient / s + sun N.L + light 0) + the sum of
//	      saturate(light) of the draw's other lights), light = colour * N.L,
//	      point lights times 1 / (a0 + a1 d): each light is clamped on its own,
//	      as in a pass of its own; s = vAmbient.w: 2 the light of a texture
//	      modulated by 2, 1 of a texture modulated
//	    uv0: DF_TEXGEN_WORLD (dot(float4(world pos, 1), vTexGenU / V)), then
//	      DF_TEXMATRIX
//	  pixel shader
//	    tex0: DF_CUBE0 (g_Cubes, the view vector reflected about the normal,
//	      N' with DF_BUMP; DF2_GLASS: lerp(cube(T), cube(R), F), T the view
//	      vector refracted with vParams2.w, F = saturate(vParams2.z +
//	      vParams2.x * (1 - N.V)^vParams2.y); DF2_REFL_AMOUNT: rgb = cube * tex5
//	      + tex4 * vAddColor.rgb, both at uv0), else DF_TEX0_ARRAY
//	      (g_TextureArrays, slice floor(u / 2) at (u - 2 * slice, v)), else DF_TEX0
//	      (g_Textures; DF2_BUMP_OFFSET at uv0 + (tex3 at uv1 rg * 2 - 1) *
//	      vParams0.x), else white; sampler from nSamplers bits 0-3
//	      (DF2_TERRAIN_LIGHT, the terrain under dynamic lights: rgb as 0.75 *
//	      rgb + 0.25)
//	    colour * tex0; DF_TEX0_ALPHA_ONLY: rgb = colour, a = colour a * tex a;
//	      DF_ALPHA_FROM_TEX: a = tex a
//	    uv1: DF_TEXGEN1_WORLD (dot(float4(world pos, 1), vTexGen1U / V)), else
//	      the vertex's second texture coordinates
//	    tex1 with uv1 (any of DF_TEX1, DF_TEX1_DETAIL, DF_TEX1_LIGHTMAP;
//	      sampler from bits 4-7): DF_TEX1_LIGHTMAP rgb *= 4 * tex1 + vAmbient
//	      (DF_LIGHTMAP_DIR, a dot3 light map: tex1 * saturate(D.z) * D.a, D =
//	      tex2 at uv1, rgb * 2 - 1), DF_TEX1_DETAIL rgb = 2 * rgb + tex1 - 0.5,
//	      else *= tex1
//	    DF2_ALPHA_GLOW: rgb += tex0 rgb * tex0 a (the texture's alpha its
//	      glow, unlit)
//	    DF_TERRAIN_CLIFF (terrain with cliffs): rgb = 2 * rgb * lerp(1, 2 * tex2,
//	      w.x) * lerp(1, 2 * tex3, w.y), w = vertex colour rg * vTexGen1V.xy, uv
//	      of tex2 from the world position with vParams0 / 1, of tex3 with 2 / 3
//	    DF_DECAL_FADE (decals): a *= 1 - f(r), r = 2 * |uv0 - 0.5| (1 on the circle
//	      inside the decal's square), f(r) = (r + smoothstep(0, 1, r)) / 2
//	    DF2_GLOW_FALLOFF (procedural flares): rgb *= saturate(1 - u)^vParams0.x,
//	      u of uv0 0 on the glow's inner edge, 1 on its outer edge
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
//	      point lights falling off as saturate(1 - d / radius); on the terrain
//	      (DF2_TERRAIN_LIGHT) but for a projector as saturate(1 - (d /
//	      radius)^2)), with the
//	      vertex normal or, with DF_BUMP (bump mapping), N' = the normal map
//	      (tex3 at uv0, its y times vParams1.y) in the vertex's tangent frame;
//	      the dot3 light map takes N'.D for D.z
//	    DF_SPECULAR (with DF_PIXEL_LIGHTING): rgb += 2 * vSpecular.rgb * gloss
//	      (texture alpha if vSpecular.w, else 1) * the sum over the sun (times
//	      v) and the lights of their specular colour (clamped to 1) * falloff
//	      * saturate(R.V)^32, R the light reflected about the normal
//	    DF2_ADD_TEX4: rgb += tex4 at uv0 * vAddColor.rgb + vAddColor.w (a glow
//	      map or a texture added, unlit); with DF2_GLASS also a *= tex4 a
//	    DF2_PROJECTOR (a projected light's pass): the colour times the cube
//	      g_Cubes[nTex5] at the lookup vector float4(world pos, 1) * mParams0
//	    DF2_MODULATE (a decal multiplied into the frame): rgb = saturate(rgb +
//	      vAddColor.w), a = 1
//	    DF_CAUSTICS (the terrain under the ocean, multiplied in twice): rgb =
//	      0.5 + 0.5 * k * (I - 1), I = min of two layers of the caustics texture
//	      tex0 drifting apart, sampled where the sun's refracted ray crossed
//	      the surface (vParams0: x water level, y 1 / pattern size, z time,
//	      w k; vParams1: xy the ray's shift per metre of depth, z the depth
//	      where the caustics are gone, w the texture's scale to a mean of 1),
//	      fading in over the first metre of depth
//	    DF2_FOG_VOLUME (a fog volume over an object in it): the volume's colour
//	      (vColor.rgb), alpha FogVolumeAlpha (times the alpha with vParams0.w);
//	      nothing below applies
//	    DF_FOG (with vFogParams.w set): linear fog by the depth along the
//	      view axis (SceneFog), per pixel (towards 0.5 with DF_DETAIL_LAYER, DF_CAUSTICS, 0
//	      with DF2_FOG_BLACK, 1 with DF2_MODULATE)
//	    DF2_HEAT (heat vision): before the fog, the colour through HeatVision
//	      (Common.hlsli)
//	  DF2_MATERIAL_LIGHT lights DF_LIGHTING per pixel as the fixed function
//	  lights a material (MaterialLighting): saturate(ambient + vColor * the
//	  lights) * texture + the highlights of vSpecular.
//	  DF2_NO_SUN leaves the sun out of DF_LIGHTING (a light pass of its own).
//	  DF2_SCOPE_LENS (the glass of a scope) replaces the pixel shader with
//	  ScopeLens.
//	The upscaler's jitter (vJitter) moves every clip position. Compiled with
//	XR_VELOCITY (the draws into an upscaler's scene that write its motion
//	vectors or its depth) the pixel shader also writes the motion vector of
//	the pixel (MotionVector) from mWorldViewProj and mPrevWorldViewProj to
//	SV_Target1 and its depth to SV_Target2 (the pipeline masks what the draw
//	does not write).
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
#ifdef XR_VELOCITY
  float4 vCurClip : TEXCOORD9;  //!< the clip position without the jitter
  float4 vPrevClip : TEXCOORD10; //!< the same in the frame before
#endif
};

//! The light of the sun, the ambient colour and the draw's lights at world position vPos with the
//! normalised normal vN, which multiplies the colour (DF_LIGHTING): s * (saturate(ambient / s +
//! sun N.L + light 0) + the sum of saturate(light) of the other lights), light = colour * N.L *
//! attenuation, s = vAmbient.w (2: a texture modulated by 2, 1: modulated), with the sun;
//! vNoSunLight the same without it. Per vertex a point light attenuates by 1 / (a0 + a1 d) up to
//! its range; per pixel (bPerPixel, DF_BUMP) by saturate(1 - d / radius), and each light's colour
//! is clamped to 1.
//! The specular power of a light: saturate(R.V)^32, R = L reflected about N, 0 where N.L <= 0.
float Specular(float3 vN, float3 vToLight, float3 vToEye)
{
  float fNL = dot(vN, vToLight);
  float fRV = saturate(dot(2 * fNL * vN - vToLight, vToEye));
  float f2 = fRV * fRV, f4 = f2 * f2, f8 = f4 * f4, f16 = f8 * f8;
  return fNL > 0 ? f16 * f16 : 0;
}

float3 DrawLighting(float3 vPos, float3 vN, bool bPerPixel, out float3 vNoSunLight, out float3 vSunSpec, out float3 vOtherSpec)
{
  float fScale = g_Draw.vAmbient.w;
  bool bSun = !HasFlag2(DF2_NO_SUN);
  float3 vLight = g_Draw.vAmbient.rgb / fScale + (bSun ? g_Frame.vSunColor.rgb * saturate(dot(vN, g_Frame.vSunDir.xyz)) : 0);
  float3 vNoSun = g_Draw.vAmbient.rgb / fScale;
  float3 vOthers = 0;
  bool bSpecular = bPerPixel && HasFlag(DF_SPECULAR);
  float3 vToEye = SafeNormalize(g_Frame.vCameraPos.xyz - vPos);
  vSunSpec = bSpecular && bSun ? saturate(g_Frame.vSunSpec.rgb) * Specular(vN, g_Frame.vSunDir.xyz, vToEye) : 0;
  vOtherSpec = 0;
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
      {
        float x = fDist * vAtten.w;
        fAtten = saturate(1 - (HasFlag2(DF2_TERRAIN_LIGHT) && !HasFlag2(DF2_PROJECTOR) ? x * x : x));
      }
      else
        fAtten = fDist < vAtten.z ? 1 / (vAtten.x + vAtten.y * fDist) : 0;
      vToLight /= max(fDist, 1e-4);
    }
    float3 vColor = g_Draw.vLightColor[i].rgb;
    if (bPerPixel)
      vColor = saturate(vColor);
    float3 vThis = vColor * saturate(dot(vN, vToLight)) * fAtten;
    if (bSpecular)
      vOtherSpec += saturate(g_Draw.vLightSpec[i].rgb) * (fAtten * Specular(vN, vToLight, vToEye));
    if (i == 0)
    {
      vLight += vThis;
      vNoSun += vThis;
    }
    else
      vOthers += saturate(vThis);
  }
  vNoSunLight = fScale * (saturate(vNoSun) + vOthers);
  return fScale * (saturate(vLight) + vOthers);
}

//! The light of a material as the fixed function lights it (DF2_MATERIAL_LIGHT) at vPos with the
//! normalised normal vN: vDiffuse = saturate(ambient + vColor * the sum of colour * N.L *
//! attenuation), vSpecular = saturate(vSpecular * the sum of the specular colour * (N.H)^power *
//! attenuation) where N.L > 0, H halfway between the light and the eye, power vParams3.x; the sun's
//! terms times its visibility fSun, point lights attenuated as per vertex.
void MaterialLighting(float3 vPos, float3 vN, float fSun, out float3 vDiffuse, out float3 vSpecular)
{
  float3 vToEye = SafeNormalize(g_Frame.vCameraPos.xyz - vPos);
  float fPower = max(g_Draw.vParams3.x, 1);
  float3 vLight = 0, vSpec = 0;
  if (!HasFlag2(DF2_NO_SUN))
  {
    float fNL = dot(vN, g_Frame.vSunDir.xyz);
    vLight += fSun * g_Frame.vSunColor.rgb * saturate(fNL);
    if (fNL > 0)
      vSpec += fSun * g_Frame.vSunSpec.rgb * pow(saturate(dot(vN, SafeNormalize(g_Frame.vSunDir.xyz + vToEye))), fPower);
  }
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
      fAtten = fDist < vAtten.z ? 1 / (vAtten.x + vAtten.y * fDist) : 0;
      vToLight /= max(fDist, 1e-4);
    }
    float fNL = dot(vN, vToLight);
    vLight += g_Draw.vLightColor[i].rgb * saturate(fNL) * fAtten;
    if (fNL > 0)
      vSpec += g_Draw.vLightSpec[i].rgb * pow(saturate(dot(vN, SafeNormalize(vToLight + vToEye))), fPower) * fAtten;
  }
  vDiffuse = saturate(g_Draw.vAmbient.rgb + g_Draw.vColor.rgb * vLight);
  vSpecular = saturate(g_Draw.vSpecular.rgb * vSpec);
}

//! How much of a fog volume's colour covers the point vPos of an object in it (DF2_FOG_VOLUME):
//! the fog lies under the volume's layer (vParams0.x). Seen from above, the fog of a point grows
//! from nothing at the layer to all of it 5 m down, the faster the nearer the camera is to the
//! layer (the view crosses less of the fog the steeper it looks down into it), times the fog of
//! the depth d along the view axis (ViewDepth), 1 - exp(-d * vParams0.y), 99 % at twice the
//! volume's fog distance. Seen from inside, every point under the layer is in it, with the fog of
//! its depth (FogInsideVolume). The volume's cap (vParams0.z), the layer seen from below, has the
//! fog of the depth from inside and none from above.
float FogVolumeAlpha(float3 vPos)
{
  float fCamera = g_Frame.vCameraPos.z - g_Draw.vParams0.x;
  float fDepth = g_Draw.vParams0.x - vPos.z;
  if (fCamera <= 0)
    return (g_Draw.vParams0.z > 0 || fDepth > 0) ? FogInsideVolume(ViewDepth(vPos), g_Draw.vParams0.y) : 0;
  if (g_Draw.vParams0.z > 0 || fDepth <= 0)
    return 0;
  float fGate = 1 - pow(1 - saturate(fDepth / 5), lerp(3, 1.6, saturate(fCamera / 5)));
  return fGate * (1 - exp(-ViewDepth(vPos) * g_Draw.vParams0.y));
}

//! The glass of a scope (DF2_SCOPE_LENS): the copy of the scene (tex0) around the middle of the
//! screen, where the scope aims, as through a real one: each pixel shows the scene at its own offset
//! from the middle of the glass times vParams0.x from there. The middle of the glass is uv0 (0.5,
//! 0.5); the offset in pixels comes from uv0 through its screen derivatives, so the picture stands
//! upright whichever way the glass's texture coordinates run. The wave slopes (tex1, unit RMS) times
//! vParams0.y move it in uv0: the glass is not quite even. rgb times vColor.rgb; alpha vColor.a *
//! 2^-(r / vParams0.z)^4, r = |uv0 - 0.5|.
float4 ScopeLens(SVertexOutput In)
{
  float2 vMiddle = 0.5 * g_Frame.vScreen.xy;
  float2 vUV = In.vTex0;
  float2 s1 = (g_Textures[g_Draw.nTex1].Sample(g_SamplerLinearWrap, 2 * vUV).rg * 255 - 128) / 32;
  float2 s2 = (g_Textures[g_Draw.nTex1].Sample(g_SamplerLinearWrap, 2 * vUV.yx + 0.5).gr * 255 - 128) / 32;
  float2 vOffset = vUV - 0.5 + (s1 + s2) * (0.7071 * g_Draw.vParams0.y);
  // The offset in pixels p: vOffset = p.x * ddx(uv0) + p.y * ddy(uv0).
  float2 a = ddx(vUV), b = ddy(vUV);
  float fDet = a.x * b.y - b.x * a.y;
  float2 vPixels = abs(fDet) > 1e-12 ? float2(vOffset.x * b.y - b.x * vOffset.y, a.x * vOffset.y - vOffset.x * a.y) / fDet : 0;
  float3 vScene = g_Textures[g_Draw.nTex0].SampleLevel(g_SamplerLinearClamp, (vMiddle + vPixels * g_Draw.vParams0.x) * g_Frame.vScreen.zw, 0).rgb;

  float2 d = (vUV - 0.5) / g_Draw.vParams0.z;
  float r2 = dot(d, d);
  return float4(vScene * g_Draw.vColor.rgb, g_Draw.vColor.a * exp2(-r2 * r2));
}

SVertexOutput VSMain(SVertexInput In)
{
  SVertexOutput Out;

  float3 vWorldPos;
  if (HasFlag(DF_SCREEN_POS))
  {
    Out.vPos = ScreenToClip(In.vPos);
    vWorldPos = In.vPos.xyz;
#ifdef XR_VELOCITY
    Out.vCurClip = Out.vPos;
    Out.vPrevClip = Out.vPos;
#endif
  }
  else
  {
    float4 vPos = float4(In.vPos.xyz, 1);
    Out.vPos = mul(vPos, g_Draw.mWorldViewProj);
    vWorldPos = mul(vPos, g_Draw.mWorld).xyz;
#ifdef XR_VELOCITY
    Out.vCurClip = Out.vPos;
    Out.vPrevClip = mul(vPos, g_Draw.mPrevWorldViewProj);
#endif
  }
  Out.vPos = Jitter(Out.vPos);
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
  if (HasFlag(DF_LIGHTING) && !HasFlag(DF_PIXEL_LIGHTING) && !HasFlag2(DF2_MATERIAL_LIGHT))
  {
    float3 vNoSunLight, vSunSpec, vOtherSpec;
    vColor.rgb *= DrawLighting(vWorldPos, SafeNormalize(vNormal), false, vNoSunLight, vSunSpec, vOtherSpec);
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

float4 ShadePixel(SVertexOutput In)
{
  if (HasFlag2(DF2_SCOPE_LENS))
    return ScopeLens(In);
  uint nSampler0 = g_Draw.nSamplers & 15;
  uint nSampler1 = (g_Draw.nSamplers >> 4) & 15;

  // Bump mapping: the normal map in tangent space (vMap) and in world space (vBumpN).
  float3 vMap = float3(0, 0, 1);
  float3 vBumpN = 0;
  if (HasFlag(DF_BUMP))
  {
    vMap = SafeNormalize(SampleTexture(g_Draw.nTex3, XR_SAMPLER_LINEAR_WRAP, In.vTex0).rgb * 2 - 1);
    vMap.y *= g_Draw.vParams1.y;
    vBumpN = SafeNormalize(vMap.x * In.vTangent + vMap.y * In.vBinormal + vMap.z * In.vTNormal);
  }

  float4 vTex = float4(1, 1, 1, 1);
  if (HasFlag(DF_CUBE0))
  {
    float3 vView = SafeNormalize(In.vWorldPos - g_Frame.vCameraPos.xyz);
    float3 vN = HasFlag(DF_BUMP) ? vBumpN : SafeNormalize(In.vNormal);
    vTex = SampleCube(g_Draw.nTex0, nSampler0, reflect(vView, vN));
    if (HasFlag2(DF2_GLASS))
    {
      float4 vThrough = SampleCube(g_Draw.nTex0, nSampler0, refract(vView, vN, g_Draw.vParams2.w));
      float fFresnel = saturate(g_Draw.vParams2.z + g_Draw.vParams2.x * pow(1 - saturate(-dot(vView, vN)), g_Draw.vParams2.y));
      vTex = lerp(vThrough, vTex, fFresnel);
    }
    if (HasFlag2(DF2_REFL_AMOUNT))
      vTex.rgb = vTex.rgb * SampleTexture(g_Draw.nTex5, XR_SAMPLER_LINEAR_WRAP, In.vTex0).rgb +
        SampleTexture(g_Draw.nTex4, XR_SAMPLER_LINEAR_WRAP, In.vTex0).rgb * g_Draw.vAddColor.rgb;
  }
  else if (HasFlag(DF_TEX0_ARRAY))
  {
    float fSlice = floor(In.vTex0.x * 0.5);
    vTex = SampleTextureArray(g_Draw.nTex0, nSampler0, float3(In.vTex0.x - 2 * fSlice, In.vTex0.y, fSlice));
  }
  else if (HasFlag(DF_TEX0))
  {
    float2 vUV0 = In.vTex0;
    if (HasFlag2(DF2_BUMP_OFFSET))
      vUV0 += (SampleTexture(g_Draw.nTex3, XR_SAMPLER_LINEAR_WRAP, In.vTex1).rg * 2 - 1) * g_Draw.vParams0.x;
    vTex = SampleTexture(g_Draw.nTex0, nSampler0, vUV0);
  }
  if (HasFlag2(DF2_TERRAIN_LIGHT))
    vTex.rgb = 0.75 * vTex.rgb + 0.25;

  float fSun = HasFlag(DF_SHADOWS) ? SunVisibility(In.vWorldPos, In.vNormal, In.vPos.xy) : 1;
  float4 vVertexColor = In.vColor;
  float3 vSpecular = 0;
  if (HasFlag(DF_LIGHTING) && HasFlag(DF_PIXEL_LIGHTING))
  {
    float3 vNoSunLight, vSunSpec, vOtherSpec;
    float3 vN = HasFlag(DF_BUMP) ? vBumpN : SafeNormalize(In.vNormal);
    float3 vLight = DrawLighting(In.vWorldPos, vN, true, vNoSunLight, vSunSpec, vOtherSpec);
    vVertexColor.rgb *= lerp(vNoSunLight, vLight, fSun);
    if (HasFlag(DF_SPECULAR))
      vSpecular = 2 * g_Draw.vSpecular.rgb * (g_Draw.vSpecular.w > 0 ? vTex.a : 1) * (fSun * vSunSpec + vOtherSpec);
  }
  else if (HasFlag(DF_LIGHTING) && HasFlag2(DF2_MATERIAL_LIGHT))
  {
    float3 vDiffuse;
    MaterialLighting(In.vWorldPos, HasFlag(DF_BUMP) ? vBumpN : SafeNormalize(In.vNormal), fSun, vDiffuse, vSpecular);
    vVertexColor.rgb = vDiffuse;
  }
  else if (HasFlag(DF_SHADOWS) && HasFlag(DF_LIGHTING))
    vVertexColor.rgb = lerp(In.vColorNoSun.rgb, In.vColor.rgb, fSun);

  float4 vColor;
  if (HasFlag(DF_TEX0_ALPHA_ONLY))
    vColor = float4(vVertexColor.rgb, vVertexColor.a * vTex.a);
  else
    vColor = vVertexColor * vTex;
  vColor.rgb += vSpecular;
  if (HasFlag2(DF2_ADD_TEX4))
  {
    float4 vTex4 = SampleTexture(g_Draw.nTex4, XR_SAMPLER_LINEAR_WRAP, In.vTex0);
    vColor.rgb += vTex4.rgb * g_Draw.vAddColor.rgb + g_Draw.vAddColor.w;
    if (HasFlag2(DF2_GLASS))
      vColor.a *= vTex4.a;
  }
  if (HasFlag2(DF2_PROJECTOR))
    vColor *= SampleCube(g_Draw.nTex5, XR_SAMPLER_LINEAR_CLAMP, mul(float4(In.vWorldPos, 1), g_Draw.mParams0).xyz);
  if (HasFlag2(DF2_MODULATE))
    vColor = float4(saturate(vColor.rgb + g_Draw.vAddColor.w), 1);
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
  if (HasFlag2(DF2_ALPHA_GLOW))
    vColor.rgb += vTex.rgb * vTex.a;

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
  if (HasFlag2(DF2_GLOW_FALLOFF))
    vColor.rgb *= pow(saturate(1 - In.vTex0.x), g_Draw.vParams0.x);

  if (HasFlag(DF_ALPHATEST))
  {
    if (HasFlag(DF_ALPHATEST_LESS))
      clip(g_Draw.fAlphaRef - vColor.a - 1.0 / 512);
    else
      clip(vColor.a - g_Draw.fAlphaRef);
  }
  if (HasFlag(DF_ALPHA_ONE))
    vColor.a = 1;
  if (HasFlag2(DF2_FOG_VOLUME))
    return float4(g_Draw.vColor.rgb, FogVolumeAlpha(In.vWorldPos) * (g_Draw.vParams0.w > 0 ? vColor.a : 1));

  float3 vFogColor = HasFlag2(DF2_FOG_BLACK) ? 0 : HasFlag2(DF2_MODULATE) ? 1 : g_Frame.vFogColor.rgb;
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

  if (HasFlag2(DF2_HEAT))
  {
    float3 vN = HasFlag(DF_BUMP) ? vBumpN : SafeNormalize(In.vNormal);
    float fFacing = dot(vN, vN) > 0 ? abs(dot(vN, SafeNormalize(g_Frame.vCameraPos.xyz - In.vWorldPos))) : 1;
    vColor.rgb = HeatVision(vColor.rgb, fFacing);
  }

  if (HasFlag(DF_FOG) && g_Frame.vFogParams.w != 0)
  {
    vColor.rgb = lerp(vFogColor, vColor.rgb, SceneFog(In.vWorldPos));
  }

  if (HasFlag(DF_SHADOW_DEBUG))
    vColor = float4(fSun, fSun, fSun, 1);
  return vColor;
}

#ifdef XR_VELOCITY
struct SPixelOutput
{
  float4 vColor : SV_Target0;
  float2 vMotion : SV_Target1;
  float fDepth : SV_Target2;
};

SPixelOutput PSMain(SVertexOutput In)
{
  SPixelOutput Out;
  Out.vColor = ShadePixel(In);
  Out.vMotion = MotionVector(In.vCurClip, In.vPrevClip);
  Out.fDepth = In.vPos.z;
  return Out;
}
#else
float4 PSMain(SVertexOutput In) : SV_Target
{
  return ShadePixel(In);
}
#endif
