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
//	  t0, space6: Texture3D   g_Volumes[]   (the same table)
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
#define XR_LIGHT_LIGHTMAP_LAMP 3  //!< SDrawConstants::vLightPos[i].w: a lamp baked into the draw's light map, its highlight alone (DF_SPECULAR)
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
#define DF_TEX0_ARRAY       0x00040000  //!< tex0 is g_TextureArrays[nTex0]: slice floor(u / 2), sampled at (u - 2 * slice, v); v = v' + 2 m + 0.5 (m 1 to 256): blended with the next slice by (m - 1) / 255 (ArrayCoords.hlsli)
#define DF_TERRAIN_CLIFF    0x00100000  //!< rgb = 2 * rgb * lerp(1, 2 * tex2, w.x) * lerp(1, 2 * tex3, w.y), w = vertex colour rg * vTexGen1V.xy; uv of tex2 = (dot(world pos, vParams0), dot(.., vParams1)), of tex3 with vParams2 / 3
#define DF_DETAIL_BUMP      0x00200000  //!< with DF_DETAIL_LAYER: tex0 *= clamp(1 + (sun N'.L - sun N.L) / (vParams1.x + saturate(N.L)), 0, 2), N' = the normal map tex1 (uv0) in the frame of N and the texgen rows, its y times vParams1.y
#define DF_DECAL_FADE       0x00400000  //!< a *= 1 - f(r), r = saturate(2 * |uv0 - 0.5|), f(r) = (r + smoothstep(0, 1, r)) / 2: a decal fading out from its centre to a circle
#define DF_SHADOWS          0x00800000  //!< the sun is shadowed by the draw's shadow maps (SunVisibility): DF_LIGHTING lerps to the vertex light without the sun, DF_TEX1_LIGHTMAP scales the lightmap by the visibility
#define DF_SHADOW_PASS      0x01000000  //!< with DF_SHADOWS: rgb = colour * tex0, a = 1 - visibility (a shadow blended over the terrain)
#define DF_SHADOW_DEBUG     0x02000000  //!< with DF_SHADOWS: the result is the sun's visibility in grey (d3d12_ShadowDebug 2)
#define DF_CAUSTICS         0x10000000  //!< rgb = 0.5 + 0.5 * k * (I - 1), I the caustics (tex0, see General.hlsl), for 2 * dst * src
#define DF_VALPHA_ONLY      0x08000000  //!< with DF_VCOLOR: only the vertex colour's alpha counts (rgb white)
#define DF_LIGHTMAP_DIR     0x04000000  //!< with DF_TEX1_LIGHTMAP, a dot3 light map: the light map times saturate(D.z) * D.a, D = tex2 at uv1 (rgb * 2 - 1: the light's dominant direction in the surface's tangent space, a: its strength);
                                        //!< with SFrameConstants::vLightmap.x (d3d12_LightmapDot3) times lerp(1, saturate(D.z), the light map's alpha) * D.a instead, as the original's
#define DF_DETAIL_LAYER     0x00080000  //!< rgb = lerp(0.5, tex0, colour a * saturate((vParams0.y - d) * vParams0.z)), a = 1, fogged towards 0.5 (d: distance to the camera)
#define DF_PIXEL_LIGHTING   0x40000000  //!< with DF_LIGHTING: lit per pixel (each light's colour clamped to 1, point lights falling off as saturate(1 - d * vLightAtten.w); DF2_VERTEX_LIGHTS: as per vertex), with the vertex normal or N' (DF_BUMP)
#define DF_SPECULAR         0x80000000  //!< with DF_PIXEL_LIGHTING: + 2 * vSpecular.rgb * gloss * sum of the lights' specular colour (clamped to 1) * falloff * saturate(R.V)^32, R the light reflected about N', where N'.L > 0;
                                        //!< with DF_TEX1_LIGHTMAP (d3d12_LightmapSpecular): the same of its light map's lamps (XR_LIGHT_LIGHTMAP_LAMP) alone, after the light map, falling off as the original's (LightmapLampFalloff)
#define DF_BUMP             0x20000000  //!< bump mapping: N' = the normal map tex3 at uv0 (rgb * 2 - 1, its green times vParams1.y) in the vertex's tangent frame (TANGENT, BINORMAL, TNORMAL) for DF_PIXEL_LIGHTING; DF_LIGHTMAP_DIR takes saturate(dot(N', D)) for D.z

