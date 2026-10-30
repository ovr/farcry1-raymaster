// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Constants.h
//	Description: The constant buffers of the shaders, shared by C++ and
//	HLSL (included by both).
//
//	Root signature (D3D12Pipeline.h):
//	  b0  SFrameConstants, per view (camera, sun, fog, time)
//	  b1  SDrawConstants, per draw
//	  t0, space1: Texture2D   g_Textures[]  (the texture table, D3D12Descriptors.h)
//	  t0, space2: TextureCube g_Cubes[]     (the same table)
//	  s0 linear wrap (anisotropic), s1 linear clamp, s2 point wrap, s3 point clamp (static)
//	  s4 the shadow maps' comparison sampler (LESS_EQUAL, bilinear, white border: lit outside)
//	Matrices are row major and multiply row vectors (v * M), as Matrix44 of the engine; dxc
//	compiles with -Zpr, so C++ copies a Matrix44 as it is.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_CONSTANTS_H
#define XRENDERD3D12_CONSTANTS_H

#ifdef __cplusplus
typedef float float4x4_t[16];
#define XR_FLOAT4X4(name) float name[16]
#define XR_FLOAT4X4_ARRAY(name, n) float name[n][16]
#define XR_FLOAT4(name) float name[4]
#define XR_UINT4(name) uint32 name[4]
#define XR_FLOAT4_ARRAY(name, n) float name[n][4]
#define XR_UINT(name) uint32 name
#define XR_FLOAT(name) float name
#else
#define XR_FLOAT4X4(name) float4x4 name
#define XR_FLOAT4X4_ARRAY(name, n) float4x4 name[n]
#define XR_FLOAT4(name) float4 name
#define XR_UINT4(name) uint4 name
#define XR_FLOAT4_ARRAY(name, n) float4 name[n]
#define XR_UINT(name) uint name
#define XR_FLOAT(name) float name
#endif

//! The most lights SDrawConstants holds besides the sun.
#define XR_MAX_DRAW_LIGHTS 16
//! The most shadow maps a draw samples (DF_SHADOWS).
#define XR_MAX_DRAW_SHADOWS 4
//! The most cascades of the sun's shadow (DF2_SUN_CASCADES).
#define XR_MAX_CASCADES 4

