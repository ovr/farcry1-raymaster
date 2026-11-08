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
//	      + tex4 * vAddColor.rgb, both at uv0; DF2_CUBE_SHADER: as the original's
//	      shader of the template, by the mode vParams3.w, CubeShader), else
//	      DF_TEX0_ARRAY
//	      (g_TextureArrays, slice floor(u / 2) at (u - 2 * slice, v), or two
//	      slices, one dissolved into the other, ArrayCoords.hlsli), else DF_TEX0
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
//	      tex2 at uv1, rgb * 2 - 1; with g_Frame.vLightmap.x, d3d12_LightmapDot3,
//	      tex1 * lerp(1, saturate(D.z), tex1 a) * D.a as the original's),
//	      DF_TEX1_DETAIL rgb = 2 * rgb + tex1 - 0.5, else *= tex1
//	    DF2_ALPHA_GLOW: rgb += tex0 rgb * tex0 a (the texture's alpha its
//	      glow, unlit)
//	    DF2_AMBIENT_CUBE: rgb += 0.25 * (the cube g_Cubes[nTex6] at the view
//	      vector reflected about the vertex normal - tex0 * vColor * vAmbient):
//	      a quarter of the ambient light from the cube, unlit
//	    DF_TERRAIN_CLIFF (terrain with cliffs): rgb = 2 * rgb * lerp(1, 2 * tex2,
//	      w.x) * lerp(1, 2 * tex3, w.y), w = vertex colour rg * vTexGen1V.xy, uv
//	      of tex2 from the world position with vParams0 / 1, of tex3 with 2 / 3
//	    DF_DECAL_FADE (decals): a *= 1 - f(r), r = 2 * |uv0 - 0.5| (1 on the circle
//	      inside the decal's square), f(r) = (r + smoothstep(0, 1, r)) / 2
//	    DF2_GLOW_FALLOFF (procedural flares): rgb *= saturate(1 - u)^vParams0.x,
//	      u of uv0 0 on the glow's inner edge, 1 on its outer edge
//	    DF2_SKY_GLOW (the sky with r_HDRRendering; this and DF2_HDR_CORE only with
//	      XR_HDR, the programs for D3D12_HDR_FORMAT): rgb *= SkyGlow (Common.hlsli)
//	      in the direction of the world position (the sky's are around the camera)
//	    DF2_HDR_CORE (a light added with r_HDRRendering): rgb *= HdrEncode(1 +
//	      (vHdrCore.x - 1) * m^HDR_CORE_POWER), the factor that makes white's
//	      light that much brighter (Common.hlsli), m the light's shape: the
//	      largest channel of tex0 (times its alpha with vHdrCore.y), times the
//	      fall-off of DF2_GLOW_FALLOFF. Its core goes above white, its soft edge
//	      stays as the game's, whatever its colour and fade
//	    DF_ALPHATEST: discard when a < fAlphaRef (DF_ALPHATEST_LESS: when a >= fAlphaRef);
//	      a negative fAlphaRef (alpha to coverage with MSAA): a = CoverageAlpha
//	      (AlphaCoverage.hlsli) at -fAlphaRef, the samples it covers in place of the test;
//	      then DF_ALPHA_ONE: a = 1
//	    DF_DETAIL_LAYER (a terrain detail layer, multiplied in twice; its vertices 5 cm
//	      out along the normal, its clip w + 0.005, as the original's): rgb =
//	      lerp(0.5, tex0, a * the fade by the distance), a = 1; DF_DETAIL_BUMP
//	      (bump mapped layer) first scales tex0 by 1 + (sun N'.L - sun N.L) /
//	      (vParams1.x + saturate(N.L)), clamped to 0..2: N the vertex normal, N'
//	      the normal map (tex1 at uv0) in its frame (tangent along vTexGenU made
//	      orthogonal to N, binormal along vTexGenV, the map's y times vParams1.y)
//	    DF_SHADOWS: the sun's visibility through the draw's shadow maps
//	      (SunVisibility, Common.hlsli) v: DF_LIGHTING colour = lerp(the vertex
//	      light without the sun, with it, v) * texture; DF_TEX1_LIGHTMAP rgb *=
//	      4 * v * tex1 + vAmbient; DF_SHADOW_PASS rgb = colour * tex0, a = 1 - v
//	    DF2_SUN_CASCADES: v from the sun's cascades where they reach
//	      (SunCascades, Common.hlsli; the live casters' with DF2_CASCADES_LIVE),
//	      the shadow maps' beyond them; DF2_SHADOW_TINT (the terrain): rgb *=
//	      lerp(vShadowTint, 1, v) after the terrain's cliff textures; with
//	      DF_LIGHTING (a lit object in the full set's shadow) the light without
//	      the sun becomes the larger of it and the light with the sun times
//	      vShadowTint (and so its ambient part): as dark as the original's
//	      shadows on the terrain, where the game's light would leave the
//	      ambient colour alone; under XR_PT the G-buffer's ambient light so
//	      in the shadow instead (PSMain). Only in
//	      the pixel shaders with XR_CASCADES (Shaders.cmake), which the draws
//	      with DF2_SUN_CASCADES or DF2_CASCADE_CASTER get: the others' code is
//	      the same as without the cascades. With XR_RAY_SUN too (the main
//	      view's opaque list with d3d12_RTSunShadows, RaySun.cpp): v from the
//	      mask of the sun's shadow by rays where it is of the pixel's surface
//	      (its view depth the pixel's: the depth prepass's surfaces; RaySunMask),
//	      giving way to the cascades over the end of its range (DF_SHADOW_PASS:
//	      1 there, as where the cascades reach); d3d12_ShadowDebug 4 shows it
//	      as the cascades' with blue where it counts
//	    DF_PIXEL_LIGHTING: DF_LIGHTING is lit here instead of per vertex
//	      (DrawLighting: the same sum, but the lights' colours clamped to 1 and
//	      point lights falling off as saturate(1 - d / radius); on the terrain
//	      (DF2_TERRAIN_LIGHT) but for a projector as saturate(1 - (d /
//	      radius)^2); DF2_VERTEX_LIGHTS: the lights as per vertex,
//	      DF2_SMOOTH_FALLOFF falling off as 2 (1 - smoothstep(0, 0.9, d /
//	      radius))), with the
//	      vertex normal or, with DF_BUMP (bump mapping), N' = the normal map
//	      (tex3 at uv0, its y times vParams1.y) in the vertex's tangent frame;
//	      the dot3 light map takes N'.D for D.z
//	    nParallax.x non-zero (r_Parallax, a lit opaque surface with the tangent
//	      frames): uv0 of tex0, tex3 and tex4 (DF2_TEX4_LAYER, DF2_ADD_TEX4)
//	      is where the view meets the height g_Textures[nParallax.x].a
//	      (sampler nParallax.y; Parallax.hlsli, at most nParallax.z steps),
//	      vParallax.x deep in uv0 from 0 (a hollow) to 1 (a bump), the height
//	      vParallax.w not offset, fading from vParallax.y pixels of depth to
//	      none at vParallax.z; those textures read by uv0's gradients (no
//	      seams where the offset jumps), the march's height with nParallax.w
//	      (r_ParallaxFilter) by them at least nParallax.w texels and the
//	      march's step long (ParallaxMarchGrad); the depth, the light map,
//	      the decal fade and the glow falloff the geometry's
//	    DF_SPECULAR (with DF_PIXEL_LIGHTING): rgb += 2 * vSpecular.rgb * gloss
//	      (texture alpha if vSpecular.w, else 1) * the sum over the sun (times
//	      v) and the lights of their specular colour (clamped to 1) * falloff
//	      * saturate(R.V)^32, R the light reflected about the normal; with
//	      nPbr.x non-zero (r_PbrRoughness) the lobe by the roughness of
//	      g_Textures[nPbr.x].r at uv0 (sampler nPbr.y; PbrSpecular.hlsli):
//	      saturate(R.V)^n * (n + 1) / 33, widened by the normal's change
//	      across the pixel
//	    vSkin.x > 0 (r_Skin, a character's face or head lit per pixel;
//	      Skin.hlsli): N.L of the sun and the lights SkinWrap's (vSkin.w the
//	      scatter), and with DF_SPECULAR rgb += vAmbient.w * vSpecular.rgb *
//	      SkinSpecularShare(gloss) * the sum of the lights' specular colours *
//	      falloff * SkinSpecular (GGX, F0 0.028) of the roughness
//	      SkinRoughness(gloss, vSkin.y, vSkin.z), widened by the normal's
//	      change across the pixel, in place of the templates' highlight
//	    DF2_ADD_TEX4: rgb += tex4 at uv0 + (vTexMat0.w, vTexMat1.w) * vAddColor.rgb
//	      + vAddColor.w (a glow map or a texture added, unlit); with DF2_GLASS
//	      also a *= tex4 a
//	    DF2_TEX4_LAYER: the texture is tex4 (there) over tex0 by tex4's alpha,
//	      before the light (a second layer moving on its own); tex0 + tex4 *
//	      vAddColor.x with vAddColor.x > 0
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
//	      fading in over the first metre of depth; with vParams2.x > 0 the FFT
//	      ocean's (d3d12_WaterCaustics, WaterCaustics.hlsli): the product of
//	      the caustics cascades' light at the depth (the array nTex1, slice i
//	      at that point times vParams2[i] (1 / its tile), its first depth
//	      vParams3[i]; the sun's disc's blur; its mean where a pixel takes
//	      more than its last mip level's texel), rgb = 0.5 * clamp(1 + k
//	      (I - 1), 0, vParams1.w), k = vParams0.w times the sun's visibility
//	      (DF2_SUN_CASCADES: one tap, CascadeTapNormal), fading out to
//	      vParams1.z m of depth
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
//	  DF2_LIGHTMAP_LIGHTS (with DF_TEX1_LIGHTMAP, without DF_LIGHTING): the
//	  draw's lights without the sun (DrawLighting per pixel with
//	  DF2_VERTEX_LIGHTS, the normal as for DF_PIXEL_LIGHTING) take vAmbient's
//	  place under the light map: rgb *= 4 * v * tex1 + those lights. With
//	  DF_SPECULAR (d3d12_LightmapSpecular, the specular bump templates over a
//	  dot3 light map) the lamps baked into it among them (XR_LIGHT_LIGHTMAP_LAMP) add their
//	  highlights after that, as the original's light-map bump shaders: rgb +=
//	  2 * vSpecular.rgb * gloss * their specular colour * LightmapLampFalloff(d
//	  / radius) * saturate(R.V)^32; nothing else of them.
//	  DF2_SHADER_FX (the glass of a scope, a mirror, heat haze, the templates
//	  of special lighting) replaces the pixel shader with ShaderFx; the fur's
//	  shells and the bubble also move the vertices out along the normal.
//	The upscaler's jitter (vJitter) moves every clip position. Compiled with
//	XR_VELOCITY (the draws into an upscaler's scene that write its motion
//	vectors or its depth) the pixel shader also writes the motion vector of
//	the pixel (MotionVector) from mWorldViewProj and mPrevWorldViewProj to
//	SV_Target1 and its depth to SV_Target2 (the pipeline masks what the draw
//	does not write).
//	Compiled with XR_AMBIENT (the main view's opaque list while the ambient
//	occlusion's target is bound, d3d12_AOApply 1) it also writes to
//	SV_Target3 A, how much of the colour is the ambient light's (rgb, in the
//	colour's units), what the ambient occlusion takes away
//	(AmbientOcclusion.cpp). The pipeline blends it as the colour, so each
//	pass adds or keeps its own: the ambient part of the light (per pixel
//	DrawLighting's sums with the ambient colour less those without it; per
//	vertex about min(vColor * ambient, the light); MaterialLighting's
//	diffuse less the lights alone; under a light map the colour times the
//	ambient colour, with DF2_LIGHTMAP_LIGHTS the lights' ambient part),
//	times what multiplies the colour after it, not what is added to it (the
//	highlights, DF2_ADD_TEX4, DF2_ALPHA_GLOW); DF2_AO_BAKED the colour
//	before DF2_SHADOW_TINT times vMipBias.yzw, DF2_AO_ALL all of it; at most
//	the colour, times the fog's share of it. A pass that multiplies the
//	picture (DF_DETAIL_LAYER, DF_CAUSTICS, DF2_MODULATE) writes its colour,
//	the factor; the rest 0 (the unlit, the light passes, the fog volumes,
//	the scope's glass). Without an upscaler SV_Target1 and 2 are null
//	targets.
//	Compiled with XR_CASCADE_INSTANCES (the vertex shader of the full set of
//	the sun's cascades, Cascades.cpp) instance i is drawn with mShadow[i] in
//	place of mWorldViewProj into slice nShadowTex[i] of the array
//	(SV_RenderTargetArrayIndex).
//	Compiled with XR_RAY_REFL (Shader Model 6.5, the main view's draws with
//	DF_CUBE0 while d3d12_RTReflections and d3d12_RTReflectionSurfaces are on,
//	SPipelineKey::bRayTrace) DF_CUBE0 traces a ray where it looks the cube
//	up (SurfaceRay, RayShade.hlsli): the reflected one and DF2_GLASS's one
//	through, from the pixel's point a little off its face, as long as
//	g_Frame.nRaySun.y (d3d12_RTDistance), among the instances of the mask
//	nRaySun.z's low bits (RayMask: bit 8 the sun's shadow at the hits); what
//	it hits, shaded and fogged by its path, takes the cube's place, which
//	stays where it hits nothing (the sky, the far) or the ocean first (not in
//	the scene for rays: from above the ocean's level, nRaySun.w, a ray down
//	ends there) and takes over again over the last quarter of its length.
//	nRaySun.z >> 16 & 0xf 2 tints the hits red, the misses blue, 0 traces
//	none (the cube alone: a draw here for its lamps). The metals and smooth
//	materials of the RayMaster maps (nPbr.z, d3d12_RTPbrReflections) trace
//	a ray a pixel by their roughness's lobe (PbrSpecular.hlsli), or one for
//	the 2 x 2 pixels of a quad where their reflections go the same way
//	(nRaySun.z's bit 26, d3d12_RTPbrQuad; SV_Coverage: no helper lane),
//	their hits lit as the last frame's picture saw them where it did
//	(d3d12_RTPbrScreen, nPrevScene; tinted green in the debug mode); their
//	light passes (nPbr.w's XR_PBR_LIGHT_PASS, in any program) none, a
//	metal's light as much less as the first pass's diffuse.
//	Compiled with XR_RAY_LIGHT too (the same programs, the main view's draws
//	lit by lamps while d3d12_RTLightShadows is on) DrawLighting and
//	MaterialLighting in the pixel shader trace a ray from the pixel's point
//	towards each point light that lights it (TraceLight), up to nRaySun.z >>
//	28 of them a pixel, and leave out the light of those it does not reach:
//	the lamps' hard shadows, ending nRaySun.z >> 9 & 127 times 2 cm short of
//	the lamp, or the light's vLightColor.w where that is more (a projector:
//	its picture draws its fitting's shade). nRaySun.z >> 24 & 3: 0 none, 2
//	the light taken away red, 3 the light taken away green where the ray
//	hits next to the surface to red next to the lamp.
//	Compiled with XR_PT (the main view's opaque list into the path tracer's
//	G-buffer, PathGBuffer.hlsli; the HDR scene targets) the pixel shader
//	also writes the albedo to SV_Target3, the normal, the fog's share and
//	the path material to SV_Target4 and the sun's visibility (the shadow
//	maps', the cascades', the mask by rays; with XR_CASCADES and XR_RAY_SUN
//	as the draw has them) and the game's ambient light on it (vAmbient.rgb,
//	which Draw3D.cpp sets so, plus 2 * colour * N.L of each directional
//	light of the draw: a lit object's fill lights, SetPathFillLights) to
//	SV_Target5. A surface the path tracer lights
//	(DF2_PT_MASK; its light flags are off, its colour its albedo times
//	vColor) writes its colour less what it adds unlit (DF2_ADD_TEX4, times
//	tex1, and DF2_ALPHA_GLOW) as the albedo, that as its colour, and its
//	highlights' colour to SV_Target6 (half of the raster's 2 * vSpecular *
//	gloss: vSpecular.rgb, times texture 0's alpha where vSpecular.w > 0,
//	which Draw3D.cpp sets so); a pass that multiplies the picture its
//	factor (unfogged) to the albedo, the highlights' colour and the colour;
//	the rest no albedo and no highlights. Such a surface with its light map
//	(DF_TEX1_LIGHTMAP, which Draw3D.cpp leaves on with d3d12_PTLightMaps;
//	a dot3 one with its direction map's share) writes the light map's
//	light / 4 to SV_Target7 instead of multiplying its colour by it: the
//	path tracer takes what the map lights beyond its own lamps
//	(PathLightmapFill, PathShade.hlsli). Skin (vSkin.x > 0, r_Skin) writes
//	its highlights' colour times SkinSpecularShare(gloss) and their
//	roughness to SV_Target7's alpha (PathGBufferSkin; 1 elsewhere).
//
//////////////////////////////////////////////////////////////////////

