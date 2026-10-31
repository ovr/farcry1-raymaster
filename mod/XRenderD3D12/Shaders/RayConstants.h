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

//! d3d12_RTDebug, the views of RayDebug.hlsl.
#define XR_RAY_DEBUG_INSTANCES  1   //!< the instances in colours, their triangles' edges darker
#define XR_RAY_DEBUG_SHADED     2   //!< the hits shaded (RayShade.hlsli) on the right half, the picture on the left
#define XR_RAY_DEBUG_DISTANCE   3   //!< the distance to the hit
#define XR_RAY_DEBUG_LOD        4   //!< the mip level of the hit's texture 0
#define XR_RAY_DEBUG_KIND       5   //!< the kind of the BLAS
#define XR_RAY_DEBUG_ALPHA      6   //!< the alpha tests along the ray
#define XR_RAY_DEBUG_SHADED_ALL 7   //!< the hits shaded over the whole picture

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
  XR_UINT4(nFlags);               //!< x 1: a history to blend into, y the frame (the noise), z d3d12_RTSunDenoise, w the mask of all
                                  //!< the casters (RayMask, the low 8 bits) and 0x100 when the scene has live casters
};

#endif