// SDrawConstants::nFlags
#define DF_TEX0             0x00000001  //!< sample g_Textures[nTex0] (else white)
#define DF_TEX1             0x00000002  //!< second texture g_Textures[nTex1] with uv1 (lightmap, detail)
#define DF_VCOLOR           0x00000004  //!< multiply by the vertex colour (COLOR0; the default stream is white)
#define DF_VCOLOR_X2        0x00000008  //!< the vertex colour times 2 (vegetation)
#define DF_LIGHTING         0x00000010  //!< per vertex s * (saturate(ambient / s + sun N.L + light 0) + saturate(each other light)), s = vAmbient.w (2: overbright, 1: none), normal from NORMAL
#define DF_ALPHATEST        0x00000020  //!< clip when alpha < fAlphaRef (GREATER0: fAlphaRef 1/255)
#define DF_TEXGEN_WORLD     0x00000040  //!< uv0 = (dot(world pos, vTexGenU), dot(world pos, vTexGenV))
#define DF_TEXMATRIX        0x00000080  //!< uv0 = (uv0, 1) * the 2x3 texture matrix vTexMat0/vTexMat1
#define DF_FOG              0x00000100  //!< linear fog from SFrameConstants
#define DF_TEX1_DETAIL      0x00000200  //!< rgb = 2 * rgb + tex1 - 0.5 (terrain detail texture); else colour *= tex1
#define DF_TEX1_LIGHTMAP    0x00000400  //!< colour = tex0 * (4 * lightmap + ambient) (lightmapped objects)
#define DF_ALPHA_FROM_TEX   0x00000800  //!< alpha = texture alpha only (no vertex or constant alpha)
#define DF_CUBE0            0x00001000  //!< tex0 is g_Cubes[nTex0], sampled with the view vector reflected about the normal (N' with DF_BUMP), world space
#define DF_TEX0_ALPHA_ONLY  0x00002000  //!< rgb = colour, a = texture alpha * colour alpha (fonts)
#define DF_SCREEN_POS       0x00004000  //!< POSITION is in pixels (x, y from the top left, z); mViewProj is ignored
#define DF_ALPHATEST_LESS   0x00008000  //!< with DF_ALPHATEST: clip when alpha >= fAlphaRef instead (GS_ALPHATEST_LESS128)
#define DF_TEXGEN1_WORLD    0x00010000  //!< uv1 = (dot(world pos, vTexGen1U), dot(world pos, vTexGen1V)) (terrain detail)
#define DF_ALPHA_ONE        0x00020000  //!< alpha 1 after the alpha test (the pictures of the far vegetation sprites)
#define DF_TEX0_ARRAY       0x00040000  //!< tex0 is g_TextureArrays[nTex0]: slice floor(u / 2), sampled at (u - 2 * slice, v)
#define DF_TERRAIN_CLIFF    0x00100000  //!< rgb = 2 * rgb * lerp(1, 2 * tex2, w.x) * lerp(1, 2 * tex3, w.y), w = vertex colour rg * vTexGen1V.xy; uv of tex2 = (dot(world pos, vParams0), dot(.., vParams1)), of tex3 with vParams2 / 3
#define DF_DETAIL_BUMP      0x00200000  //!< with DF_DETAIL_LAYER: tex0 *= clamp(1 + (sun N'.L - sun N.L) / (vParams1.x + saturate(N.L)), 0, 2), N' = the normal map tex1 (uv0) in the frame of N and the texgen rows, its y times vParams1.y
#define DF_DECAL_FADE       0x00400000  //!< a *= 1 - f(r), r = saturate(2 * |uv0 - 0.5|), f(r) = (r + smoothstep(0, 1, r)) / 2: a decal fading out from its centre to a circle
#define DF_SHADOWS          0x00800000  //!< the sun is shadowed by the draw's shadow maps (SunVisibility): DF_LIGHTING lerps to the vertex light without the sun, DF_TEX1_LIGHTMAP scales the lightmap by the visibility
#define DF_SHADOW_PASS      0x01000000  //!< with DF_SHADOWS: rgb = colour * tex0, a = 1 - visibility (a shadow blended over the terrain)
#define DF_SHADOW_DEBUG     0x02000000  //!< with DF_SHADOWS: the result is the sun's visibility in grey (d3d12_ShadowDebug 2)
#define DF_CAUSTICS         0x10000000  //!< rgb = 0.5 + 0.5 * k * (I - 1), I the caustics (tex0, see General.hlsl), for 2 * dst * src
#define DF_VALPHA_ONLY      0x08000000  //!< with DF_VCOLOR: only the vertex colour's alpha counts (rgb white)
#define DF_LIGHTMAP_DIR     0x04000000  //!< with DF_TEX1_LIGHTMAP, a dot3 light map: the light map times saturate(D.z) * D.a, D = tex2 at uv1 (rgb * 2 - 1: the light's dominant direction in the surface's tangent space, a: its strength)
#define DF_DETAIL_LAYER     0x00080000  //!< rgb = lerp(0.5, tex0, colour a * saturate((vParams0.y - d) * vParams0.z)), a = 1, fogged towards 0.5 (d: distance to the camera)
#define DF_PIXEL_LIGHTING   0x40000000  //!< with DF_LIGHTING: lit per pixel (each light's colour clamped to 1, point lights falling off as saturate(1 - d * vLightAtten.w); DF2_VERTEX_LIGHTS: as per vertex), with the vertex normal or N' (DF_BUMP)
#define DF_SPECULAR         0x80000000  //!< with DF_PIXEL_LIGHTING: + 2 * vSpecular.rgb * gloss * sum of the lights' specular colour (clamped to 1) * falloff * saturate(R.V)^32, R the light reflected about N', where N'.L > 0
#define DF_BUMP             0x20000000  //!< bump mapping: N' = the normal map tex3 at uv0 (rgb * 2 - 1, its green times vParams1.y) in the vertex's tangent frame (TANGENT, BINORMAL, TNORMAL) for DF_PIXEL_LIGHTING; DF_LIGHTMAP_DIR takes saturate(dot(N', D)) for D.z