// SDrawConstants::nFlags2
#define DF2_ADD_TEX4        0x00000001  //!< rgb += tex4 (g_Textures[nTex4] at uv0 + (vTexMat0.w, vTexMat1.w)) * vAddColor.rgb + vAddColor.w: a glow map or a texture added, unlit
#define DF2_GLASS           0x00000002  //!< with DF_CUBE0: lerp(cube(T), cube(R), F), T the view vector refracted with the ratio vParams2.w, F = saturate(vParams2.z + vParams2.x * (1 - N.V)^vParams2.y); with DF2_ADD_TEX4 a *= tex4 a
#define DF2_NO_SUN          0x00000004  //!< with DF_LIGHTING: the draw's lights without the sun (a light pass added over the object)
#define DF2_FOG_BLACK       0x00000008  //!< with DF_FOG: fogged towards black (an additive pass)
#define DF2_FOG_VOLUME      0x00000010  //!< the fog of a fog volume over an object in it: rgb = vColor.rgb, a = FogVolumeAlpha (General.hlsl; vParams0: x the layer's height, y ln 100 / (2 * fog distance), z 1 for the volume's cap, w 1: times the alpha after the alpha test)
#define DF2_HEAT            0x00000020  //!< heat vision: the colour before the fog through HeatVision (Common.hlsli) with vHeat
#define DF2_SHADER_FX       0x00000040  //!< a template's shader of its own in place of the shading (ShaderFx, General.hlsl), the mode vParams3.w (XR_FX_*): the pictures of the scene (the glass of a scope: the scene copy tex0 around the middle of the screen; a mirror: tex0 projected by mParams0; heat haze: the scene copy rippled; bump offset: the scene copy moved by the normal map tex1, + tex2) and the special lighting of the templates (rainbow, membrane, cartoon, fur, iridescence, bubble) and the lava; nothing else applies but the fog (DF_FOG, towards black with DF2_FOG_BLACK)
#define XR_FX_SCOPE         0
#define XR_FX_MIRROR        1
#define XR_FX_HAZE          2
#define XR_FX_BUMP_OFFSET   3
#define XR_FX_RAINBOW       4
#define XR_FX_MEMBRANE      5
#define XR_FX_CARTOON       6
#define XR_FX_FUR           7   //!< also in the vertex shader: the position vParams0.x out along the normal (a shell)
#define XR_FX_IRIDESCENCE   8
#define XR_FX_BUBBLE        9   //!< also in the vertex shader: the position out along the normal by a gradient noise
#define XR_FX_LAVA          10
#define DF2_MODULATE        0x00000080  //!< a decal multiplied into the frame (DESTCOLOR, ZERO): rgb = saturate(rgb + vAddColor.w), a = 1, fogged towards white
#define DF2_PROJECTOR       0x00000100  //!< a projected light's pass: the colour (with the specular) times the cube g_Cubes[nTex5] at float4(world pos, 1) * mParams0 (the light's m_TextureMatrix)
#define DF2_TERRAIN_LIGHT   0x00000200  //!< the terrain under dynamic lights: tex0 rgb as 0.75 * rgb + 0.25; with DF_PIXEL_LIGHTING point lights fall off as saturate(1 - (d * vLightAtten.w)^2) (linearly with DF2_PROJECTOR)
#define DF2_MATERIAL_LIGHT  0x00000400  //!< with DF_LIGHTING: lit per pixel as the fixed function lights a material: saturate(vAmbient.rgb + vColor.rgb * the lights' colour * N.L) * texture + saturate(vSpecular.rgb * the lights' specular colour * (N.H)^vParams3.x)
#define DF2_GLOW_FALLOFF    0x00000800  //!< rgb *= saturate(1 - uv0.x)^vParams0.x: a glow fading out from its inner edge (u 0) to its outer edge (u 1)
#define DF2_REFL_AMOUNT     0x00001000  //!< with DF_CUBE0: tex0 rgb = cube * tex5 at uv0 (the reflect amount) + tex4 at uv0 * vAddColor.rgb (the texture), lit as tex0
#define DF2_ALPHA_GLOW      0x00002000  //!< rgb += tex0 rgb * tex0 a after the light and the light map: the texture's alpha is a glow mask, unlit
#define DF2_BUMP_OFFSET     0x00004000  //!< with DF_TEX0: tex0 looked up at uv0 + (tex3 at uv1 rg * 2 - 1) * vParams0.x, the texture wobbled by a normal map (the beach foam)
#define DF2_SKY_GLOW        0x00008000  //!< the sky with r_HDRRendering: rgb *= SkyGlow (Common.hlsli) in the direction of the world position
#define DF2_HDR_CORE        0x00010000  //!< a light added with r_HDRRendering: rgb *= HdrEncode(1 + (vHdrCore.x - 1) * m^4) (HdrLight.hlsli), m its shape 0..1 (tex0's largest channel, times tex0 a with vHdrCore.y, times DF2_GLOW_FALLOFF): brighter in its core, its soft edge as the game's
#define DF2_VERTEX_LIGHTS   0x00020000  //!< with DF_PIXEL_LIGHTING: the lights as the vertex lighting takes them, per pixel: colours not clamped, a point light's attenuation 1 / (vLightAtten.x + vLightAtten.y d) up to vLightAtten.z (d3d12_PixelLighting)
#define DF2_SMOOTH_FALLOFF  0x00040000  //!< with DF2_VERTEX_LIGHTS: a point light falls off as 2 (1 - smoothstep(0, 0.9, d * vLightAtten.w)) instead (static objects: the falloff the per vertex attenuation is fitted to)
#define DF2_LIGHTMAP_LIGHTS 0x00080000  //!< with DF_TEX1_LIGHTMAP (no DF_LIGHTING): the draw's lights without the sun (per pixel, DF2_VERTEX_LIGHTS) in place of the ambient under the light map (d3d12_PixelLighting)
#define DF2_SUN_CASCADES    0x00100000  //!< the sun's visibility from the cascades (SunCascades, Common.hlsli) where they reach, the shadow maps' (DF_SHADOWS) beyond; with DF_SHADOW_PASS the shadow maps' fade out where the cascades reach
#define DF2_CASCADES_LIVE   0x00200000  //!< with DF2_SUN_CASCADES: the cascades of the live casters only (light maps, the terrain)
#define DF2_SHADOW_TINT     0x00400000  //!< with DF2_SUN_CASCADES: rgb *= lerp(vShadowTint, 1, the visibility) (the terrain); with DF_LIGHTING (lit objects) the light in the shadow at least the lit one times vShadowTint; XR_PT: the G-buffer's ambient light so
#define DF2_CASCADE_CASTER  0x00800000  //!< a caster of the live cascades: drawn only where it is not behind the full set's map g_TextureArrays[nTex4] slice vParams3.x by more than vParams3.y (depth)
// The ambient light's part of a colour whose light is baked into it, for the ambient occlusion's target
// (XR_AMBIENT, General.hlsl; set only while it is bound)
#define DF2_AO_BAKED        0x01000000  //!< the terrain, the vegetation: the colour (before DF2_SHADOW_TINT) times vMipBias.yzw, the share of the light the shadow leaves
#define DF2_AO_ALL          0x02000000  //!< the templates lit by the ambient colour alone, the terrain's shadow: all of the colour
#define DF2_FOLIAGE         0x04000000  //!< the leaves and the grass, seen against the sun (XR_FOLIAGE): rgb += tex0 rgb * vColor.rgb * FoliageLight (General.hlsl, vFoliage)
#define DF2_TEX4_LAYER      0x20000000  //!< with DF_TEX0: tex0 = (lerp(tex0 rgb, tex4 rgb, tex4 a), a + tex4 a - a * tex4 a), tex4 at uv0 + (vTexMat0.w, vTexMat1.w): a second layer blended over the texture before the light; vAddColor.x > 0: tex0 rgb + tex4 rgb * vAddColor.x instead (added, lit as the texture)
#define DF2_CUBE_SHADER     0x40000000  //!< with DF_CUBE0: tex0 rgb as the original's shader of the template, the mode vParams3.w (CubeShader, General.hlsl): 1 refraction, 2 dispersion, 3 the glass of ps_2_0, 4 the bump refraction and bump glass, 5 tex4 + cube * tex4 a, 6 cube * tex4; the cube alone, no rays (ray queries in its branches hung the GPU)
#define DF2_AMBIENT_CUBE    0x80000000  //!< rgb += 0.25 * (g_Cubes[nTex6] at the view vector reflected about the vertex normal - tex0 * vColor * vAmbient): a quarter of the ambient light from the cube (the *_EnvCMAmb templates), unlit
// The path tracer's G-buffer (XR_PT, General.hlsl; set only while it is bound, PathTrace.cpp)
#define DF2_PT_MASK         0x18000000  //!< the surface's path material plus 1 (XR_PT_MAT_* + 1 & 3, RayConstants.h; 0 unlit): its light flags off, its colour less what it adds unlit (DF2_ADD_TEX4, DF2_ALPHA_GLOW) is its albedo
#define DF2_PT_SHIFT        27

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
#define POST_AO_TAKEN       0x00000800  //!< the light the ambient occlusion takes (AoTaken, Post.hlsl): tex0 the visibility, tex1 the depth, tex2 the ambient light
// The render modes (r_RenderMode, PostFx.cpp; Post.hlsl's RenderMode* functions)
#define POST_RM_ADAPT       0x00001000  //!< the luminance of 4 m^2, m the mean of the scene copy tex0 at the four spots the original takes, alpha vParams1.x (blended into the 1 x 1 target)
#define POST_RM_BRIGHT      0x00002000  //!< the glare's source: 2 (saturate(tex0 + vParams1.x) * vParams1.y)^2 * saturate(1 - the adapted luminance tex1)
#define POST_RM_BLUR        0x00004000  //!< the mean of 4 bilinear taps of tex0 vParams1.x texels away along (1, 2) turned by right angles
#define POST_RM_COMPOSITE   0x00008000  //!< the scene copy tex0 plus (1 - its luminance) * the glare tex1, towards its luminance by vParams1.x; vParams1.y: cold (tex2 the 256 x 256 copy)
#define POST_RM_CARTOON     0x00010000  //!< the cartoon's colours of the scene copy tex0 darkened at its edges

