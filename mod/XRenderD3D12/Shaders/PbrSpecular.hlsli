// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: PbrSpecular.hlsli
//	Description: The highlights of the specular templates by the
//	roughness of the texture's RayMaster material map (r_PbrRoughness;
//	its red, PbrCompanion.h; General.hlsl's Specular). The templates
//	give every material the lobe saturate(R.V)^32, R the light reflected
//	about the normal; by the perceptual roughness r (alpha = r^2, the
//	Beckmann roughness) its power is n = (2 / alpha^2 - 2) / 4
//	(Blinn-Phong's power of N.H for that roughness, R.V's lobe about as
//	wide at a quarter of it), between PBR_SPEC_LEAST and PBR_SPEC_MOST,
//	and its height (n + 1) / (32 + 1) times the templates' (the lobe's
//	integral over the directions about R, 2 pi / (n + 1), the same as
//	the templates': as much light reflected, a broad lobe dimmer, a
//	narrow one brighter); the templates' 32 is the lobe of r = 0.35.
//	Where the normal changes across the pixel the lobe is wider:
//	alpha^2 + min(2 sigma^2 (|dN/dx|^2 + |dN/dy|^2), kappa)
//	(Tokuyoshi and Kaplanyan 2019, "Improved geometric specular
//	antialiasing"), no sparkle of the narrow lobes over bumps far off.
//	The reflections by rays of the metals and smooth materials
//	(d3d12_RTPbrReflections): their ray's direction by GGX's visible
//	normals (PbrReflectDir), the share they reflect (PbrEnvBrdf), less
//	towards the rough (PbrReflectWeight), less where the texture's ambient
//	occlusion hides the lobe (r_PbrAO: PbrSpecOcclusion); where the last frame's
//	picture saw what a ray hits (d3d12_RTPbrScreen: PbrPrevUV,
//	PbrViewDistance, PbrPrevShare); one ray for each 2 x 2 pixels where
//	their reflections go the same way (d3d12_RTPbrQuad: PbrQuadStart,
//	PbrQuadLane, PbrQuadShares).
//	Stands alone (the unit tests' TestHeaders.hlsl).
//
//////////////////////////////////////////////////////////////////////

#ifndef XR_PBR_SPECULAR_HLSLI
#define XR_PBR_SPECULAR_HLSLI

//! The templates' power of saturate(R.V) (General.hlsl's Specular).
static const float PBR_SPEC_TEMPLATE = 32;
//! The lobe's least and most power (r about 0.9 and 0.17 and beyond).
static const float PBR_SPEC_LEAST = 1;
static const float PBR_SPEC_MOST = 512;
//! The pixel's filter's variance (1 / (2 pi)) and the most the normal's change widens alpha^2 by (Tokuyoshi and
//! Kaplanyan's sigma^2 and kappa).
static const float PBR_SPEC_SIGMA2 = 0.15915494;
static const float PBR_SPEC_KAPPA = 0.18;

//! alpha^2 of the perceptual roughness fRough (0 to 1, clamped), widened by fNormalChange = |dN/dx|^2 + |dN/dy|^2 of
//! the shading normal across the pixel.
float PbrSpecAlpha2(float fRough, float fNormalChange)
{
  const float r = saturate(fRough);
  const float a = r * r;
  return saturate(a * a + min(2 * PBR_SPEC_SIGMA2 * max(fNormalChange, 0), PBR_SPEC_KAPPA));
}

//! The lobe of alpha^2: x its power of saturate(R.V), y its height against the templates' lobe.
float2 PbrSpecLobe(float fAlpha2)
{
  const float n = clamp((2 / max(fAlpha2, 1e-6) - 2) / 4, PBR_SPEC_LEAST, PBR_SPEC_MOST);
  return float2(n, (n + 1) / (PBR_SPEC_TEMPLATE + 1));
}

// The reflections by rays of d3d12_RTPbrReflections (General.hlsl): one ray a pixel, its direction the view reflected
// about a normal of the surface's microfacets as GGX's distribution of the visible normals spreads them (Heitz 2018,
// "Sampling the GGX distribution of visible normals"), another each frame (the temporal anti-aliasing gathers them);
// what it finds weighted by the share of the light the surface reflects that way over the lobe (Karis 2014's fit of
// the split sum's scale and bias to F0), fading out towards the rough (beyond PBR_REFL_ROUGH_TO no ray: the highlights
// alone, the noise of a broad lobe's single ray too much for the frames to gather); a dielectric's sooner (it reflects
// 4 % head on: worth a ray where it is glossy alone, PBR_REFL_DIELECTRIC_FROM to _TO), between them by the metalness.
static const float PBR_REFL_ROUGH_FROM = 0.45;
static const float PBR_REFL_ROUGH_TO = 0.65;
static const float PBR_REFL_DIELECTRIC_FROM = 0.2;
static const float PBR_REFL_DIELECTRIC_TO = 0.35;
//! The least alpha a ray's lobe has (a mirror's: the distribution's formulas degenerate at 0).
static const float PBR_REFL_ALPHA_LEAST = 0.002;
//! F0 of a dielectric (the share it reflects head on: 4 %).
static const float PBR_REFL_DIELECTRIC = 0.04;
//! How fast a reflection's cone spreads by alpha (radians a radian of alpha): the detail of what it finds.
static const float PBR_REFL_SPREAD = 0.5;