// SDrawConstants::nFlags2
#define DF2_ADD_TEX4        0x00000001  //!< rgb += tex4 (g_Textures[nTex4] at uv0) * vAddColor.rgb + vAddColor.w: a glow map or a texture added, unlit
#define DF2_GLASS           0x00000002  //!< with DF_CUBE0: lerp(cube(T), cube(R), F), T the view vector refracted with the ratio vParams2.w, F = saturate(vParams2.z + vParams2.x * (1 - N.V)^vParams2.y); with DF2_ADD_TEX4 a *= tex4 a
#define DF2_NO_SUN          0x00000004  //!< with DF_LIGHTING: the draw's lights without the sun (a light pass added over the object)
#define DF2_FOG_BLACK       0x00000008  //!< with DF_FOG: fogged towards black (an additive pass)
#define DF2_FOG_VOLUME      0x00000010  //!< the fog of a fog volume over an object in it: rgb = vColor.rgb, a = FogVolumeAlpha (General.hlsl; vParams0: x the layer's height, y ln 100 / (2 * fog distance), z 1 for the volume's cap, w 1: times the alpha after the alpha test)
#define DF2_HEAT            0x00000020  //!< heat vision: the colour before the fog through HeatVision (Common.hlsli) with vHeat
#define DF2_SCOPE_LENS      0x00000040  //!< the glass of a scope: the scene copy tex0 around the middle of the screen (ScopeLens, General.hlsl); nothing else applies
#define DF2_MODULATE        0x00000080  //!< a decal multiplied into the frame (DESTCOLOR, ZERO): rgb = saturate(rgb + vAddColor.w), a = 1, fogged towards white
#define DF2_PROJECTOR       0x00000100  //!< a projected light's pass: the colour (with the specular) times the cube g_Cubes[nTex5] at float4(world pos, 1) * mParams0 (the light's m_TextureMatrix)
#define DF2_TERRAIN_LIGHT   0x00000200  //!< the terrain under dynamic lights: tex0 rgb as 0.75 * rgb + 0.25; with DF_PIXEL_LIGHTING point lights fall off as saturate(1 - (d * vLightAtten.w)^2) (linearly with DF2_PROJECTOR)
#define DF2_MATERIAL_LIGHT  0x00000400  //!< with DF_LIGHTING: lit per pixel as the fixed function lights a material: saturate(vAmbient.rgb + vColor.rgb * the lights' colour * N.L) * texture + saturate(vSpecular.rgb * the lights' specular colour * (N.H)^vParams3.x)
#define DF2_GLOW_FALLOFF    0x00000800  //!< rgb *= saturate(1 - uv0.x)^vParams0.x: a glow fading out from its inner edge (u 0) to its outer edge (u 1)
#define DF2_REFL_AMOUNT     0x00001000  //!< with DF_CUBE0: tex0 rgb = cube * tex5 at uv0 (the reflect amount) + tex4 at uv0 * vAddColor.rgb (the texture), lit as tex0
#define DF2_ALPHA_GLOW      0x00002000  //!< rgb += tex0 rgb * tex0 a after the light and the light map: the texture's alpha is a glow mask, unlit
#define DF2_BUMP_OFFSET     0x00004000  //!< with DF_TEX0: tex0 looked up at uv0 + (tex3 at uv1 rg * 2 - 1) * vParams0.x, the texture wobbled by a normal map (the beach foam)
#define DF2_SKY_GLOW        0x00008000  //!< the sky with r_HDRRendering: rgb *= SkyGlow (Common.hlsli) in the direction of the world position
#define DF2_HDR_CORE        0x00010000  //!< a light added with r_HDRRendering: rgb *= HdrEncode(1 + (vHdrCore.x - 1) * m^4) (Common.hlsli), m its shape 0..1 (tex0's largest channel, times tex0 a with vHdrCore.y, times DF2_GLOW_FALLOFF): brighter in its core, its soft edge as the game's
#define DF2_VERTEX_LIGHTS   0x00020000  //!< with DF_PIXEL_LIGHTING: the lights as the vertex lighting takes them, per pixel: colours not clamped, a point light's attenuation 1 / (vLightAtten.x + vLightAtten.y d) up to vLightAtten.z (d3d12_PixelLighting)
#define DF2_SMOOTH_FALLOFF  0x00040000  //!< with DF2_VERTEX_LIGHTS: a point light falls off as 2 (1 - smoothstep(0, 0.9, d * vLightAtten.w)) instead (static objects: the falloff the per vertex attenuation is fitted to)
#define DF2_LIGHTMAP_LIGHTS 0x00080000  //!< with DF_TEX1_LIGHTMAP (no DF_LIGHTING): the draw's lights without the sun (per pixel, DF2_VERTEX_LIGHTS) in place of the ambient under the light map (d3d12_PixelLighting)
#define DF2_SUN_CASCADES    0x00100000  //!< the sun's visibility from the cascades (SunCascades, Common.hlsli) where they reach, the shadow maps' (DF_SHADOWS) beyond; with DF_SHADOW_PASS the shadow maps' fade out where the cascades reach
#define DF2_CASCADES_LIVE   0x00200000  //!< with DF2_SUN_CASCADES: the cascades of the live casters only (light maps, the terrain)
#define DF2_SHADOW_TINT     0x00400000  //!< with DF2_SUN_CASCADES: rgb *= lerp(vShadowTint, 1, the visibility) (the terrain)
#define DF2_CASCADE_CASTER  0x00800000  //!< a caster of the live cascades: drawn only where it is not behind the full set's map g_TextureArrays[nTex4] slice vParams3.x by more than vParams3.y (depth)

