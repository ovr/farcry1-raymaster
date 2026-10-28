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
//	Matrices are row major and multiply row vectors (v * M), as Matrix44 of the engine; dxc
//	compiles with -Zpr, so C++ copies a Matrix44 as it is.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_CONSTANTS_H
#define XRENDERD3D12_CONSTANTS_H

#ifdef __cplusplus
typedef float float4x4_t[16];
#define XR_FLOAT4X4(name) float name[16]
#define XR_FLOAT4(name) float name[4]
#define XR_FLOAT4_ARRAY(name, n) float name[n][4]
#define XR_UINT(name) uint32 name
#define XR_FLOAT(name) float name
#else
#define XR_FLOAT4X4(name) float4x4 name
#define XR_FLOAT4(name) float4 name
#define XR_FLOAT4_ARRAY(name, n) float4 name[n]
#define XR_UINT(name) uint name
#define XR_FLOAT(name) float name
#endif

//! The most lights SDrawConstants holds besides the sun.
#define XR_MAX_DRAW_LIGHTS 16

// SDrawConstants::nFlags
#define DF_TEX0             0x00000001  //!< sample g_Textures[nTex0] (else white)
#define DF_TEX1             0x00000002  //!< second texture g_Textures[nTex1] with uv1 (lightmap, detail)
#define DF_VCOLOR           0x00000004  //!< multiply by the vertex colour (COLOR0; the default stream is white)
#define DF_VCOLOR_X2        0x00000008  //!< the vertex colour times 2 (vegetation)
#define DF_LIGHTING         0x00000010  //!< per vertex 2 * (saturate(ambient / 2 + sun N.L + light 0) + saturate(each other light)), normal from NORMAL
#define DF_ALPHATEST        0x00000020  //!< clip when alpha < fAlphaRef (GREATER0: fAlphaRef 1/255)
#define DF_TEXGEN_WORLD     0x00000040  //!< uv0 = (dot(world pos, vTexGenU), dot(world pos, vTexGenV))
#define DF_TEXMATRIX        0x00000080  //!< uv0 = (uv0, 1) * the 2x3 texture matrix vTexMat0/vTexMat1
#define DF_FOG              0x00000100  //!< linear fog from SFrameConstants
#define DF_TEX1_DETAIL      0x00000200  //!< rgb = 2 * rgb + tex1 - 0.5 (terrain detail texture); else colour *= tex1
#define DF_TEX1_LIGHTMAP    0x00000400  //!< colour = tex0 * (4 * lightmap + ambient) (lightmapped objects)
#define DF_ALPHA_FROM_TEX   0x00000800  //!< alpha = texture alpha only (no vertex or constant alpha)
#define DF_CUBE0            0x00001000  //!< tex0 is g_Cubes[nTex0], sampled with the reflected view vector
#define DF_TEX0_ALPHA_ONLY  0x00002000  //!< rgb = colour, a = texture alpha * colour alpha (fonts)
#define DF_SCREEN_POS       0x00004000  //!< POSITION is in pixels (x, y from the top left, z); mViewProj is ignored
#define DF_ALPHATEST_LESS   0x00008000  //!< with DF_ALPHATEST: clip when alpha >= fAlphaRef instead (GS_ALPHATEST_LESS128)
#define DF_TEXGEN1_WORLD    0x00010000  //!< uv1 = (dot(world pos, vTexGen1U), dot(world pos, vTexGen1V)) (terrain detail)
#define DF_ALPHA_ONE        0x00020000  //!< alpha 1 after the alpha test (the pictures of the far vegetation sprites)
#define DF_TEX0_ARRAY       0x00040000  //!< tex0 is g_TextureArrays[nTex0]: slice floor(u / 2), sampled at (u - 2 * slice, v)
#define DF_TERRAIN_CLIFF    0x00100000  //!< rgb = 2 * rgb * lerp(1, 2 * tex2, w.x) * lerp(1, 2 * tex3, w.y), w = vertex colour rg * vTexGen1V.xy; uv of tex2 = (dot(world pos, vParams0), dot(.., vParams1)), of tex3 with vParams2 / 3
#define DF_DETAIL_BUMP      0x00200000  //!< with DF_DETAIL_LAYER: tex0 *= clamp(1 + (sun N'.L - sun N.L) / (vParams1.x + saturate(N.L)), 0, 2), N' = the normal map tex1 (uv0) in the frame of N and the texgen rows, its y times vParams1.y
#define DF_DETAIL_LAYER     0x00080000  //!< rgb = lerp(0.5, tex0, colour a * saturate((vParams0.y - d) * vParams0.z)), a = 1, fogged towards 0.5 (d: distance to the camera)

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
};

//! b1: per draw.
struct SDrawConstants
{
  XR_FLOAT4X4(mWorld);            //!< object to world (identity for world space vertices)
  XR_FLOAT4X4(mWorldViewProj);    //!< object to clip
  XR_FLOAT4(vColor);              //!< multiplies the result (material / object colour)
  XR_FLOAT4(vAmbient);            //!< rgb ambient for DF_LIGHTING / DF_TEX1_LIGHTMAP
  XR_FLOAT4(vTexGenU);            //!< DF_TEXGEN_WORLD
  XR_FLOAT4(vTexGenV);
  XR_FLOAT4(vTexMat0);            //!< DF_TEXMATRIX: u' = dot(float3(u, v, 1), vTexMat0.xyz)
  XR_FLOAT4(vTexMat1);            //!<               v' = dot(float3(u, v, 1), vTexMat1.xyz)
  XR_FLOAT4_ARRAY(vLightPos, XR_MAX_DRAW_LIGHTS);   //!< DF_LIGHTING, the draw's lights: w 1 a point light at xyz (world), w 2 a directional light from xyz
  XR_FLOAT4_ARRAY(vLightColor, XR_MAX_DRAW_LIGHTS); //!< rgb: the light's colour
  XR_FLOAT4_ARRAY(vLightAtten, XR_MAX_DRAW_LIGHTS); //!< a point light's attenuation 1 / (x + y d) at the distance d, 0 from z (the range) on
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
  XR_FLOAT4(vParams0);            //!< parameters of the other programs (Water.hlsl), DF_DETAIL_LAYER and DF_TERRAIN_CLIFF
  XR_FLOAT4(vParams1);
  XR_FLOAT4(vParams2);
  XR_FLOAT4(vParams3);
  XR_UINT(nTex4);
  XR_UINT(nTex5);
  XR_UINT(nTex6);
  XR_FLOAT(fPad1);
  XR_FLOAT4X4(mParams0);          //!< a matrix of the other programs (Water.hlsl)
};

#endif