// SDrawConstants::nFlags of the anti-aliasing program (AntiAlias.hlsl, PostAA.cpp): one mode
#define AA_FXAA             0x00000001  //!< FXAA (Fxaa.hlsli) of the copy tex0 at the quality vParams1.x; the pixels it keeps are not written
#define AA_SMAA             0x00000002  //!< SMAA 1x's neighbourhood blending (SmaaBlend.hlsli) of the copy tex0 by the weights tex1; the same
#define AA_CMAA2            0x00000004  //!< CMAA2's picture tex1 (its kernels', in place of a copy of tex0) where it differs from the copy tex0, tex0's alpha
#define AA_DEBUG            0x80000000  //!< with the mode: its view of the edges (d3d12_AADebug 1)

// SDrawConstants::nFlags of the HDR program (Hdr.hlsl, Hdr.cpp): one mode
#define HDR_LUMINANCE       0x00000001  //!< the meter's first level: (w * log2 L, w) of the scene tex0 over the target's texel, w the centre weight
#define HDR_REDUCE          0x00000002  //!< the mean of tex0 over the target's texel: vParams0.z x vParams0.z bilinear taps
#define HDR_ADAPT           0x00000004  //!< the exposure (EV) towards the meter's (tex0, 1 x 1) from the one before (tex1)
#define HDR_BLOOM_DOWN      0x00000008  //!< the bloom's next level: 13 taps of tex0
#define HDR_BLOOM_PREFILTER 0x00000010  //!< with HDR_BLOOM_DOWN: from the scene, exposed, the fireflies tamed, above the threshold
#define HDR_BLOOM_UP        0x00000020  //!< the smaller level tex0 through a 3x3 tent, added to the target
#define HDR_RESOLVE         0x00000040  //!< the scene tex0 exposed, with the bloom tex1, tone mapped to the display
#define HDR_OUTPUT          0x00000080  //!< the frame tex0 with the highlight layer tex1 for an HDR swap chain (HdrOutput.cpp)
#define HDR_VOLUMETRIC      0x00000100  //!< with HDR_RESOLVE or HDR_SCENE_LIGHT: the scene's light through the volumetric light (Volumetric.cpp): tex4 its froxels, tex5 the depth
#define HDR_EXPOSURE_SCALE  0x00000200  //!< the exposure's factor of the light, 2^(the compensation + the EV tex0), for an upscaler (1 x 1 R32_FLOAT)
#define HDR_LINEAR          0x00000400  //!< with HDR_RESOLVE or HDR_BLOOM_DOWN: tex0 holds light (the scene's upscaled, d3d12_UpscaleHdr), not the game's colours
#define HDR_SCENE_LIGHT     0x00000800  //!< the scene tex0's light, not exposed, for an upscaler (d3d12_UpscaleHdr): the input of the tone map after the upscale
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
  XR_FLOAT4(vTime);               //!< x ITimer::GetCurrTime(), y the frame's number (0 to 63) in the scene of a temporal
                                  //!< anti-aliasing or upscaler, which gathers the frames, else 0 (ArrayCoords.hlsli's PictureNoise)
  XR_FLOAT4(vWorldColor);         //!< EF_SetWorldColor
  XR_FLOAT4(vDepthParams);        //!< the projection's m22 (x) and m32 (y): distance along the view axis = y / (depth + x)
  XR_FLOAT4(vSunSpec);            //!< rgb: the sun's specular colour (DF_SPECULAR)
  XR_FLOAT4(vJitter);             //!< the upscaler's (Upscaler.h): xy its jitter in clip space (xy += vJitter.xy * w), zw the target's pixels per pixel of the engine (DF_SCREEN_POS); 0, 0, 1, 1 without one
  XR_FLOAT4(vMipBias);            //!< x: the mip bias of the materials' textures (log2(render / display size) - 1 while upscaling, else 0;
                                  //!< plus 1.5 in the cartoon render mode, PostFx.cpp);
                                  //!< yzw: DF2_AO_BAKED's share of the ambient light in a baked colour
  XR_FLOAT4(vHdr);                //!< x: the light of the sky towards the sun times this (SkyGlow; 1: as the game's, always outside HDR targets), y: the power of the cosine of the angle to the sun it falls off with
  XR_FLOAT4X4_ARRAY(mCascade, XR_MAX_CASCADES);  //!< world to the sun's cascade i (DF2_SUN_CASCADES): x, y -1..1 across its map, z depth 0..1 away from the sun
  XR_FLOAT4(vCascadeEnd);         //!< the view depth (ViewDepth) where each cascade ends
  XR_FLOAT4(vCascadeTexel);       //!< the size of a texel of each in the world (m)
  XR_FLOAT4(vCascadeDepth);       //!< 1 / the depth range of each (m): metres along the sun's direction to the map's depth
  XR_FLOAT4(vCascadeParams);      //!< x the number of cascades (0: none), y the filter's radius (m), z the part of a cascade blended into the next one, w the depth bias (m)
  XR_UINT4(nCascadeTex);          //!< x the full set's maps, y the live set's (texture table indices of the arrays)
  XR_FLOAT4(vShadowTint);         //!< rgb: the terrain in the shadow of a live caster (DF2_SHADOW_TINT); w d3d12_ShadowDebug
  XR_FLOAT4(vFoliage);            //!< DF2_FOLIAGE: x the strength (d3d12_FoliageTranslucency), y the power of the cosine of the angle
                                  //!< to the sun, z the distance where it ends (m), w 2 / that distance (all of it up to half of it)
  XR_UINT4(nRaySun);              //!< x the texture table index of the mask of the sun's shadow by rays (XR_RAY_SUN, RaySun.cpp)
                                  //!< y z w the general program's rays (XR_RAY_REFL, XR_RAY_LIGHT; Draw.cpp): y their length, z RayMask with
                                  //!< the cube maps' mode in bits 16-23, the lamps' in 24-25, their number a pixel in 28-31 and their rays' gap at
                                  //!< the lamp in 9-15 (2 cm steps), w the ocean's level
  XR_FLOAT4(vLightmap);           //!< x 1: a dot3 light map's N'.D mixed towards 1 by the light map's alpha (DF_LIGHTMAP_DIR, d3d12_LightmapDot3);
                                  //!< y 1: a bump template's light per pixel (DF_PIXEL_LIGHTING) times the material's colour scaled down by its
                                  //!< largest channel above 1, the light's hue kept, its term not clamped (d3d12_LightColorScale; 0: each
                                  //!< channel of the light's colour clamped to 1)
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
  XR_FLOAT4(vTexMat0);            //!< DF_TEXMATRIX: u' = dot(float3(u, v, 1), vTexMat0.xyz); w: tex4's u offset (DF2_ADD_TEX4, DF2_TEX4_LAYER)
  XR_FLOAT4(vTexMat1);            //!<               v' = dot(float3(u, v, 1), vTexMat1.xyz); w: tex4's v offset
  XR_FLOAT4_ARRAY(vLightPos, XR_MAX_DRAW_LIGHTS);   //!< DF_LIGHTING, the draw's lights: w 1 a point light at xyz (world), w 2 a directional light from xyz,
                                                    //!< w 3 (XR_LIGHT_LIGHTMAP_LAMP) a lamp baked into the draw's light map at xyz: its highlight alone (DF_SPECULAR)
  XR_FLOAT4_ARRAY(vLightColor, XR_MAX_DRAW_LIGHTS); //!< rgb: the light's colour; w a point light's least gap short of it its rays end (XR_RAY_LIGHT, m:
                                                    //!< a projector's d3d12_RTProjectorShadowGap, else 0: the frame's)
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
  XR_FLOAT4(vParams0);            //!< parameters of the other programs (Water.hlsl), DF_DETAIL_LAYER, DF_TERRAIN_CLIFF, DF2_GLASS, DF2_BUMP_OFFSET and DF2_CUBE_SHADER
  XR_FLOAT4(vParams1);
  XR_FLOAT4(vParams2);
  XR_FLOAT4(vParams3);
  XR_UINT(nTex4);
  XR_UINT(nTex5);
  XR_UINT(nTex6);
  XR_UINT(nFlags2);               //!< DF2_*
  XR_FLOAT4X4(mParams0);          //!< a matrix of the other programs (Water.hlsl) and DF2_PROJECTOR
  XR_FLOAT4X4_ARRAY(mShadow, XR_MAX_DRAW_SHADOWS);  //!< DF_SHADOWS: world to a shadow map: x, y -1..1 across its circle, z depth 0..1 (away from the sun);
                                                    //!< XR_CASCADE_INSTANCES: object to slice nShadowTex[i] of the cascades for instance i
  XR_UINT4(nShadowTex);           //!< their depth textures (texture table indices); XR_CASCADE_INSTANCES: the instances' slices
  XR_FLOAT4(vShadowParams);       //!< x the number of maps, y the fade of the shadow per unit of depth behind its casters (0: none), z filter radius in texels, w offset along the normal (m)
  XR_FLOAT4_ARRAY(vLightSpec, XR_MAX_DRAW_LIGHTS);  //!< DF_SPECULAR: rgb the light's specular colour
  XR_FLOAT4(vSpecular);           //!< DF_SPECULAR: rgb the material's specular colour, w 1: times the texture's alpha (gloss)
  XR_FLOAT4(vAddColor);           //!< DF2_ADD_TEX4: rgb the factor of tex4, w added
  XR_FLOAT4(vHeat);               //!< DF2_HEAT: x the heat 0 (cold) to 1 (hot), y 1 for an additive draw
  XR_FLOAT4X4(mPrevWorldViewProj);  //!< the motion vectors of an upscaler: object to clip in the frame before (all 0: as mWorldViewProj, still on the screen)
  XR_FLOAT4(vHdrCore);            //!< DF2_HDR_CORE: x the factor of the light in its core, y 1: the shape times tex0 a (a SRCALPHA blend)
  XR_FLOAT4(vEdgeFade);           //!< x 1: the colour per vertex times min((2 |N.V|)^4, 1), N the vertex normal, V the view direction: planes
                                  //!< fading out seen edge on (TemplMuzzleFlash_Auto, d3d12_MuzzleFlashAuto)
};