#include "Common.hlsli"
#include "LightmapLight.hlsli"
#include "WaterCaustics.hlsli"
#include "Underwater.hlsli"
#include "AlphaCoverage.hlsli"
// r_Parallax's height: the RayMaster material map's alpha (Draw3D.cpp's nParallax).
#define PARALLAX_HEIGHT(uv, dx, dy) SampleTextureGrad(g_DrawExt.nParallax.x, g_DrawExt.nParallax.y, uv, dx, dy).a
#include "Parallax.hlsli"
#include "PbrSpecular.hlsli"
#include "Skin.hlsli"
#if defined(XR_RAY_REFL) || defined(XR_RAY_LIGHT)
#include "RayShade.hlsli"
#include "RayFace.hlsli"
#endif
#ifdef XR_PT
#include "PathGBuffer.hlsli"
#endif

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
#ifdef XR_CASCADE_INSTANCES
  uint nSlice : SV_RenderTargetArrayIndex;  //!< the slice of the cascades' array (last: the pixel shaders' inputs stay where they are)
#endif
};

#ifdef XR_AMBIENT
//! The ambient colour's part of DrawLighting's last light and of its vNoSunLight, or of
//! MaterialLighting's last vDiffuse (s_vAmbientLight); A of ShadePixel.
static float3 s_vAmbientLight, s_vAmbientNoSun, s_vAmbient;
#endif
#ifdef XR_PT
//! The path tracer's G-buffer of the pixel (PathGBuffer.hlsli): what the colour adds unlit, the albedo, the normal
//! (N' with DF_BUMP), the side of its triangle the camera sees, the highlights' colour / 2, the fog's share, the sun's
//! visibility, the light map's light / 4, the skin (the light map target's alpha, PathGBufferSkin; 1 not skin).
static float3 s_vPTEmission, s_vPTAlbedo, s_vPTNormal, s_vPTFace, s_vPTSpecular, s_vPTLightmap;
static float s_fPTFog, s_fPTSun, s_fPTSkin;
#endif

#ifdef XR_RAY_LIGHT
//! The pixel shader's rays towards the lamps (the vertex shader's lighting has none): their origin's
//! face, their cone's width there, how many more there may be.
static bool s_bRayLights = false;
static float3 s_vRayFace, s_vRayNormal;
static float s_fRayWidth;
static uint s_nRayLightsLeft;

//! Whether a point light fDistance away along vToLight reaches vPos (TraceLight; 1 past the pixel's
//! rays), its ray ending the frame's gap short of it, or fMinGap where that is more (a projector's,
//! vLightColor.w); fHit: where its ray hits, of the way to the lamp.
float LightVisible(float3 vPos, float3 vToLight, float fDistance, float fMinGap, out float fHit)
{
  fHit = 1;
  if (!s_bRayLights || s_nRayLightsLeft == 0)
    return 1;
  s_nRayLightsLeft--;
  const float fGap = max(((g_Frame.nRaySun.z >> 9) & 127) * 0.02, fMinGap);
  // d3d12_RTFaceNormal 1 (nRaySun.z's bit 21): where the face is turned from the lamp and the vertices' normal is not (a
  // coarse mesh's triangle by the line where the light passes it, a head's), from off the surface along that normal: a
  // ray from behind the face met the head itself, a hard shadow cut by the triangles (a thin surface lit from behind,
  // its normal turned from the lamp too, as before). Skin's light reaches past that line (SkinWrap, and by its normal
  // map's normal): all of its rays (skin is on closed heads; 2 cm along the normal a ray towards the lamp passes the
  // head's curve there).
  const bool bSmooth = (g_Frame.nRaySun.z & (1u << 21)) && dot(s_vRayFace, vToLight) < 0 &&
                       (g_DrawExt.vSkin.x > 0 || dot(s_vRayNormal, vToLight) > 0);
  return TraceLight(vPos, bSmooth ? s_vRayNormal : s_vRayFace, vToLight, fDistance, fGap, g_Frame.nRaySun.z & 0xff, s_fRayWidth, fHit, 4);
}
#endif

//! The light of the sun, the ambient colour and the draw's lights at world position vPos with the
//! normalised normal vN, which multiplies the colour (DF_LIGHTING): s * (saturate(ambient / s +
//! sun N.L + light 0) + the sum of saturate(light) of the other lights), light = colour * N.L *
//! attenuation, s = vAmbient.w (2: a texture modulated by 2, 1: modulated), with the sun;
//! vNoSunLight the same without it. Per vertex a point light attenuates by 1 / (a0 + a1 d) up to
//! its range; per pixel (bPerPixel, DF_BUMP) by saturate(1 - d / radius), and each light's colour
//! is clamped to 1; per pixel with DF2_VERTEX_LIGHTS as per vertex, with DF2_SMOOTH_FALLOFF by
//! 2 (1 - smoothstep(0, 0.9, d / radius)), the falloff the per vertex attenuation of a static
//! object is fitted to.
//! r_PbrRoughness (nPbr.x): the highlights' lobe by the RayMaster roughness, x its power of saturate(R.V), y its height
//! (PbrSpecular.hlsli's PbrSpecLobe); x 0: the templates' lobe.
static float2 s_vSpecLobe = 0;
//! r_Skin (vSkin.x > 0, ShadePixel): alpha^2 of the skin's highlights (Skin.hlsli's SkinSpecular), 0 not skin.
static float s_fSkinAlpha2 = 0;
#ifdef XR_RAY_REFL
//! The pixel's coverage (SV_Coverage, PSMain): 0 a helper lane (the quad's lanes outside the triangle, for the
//! derivatives), not to trace the quad's ray of d3d12_RTPbrQuad (its point off the surface).
static uint s_nCoverage = 1;
#define XR_PS_INPUT SVertexOutput In, uint nCoverage : SV_Coverage
#define XR_PS_COVERAGE s_nCoverage = nCoverage;
#else
#define XR_PS_INPUT SVertexOutput In
#define XR_PS_COVERAGE
#endif

//! The specular power of a light: saturate(R.V)^32, R = L reflected about N, 0 where N.L <= 0 (s_vSpecLobe's where it
//! has one).
float Specular(float3 vN, float3 vToLight, float3 vToEye)
{
  float fNL = dot(vN, vToLight);
  float fRV = saturate(dot(2 * fNL * vN - vToLight, vToEye));
  if (s_vSpecLobe.x > 0)
    return fNL > 0 ? pow(fRV, s_vSpecLobe.x) * s_vSpecLobe.y : 0;
  float f2 = fRV * fRV, f4 = f2 * f2, f8 = f4 * f4, f16 = f8 * f8;
  return fNL > 0 ? f16 * f16 : 0;
}

//! A light's highlight as DrawLightingT takes it: Specular's, on skin (SKIN, r_Skin) SkinSpecular's of s_fSkinAlpha2.
template<bool SKIN> float LightSpecular(float3 vN, float3 vToLight, float3 vToEye)
{
  return SKIN ? SkinSpecular(vN, vToLight, vToEye, s_fSkinAlpha2) : Specular(vN, vToLight, vToEye);
}

//! DrawLighting; SKIN (r_Skin per pixel, ShadePixel): N.L of the sun and the lights SkinWrap's (vSkin.w the scatter),
//! the highlights SkinSpecular's. A template: without SKIN it compiles as it did before skin.
template<bool SKIN>
float3 DrawLightingT(float3 vPos, float3 vN, bool bPerPixel, out float3 vNoSunLight, out float3 vSunSpec, out float3 vOtherSpec)
{
  float fScale = g_DrawExt.vAmbient.w;
  bool bSun = !HasFlag2(DF2_NO_SUN);
  float3 vLight = g_DrawExt.vAmbient.rgb / fScale + (bSun ? g_Frame.vSunColor.rgb * saturate(dot(vN, g_Frame.vSunDir.xyz)) : 0);
  if (SKIN && bSun)
    vLight = g_DrawExt.vAmbient.rgb / fScale + g_Frame.vSunColor.rgb * SkinWrap(dot(vN, g_Frame.vSunDir.xyz), g_DrawExt.vSkin.w);
  float3 vNoSun = g_DrawExt.vAmbient.rgb / fScale;
  float3 vOthers = 0;
  // With a light map (DF_TEX1_LIGHTMAP) the highlights are its lamps' alone (XR_LIGHT_LIGHTMAP_LAMP, below).
  bool bSpecular = bPerPixel && HasFlag(DF_SPECULAR) && !HasFlag(DF_TEX1_LIGHTMAP);
  // The per pixel lights of the bump templates; DF2_VERTEX_LIGHTS: those of the vertex lighting.
  bool bClamp = bPerPixel && !HasFlag2(DF2_VERTEX_LIGHTS);
  float3 vToEye = SafeNormalize(g_Frame.vCameraPos.xyz - vPos);
  vSunSpec = bSpecular && bSun ? saturate(g_Frame.vSunSpec.rgb) * LightSpecular<SKIN>(vN, g_Frame.vSunDir.xyz, vToEye) : 0;
  vOtherSpec = 0;
  for (uint i = 0; i < min(g_Draw.nLights, XR_MAX_DRAW_LIGHTS); i++)
  {
    float4 vLightPos = g_DrawExt.vLightPos[i];
    float4 vAtten = g_DrawExt.vLightAtten[i];
    // A lamp baked into the light map (d3d12_LightmapSpecular): no light (the light map has it, with its shadows), its
    // highlight as the original's light-map bump shaders give it: R.V^32 with R the light reflected about N', nothing
    // left out where N'.L < 0, no ray (the original's has no shadow).
    if (vLightPos.w == XR_LIGHT_LIGHTMAP_LAMP)
    {
      if (bPerPixel && HasFlag(DF_SPECULAR) && HasFlag(DF_TEX1_LIGHTMAP))
      {
        const float3 vTo = vLightPos.xyz - vPos;
        const float fDist = length(vTo);
        vOtherSpec += saturate(g_DrawExt.vLightSpec[i].rgb) *
                      (LightmapLampFalloff(fDist * vAtten.w) * LightmapLampHighlight(vN, vTo / max(fDist, 1e-4), vToEye));
      }
      continue;
    }
    float3 vToLight = vLightPos.xyz;
    float fAtten = 1;
#ifdef XR_RAY_LIGHT
    float fLightDist = 0;
#endif
    if (vLightPos.w == 1)
    {
      vToLight = vLightPos.xyz - vPos;
      float fDist = length(vToLight);
      if (bClamp)
      {
        float x = fDist * vAtten.w;
        fAtten = saturate(1 - (HasFlag2(DF2_TERRAIN_LIGHT) && !HasFlag2(DF2_PROJECTOR) ? x * x : x));
      }
      else if (bPerPixel && HasFlag2(DF2_SMOOTH_FALLOFF))
        fAtten = 2 * (1 - smoothstep(0, 0.9, fDist * vAtten.w));
      else
        fAtten = fDist < vAtten.z ? 1 / (vAtten.x + vAtten.y * fDist) : 0;
      vToLight /= max(fDist, 1e-4);
#ifdef XR_RAY_LIGHT
      fLightDist = fDist;
#endif
    }
    float3 vColor = g_DrawExt.vLightColor[i].rgb;
    // A bump template's light as the original's shaders take its colour (g_Frame.vLightmap.y, d3d12_LightColorScale): the
    // light's colour times the material's (the draw's colour multiplies it below) scaled down by the largest channel where
    // that is above 1, its hue kept, and its term added without a clamp of its own (the original's clamps the sum of all
    // its terms times the texture). Else each channel of the light's colour clamped to 1.
    const bool bScale = bClamp && g_Frame.vLightmap.y > 0;
    if (bScale)
    {
      const float3 c = vColor * g_Draw.vColor.rgb;
      vColor /= max(1, max(c.r, max(c.g, c.b)));
    }
    else if (bClamp)
      vColor = saturate(vColor);
    float3 vThis = vColor * saturate(dot(vN, vToLight)) * fAtten;
    if (SKIN)
      vThis = vColor * SkinWrap(dot(vN, vToLight), g_DrawExt.vSkin.w) * fAtten;
#ifdef XR_RAY_LIGHT
    // A point light the pixel's ray does not reach: none of its light, nor its highlight.
    float fHit;
    if (vLightPos.w == 1 && any(vThis > 0) && LightVisible(vPos, vToLight, fLightDist, g_DrawExt.vLightColor[i].w, fHit) == 0)
    {
      // d3d12_RTLightShadows 2: what it takes away red; 3: by where the ray hits, green at the surface to red at the lamp.
      const uint nMode = (g_Frame.nRaySun.z >> 24) & 3;
      const float fMax = max(vThis.r, max(vThis.g, vThis.b));
      vThis = nMode == 2 ? float3(fMax, 0, 0) : nMode == 3 ? fMax * float3(fHit, 1 - fHit, 0) : 0;
      fAtten = 0;
    }
#endif
    // Its highlight's colour: with bScale scaled down by its largest channel above 1, as the original's (its hue kept,
    // times the material's specular colour after that); else each channel clamped to 1.
    if (bSpecular)
    {
      const float3 s = g_DrawExt.vLightSpec[i].rgb;
      vOtherSpec += (bScale ? s / max(1, max(s.r, max(s.g, s.b))) : saturate(s)) * (fAtten * LightSpecular<SKIN>(vN, vToLight, vToEye));
    }
    if (i == 0 && !bScale)
    {
      vLight += vThis;
      vNoSun += vThis;
    }
    else
      vOthers += bScale ? vThis : saturate(vThis);
  }
#ifdef XR_AMBIENT
  // What each sum loses without the ambient colour.
  float3 vAmbient = g_DrawExt.vAmbient.rgb / fScale;
  s_vAmbientLight = fScale * (saturate(vLight) - saturate(vLight - vAmbient));
  s_vAmbientNoSun = fScale * (saturate(vNoSun) - saturate(vNoSun - vAmbient));
#endif
  vNoSunLight = fScale * (saturate(vNoSun) + vOthers);
  return fScale * (saturate(vLight) + vOthers);
}

//! DrawLightingT<false>: the lights as the game's material takes them (all but r_Skin's draws).
float3 DrawLighting(float3 vPos, float3 vN, bool bPerPixel, out float3 vNoSunLight, out float3 vSunSpec, out float3 vOtherSpec)
{
  return DrawLightingT<false>(vPos, vN, bPerPixel, vNoSunLight, vSunSpec, vOtherSpec);
}

