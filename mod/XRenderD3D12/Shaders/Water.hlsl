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
//	    (the copy of the depth buffer). The ocean with a fog volume
//	    (d3d12_WaterVolume, OceanVolume; WaterVolume.hlsli): the water
//	    attenuates each channel of the light (from its fog volume's colour
//	    and density: a channel as bright as the colour's luminance by the
//	    density, one half as bright twice as fast, at least pure water's
//	    absorption) along the view's ray refracted
//	    down to the scene behind and along the light's way down to its
//	    depth, and scatters the light coming down (fading with the depth)
//	    towards the camera, deep water seen straight down its fog volume's
//	    colour; the sun's share of that light in the sun's shadow where the
//	    sun's ray to each segment of the view's ray left the surface (the
//	    cascades); with d3d12_WaterVolume 0 the fog volume's colour laid
//	    over the scene behind as the fog volumes of the game do (OceanFog):
//	    by the depth of the point under the fog layer and the camera's
//	    height above it, times the point's distance along the view axis.
//	    Other water: the light of the scene behind went
//	    down to it through the water and comes up along the view ray, so it
//	    keeps exp(-extinction * (light path + view path)), the light path
//	    being its depth under the surface along the refracted sun; the water
//	    adds its own colour, the light it scatters towards the camera, by
//	    1 - exp(-extinction * view path) (extinction = absorption +
//	    turbidity);
//	  - reflection: the scene where the colour and depth copies show it
//	    (a ray marched in screen space), else the ocean's planar reflection
//	    (the scene seen by the camera mirrored in the water plane), else the
//	    sky box in the reflected direction (the box as the sky draws it for
//	    the mirrored camera, with the fog band over its lower part; without
//	    a sky, none); the scene is looked up along the reflection of
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
//	  - the sun's glint (WaterLight.hlsli; the game has none, d3d12_WaterGlint
//	    0 leaves it out): the facets of the waves that mirror the sun, their
//	    slopes about the pixel's normal a gaussian of the variance of the
//	    ripple finer than a pixel (the reflection's LEAN, at full strength)
//	    and of the ripple finer than the texture: sparks near the camera, the
//	    sun's path far away; light added to the colour's, in the HDR targets
//	    up to the sun's flare (d3d12_HDRSun), none in the sun's shadow (the
//	    cascades, or a ray with d3d12_RTSunShadows);
//	  - the sun's light through the thin crests of the waves (WaterLight.hlsli;
//	    d3d12_WaterCrestLight 0 leaves it out): turquoise, half in the deep
//	    water's hue, on the upper part of the ripple (the wave texture's
//	    height), more on its faces turned away from the sun, seen against it
//	    at a slant; light added as the glint's, in the sun's shadow none;
//	  - the FFT ocean's foam (WaterFoam.hlsli; d3d12_WaterFoam 0 leaves it
//	    out): the whitecaps where its crests break (the foam kernel's share
//	    white, fading) and the waves breaking on the shore, the rocks and what
//	    stands in the water (where the water under the surface is shallower
//	    than their height over the breaker index), in a lace of bubbles that
//	    sways with the waves; white under the sun and the sky over the
//	    colour, the glint and the crests' light gone under it;
//	  - the wakes (WaterWake; d3d12_WaterWakes 0 leaves them out): the
//	    waves of the bodies moving through the ocean in a window around the
//	    camera (Wake.cpp), their slopes added to the waves' and their foam
//	    to the foam's, fading out at the window's edge;
//	  - the waves' geometry (d3d12_WaterGeometry; OceanGrid.cpp,
//	    OceanGrid.hlsli): in place of the game's flat disc of the ocean a
//	    polar grid of the same extent around the camera, whose vertices
//	    this program raises to the waves' height (OceanGridVertex: the FFT
//	    ocean's cascades, each at the mip level of the grid's spacing there,
//	    none under the bodies in the water, and the wakes'), limited by the
//	    depth under the level, flat around a camera at the water, no crest
//	    up to a camera over it, none far off: the crests stand against the
//	    sky and the shore, the troughs hide what is behind them. Its vertex
//	    alpha the disc's, read from its vertices (GridDiscAlpha); from below
//	    by the level as the game takes it, the foam's depth under the level
//	    too (none on what a crest hides). It writes the depth buffer (its
//	    rings from the camera out: what a nearer crest hides fails the
//	    test). With d3d12_WaterDeferred (XR_WATER_DEFERRED, PSDeferred)
//	    the grid is drawn into the depth alone and this program shades the
//	    view where the waves are, each pixel once (the world position from
//	    the depth, the disc's alpha from a table by radius,
//	    GridDiscAlphaTable);
//	  - near the shore (close to the camera) the surface fades into the
//	    scene behind it, so the water has no hard edge; seen from below,
//	    the ocean shows the scene above bent by large swells, in its fog
//	    volume's fog, through Snell's window (d3d12_WaterVolume: Fresnel's
//	    transmission from water to air, none beyond the critical angle,
//	    where the surface mirrors the water's colour), other water the
//	    scene above through the window of total internal reflection;
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
//	buffer (but for the ocean's grid), but it is what the upscaler sees there.
//
//	SDrawConstants:
//	  vParams0  x wave slope scale, y reflection amount, z d3d12_WaterDebug,
//	            w the strength of the light through the crests
//	            (d3d12_WaterCrestLight; 0: none)
//	  vParams1  x z1 of the sky box sides, y 1 when there is a sky box (2:
//	            with the fog band over its lower part, SkyFogBand.hlsli),
//	            z the sky box's alpha (blends it towards the fog colour),
//	            w 1 when there is a planar reflection
//	  vParams2  rgb extinction per metre (with a fog volume: x the z of its
//	            layer, y its density per metre of distance), w refraction
//	            displacement
//	  vParams3  rgb the colour of deep water; w 0 that of the ocean's fog
//	            volume (OceanFog), 1 lit by the scene behind it (water
//	            volumes, which may be indoors), 2 lit by the sky
//	  vColor    rgb a water volume's scene behind times it (a WaterVolume
//	            material's diffuse colour, d3d12_WaterVolumeColor; white:
//	            as it is)
//	  nTex0     colour copy, nTex1 depth copy (R24 of D24S8, R32 of an
//	            upscaler's scene)
//	  nTex2..4  the sky box textures: top, _12, _34
//	  nTex5     the wave slopes (red dH/du, green dH/dv; 128 flat, 32 the RMS;
//	            alpha their square, 16 at 1), blue the height (128: 0, 32 the
//	            RMS)
//	  nTex6     the planar reflection (with mip levels), mParams0 its camera's
//	            world to clip
//	  vTexGenU  x the factor of the variance of the slopes finer than a pixel
//	            (d3d12_WaterReflectionBlur; 0: the reflection is not blurred),
//	            yz the first row (c, s) of the sky box's turn about z (the
//	            level's sky box angle, DrawSkyItem: a direction of the world is
//	            turned back by it before the box is looked up), w 1: the FFT
//	            ocean's waves (d3d12_WaterFFT) in place of nTex5's, vParams0.x
//	            the factor of their slopes
//	  vTexGenV  XR_RAYTRACE: x the mode (1, 2), y the rays' length (m), z the
//	            instance mask (bits 0-7) and bit 8 the sun's shadow at the
//	            hits
//	  nShadowTex  x the depth tiles of the depth copy (Water.cpp, the ocean's;
//	            R32_FLOAT: at level 0 the least depth of each XR_DEPTH_TILE x
//	            XR_DEPTH_TILE pixels, at each next one of 2 x 2 texels of the
//	            one before), y their levels (0: none), z with vTexGenU.w the
//	            FFT ocean's waves (OceanFFT.cpp: a texture array, a slice a
//	            cascade, OceanFFT.hlsli's texels), w with vTexGen1U.x its
//	            foam (the same: OceanFFT.hlsli's OceanFoam)
//	  vTexGen1U the foam: x d3d12_WaterFoam (0: none), y one over the RMS of
//	            the long waves' height, z the sea's significant height (m,
//	            scaled as the slopes), w (as uint) the pattern (g_Textures,
//	            FoamPattern.h)
//	  vTexGen1V the foam: xy the speed of the waves at the spectrum's peak
//	            along the wind (m/s), z the swell's height at the shore (m,
//	            d3d12_WaterSurf; 0 no surf), w its periods gone (the time over
//	            XR_FOAM_SURF_PERIOD)
//	  vTexMat0  the foam: xy, zw the pattern's two samples' drift with the
//	            wind (m)
//	  vTexMat1  the foam: xy the breaker index's noise's drift (m)
//	  vLightPos the wakes (WaterWake): [1].w the share shown (0: none), [0] xy the
//	            window's centre point (m), zw its texel there over the
//	            window's size, [1] x 1 / the sponge's width (m), y 1 their
//	            foam, z (as uint) the view's SRV (Shaders/Wake.hlsl's view);
//	            XR_WATER_DEFERRED: [2..4] xyz the view's rays (DeferredPos),
//	            [2].w (as uint) the depth buffer, [3].w the depth of the
//	            farthest water (VSDeferred)
//	  vLightSpec the ocean's grid (OceanGrid.cpp; [0].x 0: the game's disc):
//	            [0] x 1, y the level, z the disc's radius, w the bodies; [1]
//	            xy the grid's centre (the camera's), z (as uint) the
//	            terrain's heights (R32F), w the camera's height over the
//	            level; [2] xyz each cascade's height (m) for its texels'
//	            height channel, w the rows a doubling of the disc's alpha
//	            table (OceanGrid::AlphaScale); [3] x 1 with the wakes'
//	            heights, y (as uint)
//	            their SRV (R16F, Wake.hlsl's heights), zw the terrain's uv of
//	            the world (z 0: no terrain); [4] x, y (as uint) the raw buffer
//	            and the byte offset of the disc's table (OceanGrid::
//	            DiscTable), z its rings, w the grid's rings, their radii after
//	            it (OceanGrid::FrameRings), then the disc's alpha by radius
//	            for the deferred ocean (OceanGrid::AlphaTable)
//	  vLightColor, vLightAtten  the grid's bodies: [n] the middle of the
//	            box (xy) and the cos and sin of its turn (zw); its half sides
//	            (xy)
//	  vAddColor with vTexGenU.w: x the RMS along an axis of the FFT ocean's
//	            slopes, y the variance of its ripple finer than the short
//	            cascade (both axes, times the bump amounts' factor; the
//	            glint's in place of vSpecular.y); the ocean with a fog
//	            volume: w 1 the light in its water (d3d12_WaterVolume,
//	            OceanVolume; 0 OceanFog), 2 and from below the surface's
//	            light alone (d3d12_WaterUnder: Underwater.hlsl lays the
//	            water's between it and the camera over it), z the sun's
//	            share of the light the water scatters (0: none in the sun's
//	            shadow)
//	  vSpecular the sun's glint: x its strength (d3d12_WaterGlint; 0: none),
//	            y the variance of the slopes of the ripple finer than the
//	            texture (d3d12_WaterGlintRoughness), z its most light (the
//	            sun's flare's in the HDR targets, else 1), w the sun's shadow:
//	            1 the cascades', 2 a ray (XR_RAYTRACE)
//
//////////////////////////////////////////////////////////////////////

