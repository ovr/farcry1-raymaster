// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: RayConstants.h
//	Description: The scene for rays (RayTracing.h), shared by C++ and the
//	ray tracing shaders (only they include it).
//
//	Root signature (D3D12Pipeline.h), the parameters of the programs with
//	rays:
//	  t0, space5: RaytracingAccelerationStructure g_RayScene (root SRV 3)
//	  t1, space5: StructuredBuffer<SRayInstance> g_RayInstances (root SRV 4),
//	              one per instance of the TLAS, by its InstanceID
//	  t0, space4: ByteAddressBuffer g_Buffers[] (the texture table): the raw
//	              views of the vertices and indices (CD3D12Upload::GetRawSRV)
//	  t2, space5: StructuredBuffer<SPTLight> g_PTLights, the path tracer's
//	              lights (the kernels with rays only, D3D12Compute.h)
//	Every instance of the TLAS has the kind of its draw (XR_RAY_*) as its
//	mask and as its InstanceContributionToHitGroupIndex, which a RayQuery
//	reads back (CommittedInstanceContributionToHitGroupIndex).
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_RAYCONSTANTS_H
#define XRENDERD3D12_RAYCONSTANTS_H

#include "Constants.h"

//! The kinds of instances: bits of their masks (the TLAS has those whose kinds d3d12_RTMask has all of).
#define XR_RAY_STATIC     1     //!< a BLAS kept from frame to frame: static vertices and indices
#define XR_RAY_ALPHATEST  2     //!< an alpha tested draw (its triangles not opaque)
#define XR_RAY_DYNAMIC    4     //!< a BLAS of this frame: vertices or indices in per frame memory
//! In the masks only (not a kind): a caster the engine names for its own shadows in the frame, the
//! live set of the sun's cascades (characters, vehicles, the vegetation that casts), which casts on
//! the light maps and the terrain (the sun's shadow by rays, RaySun.hlsl).
#define XR_RAY_LIVE       8
//! In the masks only: the terrain's sectors (the path tracer's ray towards the sun over the terrain alone, the shadows
//! its texture has baked in: PathShade.hlsli). The rays of d3d12_RTMask never have it (RayMask).
#define XR_RAY_TERRAIN    16
//! In the masks: a piece of the sky (a surface of a sky box's texture, CTexture::m_bSky: the windows of Training's bunker),
//! its instance's mask this alone (its kinds in InstanceContributionToHitGroupIndex). The rays of RayMask see it; the
//! volumetric light's towards the sun pass it (VolumetricRays.hlsl).
#define XR_RAY_SKY        32

//! d3d12_RTDebug, the views of RayDebug.hlsl.
#define XR_RAY_DEBUG_INSTANCES  1   //!< the instances in colours (of the mesh and where it is, to 0.25 m: not the index of the
                                    //!< frame's list), their triangles' edges darker
#define XR_RAY_DEBUG_SHADED     2   //!< the hits shaded (RayShade.hlsli) on the right half, the picture on the left
#define XR_RAY_DEBUG_DISTANCE   3   //!< the distance to the hit
#define XR_RAY_DEBUG_LOD        4   //!< the mip level of the hit's texture 0
#define XR_RAY_DEBUG_KIND       5   //!< the kind of the BLAS
#define XR_RAY_DEBUG_ALPHA      6   //!< the alpha tests along the ray
#define XR_RAY_DEBUG_SHADED_ALL 7   //!< the hits shaded over the whole picture
#define XR_RAY_DEBUG_PT_ALBEDO  8   //!< the path tracer's albedo of the hits (PathShade.hlsli)
#define XR_RAY_DEBUG_PT_EMISSION 9  //!< its emission of the hits
#define XR_RAY_DEBUG_PT_DIRECT  10  //!< the hits lit by the sun and the lamps as the path tracer lights them, with their emission
#define XR_RAY_DEBUG_MATERIALS  11  //!< a colour per material the rays take of the hit's instance (SRayInstance but where its
                                    //!< geometry is): the same from frame to frame while that material is
#define XR_RAY_DEBUG_FACING     12  //!< the side of the hit's triangle as the raster's culling sees it: its front green, its back
                                    //!< red (a surface the raster shows from there is green: a red one only the rays see)