//! The light of a material as the fixed function lights it (DF2_MATERIAL_LIGHT) at vPos with the
//! normalised normal vN: vDiffuse = saturate(ambient + vColor * the sum of colour * N.L *
//! attenuation), vSpecular = saturate(vSpecular * the sum of the specular colour * (N.H)^power *
//! attenuation) where N.L > 0, H halfway between the light and the eye, power vParams3.x; the sun's
//! terms times its visibility fSun, point lights attenuated as per vertex.
void MaterialLighting(float3 vPos, float3 vN, float fSun, out float3 vDiffuse, out float3 vSpecular)
{
  float3 vToEye = SafeNormalize(g_Frame.vCameraPos.xyz - vPos);
  float fPower = max(g_DrawExt.vParams3.x, 1);
  float3 vLight = 0, vSpec = 0, vSun = 0;
  if (!HasFlag2(DF2_NO_SUN))
  {
    float fNL = dot(vN, g_Frame.vSunDir.xyz);
    vSun = g_Frame.vSunColor.rgb * saturate(fNL);
    vLight += fSun * g_Frame.vSunColor.rgb * saturate(fNL);   // as before vSun (the same rounding without the cascades)
    if (fNL > 0)
      vSpec += fSun * g_Frame.vSunSpec.rgb * pow(saturate(dot(vN, SafeNormalize(g_Frame.vSunDir.xyz + vToEye))), fPower);
  }
  for (uint i = 0; i < min(g_Draw.nLights, XR_MAX_DRAW_LIGHTS); i++)
  {
    float4 vLightPos = g_DrawExt.vLightPos[i];
    float4 vAtten = g_DrawExt.vLightAtten[i];
    if (vLightPos.w == XR_LIGHT_LIGHTMAP_LAMP)
      continue;
    float3 vToLight = vLightPos.xyz;
    float fAtten = 1;
#ifdef XR_RAY_LIGHT
    float fLightDist = 0;
#endif
    if (vLightPos.w == 1)
    {
      vToLight = vLightPos.xyz - vPos;
      float fDist = length(vToLight);
      fAtten = fDist < vAtten.z ? 1 / (vAtten.x + vAtten.y * fDist) : 0;
      vToLight /= max(fDist, 1e-4);
#ifdef XR_RAY_LIGHT
      fLightDist = fDist;
#endif
    }
    float fNL = dot(vN, vToLight);
#ifdef XR_RAY_LIGHT
    float fHit;
    if (vLightPos.w == 1 && fNL > 0 && fAtten > 0)
      fAtten *= LightVisible(vPos, vToLight, fLightDist, g_DrawExt.vLightColor[i].w, fHit);
#endif
    vLight += g_DrawExt.vLightColor[i].rgb * saturate(fNL) * fAtten;
    if (fNL > 0)
      vSpec += g_DrawExt.vLightSpec[i].rgb * pow(saturate(dot(vN, SafeNormalize(vToLight + vToEye))), fPower) * fAtten;
  }
  vDiffuse = saturate(g_DrawExt.vAmbient.rgb + g_Draw.vColor.rgb * vLight);
#ifdef XR_CASCADES
  // DF2_SHADOW_TINT: in the shadow the lit diffuse times the tint where that is more, towards the lit one as the sun
  // shows (never less than without the tint: the light with part of the sun is more than that lerp, saturate's curve).
  if (HasFlag2(DF2_SHADOW_TINT))
  {
    const float3 vLit = saturate(g_DrawExt.vAmbient.rgb + g_Draw.vColor.rgb * (vLight + (1 - fSun) * vSun));
    vDiffuse = max(vDiffuse, lerp(vLit * g_Frame.vShadowTint.rgb, vLit, fSun));
  }
#endif
  vSpecular = saturate(g_DrawExt.vSpecular.rgb * vSpec);
#ifdef XR_AMBIENT
  s_vAmbientLight = vDiffuse - saturate(g_Draw.vColor.rgb * vLight);
#endif
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
  float fCamera = g_Frame.vCameraPos.z - g_DrawExt.vParams0.x;
  float fDepth = g_DrawExt.vParams0.x - vPos.z;
  if (fCamera <= 0)
    return (g_DrawExt.vParams0.z > 0 || fDepth > 0) ? FogInsideVolume(ViewDepth(vPos), g_DrawExt.vParams0.y) : 0;
  if (g_DrawExt.vParams0.z > 0 || fDepth <= 0)
    return 0;
  float fGate = 1 - pow(1 - saturate(fDepth / 5), lerp(3, 1.6, saturate(fCamera / 5)));
  return fGate * (1 - exp(-ViewDepth(vPos) * g_DrawExt.vParams0.y));
}

//! The glass of a scope (DF2_SHADER_FX, XR_FX_SCOPE): the copy of the scene (tex0) around the middle of the
//! screen, where the scope aims, as through a real one: each pixel shows the scene at its own offset
//! from the middle of the glass times vParams0.x from there. The middle of the glass is uv0 (0.5,
//! 0.5); the offset in pixels comes from uv0 through its screen derivatives, so the picture stands
//! upright whichever way the glass's texture coordinates run. The wave slopes (tex1, unit RMS) times
//! vParams0.y move it in uv0: the glass is not quite even. The samples stay in the part of the
//! screen copied (vParams1: xy to zw in uv). rgb times vColor.rgb; alpha vColor.a *
//! 2^-(r / vParams0.z)^4, r = |uv0 - 0.5|.
float4 ScopeLens(SVertexOutput In)
{
  float2 vMiddle = 0.5 * g_Frame.vScreen.xy;
  float2 vUV = In.vTex0;
  float2 s1 = (g_Textures[g_Draw.nTex1].Sample(g_SamplerLinearWrap, 2 * vUV).rg * 255 - 128) / 32;
  float2 s2 = (g_Textures[g_Draw.nTex1].Sample(g_SamplerLinearWrap, 2 * vUV.yx + 0.5).gr * 255 - 128) / 32;
  float2 vOffset = vUV - 0.5 + (s1 + s2) * (0.7071 * g_DrawExt.vParams0.y);
  // The offset in pixels p: vOffset = p.x * ddx(uv0) + p.y * ddy(uv0).
  float2 a = ddx(vUV), b = ddy(vUV);
  float fDet = a.x * b.y - b.x * a.y;
  float2 vPixels = abs(fDet) > 1e-12 ? float2(vOffset.x * b.y - b.x * vOffset.y, a.x * vOffset.y - vOffset.x * a.y) / fDet : 0;
  float2 vAt = clamp((vMiddle + vPixels * g_DrawExt.vParams0.x) * g_Frame.vScreen.zw, g_DrawExt.vParams1.xy, g_DrawExt.vParams1.zw);
  float3 vScene = g_Textures[g_Draw.nTex0].SampleLevel(g_SamplerLinearClamp, vAt, 0).rgb;

  float2 d = (vUV - 0.5) / g_DrawExt.vParams0.z;
  float r2 = dot(d, d);
  return float4(vScene * g_Draw.vColor.rgb, g_Draw.vColor.a * exp2(-r2 * r2));
}

//! A mirror (XR_FX_MIRROR; TemplGlassEnvTex, the original's CGRCGlassTex): the scene mirrored in
//! its plane (tex0, seen by the mirrored camera, mParams0 its world to clip) at the pixel's world
//! position * vParams2.x (ReflectAmount; 0 without the picture) + the texture (tex1 at uv0) *
//! vParams2.z (1 - ReflectAmount); alpha vColor.a (Opacity) * the vertex colour's alpha * the
//! texture's.
float4 Mirror(SVertexOutput In)
{
  float4 vClip = mul(float4(In.vWorldPos, 1), g_DrawExt.mParams0);
  float2 vAt = vClip.xy / max(vClip.w, 1e-4) * float2(0.5, -0.5) + 0.5;
  float3 vScene = g_Textures[g_Draw.nTex0].SampleLevel(g_SamplerLinearClamp, vAt, 0).rgb;
  float4 vDecal = SampleTexture(g_Draw.nTex1, g_Draw.nSamplers & 15, In.vTex0);
  return float4(vScene * g_DrawExt.vParams2.x + vDecal.rgb * g_DrawExt.vParams2.z, In.vColor.a * vDecal.a);
}

//! Heat haze (XR_FX_HAZE; the particles of ParticleMin/Med/MaxHeatHaze, the original's
//! CGVProgHeatHaze and CGRCHeatHaze): the copy of the scene (tex0) at the pixel moved by the ripple
//! (tex1, WaterBump's normal map, whose xy are the height's slopes as the original's du/dv map: rg *
//! 2 - 1, at (uv0 + vParams0.zw) * vParams0.y) times vParams0.x,
//! * vColor.rgb; alpha the mask (tex2's alpha at uv0, haze_mask) * the particle's alpha.
float4 HeatHaze(SVertexOutput In)
{
  float2 vRipple = SampleTexture(g_Draw.nTex1, XR_SAMPLER_LINEAR_WRAP, (In.vTex0 + g_DrawExt.vParams0.zw) * g_DrawExt.vParams0.y).rg * 2 - 1;
  uint nW, nH;
  g_Textures[g_Draw.nTex0].GetDimensions(nW, nH);
  float2 vAt = In.vPos.xy / float2(nW, nH) + vRipple * g_DrawExt.vParams0.x;
  float3 vScene = g_Textures[g_Draw.nTex0].SampleLevel(g_SamplerLinearClamp, vAt, 0).rgb;
  float fMask = SampleTexture(g_Draw.nTex2, XR_SAMPLER_LINEAR_CLAMP, In.vTex0).a;
  return float4(vScene * g_Draw.vColor.rgb, fMask * In.vColor.a);
}

//! The scene behind a model moved by its bump map (XR_FX_BUMP_OFFSET; TemplBumpOffsetEnv, the original's
//! CGPSBumpOffset): the copy of the scene (tex0) at the pixel moved by the normal map's xy (tex1 at
//! uv0, as the original's du/dv map) * vParams0.xy (BumpAmountX/Y), + the texture (tex2 at uv0, when
//! vParams0.z), opaque.
float4 BumpOffset(SVertexOutput In)
{
  float2 vBump = SampleTexture(g_Draw.nTex1, XR_SAMPLER_LINEAR_WRAP, In.vTex0).rg * 2 - 1;
  uint nW, nH;
  g_Textures[g_Draw.nTex0].GetDimensions(nW, nH);
  float2 vAt = In.vPos.xy / float2(nW, nH) + vBump * g_DrawExt.vParams0.xy;
  float3 vScene = g_Textures[g_Draw.nTex0].SampleLevel(g_SamplerLinearClamp, vAt, 0).rgb;
  float3 vBase = g_DrawExt.vParams0.z > 0 ? SampleTexture(g_Draw.nTex2, g_Draw.nSamplers & 15, In.vTex0).rgb : 0;
  return float4(vScene + vBase, 1);
}

// The templates of special lighting, per pixel where the original's vertex shaders take N.E, N.L as
// texture coordinates of their vertices: N the vertex normal, E towards the camera, L towards the sun.

//! The rainbow (XR_FX_RAINBOW; TemplRainbow: the original's CGVProg_RainbowShading over its fixed function
//! stage, its pixel shader CGRCRainbow not used): tex0 (Defaults/colors2) at (N.E, N.H), H = normalize(E + N);
//! opaque, unlit.
float4 Rainbow(SVertexOutput In)
{
  float3 N = SafeNormalize(In.vNormal);
  float3 E = SafeNormalize(g_Frame.vCameraPos.xyz - In.vWorldPos);
  float3 H = SafeNormalize(E + N);
  return float4(SampleTexture(g_Draw.nTex0, XR_SAMPLER_LINEAR_WRAP, float2(dot(N, E), dot(N, H))).rgb, 1);
}

//! The membrane (XR_FX_MEMBRANE; TemplMembrane, CGVProg_MembraneShading): tex0 (membrane/yellowred, a row of
//! colours) at u = E.N + vParams0.x (0.2 t), added (ONE, ONE).
float4 Membrane(SVertexOutput In)
{
  float3 N = SafeNormalize(In.vNormal);
  float3 E = SafeNormalize(g_Frame.vCameraPos.xyz - In.vWorldPos);
  return float4(SampleTexture(g_Draw.nTex0, XR_SAMPLER_LINEAR_WRAP, float2(dot(E, N) + g_DrawExt.vParams0.x, 0)).rgb, 1);
}

//! Cartoon shading (XR_FX_CARTOON; TemplCartoonShading, CGVProg_CartoonShading): tex0 (defaults/4shade, the
//! steps of the light) at L.N times tex1 (defaults/edgeimage, dark at the outline) at E.N, both clamped, times
//! vColor (the script's 1 0.8 0.2); opaque, unlit.
float4 Cartoon(SVertexOutput In)
{
  float3 N = SafeNormalize(In.vNormal);
  float3 E = SafeNormalize(g_Frame.vCameraPos.xyz - In.vWorldPos);
  float3 vShade = SampleTexture(g_Draw.nTex0, XR_SAMPLER_LINEAR_CLAMP, float2(dot(g_Frame.vSunDir.xyz, N), 0)).rgb;
  float3 vEdge = SampleTexture(g_Draw.nTex1, XR_SAMPLER_LINEAR_CLAMP, float2(dot(E, N), 0)).rgb;
  return float4(vShade * vEdge * g_Draw.vColor.rgb, 1);
}

//! A shell of fur (XR_FX_FUR; TemplFurRendering, CGVProg_Fur; the vertex shader moves it vParams0.x out along
//! the normal): tex0 (defaults/fuzz) at uv0 times the sun's light by N.L (the script's: N.L of each light alone)
//! plus the object's ambient colour (vAmbient.rgb; without it the fur away from the sun is black); alpha the
//! texture's times vColor.a (the shell's: 0.9 for the first down to 0.2).
float4 Fur(SVertexOutput In)
{
  float4 vFuzz = SampleTexture(g_Draw.nTex0, XR_SAMPLER_LINEAR_WRAP, In.vTex0);
  float3 vLight = g_DrawExt.vAmbient.rgb + g_Frame.vSunColor.rgb * saturate(dot(SafeNormalize(In.vNormal), g_Frame.vSunDir.xyz));
  return float4(vFuzz.rgb * vLight, vFuzz.a * g_Draw.vColor.a);
}

//! Iridescence (XR_FX_IRIDESCENCE; TemplIridescence_PS20, CGRCIridescence_PS20): the wing tex0, whose alpha is
//! how much light it lets through, lit from both sides (N'.L + 0.5, and -N'.L * a * vParams1.x the light
//! scattered through) and times its opacity 1 - a, plus the gloss map tex2 * (saturate(N'.H) * vParams0.x +
//! vParams0.y) * the rainbow tex3 (Defaults/rainbowgradient) at N'.E * vParams0.z + vParams0.w; alpha 1 - a, for
//! ONE, INVSRCALPHA. N' the normal map tex1 in the tangent frame (vParams1.y 1 with one; the vertex normal
//! without), turned towards the camera for H.
float4 Iridescence(SVertexOutput In)
{
  float4 vBase = SampleTexture(g_Draw.nTex0, g_Draw.nSamplers & 15, In.vTex0);
  float3 N = SafeNormalize(In.vNormal);
  if (g_DrawExt.vParams1.y > 0)
  {
    float3 m = SampleTexture(g_Draw.nTex1, XR_SAMPLER_LINEAR_WRAP, In.vTex0).rgb * 2 - 1;
    N = SafeNormalize(m.x * In.vTangent + m.y * In.vBinormal + m.z * In.vTNormal);
  }
  float3 L = g_Frame.vSunDir.xyz;
  float3 E = SafeNormalize(g_Frame.vCameraPos.xyz - In.vWorldPos);
  float3 H = SafeNormalize(L + E);
  float fOpacity = 1 - vBase.a;
  float fScattered = saturate(dot(-N, L)) * vBase.a * g_DrawExt.vParams1.x;
  float3 c = vBase.rgb * (fScattered + saturate(dot(N, L)) + 0.5) * fOpacity;
  float3 vRainbow = SampleTexture(g_Draw.nTex3, XR_SAMPLER_LINEAR_WRAP, float2(dot(N, E) * g_DrawExt.vParams0.z + g_DrawExt.vParams0.w, 0.5)).rgb;
  float3 Nh = dot(In.vNormal, E) < 0 ? -N : N;
  float3 vGloss = SampleTexture(g_Draw.nTex2, XR_SAMPLER_LINEAR_WRAP, In.vTex0).rgb * (saturate(dot(Nh, H)) * g_DrawExt.vParams0.x + g_DrawExt.vParams0.y);
  return float4(c + vGloss * vRainbow, fOpacity);
}