// SDrawConstants::nFlags of the post program (Post.hlsl): one mode, with POST_EFFECTS the effects drawn
#define POST_DOWNSAMPLE     0x00000001  //!< the mean of tex0 over the target's pixel: vParams0.z x vParams0.z bilinear taps
#define POST_BLUR           0x00000002  //!< a gaussian of tex0 (sigma vParams1.z texels) along vParams1.xy (one texel in uv)
#define POST_EFFECTS        0x00000004  //!< the screen effects over the scene copy tex0 (Post.hlsl)
#define POST_BLEND          0x00000008  //!< tex0 (point sampled at uv vParams0.xy per pixel) times vParams1, for a blended draw
#define POST_NIGHTVISION    0x00000010
#define POST_SCREENBLUR     0x00000020
#define POST_FLASHBANG      0x00000040
#define POST_FADE           0x00000080
#define POST_SUN_PATCH      0x00000100  //!< 1 where the depth copy tex0 is the far plane, around the sun (SunPatch, Post.hlsl)
#define POST_SUN_MASK       0x00000200  //!< the sun's mask: the patch tex0 blurred towards its middle (SunMask, Post.hlsl)
#define POST_DEPTH_VIEW     0x00000400  //!< the depth tex0 as grey: the distance from vParams1.x (near, black) to vParams1.y (far, white), log scale

// SDrawConstants::nFlags of the HDR program (Hdr.hlsl, Hdr.cpp): one mode
#define HDR_LUMINANCE       0x00000001  //!< the meter's first level: (w * log2 L, w) of the scene tex0 over the target's texel, w the centre weight
#define HDR_REDUCE          0x00000002  //!< the mean of tex0 over the target's texel: vParams0.z x vParams0.z bilinear taps
#define HDR_ADAPT           0x00000004  //!< the exposure (EV) towards the meter's (tex0, 1 x 1) from the one before (tex1)
#define HDR_BLOOM_DOWN      0x00000008  //!< the bloom's next level: 13 taps of tex0
#define HDR_BLOOM_PREFILTER 0x00000010  //!< with HDR_BLOOM_DOWN: from the scene, exposed, the fireflies tamed, above the threshold
#define HDR_BLOOM_UP        0x00000020  //!< the smaller level tex0 through a 3x3 tent, added to the target
#define HDR_RESOLVE         0x00000040  //!< the scene tex0 exposed, with the bloom tex1, tone mapped to the display
#define HDR_OUTPUT          0x00000080  //!< the frame tex0 with the highlight layer tex1 for an HDR swap chain (HdrOutput.cpp)
#define HDR_COLOR_LUT_SIZE  32          //!< the colour grading's table (ColorGrading.cpp): points along each axis