#define XR_RAY_DEBUG_SUN_WAY    13  //!< the way to the sun from the hit, as the volumetric light's rays take it (not the pieces of
                                    //!< the sky): reaching it white, hidden the distance to what hides it in the distance's colours
#define XR_RAY_DEBUG_SUN_AIR    14  //!< the air the sun reaches along the camera's ray to its hit, as the volumetric light's rays
                                    //!< find it but without its froxels: the shafts as they are in the scene for rays, white
// The path tracer's G-buffer (d3d12_PTDebug 1 to 4, PathTrace.cpp; Shaders/PathGBuffer.hlsli), no rays
#define XR_RAY_DEBUG_GB_ALBEDO  16  //!< the albedo, the game's colour
#define XR_RAY_DEBUG_GB_NORMAL  17  //!< the normal, rgb = n * 0.5 + 0.5; no surface black
#define XR_RAY_DEBUG_GB_FOG     18  //!< the fog's share, grey
#define XR_RAY_DEBUG_GB_MATERIAL 19 //!< the material over the albedo's grey: lit grey, the terrain green, the vegetation yellow; none black
#define XR_RAY_DEBUG_GB_LIGHT   20  //!< the light of the lamps and the bounces on the surfaces as the composite takes it (PathTrace.hlsl CSFilter), grey
#define XR_RAY_DEBUG_GB_RAYS    21  //!< the same as the paths of the frame found it (CSTrace), no history or filter
#define XR_RAY_DEBUG_GB_SUN     22  //!< the sun's visibility the raster found (the G-buffer's), grey
#define XR_RAY_DEBUG_GB_AMBIENT 23  //!< the game's ambient light the paths fill in with (the G-buffer's), as the game's colour
#define XR_RAY_DEBUG_GB_SPECULAR 24 //!< the highlights' colour (the G-buffer's: the material's specular colour times its gloss, times 2)
#define XR_RAY_DEBUG_GB_HIGHLIGHTS 25 //!< the highlights of the sun and the lamps the composite adds (CSSpecular), the game's colour
#define XR_RAY_DEBUG_GB_CACHE   26  //!< the radiance cache's light at the surfaces (CSTrace with SPathTraceConstants::nCache.z), grey
#define XR_RAY_DEBUG_GB_TERRAIN 27  //!< the terrain's share of the light baked into its texture as the composite takes it (CSFilter), grey;
                                    //!< what is not the terrain black

//! SRayInstance: no view, no stream.
#define XR_RAY_NONE         0xffffffff
//! SRayInstance::nLayout.x: 32 bit indices.
#define XR_RAY_INDEX32      0x80000000
//! The alpha tests of a ray: the candidates after these are taken as they are.
#define XR_RAY_MAX_ALPHA_TESTS  16

//! An instance of the TLAS (g_RayInstances[its InstanceID]): where the vertices and indices of its
//! BLAS are, and the part of its draw's SDrawConstants the hits are shaded with (RayShade.hlsli).
struct SRayInstance
{
  XR_UINT4(nGeom);        //!< x the vertices' raw view (g_Buffers), y the bytes to the BLAS's vertex 0 in it; z the indices'
                          //!< (XR_RAY_NONE: not indexed), w the bytes to the BLAS's first index in it
  XR_UINT4(nLayout);      //!< x the stride | XR_RAY_INDEX32; y the bytes to the normal, the colour, uv0 and uv1 in a vertex
                          //!< (8 bits each from the lowest, 0xff: not there); z the raw view of the stream of uv1
                          //!< (VERTEX_FORMAT_TEX2F, XR_RAY_NONE: none), w the bytes to the BLAS's vertex 0 in it
  XR_UINT4(nMaterial);    //!< x nFlags, y nFlags2, z nTex0, w nTex1
  XR_UINT4(nMaterial2);   //!< x nSamplers, y fAlphaRef (its bits), z nTex4, w nTex2
  XR_FLOAT4(vColor);
  XR_FLOAT4(vAmbient);
  XR_FLOAT4(vTexGenU);
  XR_FLOAT4(vTexGenV);
  XR_FLOAT4(vTexMat0);
  XR_FLOAT4(vTexMat1);
  XR_FLOAT4(vAddColor);
  // The path tracer's material (d3d12_PathTracing, PathShade.hlsli):
  XR_FLOAT4(vAlbedo);     //!< rgb what the texture is multiplied by for the albedo: vColor without the light it has (the object's
                          //!< ambient colour of the templates lit by it alone)
  XR_FLOAT4(vSpecular);   //!< rgb the material's specular colour (0: none), w its shininess (the exponent of N.H)
  XR_UINT4(nPath);        //!< x XR_PT_MAT_*, y the game's ambient light on it (what its ambient colour adds to its albedo, the
                          //!< game's colour) / 2, 8 bits a channel from the lowest (PathPackAmbient); z a hash of the mesh
                          //!< (the static buffers and the part of them, this frame's by the counts: d3d12_RTDebug 1's colours);
                          //!< w unused
};