//! A soap bubble (XR_FX_BUBBLE; TemplBubble_PS20, CGRCBubble_PS20; the vertex shader makes it wobble): the
//! texture tex0 times the cube g_Cubes[nTex1] at the view reflected about the normal, times 2, towards the cube
//! where its alpha glows (saturate((a^2 - 0.75) * 4)); alpha (1 - |N.E|) * vParams0.x + vParams0.y + the glow.
float4 Bubble(SVertexOutput In)
{
  float4 vBase = SampleTexture(g_Draw.nTex0, g_Draw.nSamplers & 15, In.vTex0);
  float3 N = SafeNormalize(In.vNormal);
  float3 I = SafeNormalize(In.vWorldPos - g_Frame.vCameraPos.xyz);
  float4 vEnv = SampleCube(g_Draw.nTex1, XR_SAMPLER_LINEAR_CLAMP, reflect(I, N));
  float fGlow = saturate((vEnv.a * vEnv.a - 0.75) * 4);
  float3 c = lerp(saturate(vBase.rgb * vEnv.rgb * 2), vEnv.rgb, fGlow);
  return float4(c, (1 - abs(dot(I, N))) * g_DrawExt.vParams0.x + g_DrawExt.vParams0.y + fGlow);
}

//! Lava (XR_FX_LAVA; LavaVolume, the original's CGVProgLava and CGRCLava). b = (dot(world pos, vTexGenU), dot(..,
//! vTexGenV)) the position in the object's space by its planes (an eighth of it, times TillingAmount); the ripple
//! r: WaterBump's normal map tex1 at b + vParams2.xy, xy as the original's du/dv map (rg * 2 - 1); the glow map
//! tex2 (defaults/white_plain_d, a noise of greens) at b + vParams2.zw + r * vParams3.xy, its green g making
//! k = (2 (g - 0.5) - 0.75) * 4. The texture tex0 at (uv0 + vParams0.zw) * vParams0.xy (it flows with time) under
//! the lava colour L = vParams1.rgb: f = saturate(k L + L / 2), f = saturate(f^2 + L / 2), then twice lerp(f,
//! (texture * f)^2 * 4, vParams1.w) (the overlay; twice as the original's last instruction, _x2: its colour is
//! above white where the glow map is greenest, which the HDR target keeps); opaque, unlit.
float4 Lava(SVertexOutput In)
{
  float4 vWorld = float4(In.vWorldPos, 1);
  float2 b = float2(dot(vWorld, g_DrawExt.vTexGenU), dot(vWorld, g_DrawExt.vTexGenV));
  float2 r = SampleTexture(g_Draw.nTex1, XR_SAMPLER_LINEAR_WRAP, b + g_DrawExt.vParams2.xy).rg * 2 - 1;
  float g = SampleTexture(g_Draw.nTex2, XR_SAMPLER_LINEAR_WRAP, b + g_DrawExt.vParams2.zw + r * g_DrawExt.vParams3.xy).g;
  float k = (2 * (g - 0.5) - 0.75) * 4;
  float3 vTex = SampleTexture(g_Draw.nTex0, g_Draw.nSamplers & 15, (In.vTex0 + g_DrawExt.vParams0.zw) * g_DrawExt.vParams0.xy).rgb;
  float3 L = g_DrawExt.vParams1.rgb;
  float3 f = saturate(k * L + 0.5 * L);
  f = saturate(f * f + 0.5 * L);
  float3 o = vTex * f;
  return float4(2 * lerp(f, o * o * 4, g_DrawExt.vParams1.w), 1);
}

//! DF2_SHADER_FX: the mode vParams3.w; fogged with DF_FOG (towards black with DF2_FOG_BLACK).
float4 ShaderFx(SVertexOutput In)
{
  float4 vColor;
  switch ((uint)g_DrawExt.vParams3.w)
  {
  case XR_FX_SCOPE:
    return ScopeLens(In);
  case XR_FX_HAZE:
    return HeatHaze(In);
  case XR_FX_MIRROR:
    vColor = Mirror(In);
    break;
  case XR_FX_BUMP_OFFSET:
    vColor = BumpOffset(In);
    break;
  case XR_FX_RAINBOW:
    vColor = Rainbow(In);
    break;
  case XR_FX_MEMBRANE:
    vColor = Membrane(In);
    break;
  case XR_FX_CARTOON:
    vColor = Cartoon(In);
    break;
  case XR_FX_FUR:
    vColor = Fur(In);
    break;
  case XR_FX_IRIDESCENCE:
    vColor = Iridescence(In);
    break;
  case XR_FX_LAVA:
    vColor = Lava(In);
    break;
  default:
    vColor = Bubble(In);
    break;
  }
  if (HasFlag(DF_FOG) && g_Frame.vFogParams.w != 0)
    vColor.rgb = lerp(HasFlag2(DF2_FOG_BLACK) ? float3(0, 0, 0) : g_Frame.vFogColor.rgb, vColor.rgb, SceneFog(In.vWorldPos));
  return vColor;
}

//! The gradient noise of the bubble's wobble (the original's 2D Perlin noise of 32 x 32 random gradients):
//! about -0.7 to 0.7, repeating every 32 units.
float BubbleNoise(float2 v)
{
  float2 i = floor(v), f = v - i;
  float2 s = f * f * (3 - 2 * f);
  float vCorner[4];
  [unroll] for (uint k = 0; k < 4; k++)
  {
    float2 o = float2(k & 1, k >> 1);
    uint2 q = (uint2)(int2(i + o) & 31);
    uint h = q.x * 0x8da6b343u ^ q.y * 0xd8163841u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    float a = h * (6.2831853 / 4294967296.0);
    vCorner[k] = dot(float2(cos(a), sin(a)), f - o);
  }
  return lerp(lerp(vCorner[0], vCorner[1], s.x), lerp(vCorner[2], vCorner[3], s.x), s.y);
}

#ifdef XR_CASCADE_INSTANCES
//! A caster of the full set of the sun's cascades into all the slices it reaches at once, an
//! instance each: mShadow[i] the world to slice nShadowTex[i] in place of mWorldViewProj.
SVertexOutput VSMain(SVertexInput In, uint nInstance : SV_InstanceID)
#else
SVertexOutput VSMain(SVertexInput In)
#endif
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
    // A terrain detail layer lies over the terrain as the original's (its position script
    // PosTerrainLayerOverlay): 5 cm out along the normal, and its clip w 0.005 further, which
    // brings its depth nearer the more the farther it is. The engine builds the layers' vertices
    // a few centimetres off the sectors' (2.6 cm along -x and -y, 1 cm up), so without it a
    // layer is behind its sector wherever the ground falls towards +x and +y.
    const bool bOverlay = HasFlag(DF_DETAIL_LAYER);
    if (bOverlay)
      vPos.xyz += In.vNormal * 0.05;
    // A shell of fur, vParams0.x out along the normal; the bubble wobbling, out along the normal by the noise
    // at its xy moving with vParams1.xy, times vParams1.w (CGVProgBubble_VS20's PosWaterDeform_TNorm).
    if (HasFlag2(DF2_SHADER_FX))
    {
      if (g_DrawExt.vParams3.w == XR_FX_FUR)
        vPos.xyz += In.vNormal * g_DrawExt.vParams0.x;
      else if (g_DrawExt.vParams3.w == XR_FX_BUBBLE)
        vPos.xyz += In.vNormal * (BubbleNoise(vPos.xy + g_DrawExt.vParams1.xy) * g_DrawExt.vParams1.w);
    }
#ifdef XR_CASCADE_INSTANCES
    Out.vPos = mul(vPos, g_DrawExt.mShadow[nInstance]);
    Out.nSlice = g_DrawExt.nShadowTex[nInstance];
#else
    Out.vPos = mul(vPos, g_Draw.mWorldViewProj);
#endif
    vWorldPos = mul(vPos, g_Draw.mWorld).xyz;
#ifdef XR_VELOCITY
    Out.vCurClip = Out.vPos;
    Out.vPrevClip = mul(vPos, g_Draw.mPrevWorldViewProj);
