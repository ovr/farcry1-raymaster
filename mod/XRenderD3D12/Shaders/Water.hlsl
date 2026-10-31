// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
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
//	    (the copy of the depth buffer). The ocean with a fog volume lays the
//	    fog volume's colour over the scene behind as the fog volumes of the
//	    game do (OceanFog): by the depth of the point under the fog layer
//	    and the camera's height above it, times the point's distance along
//	    the view axis. Other water: the light of the scene behind went
//	    down to it through the water and comes up along the view ray, so it
//	    keeps exp(-extinction * (light path + view path)), the light path
//	    being its depth under the surface along the refracted sun; the water
//	    adds its own colour, the light it scatters towards the camera, by
//	    1 - exp(-extinction * view path) (extinction = absorption +
//	    turbidity);
//	  - reflection: the scene where the colour and depth copies show it
//	    (a ray marched in screen space), else the ocean's planar reflection
//	    (the scene seen by the camera mirrored in the water plane), else the
//	    sky box in the reflected direction (the box as the sky draws it;
//	    without a sky, none); the scene is looked up along the reflection of
//	    a calmer normal (half the slopes), so that it ripples rather than
//	    breaks up. Where the ripple gets finer than a pixel (the texture's
//	    mip levels smooth it out of the normal) it still turns the reflected
//	    rays: the planar reflection is blurred by the variance of the slopes
//	    lost (LEAN: the mean of the squared slope, which the texture keeps,
//	    less the square of the mean slope), up and down most (streaks
//	    towards the camera), from its mip levels; the screen space one,
//	    sharp, fades out where that blur gets wider than a few pixels; the
//	    lost slopes also turn facets towards the camera, which lowers the
//	    fresnel term at grazing angles. Weighted by a fresnel term (the
//	    ocean as the game's terrain water: a quarter looking straight down,
//	    times twice the vertex alpha, which the 3D engine lowers in shallow
//	    water near the camera; other water Schlick's: 2 %; all of it at
//	    grazing angles) and the reflection amount;
//	  - the sun's highlight on the waves (off as in the game; a console
//	    variable of our own);
//	  - near the shore (close to the camera) the surface fades into the
//	    scene behind it, so the water has no hard edge; seen from below,
//	    the ocean shows the scene above bent by large swells, in its fog
//	    volume's fog, other water the scene above through the window of
//	    total internal reflection;
//	  - the frame's linear fog (not on the ocean from below).
//	Compiled with XR_RAYTRACE (d3d12_RTReflections, Shader Model 6.5) the
//	reflection also comes from the scene for rays (RayShade.hlsli): a ray
//	along the calmer normal's reflected direction, as long as the rays'
//	length, shaded where it hits and fogged as the planar reflection is
//	(by the whole path from the camera), fading out over the last quarter
//	of the length. Mode 1 puts it in place of the screen space reflection,
//	mode 2 where the screen space one finds nothing (which there gives up
//	where its ray passes behind an object: the ray tracing sees what is
//	behind, and the march on would mostly find nothing); sharp as that one is,
//	it gives way to the blurred planar reflection where the ripple finer
//	than a pixel blurs it. The ray cone starts as wide as the pixel on the
//	surface and spreads by the pixel's angle and the ripple's RMS slope
//	(its textures' mip levels).
//	The result replaces the pixel (no blending). The upscaler's jitter
//	(vJitter) moves the clip position; compiled with XR_VELOCITY (in an
//	upscaler's scene) the pixel shader also writes the motion vector of the
//	surface (MotionVector, from mWorldViewProj and mPrevWorldViewProj) to
//	SV_Target1 and its depth to SV_Target2: the water does not write the depth
//	buffer, but it is what the upscaler sees there.
//
//	SDrawConstants:
//	  vParams0  x wave slope scale, y reflection amount, z d3d12_WaterDebug,
//	            w the strength of the sun's highlight (d3d12_WaterSunGlint)
//	  vParams1  x z1 of the sky box sides, y 1 when there is a sky box,
//	            z the sky box's alpha (blends it towards the fog colour),
//	            w 1 when there is a planar reflection
//	  vParams2  rgb extinction per metre (with a fog volume: x the z of its
//	            layer, y its density per metre of distance), w refraction
//	            displacement
//	  vParams3  rgb the colour of deep water; w 0 that of the ocean's fog
//	            volume (OceanFog), 1 lit by the scene behind it (water
//	            volumes, which may be indoors), 2 lit by the sky
//	  nTex0     colour copy, nTex1 depth copy (R24 of D24S8, R32 of an
//	            upscaler's scene)
//	  nTex2..4  the sky box textures: top, _12, _34
//	  nTex5     the wave slopes (red dH/du, green dH/dv; 128 flat, 32 the RMS;
//	            alpha their square, 16 at 1)
//	  nTex6     the planar reflection (with mip levels), mParams0 its camera's
//	            world to clip
//	  vTexGenU  x the factor of the variance of the slopes finer than a pixel
//	            (d3d12_WaterReflectionBlur; 0: the reflection is not blurred)
//	  vTexGenV  XR_RAYTRACE: x the mode (1, 2), y the rays' length (m), z the
//	            instance mask (bits 0-7) and bit 8 the sun's shadow at the
//	            hits
//
//////////////////////////////////////////////////////////////////////