//! SRayInstance::nPath.x, how the path tracer takes the draw's colour.
#define XR_PT_MAT_LIT         0   //!< lit by its lights (the sun, the ambient, the lamps, a light map): the albedo is its colour without them
#define XR_PT_MAT_TERRAIN     1   //!< a sector of the terrain: its texture has the sun and the terrain's shadows in it (d3d12_PTTerrainDelight)
#define XR_PT_MAT_VEGETATION  2   //!< the vegetation: the vertex colour times 2 (DF_VCOLOR_X2), the light the renderer bakes into it left
                                  //!< out with d3d12_PathTracing (LeafBuffer.cpp)
#define XR_PT_MAT_UNLIT       3   //!< drawn without light (the templates of no lights, the vertex colour alone): its colour is its emission
//! The material of a draw of the vegetation (DF_VCOLOR_X2) by its flags (DF_*, Constants.h): its leaves (alpha tested: thin, a
//! path goes through them, PathLeafSide) XR_PT_MAT_VEGETATION, the rest of the plant (a trunk, its branches: opaque and closed, a
//! path through its face went into it and came back dark) XR_PT_MAT_LIT.
#define XR_PT_VEGETATION_MATERIAL(nFlags) (((nFlags) & DF_ALPHATEST) ? XR_PT_MAT_VEGETATION : XR_PT_MAT_LIT)

//! The path tracer's lights (d3d12_PathTracing, PathTrace.cpp): the frame's lights besides the sun and the level's static ones,
//! the nearest at most this many a frame.
#define XR_PT_MAX_LIGHTS 128
//! The path tracer's radiance cache (d3d12_PTCache, PathCache.hlsli): its entries (a power of 2) and its buffer's bytes (an
//! entry's key, its samples' sums and count, its light).
#define XR_PT_CACHE_ENTRIES (1u << 20)
#define XR_PT_CACHE_BYTES (XR_PT_CACHE_ENTRIES * 28u)
//! SPTLight::nParams.x, the kind of light.
#define XR_PT_LIGHT_POINT        1   //!< from vPos over vPos.w metres (DLF_POINT)
#define XR_PT_LIGHT_DIRECTIONAL  2   //!< from the direction vPos.xyz, unattenuated (DLF_DIRECTIONAL without DLF_SUN)
#define XR_PT_LIGHT_PROJECTOR    3   //!< a point light through the picture of its cube (DLF_PROJECT with m_pLightImage)
//! SPTLight::nParams.y, bits.
#define XR_PT_LIGHT_LM           1   //!< baked into the light maps (DLF_LM): the light maps are not read in its place
#define XR_PT_LIGHT_FILL         2   //!< a fill light of the characters (DLF_AMBIENT_LIGHT)
#define XR_PT_LIGHT_HEAT         4   //!< also a heat source (DLF_HEATSOURCE)
#define XR_PT_LIGHT_STATIC       8   //!< a static light of the level the frame does not have (I3DEngine::GetStaticLightSources)

