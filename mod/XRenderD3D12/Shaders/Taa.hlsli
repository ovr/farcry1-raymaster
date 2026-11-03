// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Taa.hlsli
//	Description: TAA (d3d12_AA taa, UpscalerTaa.cpp), our own temporal
//	anti-aliasing written from the published descriptions of the method
//	(B. Karis, "High Quality Temporal Supersampling", SIGGRAPH 2014;
//	M. Salvi, "An Excursion in Temporal Supersampling", GDC 2016). Each
//	frame's scene is drawn moved by a sub-pixel jitter (8 phases of
//	Halton(2, 3) over the pixel), and each pixel blends what it shows into
//	its history, the picture of the frames before at the place the pixel
//	was in them: over the frames the pixel gathers its whole area, as a
//	picture supersampled would.
//	  - The pixel moves as the nearest (by the depth) of its 3 x 3
//	    (TaaDilate): the edge of an object in front moves with it.
//	  - The history at that place by a Catmull-Rom filter (TaaCatmullRom),
//	    sharp where a bilinear one would blur it frame after frame.
//	  - What the frame shows around the pixel bounds the history
//	    (TaaClipToBox): the history clipped towards the box of the 3 x 3's
//	    mean +- TAA_CLIP_SIGMA of their standard deviation, within their
//	    range, in YCoCg, so that a history of what is no longer there
//	    (uncovered, moved, lit anew) goes.
//	  - The frame's share: 1 / (n + 1) after n frames of history (the
//	    history's alpha counts them, up to TAA_FRAMES), at least the least
//	    share (d3d12_AAQuality), more in motion (up to the share in motion
//	    from a pixel a frame on: the history, resampled every frame, blurs
//	    as it moves); the frame and the history each weighted by
//	    1 / (1 + their luma), so that a bright pixel of one frame flickers
//	    less.
//	  - The HDR scene's light is blended exposed and compressed
//	    (TaaToWorking: c / (1 + max(r, g, b)), undone after), a highlight
//	    weighing as much as a white.
//	The sharpening after it (d3d12_UpscaleSharpness) is AMD FidelityFX
//	CAS's (TaaCas.hlsli) in TaaToCas's space.
//	The includer defines the loads first:
//	  float3 TaaLoadColor(int2 p)         the frame's colour of pixel p
//	  float TaaLoadDepth(int2 p)          its depth (0 near, 1 far)
//	  float2 TaaLoadVelocity(int2 p)      its motion vector: where it was
//	                                      in the frame before minus where
//	                                      it is (uv)
//	  float2 TaaLoadMotion(int2 p)        its motion as TaaDilate gives it
//	  float4 TaaSampleHistory(float2 uv)  the history, bilinearly
//	the loads clamped into the picture, the samples clamped to it.
//
//////////////////////////////////////////////////////////////////////

#ifndef XR_TAA_HLSLI
#define XR_TAA_HLSLI

//! The history's standard deviations around the 3 x 3's mean that it keeps.
#define TAA_CLIP_SIGMA 1.25
//! The frames the history's alpha counts (alpha 1: this many or more).
#define TAA_FRAMES 32.0

//! The colours as TAA blends them: LDR colours as they are (fExposure 0); the HDR light exposed by fExposure and
//! compressed into 0..1, c / (1 + max(r, g, b)).
float3 TaaToWorking(float3 c, float fExposure)
{
  if (fExposure <= 0)
    return c;
  c = max(c * fExposure, 0);
  return c / (1 + max(c.r, max(c.g, c.b)));
}

//! And back.
float3 TaaFromWorking(float3 c, float fExposure)
{
  if (fExposure <= 0)
    return c;
  c = max(c, 0);
  return c / (max(1 - max(c.r, max(c.g, c.b)), 1.0 / 65536) * fExposure);
}

float3 TaaRgbToYCoCg(float3 c)
{
  return float3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b, 0.5 * c.r - 0.5 * c.b, -0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}