#include "Common.hlsli"
#ifdef XR_RAYTRACE
#include "RayShade.hlsli"
#endif

struct SWaterOutput
{
  float4 vPos : SV_Position;
  float3 vWorldPos : TEXCOORD0;
  float4 vColor : COLOR0;
  float2 vTex0 : TEXCOORD1;
#ifdef XR_VELOCITY
  float4 vCurClip : TEXCOORD2;  //!< the clip position without the jitter
  float4 vPrevClip : TEXCOORD3; //!< the same in the frame before
#endif
};

SWaterOutput VSMain(SVertexInput In)
{
  SWaterOutput Out;
  float4 vPos = float4(In.vPos.xyz, 1);
  Out.vPos = Jitter(mul(vPos, g_Draw.mWorldViewProj));
#ifdef XR_VELOCITY
  Out.vCurClip = mul(vPos, g_Draw.mWorldViewProj);
  Out.vPrevClip = mul(vPos, g_Draw.mPrevWorldViewProj);
#endif
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
//! smooth the waves out where they get finer than a pixel; fLostVar: the variance of the slopes
//! (across and along together) they smoothed out of this pixel, the mean of the squared slope
//! (the texture's alpha) less the square of the mean slope, for each layer.
float3 WaveNormal(float2 vPos, float fTime, float fScale, out float fLostVar)
{
  static const float4 Layers[3] =
  {
    float4( 0.94,  0.34, 61.0, 1.10),
    float4( 0.50, -0.87, 17.3, 0.60),
    float4(-0.60,  0.80,  5.9, 0.35),
  };
  static const float Weights[3] = { 0.55, 0.40, 0.30 };
  float2 vSlope = 0;
  fLostVar = 0;
  [unroll]
  for (int i = 0; i < 3; i++)
  {
    float2 d = Layers[i].xy, e = float2(-d.y, d.x);
    float2 vUV = float2(dot(vPos, d) - fTime * Layers[i].w, dot(vPos, e)) / Layers[i].z;
    float3 t = g_Textures[g_Draw.nTex5].Sample(g_SamplerLinearWrap, vUV).rga;
    float2 s = (t.xy * 255 - 128) / 32;
    vSlope += (s.x * d + s.y * e) * Weights[i];
    fLostVar += max(t.z * 16 - dot(s, s), 0) * (Weights[i] * Weights[i]);
  }
  fLostVar *= fScale * fScale;
  return normalize(float3(-vSlope * fScale, 1));
}

float3 WaveNormal(float2 vPos, float fTime, float fScale)
{
  float fLostVar;
  return WaveNormal(vPos, fTime, fScale, fLostVar);
}

//! How much of its colour the ocean's fog volume lays over a point fDepth metres under its layer,
//! fDistance metres away along the view axis, seen from fHeight metres above the layer: the share
//! for the depth, which grows from 0 at the layer to all of it 5 m down, faster the nearer the
//! camera is to the layer (all of it under the layer), times the share for the distance: 1 % left
//! at twice the fog volume's distance (fDensity = ln 100 / (2 * distance)).
float OceanFog(float fDepth, float fHeight, float fDistance, float fDensity)
{
  if (fDepth <= 0)
    return 0;
  float fShare = 1;
  if (fHeight > 0)
  {
    float s = pow(saturate(fHeight / 5), 0.25);
    float k = 1 - 0.7 * s, p = 1 + 0.4 * s;
    fShare = saturate((1 - exp(-k * pow(fDepth, p))) / (1 - exp(-k * pow(5.0, p))));
  }
  return fShare * (1 - exp(-fDensity * fDistance));
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

//! The planar reflection as the ripple finer than a pixel blurs it: fVar the variance of its
//! slopes (across and along together), c the cosine of the angle of incidence. A slope across the
//! line of sight turns the reflected ray up or down by twice as much, one along it to the side by
//! twice as much times c: streaks up and down, the thinner the lower the view. Six taps up and
//! down the RMS angle (in the texture: where the turned direction vanishes, less where R does; the
//! gaussian's weights at 0.35, 1.05 and 1.75 of it), from the mip level as wide as the gaps between
//! them and the blur to the side. fBlur: the RMS blur up and down in the texture's coordinates.
float3 BlurredPlanarReflection(float3 P, float3 R, float fReach, float fVar, float c, out float fBlur)
{
  static const float Taps[3] = { 0.35, 1.05, 1.75 };
  static const float Weights[3] = { 0.941, 0.576, 0.216 };
  fBlur = 0;
  R.z = max(R.z, 0.01);
  float2 vUV = ClipToUV(mul(float4(P + R * fReach, 1), g_Draw.mParams0), 1);
  float fUp = 2 * sqrt(fVar * 0.5), fSide = fUp * c;
  float3 Th = cross(R, float3(0, 0, 1));
  float fLength = length(Th);
  Th = fLength > 1e-4 ? Th / fLength : float3(1, 0, 0);
  float3 Tv = cross(Th, R);
  float4 vClip = mul(float4(R, 0), g_Draw.mParams0);
  float4 vClipUp = mul(float4(R + Tv * fUp, 0), g_Draw.mParams0);
  float4 vClipSide = mul(float4(R + Th * fSide, 0), g_Draw.mParams0);
  if (min(vClip.w, min(vClipUp.w, vClipSide.w)) <= 1e-3)
    return g_Textures[g_Draw.nTex6].SampleLevel(g_SamplerLinearClamp, vUV, 0).rgb;
  float2 vVanish = ClipToUV(vClip, 1);
  float2 vUp = ClipToUV(vClipUp, 1) - vVanish, vSide = ClipToUV(vClipSide, 1) - vVanish;
  uint nW, nH, nLevels;
  g_Textures[g_Draw.nTex6].GetDimensions(0, nW, nH, nLevels);
  float2 vSize = float2(nW, nH);
  float fTexels = max(length(vUp * vSize) * (Taps[1] - Taps[0]), length(vSide * vSize) * 2);
  float fLod = min(log2(max(fTexels, 1)), nLevels - 1);
  float3 vSum = 0;
  [unroll]
  for (int k = 0; k < 3; k++)
  {
    vSum += (g_Textures[g_Draw.nTex6].SampleLevel(g_SamplerLinearClamp, vUV + vUp * Taps[k], fLod).rgb +
             g_Textures[g_Draw.nTex6].SampleLevel(g_SamplerLinearClamp, vUV - vUp * Taps[k], fLod).rgb) * Weights[k];
  }
  fBlur = length(vUp);
  return vSum / (2 * (Weights[0] + Weights[1] + Weights[2]));
}

//! The scene in direction R from P where the frame's copies show it: the ray marches in steps
//! that grow with the distance; when it gets behind the depth buffer the crossing is refined, and
//! it is a hit if the ray is then no more than a few centimetres behind the surface there (more
//! means it passed behind an object, whose back the copies do not show: it goes on, or with
//! bRaysAfter it ends there, a miss, for the rays to find what is behind). Never hits the sky (the
//! far plane) or the first person weapon (nearer than 0.5 m). w: how much to trust it, 0 on a
//! miss, fading towards the screen's edges. The steps go in fours, their depths read at once: the
//! same steps as one at a time, with fewer waits for the reads.
float4 ScreenReflection(float3 P, float3 R, float2 vViewToBuffer, bool bRaysAfter)
{
  float tPrev = 0, t = 0.4;
  [loop]
  for (int i = 0; i < 40; i += 4)
  {
    float4 vClip[4];
    float2 vUV[4];
    float fDepth[4], tStep[4];
    [unroll]
    for (int j = 0; j < 4; j++)
    {
      tStep[j] = t;
      vClip[j] = mul(float4(P + R * t, 1), g_Frame.mViewProj);
      vUV[j] = ClipToUV(vClip[j], vViewToBuffer);
      fDepth[j] = g_Textures[g_Draw.nTex1].SampleLevel(g_SamplerPointClamp, vUV[j], 0).r;
      t = t * 1.18 + 0.15;
    }
    [unroll]
    for (int j = 0; j < 4; j++)
    {
      if (vClip[j].w < 0.3)
        return 0;
      if (any(vUV[j] <= 0) || any(vUV[j] >= vViewToBuffer))
        return 0;
      float fScene = LinearDepth(fDepth[j]);
      if (vClip[j].w > fScene && fScene > 0.5 && fDepth[j] < 0.99999)
      {
        float a = tPrev, b = tStep[j], fBehind = vClip[j].w - fScene;
        [unroll]
        for (int k = 0; k < 6; k++)
        {
          float m = (a + b) * 0.5;
          float4 vClipM = mul(float4(P + R * m, 1), g_Frame.mViewProj);
          float fSceneM = LinearDepth(g_Textures[g_Draw.nTex1].SampleLevel(g_SamplerPointClamp, ClipToUV(vClipM, vViewToBuffer), 0).r);
          if (vClipM.w > fSceneM)
          {
            b = m;
            fBehind = vClipM.w - fSceneM;
          }
          else
            a = m;
        }
        if (fBehind < 0.05 + 0.01 * vClip[j].w)
        {
          float2 vHit = ClipToUV(mul(float4(P + R * b, 1), g_Frame.mViewProj), vViewToBuffer);
          float2 vEdge = min(vHit, vViewToBuffer - vHit) / vViewToBuffer;
          float fTrust = saturate(min(vEdge.x, vEdge.y) * 12);
          return float4(g_Textures[g_Draw.nTex0].SampleLevel(g_SamplerLinearClamp, vHit, 0).rgb, fTrust);
        }
        if (bRaysAfter)
          return 0;
      }
      tPrev = tStep[j];
    }
  }
  return 0;
}

#ifdef XR_RAYTRACE
//! The reflection by a ray from the surface point P (fDistance from the camera) in direction R:
//! the colour of the scene for rays where it hits, fogged by the path from the camera along the
//! view axis's slant, w how much it counts (0 on a miss, fading out over the last quarter of the
//! rays' length); fT the distance to the hit. The cone: fPixelAngle wide at the camera, spreading
//! by it and twice the RMS slope of the ripple finer than a pixel (fVar its variance).
float4 RayReflection(float3 P, float3 R, float fDistance, float fPixelAngle, float fVar, out float fT)
{
  RayDesc Ray;
  Ray.Origin = P + float3(0, 0, 0.02);
  Ray.Direction = normalize(float3(R.xy, max(R.z, 0.01)));
  Ray.TMin = 0.01;
  Ray.TMax = g_Draw.vTexGenV.y;
  const uint nMask = asuint(g_Draw.vTexGenV.z);
  const float fWidth = fDistance * fPixelAngle, fSpread = fPixelAngle + 2 * sqrt(fVar);
  const SRayHit Hit = TraceScene(Ray, nMask & 0xff, fWidth, fSpread);
  fT = Hit.fT;
  if (!Hit.bHit)
    return 0;
  bool bFog;
  float4 vColor = RayShade(Hit, Ray.Direction, fWidth + Hit.fT * fSpread, (nMask & 0x100) != 0, nMask & 0xff, bFog);
  if (bFog && g_Frame.vFogParams.w != 0)
  {
    const float fDepth = ViewDepth(P) * (fDistance + Hit.fT) / max(fDistance, 1e-3);
    vColor.rgb = lerp(g_Frame.vFogColor.rgb, vColor.rgb, saturate((g_Frame.vFogParams.y - fDepth) * g_Frame.vFogParams.z));
  }
  return float4(vColor.rgb, 1 - smoothstep(0.75 * Ray.TMax, Ray.TMax, Hit.fT));
}
#endif

float4 ShadeWater(SWaterOutput In)
{
  float3 vCamera = g_Frame.vCameraPos.xyz;
  float3 vToCamera = vCamera - In.vWorldPos;
  float fDistance = length(vToCamera);
  float3 V = vToCamera / max(fDistance, 1e-4);
  bool bBelow = vCamera.z < In.vWorldPos.z;
#ifdef XR_RAYTRACE
  // The angle between neighbouring pixels' view rays (before any branch).
  float fPixelAngle = max(length(ddx(V)), length(ddy(V)));
#endif

  float fLostVar;
  float3 N = WaveNormal(In.vWorldPos.xy, g_Frame.vTime.x, g_Draw.vParams0.x, fLostVar);
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
  // of the surface (the shore, an object) keeps the undisplaced one. The ocean from below bends the
  // world above far more, in large smooth swells: by the same waves four times as large.
  float2 vOffset;
  if (bBelow && g_Draw.vParams3.w == 0)
    vOffset = WaveNormal(In.vWorldPos.xy * 0.25, g_Frame.vTime.x * 0.25, g_Draw.vParams0.x).xy * 1.5 * saturate(fThick0 * 0.5) /
              (1 + fSurface * 0.1);
  else
    vOffset = N.xy * g_Draw.vParams2.w * saturate(fThick0 * 0.5) / (1 + fSurface * 0.05);
  float2 vUV = vUV0 + vOffset;
  float fBehind = LinearDepth(g_Textures[g_Draw.nTex1].SampleLevel(g_SamplerPointClamp, vUV, 0).r);
  if (fBehind < fSurface)
  {
    vUV = vUV0;
    fBehind = fBehind0;
  }
  float3 vScene = g_Textures[g_Draw.nTex0].SampleLevel(g_SamplerLinearClamp, vUV, 0).rgb;
  float3 vScene0 = g_Textures[g_Draw.nTex0].SampleLevel(g_SamplerPointClamp, vUV0, 0).rgb;

  // The path through the water along the view ray (the depths are along the view axis), and the
  // light's path down to the scene behind: its depth under the surface over the cosine of the
  // sun's ray refracted into the water (at least 0.66).
  float fPath = max(fBehind - fSurface, 0) * fDistance / max(fSurface, 1e-3);
  float3 vFloor = vCamera - V * (fDistance * fBehind0 / max(fSurface, 1e-3));
  float fDepthBelow = max(In.vWorldPos.z - vFloor.z, 0);
  float3 L = g_Frame.vSunDir.xyz;
  float fLightPath = fDepthBelow / sqrt(1 - saturate(1 - L.z * L.z) / (1.33 * 1.33));
  float3 vExtinction = g_Draw.vParams2.rgb;
  // The water's own colour is light scattered into it: as given (w 0), in proportion to the
  // brightness of what is behind a water volume (w 1), or light the sky scatters into it (w 2):
  // from 60 % under a black sky to all of it under a bright day sky (the sky box's brightness in
  // five directions).
  float3 vScatter = g_Draw.vParams3.rgb;
  if (g_Draw.vParams3.w == 1)
    vScatter *= saturate(dot(vScene0, float3(0.30, 0.59, 0.11)) * 2.5);
  else if (g_Draw.vParams3.w == 2 && g_Draw.vParams1.y != 0)
  {
    float3 vSky = SkyBoxColor(float3(0, 0, 1)) + SkyBoxColor(float3(1, 0, 0.3)) + SkyBoxColor(float3(-1, 0, 0.3)) +
                  SkyBoxColor(float3(0, 1, 0.3)) + SkyBoxColor(float3(0, -1, 0.3));
    vScatter *= 0.6 + 0.4 * saturate(dot(vSky, float3(0.30, 0.59, 0.11)) / (5 * 0.6));
  }
  float3 vColor;
  if (!bBelow)
  {
    float3 vRefracted;
    if (g_Draw.vParams3.w == 0)
    {
      // The fog volume over the scene behind (the point there along the displaced ray).
      float3 vBehind = vCamera - V * (fDistance * fBehind / max(fSurface, 1e-3));
      float fLayer = g_Draw.vParams2.x;
      vRefracted = lerp(vScene, vScatter, OceanFog(fLayer - vBehind.z, vCamera.z - fLayer, fBehind, g_Draw.vParams2.y));
    }
    else
    {
      float3 T = exp(-vExtinction * fPath);
      vRefracted = vScene * T * exp(-vExtinction * fLightPath) + vScatter * (1 - T);
    }

    // The reflection, all of it at grazing angles: the ocean as the game's terrain water shaders
    // (a quarter straight down, by the calmer normal; times twice the alpha of the mesh the 3D
    // engine gives, which fades it out in shallow water near the camera), other water by
    // Schlick's term (2 %). The ripple finer than a pixel (its slopes' variance fVar, as
    // d3d12_WaterReflectionBlur scales it) turns facets towards the camera: the cosine as the
    // RMS of theirs.
    float fVar = fLostVar * g_Draw.vTexGenU.x;
    float3 Ns = normalize(float3(N.xy * 0.5, N.z));
    float F;
    if (g_Draw.vParams3.w != 1)
    {
      float c = saturate(dot(Ns, V));
      if (fVar > 0)
        c = sqrt(c * c + fVar);
      F = (0.9932 + c * (-0.3742 + c * (-1.1438 + c * 0.7584))) * saturate(In.vColor.a * 2);
    }
    else
    {
      float c = saturate(dot(N, V));
      if (fVar > 0)
        c = sqrt(c * c + fVar);
      F = 0.02 + 0.98 * pow(1 - c, 5);
    }
    F *= g_Draw.vParams0.y;
    float3 R = reflect(-V, N);
    float3 Rs = reflect(-V, Ns);
    float3 vReflected = g_Draw.vParams1.y != 0 ? SkyBoxColor(R) * SkyGlow(R) : vScatter;
    float3 vPlanar = 0;
    float fBlur = 0;
    if (g_Draw.vParams1.w != 0)
    {
      if (fVar > 0)
        vPlanar = BlurredPlanarReflection(In.vWorldPos, Rs, fDistance * 0.3, fVar, saturate(dot(Ns, V)), fBlur);
      else
        vPlanar = PlanarReflection(In.vWorldPos, Rs, fDistance * 0.3);
      vReflected = vPlanar;
    }
    // The screen's copy is sharp: it gives way to the blurred planar reflection where the blur up
    // and down is wider than 4 pixels on the screen, all of it from 8.
#ifdef XR_RAYTRACE
    float4 vSSR = g_Draw.vTexGenV.x >= 2 ? ScreenReflection(In.vWorldPos, Rs, g_Frame.vScreen.xy * vInvSize, true) : 0;
#else
    float4 vSSR = ScreenReflection(In.vWorldPos, Rs, g_Frame.vScreen.xy * vInvSize, false);
#endif
    if (fBlur > 0)
      vSSR.w *= saturate(2 - fBlur * g_Frame.vScreen.y / 4);
#ifdef XR_RAYTRACE
    // The rays: in place of the screen's reflection (mode 1), or where it finds nothing (2, none where
    // it is sure); as sharp, they give way to the blurred planar reflection as it does.
    // No ray where nothing of it would be seen (no reflection, or blurred away), but in the debug views.
    float fRayT = g_Draw.vTexGenV.y;
    float4 vRay = 0;
    const float fRayFade = fBlur > 0 ? saturate(2 - fBlur * g_Frame.vScreen.y / 4) : 1;
    if ((g_Draw.vTexGenV.x < 2 || vSSR.w < 1) && ((F > 0 && fRayFade > 0) || g_Draw.vParams0.z >= 13))
      vRay = RayReflection(In.vWorldPos, Rs, fDistance, fPixelAngle, fVar, fRayT);
    vRay.w *= fRayFade;
    vReflected = lerp(vReflected, vRay.rgb, vRay.w);
#endif
    vReflected = lerp(vReflected, vSSR.rgb, vSSR.w);

    // The sun's highlight (d3d12_WaterSunGlint), as much of it as the facets reflect (fresnel at the
    // half vector): faint looking down on the water, glaring towards a low sun.
    float3 H = normalize(L + V);
    float fSunF = 0.02 + 0.98 * pow(1 - saturate(dot(V, H)), 5);
    float fSpec = (pow(saturate(dot(N, H)), 700) * 40 + pow(saturate(dot(N, H)), 90) * 1.5) * fSunF;
    float3 vSun = g_Frame.vSunColor.rgb * fSpec * saturate(L.z * 4) * g_Draw.vParams0.w;

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
      return float4(vPlanar, 1);
    if (g_Draw.vParams0.z == 8)
      return float4(vScene0, 1);
    if (g_Draw.vParams0.z == 9)
      return float4(saturate(fPath / 32), saturate(fDepthBelow / 16), F, 1);
    if (g_Draw.vParams0.z == 10)
      return float4(In.vColor.rgb, 1);
    if (g_Draw.vParams0.z == 11)
      return float4(In.vColor.aaa, 1);
    if (g_Draw.vParams0.z == 12)
      return float4(saturate(sqrt(fLostVar) * 10).xxx, 1);
#ifdef XR_RAYTRACE
    if (g_Draw.vParams0.z == 13)
      return float4(vRay.rgb, 1);
    if (g_Draw.vParams0.z == 14)
      return float4(fRayT < g_Draw.vTexGenV.y ? saturate(float3(2 * fRayT / g_Draw.vTexGenV.y - 1, 1 - abs(2 * fRayT / g_Draw.vTexGenV.y - 1),
                                                               1 - 2 * fRayT / g_Draw.vTexGenV.y)) : 0, 1);
    if (g_Draw.vParams0.z == 15)
      return float4(vRay.www, 1);
#endif
  }
  else if (g_Draw.vParams3.w == 0)
  {
    // The ocean from below: the scene above through the waves (as the game's ocean shows it,
    // without a window of total internal reflection), a third of it the water's colour the surface
    // reflects back down, in the fog of its volume.
    vColor = lerp(vScene, vScatter, 1 - 0.65 * (1 - FogInsideVolume(fSurface, g_Draw.vParams2.y)));
    if (g_Draw.vParams0.z == 8)
      return float4(vScene, 1);
  }
  else
  {
    // Other water from below: the scene above inside the window (critical angle of water, cos
    // 0.66), the water's colour outside it.
    float fWindow = saturate((dot(N, V) - 0.66) * 8);
    vColor = lerp(vScatter, vScene, fWindow);
  }

  if (HasFlag2(DF2_HEAT))
    vColor = HeatVision(vColor, 1);
  // The scene's fog, not on the ocean seen from below: that view stays in the fog of its volume.
  if (g_Frame.vFogParams.w != 0 && !(bBelow && g_Draw.vParams3.w == 0))
    vColor = lerp(g_Frame.vFogColor.rgb, vColor, SceneFog(In.vWorldPos));
  return float4(vColor, 1);
}

#ifdef XR_VELOCITY
struct SPixelOutput
{
  float4 vColor : SV_Target0;
  float2 vMotion : SV_Target1;
  float fDepth : SV_Target2;
};

SPixelOutput PSMain(SWaterOutput In)
{
  SPixelOutput Out;
  Out.vColor = SceneOutput(ShadeWater(In));
  Out.vMotion = MotionVector(In.vCurClip, In.vPrevClip);
  Out.fDepth = In.vPos.z;
  return Out;
}
#else
float4 PSMain(SWaterOutput In) : SV_Target
{
  return SceneOutput(ShadeWater(In));
}
#endif