#include "Common.hlsli"
#include "WaterLight.hlsli"
#include "WaterFoam.hlsli"
#include "WaterVolume.hlsli"
#include "Underwater.hlsli"
#include "SkyFogBand.hlsli"
#ifdef XR_RAYTRACE
#include "RayShade.hlsli"
#else
// The raw buffers (the texture table, D3D12Pipeline.h's space4): the ocean's grid's table of the game's disc.
ByteAddressBuffer g_Buffers[] : register(t0, space4);
#endif
// The ocean's grid's table of the game's disc and of its frame's rings (OceanGrid.hlsli's GridDiscAlpha and GridRing):
// vLightSpec[4] x (uint) its raw buffer, y (uint) its byte offset there.
#define GRID_DISC_WORD(i) asfloat(g_Buffers[asuint(g_DrawExt.vLightSpec[4].x)].Load(asuint(g_DrawExt.vLightSpec[4].y) + 4 * (i)))
#define GRID_DISC_WORDS2(i) asfloat(g_Buffers[asuint(g_DrawExt.vLightSpec[4].x)].Load2(asuint(g_DrawExt.vLightSpec[4].y) + 4 * (i)))
#include "OceanGrid.hlsli"

struct SWaterOutput
{
  float4 vPos : SV_Position;
  float3 vWorldPos : TEXCOORD0;
  float4 vColor : COLOR0;
  float2 vTex0 : TEXCOORD1;
  float4 vGrid : TEXCOORD4;     //!< the ocean's grid (OceanGridVertex's vShares; else 0, 1, 1, 1)
#ifdef XR_VELOCITY
  float4 vCurClip : TEXCOORD2;  //!< the clip position without the jitter
  float4 vPrevClip : TEXCOORD3; //!< the same in the frame before
#endif
};

//! The wakes' height (m) at P for the ocean's grid (vLightSpec[3].x; Wake.cpp's heights, each texel the cell of its index
//! modulo the window's size as WaterWake's view) at the mip level of the grid's fSpacing there, fading out over the sponge
//! as WaterWake's slopes.
float WakeGridHeight(float2 P, float fSpacing)
{
  const float4 c0 = g_DrawExt.vLightPos[0], c1 = g_DrawExt.vLightPos[1];
  const float2 d = P - c0.xy;
  const float fFade = saturate((0.5 * XR_WAKE_SIZE * XR_WAKE_CELL - max(abs(d.x), abs(d.y))) * c1.x) * c1.w;
  if (fFade <= 0)
    return 0;
  const float2 vUV = d / (XR_WAKE_SIZE * XR_WAKE_CELL) + c0.zw;
  return g_Textures[asuint(g_DrawExt.vLightSpec[3].y)].SampleLevel(g_SamplerLinearWrap, vUV, GridLod(fSpacing, XR_WAKE_CELL)).x * fFade;
}

//! The depth (m) under the ocean's level (vLightSpec[0].y) at P from the terrain's heights (vLightSpec[1].z, its texture
//! coordinates vLightSpec[3].zw): deep water off the terrain.
float GridTerrainDepth(float2 P)
{
  const float2 vTerrainUV = P * g_DrawExt.vLightSpec[3].z + g_DrawExt.vLightSpec[3].w;
  float fDepth = 1e4;
  if (all(vTerrainUV >= 0) && all(vTerrainUV <= 1))
    fDepth = g_DrawExt.vLightSpec[0].y - g_Textures[asuint(g_DrawExt.vLightSpec[1].z)].SampleLevel(g_SamplerLinearClamp, vTerrainUV, 0).x;
  return fDepth;
}

//! The share of the open sea at P on the ocean's grid (d3d12_WaterFoamPonds, vLightSpec[5].z; the terrain's texture's y,
//! WaterBodies.h): 0 over a pond or a lake the level's terrain cuts off from the sea, 1 on the sea and off the terrain's
//! map; 1 off the grid.
float GridOpenShare(float2 P)
{
  if (g_DrawExt.vLightSpec[0].x == 0 || g_DrawExt.vLightSpec[5].z <= 0)
    return 1;
  const float2 vTerrainUV = P * g_DrawExt.vLightSpec[3].z + g_DrawExt.vLightSpec[3].w;
  if (any(vTerrainUV < 0) || any(vTerrainUV > 1))
    return 1;
  return g_Textures[asuint(g_DrawExt.vLightSpec[1].z)].SampleLevel(g_SamplerLinearClamp, vTerrainUV, 0).y;
}

//! The height z (m) of the scene behind the ocean's grid's surface point P as the water there takes it: what stands over
//! the water behind a crest hidden by it (GridHiddenShare), the floor under P in its place (GridTerrainDepth), so that
//! the water's light through the crest goes down to that floor (the scene's colour as its: in shallow water the beach
//! behind as its sand) and the foam of the shore and of what stands in the water takes that floor's depth (by the boat's
//! side behind a crest, its waterline drew the foam of the waves against it over the crest).
float GridBehindZ(float z, float3 P)
{
  const float fHidden = GridHiddenShare(z, P.z, g_DrawExt.vLightSpec[0].y);
  [branch]
  if (fHidden <= 0)
    return z;
  return lerp(z, min(g_DrawExt.vLightSpec[0].y - GridTerrainDepth(P.xy), P.z), fHidden);
}

//! The floor behind the ocean's grid's surface as its foam takes it (d3d12_WaterFoamTerrain, vLightSpec[5].x m): the scene
//! behind's point vFloor; where that lies within it of the level's terrain heights there (GridTerrainDepth), the terrain as
//! its level of detail draws it, those heights. The level of detail's triangles change from frame to frame, and in shallow
//! water the foam of the shore followed their edges (Swamp's ponds, step 331). What stands higher over the terrain (a rock,
//! a post) keeps its own point.
float FoamFloorZ(float3 vFloor)
{
  const float fSnap = g_DrawExt.vLightSpec[5].x;
  if (fSnap <= 0)
    return vFloor.z;
  const float fTerrain = g_DrawExt.vLightSpec[0].y - GridTerrainDepth(vFloor.xy);
  return abs(vFloor.z - fTerrain) <= fSnap ? fTerrain : vFloor.z;
}

//! The ocean's grid (vLightSpec[0].x; OceanGrid.cpp): the vertex vIn (xy its direction across the water from the grid's
//! centre, the camera, z its ring) on the frame's ring (GridRing: vLightSpec[4].w of them after the disc's table), pulled
//! onto the edge of the game's disc beyond it, raised to the waves' height there: the FFT ocean's cascades' (each at the
//! mip level of the grid's spacing there), none under the bodies, and the wakes'; none at a camera at the water and far
//! off, limited by the depth under the level (OceanGrid.hlsli). vShares: x the height (m), y the share of it the depth
//! left, z the bodies' share of the ocean's waves, w the camera's and the distance's.
float3 OceanGridVertex(float3 vIn, out float4 vShares)
{
  const float4 g0 = g_DrawExt.vLightSpec[0], g1 = g_DrawExt.vLightSpec[1], g2 = g_DrawExt.vLightSpec[2], g3 = g_DrawExt.vLightSpec[3];
  const uint k = (uint)vIn.z;
  const float2 vRing = GridRing(k, (uint)g_DrawExt.vLightSpec[4].z * (1 + XR_GRID_DISC_SEGMENTS), (uint)g_DrawExt.vLightSpec[4].w);
  float r = vRing.x;
  float2 vLocal = k > 0 ? normalize(vIn.xy) * r : 0;
  if (r > g0.z)
  {
    vLocal *= g0.z / r;
    r = g0.z;
  }
  vShares = float4(0, 1, 1, 1);
  if (r >= XR_GRID_DISPLACED)
    return float3(vLocal, 0);
  const float2 P = g1.xy + vLocal;
  const float fSpacing = max(GridSpacing(r), vRing.y);
  static const float Tiles[XR_OCEAN_CASCADES] = { XR_OCEAN_TILE0, XR_OCEAN_TILE1, XR_OCEAN_TILE2 };
  float h = 0;
  [unroll]
  for (uint c = 0; c < XR_OCEAN_CASCADES; c++)
    h += g_TextureArrays[g_DrawExt.nShadowTex.z].SampleLevel(g_SamplerLinearWrap, float3(P / Tiles[c], c),
                                                          GridLod(fSpacing, Tiles[c] / XR_OCEAN_SIZE)).z * g2[c];
  float fBodies = 1;
  for (uint n = 0; n < (uint)g0.w; n++)
    fBodies = min(fBodies, GridBodyShare(P, g_DrawExt.vLightColor[n], g_DrawExt.vLightAtten[n]));
  h *= fBodies;
  if (g3.x != 0)
    h += WakeGridHeight(P, fSpacing);
  const float fNear = GridCameraShare(r, g1.w) * GridFarShare(r);
  h = GridCameraCap(h * fNear, r, g1.w);
  const float fDepth = GridTerrainDepth(P);
  const float fRaised = GridShore(h, fDepth);
  vShares = float4(fRaised, abs(h) > 1e-5 ? fRaised / h : 1, fBodies, fNear);
  return float3(vLocal, fRaised);
}