#define XR_SAMPLER_LINEAR_WRAP  0
#define XR_SAMPLER_LINEAR_CLAMP 1
#define XR_SAMPLER_POINT_WRAP   2
#define XR_SAMPLER_POINT_CLAMP  3

//! b0: per view.
struct SFrameConstants
{
  XR_FLOAT4X4(mViewProj);         //!< world to clip
  XR_FLOAT4X4(mView);
  XR_FLOAT4(vCameraPos);          //!< xyz
  XR_FLOAT4(vSunDir);             //!< xyz: normalised direction to the sun, world space
  XR_FLOAT4(vSunColor);           //!< rgb
  XR_FLOAT4(vFogColor);           //!< rgb
  XR_FLOAT4(vFogParams);          //!< x start, y end, z 1 / (end - start), w enabled
  XR_FLOAT4(vScreen);             //!< x width, y height, z 1 / width, w 1 / height (pixels)
  XR_FLOAT4(vTime);               //!< x ITimer::GetCurrTime()
  XR_FLOAT4(vWorldColor);         //!< EF_SetWorldColor
  XR_FLOAT4(vDepthParams);        //!< the projection's m22 (x) and m32 (y): distance along the view axis = y / (depth + x)
  XR_FLOAT4(vSunSpec);            //!< rgb: the sun's specular colour (DF_SPECULAR)
  XR_FLOAT4(vJitter);             //!< the upscaler's (Upscaler.h): xy its jitter in clip space (xy += vJitter.xy * w), zw the target's pixels per pixel of the engine (DF_SCREEN_POS); 0, 0, 1, 1 without one
  XR_FLOAT4(vMipBias);            //!< x: the mip bias of the materials' textures (log2(render / display size) - 1 while upscaling, else 0)
  XR_FLOAT4(vHdr);                //!< x: the light of the sky towards the sun times this (SkyGlow; 1: as the game's, always outside HDR targets)
  XR_FLOAT4X4_ARRAY(mCascade, XR_MAX_CASCADES);  //!< world to the sun's cascade i (DF2_SUN_CASCADES): x, y -1..1 across its map, z depth 0..1 away from the sun
  XR_FLOAT4(vCascadeEnd);         //!< the view depth (ViewDepth) where each cascade ends
  XR_FLOAT4(vCascadeTexel);       //!< the size of a texel of each in the world (m)
  XR_FLOAT4(vCascadeDepth);       //!< 1 / the depth range of each (m): metres along the sun's direction to the map's depth
  XR_FLOAT4(vCascadeParams);      //!< x the number of cascades (0: none), y the filter's radius (m), z the part of a cascade blended into the next one, w the depth bias (m)
  XR_UINT4(nCascadeTex);          //!< x the full set's maps, y the live set's (texture table indices of the arrays)
  XR_FLOAT4(vShadowTint);         //!< rgb: the terrain in the shadow of a live caster (DF2_SHADOW_TINT); w d3d12_ShadowDebug
};