#endif
    if (bOverlay)
      Out.vPos.w += 0.005;
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
  // TemplMuzzleFlash_Auto's planes (Training's bunker's shafts of sunlight), as the original's vertex program: faded
  // out seen edge on, by min((2 |N.V|)^4, 1) at each vertex.
  if (g_DrawExt.vEdgeFade.x > 0)
  {
    const float f = 2 * abs(dot(SafeNormalize(vNormal), SafeNormalize(vWorldPos - g_Frame.vCameraPos.xyz)));
    vColor.rgb *= min(f * f * f * f, 1);
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
    vTex0 = float2(dot(vPos, g_DrawExt.vTexGenU), dot(vPos, g_DrawExt.vTexGenV));
  }
  if (HasFlag(DF_TEXMATRIX))
  {
    float3 vUV = float3(vTex0, 1);
    vTex0 = float2(dot(vUV, g_DrawExt.vTexMat0.xyz), dot(vUV, g_DrawExt.vTexMat1.xyz));
  }

  Out.vColor = vColor;
  Out.vTex0 = vTex0;
  Out.vTex1 = In.vTex1;
  if (HasFlag(DF_TEXGEN1_WORLD))
  {
    float4 vPos = float4(vWorldPos, 1);
    Out.vTex1 = float2(dot(vPos, g_DrawExt.vTexGen1U), dot(vPos, g_DrawExt.vTexGen1V));
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

//! DF2_HDR_CORE: how fast the light's factor falls off from its core to its edge.
static const float HDR_CORE_POWER = 4;

#ifdef XR_FOLIAGE
//! DF2_FOLIAGE: the sun's light through a leaf at world position vPos towards the camera, per unit of
//! its colour: the sun's colour times vFoliage.x, times the cosine of the angle between the view and the
//! sun to the power vFoliage.y, where the full set of the cascades shows the sun (the outer leaves of a
//! crown), fading out from half of vFoliage.z to vFoliage.z.
float3 FoliageLight(float3 vPos)
{
  float3 vView = vPos - g_Frame.vCameraPos.xyz;
  float fDist = length(vView);
  float fBack = pow(saturate(dot(vView, g_Frame.vSunDir.xyz) / max(fDist, 1e-4)), g_Frame.vFoliage.y);
  float fFade = saturate((g_Frame.vFoliage.z - fDist) * g_Frame.vFoliage.w);
  float f = g_Frame.vFoliage.x * fBack * fFade;
  if (f <= 0)
    return 0;
  return g_Frame.vSunColor.rgb * (f * CascadeTap(vPos));
}
#endif

#ifdef XR_RAY_SUN
//! The mask of the sun's shadow by rays (RaySun.hlsl) at the pixel vPos (SV_Position): x the sun's
//! visibility from all the casters, y from the live ones; fWeight how much it counts, 0 where the mask
//! is of another surface (its view depth not the pixel's: a surface the depth prepass does not have).
float2 RaySunMask(float4 vPos, out float fWeight)
{
  const float2 v = g_Textures[g_Frame.nRaySun.x].Load(int3(vPos.xy, 0)).xy;
  const float d = g_Frame.vDepthParams.y / (vPos.z + g_Frame.vDepthParams.x);
  const uint n = (uint)v.y;
  fWeight = abs(v.x - d) <= 0.001 * d + 0.003 ? (float)(n >> 16) / 255 : 0;
  return float2(n & 255, (n >> 8) & 255) / 255;
}
#endif

#ifdef XR_RAY_REFL
//! What a ray from the surface point P (fDistance from the camera; vFace the face's normal) in
//! direction R sees, over vCube, the cube's colour that way: where it hits the scene for rays, that
//! shaded and fogged by the path from the camera, fading back to vCube over the last quarter of the
//! rays' length; vCube where it hits nothing or the ocean first. The cone: fPixelAngle wide at the
//! camera (the ray's origin that much off the face), spreading by fSpread from the surface on (the
//! detail of what it finds: a rough surface's lobe).
//! d3d12_RTPbrScreen: the last frame's picture at the world point H (nPrevScene's copy, before the exposure; its fog by
//! that frame's view), w its share (PbrSpecular.hlsli's PbrPrevShare: 1 where that frame saw H, the depth there as far
//! as H give or take its tolerance and fWidth; less over the picture's sides; 0 off it, hidden there or without a copy).
float4 PrevFrameAt(float3 H, float fWidth)
{
  if (g_Frame.nPrevScene.x == 0)
    return 0;
  const float3 p = PbrPrevUV(mul(float4(H, 1), g_Frame.mPrevViewProj), g_Frame.vPrevParams.xy);
  if (p.z <= 0 || any(p.xy <= 0) || any(p.xy >= 1))
    return 0;
  const float2 vSize = float2(g_Frame.nPrevScene.zw);
  const float fDepth = g_Textures[g_Frame.nPrevScene.y].Load(int3(int2(min(p.xy * vSize, vSize - 1)), 0)).r;
  const float fShare = PbrPrevShare(p.xy, p.z, PbrViewDistance(fDepth, g_Frame.vPrevParams.z, g_Frame.vPrevParams.w), fWidth);
  if (fShare <= 0)
    return 0;
  return float4(g_Textures[g_Frame.nPrevScene.x].SampleLevel(g_SamplerLinearClamp, p.xy, 0).rgb, fShare);
}

float3 SurfaceRayCone(float3 vCube, float3 P, float3 vFace, float3 R, float fDistance, float fPixelAngle, float fSpread, bool bPrev = false)
{
  const float fWidth = fDistance * fPixelAngle;
  RayDesc Ray;
  Ray.Origin = P + (dot(R, vFace) >= 0 ? vFace : -vFace) * (0.02 + 0.5 * fWidth);
  Ray.Direction = R;
  Ray.TMin = 0.01;
  Ray.TMax = asfloat(g_Frame.nRaySun.y);
  const float fFadeStart = 0.75 * Ray.TMax;
  // The ocean, from above it.
  const float fWater = asfloat(g_Frame.nRaySun.w);
  if (R.z < 0 && Ray.Origin.z > fWater)
    Ray.TMax = min(Ray.TMax, (fWater - Ray.Origin.z) / R.z);
  const uint nMask = g_Frame.nRaySun.z;
  const SRayHit Hit = TraceScene(Ray, nMask & 0xff, fWidth, fSpread);
  const bool bDebug = ((nMask >> 16) & 0xf) == 2;
  if (!Hit.bHit)
    return bDebug ? vCube * float3(0.25, 0.25, 1) : vCube;
  // The last frame's light of the hit where it saw it (d3d12_RTPbrScreen; the hits tinted green in the debug mode):
  // where it is all of it, the hit not shaded (its texture, light map and the ray to the sun not looked up).
  const float4 vPrev = bPrev ? PrevFrameAt(Ray.Origin + R * Hit.fT, fWidth + Hit.fT * fSpread) : 0;
  float3 vColor = vPrev.rgb;
  [branch]
  if (vPrev.w < 1)
  {
    bool bFog;
    vColor = RayShade(Hit, Ray.Direction, fWidth + Hit.fT * fSpread, (nMask & 0x100) != 0, nMask & 0xff, bFog).rgb;
    if (bFog && g_Frame.vFogParams.w != 0)
    {
      const float fDepth = ViewDepth(P) * (fDistance + Hit.fT) / max(fDistance, 1e-3);
      vColor = lerp(g_Frame.vFogColor.rgb, vColor, saturate((g_Frame.vFogParams.y - fDepth) * g_Frame.vFogParams.z));
    }
    if (bPrev)
      vColor = lerp(vColor, vPrev.rgb, vPrev.w);
  }
  const float fPrev = vPrev.w;
  vColor = lerp(vColor, vCube, smoothstep(fFadeStart, asfloat(g_Frame.nRaySun.y), Hit.fT));
  return bDebug ? vColor * lerp(float3(1, 0.25, 0.25), float3(0.25, 1, 0.25), fPrev) : vColor;
}

//! SurfaceRayCone of a mirror: the cone spreading by fPixelAngle.
float3 SurfaceRay(float3 vCube, float3 P, float3 vFace, float3 R, float fDistance, float fPixelAngle)
{
  return SurfaceRayCone(vCube, P, vFace, R, fDistance, fPixelAngle, fPixelAngle);
}
#endif

//! The cube tex0 in the direction vDir (world). The rays of XR_RAY_REFL do not take its place: the pixel shader with
//! a ray query at each of these lookups hung the GPU (a page fault, AMD).
float4 CubeLookup(float3 vDir, uint nSampler)
{
  return SampleCube(g_Draw.nTex0, nSampler, vDir);
}

//! The view vector I refracted about the normal N with the ratio fEta as the original's vertex shaders refract it:
//! the square root of the absolute value (their rsq), so that beyond the critical angle it bends on instead of
//! giving 0.
float3 RefractAbs(float3 I, float3 N, float fEta)
{
  const float d = dot(N, I);
  return fEta * I - (fEta * d + sqrt(abs(1 - fEta * fEta * (1 - d * d)))) * N;
}

//! DF2_CUBE_SHADER: the colour of the cube map templates of the original's vertex and pixel shaders (Draw3D.cpp), in
//! world space, by the mode vParams3.w; vView the view vector (normalized, from the camera), vN the normal:
//!   1 TemplRefrCM: the cube seen through (vView refracted with the ratio vParams2.w) times vParams2.x
//!   2 the dispersion: red, green and blue seen through with the ratios vParams3.xyz, the cube reflected over them
//!     by F = saturate(vParams2.z + vParams2.x * (1 - N.V)^vParams2.y)
//!   3 TemplGlassCM_PS20 (sin t = saturate(sin i * vParams2.w)): seen through along the refracted ray (sin t along
//!     the incident ray's direction on the surface, - cos t along N) times the base colour vParams0.rgb, 1 - sin t and
//!     vParams2.x; reflected times sin i and vParams2.y; the rainbow (tex4 at cos i^vParams3.x) times vParams2.z, the
//!     base colour and sin t; + vParams3.y
//!   4 the bump refraction (the vertex shader's sum with the normal map tex3's unsigned values, or a flat one
//!     without vParams0.z, in the tangent frame; vParams2.x its bump scale, vParams2.y its ratio) times vParams2.z,
//!     plus the cube reflected about the bumped normal (the map's x and y times vParams0.x) times vParams2.w; with
//!     vParams0.y the amounts 1 - F and F, F = saturate(vParams3.z + vParams3.x * (1 - N.V)^vParams3.y)
//!   5 tex4 + the reflection times tex4's alpha, 6 the reflection times tex4 (tex4 at uv0)
float4 CubeShader(SVertexOutput In, float3 vView, float3 vN, uint nSampler)
{
  const int nMode = (int)g_DrawExt.vParams3.w;
  const float3 P = In.vWorldPos;
  if (nMode == 1)
    return float4(CubeLookup(RefractAbs(vView, vN, g_DrawExt.vParams2.w), nSampler).rgb * g_DrawExt.vParams2.x, 1);
  if (nMode == 2)
  {
    const float3 vThrough = float3(CubeLookup(RefractAbs(vView, vN, g_DrawExt.vParams3.x), nSampler).r,
                                   CubeLookup(RefractAbs(vView, vN, g_DrawExt.vParams3.y), nSampler).g,
                                   CubeLookup(RefractAbs(vView, vN, g_DrawExt.vParams3.z), nSampler).b);
    const float F = saturate(g_DrawExt.vParams2.z + g_DrawExt.vParams2.x * pow(1 - saturate(-dot(vView, vN)), g_DrawExt.vParams2.y));
    return float4(lerp(vThrough, CubeLookup(reflect(vView, vN), nSampler).rgb, F), 1);
  }
  if (nMode == 3)
  {
    const float fCosI = -dot(vView, vN);
    const float fSinI = sqrt(abs(1 - fCosI * fCosI));
    const float fSinT = saturate(fSinI * g_DrawExt.vParams2.w);
    const float fCosT = sqrt(abs(1 - fSinT * fSinT));
    const float3 vThrough = fSinT * SafeNormalize(vView + fCosI * vN) - fCosT * vN;
    const float3 vBase = g_DrawExt.vParams0.rgb;
    const float3 vRainbow = SampleTexture(g_Draw.nTex4, XR_SAMPLER_LINEAR_WRAP, pow(abs(fCosI), g_DrawExt.vParams3.x).xx).rgb;
    float3 c = CubeLookup(vThrough, nSampler).rgb * vBase * ((1 - fSinT) * g_DrawExt.vParams2.x);
    c += CubeLookup(reflect(vView, vN), nSampler).rgb * (fSinI * g_DrawExt.vParams2.y);
    c += vRainbow * vBase * (fSinT * g_DrawExt.vParams2.z) + g_DrawExt.vParams3.y;
    return float4(c, 1);
  }
  if (nMode == 4)
  {
    float3 vT = In.vTangent, vB = In.vBinormal, vF = In.vTNormal;
    if (dot(vT, vT) < 1e-8 || dot(vF, vF) < 1e-8)
    {
      vF = SafeNormalize(In.vNormal);
      vT = SafeNormalize(cross(abs(vF.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0), vF));
      vB = cross(vF, vT);
    }
    // The eye (camera - point, not normalized) in the tangent frame, as the vertex shader has it.
    const float3 E = g_Frame.vCameraPos.xyz - P;
    const float3 V = float3(dot(vT, E), dot(vB, E), dot(vF, E));
    const float fRatio = g_DrawExt.vParams2.y;
    const float d = sqrt(abs(fRatio * fRatio * dot(V, V) - dot(V.xy, V.xy))) - V.z;
    const float3 n = g_DrawExt.vParams0.z > 0 ? SampleTexture(g_Draw.nTex3, XR_SAMPLER_LINEAR_WRAP, In.vTex0).rgb : float3(0.5, 0.5, 1);
    const float3 D = g_DrawExt.vParams2.x * (n.x * float3(-d, 0, V.x * d) + n.y * float3(0, -d, V.y * d)) - n.z * V;
    float fRefract = g_DrawExt.vParams2.z, fReflect = g_DrawExt.vParams2.w;
    if (g_DrawExt.vParams0.y > 0)
    {
      fReflect = saturate(g_DrawExt.vParams3.z + g_DrawExt.vParams3.x * pow(1 - saturate(dot(SafeNormalize(E), SafeNormalize(In.vNormal))), g_DrawExt.vParams3.y));
      fRefract = 1 - fReflect;
    }
    float3 c = CubeLookup(D.x * vT + D.y * vB + D.z * vF, nSampler).rgb * fRefract;
    if (fReflect > 0)
    {
      const float3 Nb = g_DrawExt.vParams0.x * (n.x * vT + n.y * vB) + n.z * vF;
      c += CubeLookup(2 * Nb * dot(Nb, E) / max(dot(Nb, Nb), 1e-8) - E, nSampler).rgb * fReflect;
    }
    return float4(c, 1);
  }
  const float4 vDecal = SampleTexture(g_Draw.nTex4, XR_SAMPLER_LINEAR_WRAP, In.vTex0);
  const float3 vRefl = CubeLookup(reflect(vView, vN), nSampler).rgb;
  return float4(nMode == 5 ? vDecal.rgb + vRefl * vDecal.a : vRefl * vDecal.rgb, 1);
}

// r_Parallax (nParallax.x): the textures at uv0 read where the view meets the height, by uv0's own gradients (scaled for
// vMipBias.x as SampleTexture's bias), not the offset's (SampleUV0).
static bool s_bParallax = false;
static float2 s_vParallaxDx = 0, s_vParallaxDy = 0;

//! A texture at uv0 (or a uv moving with it): SampleTexture, or with r_Parallax by uv0's gradients (ParallaxFetch).
float4 SampleUV0(uint nIndex, uint nSampler, float2 vUV)
{
  [branch]
  if (s_bParallax)
    return SampleTextureGrad(nIndex, nSampler, vUV, s_vParallaxDx, s_vParallaxDy);
  return SampleTexture(nIndex, nSampler, vUV);
}

//! r_Parallax: the uv0 where the view meets the height of g_Textures[nParallax.x] (Parallax.hlsli's ParallaxUV), in the
//! tangent frame, the shift turned by DF_TEXMATRIX's matrix; uv0 itself where its depth is under vParallax.z pixels. Sets
//! SampleUV0's gradients. Not in a branch of the pixel (derivatives): of the draw's constants alone.
float2 ParallaxFetch(SVertexOutput In)
{
  const float2 vDx = ddx(In.vTex0), vDy = ddy(In.vTex0);
  const float fBias = exp2(g_Frame.vMipBias.x);
  s_vParallaxDx = vDx * fBias;
  s_vParallaxDy = vDy * fBias;
  s_bParallax = true;
  const float fFade = ParallaxFade(g_DrawExt.vParallax.x, vDx, vDy, g_DrawExt.vParallax.y, g_DrawExt.vParallax.z);
  [branch]
  if (fFade <= 0)
    return In.vTex0;
  const float3 vView = ParallaxView(g_Frame.vCameraPos.xyz - In.vWorldPos, In.vTangent, In.vBinormal, In.vTNormal);
  float2 vShift = ParallaxShift(vView, g_DrawExt.vParallax.x * fFade);
  if (HasFlag(DF_TEXMATRIX))
    vShift = ParallaxTexMatrix(vShift, g_DrawExt.vTexMat0.xy, g_DrawExt.vTexMat1.xy);
  const uint nSteps = ParallaxSteps(vView.z, g_DrawExt.nParallax.z);
  // r_ParallaxFilter: the march's height over nParallax.w texels at least and its step; the textures read there by the
  // pixel's footprint.
  float2 vMarchDx = s_vParallaxDx, vMarchDy = s_vParallaxDy;
  [branch]
  if (g_DrawExt.nParallax.w != 0)
  {
    uint nWidth, nHeight;
    g_Textures[g_DrawExt.nParallax.x].GetDimensions(nWidth, nHeight);
    ParallaxMarchGrad(vShift, nSteps, g_DrawExt.nParallax.w / (float)max(max(nWidth, nHeight), 1u), vMarchDx, vMarchDy);
  }
  return ParallaxUV(In.vTex0, vMarchDx, vMarchDy, vShift, g_DrawExt.vParallax.w, nSteps);
}

//! d3d12_RTPbrReflections (nPbr.z; in a branch the same for the draw: the derivatives): the shading normal, the
//! roughness, alpha^2, the metalness and the ambient occlusion a reflection by rays takes at uv vUVf: the highlights'
//! roughness (r_PbrRoughness, nPbr.x) where the draw has them, else the material map's red, alpha^2 widened by the
//! normal's change across the pixel as the highlights' (PbrSpecAlpha2), the material map's green the metalness, its
//! blue the occlusion.
void PbrReflectMaterial(float2 vUVf, float3 vBumpN, float3 vNormal, out float3 vN, out float fRough, out float fAlpha2, out float fMetal,
                        out float fAO)
{
  const float3 vMat = SampleUV0(g_DrawExt.nPbr.z, g_DrawExt.nPbr.y, vUVf).rgb;
  vN = HasFlag(DF_BUMP) ? vBumpN : SafeNormalize(vNormal);
  const float3 vDx = ddx(vN), vDy = ddy(vN);
  fRough = g_DrawExt.nPbr.x != 0 && HasFlag(DF_SPECULAR) ? SampleUV0(g_DrawExt.nPbr.x, g_DrawExt.nPbr.y, vUVf).r : vMat.r;
  fAlpha2 = PbrSpecAlpha2(fRough, dot(vDx, vDx) + dot(vDy, vDy));
  fMetal = vMat.g;
  fAO = vMat.b;
}

//! The share of its diffuse light a surface of fRough and fMetal keeps beside its reflection by rays (nPbr.w's share).
float PbrReflectDiffuse(float fRough, float fMetal)
{
  return 1 - fMetal * PbrReflectWeight(fRough, fMetal) * (1 - asfloat(g_DrawExt.nPbr.w & ~XR_PBR_LIGHT_PASS));
}

float4 ShadePixel(SVertexOutput In)
{
#ifdef XR_AMBIENT
  s_vAmbient = 0;
#endif
#ifdef XR_PT
  s_vPTEmission = 0;
  s_vPTAlbedo = 0;
  s_vPTSpecular = 0;
  s_vPTLightmap = 0;
  s_vPTNormal = In.vNormal;
  s_fPTFog = 1;
  s_fPTSun = 1;
  s_fPTSkin = 1;
  // The side of the triangle the camera sees (before any branch or discard: derivatives); the view's direction where the
  // triangle has none (edge on).
  s_vPTFace = cross(ddx(In.vWorldPos), ddy(In.vWorldPos));
  const float fPTFace = dot(s_vPTFace, g_Frame.vCameraPos.xyz - In.vWorldPos);
  if (dot(s_vPTFace, s_vPTFace) <= 1e-20 || fPTFace == 0)
    s_vPTFace = g_Frame.vCameraPos.xyz - In.vWorldPos;
  else if (fPTFace < 0)
    s_vPTFace = -s_vPTFace;
#endif
#if defined(XR_RAY_REFL) || defined(XR_RAY_LIGHT)
  // The rays' cone and their origin's face (before any branch or discard: derivatives).
  const float3 vRayView = In.vWorldPos - g_Frame.vCameraPos.xyz;
  const float fRayDistance = length(vRayView);
  const float3 vRayDir = vRayView / max(fRayDistance, 1e-4);
  const float fPixelAngle = max(length(ddx(vRayDir)), length(ddy(vRayDir)));
  float3 vRayFace = cross(ddx(In.vWorldPos), ddy(In.vWorldPos));
  // d3d12_RTFaceNormal (nRaySun.z's bit 20): the face from the plane 1 / w makes over the screen, precise in close ups
  // where the world position's differences round (RayFace.hlsli), where it has one.
  {
    const float fRhw = 1 / In.vPos.w;
    const float4 vRayClip = mul(float4(In.vWorldPos, 1), g_Frame.mViewProj);
    const float3 vFace = FaceFromRhw(fRhw, ddx(fRhw), ddy(fRhw), vRayClip.xy / vRayClip.w, float2(2, -2) * g_Frame.vScreen.zw, g_Frame.mViewProj);
    if ((g_Frame.nRaySun.z & (1u << 20)) && !HasFlag(DF_SCREEN_POS) && vRayClip.w > 0 && dot(vFace, vFace) > 1e-20)
      vRayFace = vFace;
  }
  vRayFace = dot(vRayFace, vRayFace) > 1e-20 ? normalize(vRayFace) : SafeNormalize(In.vNormal);
  if (dot(vRayFace, vRayDir) > 0)
    vRayFace = -vRayFace;
#endif
#ifdef XR_RAY_LIGHT
  s_bRayLights = ((g_Frame.nRaySun.z >> 24) & 3) != 0;
  s_vRayFace = vRayFace;
  // The vertices' normal on the face's side the camera sees (the face where it has none).
  s_vRayNormal = dot(In.vNormal, In.vNormal) > 1e-12 ? normalize(In.vNormal) : vRayFace;
  if (dot(s_vRayNormal, vRayFace) < 0)
    s_vRayNormal = -s_vRayNormal;
  s_fRayWidth = fRayDistance * fPixelAngle;
  s_nRayLightsLeft = g_Frame.nRaySun.z >> 28;
#endif
  if (HasFlag2(DF2_SHADER_FX))
    return ShaderFx(In);
#ifdef XR_CASCADES
  // A caster of the live cascades: not where the full set has something nearer to the sun.
  if (HasFlag2(DF2_CASCADE_CASTER))
    clip(g_TextureArrays[g_Draw.nTex4].Load(int4(In.vPos.xy, g_DrawExt.vParams3.x, 0)).r + g_DrawExt.vParams3.y - In.vPos.z);
#endif
  uint nSampler0 = g_Draw.nSamplers & 15;
  uint nSampler1 = (g_Draw.nSamplers >> 4) & 15;
  // r_Parallax: uv0 where the view meets the texture's height (ParallaxFetch), for the textures at uv0 (SampleUV0).
  float2 vUVf = In.vTex0;
  [branch]
  if (g_DrawExt.nParallax.x != 0)
    vUVf = ParallaxFetch(In);

  // Bump mapping: the normal map in tangent space (vMap) and in world space (vBumpN).
  float3 vMap = float3(0, 0, 1);
  float3 vBumpN = 0;
  if (HasFlag(DF_BUMP))
  {
    vMap = SafeNormalize(SampleUV0(g_Draw.nTex3, XR_SAMPLER_LINEAR_WRAP, vUVf).rgb * 2 - 1);
    vMap.y *= g_DrawExt.vParams1.y;
    vBumpN = SafeNormalize(vMap.x * In.vTangent + vMap.y * In.vBinormal + vMap.z * In.vTNormal);
#ifdef XR_PT
    s_vPTNormal = vBumpN;
#endif
  }
  // r_PbrRoughness: the highlights' lobe by the RayMaster roughness map's red (else the material map's) where the view
  // meets it, wider where the normal changes across the pixel (PbrSpecular.hlsli).
  [branch]
  if (g_DrawExt.nPbr.x != 0 && HasFlag(DF_SPECULAR))
  {
    const float3 vN = HasFlag(DF_BUMP) ? vBumpN : SafeNormalize(In.vNormal);
    const float3 vDx = ddx(vN), vDy = ddy(vN);
    s_vSpecLobe = PbrSpecLobe(PbrSpecAlpha2(SampleUV0(g_DrawExt.nPbr.x, g_DrawExt.nPbr.y, vUVf).r, dot(vDx, vDx) + dot(vDy, vDy)));
  }

  float4 vTex = float4(1, 1, 1, 1);
  if (HasFlag(DF_CUBE0))
  {
    float3 vView = SafeNormalize(In.vWorldPos - g_Frame.vCameraPos.xyz);
    float3 vN = HasFlag(DF_BUMP) ? vBumpN : SafeNormalize(In.vNormal);
    if (HasFlag2(DF2_CUBE_SHADER))
      vTex = CubeShader(In, vView, vN, nSampler0);
    else
    {
      vTex = SampleCube(g_Draw.nTex0, nSampler0, reflect(vView, vN));
#ifdef XR_RAY_REFL
      if (((g_Frame.nRaySun.z >> 16) & 0xf) != 0)
        vTex.rgb = SurfaceRay(vTex.rgb, In.vWorldPos, vRayFace, reflect(vView, vN), fRayDistance, fPixelAngle);
#endif
    }
    if (HasFlag2(DF2_GLASS))
    {
      float4 vThrough = SampleCube(g_Draw.nTex0, nSampler0, refract(vView, vN, g_DrawExt.vParams2.w));
#ifdef XR_RAY_REFL
      if (((g_Frame.nRaySun.z >> 16) & 0xf) != 0)
        vThrough.rgb = SurfaceRay(vThrough.rgb, In.vWorldPos, vRayFace, refract(vView, vN, g_DrawExt.vParams2.w), fRayDistance, fPixelAngle);
#endif
      float fFresnel = saturate(g_DrawExt.vParams2.z + g_DrawExt.vParams2.x * pow(1 - saturate(-dot(vView, vN)), g_DrawExt.vParams2.y));
      vTex = lerp(vThrough, vTex, fFresnel);
    }
    if (HasFlag2(DF2_REFL_AMOUNT))
      vTex.rgb = vTex.rgb * SampleTexture(g_Draw.nTex5, XR_SAMPLER_LINEAR_WRAP, In.vTex0).rgb +
        SampleTexture(g_Draw.nTex4, XR_SAMPLER_LINEAR_WRAP, In.vTex0).rgb * g_DrawExt.vAddColor.rgb;
  }
  else if (HasFlag(DF_TEX0_ARRAY))
    vTex = SampleTextureArrayCoords(g_Draw.nTex0, nSampler0, In.vTex0, PictureNoise(In.vPos.xy, g_Frame.vTime.y));
  else if (HasFlag(DF_TEX0))
  {
    float2 vUV0 = vUVf;
    if (HasFlag2(DF2_BUMP_OFFSET))
      vUV0 += (SampleTexture(g_Draw.nTex3, XR_SAMPLER_LINEAR_WRAP, In.vTex1).rg * 2 - 1) * g_DrawExt.vParams0.x;
    vTex = SampleUV0(g_Draw.nTex0, nSampler0, vUV0);
    if (HasFlag2(DF2_TEX4_LAYER))
    {
      // A second layer over the texture by its alpha, moving on its own (the fixed function
      // templates' second pass); added with vAddColor.x (TemplDecalLight_SpecAdd's gloss map, lit as the texture).
      float4 vLayer = SampleUV0(g_Draw.nTex4, XR_SAMPLER_LINEAR_WRAP, vUVf + float2(g_DrawExt.vTexMat0.w, g_DrawExt.vTexMat1.w));
      if (g_DrawExt.vAddColor.x > 0)
        vTex.rgb += vLayer.rgb * g_DrawExt.vAddColor.x;
      else
        vTex = float4(lerp(vTex.rgb, vLayer.rgb, vLayer.a), vTex.a + vLayer.a - vTex.a * vLayer.a);
    }
  }
  if (HasFlag2(DF2_TERRAIN_LIGHT))
    vTex.rgb = 0.75 * vTex.rgb + 0.25;

  float fSun = HasFlag(DF_SHADOWS) ? SunVisibility(In.vWorldPos, In.vNormal, In.vPos.xy) : 1;
#ifdef XR_RAY_SUN
  float fRayWeight = 0;
  float2 vRaySun = 1;
  // The FFT caustics' own (below).
  if (HasFlag2(DF2_SUN_CASCADES) && !HasFlag(DF_CAUSTICS))
  {
    vRaySun = RaySunMask(In.vPos, fRayWeight);
    if (fRayWeight < 1)
      fSun = SunCascades(In.vWorldPos, In.vNormal, In.vPos.xy, fSun);
    fSun = lerp(fSun, HasFlag(DF_SHADOW_PASS) ? 1 : HasFlag2(DF2_CASCADES_LIVE) ? vRaySun.y : vRaySun.x, fRayWeight);
  }
#elif defined(XR_CASCADES)
  if (HasFlag2(DF2_SUN_CASCADES) && !HasFlag(DF_CAUSTICS))
    fSun = SunCascades(In.vWorldPos, In.vNormal, In.vPos.xy, fSun);
#endif
#ifdef XR_PT
  s_fPTSun = HasFlag2(DF2_NO_SUN) ? 0 : fSun;
#endif
  float4 vVertexColor = In.vColor;
  float3 vSpecular = 0;
  // d3d12_RTPbrReflections: the highlights' part of vColor (DF_SPECULAR's, the light map's lamps'), as the light map,
  // the projector and the detail texture scale it: a metal's diffuse light less beside its reflection, not them.
  float3 vHigh = 0;
#ifdef XR_AMBIENT
  // A: the ambient light's part of vVertexColor.rgb, then of vColor.rgb.
  float3 vAmb = 0;
#endif
  if (HasFlag(DF_LIGHTING) && HasFlag(DF_PIXEL_LIGHTING))
  {
    float3 vNoSunLight, vSunSpec, vOtherSpec;
    float3 vN = HasFlag(DF_BUMP) ? vBumpN : SafeNormalize(In.vNormal);
    // r_Skin (uniform per draw): the lights DrawLightingT<true>'s, the skin's highlights of the roughness by its gloss
    // (the texture's alpha), widened where the normal changes across the pixel (PbrSpecular.hlsli).
    const float fSkinGloss = g_DrawExt.vSpecular.w > 0 ? vTex.a : SKIN_GLOSS_PLAIN;
    float3 vLight;
    [branch]
    if (g_DrawExt.vSkin.x > 0)
    {
      if (HasFlag(DF_SPECULAR))
      {
        const float3 vDx = ddx(vN), vDy = ddy(vN);
        s_fSkinAlpha2 = PbrSpecAlpha2(SkinRoughness(fSkinGloss, g_DrawExt.vSkin.y, g_DrawExt.vSkin.z), dot(vDx, vDx) + dot(vDy, vDy));
      }
      vLight = DrawLightingT<true>(In.vWorldPos, vN, true, vNoSunLight, vSunSpec, vOtherSpec);
    }
    else
      vLight = DrawLighting(In.vWorldPos, vN, true, vNoSunLight, vSunSpec, vOtherSpec);
    float3 vShadowLight = vNoSunLight;
#ifdef XR_AMBIENT
    float3 vShadowAmb = s_vAmbientNoSun;
#endif
#ifdef XR_CASCADES
    // DF2_SHADOW_TINT: in the shadow the light times the tint where that is more than the light without the sun, and
    // so its ambient part.
    if (HasFlag2(DF2_SHADOW_TINT))
    {
      const float3 vTinted = vLight * g_Frame.vShadowTint.rgb;
      const float3 vMore = step(vNoSunLight, vTinted);
      vShadowLight = lerp(vNoSunLight, vTinted, vMore);
#ifdef XR_AMBIENT
      vShadowAmb = lerp(s_vAmbientNoSun, s_vAmbientLight * g_Frame.vShadowTint.rgb, vMore);
#endif
    }
#endif
    vVertexColor.rgb *= lerp(vShadowLight, vLight, fSun);
#ifdef XR_AMBIENT
    vAmb = In.vColor.rgb * lerp(vShadowAmb, s_vAmbientLight, fSun);
#endif
    if (HasFlag(DF_SPECULAR))
      vSpecular = 2 * g_DrawExt.vSpecular.rgb * (g_DrawExt.vSpecular.w > 0 ? vTex.a : 1) * (fSun * vSunSpec + vOtherSpec);
    // On skin its highlights' share by the gloss (SkinSpecularShare) times the light's scale, their lobe the gloss's.
    [branch]
    if (g_DrawExt.vSkin.x > 0 && HasFlag(DF_SPECULAR))
      vSpecular = g_DrawExt.vAmbient.w * g_DrawExt.vSpecular.rgb * SkinSpecularShare(fSkinGloss) * (fSun * vSunSpec + vOtherSpec);
  }
  else if (HasFlag(DF_LIGHTING) && HasFlag2(DF2_MATERIAL_LIGHT))
  {
    float3 vDiffuse;
    MaterialLighting(In.vWorldPos, HasFlag(DF_BUMP) ? vBumpN : SafeNormalize(In.vNormal), fSun, vDiffuse, vSpecular);
    vVertexColor.rgb = vDiffuse;
#ifdef XR_AMBIENT
    vAmb = s_vAmbientLight;
#endif
  }
  else if (HasFlag(DF_SHADOWS) && HasFlag(DF_LIGHTING))
  {
    float3 vShadowLight = In.vColorNoSun.rgb;
#ifdef XR_CASCADES
    if (HasFlag2(DF2_SHADOW_TINT))
      vShadowLight = max(vShadowLight, In.vColor.rgb * g_Frame.vShadowTint.rgb);
#endif
    vVertexColor.rgb = lerp(vShadowLight, In.vColor.rgb, fSun);
  }
#ifdef XR_AMBIENT
  // Lit per vertex: about the draw's colour times the ambient colour, not more than the light.
  if (HasFlag(DF_LIGHTING) && !HasFlag(DF_PIXEL_LIGHTING) && !HasFlag2(DF2_MATERIAL_LIGHT))
    vAmb = min(g_Draw.vColor.rgb * g_DrawExt.vAmbient.rgb, vVertexColor.rgb);
#endif

  float4 vColor;
  if (HasFlag(DF_TEX0_ALPHA_ONLY))
    vColor = float4(vVertexColor.rgb, vVertexColor.a * vTex.a);
  else
    vColor = vVertexColor * vTex;
#ifdef XR_AMBIENT
  if (!HasFlag(DF_TEX0_ALPHA_ONLY))
    vAmb *= vTex.rgb;
#endif
  vColor.rgb += vSpecular;
  vHigh = vSpecular;
#ifdef XR_FOLIAGE
  // The sun through the leaves and the grass seen against it.
  if (HasFlag2(DF2_FOLIAGE))
    vColor.rgb += vTex.rgb * g_Draw.vColor.rgb * FoliageLight(In.vWorldPos);
#endif
  if (HasFlag2(DF2_ADD_TEX4))
  {
    float4 vTex4 = SampleUV0(g_Draw.nTex4, XR_SAMPLER_LINEAR_WRAP, vUVf + float2(g_DrawExt.vTexMat0.w, g_DrawExt.vTexMat1.w));
    vColor.rgb += vTex4.rgb * g_DrawExt.vAddColor.rgb + g_DrawExt.vAddColor.w;
#ifdef XR_PT
    s_vPTEmission += vTex4.rgb * g_DrawExt.vAddColor.rgb + g_DrawExt.vAddColor.w;
#endif
    if (HasFlag2(DF2_GLASS))
      vColor.a *= vTex4.a;
  }
  if (HasFlag2(DF2_PROJECTOR))
  {
    const float4 vProjected = SampleCube(g_Draw.nTex5, XR_SAMPLER_LINEAR_CLAMP, mul(float4(In.vWorldPos, 1), g_DrawExt.mParams0).xyz);
    vColor *= vProjected;
    vHigh *= vProjected.rgb;
  }
  if (HasFlag2(DF2_MODULATE))
  {
    vColor = float4(saturate(vColor.rgb + g_DrawExt.vAddColor.w), 1);
    vHigh = 0;
  }
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
        // d3d12_LightmapDot3: as the original's light-map bump shaders, N'.D towards 1 by the light map's alpha.
        vLight *= LightmapDot3(saturate(dot(vMap, vDir.xyz * 2 - 1)), vTex1.a, g_Frame.vLightmap.x > 0) * vDir.a;
      }
#ifdef XR_AMBIENT
      // The colour is unlit here: its light is the light map's and the ambient colour under it.
      vAmb = vColor.rgb * g_DrawExt.vAmbient.rgb;
#endif
#ifdef XR_PT
      // A surface the path tracer lights keeps its colour (the albedo); the light map's light goes to its own target.
      if (g_Draw.nFlags2 & DF2_PT_MASK)
        s_vPTLightmap = vLight;
      else
#endif
      if (HasFlag2(DF2_LIGHTMAP_LIGHTS))
      {
        float3 vNoSunLight, vSunSpec, vOtherSpec;
        DrawLighting(In.vWorldPos, HasFlag(DF_BUMP) ? vBumpN : SafeNormalize(In.vNormal), true, vNoSunLight, vSunSpec, vOtherSpec);
#ifdef XR_AMBIENT
        vAmb = vColor.rgb * s_vAmbientNoSun;
#endif
        const float3 vMapLight = 4 * fSun * vLight + vNoSunLight;
        vColor.rgb *= vMapLight;
        vHigh *= vMapLight;
        // The highlights of the light map's lamps (DF_SPECULAR, d3d12_LightmapSpecular), added after it as the original's.
        if (HasFlag(DF_SPECULAR))
        {
          const float3 vMapSpec = 2 * g_DrawExt.vSpecular.rgb * (g_DrawExt.vSpecular.w > 0 ? vTex.a : 1) * vOtherSpec;
          vColor.rgb += vMapSpec;
          vHigh += vMapSpec;
        }
      }
      else
      {
        const float3 vMapLight = 4 * fSun * vLight + g_DrawExt.vAmbient.rgb;
        vColor.rgb *= vMapLight;
        vHigh *= vMapLight;
      }
    }
    else if (HasFlag(DF_TEX1_DETAIL))
    {
      vColor.rgb = 2 * vColor.rgb + vTex1.rgb - 0.5;
      vHigh *= 2;
    }
    else
    {
      vColor *= vTex1;
      vHigh *= vTex1.rgb;
    }