//! A light of the frame for the path tracer (g_PTLights, root SRV t2 space5 of the kernels and the programs with rays), the
//! nearest first.
struct SPTLight
{
  XR_FLOAT4(vPos);        //!< xyz the world position (point, projector) or the direction towards the light (directional), w its radius (m)
  XR_FLOAT4(vColor);      //!< rgb the game's colour (m_Color: the shaders linearise it), w the least gap short of it the rays towards it
                          //!< end (m; a projector's d3d12_RTProjectorShadowGap, else 0: SPathShade::fLightGap)
  XR_FLOAT4(vSpecular);   //!< rgb its highlights' colour (m_SpecColor, the game's colour), w unused
  XR_UINT4(nParams);      //!< x the kind (XR_PT_LIGHT_*), y XR_PT_LIGHT_* bits, z the projector's cube (texture table, XR_RAY_NONE none),
                          //!< w the light's index in the frame's lights (its bit in the objects' light masks; XR_RAY_NONE: a static
                          //!< light alone)
  XR_FLOAT4X4(mProject);  //!< the projector's m_TextureMatrix (Direct3D style, the world point times it is its cube's lookup vector)
};

// The threads a side of a group of the sun's shadow by rays' kernels (RaySun.hlsl, RaySun.cpp)
#define XR_RAY_SUN_GROUP 8

//! b2 of the sun's shadow by rays' kernels (RaySun.hlsl, RaySun.cpp).
struct SRaySunConstants
{
  XR_FLOAT4X4(mPrevViewProj);     //!< world to clip in the frame before, without its jitter: where a point was in the history
  XR_FLOAT4(vCameraPos);          //!< xyz the camera
  XR_FLOAT4(vRayX);               //!< the world position at the view depth d and x, y -1..1 across the view (y up), the jitter
  XR_FLOAT4(vRayY);               //!< taken off: vCameraPos + d (vRayO + x vRayX + y vRayY)
  XR_FLOAT4(vRayO);
  XR_FLOAT4(vSize);               //!< x width, y height, z 1 / width, w 1 / height (the depth buffer's pixels)
  XR_FLOAT4(vJitter);             //!< xy this frame's jitter (SFrameConstants::vJitter.xy), zw the frame before's
  XR_FLOAT4(vSun);                //!< xyz the direction to the sun's centre, w the tangent of half the angle of its disc
  XR_FLOAT4(vSunX);               //!< xyz: two directions across the disc (unit, normal to vSun and to each other)
  XR_FLOAT4(vSunY);
  XR_FLOAT4(vParams);             //!< x the mask's range (view depth), y where it starts to give way to the cascades, z the rays'
                                  //!< length (m), w a pixel's size at a view depth of 1 m
  XR_UINT4(nTextures);            //!< texture table indices: x the depth buffer, y CSTrace's rays, z the history of the frame
                                  //!< before (CSTemporal), w this frame's (CSFilter)
  XR_UINT4(nTrace);               //!< the rays' grid: x the pixels a side of a cell (1, or 2 with d3d12_RTSunShadows 1), y z the
                                  //!< pixel of a cell traced in this frame
  XR_UINT4(nFlags);               //!< x a history to blend into: the frames it holds at most (low 16 bits; 0: none) and 0x10000
                                  //!< for the path tracer's (soft where the shadow moves), y the frame (the noise), z
                                  //!< 1 d3d12_RTSunDenoise, 2 d3d12_RTSunCrease, w the mask of all the casters (RayMask, the
                                  //!< low 8 bits) and 0x100 when the scene has live casters
};

//! b2 of the kernels around AMD FidelityFX Denoiser's shadows denoiser (d3d12_RTSunDenoise 2, RaySunFfx.hlsl,
//! RaySun.cpp): the view's fields as SRaySunConstants' (RayView.hlsli), then the denoiser's.
struct SRaySunFfxConstants
{
  XR_FLOAT4X4(mPrevViewProj);     //!< world to clip in the frame before, without its jitter
  XR_FLOAT4(vCameraPos);          //!< xyz the camera
  XR_FLOAT4(vRayX);               //!< the world position at the view depth d and x, y -1..1 across the view (y up), the jitter
  XR_FLOAT4(vRayY);               //!< taken off: vCameraPos + d (vRayO + x vRayX + y vRayY)
  XR_FLOAT4(vRayO);
  XR_FLOAT4(vSize);               //!< x width, y height, z 1 / width, w 1 / height (the depth buffer's pixels)
  XR_FLOAT4(vJitter);             //!< xy this frame's jitter, zw the frame before's
  XR_FLOAT4(vParams);             //!< x the mask's range (view depth), y where it starts to give way to the cascades, z unused,
                                  //!< w a pixel's size at a view depth of 1 m
  XR_UINT4(nTextures);            //!< texture table indices: x the depth buffer, y CSTrace's rays (a ray a pixel), z w unused
  XR_UINT4(nTrace);               //!< x 1 (a ray a pixel), y z 0
  XR_UINT4(nSet);                 //!< x the set of casters (0 all, 1 the live ones), y 1: the first frame (no history), z the
                                  //!< filter's pass (0 to 2), w its step (1, 2, 4)
  XR_UINT4(nInputs);              //!< texture table indices: x the normals, y this frame's depth, z the frame before's, w the
                                  //!< history (the frame before's filter's second pass)
  XR_UINT4(nInputs2);             //!< x the moments of the frame before, y the filter pass's input, z w the denoised visibility of
                                  //!< all and of the live casters (CSPack)
};

