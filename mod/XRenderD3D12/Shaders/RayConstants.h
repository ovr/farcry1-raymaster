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
//	Every instance of the TLAS has the kind of its draw (XR_RAY_*) as its
//	mask and as its InstanceContributionToHitGroupIndex, which a RayQuery
//	reads back (CommittedInstanceContributionToHitGroupIndex).
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_RAYCONSTANTS_H
#define XRENDERD3D12_RAYCONSTANTS_H

//! The kinds of instances: bits of their masks (d3d12_RTMask picks those the rays see).
#define XR_RAY_STATIC     1     //!< a BLAS kept from frame to frame: static vertices and indices
#define XR_RAY_ALPHATEST  2     //!< an alpha tested draw (its triangles not opaque)
#define XR_RAY_DYNAMIC    4     //!< a BLAS of this frame: vertices or indices in per frame memory

//! d3d12_RTDebug, the views of RayDebug.hlsl.
#define XR_RAY_DEBUG_INSTANCES  1   //!< the instances in colours, their triangles' edges darker
#define XR_RAY_DEBUG_DISTANCE   3   //!< the distance to the hit
#define XR_RAY_DEBUG_KIND       5   //!< the kind of the BLAS

#endif
