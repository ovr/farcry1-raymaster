//////////////////////////////////////////////////////////////////////
//
//	File: Water.hlsl
//	Description: The water program: water surfaces (the ocean ring around
//	the camera, water volumes) over the finished opaque scene.
//
//	The surface is lit per pixel from what the frame already holds:
//	  - waves: the normal from three scrolling layers of a texture of wave
//	    slopes (made by the renderer, Water.cpp);
//	  - refraction: the copy of the colour buffer, displaced by the normal,
//	    seen through the water between the surface and the scene behind it
//	    (the copy of the depth buffer): transmittance exp(-absorption *
//	    path), the rest the water's own scattered colour;
//	  - reflection: the scene where the colour and depth copies show it
//	    (a ray marched in screen space), else the ocean's planar reflection
//	    (the scene seen by the camera mirrored in the water plane), else the
//	    sky box in the reflected direction (the box as the sky draws it;
//	    without a sky, none), weighted by 1 - 0.8 N.V (much more than the
//	    fresnel of real water, as the original's water) and the reflection
//	    amount;
//	  - the sun's highlight on the waves;
//	  - near the shore (close to the camera) the surface fades into the
//	    scene behind it, so the water has no hard edge; seen from below,
//	    the scene above shows through the window of total internal
//	    reflection;
//	  - the frame's linear fog.
//	The result replaces the pixel (no blending).
//
//	SDrawConstants:
//	  vParams0  x wave slope scale, y reflection amount, z d3d12_WaterDebug
//	  vParams1  x z1 of the sky box sides, y 1 when there is a sky box,
//	            z the sky box's alpha (blends it towards the fog colour),
//	            w 1 when there is a planar reflection
//	  vParams2  rgb absorption per metre, w refraction displacement
//	  vParams3  rgb the water's scattered colour (lit); w 1: lit by the scene
//	            behind it instead (water volumes, which may be indoors)
//	  nTex0     colour copy, nTex1 depth copy (R24 of D24S8)
//	  nTex2..4  the sky box textures: top, _12, _34
//	  nTex5     the wave slopes (red dH/du, green dH/dv; 128 flat, 32 the RMS)
//	  nTex6     the planar reflection, mParams0 its camera's world to clip
//
//////////////////////////////////////////////////////////////////////

#include "Common.hlsli"

struct SWaterOutput
{
  float4 vPos : SV_Position;
  float3 vWorldPos : TEXCOORD0;
  float4 vColor : COLOR0;
  float2 vTex0 : TEXCOORD1;
};

SWaterOutput VSMain(SVertexInput In)
{
  SWaterOutput Out;
  float4 vPos = float4(In.vPos.xyz, 1);
  Out.vPos = mul(vPos, g_Draw.mWorldViewProj);
  Out.vWorldPos = mul(vPos, g_Draw.mWorld).xyz;
  Out.vColor = DecodeVertexColor(In.vColor0);
  Out.vTex0 = In.vTex0;
  return Out;
}

static const float PI = 3.14159265;

//! Distance along the view axis of a depth buffer value.
float LinearDepth(float fDepth)
{
  return g_Frame.vDepthParams.y / (fDepth + g_Frame.vDepthParams.x);
}

//! The surface normal at a horizontal position: three layers of the wave texture (nTex5: the
//! slopes of a tiling height field, RMS 1 after decoding), each with its own direction (the
//! texture's u), tile size in metres and speed in metres per second. The texture's mip levels
//! smooth the waves out where they get finer than a pixel.
float3 WaveNormal(float2 vPos, float fTime, float fScale)
{
  static const float4 Layers[3] =
  {
    float4( 0.94,  0.34, 61.0, 1.10),
    float4( 0.50, -0.87, 17.3, 0.60),
    float4(-0.60,  0.80,  5.9, 0.35),
  };
  static const float Weights[3] = { 0.55, 0.40, 0.30 };
  float2 vSlope = 0;
  [unroll]
  for (int i = 0; i < 3; i++)
  {
    float2 d = Layers[i].xy, e = float2(-d.y, d.x);
    float2 vUV = float2(dot(vPos, d) - fTime * Layers[i].w, dot(vPos, e)) / Layers[i].z;
    float2 s = (g_Textures[g_Draw.nTex5].Sample(g_SamplerLinearWrap, vUV).rg * 255 - 128) / 32;
    vSlope += (s.x * d + s.y * e) * Weights[i];
  }
  return normalize(float3(-vSlope * fScale, 1));
}