// The threads a side of a group of the ambient occlusion by rays' kernels (RayAO.hlsl, RayAO.cpp)
#define XR_RAY_AO_GROUP 8

//! b2 of the ambient occlusion by rays' kernels (RayAO.hlsl, RayAO.cpp); the view's fields as
//! SRaySunConstants' (RayView.hlsli).
struct SRayAOConstants
{
  XR_FLOAT4X4(mPrevViewProj);     //!< world to clip in the frame before, without its jitter: where a point was in the history
  XR_FLOAT4(vCameraPos);          //!< xyz the camera
  XR_FLOAT4(vRayX);               //!< the world position at the view depth d and x, y -1..1 across the view (y up), the jitter
  XR_FLOAT4(vRayY);               //!< taken off: vCameraPos + d (vRayO + x vRayX + y vRayY)
  XR_FLOAT4(vRayO);
  XR_FLOAT4(vSize);               //!< x width, y height, z 1 / width, w 1 / height (the depth buffer's pixels)
  XR_FLOAT4(vJitter);             //!< xy this frame's jitter (SFrameConstants::vJitter.xy), zw the frame before's
  XR_FLOAT4(vParams);             //!< x the rays' length (d3d12_AORadius, m), y the view depth beyond which all is open, z a
                                  //!< pixel's size at a view depth of 1 m, w the frames a history holds at most
  XR_UINT4(nTextures);            //!< texture table indices: x the depth buffer, y CSTrace's rays, z the history of the frame
                                  //!< before (CSTemporal), w this frame's (CSFilter)
  XR_UINT4(nTrace);               //!< the rays' grid: x the pixels a side of a cell (1 or 2), y z the pixel of a cell traced in
                                  //!< this frame
  XR_UINT4(nFlags);               //!< x 1: a history to blend into, y the frame (the noise), z 1: denoised (the history and the
                                  //!< filter), w the mask of the instances the rays see (RayMask, the low 8 bits; the GI's with
                                  //!< 0x100, the sun's shadow at the hits)
  // The rays' GI (RAY_GI; vParams.x is then d3d12_RTGIDistance):
  XR_FLOAT4(vGI);                 //!< x d3d12_RTGIBounce
  XR_FLOAT4(vOutdoorAmbient);     //!< rgb the 3D engine's outdoor ambient colour (a hit's without one of its own)
  XR_UINT4(nTextures2);           //!< texture table indices: x the frames of the history before, y this frame's
};

// The threads a side of a group of the path tracer's kernels (PathTrace.hlsl, PathTrace.cpp)
#define XR_PT_GROUP 8