SWaterOutput VSMain(SVertexInput In)
{
  SWaterOutput Out;
  float4 vPos = float4(In.vPos.xyz, 1);
  Out.vGrid = float4(0, 1, 1, 1);
  Out.vColor = DecodeVertexColor(In.vColor0);
  if (g_DrawExt.vLightSpec[0].x != 0)
  {
    vPos.xyz = OceanGridVertex(In.vPos.xyz, Out.vGrid);
    Out.vColor.a = GridDiscAlpha(vPos.xy, (uint)g_DrawExt.vLightSpec[4].z);
  }
  Out.vPos = Jitter(mul(vPos, g_Draw.mWorldViewProj));
#ifdef XR_VELOCITY
  Out.vCurClip = mul(vPos, g_Draw.mWorldViewProj);
  Out.vPrevClip = mul(vPos, g_Draw.mPrevWorldViewProj);
#endif
  Out.vWorldPos = mul(vPos, g_Draw.mWorld).xyz;
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
//! (the texture's alpha) less the square of the mean slope, for each layer; fHeight: the height of
//! the waves (the texture's blue) in the shape of the waves the normal shows, each layer's in the
//! weight of its slopes (the long waves' metres would make patches tens of metres wide of what the
//! light through the crests, WaterCrestLight, finds on the short waves' sharper crests), RMS 1 (as
//! far as the mip levels leave it).
//! WaveNormal of the FFT ocean's waves (vTexGenU.w; OceanFFT.cpp): the three cascades (the slices of
//! g_TextureArrays[nShadowTex.z]) over their tiles, their slopes in metres per metre summed, times
//! fScale (the bump amounts' factor); they run and change their shape by themselves (no time).
//! fLostVar the variance their mip levels smoothed out of each, fHeight their heights each in the
//! weight of its slopes (RMS 1, OceanSpectrum.h's HeightWeight).
float3 OceanWaveNormal(float2 vPos, float fScale, out float fLostVar, out float fHeight)
{
  static const float Tiles[XR_OCEAN_CASCADES] = { XR_OCEAN_TILE0, XR_OCEAN_TILE1, XR_OCEAN_TILE2 };
  float2 vSlope = 0;
  fLostVar = 0;
  fHeight = 0;
  [unroll]
  for (uint c = 0; c < XR_OCEAN_CASCADES; c++)
  {
    float4 t = g_TextureArrays[g_DrawExt.nShadowTex.z].Sample(g_SamplerLinearWrap, float3(vPos / Tiles[c], c));
    vSlope += t.xy;
    fLostVar += max(t.w - dot(t.xy, t.xy), 0);
    fHeight += t.z;
  }
  fLostVar *= fScale * fScale;
  return normalize(float3(-vSlope * fScale, 1));
}

float3 WaveNormal(float2 vPos, float fTime, float fScale, out float fLostVar, out float fHeight)
{
  if (g_DrawExt.vTexGenU.w != 0)
    return OceanWaveNormal(vPos, fScale, fLostVar, fHeight);
  static const float4 Layers[3] =
  {
    float4( 0.94,  0.34, 61.0, 1.10),
    float4( 0.50, -0.87, 17.3, 0.60),
    float4(-0.60,  0.80,  5.9, 0.35),
  };
  static const float Weights[3] = { 0.55, 0.40, 0.30 };
  // 1 / the RMS of the layers' heights together: sqrt of the sum of Weights[i]^2.
  static const float HEIGHT_NORM = 1 / 0.743;
  float2 vSlope = 0;
  fLostVar = 0;
  fHeight = 0;
  [unroll]
  for (int i = 0; i < 3; i++)
  {
    float2 d = Layers[i].xy, e = float2(-d.y, d.x);
    float2 vUV = float2(dot(vPos, d) - fTime * Layers[i].w, dot(vPos, e)) / Layers[i].z;
    float4 t = g_Textures[g_Draw.nTex5].Sample(g_SamplerLinearWrap, vUV);
    float2 s = (t.rg * 255 - 128) / 32;
    vSlope += (s.x * d + s.y * e) * Weights[i];
    fLostVar += max(t.a * 16 - dot(s, s), 0) * (Weights[i] * Weights[i]);
    fHeight += (t.b * 255 - 128) / 32 * Weights[i];
  }
  fLostVar *= fScale * fScale;
  fHeight *= HEIGHT_NORM;
  return normalize(float3(-vSlope * fScale, 1));
}

//! The wakes (d3d12_WaterWakes, vLightPos[1].w; Wake.cpp, Shaders/Wake.hlsl) at P: their slopes (xy: dh/dx, dh/dy), the
//! variance their mip levels smoothed out of the pixel (z), their foam's share white (w; none without vLightPos[1].y), fading
//! out over the sponge at the window's edge. vLightPos[0] xy the window's centre point (m), zw its texel there over the
//! window's size (each texel the cell of its index modulo the size: the sampler wraps); vLightPos[1] x 1 / the sponge's
//! width (m), z (as uint) the view's SRV, w the share shown (fading out as the window calms down).
float4 WaterWake(float2 P)
{
  const float4 c0 = g_DrawExt.vLightPos[0], c1 = g_DrawExt.vLightPos[1];
  const float2 d = P - c0.xy;
  const float fFade = saturate((0.5 * XR_WAKE_SIZE * XR_WAKE_CELL - max(abs(d.x), abs(d.y))) * c1.x) * c1.w;
  const float4 t = g_Textures[asuint(c1.z)].Sample(g_SamplerLinearWrap, d / (XR_WAKE_SIZE * XR_WAKE_CELL) + c0.zw);
  return float4(t.xy * fFade, max(t.z - dot(t.xy, t.xy), 0) * (fFade * fFade), t.w * c1.y * fFade);
}

//! The RMS along one axis of WaveNormal's slopes before fScale: the layers' together, sqrt of the
//! sum of Weights^2.
static const float WAVE_SLOPE_RMS = 0.743;

//! The RMS along one axis of WaveNormal's slopes at fScale: the wave texture's or the FFT ocean's.
float WaveSlopeRMS(float fScale)
{
  return fScale * (g_DrawExt.vTexGenU.w != 0 ? g_DrawExt.vAddColor.x : WAVE_SLOPE_RMS);
}

float3 WaveNormal(float2 vPos, float fTime, float fScale, out float fLostVar)
{
  float fHeight;
  return WaveNormal(vPos, fTime, fScale, fLostVar, fHeight);
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
  if (g_DrawExt.vParams1.y == 0)
    return vFog;
  vDir.z = max(vDir.z, 0.0);
  // Into the box's axes: the box is drawn turned by (c s; -s c) (row vectors), so back by its transpose.
  float c = g_DrawExt.vTexGenU.y, s = g_DrawExt.vTexGenU.z;
  vDir.xy = float2(c * vDir.x + s * vDir.y, -s * vDir.x + c * vDir.y);
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
    float fZ1 = g_DrawExt.vParams1.x;
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
  return lerp(vFog, vTex.rgb, g_DrawExt.vParams1.z);
}

//! The sky as the sky draws it, in the direction vDir (up or level) seen from a camera at height fCameraZ: the sky box
//! brighter towards the sun with HDR (SkyGlow), the fog band over its lower part (vParams1.y 2; DrawSkyItem, not
//! brighter towards the sun) over it.
float3 SkySeen(float3 vDir, float fCameraZ)
{
  float3 vSky = SkyBoxColor(vDir) * SkyGlow(vDir);
  if (g_DrawExt.vParams1.y == 2)
    vSky = lerp(vSky, g_Frame.vFogColor.rgb, SkyFogBandAlpha(vDir, SkyFogBandEdge(g_Frame.vFogParams.y, fCameraZ)));
  return vSky;
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
  float4 vClip = mul(float4(P + R * fReach, 1), g_DrawExt.mParams0);
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
  float2 vUV = ClipToUV(mul(float4(P + R * fReach, 1), g_DrawExt.mParams0), 1);
  float fUp = 2 * sqrt(fVar * 0.5), fSide = fUp * c;
  float3 Th = cross(R, float3(0, 0, 1));
  float fLength = length(Th);
  Th = fLength > 1e-4 ? Th / fLength : float3(1, 0, 0);
  float3 Tv = cross(Th, R);
  float4 vClip = mul(float4(R, 0), g_DrawExt.mParams0);
  float4 vClipUp = mul(float4(R + Tv * fUp, 0), g_DrawExt.mParams0);
  float4 vClipSide = mul(float4(R + Th * fSide, 0), g_DrawExt.mParams0);
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

//! Whether the steps of ScreenReflection's march from t ta to tb can all go by without a look at the
//! copies: none of them gets nearer than 0.3 m to the camera, and under each on the screen the depth
//! copy holds the sky or what is farther than the step (the march ends at the first off it, a miss,
//! and the steps after it are off it too: their path on the screen is a straight line). Their clip
//! positions are on the line vC0 + t vCR (w above 0 all along it: their path on the screen is the
//! line between its ends'), fError at most off it either way; the depth tiles of the level whose
//! 2 x 2 texels cover that path, widened by the error (divided by w) and two texels (the sampler's)
//! and cut to the screen, give the least depth under it. vSize: the copies' size in texels.
bool MarchClear(float4 vC0, float4 vCR, float ta, float tb, float fError, float2 vViewToBuffer, float2 vSize)
{
  const float4 a = vC0 + vCR * ta, b = vC0 + vCR * tb;
  const float wMin = min(a.w, b.w) - 2 * fError;
  if (wMin <= 0.31)
    return false;
  const float2 vView = vViewToBuffer * vSize;
  const float2 ua = ClipToUV(a, vViewToBuffer) * vSize, ub = ClipToUV(b, vViewToBuffer) * vSize;
  const float2 vWiden = 2 * vView * fError / wMin + 2;
  const float2 vLow = max(min(ua, ub) - vWiden, 0), vHigh = min(max(ua, ub) + vWiden, vView - 1);
  if (any(vLow > vHigh))
    return true;
  const uint2 t0 = (uint2)vLow / XR_DEPTH_TILE, t1 = (uint2)vHigh / XR_DEPTH_TILE;
  // A level where the tiles of the two corners are the same or next to each other: that of the
  // highest bit they differ in, or the first whose texels are wider than the path (the tile of its
  // low corner and the next then cover it), whichever is lower.
  const uint2 vDiffer = t0 ^ t1, vSpan = t1 - t0;
  const uint nLevel = (uint)max(min((int)firstbithigh(max(vDiffer.x, vDiffer.y)), (int)firstbithigh(max(vSpan.x, vSpan.y)) + 1), 0);
  if (nLevel >= g_DrawExt.nShadowTex.y)
    return false;
  const uint2 p0 = t0 >> nLevel, p1 = t1 >> nLevel;
  const uint nTiles = g_DrawExt.nShadowTex.x;
  const float d = min(min(g_Textures[nTiles].Load(int3(p0, nLevel)).r, g_Textures[nTiles].Load(int3(p1.x, p0.y, nLevel)).r),
                      min(g_Textures[nTiles].Load(int3(p0.x, p1.y, nLevel)).r, g_Textures[nTiles].Load(int3(p1, nLevel)).r));
  // The march's test of a step: fDepth < 0.99999 (not the sky) and w > LinearDepth(fDepth).
  return d >= 0.99999 || LinearDepth(d) * 0.999 > max(a.w, b.w) + 2 * fError;
}

//! The scene in direction R from P where the frame's copies show it: the ray marches in steps
//! that grow with the distance; when it gets behind the depth buffer the crossing is refined, and
//! it is a hit if the ray is then no more than a few centimetres behind the surface there (more
//! means it passed behind an object, whose back the copies do not show: it goes on, or with
//! bRaysAfter it ends there, a miss, for the rays to find what is behind). Never hits the sky (the
//! far plane) or the first person weapon (nearer than 0.5 m). w: how much to trust it, 0 on a
//! miss, fading towards the screen's edges. The steps go in fours, their depths read at once: the
//! same steps as one at a time, with fewer waits for the reads. Four steps that the depth tiles
//! show cannot end the march (MarchClear) go by without being worked out: the same result.
//! vSize: the copies' size in texels.
float4 ScreenReflection(float3 P, float3 R, float2 vViewToBuffer, float2 vSize, bool bRaysAfter)
{
  // The steps' clip positions are on the line vC0 + t vCR but for rounding: at most 4e-6 of the
  // largest of their sums and products (several times their rounding, at either end), those of the
  // world position (no coordinate larger than fLargest + t) by the matrix's rows of x, y and z, and
  // the row of the translation.
  const float4 vC0 = mul(float4(P, 1), g_Frame.mViewProj), vCR = mul(float4(R, 0), g_Frame.mViewProj);
  const float4 vTurn = abs(g_Frame.mViewProj[0]) + abs(g_Frame.mViewProj[1]) + abs(g_Frame.mViewProj[2]);
  const float4 vMove = abs(g_Frame.mViewProj[3]);
  const float fTurn = 4e-6 * max(vTurn.x, max(vTurn.y, vTurn.w)), fMove = 4e-6 * max(vMove.x, max(vMove.y, vMove.w));
  const float fLargest = max(abs(P.x), max(abs(P.y), abs(P.z)));
  const bool bTiles = g_DrawExt.nShadowTex.y != 0;
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
      t = t * 1.18 + 0.15;
    }
    if (bTiles && MarchClear(vC0, vCR, tStep[0], tStep[3], fTurn * (fLargest + tStep[3]) + fMove, vViewToBuffer, vSize))
    {
      tPrev = tStep[3];
      continue;
    }
    [unroll]
    for (int j = 0; j < 4; j++)
    {
      vClip[j] = mul(float4(P + R * tStep[j], 1), g_Frame.mViewProj);
      vUV[j] = ClipToUV(vClip[j], vViewToBuffer);
      fDepth[j] = g_Textures[g_Draw.nTex1].SampleLevel(g_SamplerPointClamp, vUV[j], 0).r;
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
  Ray.TMax = g_DrawExt.vTexGenV.y;
  const uint nMask = asuint(g_DrawExt.vTexGenV.z);
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

//! How much of the sun reaches the water at P (vSpecular.w): 2 a ray towards it (XR_RAYTRACE, with
//! d3d12_RTSunShadows; fConeWidth the pixel's width there, for the alpha tests), else the cascades'
//! (CascadeTap, a bilinear comparison); beyond them the ocean in full sun, a water volume (it may be
//! indoors) in none.
float WaterSunShadow(float3 P, float fConeWidth)
{
#ifdef XR_RAYTRACE
  if (g_DrawExt.vSpecular.w == 2)
    return TraceSun(P, float3(0, 0, 1), asuint(g_DrawExt.vTexGenV.z) & 0xff, fConeWidth);
#endif
  const uint n = min((uint)g_Frame.vCascadeParams.x, XR_MAX_CASCADES);
  if (n == 0 || ViewDepth(P) >= g_Frame.vCascadeEnd[n - 1])
    return g_DrawExt.vParams3.w == 1 ? 0 : 1;
  return CascadeTap(P);
}

//! Whether b holds for any lane of the pixel's quad (its 2 x 2, the helpers too): a branch on it keeps the derivatives
//! of what is inside it.
bool FoamQuadAny(bool b)
{
  const uint u = b ? 1u : 0u;
  return (u | QuadReadAcrossX(u) | QuadReadAcrossY(u) | QuadReadAcrossDiagonal(u)) != 0;
}

//! The foam on the FFT ocean (vTexGen1U.x, d3d12_WaterFoam; WaterFoam.hlsli) at the world position P, the water fDepth
//! deep there (m), fPath the view's path through the water to the scene behind (m, the undisplaced pixel's): how much of the water's light the foam over the pixel hides (its share white, thin foam less: the
//! breaker index from a slowly drifting noise, vTexMat1); fWhitecaps, fShore the amounts of the whitecaps and of the waves breaking
//! on the shore (the debug views). The foam's texels (nShadowTex.w, OceanFFT.hlsli's OceanFoam) of the long and the middle
//! cascades: the whitecaps (their mip levels the share of the sea white), the displacement (the pattern taken where the
//! water moved here from, so that it sways with the waves; drifting with the wind, vTexMat0), the height (the crest: the
//! highest of the long waves' now and as they were 0.7 and 1.4 s ago, what was here then that far ahead now at the speed
//! of the spectrum's peak, vTexGen1V.xy, weaker as it passes; scaled as the slopes, vParams0.x). The shore's foam only
//! in the shallows and by what stands in the water, the pattern only where there is foam (most of the sea has none).
//! fShade the foam's brightness there (FoamShade, 1 on the mean). fWake the wakes' foam (WaterWake), torn into clumps
//! (WakeClumps) and added to the amount.
float WaterFoam(float2 P, float fDepth, float fPath, float fWake, out float fWhitecaps, out float fShore, out float fShade)
{
  const uint t = g_DrawExt.nShadowTex.w;
  fShade = 1;
  const float4 f0 = g_TextureArrays[t].Sample(g_SamplerLinearWrap, float3(P / XR_OCEAN_TILE0, 0));
  const float4 f1 = g_TextureArrays[t].Sample(g_SamplerLinearWrap, float3(P / XR_OCEAN_TILE1, 1));
  // None of the sea's foam on a pond the terrain cuts off from it (GridOpenShare): no fetch for the wind's waves, no swell.
  const float fOpen = GridOpenShare(P);
  fWhitecaps = WhitecapAmount(f0.x, f1.x, f0.w, g_DrawExt.vTexGen1U.y) * fOpen;
  fShore = 0;
  // Out to the deepest water a wave breaks in and to the reach of the foam by what stands in it: the surf's height over
  // the least breaker index, the wind sea's highest crests (about its significant height) twice over it, 5 of it.
  const float fNear = max(g_DrawExt.vTexGen1V.z / FoamBreakerIndex(0), 5 * g_DrawExt.vTexGen1U.z) + 0.05;
  if (FoamQuadAny((fDepth < fNear || fPath < fNear) && fOpen > 0))
  {
    const float2 vAhead = g_DrawExt.vTexGen1V.xy;
    const float h1 = g_TextureArrays[t].Sample(g_SamplerLinearWrap, float3((P + vAhead * 0.7) / XR_OCEAN_TILE0, 0)).w;
    const float h2 = g_TextureArrays[t].Sample(g_SamplerLinearWrap, float3((P + vAhead * 1.4) / XR_OCEAN_TILE0, 0)).w;
    const float fCrest = max(max(f0.w + f1.w, 0.75 * h1), 0.5 * h2) * g_DrawExt.vParams0.x;
    // The breaker index from two octaves of noise 6 and 2.6 m apart, changing slowly as they drift apart (vTexMat1.xy).
    const float2 R = float2(XR_FOAM_TURN_COS * P.x - XR_FOAM_TURN_SIN * P.y, XR_FOAM_TURN_SIN * P.x + XR_FOAM_TURN_COS * P.y);
    const float fNoise = 0.65 * FoamValueNoise((P + g_DrawExt.vTexMat1.xy) / 6) + 0.35 * FoamValueNoise((R - g_DrawExt.vTexMat1.xy) / 2.6);
    const float fIndex = FoamBreakerIndex(0.5 + 1.6 * (fNoise - 0.5));
    fShore = max(ShoreFoam(fDepth, fCrest, g_DrawExt.vTexGen1U.z, fIndex), ContactFoam(fPath, fCrest, g_DrawExt.vTexGen1U.z));
    // The swell's surf (vTexGen1V.z its height, 0 none; w the periods gone): its fronts not quite along the depth's lines,
    // their phase from a noise 40 m across; torn into clumps.
    if (g_DrawExt.vTexGen1V.z > 0)
    {
      const float fBreak = g_DrawExt.vTexGen1V.z / fIndex, fCycle = g_DrawExt.vTexGen1V.w + 0.6 * FoamValueNoise(P / 40);
      const float fSurf = SurfFoam(fDepth, fBreak, fCycle);
      if (fSurf > 0)
        fShore = max(fShore, saturate(fSurf * SurfClumps(P, fDepth, fBreak, fCycle)));
    }
    fShore *= fOpen;
  }
  const float fAmount = saturate((fWhitecaps + fShore + (fWake > 0 ? WakeClumps(P, fWake) : 0)) * g_DrawExt.vTexGen1U.x);
  if (!FoamQuadAny(fAmount > 0))
    return 0;

  const float2 Q = P - (f0.yz + f1.yz);
  const float2 uv0 = (Q + g_DrawExt.vTexMat0.xy) / XR_FOAM_TILE0;
  const float2 uv1 = (float2(XR_FOAM_TURN_COS * Q.x - XR_FOAM_TURN_SIN * Q.y, XR_FOAM_TURN_SIN * Q.x + XR_FOAM_TURN_COS * Q.y) +
                      g_DrawExt.vTexMat0.zw) / XR_FOAM_TILE1;
  const uint p = asuint(g_DrawExt.vTexGen1U.w);
  const float u = FoamUniform(g_Textures[p].Sample(g_SamplerLinearWrap, uv0).r, g_Textures[p].Sample(g_SamplerLinearWrap, uv1).r);
  const float fTexels = max(length(fwidth(uv0)), length(fwidth(uv1))) * XR_FOAM_SIZE;
  const float fBlur = saturate(log2(max(fTexels, 1)) / 3);
  fShade = FoamShade(u, fAmount, fBlur);
  // Where the water meets what stands in it the foam fades in over its first 2 cm (no hard edge).
  return FoamOpacity(FoamCover(fAmount, u, fwidth(u), fBlur), fAmount) * saturate(fDepth * 50);
}

//! The sky's light on a white face facing up (linear): the sky box in five directions (as the water's own colour lit by the
//! sky takes it), the fog's colour without one.
float3 WaterSkyLight()
{
  float3 vSky = g_Frame.vFogColor.rgb;
  if (g_DrawExt.vParams1.y != 0)
    vSky = 0.2 * (SkyBoxColor(float3(0, 0, 1)) + SkyBoxColor(float3(1, 0, 0.3)) + SkyBoxColor(float3(-1, 0, 0.3)) +
                  SkyBoxColor(float3(0, 1, 0.3)) + SkyBoxColor(float3(0, -1, 0.3)));
  return float3(HdrDecode(vSky.r), HdrDecode(vSky.g), HdrDecode(vSky.b));
}

//! The light of a white face turned to the sun: the game lights a texture modulated by 2 with twice
//! the sun's colour (General.hlsl's DrawLighting), in gamma space (HdrDecode), as the path tracer's
//! sun (PathShade.hlsli).
float3 WaterSunLight()
{
  const float3 c = 2 * g_Frame.vSunColor.rgb;
  return float3(HdrDecode(c.r), HdrDecode(c.g), HdrDecode(c.b));
}

float3 WaterDecode(float3 c)
{
  return float3(HdrDecode(c.r), HdrDecode(c.g), HdrDecode(c.b));
}

float3 WaterEncode(float3 v)
{
  return float3(HdrEncode(v.r), HdrEncode(v.g), HdrEncode(v.b));
}

//! The ocean's water seen from above (d3d12_WaterVolume, vAddColor.w; WaterVolume.hlsli), in linear light: the scene
//! behind vScene (the point there vBehind) through the water, attenuated in each channel (from its fog volume's colour,
//! vParams3, and density, vParams2.y) along the view's ray refracted at the waves' normal N down to it and along the light's
//! way down to its depth (the sun's ray refracted), and the light the water scatters towards the camera on the way; the
//! sun's share of that light (vAddColor.z) in the sun's shadow at each segment of the ray, where the sun's refracted ray to
//! it left the surface (one bilinear comparison of the cascades a segment, CascadeTap). vFloor the share of the scene
//! behind left, vInscatter the water's light, fShade the share of the sun's left in it (the debug views).
float3 OceanVolume(float3 vScene, float3 P, float3 N, float3 V, float3 vBehind, float2 vPixel, out float3 vFloor, out float3 vInscatter,
                   out float fShade)
{
  const float3 vDeep = WaterDecode(g_DrawExt.vParams3.rgb);
  const float3 c = VolumeAttenuation(vDeep, g_DrawExt.vParams2.y);
  const float3 T = VolumeRefract(V, N);
  const float fMu = max(-T.z, 0.1);
  const float fDepth = max(P.z - vBehind.z, 0);
  const float fPath = fDepth / fMu;
  const float3 L = g_Frame.vSunDir.xyz;
  vFloor = exp(-c * (fPath + fDepth / VolumeRefractedCos(saturate(L.z))));
  const float3 k = c * (1 + fMu);
  const float fShare = g_DrawExt.vAddColor.z;
  fShade = 1;
  const uint n = min((uint)g_Frame.vCascadeParams.x, XR_MAX_CASCADES);
  const float fReach = VolumeReach(k, fPath);
  // The points where the sun's rays to the ray's points left the surface: P + t W (level, the sun's ray climbs back the
  // depth the view's went down), nearer than P by at most t |W|; all beyond the cascades, none of them in their shadow.
  const float3 W = VolumeSunEntry(T, fMu, L);
  const bool bTaps = fShare > 0 && n > 0 && fPath > 0.01 && ViewDepth(P) - fReach * length(W) < g_Frame.vCascadeEnd[n - 1];
  // One comparison a pixel, the quad's four shared: lane q (its place in the 2 x 2 pixels) the sun of the q-th segment
  // of its own ray, the quad's rays a pixel apart (at a quarter of the cost of four, the sun in the water the same for
  // the quad's pixels); where any of the quad's pixels needs them.
#if XR_VOLUME_TAPS != 4
#error The quad's lanes take a segment each.
#endif
  float fTap = 1;
  if (FoamQuadAny(bTaps))
    fTap = CascadeTap(P + W * VolumeSegmentPoint((uint)vPixel.x % 2 + 2 * ((uint)vPixel.y % 2), fReach));
  const float4 vSun = saturate(float4(QuadReadLaneAt(fTap, 0), QuadReadLaneAt(fTap, 1), QuadReadLaneAt(fTap, 2), QuadReadLaneAt(fTap, 3)));
  [branch]
  if (bTaps)
  {
    float3 vSum = 0, e0 = 1;
    float v = 1, fWeight = 0, fLit = 0;
    [unroll]
    for (uint j = 0; j < XR_VOLUME_TAPS; j++)
    {
      v = vSun[j];
      const float3 e1 = exp(-k * VolumeSegmentEnd(j, fReach));
      const float f = 1 - fShare + fShare * v;
      vSum += (e0 - e1) * f;
      fWeight += e0.g - e1.g;
      fLit += (e0.g - e1.g) * v;
      e0 = e1;
    }
    const float3 e = exp(-k * fPath);
    vSum += (e0 - e) * (1 - fShare + fShare * v);
    fLit += (e0.g - e.g) * v;
    fWeight += e0.g - e.g;
    vInscatter = vDeep * 2 / (1 + fMu) * vSum;
    fShade = fWeight > 0 ? fLit / fWeight : 1;
  }
  else
    vInscatter = VolumeInscatter(vDeep, c, c, fMu, 0, fPath);
  return vScene * vFloor + vInscatter;
}

//! The scene above the ocean seen from below through Snell's window at the surface point P: the light coming down along D
//! (the view's ray refracted out of the water, up), where the screen's copy shows it in that direction (what the camera
//! sees along D is above the water there: the copy's depth beyond where D leaves it), else the sky as the sky draws it
//! (SkySeen; without a sky box the fog's colour). vViewToBuffer the viewport's size over the buffer's.
float3 SceneAbove(float3 P, float3 D, float2 vViewToBuffer)
{
  const float4 vClip = mul(float4(D, 0), g_Frame.mViewProj);
  if (vClip.w > 0)
  {
    const float2 vUV = ClipToUV(vClip, vViewToBuffer);
    if (all(vUV >= 0) && all(vUV <= vViewToBuffer))
    {
      const float3 vCamera = g_Frame.vCameraPos.xyz;
      const float fLeaves = ViewDepth(vCamera + D * ((P.z - vCamera.z) / max(D.z, 1e-3)));
      if (LinearDepth(g_Textures[g_Draw.nTex1].SampleLevel(g_SamplerPointClamp, vUV, 0).r) > fLeaves)
        return g_Textures[g_Draw.nTex0].SampleLevel(g_SamplerLinearClamp, vUV, 0).rgb;
    }
  }
  return g_DrawExt.vParams1.y != 0 ? SkySeen(D, g_Frame.vCameraPos.z) : g_Frame.vFogColor.rgb;
}

float4 ShadeWater(SWaterOutput In)
{
  float3 vCamera = g_Frame.vCameraPos.xyz;
  float3 vToCamera = vCamera - In.vWorldPos;
  float fDistance = length(vToCamera);
  float3 V = vToCamera / max(fDistance, 1e-4);
  // From below by the level (the ocean's grid raised: as the game takes the camera to be under the water).
  bool bBelow = vCamera.z < (g_DrawExt.vLightSpec[0].x != 0 ? g_DrawExt.vLightSpec[0].y : In.vWorldPos.z);
  // The ocean's grid's debug views (OceanGridVertex's shares), before anything else holds registers.
  if (g_DrawExt.vParams0.z == 29)
    return float4(saturate(0.5 + In.vGrid.x / 0.6), saturate(0.5 + In.vGrid.x / 0.06), saturate(0.5 - In.vGrid.x / 0.6), 1);
  if (g_DrawExt.vParams0.z == 30)
    return float4(In.vGrid.yzw, 1);
#ifdef XR_RAYTRACE
  // The angle between neighbouring pixels' view rays (before any branch).
  float fPixelAngle = max(length(ddx(V)), length(ddy(V)));
#endif

  float fLostVar, fHeight;
  float3 N = WaveNormal(In.vWorldPos.xy, g_Frame.vTime.x, g_DrawExt.vParams0.x, fLostVar, fHeight);
  // The wakes (WaterWake): their slopes added to the waves', the variance their mip levels lost to the blur of the
  // reflection and the glint; their foam to the ocean's (its debug views here: kept on they would hold registers).
  float fWakeFoam = 0;
  if (g_DrawExt.vLightPos[1].w != 0)
  {
    const float4 vWake = WaterWake(In.vWorldPos.xy);
    N = normalize(float3(N.xy / N.z - vWake.xy, 1));
    fLostVar += vWake.z;
    fWakeFoam = vWake.w;
    if (g_DrawExt.vParams0.z == 27)
      return float4(saturate(vWake.xy * 2 + 0.5), saturate(sqrt(vWake.z) * 10), 1);
    if (g_DrawExt.vParams0.z == 28)
      return float4(vWake.www, 1);
  }
  if (bBelow)
    N = -N;

  // What the opaque scene left behind the surface: the undisplaced pixel first.
  uint nW, nH;
  g_Textures[g_Draw.nTex0].GetDimensions(nW, nH);
  float2 vInvSize = 1 / float2(nW, nH);
  float2 vUV0 = In.vPos.xy * vInvSize;
  float fSurface = LinearDepth(In.vPos.z);
  const float fRawBehind0 = g_Textures[g_Draw.nTex1].SampleLevel(g_SamplerPointClamp, vUV0, 0).r;
  float fBehind0 = LinearDepth(fRawBehind0);
  float fThick0 = max(fBehind0 - fSurface, 0);

  // Displaced by the waves, less in shallow water and with distance; a displaced sample in front
  // of the surface (the shore, an object) keeps the undisplaced one. The ocean from below bends the
  // world above far more, in large smooth swells: by the same waves four times as large.
  float2 vOffset;
  if (bBelow && g_DrawExt.vParams3.w == 0)
    vOffset = WaveNormal(In.vWorldPos.xy * 0.25, g_Frame.vTime.x * 0.25, g_DrawExt.vParams0.x).xy * 1.5 * saturate(fThick0 * 0.5) /
              (1 + fSurface * 0.1);
  else
    vOffset = N.xy * g_DrawExt.vParams2.w * saturate(fThick0 * 0.5) / (1 + fSurface * 0.05);
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
  float3 vExtinction = g_DrawExt.vParams2.rgb;
  // The water's own colour is light scattered into it: as given (w 0), in proportion to the
  // brightness of what is behind a water volume (w 1), or light the sky scatters into it (w 2):
  // from 60 % under a black sky to all of it under a bright day sky (the sky box's brightness in
  // five directions).
  float3 vScatter = g_DrawExt.vParams3.rgb;
  if (g_DrawExt.vParams3.w == 1)
    vScatter *= saturate(dot(vScene0, float3(0.30, 0.59, 0.11)) * 2.5);
  else if (g_DrawExt.vParams3.w == 2 && g_DrawExt.vParams1.y != 0)
  {
    float3 vSky = SkyBoxColor(float3(0, 0, 1)) + SkyBoxColor(float3(1, 0, 0.3)) + SkyBoxColor(float3(-1, 0, 0.3)) +
                  SkyBoxColor(float3(0, 1, 0.3)) + SkyBoxColor(float3(0, -1, 0.3));
    vScatter *= 0.6 + 0.4 * saturate(dot(vSky, float3(0.30, 0.59, 0.11)) / (5 * 0.6));
  }
  // The foam on the FFT ocean seen from above (d3d12_WaterFoam, vTexGen1U.x; WaterFoam): the share of the pixel white
  // (its textures' derivatives outside the branches).
  float fFoam = 0, fWhitecaps = 0, fShoreFoam = 0, fFoamShade = 1;
  if (g_DrawExt.vTexGen1U.x > 0)
  {
    // On the ocean's grid the still water's depth over the scene behind and the view's path under the level to it (as
    // the flat disc's: the scene behind a raised crest may be above the water, a boat's side, which no wave breaks on;
    // what a crest hides the floor under it, GridBehindZ).
    float fFoamDepth = fDepthBelow, fFoamPath = fThick0 * fDistance / max(fSurface, 1e-3);
    if (g_DrawExt.vLightSpec[0].x != 0)
    {
      // 34: the floor behind against the level's terrain heights (red above them, blue below, 0.5 m full; green where the
      // foam takes the heights, FoamFloorZ).
      if (g_DrawExt.vParams0.z == 34)
      {
        const float d = vFloor.z - (g_DrawExt.vLightSpec[0].y - GridTerrainDepth(vFloor.xy));
        return float4(saturate(d * 2), FoamFloorZ(vFloor) != vFloor.z ? 0.5 : 0, saturate(-d * 2), 1);
      }
      // 35: the open sea's share (white; black on the ponds cut off from it: GridOpenShare, d3d12_WaterFoamPonds).
      if (g_DrawExt.vParams0.z == 35)
        return float4(GridOpenShare(In.vWorldPos.xy).xxx, 1);
      // d3d12_WaterFoamFar (vLightSpec[5].y): no shore there where nothing is drawn behind the surface (the depth buffer's
      // clear) or the floor behind lies off the level's terrain (its heights' map, vLightSpec[3].z > 0): the engine's filler
      // past the map's edge, which rises through the level far out. The shore's foam drew a thin torn line across the open
      // sea where it crosses the level (Training's bay 670-700 m off); the original draws no foam there.
      if (g_DrawExt.vLightSpec[5].y > 0 && (fRawBehind0 >= 1 || (g_DrawExt.vLightSpec[3].z > 0 && GridTerrainDepth(vFloor.xy) >= 1e4)))
        fFoamDepth = 1e4;
      else
        fFoamDepth = max(g_DrawExt.vLightSpec[0].y - GridBehindZ(FoamFloorZ(vFloor), In.vWorldPos), 0);
      fFoamPath = fFoamDepth / max(V.z, 0.01);
    }
    fFoam = WaterFoam(In.vWorldPos.xy, fFoamDepth, fFoamPath, fWakeFoam, fWhitecaps, fShoreFoam, fFoamShade);
    if (bBelow)
      fFoam = 0;
  }
  float3 vColor;
  if (!bBelow)
  {
    float3 vRefracted;
    if (g_DrawExt.vParams3.w == 0)
    {
      // The point of the scene behind along the displaced ray; the ocean's water over it (d3d12_WaterVolume, vAddColor.w),
      // else its fog volume's fog.
      float3 vBehind = vCamera - V * (fDistance * fBehind / max(fSurface, 1e-3));
      // On the ocean's grid what stands over the water behind a crest hidden by it (GridBehindZ).
      if (g_DrawExt.vLightSpec[0].x != 0)
        vBehind.z = GridBehindZ(vBehind.z, In.vWorldPos);
      if (g_DrawExt.vAddColor.w > 0)
      {
        // Its terms' debug views here: kept to the end of the shader they would hold registers through all of it.
        float3 vFloorShare, vInscatter;
        float fVolumeShade;
        vRefracted = WaterEncode(OceanVolume(WaterDecode(vScene), In.vWorldPos, N, V, vBehind, In.vPos.xy, vFloorShare, vInscatter,
                                             fVolumeShade));
        if (g_DrawExt.vParams0.z == 24)
          return float4(WaterEncode(vInscatter), 1);
        if (g_DrawExt.vParams0.z == 25)
          return float4(vFloorShare, 1);
        if (g_DrawExt.vParams0.z == 26)
          return float4(fVolumeShade.xxx, 1);
      }
      else
      {
        float fLayer = g_DrawExt.vParams2.x;
        vRefracted = lerp(vScene, vScatter, OceanFog(fLayer - vBehind.z, vCamera.z - fLayer, fBehind, g_DrawExt.vParams2.y));
      }
    }
    else
    {
      // A WaterVolume material's water times its diffuse colour (vColor rgb; white for the others: d3d12_WaterVolumeColor).
      float3 T = exp(-vExtinction * fPath);
      vRefracted = (vScene * T * exp(-vExtinction * fLightPath) + vScatter * (1 - T)) * g_Draw.vColor.rgb;
    }

    // The reflection, all of it at grazing angles: the ocean as the game's terrain water shaders
    // (a quarter straight down, by the calmer normal; times twice the alpha of the mesh the 3D
    // engine gives, which fades it out in shallow water near the camera), other water by
    // Schlick's term (2 %). The ripple finer than a pixel (its slopes' variance fVar, as
    // d3d12_WaterReflectionBlur scales it) turns facets towards the camera: the cosine as the
    // RMS of theirs.
    float fVar = fLostVar * g_DrawExt.vTexGenU.x;
    float3 Ns = normalize(float3(N.xy * 0.5, N.z));
    float F;
    if (g_DrawExt.vParams3.w != 1)
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
    F *= g_DrawExt.vParams0.y;
    float3 R = reflect(-V, N);
    float3 Rs = reflect(-V, Ns);
    // The sky in the mirror: as the water's reflection camera (the camera mirrored in the surface) sees it.
    float3 vReflected = g_DrawExt.vParams1.y != 0 ? SkySeen(R, 2 * In.vWorldPos.z - g_Frame.vCameraPos.z) : vScatter;
    float3 vPlanar = 0;
    float fBlur = 0;
    if (g_DrawExt.vParams1.w != 0)
    {
      if (fVar > 0)
        vPlanar = BlurredPlanarReflection(In.vWorldPos, Rs, fDistance * 0.3, fVar, saturate(dot(Ns, V)), fBlur);
      else
        vPlanar = PlanarReflection(In.vWorldPos, Rs, fDistance * 0.3);
      vReflected = vPlanar;
    }
    // The screen's copy is sharp: it gives way to the blurred planar reflection where the blur up
    // and down is wider than 4 pixels on the screen, all of it from 8. None where nothing of it
    // would be seen (no reflection, or blurred away: open water away from the camera), but in the
    // debug views.
    const float fSharp = fBlur > 0 ? saturate(2 - fBlur * g_Frame.vScreen.y / 4) : 1;
    const bool bSharp = (F != 0 && fSharp > 0) || g_DrawExt.vParams0.z != 0;
#ifdef XR_RAYTRACE
    float4 vSSR = g_DrawExt.vTexGenV.x >= 2 && bSharp ? ScreenReflection(In.vWorldPos, Rs, g_Frame.vScreen.xy * vInvSize, float2(nW, nH), true) : 0;
#else
    float4 vSSR = bSharp ? ScreenReflection(In.vWorldPos, Rs, g_Frame.vScreen.xy * vInvSize, float2(nW, nH), false) : 0;
#endif
    vSSR.w *= fSharp;
#ifdef XR_RAYTRACE
    // The rays: in place of the screen's reflection (mode 1), or where it finds nothing (2, none where
    // it is sure); as sharp, they give way to the blurred planar reflection as it does.
    // No ray where nothing of it would be seen, but in the debug views.
    float fRayT = g_DrawExt.vTexGenV.y;
    float4 vRay = 0;
    if ((g_DrawExt.vTexGenV.x < 2 || vSSR.w < 1) && ((F > 0 && fSharp > 0) || g_DrawExt.vParams0.z >= 13))
      vRay = RayReflection(In.vWorldPos, Rs, fDistance, fPixelAngle, fVar, fRayT);
    vRay.w *= fSharp;
    vReflected = lerp(vReflected, vRay.rgb, vRay.w);
#endif
    vReflected = lerp(vReflected, vSSR.rgb, vSSR.w);

    // The sun's glint (d3d12_WaterGlint, vSpecular.x; WaterLight.hlsli): the facets that mirror it,
    // the pixel's waves and the slopes about them of the ripple finer than a pixel (the variance
    // fLostVar at full strength, d3d12_WaterReflectionBlur is the reflection's) and of the ripple
    // finer than the texture (d3d12_WaterGlintRoughness, vSpecular.y; the FFT ocean's: Cox and Munk's
    // of the wind less its cascades', vAddColor.y); times the reflection amount,
    // fading out as the sun sets (below 6 degrees), at most the sun's flare (vSpecular.z). The sun's
    // light through the thin crests (d3d12_WaterCrestLight, vParams0.w; WaterCrestLight), what the
    // surface does not reflect of it (1 - F), fading out as the glint. In the sun's shadow
    // (WaterSunShadow, looked up only where there is light to shade) neither.
    const float fGlintVar = fLostVar + (g_DrawExt.vTexGenU.w != 0 ? g_DrawExt.vAddColor.y : g_DrawExt.vSpecular.y);
    const float fSunUp = saturate(L.z * 10);
    float3 vGlint = 0, vCrest = 0;
    if (g_DrawExt.vSpecular.x > 0)
    {
      const float fGlint = WaterGlint(N, V, L, fGlintVar) * g_DrawExt.vSpecular.x * g_DrawExt.vParams0.y * fSunUp;
      vGlint = WaterCapLight(WaterSunLight() * fGlint, g_DrawExt.vSpecular.z);
    }
    if (g_DrawExt.vParams0.w > 0)
      vCrest = WaterSunLight() * WaterCrestColor(g_DrawExt.vParams3.rgb) *
               (WaterCrestLight(N, V, L, fHeight, WaveSlopeRMS(g_DrawExt.vParams0.x)) * g_DrawExt.vParams0.w * (1 - F) * fSunUp);
    // The foam neither mirrors the sun nor lets its light through.
    if (fFoam > 0)
    {
      vGlint *= 1 - fFoam;
      vCrest *= 1 - fFoam;
    }
    float fSunShadow = 1;
    {
#ifdef XR_RAYTRACE
      const float fCone = fDistance * fPixelAngle;
#else
      const float fCone = 0;
#endif
      const float3 vSun = vGlint + vCrest;
      if (max(vSun.r, max(vSun.g, vSun.b)) > 1e-3 || fFoam > 0)
      {
        fSunShadow = WaterSunShadow(In.vWorldPos, fCone);
        vGlint *= fSunShadow;
        vCrest *= fSunShadow;
      }
      else
        vGlint = vCrest = 0;
    }

    // The shore: the surface fades in over the first 15 cm of water, up to 40 m away; beyond 100 m
    // it covers everything (the coarse distant terrain comes close to the surface in places).
    // The glint and the crests' light are light added to the colour's (HdrDecode: the game's colours
    // are gamma encoded).
    float fShore = saturate(fThick0 * 6.6 + saturate((fSurface - 40) / 60));
    vColor = lerp(vRefracted, vReflected, F);
    if (any(vGlint + vCrest > 0))
    {
      const float3 vLight = float3(HdrDecode(vColor.r), HdrDecode(vColor.g), HdrDecode(vColor.b)) + (vGlint + vCrest) * fShore;
      vColor = float3(HdrEncode(vLight.r), HdrEncode(vLight.g), HdrEncode(vLight.b));
    }
    vColor = lerp(vScene0, vColor, fShore);
    // The foam over it, lit by the sun (as the glint fades as it sets) and the sky.
    if (fFoam > 0)
    {
      const float3 vFoam = FoamLight(N, L, WaterSunLight() * fSunUp, fSunShadow, WaterSkyLight()) * fFoamShade;
      vColor = lerp(vColor, float3(HdrEncode(vFoam.r), HdrEncode(vFoam.g), HdrEncode(vFoam.b)), fFoam);
    }
    if (g_DrawExt.vParams0.z == 1)
      return float4(vRefracted, 1);
    if (g_DrawExt.vParams0.z == 2)
      return float4(vReflected, 1);
    if (g_DrawExt.vParams0.z == 3)
      return float4(saturate(fThick0 / 2).xxx, 1);
    if (g_DrawExt.vParams0.z == 4)
      return float4(N * 0.5 + 0.5, 1);
    if (g_DrawExt.vParams0.z == 5)
      return float4(vSSR.www, 1);
    if (g_DrawExt.vParams0.z == 6)
      return float4(vSSR.rgb, 1);
    if (g_DrawExt.vParams0.z == 7)
      return float4(vPlanar, 1);
    if (g_DrawExt.vParams0.z == 8)
      return float4(vScene0, 1);
    if (g_DrawExt.vParams0.z == 9)
      return float4(saturate(fPath / 32), saturate(fDepthBelow / 16), F, 1);
    if (g_DrawExt.vParams0.z == 10)
      return float4(In.vColor.rgb, 1);
    if (g_DrawExt.vParams0.z == 11)
      return float4(In.vColor.aaa, 1);
    if (g_DrawExt.vParams0.z == 12)
      return float4(saturate(sqrt(fLostVar) * 10).xxx, 1);
    if (g_DrawExt.vParams0.z == 16)
      return float4(HdrEncode(vGlint.r), HdrEncode(vGlint.g), HdrEncode(vGlint.b), 1);
    if (g_DrawExt.vParams0.z == 17)
      return float4(HdrEncode(vCrest.r), HdrEncode(vCrest.g), HdrEncode(vCrest.b), 1);
    if (g_DrawExt.vParams0.z == 18)
      return float4(saturate(fGlintVar / 0.05).xxx, 1);
    if (g_DrawExt.vParams0.z == 20)
      return float4(saturate(fHeight * 0.25 + 0.5).xxx, 1);
    if (g_DrawExt.vParams0.z == 21)
      return float4(fFoam.xxx, 1);
    if (g_DrawExt.vParams0.z == 22)
      return float4(fWhitecaps.xxx, 1);
    if (g_DrawExt.vParams0.z == 23)
      return float4(fShoreFoam.xxx, 1);
    if (g_DrawExt.vParams0.z == 19)
    {
#ifdef XR_RAYTRACE
      return float4(WaterSunShadow(In.vWorldPos, fDistance * fPixelAngle).xxx, 1);
#else
      return float4(WaterSunShadow(In.vWorldPos, 0).xxx, 1);
#endif
    }
#ifdef XR_RAYTRACE
    if (g_DrawExt.vParams0.z == 13)
      return float4(vRay.rgb, 1);
    if (g_DrawExt.vParams0.z == 14)
      return float4(fRayT < g_DrawExt.vTexGenV.y ? saturate(float3(2 * fRayT / g_DrawExt.vTexGenV.y - 1, 1 - abs(2 * fRayT / g_DrawExt.vTexGenV.y - 1),
                                                               1 - 2 * fRayT / g_DrawExt.vTexGenV.y)) : 0, 1);
    if (g_DrawExt.vParams0.z == 15)
      return float4(vRay.www, 1);
#endif
  }
  else if (g_DrawExt.vParams3.w == 0)
  {
    // The ocean from below: the scene above through the waves, in the fog of its volume. With d3d12_WaterVolume
    // (vAddColor.w) through Snell's window: the light from the air that passes the surface (SnellWindow at the waves'
    // normal; none beyond the critical angle, where the surface mirrors the water below), the rest the water's colour;
    // else as the game's ocean shows it, without the window, a third of it the water's colour the surface reflects
    // back down.
    if (g_DrawExt.vAddColor.w > 0)
    {
      const float fWindow = SnellWindow(dot(N, V));
      float3 vAbove = vScatter;
      if (fWindow > 0)
        vAbove = SceneAbove(In.vWorldPos, refract(-V, N, VOLUME_ETA), g_Frame.vScreen.xy * vInvSize);
      // With d3d12_WaterUnder (w 2) the surface's own light, the water's between it and the camera Underwater.hlsl's; with
      // d3d12_WaterUnderClear (w 3) beyond the window the light the water below sends up along the mirrored ray (the light
      // it scatters a metre at the surface from the scene's sun and sky vUnderColor rgb, Underwater.hlsl's; the flat
      // surface's mirror, as UnderSurface's), the same as the view's water at the horizon.
      if (g_DrawExt.vAddColor.w > 2.5)
      {
        const float3 vDeep = WaterDecode(vScatter), c = VolumeAttenuation(vDeep, g_DrawExt.vParams2.y), R = float3(-V.xy, V.z);
        const float fShare = g_DrawExt.vAddColor.z;
        const float3 vBelow = UnderClearBelow(g_DrawExt.vUnderColor.rgb, c, saturate(-V.z), 1 - fShare + fShare * UnderSunPhase(g_Frame.vSunDir.xyz, R));
        vColor = WaterEncode(lerp(vBelow, WaterDecode(vAbove), fWindow));
      }
      else if (g_DrawExt.vAddColor.w > 1.5)
        vColor = WaterEncode(UnderSurface(WaterDecode(vScatter), WaterDecode(vAbove), fWindow, saturate(-V.z)));
      else
        vColor = lerp(WaterEncode(lerp(WaterDecode(vScatter), WaterDecode(vAbove), fWindow)), vScatter,
                      FogInsideVolume(fSurface, g_DrawExt.vParams2.y));
      if (g_DrawExt.vParams0.z == 8)
        return float4(vAbove, 1);
      if (g_DrawExt.vParams0.z == 25)
        return float4(fWindow.xxx, 1);
    }
    else
      vColor = lerp(vScene, vScatter, 1 - 0.65 * (1 - FogInsideVolume(fSurface, g_DrawExt.vParams2.y)));
    if (g_DrawExt.vParams0.z == 8)
      return float4(vScene, 1);
  }
  else
  {
    // Other water from below: the scene above inside the window (critical angle of water, cos
    // 0.66), the water's colour outside it.
    float fWindow = saturate((dot(N, V) - 0.66) * 8);
    vColor = lerp(vScatter, vScene * g_Draw.vColor.rgb, fWindow);
  }

  if (HasFlag2(DF2_HEAT))
    vColor = HeatVision(vColor, 1);
  // The scene's fog, not on the ocean seen from below: that view stays in the fog of its volume.
  if (g_Frame.vFogParams.w != 0 && !(bBelow && g_DrawExt.vParams3.w == 0))
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

#ifdef XR_WATER_DEFERRED
// The deferred ocean (XR_WATER_DEFERRED; OceanGrid.cpp's DrawOceanGrid): after the grid's depth pass (the waves' depth in
// the depth buffer, no pixel shader) the water program over the whole view, its pixels where the depth buffer holds a
// nearer surface than the scene's copy (nTex1): the waves'. Each of the water's pixels is shaded once, where the grid's
// triangles, a few pixels across, shaded the quads along their edges twice (at 4K the water 0.4 ms more on Carrier).
// vLightPos[2..4] xyz the view's rays (the world position from the view's x, y and depth, CD3D12Renderer::GetRayView's),
// vLightPos[2].w (as uint) the depth buffer.

struct SDeferredOutput
{
  float4 vPos : SV_Position;
  float2 vNDC : TEXCOORD0;      //!< the view's x and y (-1..1, y up)
};

//! The triangle over the view (OceanGrid.cpp: its vertices' xy the view's x and y) at the farthest water's depth
//! (vLightPos[3].w; the test GREATER: nothing of it over what is further off).
SDeferredOutput VSDeferred(SVertexInput In)
{
  SDeferredOutput Out;
  Out.vPos = float4(In.vPos.xy, g_DrawExt.vLightPos[3].w, 1);
  Out.vNDC = In.vPos.xy;
  return Out;
}

//! The depth buffer's value at pixel p (0 off it).
float DeferredDepth(int2 p)
{
  return g_Textures[asuint(g_DrawExt.vLightPos[2].w)].Load(int3(p, 0)).r;
}

//! The world position at the view's vNDC and view depth d (the upscaler's jitter, which moved the grid's vertices, taken off).
float3 DeferredPos(float2 vNDC, float d)
{
  const float2 v = vNDC - g_Frame.vJitter.xy;
  return g_Frame.vCameraPos.xyz + d * (g_DrawExt.vLightPos[4].xyz + v.x * g_DrawExt.vLightPos[2].xyz + v.y * g_DrawExt.vLightPos[3].xyz);
}

//! What the grid's vertex program would have given the pixel (fDepth its depth): the world position along its view ray
//! (the textures' mip levels from its derivatives across the quad: where a quad holds a crest and the water behind it, or
//! the water and what stands in front of it, a level too coarse; the frames those of the grid shaded as it is drawn but
//! for single pixels along such edges, the neighbour nearer in depth's derivatives with SampleGrad cost 0.06-0.15 ms
//! at 4K); the vertex alpha the game's disc's there (GridDiscAlphaTable, bilinear as the forward grid's linear between
//! its vertices); the clip positions of the still surface by the grid's matrices (its vertices across the water from
//! mWorld's translation).
SWaterOutput DeferredWater(SDeferredOutput In, float fDepth)
{
  const float3 P = DeferredPos(In.vNDC, LinearDepth(fDepth));
  SWaterOutput Out;
  Out.vPos = float4(In.vPos.xy, fDepth, 1);
  Out.vWorldPos = P;
  const uint nRings = (uint)g_DrawExt.vLightSpec[4].z, nGridRings = (uint)g_DrawExt.vLightSpec[4].w;
  Out.vColor = float4(1, 1, 1, GridDiscAlphaTable(P.xy - g_DrawExt.vLightSpec[1].xy, nRings * (1 + XR_GRID_DISC_SEGMENTS) + nGridRings,
                                                  g_DrawExt.vLightSpec[2].w));
  Out.vTex0 = 0;
  Out.vGrid = float4(0, 1, 1, 1);
#ifdef XR_VELOCITY
  const float4 vLocal = float4(P - g_Draw.mWorld[3].xyz, 1);
  Out.vCurClip = mul(vLocal, g_Draw.mWorldViewProj);
  Out.vPrevClip = mul(vLocal, g_Draw.mPrevWorldViewProj);
#endif
  return Out;
}

//! The deferred ocean's pixel: none of a quad without the waves (its pixels' quads shaded together, the derivatives
//! within them kept), the pixels of a quad with them shaded, those without them discarded after.
#ifdef XR_VELOCITY
[earlydepthstencil]
SPixelOutput PSDeferred(SDeferredOutput In)
#else
[earlydepthstencil]
float4 PSDeferred(SDeferredOutput In) : SV_Target
#endif
{
  const int2 p = (int2)In.vPos.xy;
  const float fDepth = DeferredDepth(p);
  const bool bWater = fDepth < g_Textures[g_Draw.nTex1].Load(int3(p, 0)).r;
  [branch]
  if (!FoamQuadAny(bWater))
  {
    discard;
#ifdef XR_VELOCITY
    return (SPixelOutput)0;
#else
    return 0;
#endif
  }
  const SWaterOutput W = DeferredWater(In, fDepth);
#ifdef XR_VELOCITY
  SPixelOutput Out;
  Out.vColor = SceneOutput(ShadeWater(W));
  Out.vMotion = MotionVector(W.vCurClip, W.vPrevClip);
  Out.fDepth = fDepth;
  if (!bWater)
    discard;
  return Out;
#else
  const float4 vColor = SceneOutput(ShadeWater(W));
  if (!bWater)
    discard;
  return vColor;
#endif
}
#endif