//! b1: per draw.
struct SDrawConstants
{
  XR_FLOAT4X4(mWorld);            //!< object to world (identity for world space vertices)
  XR_FLOAT4X4(mWorldViewProj);    //!< object to clip
  XR_FLOAT4(vColor);              //!< multiplies the result (material / object colour)
  XR_FLOAT4(vAmbient);            //!< rgb ambient for DF_LIGHTING / DF_TEX1_LIGHTMAP; w the scale s of DF_LIGHTING (2 or 1)
  XR_FLOAT4(vTexGenU);            //!< DF_TEXGEN_WORLD
  XR_FLOAT4(vTexGenV);
  XR_FLOAT4(vTexMat0);            //!< DF_TEXMATRIX: u' = dot(float3(u, v, 1), vTexMat0.xyz)
  XR_FLOAT4(vTexMat1);            //!<               v' = dot(float3(u, v, 1), vTexMat1.xyz)
  XR_FLOAT4_ARRAY(vLightPos, XR_MAX_DRAW_LIGHTS);   //!< DF_LIGHTING, the draw's lights: w 1 a point light at xyz (world), w 2 a directional light from xyz
  XR_FLOAT4_ARRAY(vLightColor, XR_MAX_DRAW_LIGHTS); //!< rgb: the light's colour
  XR_FLOAT4_ARRAY(vLightAtten, XR_MAX_DRAW_LIGHTS); //!< a point light's attenuation 1 / (x + y d) at the distance d, 0 from z (the range) on; w 1 / its radius (DF_BUMP)
  XR_FLOAT4(vTexGen1U);           //!< DF_TEXGEN1_WORLD
  XR_FLOAT4(vTexGen1V);           //!< (DF_TERRAIN_CLIFF: xy 1 where tex2 / tex3 is set)
  XR_UINT(nFlags);                //!< DF_*
  XR_UINT(nTex0);                 //!< texture table index
  XR_UINT(nTex1);
  XR_UINT(nSamplers);             //!< bits 0-3 sampler of tex0, bits 4-7 sampler of tex1 (XR_SAMPLER_*)
  XR_FLOAT(fAlphaRef);
  XR_UINT(nLights);               //!< the lights in vLightPos
  XR_UINT(nTex2);                 //!< more textures for the other programs (Water.hlsl) and DF_TERRAIN_CLIFF
  XR_UINT(nTex3);
  XR_FLOAT4(vParams0);            //!< parameters of the other programs (Water.hlsl), DF_DETAIL_LAYER, DF_TERRAIN_CLIFF, DF2_GLASS and DF2_BUMP_OFFSET
  XR_FLOAT4(vParams1);
  XR_FLOAT4(vParams2);
  XR_FLOAT4(vParams3);
  XR_UINT(nTex4);
  XR_UINT(nTex5);
  XR_UINT(nTex6);
  XR_UINT(nFlags2);               //!< DF2_*
  XR_FLOAT4X4(mParams0);          //!< a matrix of the other programs (Water.hlsl) and DF2_PROJECTOR
  XR_FLOAT4X4_ARRAY(mShadow, XR_MAX_DRAW_SHADOWS);  //!< DF_SHADOWS: world to a shadow map: x, y -1..1 across its circle, z depth 0..1 (away from the sun)
  XR_UINT4(nShadowTex);           //!< their depth textures (texture table indices)
  XR_FLOAT4(vShadowParams);       //!< x the number of maps, y the fade of the shadow per unit of depth behind its casters (0: none), z filter radius in texels, w offset along the normal (m)
  XR_FLOAT4_ARRAY(vLightSpec, XR_MAX_DRAW_LIGHTS);  //!< DF_SPECULAR: rgb the light's specular colour
  XR_FLOAT4(vSpecular);           //!< DF_SPECULAR: rgb the material's specular colour, w 1: times the texture's alpha (gloss)
  XR_FLOAT4(vAddColor);           //!< DF2_ADD_TEX4: rgb the factor of tex4, w added
  XR_FLOAT4(vHeat);               //!< DF2_HEAT: x the heat 0 (cold) to 1 (hot), y 1 for an additive draw
  XR_FLOAT4X4(mPrevWorldViewProj);  //!< the motion vectors of an upscaler: object to clip in the frame before (all 0: as mWorldViewProj, still on the screen)
  XR_FLOAT4(vHdrCore);            //!< DF2_HDR_CORE: x the factor of the light in its core, y 1: the shape times tex0 a (a SRCALPHA blend)
};

#endif