//! b2 of the path tracer's kernels (PathTrace.hlsl, PathTrace.cpp); the view's fields as SRayAOConstants' (RayView.hlsli).
struct SPathTraceConstants
{
  XR_FLOAT4X4(mPrevViewProj);     //!< world to clip in the frame before, without its jitter: where a point was in the history
  XR_FLOAT4(vCameraPos);          //!< xyz the camera
  XR_FLOAT4(vRayX);               //!< the world position at the view depth d and x, y -1..1 across the view (y up), the jitter
  XR_FLOAT4(vRayY);               //!< taken off: vCameraPos + d (vRayO + x vRayX + y vRayY)
  XR_FLOAT4(vRayO);
  XR_FLOAT4(vSize);               //!< x width, y height, z 1 / width, w 1 / height (the depth buffer's and the G-buffer's pixels)
  XR_FLOAT4(vJitter);             //!< xy this frame's jitter (SFrameConstants::vJitter.xy), zw the frame before's
  XR_FLOAT4(vParams);             //!< x the rays' length (m), y a pixel's size at a view depth of 1 m, z the frames a history holds
                                  //!< at most, w the most light a path's sample brings (luminance; 0: no limit)
  XR_FLOAT4(vSun);                //!< rgb the sun's light (SPathShade::vSun), w d3d12_PTLamps
  XR_FLOAT4(vSky);                //!< rgb the sky's light where a path finds nothing, w d3d12_PTEmission
  XR_FLOAT4(vShade);              //!< x d3d12_PTTerrainDelight, y the lamps' rays' gap (d3d12_RTLightShadowGap), z
                                  //!< d3d12_PTBakedFalloff, w d3d12_PTLampSize (a lamp's radius over its light's)
  XR_FLOAT4(vFill);               //!< x d3d12_PTAmbient (the game's ambient light a path's ray brings beyond y), y
                                  //!< d3d12_PTAmbientRange (m), z d3d12_PTLightMaps where the frame has no sun (0: no light
                                  //!< maps' light, PathLightmapFill)
  XR_FLOAT4(vSpecular);           //!< rgb the sun's highlights' colour (its specular colour times d3d12_PTSpecular; 0 indoors), w
                                  //!< the lamps' highlights' scale (CSSpecular)
  XR_UINT4(nTextures);            //!< texture table indices: x the depth buffer, y CSTrace's paths, z the history of the frame
                                  //!< before (CSTemporal), w this frame's (CSFilter)
  XR_UINT4(nTextures2);           //!< texture table indices: x the G-buffer's normal, y the frames of the history before, z this
                                  //!< frame's, w the G-buffer's sun and ambient light
  XR_UINT4(nTextures3);           //!< texture table indices: x the G-buffer's highlights' colour, y NRD's light (CSResolve), z the
                                  //!< scene's motion vectors (CSPack; XR_RAY_NONE: the camera's from mPrevViewProj), w the hit
                                  //!< distance of the paths' first bounce (CSPack)
  XR_UINT4(nTrace);               //!< RayView.hlsli's grid: x 1 (a path for each pixel), y z 0
  XR_UINT4(nFlags);               //!< x 1: a history to blend into, y the frame (the noise), z 1: filtered, w the mask of the
                                  //!< instances the rays see (RayMask)
  XR_UINT4(nPath);                //!< x the lamps (g_PTLights), y the bounces of a path, z the alpha tests of a ray at most, w 1:
                                  //!< the reference (no limit to a sample's light)
  XR_FLOAT4(vNrd);                //!< NRD (PathNrd.hlsl): xyz its hit distances' normalization (A, B, C of REBLUR), w the view
                                  //!< depth it takes where there is no surface (beyond its denoising range)
  XR_FLOAT4(vCache);              //!< the radiance cache (PathCache.hlsli): x its cells' size at the camera (m), y their size per
                                  //!< metre of distance, z the frames its light holds at most, w the jitter of a lookup (cells)
  XR_UINT4(nCache);               //!< x 1: CSTrace's last bounce takes it, y 1: its light starts again (CSCacheResolve), z 1:
                                  //!< CSTrace writes it at the pixels' surfaces (d3d12_PTDebug 11), w CSCacheUpdate's tile (pixels)
  XR_UINT4(nCheckerboard);        //!< x 1: CSTrace traces half the pixels in a checkerboard (d3d12_PTCheckerboard; a thread
                                  //!< for each two pixels of a row), y those it traces: ((x ^ y) & 1) == y
  XR_FLOAT4(vBakeLight);          //!< rgb the light baked into the terrain on a lit open face towards the sun, each channel
                                  //!< (SPathShade::vBakeLight)
  XR_UINT4(nTextures4);           //!< texture table indices: x the G-buffer's light map light (D3D12_PT_LIGHTMAP_FORMAT), y z w
                                  //!< unused
};

#endif
