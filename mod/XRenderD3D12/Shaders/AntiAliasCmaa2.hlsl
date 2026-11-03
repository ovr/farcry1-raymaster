// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: AntiAliasCmaa2.hlsl
//	Description: The compute kernels of CMAA2 (d3d12_AA cmaa2, PostAA.cpp):
//	CMAA2.hlsl of third_party/cmaa2 as it is, with its entry points
//	(Shaders.cmake), the edges' kernel compiled with the preset of
//	d3d12_AAQuality (CMAA2_STATIC_QUALITY_PRESET 0 to 3):
//	  EdgesColor2x2CS          the edges and the shapes to process
//	  ComputeDispatchArgsCS    the next indirect dispatch's arguments
//	  ProcessCandidatesCS      the shapes' new colours (indirect)
//	  DeferredColorApply2x2CS  those colours into the picture (indirect)
//	The picture: t0 its sRGB view (B8G8R8A8_UNORM_SRGB or
//	R8G8B8A8_UNORM_SRGB, linear light), u0 its UNORM view of the same
//	texture, into which CMAA2 writes the pixels it anti-aliases, in place,
//	converted to sRGB, with the alpha 0; the luma of its edges from the
//	colour (CMAA2_EDGE_DETECTION_LUMA_PATH 1, its default). u1..u7 its
//	working buffers. Bindings: the wide root signature (D3D12Compute.h).
//
//////////////////////////////////////////////////////////////////////

// The picture's UAV is a typed UNORM view of an sRGB texture (its integration guide's common case).
#define CMAA2_UAV_STORE_TYPED 1
#define CMAA2_UAV_STORE_CONVERT_TO_SRGB 1
#define CMAA2_UAV_STORE_TYPED_UNORM_FLOAT 1

#include "CMAA2.hlsl"