float3 TaaYCoCgToRgb(float3 c)
{
  return float3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

//! h moved along the line to the box's centre onto the box (vMin..vMax), when it is outside it.
float3 TaaClipToBox(float3 h, float3 vMin, float3 vMax)
{
  const float3 c = 0.5 * (vMax + vMin), e = 0.5 * (vMax - vMin) + 1.0 / 65536;
  const float3 d = h - c;
  const float3 u = abs(d) / e;
  const float m = max(u.x, max(u.y, u.z));
  return m > 1 ? c + d / m : h;
}

//! The history at uv by a Catmull-Rom filter over its 4 x 4 texels around uv, as five bilinear samples (the middle two
//! texels of each way by one sample between them; the four corners, whose weights are the least, left out and the rest
//! weighed to 1). vSize: x width, y height, z 1 / width, w 1 / height.
float4 TaaCatmullRom(float2 uv, float4 vSize)
{
  const float2 vPos = uv * vSize.xy;
  const float2 vTexel = floor(vPos - 0.5) + 0.5;
  const float2 f = vPos - vTexel;
  // The cubic's weights of the texels at -1, 0, 1 and 2 from vTexel.
  const float2 w0 = f * (-0.5 + f * (1 - 0.5 * f));
  const float2 w1 = 1 + f * f * (-2.5 + 1.5 * f);
  const float2 w2 = f * (0.5 + f * (2 - 1.5 * f));
  const float2 w3 = f * f * (-0.5 + 0.5 * f);
  const float2 w12 = w1 + w2;
  const float2 t0 = (vTexel - 1) * vSize.zw, t12 = (vTexel + w2 / w12) * vSize.zw, t3 = (vTexel + 2) * vSize.zw;
  const float4 v = TaaSampleHistory(float2(t12.x, t0.y)) * (w12.x * w0.y) + TaaSampleHistory(float2(t0.x, t12.y)) * (w0.x * w12.y) +
                   TaaSampleHistory(t12) * (w12.x * w12.y) + TaaSampleHistory(float2(t3.x, t12.y)) * (w3.x * w12.y) +
                   TaaSampleHistory(float2(t12.x, t3.y)) * (w12.x * w3.y);
  const float fWeight = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
  return v / fWeight;
}

//! The motion of pixel p: that of the nearest of its 3 x 3 (the first of equals, row by row from the top left).
float2 TaaDilate(int2 p)
{
  int2 q = p;
  float fNearest = TaaLoadDepth(p);
  [unroll] for (int y = -1; y <= 1; y++)
  {
    [unroll] for (int x = -1; x <= 1; x++)
    {
      const float d = TaaLoadDepth(p + int2(x, y));
      if (d < fNearest)
      {
        fNearest = d;
        q = p + int2(x, y);
      }
    }
  }
  return TaaLoadVelocity(q);
}

//! What TaaResolve gives for a pixel.
struct STaaPixel
{
  float3 vColor;      //!< the picture's colour (the colours' space: the LDR colours', the HDR light's)
  float fFrames;      //!< the history's alpha: the frames it holds / TAA_FRAMES
  float fShare;       //!< the frame's share in the colour (1: no history)
  float fClip;        //!< how far the box moved the history (0 not at all)
  float fLuma;        //!< the colour's luma as blended (0..1)
  float fLeast;       //!< the frame's least share at the pixel's motion
};

//! The pixel p of the picture: the frame blended into the history. vSize as TaaCatmullRom's, fExposure as TaaToWorking's,
//! fLeastShare the frame's least share at rest, fMovingShare from a pixel's motion a frame on, bReset: no history.
STaaPixel TaaResolve(int2 p, float4 vSize, float fExposure, float fLeastShare, float fMovingShare, bool bReset)
{
  // The frame's 3 x 3 in YCoCg: the pixel, the mean and the standard deviation, the range.
  float3 vFrame = 0, m1 = 0, m2 = 0, vMin = 1e30, vMax = -1e30;
  [unroll] for (int y = -1; y <= 1; y++)
  {
    [unroll] for (int x = -1; x <= 1; x++)
    {
      const float3 c = TaaRgbToYCoCg(TaaToWorking(TaaLoadColor(p + int2(x, y)), fExposure));
      if (x == 0 && y == 0)
        vFrame = c;
      m1 += c;
      m2 += c * c;
      vMin = min(vMin, c);
      vMax = max(vMax, c);
    }
  }
  m1 /= 9;
  m2 /= 9;
  const float3 vSigma = sqrt(max(m2 - m1 * m1, 0));
  // The box holds the pixel itself: a history that agrees with it stays even where it is the 3 x 3's extreme.
  const float3 vBoxMin = min(max(vMin, m1 - TAA_CLIP_SIGMA * vSigma), vFrame), vBoxMax = max(min(vMax, m1 + TAA_CLIP_SIGMA * vSigma), vFrame);

  // The history where the pixel was, unless that is off the picture.
  const float2 vMotion = TaaLoadMotion(p);
  const float2 uv = ((float2)p + 0.5) * vSize.zw, vBefore = uv + vMotion;
  const float fLeast = lerp(fLeastShare, max(fMovingShare, fLeastShare), saturate(length(vMotion * vSize.xy)));
  float fFrames = 0, fClip = 0;
  float3 vHistory = vFrame;
  if (!bReset && all(vBefore >= 0) && all(vBefore <= 1))
  {
    const float4 h = TaaCatmullRom(vBefore, vSize);
    fFrames = saturate(h.a) * TAA_FRAMES;
    const float3 vHeld = TaaRgbToYCoCg(TaaToWorking(max(h.rgb, 0), fExposure));
    vHistory = TaaClipToBox(vHeld, vBoxMin, vBoxMax);
    fClip = saturate(4 * length(vHistory - vHeld));
  }
  const float fShare = max(1 / (fFrames + 1), fLeast);
  const float wFrame = fShare / (1 + vFrame.x), wHistory = (1 - fShare) / (1 + max(vHistory.x, 0));
  const float3 v = (vFrame * wFrame + vHistory * wHistory) / (wFrame + wHistory);

  STaaPixel r;
  r.vColor = TaaFromWorking(TaaYCoCgToRgb(v), fExposure);
  r.fFrames = min(fFrames + 1, TAA_FRAMES) / TAA_FRAMES;
  r.fShare = wFrame / (wFrame + wHistory);
  r.fClip = fClip;
  r.fLuma = saturate(v.x);
  r.fLeast = fLeast;
  return r;
}

//! The debug view of a pixel (d3d12_AADebug 1), in the colours' space: the picture grey, yellow by how far the box moved
//! the history, red by the frame's share above the least (the history gone: uncovered, off the picture, the first frames).
float3 TaaDebug(STaaPixel r, float fExposure)
{
  const float fNew = saturate((r.fShare - r.fLeast) / max(1 - r.fLeast, 1e-3));
  float3 v = 0.2 + 0.6 * r.fLuma;
  v = lerp(v, float3(1, 0.85, 0), r.fClip);
  v = lerp(v, float3(1, 0, 0), fNew);
  return TaaFromWorking(min(v, 0.95), fExposure);
}

//! The picture's colour as the sharpening (CAS, TaaCas.hlsli) takes it, linear light from 0 to 1: the HDR light exposed
//! and compressed as the history blends it (TaaToWorking, fExposure > 0), the LDR colours (the game's, gamma encoded)
//! decoded.
float3 TaaToCas(float3 c, float fExposure)
{
  return fExposure > 0 ? TaaToWorking(c, fExposure) : pow(saturate(c), 2.2);
}

//! And back.
float3 TaaFromCas(float3 c, float fExposure)
{
  return fExposure > 0 ? TaaFromWorking(c, fExposure) : pow(saturate(c), 1 / 2.2);
}

#endif