//! The sky box (as the sky draws it) in direction vDir, which points up or level.
float3 SkyBoxColor(float3 vDir)
{
  float3 vFog = g_Frame.vFogColor.rgb;
  if (g_Draw.vParams1.y == 0)
    return vFog;
  vDir.z = max(vDir.z, 0.0);
  float fSide = max(abs(vDir.x), abs(vDir.y));
  float4 vTex;
  if (vDir.z >= fSide)
  {
    // The top, z = 32: u = (32 - y) / 64, v = (x + 32) / 64.
    float2 p = vDir.xy / vDir.z * 32;
    vTex = g_Textures[g_Draw.nTex2].SampleLevel(g_SamplerLinearClamp, float2((32 - p.y) / 64, (p.x + 32) / 64), 0);
  }
  else
  {
    float3 p = vDir * (32 / fSide);
    float fZ1 = g_Draw.vParams1.x;
    float f = saturate((32 - p.z) / (32 - fZ1));
    float u, vTop;
    uint nTex;
    if (abs(vDir.x) >= abs(vDir.y))
    {
      // x = 32: the upper half of _12, u 1 at y = -32; x = -32: the upper half of _34, u 1 at y = 32.
      u = vDir.x > 0 ? (32 - p.y) / 64 : (32 + p.y) / 64;
      vTop = 0;
      nTex = vDir.x > 0 ? g_Draw.nTex3 : g_Draw.nTex4;
    }
    else
    {
      // y = -32: the lower half of _12, u 1 at x = -32; y = 32: the lower half of _34, u 1 at x = 32.
      u = vDir.y < 0 ? (32 - p.x) / 64 : (32 + p.x) / 64;
      vTop = 1;
      nTex = vDir.y < 0 ? g_Draw.nTex3 : g_Draw.nTex4;
    }
    // nTex differs between neighbouring pixels at the corners of the box.
    vTex = g_Textures[NonUniformResourceIndex(nTex)].SampleLevel(g_SamplerLinearClamp, float2(u, lerp(vTop, 0.5, f)), 0);
  }
  return lerp(vFog, vTex.rgb, g_Draw.vParams1.z);
}

//! Buffer texture coordinates of a clip space position; vViewToBuffer: the viewport's size over
//! the buffer's (the viewport is at the top left).
float2 ClipToUV(float4 vClip, float2 vViewToBuffer)
{
  return float2(vClip.x / vClip.w * 0.5 + 0.5, 0.5 - vClip.y / vClip.w * 0.5) * vViewToBuffer;
}

//! The planar reflection in direction R from the surface point P: the point fReach along R as the
//! mirrored camera sees it (for a flat surface P itself, whatever fReach is: the camera, P and the
//! reflected ray are on one line).
float3 PlanarReflection(float3 P, float3 R, float fReach)
{
  R.z = max(R.z, 0.01);
  float4 vClip = mul(float4(P + R * fReach, 1), g_Draw.mParams0);
  return g_Textures[g_Draw.nTex6].SampleLevel(g_SamplerLinearClamp, ClipToUV(vClip, 1), 0).rgb;
}