#ifdef XR_PT
    if (!HasFlag(DF_TEX1_LIGHTMAP | DF_TEX1_DETAIL))
      s_vPTEmission *= vTex1.rgb;
#endif
#ifdef XR_AMBIENT
    // The detail texture (the terrain's, DF2_AO_BAKED below) adds about nothing to the mean.
    if (HasFlag(DF_TEX1_DETAIL) && !HasFlag(DF_TEX1_LIGHTMAP))
      vAmb *= 2;
    else if (!HasFlag(DF_TEX1_LIGHTMAP))
      vAmb *= vTex1.rgb;
#endif
  }
  if (HasFlag2(DF2_ALPHA_GLOW))
    vColor.rgb += vTex.rgb * vTex.a;
#ifdef XR_PT
  if (HasFlag2(DF2_ALPHA_GLOW))
    s_vPTEmission += vTex.rgb * vTex.a;
#endif
  // DF2_AMBIENT_CUBE: a quarter of the ambient light is the cube reflected about the vertex normal, unlit (the path
  // tracer's surfaces keep their albedo and add it).
  if (HasFlag2(DF2_AMBIENT_CUBE))
  {
    const float3 vCube = 0.25 * SampleCube(g_Draw.nTex6, XR_SAMPLER_LINEAR_CLAMP,
                                           reflect(SafeNormalize(In.vWorldPos - g_Frame.vCameraPos.xyz), SafeNormalize(In.vNormal))).rgb;
    vColor.rgb += vCube;
#ifdef XR_PT
    s_vPTEmission += vCube;
    if (!(g_Draw.nFlags2 & DF2_PT_MASK))
#endif
      vColor.rgb -= 0.25 * vTex.rgb * g_Draw.vColor.rgb * g_DrawExt.vAmbient.rgb;
  }
  // d3d12_RTPbrReflections: a light pass or a projector's of a draw whose first pass reflects by rays (nPbr.w's
  // XR_PBR_LIGHT_PASS; Draw.cpp leaves nPbr.z 0 where the first pass has no rays): its diffuse light on a metal as much
  // less as that pass's, its highlights kept (vHigh), no ray (the reflection has the lamp's light where the last frame's
  // picture shows it).
  [branch]
  if (g_DrawExt.nPbr.z != 0 && (g_DrawExt.nPbr.w & XR_PBR_LIGHT_PASS) && ((g_Frame.nRaySun.z >> 16) & 0xf) != 0)
  {
    float3 vN;
    float fRough, fAlpha2, fMetal, fAO;
    PbrReflectMaterial(vUVf, vBumpN, In.vNormal, vN, fRough, fAlpha2, fMetal, fAO);
    const float fDiffuse = PbrReflectDiffuse(fRough, fMetal);
    vColor.rgb = max(vColor.rgb - vHigh, 0) * fDiffuse + vHigh;
#ifdef XR_AMBIENT
    vAmb *= fDiffuse;
#endif
  }