//! b0 of the ambient occlusion's resolve (AoResolve.hlsl, AmbientOcclusion.cpp).
struct SAoResolveConstants
{
  XR_FLOAT4(vSize);               //!< x width, y height (pixels), z 1 / width, w 1 / height
  XR_FLOAT4(vDepthParams);        //!< as SFrameConstants'
  XR_FLOAT4(vFogParams);          //!< as SFrameConstants'
  XR_FLOAT4(vParams);             //!< x the exponent of the visibility (d3d12_AOStrength / 2.2: the scene's colours are the game's, not linear;
                                  //!< d3d12_AOStrength with y), y 1: the ambient light's part (d3d12_AOApply 1), z 1: with it the factor too
                                  //!< (d3d12_AODebug 1)
};

//! b0 of the meter's first level (Meter.hlsl, Hdr.cpp).
struct SMeterConstants
{
  XR_FLOAT4(vSize);               //!< the level: x width, y height (texels), z 1 / width, w 1 / height
  XR_UINT4(vTaps);                //!< x the taps a side of a texel (as HDR_LUMINANCE's vParams0.z)
};

// The HDR statistics (HdrStats.cpp, HdrStats.hlsl; d3d12_DisplayHDR, d3d12_HDRLog): a histogram of the scene's light and
// counts of the HDR output's nits, words of a buffer the CPU reads D3D12_FRAMES frames later. A sample every
// XR_HDR_STRIDE pixels across and down.
#define XR_HDR_STRIDE           2
#define XR_HDR_HIST_BINS        64          // the histogram's bins of log2 of the light, from XR_HDR_HIST_MIN to XR_HDR_HIST_MAX
#define XR_HDR_HIST_MIN         (-12)       // (the light outside goes to the first or the last bin)
#define XR_HDR_HIST_MAX         8
#define XR_HDR_WEIGHT_ONE       256         // the centre weight 1 in XR_HDR_STAT_LUMA_W and _WEIGHT
#define XR_HDR_NITS_UNIT        8           // XR_HDR_STAT_OUT_SUM's unit (nits x pixels)
#define XR_HDR_STAT_LUMA        0                                 // the scene's samples by log2 of their luminance
#define XR_HDR_STAT_LUMA_W      XR_HDR_HIST_BINS                  // the same, by the meter's centre weight (x XR_HDR_WEIGHT_ONE)
#define XR_HDR_STAT_MAX         (2 * XR_HDR_HIST_BINS)            // the samples by log2 of their brightest channel
#define XR_HDR_STAT_SAMPLES     (3 * XR_HDR_HIST_BINS)            // the samples
#define XR_HDR_STAT_WEIGHT      (XR_HDR_STAT_SAMPLES + 1)         // their weight (x XR_HDR_WEIGHT_ONE)
#define XR_HDR_STAT_SHOULDER    (XR_HDR_STAT_SAMPLES + 2)         // exposed, the brightest channel above the tone map's knee
#define XR_HDR_STAT_WHITE       (XR_HDR_STAT_SAMPLES + 3)         // exposed, the brightest channel at the game's white (1) or above
#define XR_HDR_STAT_SCENE_WORDS (XR_HDR_STAT_SAMPLES + 4)         // the words the histogram's kernel adds to
#define XR_HDR_STAT_EV          (XR_HDR_STAT_SAMPLES + 4)         // asuint: the EV the samples were exposed with (the adapted and the compensation)
#define XR_HDR_STAT_METER       (XR_HDR_STAT_SAMPLES + 5)         // asuint: the meter's mean log2 luminance (with the automatic exposure)
#define XR_HDR_STAT_TARGET      (XR_HDR_STAT_SAMPLES + 6)         // asuint: d3d12_ExposureMeter 1, the target EV (HdrExposure.hlsli, before the range)
#define XR_HDR_STAT_CEILING     (XR_HDR_STAT_SAMPLES + 7)         // asuint: its sky's ceiling EV (XR_EXPOSURE_NONE: none)
#define XR_HDR_STAT_OUT_PIXELS  (XR_HDR_STAT_SAMPLES + 8)         // the HDR output's samples
#define XR_HDR_STAT_OUT_SUM     (XR_HDR_STAT_SAMPLES + 9)         // the sum of their brightest channel's nits (in XR_HDR_NITS_UNIT), its low word
#define XR_HDR_STAT_OUT_MAX     (XR_HDR_STAT_SAMPLES + 10)        // asuint: the most nits of a channel (MaxCLL)
#define XR_HDR_STAT_OUT_PAPER   (XR_HDR_STAT_SAMPLES + 11)        // the samples whose brightest channel is above paper white (x 1.02)
#define XR_HDR_STAT_OUT_PEAK    (XR_HDR_STAT_SAMPLES + 12)        // ... is at the peak (95 % of it or more)
#define XR_HDR_STAT_SKY         (XR_HDR_STAT_SAMPLES + 13)        // asuint: d3d12_ExposureMeter 1, the sky's share of its samples
#define XR_HDR_STAT_OUT_SUM_HIGH (XR_HDR_STAT_SAMPLES + 14)       // XR_HDR_STAT_OUT_SUM's high word: its carries (HdrSumCarries)
#define XR_HDR_STAT_WORDS       256