//! The share of the reflection by rays a surface of the perceptual roughness fRough and the metalness fMetal keeps: 1
//! smooth, 0 rough.
float PbrReflectWeight(float fRough, float fMetal)
{
  const float m = saturate(fMetal);
  return 1 - smoothstep(lerp(PBR_REFL_DIELECTRIC_FROM, PBR_REFL_ROUGH_FROM, m), lerp(PBR_REFL_DIELECTRIC_TO, PBR_REFL_ROUGH_TO, m), fRough);
}

//! A microfacet normal of GGX's visible normals for the view vVe (towards the eye, the surface's frame: its normal
//! +z; vVe.z > 0) and alpha (isotropic), u two numbers 0 to 1 (Heitz 2018, listing 1).
float3 PbrGgxVisibleNormal(float3 vVe, float fAlpha, float2 u)
{
  const float a = max(fAlpha, PBR_REFL_ALPHA_LEAST);
  const float3 vVh = normalize(float3(a * vVe.x, a * vVe.y, max(vVe.z, 1e-4)));
  const float fLen2 = vVh.x * vVh.x + vVh.y * vVh.y;
  const float3 vT1 = fLen2 > 0 ? float3(-vVh.y, vVh.x, 0) * rsqrt(fLen2) : float3(1, 0, 0);
  const float3 vT2 = cross(vVh, vT1);
  const float r = sqrt(saturate(u.x));
  const float fPhi = 6.28318531 * u.y;
  const float t1 = r * cos(fPhi);
  const float s = 0.5 * (1 + vVh.z);
  const float t2 = (1 - s) * sqrt(saturate(1 - t1 * t1)) + s * r * sin(fPhi);
  const float3 vNh = t1 * vT1 + t2 * vT2 + sqrt(saturate(1 - t1 * t1 - t2 * t2)) * vVh;
  return normalize(float3(a * vNh.x, a * vNh.y, max(vNh.z, 0)));
}

//! The direction a reflection's ray takes: the view vToEye (from the surface towards the eye, unit) reflected about a
//! visible microfacet normal of the shading normal vN (unit) for alpha and u (PbrGgxVisibleNormal); the mirror's
//! direction where that would go under the surface.
float3 PbrReflectDir(float3 vToEye, float3 vN, float fAlpha, float2 u)
{
  // The surface's frame about vN (Duff et al. 2017).
  const float fSign = vN.z >= 0 ? 1 : -1;
  const float a = -1 / (fSign + vN.z);
  const float b = vN.x * vN.y * a;
  const float3 vT = float3(1 + fSign * vN.x * vN.x * a, fSign * b, -fSign * vN.x);
  const float3 vB = float3(b, fSign + vN.y * vN.y * a, -vN.y);
  const float3 vVe = float3(dot(vToEye, vT), dot(vToEye, vB), dot(vToEye, vN));
  const float3 vH = PbrGgxVisibleNormal(vVe, fAlpha, u);
  const float3 vR = reflect(-vToEye, vH.x * vT + vH.y * vB + vH.z * vN);
  return dot(vR, vN) > 1e-3 ? vR : reflect(-vToEye, vN);
}

//! The share of the light coming in over the lobe a surface of the perceptual roughness fRough reflects at N.V fNV:
//! F0 * x + y (Karis 2014, "Physically based shading on mobile": the split sum's scale and bias fitted).
float2 PbrEnvBrdf(float fRough, float fNV)
{
  const float4 c0 = float4(-1, -0.0275, -0.572, 0.022);
  const float4 c1 = float4(1, 0.0425, 1.04, -0.04);
  const float4 r = saturate(fRough) * c0 + c1;
  const float a004 = min(r.x * r.x, exp2(-9.28 * saturate(fNV))) * r.x + r.y;
  // (The fit's bias a little under 0 for the roughest: none.)
  return max(float2(-1.04, 1.04) * a004 + r.zw, 0);
}