#ifdef XR_RAY_REFL
  // d3d12_RTPbrReflections (nPbr.z the RayMaster material map, its red the roughness, its green the metalness; in the
  // object's first pass): what a ray finds, its direction by the roughness's lobe (PbrReflectDir, another each
  // frame), weighted by the share the surface reflects over the lobe (PbrEnvBrdf: F0 4 % for a dielectric, the
  // texture's colour for a metal), less towards the rough (PbrReflectWeight); a metal's diffuse light less by the
  // metalness times 1 - asfloat(nPbr.w). What a ray hits lit as the last frame's picture shows it where it does
  // (d3d12_RTPbrScreen, PrevFrameAt). The roughness the highlights' (r_PbrRoughness's, its map's) where the draw has
  // them. A miss (the sky, beyond the rays' length, what is out of the view and so not in the scene for rays) the
  // draw's ambient light: the light about it the game gives it, as much as a rough surface's lobe would gather. With
  // r_PbrAO (nRaySun.z's bit 27) less where the material map's ambient occlusion (its blue) hides the lobe
  // (PbrSpecOcclusion): the pits between a metal's treads, the holes of its mesh dark, not the scene's light.
  [branch]
  if (g_DrawExt.nPbr.z != 0 && !(g_DrawExt.nPbr.w & XR_PBR_LIGHT_PASS) && ((g_Frame.nRaySun.z >> 16) & 0xf) != 0)
  {
    float3 vN;
    float fRough, fAlpha2, fMetal, fAO;
    PbrReflectMaterial(vUVf, vBumpN, In.vNormal, vN, fRough, fAlpha2, fMetal, fAO);
    const float fWeight = PbrReflectWeight(fRough, fMetal);
    const float3 vToEye = -vRayDir;
    const float fAlpha = sqrt(fAlpha2);
    // d3d12_RTPbrQuad (bit 26, the same for the draw): one ray for the 2 x 2 pixels of the quad, traced by one of its
    // lanes that reflect (not a helper: its point off the triangle), another each frame and quad (PbrQuadStart), its
    // light shared by the others whose views reflect about the same way (PbrQuadShares; each weighting it by its own
    // share), the rest tracing their own; the shared ray's cone as wide as the 2 pixels.
    const bool bQuad = (g_Frame.nRaySun.z & (1u << 26)) != 0;
    const uint nOwn = ((uint)In.vPos.x & 1) + 2 * ((uint)In.vPos.y & 1);
    uint nLane = nOwn;
    bool bOwn = true;
    if (bQuad)
    {
      const uint nBit = fWeight > 0 && s_nCoverage != 0 ? 1u << nOwn : 0u;
      const uint nMask = nBit | QuadReadAcrossX(nBit) | QuadReadAcrossY(nBit) | QuadReadAcrossDiagonal(nBit);
      nLane = PbrQuadLane(nMask, PbrQuadStart((uint2)In.vPos.xy >> 1, (uint)g_Frame.vTime.y));
      const float3 vMirror = reflect(vRayDir, vN);
      const float3 m0 = QuadReadLaneAt(vMirror, 0), m1 = QuadReadLaneAt(vMirror, 1), m2 = QuadReadLaneAt(vMirror, 2),
                   m3 = QuadReadLaneAt(vMirror, 3);
      const float3 vLane = nLane == 0 ? m0 : nLane == 1 ? m1 : nLane == 2 ? m2 : nLane == 3 ? m3 : vMirror;
      bOwn = nLane == nOwn || !PbrQuadShares(vMirror, vLane, fAlpha);
    }
    const float fConeAngle = bQuad && nLane == nOwn ? 2 * fPixelAngle : fPixelAngle;
    float3 vFound = 0;
    [branch]
    if (fWeight > 0 && bOwn)
    {
      const float2 u = float2(PictureNoise(In.vPos.xy, g_Frame.vTime.y), PictureNoise(In.vPos.yx + float2(113, 71), g_Frame.vTime.y + 29));
      const float3 vR = PbrReflectDir(vToEye, vN, fAlpha, u);
      vFound = SurfaceRayCone(g_DrawExt.vAmbient.rgb, In.vWorldPos, vRayFace, vR, fRayDistance, fConeAngle,
                              fConeAngle + PBR_REFL_SPREAD * fAlpha, true);
    }
    if (bQuad)
    {
      const float3 v0 = QuadReadLaneAt(vFound, 0), v1 = QuadReadLaneAt(vFound, 1), v2 = QuadReadLaneAt(vFound, 2),
                   v3 = QuadReadLaneAt(vFound, 3);
      if (!bOwn)
        vFound = nLane == 0 ? v0 : nLane == 1 ? v1 : nLane == 2 ? v2 : v3;
    }
    if (fWeight > 0)
    {
      const float fNV = saturate(dot(vN, vToEye));
      const float2 vAB = PbrEnvBrdf(fRough, fNV);
      float3 vShare = (lerp(PBR_REFL_DIELECTRIC, saturate(vTex.rgb * g_Draw.vColor.rgb), fMetal) * vAB.x + vAB.y) * fWeight;
      if (g_Frame.nRaySun.z & (1u << 27))
        vShare *= PbrSpecOcclusion(fNV, fAO, fAlpha);
      const float fDiffuse = PbrReflectDiffuse(fRough, fMetal);
      // The highlights (vHigh) kept: the lamps' and the sun's own lobe, not the diffuse light.
      vColor.rgb = max(vColor.rgb - vHigh, 0) * fDiffuse + vHigh + vFound * vShare;
#ifdef XR_AMBIENT
      // The ambient light's part as much less (else at most the colour: the ambient occlusion would take all a dark
      // metal's colour where its light is mostly the ambient's); the reflection not the ambient's.
      vAmb *= fDiffuse;
#endif
    }
  }
#endif

  if (HasFlag(DF_TERRAIN_CLIFF))
  {
    float4 vPos = float4(In.vWorldPos, 1);
    float2 vW = In.vCliff * g_DrawExt.vTexGen1V.xy;
    vColor.rgb *= 2;
    if (vW.x > 0)
      vColor.rgb *= lerp(1, 2 * SampleTexture(g_Draw.nTex2, XR_SAMPLER_LINEAR_WRAP, float2(dot(vPos, g_DrawExt.vParams0), dot(vPos, g_DrawExt.vParams1))).rgb, vW.x);
    if (vW.y > 0)
      vColor.rgb *= lerp(1, 2 * SampleTexture(g_Draw.nTex3, XR_SAMPLER_LINEAR_WRAP, float2(dot(vPos, g_DrawExt.vParams2), dot(vPos, g_DrawExt.vParams3))).rgb, vW.y);
  }
#ifdef XR_AMBIENT
  // A baked light's ambient part: the share of it the shadow leaves (the terrain's in the shadow).
  if (HasFlag2(DF2_AO_BAKED))
    vAmb = vColor.rgb * g_Frame.vMipBias.yzw;
#endif
#if defined(XR_CASCADES) && !defined(XR_PT)
  if (HasFlag2(DF2_SHADOW_TINT) && !HasFlag(DF_LIGHTING))
    vColor.rgb *= lerp(g_Frame.vShadowTint.rgb, 1, fSun);
#endif

  if (HasFlag(DF_DECAL_FADE))
  {
    float r = saturate(2 * length(In.vTex0 - 0.5));
    vColor.a *= 1 - 0.5 * (r + smoothstep(0, 1, r));
  }
  if (HasFlag2(DF2_GLOW_FALLOFF))
    vColor.rgb *= pow(saturate(1 - In.vTex0.x), g_DrawExt.vParams0.x);
#ifdef XR_HDR
  if (HasFlag2(DF2_SKY_GLOW))
    vColor.rgb *= SkyGlow(In.vWorldPos);
  if (HasFlag2(DF2_HDR_CORE))
  {
    float m = max(vTex.r, max(vTex.g, vTex.b)) * (g_DrawExt.vHdrCore.y > 0 ? vTex.a : 1);
    if (HasFlag2(DF2_GLOW_FALLOFF))
      m *= pow(saturate(1 - In.vTex0.x), g_DrawExt.vParams0.x);
    vColor.rgb *= HdrEncode(max(1 + (g_DrawExt.vHdrCore.x - 1) * pow(saturate(m), HDR_CORE_POWER), 0));
  }
#endif

  if (HasFlag(DF_ALPHATEST))
  {
    if (HasFlag(DF_ALPHATEST_LESS))
      clip(g_Draw.fAlphaRef - vColor.a - 1.0 / 512);
    // Alpha to coverage (the reference negated, Draw.cpp's DrawGeometry): the samples the edge covers in place of the test.
    else if (g_Draw.fAlphaRef < 0)
      vColor.a = CoverageAlpha(vColor.a, -g_Draw.fAlphaRef, fwidth(vColor.a));
    else
      clip(vColor.a - g_Draw.fAlphaRef);
  }
  if (HasFlag(DF_ALPHA_ONE))
    vColor.a = 1;
  // An object fading in or out by its distance (d3d12_FadeBlend): its alpha alone after the alpha test (FadeAlpha).
  vColor.a = FadeAlpha(vColor.a, g_DrawExt.vEdgeFade.y);
  if (HasFlag2(DF2_FOG_VOLUME))
    return float4(g_Draw.vColor.rgb, FogVolumeAlpha(In.vWorldPos) * (g_DrawExt.vParams0.w > 0 ? vColor.a : 1));
#ifdef XR_AMBIENT
  if (HasFlag2(DF2_AO_ALL))
    vAmb = vColor.rgb;
  vAmb = min(vAmb, max(vColor.rgb, 0));
#endif
#ifdef XR_PT
  // A surface the path tracer lights: its albedo; what it adds unlit, its colour.
  if (g_Draw.nFlags2 & DF2_PT_MASK)
  {
    s_vPTAlbedo = max(vColor.rgb - s_vPTEmission, 0);
    s_vPTSpecular = saturate(g_DrawExt.vSpecular.rgb * (g_DrawExt.vSpecular.w > 0 ? vTex.a : 1));
    // r_Skin: its highlights' colour by the share its gloss leaves, their roughness by the gloss into the light map
    // target's alpha (CSSpecular's SkinSpecular; the composite's sun SkinWrap's).
    [branch]
    if (g_DrawExt.vSkin.x > 0)
    {
      const float fGloss = g_DrawExt.vSpecular.w > 0 ? vTex.a : SKIN_GLOSS_PLAIN;
      const float3 vN = SafeNormalize(s_vPTNormal);
      const float3 vDx = ddx(vN), vDy = ddy(vN);
      s_vPTSpecular = saturate(g_DrawExt.vSpecular.rgb * SkinSpecularShare(fGloss));
      s_fPTSkin = PathGBufferSkin(sqrt(sqrt(PbrSpecAlpha2(SkinRoughness(fGloss, g_DrawExt.vSkin.y, g_DrawExt.vSkin.z),
                                                          dot(vDx, vDx) + dot(vDy, vDy)))));
    }
    vColor.rgb = s_vPTEmission;
  }