//! b0 of the HDR statistics' kernels (HdrStats.hlsl, HdrStats.cpp).
struct SHdrStatsConstants
{
  XR_FLOAT4(vSize);               //!< what is sampled: x width, y height (pixels), z 1 / width, w 1 / height
  XR_FLOAT4(vParams);             //!< CSHistogram: x the exposure's compensation (EV), y 1: plus the adapted EV (t1), z the light of the tone
                                  //!< map's knee, w 1: the meter's mean in t2, 2: t2 the exposure's target (d3d12_ExposureMeter 1);
                                  //!< CSOutput: x paper white, y the peak (nits), z 1: t2 the frame average limit's target
};

// The automatic exposure's histogram (d3d12_ExposureMeter 1; Exposure.hlsl, HdrExposure.hlsli, Hdr.cpp): words of a buffer, the
// bins those of the HDR statistics (XR_HDR_HIST_*), a sample every SExposureConstants::vStride.x pixels across and down; the
// target kernel reads them and clears them for the next frame.
#define XR_EXPOSURE_LUMA        0                                 // the scene's samples by log2 of their luminance, by the meter's
                                                                  // centre weight (x XR_HDR_WEIGHT_ONE)
#define XR_EXPOSURE_SKY         XR_HDR_HIST_BINS                  // the sky's (the depth at the far plane) by log2 of their brightest channel
#define XR_EXPOSURE_SAMPLES     (2 * XR_HDR_HIST_BINS)            // the samples
#define XR_EXPOSURE_WORDS       (XR_EXPOSURE_SAMPLES + 1)
#define XR_EXPOSURE_NONE        64.0f                             // the target's sky ceiling when there is none
#define XR_EXPOSURE_SKY_FROM    0.02f                             // the sky's share of the samples where its ceiling starts to count
#define XR_EXPOSURE_SKY_FULL    0.08f                             // ... and where it counts whole