//! The scene in direction R from P where the frame's copies show it: the ray marches in steps
//! that grow with the distance until it is behind the depth buffer but not far behind (not
//! passing behind an object), then the crossing is refined. Never hits the sky (the far plane)
//! or the first person weapon (nearer than 0.5 m). w: how much to trust it, 0 on a miss, fading
//! towards the screen's edges.
float4 ScreenReflection(float3 P, float3 R, float2 vViewToBuffer)
{
  float tPrev = 0, t = 0.4;
  [loop]
  for (int i = 0; i < 40; i++)
  {
    float4 vClip = mul(float4(P + R * t, 1), g_Frame.mViewProj);
    if (vClip.w < 0.3)
      break;
    float2 vUV = ClipToUV(vClip, vViewToBuffer);
    if (any(vUV <= 0) || any(vUV >= vViewToBuffer))
      break;
    float fDepth = g_Textures[g_Draw.nTex1].SampleLevel(g_SamplerPointClamp, vUV, 0).r;
    float fScene = LinearDepth(fDepth);
    if (vClip.w > fScene && fScene > 0.5 && fDepth < 0.99999 && vClip.w - fScene < max(0.3, (t - tPrev) * 2))
    {
      float a = tPrev, b = t;
      [unroll]
      for (int k = 0; k < 5; k++)
      {
        float m = (a + b) * 0.5;
        float4 vClipM = mul(float4(P + R * m, 1), g_Frame.mViewProj);
        float fSceneM = LinearDepth(g_Textures[g_Draw.nTex1].SampleLevel(g_SamplerPointClamp, ClipToUV(vClipM, vViewToBuffer), 0).r);
        if (vClipM.w > fSceneM)
          b = m;
        else
          a = m;
      }
      float2 vHit = ClipToUV(mul(float4(P + R * b, 1), g_Frame.mViewProj), vViewToBuffer);
      float2 vEdge = min(vHit, vViewToBuffer - vHit) / vViewToBuffer;
      float fTrust = saturate(min(vEdge.x, vEdge.y) * 12);
      return float4(g_Textures[g_Draw.nTex0].SampleLevel(g_SamplerLinearClamp, vHit, 0).rgb, fTrust);
    }
    tPrev = t;
    t = t * 1.18 + 0.15;
  }
  return 0;
}