#endif

  float3 vFogColor = HasFlag2(DF2_FOG_BLACK) ? 0 : HasFlag2(DF2_MODULATE) ? 1 : g_Frame.vFogColor.rgb;
  if (HasFlag(DF_DETAIL_LAYER))
  {
    float fDist = distance(In.vWorldPos, g_Frame.vCameraPos.xyz);
    float fWeight = vColor.a * saturate((g_DrawExt.vParams0.y - fDist) * g_DrawExt.vParams0.z);
    float3 vDetail = vTex.rgb;
    if (HasFlag(DF_DETAIL_BUMP))
    {
      float3 vN = SafeNormalize(In.vNormal);
      float3 vT = SafeNormalize(g_DrawExt.vTexGenU.xyz - vN * dot(g_DrawExt.vTexGenU.xyz, vN));
      float3 vB = cross(vN, vT);
      if (dot(vB, g_DrawExt.vTexGenV.xyz) < 0)
        vB = -vB;
      float3 vMap = SampleTexture(g_Draw.nTex1, nSampler1, In.vTex0).rgb * 2 - 1;
      float3 vBump = SafeNormalize(vMap.x * vT + vMap.y * g_DrawExt.vParams1.y * vB + vMap.z * vN);
      float3 vL = g_Frame.vSunDir.xyz;
      float fN = dot(vN, vL);
      vDetail *= clamp(1 + (dot(vBump, vL) - fN) / (g_DrawExt.vParams1.x + saturate(fN)), 0, 2);
    }
    vColor = float4(lerp(0.5, vDetail, fWeight), 1);
    vFogColor = 0.5;
  }

  if (HasFlag(DF_CAUSTICS) && g_DrawExt.vParams2.x > 0)
  {
    // The FFT ocean's (d3d12_WaterCaustics): each caustics cascade's light at the floor's depth where the sun's refracted
    // ray crossed the surface, blurred by the sun's disc (the gradients at least its width there), its mean where a pixel
    // takes more than its last mip level's texel, their product; the sun's share of the floor's light times it, in the
    // sun's light, fading out with the depth. The sun's visibility only where the light changes (above the water, below
    // the fade and far off none): one bilinear comparison in the cascades (the rays' mask with the rays' shadows).
    const float fDepth = max(g_DrawExt.vParams0.x - In.vWorldPos.z, 0);
    const float2 vP = In.vWorldPos.xy + g_DrawExt.vParams1.xy * fDepth;
    const Texture2DArray Caustics = g_TextureArrays[g_Draw.nTex1];
    float fLight = 1;
    [unroll]
    for (uint i = 0; i < XR_CAUSTICS_CASCADES; i++)
    {
      const float2 uv = vP * g_DrawExt.vParams2[i], dx = ddx(uv), dy = ddy(uv);
      const float fBlur = fDepth * CAUSTICS_SUN_ANGLE * g_DrawExt.vParams2[i];
      const float fSize = max(max(length(dx), length(dy)), 1e-9), fGrow = max(1, fBlur / fSize);
      const float fCascade = CausticsAtDepth(Caustics.SampleGrad(g_SamplerLinearWrap, float3(uv, i), dx * fGrow, dy * fGrow), g_DrawExt.vParams3[i], fDepth);
      fLight *= lerp(1, fCascade, CausticsFar(fSize * fGrow * XR_OCEAN_SIZE));
    }
    const float fFade = saturate(1 - fDepth / g_DrawExt.vParams1.z);
    float fLit = 1;
    [branch]
    if (fLight != 1 && fFade > 0 && HasFlag2(DF2_SUN_CASCADES))
    {
#ifdef XR_RAY_SUN
      float fWeight = 0;
      const float2 vMask = RaySunMask(In.vPos, fWeight);
      fLit = lerp(fWeight < 1 ? CascadeTapNormal(In.vWorldPos, In.vNormal) : 1, vMask.x, fWeight);
#elif defined(XR_CASCADES)
      fLit = CascadeTapNormal(In.vWorldPos, In.vNormal);
#endif
    }
    const float k = g_DrawExt.vParams0.w * saturate(fLit) * fFade;
    vColor = float4((0.5 * CausticsFactor(fLight, k, g_DrawExt.vParams1.w)).xxx, 1);
    vFogColor = 0.5;
  }
  else if (HasFlag(DF_CAUSTICS))
  {
    float fDepth = g_DrawExt.vParams0.x - In.vWorldPos.z;
    float2 vP = (In.vWorldPos.xy + g_DrawExt.vParams1.xy * fDepth) * g_DrawExt.vParams0.y;
    float t = g_DrawExt.vParams0.z;
    float c1 = SampleTexture(g_Draw.nTex0, XR_SAMPLER_LINEAR_WRAP, vP + float2(0.031, 0.017) * t).r;
    float c2 = SampleTexture(g_Draw.nTex0, XR_SAMPLER_LINEAR_WRAP, vP * 1.37 + float2(-0.023, 0.029) * t).r;
    float fLight = min(c1, c2) * g_DrawExt.vParams1.w;
    float fFade = saturate(fDepth) * saturate(1 - fDepth / g_DrawExt.vParams1.z);
    vColor = float4(saturate(0.5 + 0.5 * g_DrawExt.vParams0.w * fFade * (fLight - 1)).xxx, 1);
    vFogColor = 0.5;
  }
#ifdef XR_PT
  // A pass that multiplies the picture multiplies the albedo and the highlights as much (not fogged).
  if (HasFlag(DF_DETAIL_LAYER | DF_CAUSTICS) || HasFlag2(DF2_MODULATE))
    s_vPTAlbedo = s_vPTSpecular = vColor.rgb;
#endif

  if (HasFlag2(DF2_HEAT))
  {
    float3 vN = HasFlag(DF_BUMP) ? vBumpN : SafeNormalize(In.vNormal);
    float fFacing = dot(vN, vN) > 0 ? abs(dot(vN, SafeNormalize(g_Frame.vCameraPos.xyz - In.vWorldPos))) : 1;
    vColor.rgb = HeatVision(vColor.rgb, fFacing);
  }

  if (HasFlag(DF_FOG) && g_Frame.vFogParams.w != 0)
  {
    vColor.rgb = lerp(vFogColor, vColor.rgb, SceneFog(In.vWorldPos));
#ifdef XR_AMBIENT
    vAmb *= SceneFog(In.vWorldPos);
#endif
#ifdef XR_PT
    s_fPTFog = SceneFog(In.vWorldPos);
#endif
  }
  // Under the ocean's surface, a blended object drawn after the light in its water (Underwater.cpp): that light over it, its
  // colour attenuated along its path through the water and the light's way down to it, the light the water scatters in front
  // of it added (by its alpha in the blend: over the water behind, already so, exactly). The light the water scatters as the
  // view's (Underwater.hlsl's UnderSource: vUnder.w over 100 with d3d12_WaterUnderClear, from the scene's sun and sky).
  [branch]
  if (g_DrawExt.vUnder.w > 0)
  {
    const float3 vToPixel = In.vWorldPos - g_Frame.vCameraPos.xyz;
    const float fDist = length(vToPixel);
    const float3 vDeep = float3(HdrDecode(g_DrawExt.vUnderColor.r), HdrDecode(g_DrawExt.vUnderColor.g), HdrDecode(g_DrawExt.vUnderColor.b));
    const float3 c = VolumeAttenuation(vDeep, g_DrawExt.vUnderColor.w);
    const bool bClear = g_DrawExt.vUnder.w > 100;
    const float fUnder = bClear ? g_DrawExt.vUnder.w - 100 : g_DrawExt.vUnder.w;
    float3 vSource = UnderDeepSource(vDeep, c);
    if (bClear)
    {
      const float3 vSun = 2 * g_Frame.vSunColor.rgb, vSky = g_Frame.vFogColor.rgb;
      vSource = UnderClearSource(c, UnderDownLight(float3(HdrDecode(vSun.r), HdrDecode(vSun.g), HdrDecode(vSun.b)),
                                                   float3(HdrDecode(vSky.r), HdrDecode(vSky.g), HdrDecode(vSky.b)), g_Frame.vSunDir.z));
    }
    float3 T;
    const float3 vScatter = UnderFog(vSource, c, max(g_DrawExt.vUnder.x - g_Frame.vCameraPos.z, 0), vToPixel / max(fDist, 1e-4), fDist,
                                     max(g_DrawExt.vUnder.x - In.vWorldPos.z, 0), g_DrawExt.vUnder.y, g_Frame.vSunDir.xyz, g_DrawExt.vUnder.z, T);
    // The mode, and d3d12_WaterDebug 31-33 by tens (the view's terms: an added light none).
    const bool bOver = fmod(fUnder, 10) < 1.5;
    const float fDebug = floor(fUnder / 10);
    const float3 v = fDebug == 1 ? (bOver ? vScatter : 0)
                     : fDebug == 2 ? (bOver ? float3(1, 1, 1) * HdrDecode(0.5) : 0)
                     : fDebug == 3 ? (bOver ? float3(HdrDecode(T.r), HdrDecode(T.g), HdrDecode(T.b)) : 0)
                     : float3(HdrDecode(vColor.r), HdrDecode(vColor.g), HdrDecode(vColor.b)) * T + (bOver ? vScatter : 0);
    vColor.rgb = float3(HdrEncode(v.r), HdrEncode(v.g), HdrEncode(v.b));
  }
#ifdef XR_AMBIENT
  // A pass that multiplies the picture multiplies A as much.
  if (HasFlag(DF_DETAIL_LAYER | DF_CAUSTICS) || HasFlag2(DF2_MODULATE))
    vAmb = vColor.rgb;
  s_vAmbient = vAmb;
#endif

  if (HasFlag(DF_SHADOW_DEBUG))
    vColor = float4(fSun, fSun, fSun, 1);
#ifdef XR_CASCADES
  if (HasFlag2(DF2_SUN_CASCADES) && g_Frame.vShadowTint.w >= 3 && !HasFlag(DF_SHADOW_PASS | DF_CAUSTICS))
    vColor.rgb = CascadeDebug(In.vWorldPos, In.vNormal, In.vPos.xy, vColor.rgb);
#endif
#ifdef XR_RAY_SUN
  if (HasFlag2(DF2_SUN_CASCADES) && g_Frame.vShadowTint.w >= 4 && !HasFlag(DF_SHADOW_PASS | DF_CAUSTICS) && fRayWeight > 0)
    vColor.rgb = lerp(vColor.rgb, float3(1 - vRaySun.x, 1 - vRaySun.y, 0.6) * 0.8 + 0.2 * dot(saturate(vColor.rgb), float3(0.299, 0.587, 0.114)), fRayWeight);
#endif
  return vColor;
}

#ifdef XR_PT
struct SPixelOutput
{
  float4 vColor : SV_Target0;
#ifdef XR_VELOCITY
  float2 vMotion : SV_Target1;
  float fDepth : SV_Target2;
#endif
  float4 vAlbedo : SV_Target3;    //!< the albedo, with the colour's alpha for the blend
  float4 vNormal : SV_Target4;    //!< the normal, the fog's share, the path material (PathGBufferNormal)
  float4 vSun : SV_Target5;       //!< r the sun's visibility, gba the game's ambient light / 2 (the draw's vAmbient)
  float4 vSpecular : SV_Target6;  //!< the highlights' colour / 2, with the colour's alpha for the blend
  float4 vLightmap : SV_Target7;  //!< the light map's light / 4 (rgb; 0 without one), a the skin (PathGBufferSkin)
};

SPixelOutput PSMain(XR_PS_INPUT)
{
  XR_PS_COVERAGE
  SPixelOutput Out;
  Out.vColor = SceneOutput(ShadePixel(In));
#ifdef XR_VELOCITY
  Out.vMotion = MotionVector(In.vCurClip, In.vPrevClip);
  Out.fDepth = In.vPos.z;
#endif
  Out.vAlbedo = float4(max(s_vPTAlbedo, 0), Out.vColor.a);
  // The vegetation's opaque parts (a trunk, its branches: DF_VCOLOR_X2 without the alpha test, XR_PT_VEGETATION_MATERIAL):
  // the vertices' normals are the crown's, straight up, the triangle's own instead (a trunk's paths went up into the
  // crown, or, the normal turned to the camera's side, down into the ground).
  if (HasFlag(DF_VCOLOR_X2) && !HasFlag(DF_ALPHATEST))
    s_vPTNormal = s_vPTFace;
  Out.vNormal = PathGBufferNormal(s_vPTNormal, s_vPTFace, s_fPTFog, (g_Draw.nFlags2 & DF2_PT_MASK) >> DF2_PT_SHIFT);
  float3 vAmbient = g_DrawExt.vAmbient.rgb;
  // A lit object's fill lights (Draw3D.cpp SetPathFillLights: directional, the characters' light from above), as the
  // raster lights it with them, unshadowed: in these units 2 * colour * N.L.
  const float3 vPTN = SafeNormalize(s_vPTNormal);
  for (uint i = 0; i < min(g_Draw.nLights, XR_MAX_DRAW_LIGHTS); i++)
  {
    if (g_DrawExt.vLightPos[i].w == 2)
      vAmbient += 2 * g_DrawExt.vLightColor[i].rgb * saturate(dot(vPTN, g_DrawExt.vLightPos[i].xyz));
  }
#ifdef XR_CASCADES
  // A lit object in the cascades' shadow (DF2_SHADOW_TINT): the ambient light the paths fill in with at least its lit
  // light times the tint where the sun is hidden, as the raster's shadow. In these units (the albedo s / 2 of the colour,
  // the ambient 2 / s of the game's) the lit light is 2 saturate(ambient / 2 + sun N.L).
  if (HasFlag2(DF2_SHADOW_TINT))
  {
    const float fNL = saturate(dot(SafeNormalize(s_vPTNormal), g_Frame.vSunDir.xyz));
    const float3 vLit = 2 * saturate(0.5 * vAmbient + g_Frame.vSunColor.rgb * fNL);
    vAmbient = lerp(max(vAmbient, vLit * g_Frame.vShadowTint.rgb), vAmbient, s_fPTSun);
  }
#endif
  Out.vSun = float4(s_fPTSun, saturate(0.5 * vAmbient));
  Out.vSpecular = float4(saturate(s_vPTSpecular), Out.vColor.a);
  Out.vLightmap = float4(saturate(s_vPTLightmap), s_fPTSkin);
  return Out;
}
#elif defined(XR_VELOCITY)
struct SPixelOutput
{
  float4 vColor : SV_Target0;
  float2 vMotion : SV_Target1;
  float fDepth : SV_Target2;
#ifdef XR_AMBIENT
  float4 vAmbient : SV_Target3;   //!< A, with the colour's alpha for the blend
#endif
};

SPixelOutput PSMain(XR_PS_INPUT)
{
  XR_PS_COVERAGE
  SPixelOutput Out;
  Out.vColor = SceneOutput(ShadePixel(In));
  Out.vMotion = MotionVector(In.vCurClip, In.vPrevClip);
  Out.fDepth = In.vPos.z;
#ifdef XR_AMBIENT
  Out.vAmbient = float4(s_vAmbient, Out.vColor.a);
#endif
  return Out;
}
#elif defined(XR_AMBIENT)
struct SPixelOutput
{
  float4 vColor : SV_Target0;
  float4 vAmbient : SV_Target3;
};

SPixelOutput PSMain(XR_PS_INPUT)
{
  XR_PS_COVERAGE
  SPixelOutput Out;
  Out.vColor = SceneOutput(ShadePixel(In));
  Out.vAmbient = float4(s_vAmbient, Out.vColor.a);
  return Out;
}
#else
float4 PSMain(XR_PS_INPUT) : SV_Target
{
  XR_PS_COVERAGE
  return SceneOutput(ShadePixel(In));
}
#endif