//! b0 of the automatic exposure's kernels (Exposure.hlsl, Hdr.cpp).
struct SExposureConstants
{
  XR_FLOAT4(vSize);               //!< the scene: x width, y height (pixels), z 1 / width, w 1 / height
  XR_FLOAT4(vParams);             //!< x log2 of the key, y z the window's shares (d3d12_ExposureLow, High), w the sky's percentile kept at the
                                  //!< knee (d3d12_ExposureHighlights; 0: none)
  XR_FLOAT4(vParams1);            //!< x log2 of the tone map's knee as light, y 1: t1 the depth buffer (the sky where it is at the far plane)
  XR_UINT4(vStride);              //!< x a sample every this many pixels across and down
};

// The HDR output's frame average limit (d3d12_HDRFrameAverage; FrameAverage.hlsl, HdrFrameAverage.hlsli, HdrOutput.cpp): words
// of a buffer, the sums in paper whites times XR_FALL_UNIT, which the limit's kernel reads and clears; it keeps its factor there.
#define XR_FALL_SAMPLES         0           // the samples
#define XR_FALL_SDR             1           // the sum of the SDR picture's light (of the brightest channel)
#define XR_FALL_HIGHLIGHTS      2           // the sum of what the highlights add to it
#define XR_FALL_LAST            3           // asuint: the last frame's factor
#define XR_FALL_WORDS           4
#define XR_FALL_UNIT            256         // a paper white in the sums
#define XR_FALL_MOST            64.0f       // what a sample's highlights add at most (paper whites), so that a frame's sum fits a word