//! r_PbrAO: the share of the light over the lobe a surface lets through where its texture's ambient occlusion is fAO
//! (the share of the hemisphere open), at N.V fNV and alpha fAlpha (Lagarde and de Rousiers 2014, "Moving Frostbite to
//! PBR": the lobe's share in the open cone; 1 where nothing occludes, about fAO for a rough lobe, more than it for a
//! narrow one seen from above, less towards the grazing view).
float PbrSpecOcclusion(float fNV, float fAO, float fAlpha)
{
  const float a = saturate(fAO);
  return saturate(pow(saturate(fNV) + a, exp2(-16 * saturate(fAlpha) - 1)) - 1 + a);
}

//! d3d12_RTPbrScreen (General.hlsl's PrevFrameAt): a world point's place in the last frame's picture from its clip
//! position c there (world to that frame's clip space, without its jitter) and that jitter (in clip space: xy += vJitter *
//! w): xy its uv (0 to 1, y down), z its distance along that view (c.w); all 0 behind that camera.
float3 PbrPrevUV(float4 c, float2 vJitter)
{
  if (c.w <= 1e-3)
    return 0;
  const float2 vNdc = c.xy / c.w + vJitter;
  return float3(0.5 + 0.5 * vNdc.x, 0.5 - 0.5 * vNdc.y, c.w);
}

//! The distance along the view (clip w) of the depth buffer's fDepth by the projection's m22 and m32 (row vectors, the
//! camera looking down -z: clip z = m22 z + m32, w = -z).
float PbrViewDistance(float fDepth, float m22, float m32)
{
  return m32 / (fDepth + m22);
}

static const float PBR_PREV_TOLERANCE = 0.03;        // of the distance, PBR_PREV_TOLERANCE_LEAST m at least
static const float PBR_PREV_TOLERANCE_LEAST = 0.05;
static const float PBR_PREV_EDGE = 0.05;             // the share of the picture at its sides the share fades over

//! The share of the last frame's picture at uv for a point fDistance along that view, the picture having seen fSeen
//! along it there: 1 where the two are within the tolerance (fWidth more: the ray's cone that wide there), less over
//! the picture's last PBR_PREV_EDGE, 0 off it or where it saw something nearer (the point hidden) or farther.
float PbrPrevShare(float2 uv, float fDistance, float fSeen, float fWidth)
{
  if (any(uv <= 0) || any(uv >= 1) || fDistance <= 0)
    return 0;
  if (abs(fSeen - fDistance) > max(PBR_PREV_TOLERANCE * fDistance, PBR_PREV_TOLERANCE_LEAST) + fWidth)
    return 0;
  const float2 vEdge = saturate(min(uv, 1 - uv) / PBR_PREV_EDGE);
  return vEdge.x * vEdge.y;
}

//! d3d12_RTPbrQuad: the lane of the 2 x 2 quad (0 top left, 1 top right, 2 bottom left, 3 bottom right) its first lane to
//! trace the quad's ray, another each frame: the frame's place in the order 0, 3, 1, 2 (the diagonal first: the quad's
//! pixels in 2 frames from far apart), offset by the quad's own (vQuad: its place in pixels / 2) so that neighbouring
//! quads take different ones (no pattern of the whole picture moving from frame to frame).
uint PbrQuadStart(uint2 vQuad, uint nFrame)
{
  const uint nOrder = 0x9C; // 0, 3, 1, 2: two bits each, from the lowest
  return ((nOrder >> (2 * ((nFrame + vQuad.x + 2 * vQuad.y) & 3))) & 3);
}

//! The lane of the quad that traces its ray: the first from nStart on (in the order nStart, nStart + 1, ... wrapping)
//! whose bit (1 << lane) nMask has (the lanes that reflect, not the helpers); 4 none of them.
uint PbrQuadLane(uint nMask, uint nStart)
{
  [unroll]
  for (uint i = 0; i < 4; i++)
  {
    const uint l = (nStart + i) & 3;
    if (nMask & (1u << l))
      return l;
  }
  return 4;
}

static const float PBR_QUAD_ANGLE = 0.03;        // rad: the most two pixels' mirror directions may be apart to share a ray,
static const float PBR_QUAD_ANGLE_LOBE = 0.5;    // ... and this much of alpha more (the lobe about as wide: what it blurs)

//! Whether a pixel whose view reflects about its normal to vMirror (unit) takes the ray of the quad's lane whose view
//! reflects to vLane: they are at most PBR_QUAD_ANGLE + PBR_QUAD_ANGLE_LOBE alpha apart (a bumpy smooth metal's pixels
//! each their own: the bumps' reflections kept; a rough one's lobe blurs as much).
bool PbrQuadShares(float3 vMirror, float3 vLane, float fAlpha)
{
  return dot(vMirror, vLane) >= cos(PBR_QUAD_ANGLE + PBR_QUAD_ANGLE_LOBE * fAlpha);
}

#endif // XR_PBR_SPECULAR_HLSLI
