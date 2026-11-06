// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: RayFace.hlsli
//	Description: The face of a pixel's triangle for the rays from it
//	(d3d12_RTFaceNormal: the side the lamps' shadow rays and the
//	reflections' rays start from). The world position's differences from
//	pixel to pixel (ddx, ddy) round where the camera is close: a pixel of
//	Jack's face in a cut scene is 0.1 mm, a float step of a position some
//	hundreds of metres from the level's origin 0.03 mm; their cross
//	product's normal then turns by tens of degrees from pixel to pixel,
//	and where a lamp grazes the surface its side does too: the ray starts
//	in front of the surface or behind it, a stipple of the lamp's shadow.
//	1 / w (of the clip position) is precise there, and over the screen it
//	is a plane: the triangle's plane n . (x, y, w) = d in clip space (the
//	camera at its origin, as a perspective has it) makes 1 / w = (n.x X +
//	n.y Y + n.z) / d over the NDC X, Y, an affine function of the pixel
//	whose slopes and value give n up to d; and the camera relative clip
//	(x, y, w) = R A of a world position R (row vectors) carries a normal
//	to the world as n_R = A n.
//	  FaceFromRhw: that normal (world space, not normalised, either side)
//	    from 1 / w at the pixel and its change a pixel right and down, the
//	    pixel's NDC without the jitter, the NDC a pixel spans (2 / width,
//	    -2 / height) and the world to clip matrix; 0 where 1 / w is the same
//	    everywhere (an orthographic view).
//
//////////////////////////////////////////////////////////////////////

#ifndef XR_RAY_FACE_HLSLI
#define XR_RAY_FACE_HLSLI

float3 FaceFromRhw(float fRhw, float fRhwDx, float fRhwDy, float2 vNDC, float2 vNDCPerPixel, float4x4 mViewProj)
{
  const float a = fRhwDx / vNDCPerPixel.x, b = fRhwDy / vNDCPerPixel.y;
  const float3 n = float3(a, b, fRhw - vNDC.x * a - vNDC.y * b);
  // n_R = A n: A's columns are mViewProj's x, y and w columns (their xyz).
  return mul(mViewProj, float4(n.xy, 0, n.z)).xyz;
}

#endif