//! b0 of the frame average limit's kernels (FrameAverage.hlsl, HdrOutput.cpp).
struct SFrameAverageConstants
{
  XR_FLOAT4(vSize);               //!< the frame: x width, y height (pixels), z 1 / width, w 1 / height
  XR_FLOAT4(vParams);             //!< x paper white, y the peak (nits), z the limit (paper whites), w 1: no frame before (the factor at once)
  XR_FLOAT4(vSteps);              //!< x the most the factor goes down in this frame, y the most it goes up
  XR_UINT4(vStride);              //!< x a sample every this many pixels across and down
};

// The texels a side of the water's depth copy in a texel of level 0 of its depth tiles (DepthTiles.hlsl, Water.hlsl)
#define XR_DEPTH_TILE       8

//! b0 of the depth tiles of the water's depth copy (DepthTiles.hlsl, Water.cpp).
struct SDepthTileConstants
{
  XR_FLOAT4(vSize);               //!< x 1 / the copy's width, y 1 / its height (texels), zw the size of level 5
};

// The FFT ocean (OceanFFT.cpp, OceanFFT.hlsl, Water.hlsl; OceanSpectrum.h on the CPU): the texels a side of a cascade,
// the cascades, their tiles (metres a side, OceanSpectrum::TILE), the levels of the waves' mip chain (down to 1 x 1) and
// the texels a side of its level 4 (the last the waves' kernel makes).
#define XR_OCEAN_SIZE       256
#define XR_OCEAN_CASCADES   3
#define XR_OCEAN_TILE0      250.0
#define XR_OCEAN_TILE1      40.5
#define XR_OCEAN_TILE2      6.9
#define XR_OCEAN_MIPS       9
#define XR_OCEAN_LEVEL4     16

//! b0 of the FFT ocean's kernels (OceanFFT.hlsl, OceanFFT.cpp).
struct SOceanConstants
{
  XR_FLOAT4(vParams);             //!< x the time's fraction of the sea's period (OceanSpectrum::PERIOD), y the horizontal
                                  //!< displacement's factor (d3d12_WaterChop; 0: none), z the least determinant of its
                                  //!< Jacobian (where a crest would fold over)
  XR_FLOAT4_ARRAY(vCascades, XR_OCEAN_CASCADES);  //!< each cascade's: x its tile (m), y the weight of its height
                                  //!< (OceanSpectrum::SSea::HeightWeight), z its texel (m)
};

//! b0 of a level of the bloom from one twice its size (Bloom.hlsl, Hdr.cpp).
struct SBloomConstants
{
  XR_FLOAT4(vSize);               //!< the level: x width, y height (texels), z 1 / width, w 1 / height
  XR_FLOAT4(vParams);             //!< as HDR_BLOOM_DOWN's vParams1 (Hdr.hlsl): x the compensation, y 1 with the adapted EV, z the threshold,
                                  //!< w its soft knee
  XR_UINT4(vFlags);               //!< x 1: from the scene, its light exposed (HDR_BLOOM_PREFILTER); y 1: the scene holds light already (HDR_LINEAR)
};

