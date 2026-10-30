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

//! The kinds of instances: bits of their masks (d3d12_RTMask picks those the rays see).
#define XR_RAY_STATIC     1     //!< a BLAS kept from frame to frame: static vertices and indices
#define XR_RAY_ALPHATEST  2     //!< an alpha tested draw (its triangles not opaque)
#define XR_RAY_DYNAMIC    4     //!< a BLAS of this frame: vertices or indices in per frame memory

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

#endif