float4 PSMain(SWaterOutput In) : SV_Target
{
  float3 vCamera = g_Frame.vCameraPos.xyz;
  float3 vToCamera = vCamera - In.vWorldPos;
  float fDistance = length(vToCamera);
  float3 V = vToCamera / max(fDistance, 1e-4);
  bool bBelow = vCamera.z < In.vWorldPos.z;

  float3 N = WaveNormal(In.vWorldPos.xy, g_Frame.vTime.x, g_Draw.vParams0.x);
  if (bBelow)
    N = -N;

  // What the opaque scene left behind the surface: the undisplaced pixel first.
  uint nW, nH;
  g_Textures[g_Draw.nTex0].GetDimensions(nW, nH);
  float2 vInvSize = 1 / float2(nW, nH);
  float2 vUV0 = In.vPos.xy * vInvSize;
  float fSurface = LinearDepth(In.vPos.z);
  float fBehind0 = LinearDepth(g_Textures[g_Draw.nTex1].SampleLevel(g_SamplerPointClamp, vUV0, 0).r);
  float fThick0 = max(fBehind0 - fSurface, 0);

  // Displaced by the waves, less in shallow water and with distance; a displaced sample in front
  // of the surface (the shore, an object) keeps the undisplaced one.
  float2 vOffset = N.xy * g_Draw.vParams2.w * saturate(fThick0 * 0.5) / (1 + fSurface * 0.05);
  float2 vUV = vUV0 + vOffset;
  float fBehind = LinearDepth(g_Textures[g_Draw.nTex1].SampleLevel(g_SamplerPointClamp, vUV, 0).r);
  if (fBehind < fSurface)
  {
    vUV = vUV0;
    fBehind = fBehind0;
  }
  float3 vScene = g_Textures[g_Draw.nTex0].SampleLevel(g_SamplerLinearClamp, vUV, 0).rgb;
  float3 vScene0 = g_Textures[g_Draw.nTex0].SampleLevel(g_SamplerPointClamp, vUV0, 0).rgb;

  // The path through the water along the view ray (the depths are along the view axis).
  float fPath = max(fBehind - fSurface, 0) * fDistance / max(fSurface, 1e-3);
  // The water's own colour is light the sky (or, in a water volume, its surroundings) scatters
  // into it: from 60 % under a black sky to all of it under a bright day sky (the sky box's
  // brightness in five directions), or in proportion to the brightness of what is behind a water
  // volume.
  float3 vScatter = g_Draw.vParams3.rgb;
  if (g_Draw.vParams3.w > 0)
    vScatter *= saturate(dot(vScene0, float3(0.30, 0.59, 0.11)) * 2.5);
  else if (g_Draw.vParams1.y != 0)
  {
    float3 vSky = SkyBoxColor(float3(0, 0, 1)) + SkyBoxColor(float3(1, 0, 0.3)) + SkyBoxColor(float3(-1, 0, 0.3)) +
                  SkyBoxColor(float3(0, 1, 0.3)) + SkyBoxColor(float3(0, -1, 0.3));
    vScatter *= 0.6 + 0.4 * saturate(dot(vSky, float3(0.30, 0.59, 0.11)) / (5 * 0.6));
  }
  float3 vColor;
  if (!bBelow)
  {
    float3 T = exp(-g_Draw.vParams2.rgb * fPath);
    float3 vRefracted = vScene * T + vScatter * (1 - T);

    // The reflection: a fifth of the sky looking straight down, all of it at grazing angles.
    float NdotV = saturate(dot(N, V));
    float F = (1 - 0.8 * NdotV) * g_Draw.vParams0.y;
    float3 R = reflect(-V, N);
    float3 vReflected = g_Draw.vParams1.y != 0 ? SkyBoxColor(R) : vScatter;
    if (g_Draw.vParams1.w != 0)
      vReflected = PlanarReflection(In.vWorldPos, R, fDistance * 0.3);
    float4 vSSR = ScreenReflection(In.vWorldPos, R, g_Frame.vScreen.xy * vInvSize);
    vReflected = lerp(vReflected, vSSR.rgb, vSSR.w);

    float3 L = g_Frame.vSunDir.xyz;
    float3 H = normalize(L + V);
    float fSpec = pow(saturate(dot(N, H)), 700) * 4 + pow(saturate(dot(N, H)), 90) * 0.15;
    float3 vSun = g_Frame.vSunColor.rgb * fSpec * saturate(L.z * 4);

    // The shore: the surface fades in over the first 15 cm of water, up to 40 m away; beyond 100 m
    // it covers everything (the coarse distant terrain comes close to the surface in places).
    float fShore = saturate(fThick0 * 6.6 + saturate((fSurface - 40) / 60));
    vColor = lerp(vRefracted, vReflected, F) + vSun * fShore;
    vColor = lerp(vScene0, vColor, fShore);
    if (g_Draw.vParams0.z == 1)
      return float4(vRefracted, 1);
    if (g_Draw.vParams0.z == 2)
      return float4(vReflected, 1);
    if (g_Draw.vParams0.z == 3)
      return float4(saturate(fThick0 / 2).xxx, 1);
    if (g_Draw.vParams0.z == 4)
      return float4(N * 0.5 + 0.5, 1);
    if (g_Draw.vParams0.z == 5)
      return float4(vSSR.www, 1);
    if (g_Draw.vParams0.z == 6)
      return float4(vSSR.rgb, 1);
    if (g_Draw.vParams0.z == 7)
      return float4(g_Draw.vParams1.w != 0 ? PlanarReflection(In.vWorldPos, R, fDistance * 0.3) : 0, 1);
  }
  else
  {
    // From below: the scene above inside the window (critical angle of water, cos 0.66), the
    // water's colour outside it.
    float fWindow = saturate((dot(N, V) - 0.66) * 8);
    vColor = lerp(vScatter, vScene, fWindow);
  }

  if (g_Frame.vFogParams.w != 0)
  {
    float fFog = saturate((g_Frame.vFogParams.y - fDistance) * g_Frame.vFogParams.z);
    vColor = lerp(g_Frame.vFogColor.rgb, vColor, fFog);
  }
  return float4(vColor, 1);
}