// The threads a side of a group of the volumetric light's kernels (Volumetric.hlsl, Volumetric.cpp): 8 x 8 froxels
// of a slice (the injection), or 8 x 8 columns of them (the integration)
#define XR_VOLUMETRIC_GROUP 8

//! b0 of the volumetric light's kernels (Volumetric.hlsl, Volumetric.cpp). A froxel (x, y, z) of the volume covers
//! the view from x / width to (x + 1) / width across (left to right) and y / height to (y + 1) / height down, and the
//! view depths from Depth(z / slices) to Depth((z + 1) / slices), Depth(w) = near * 2^(w log2(far / near)).
struct SVolumetricConstants
{
  XR_FLOAT4X4_ARRAY(mCascade, XR_MAX_CASCADES);  //!< as SFrameConstants'
  XR_FLOAT4X4(mPrevViewProj);     //!< world to clip in the frame before: where a froxel was in the history
  XR_FLOAT4(vCameraPos);          //!< xyz the camera
  XR_FLOAT4(vRayX);               //!< the world position at the view depth d and x, y -1..1 across the view (y up):
  XR_FLOAT4(vRayY);               //!< vCameraPos + d (vRayO + x vRayX + y vRayY)
  XR_FLOAT4(vRayO);
  XR_FLOAT4(vSize);               //!< x width, y height, z the slices (froxels), w 1 / the slices
  XR_FLOAT4(vSlices);             //!< x near (m), y log2(far / near), z 1 / y, w far (m)
  XR_FLOAT4(vJitter);             //!< xyz: this frame's point in its froxels (0..1 each way)
  XR_FLOAT4(vSunDir);             //!< xyz: the direction to the sun; w the phase's anisotropy g (Henyey-Greenstein)
  XR_FLOAT4(vSunLight);           //!< rgb: the sun's light scattered (d3d12_VolumetricLightSun), 0 without a sun
  XR_FLOAT4(vAmbientLight);       //!< rgb: the light scattered from all round (the fog's colour, d3d12_VolumetricLightAmbient), w the part of it in the sun's shadow
  XR_FLOAT4(vDensity);            //!< x the extinction (1 / m) up to the height y (m), z 1 / the height over which it falls to half above
                                  //!< (0: the same everywhere), w the history's weight (0: none)
  XR_FLOAT4(vCascadeEnd);         //!< as SFrameConstants'
  XR_FLOAT4(vCascadeDepth);       //!< as SFrameConstants'
  XR_FLOAT4(vCascadeParams);      //!< x the number of cascades (0: the sun reaches everywhere), w the depth bias (m)
  XR_FLOAT4(vShafts);             //!< the shafts alone (d3d12_VolumetricLightShafts): x the radius of the air around (m; 0 all the
                                  //!< air, as before), y the power of the part of it in shadow (the sun's light as much as that)
};

// The threads a side of a group of TAA's kernels (Taa.hlsl, UpscalerTaa.cpp)
#define XR_TAA_GROUP        8

// TAA's flags (STaaConstants::vFlags.x)
#define TAA_RESET           0x00000001  //!< no history: the frame as it is (the first frame, a jump of the camera, a new mode)
#define TAA_HDR             0x00000002  //!< the colours are the HDR scene's light (else LDR colours, the back buffer's)
#define TAA_EXPOSURE        0x00000004  //!< with TAA_HDR: t3 holds the light's exposure (else vParams.z)
#define TAA_OUTPUT          0x00000008  //!< the resolve writes the picture too (no sharpening after it)
#define TAA_DEBUG           0x00000010  //!< its debug view in place of the picture (d3d12_AADebug 1)

//! b0 of TAA's kernels (Taa.hlsl, UpscalerTaa.cpp).
struct STaaConstants
{
  XR_FLOAT4(vSize);               //!< x width, y height (pixels), z 1 / width, w 1 / height
  XR_FLOAT4(vParams);             //!< x the least share of the frame in the picture at rest (d3d12_AAQuality), y the sharpening
                                  //!< (0..1, d3d12_UpscaleSharpness), z the exposure without TAA_EXPOSURE, w the least share from a
                                  //!< pixel's motion a frame on
  XR_UINT4(vFlags);               //!< x TAA_*
  XR_UINT4(vCas0);                //!< the sharpening's CAS constants of its sharpness (CasSetup's const0, const1; TaaCas.hlsli)
  XR_UINT4(vCas1);
};

// MSAA's resolve's flags (SMsaaConstants::vParams.y)
#define MSAA_HDR            0x00000001  //!< the HDR target's colours (tone mapped for their mean), else the back buffer's
#define MSAA_AUTO_EXPOSURE  0x00000002  //!< t1 holds the adapted EV (Hdr.cpp's), added to vExposure.x

//! b0 of MSAA's resolve passes (Msaa.hlsl, Msaa.cpp; root constants).
struct SMsaaConstants
{
  XR_UINT4(vParams);              //!< x the samples of a pixel (2, 4 or 8), y MSAA_*
  XR_FLOAT4(vExposure);           //!< x the exposure's compensation, EV (d3d12_Exposure)
};

#endif
