// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: ArrayCoords.hlsli
//	Description: DF_TEX0_ARRAY's coordinates (Common.hlsli, RayShade.hlsli;
//	the unit tests): u = 2 slice + u' and v = v' (u', v' from 0 to 1) for
//	one slice; v = v' + 2 m + 0.5 (m from 1 to 256) for the slice and the
//	next one (after the last the first) blended, the next one's weight
//	(m - 1) / 255: the far vegetation sprites between the pictures of the
//	two directions nearest the view (FarSprites.cpp, FarSpriteAngles.h).
//	The rasterised sprites dissolve one picture's outline into the
//	other's (DissolvePictures, PictureNoise); the rays' outline is the
//	nearer picture's (a noise of 0.5). Without resources.
//
//////////////////////////////////////////////////////////////////////

#ifndef XR_ARRAY_COORDS_HLSLI
#define XR_ARRAY_COORDS_HLSLI

//! Where a DF_TEX0_ARRAY draw samples its array.
struct SArrayCoords
{
  float3 vA;   //!< the slice's (u', v', slice)
  float3 vB;   //!< the next slice's (the slice's again without a blend)
  float fW;    //!< vB's weight
};

//! The coordinates of vTex in an array of nSlices slices.
SArrayCoords ArrayCoords(float2 vTex, uint nSlices)
{
  const float fSlice = floor(vTex.x * 0.5);
  const float m = floor(vTex.y * 0.5);
  const float2 vUV = float2(vTex.x - 2 * fSlice, m > 0 ? vTex.y - 2 * m - 0.5 : vTex.y);
  float fNext = fSlice;
  if (m > 0)
    fNext = fSlice + 1 < (float)nSlices ? fSlice + 1 : 0;
  SArrayCoords c;
  c.vA = float3(vUV, fSlice);
  c.vB = float3(vUV, fNext);
  c.fW = m > 0 ? (m - 1) / 255 : 0;
  return c;
}

//! Two pictures a and b (colour not multiplied by alpha, alpha the part covered) blended by b's weight fW: the parts
//! covered blended, the colour that of what covers (the colour of a picture's empty texels does not show); a itself at
//! fW 0.
float4 BlendPictures(float4 a, float4 b, float fW)
{
  const float fAlpha = lerp(a.a, b.a, fW);
  const float3 vCovered = a.rgb * (a.a * (1 - fW)) + b.rgb * (b.a * fW);
  const float4 vBlend = float4(fAlpha > 0 ? vCovered / fAlpha : lerp(a.rgb, b.rgb, fW), fAlpha);
  return fW > 0 ? vBlend : a;
}

//! A noise from 0 to 1 over the pixels (vPixel the pixel's position, SV_Position), its values spread evenly over every few
//! neighbours (the interleaved gradient noise, Jimenez 2014), moved along by the frame's number fFrame (0 without a
//! temporal anti-aliasing: the same each frame) so that the frames a temporal one gathers take every value.
float PictureNoise(float2 vPixel, float fFrame)
{
  const float2 p = floor(vPixel) + 5.588238 * fFrame;
  return frac(52.9829189 * frac(dot(p, float2(0.06711056, 0.00583715))));
}

//! BlendPictures' colour with the alpha of one of the pictures, b's where fNoise (PictureNoise) < fW, else a's: alpha
//! tested, as many of the pixels that only a covers as 1 - fW says are kept and as many of those only b covers as fW
//! says, the outline going from a's to b's as fW does (a's alpha blended into b's and tested changes it at once at fW
//! 0.5). a itself at fW 0.
float4 DissolvePictures(float4 a, float4 b, float fW, float fNoise)
{
  return float4(BlendPictures(a, b, fW).rgb, fNoise < fW ? b.a : a.a);
}

#endif
