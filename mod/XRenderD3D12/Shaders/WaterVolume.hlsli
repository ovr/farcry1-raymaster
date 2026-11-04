// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: WaterVolume.hlsli
//	Description: The light in the ocean's water (Water.hlsl with
//	d3d12_WaterVolume; the unit tests' kernels), in linear light: the
//	water's attenuation in each channel from its fog volume's colour and
//	density, the light of the scene behind the surface that comes through
//	it, the light the water scatters towards the camera (single scattering
//	of the light coming down, which fades with the depth, in segments
//	along the view's ray, each with its own share of the sun) and the
//	window of the light from the air seen from below (Snell's, Fresnel's
//	transmission).
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_WATERVOLUME_HLSLI
#define XRENDERD3D12_WATERVOLUME_HLSLI

//! Water's index of refraction.
static const float VOLUME_ETA = 1.333;
//! A channel's attenuation at most this many times the fog volume's density (a channel of the colour near 0).
static const float VOLUME_MOST_RATIO = 64;
//! Pure water's absorption per metre in the red, green and blue channels (Pope and Fry 1997 at 610, 550 and 470 nm): no
//! water is clearer.
static const float3 VOLUME_PURE_WATER = float3(0.2644, 0.0565, 0.0106);
//! The points along the view's ray in the water whose sun the light it scatters takes, out to this many times the mean
//! path of its least attenuated channel's light (1 / k: 95 % of that channel's light comes from nearer).
#define XR_VOLUME_TAPS 4
static const float VOLUME_REACH = 3;

//! The ocean's water's attenuation per metre in each channel from its fog volume's colour vColor (linear) and density
//! fDensity (per metre: ln 100 / (2 * its distance)): the colour's luminance (Rec. 709) fDensity (the fog's distance is
//! how far one sees), each channel as many times it as it is darker than the luminance (deep water's colour is the light
//! it scatters back over the light it absorbs: a channel half as bright is absorbed twice as fast), at most
//! VOLUME_MOST_RATIO times, at least pure water's absorption; a black colour fDensity in all three. The light coming down
//! fades as fast (diffuse attenuation taken as the beam's).
float3 VolumeAttenuation(float3 vColor, float fDensity)
{
  const float Y = dot(vColor, float3(0.2126, 0.7152, 0.0722));
  return max(Y > 0 ? fDensity * (Y / max(vColor, Y / VOLUME_MOST_RATIO)) : fDensity, VOLUME_PURE_WATER);
}

//! The cosine to the vertical of a ray refracted into the water whose cosine to the vertical in the air is fCos.
float VolumeRefractedCos(float fCos)
{
  return sqrt(1 - saturate(1 - fCos * fCos) / (VOLUME_ETA * VOLUME_ETA));
}

//! The view's ray refracted into the water at the normal N (up), V towards the camera (above the surface); a facet turned
//! away from the camera refracts the view's ray as if it grazed it (V moved into the facet's plane).
float3 VolumeRefract(float3 V, float3 N)
{
  float c = dot(N, V);
  if (c < 0)
  {
    V = normalize(V - c * N);
    c = 0;
  }
  const float e = 1 / VOLUME_ETA;
  return -e * V + (e * c - sqrt(1 - e * e * (1 - c * c))) * N;
}

//! The light the water scatters towards the camera from a view ray going down at fMu (the cosine to the vertical of its
//! direction in the water) from the surface, from fT0 to fT1 metres along it, the water's attenuation c and the light
//! coming down fading by exp(-Kd * depth): the light scattered at each point (J0 exp(-Kd mu t)) less what the water takes
//! of it on the way back (exp(-c t)), J0 = vDeep (c + Kd) so that infinitely deep water seen straight down is vDeep (its
//! fog volume's colour): vDeep (c + Kd) / (c + Kd mu) (exp(-k fT0) - exp(-k fT1)), k = c + Kd mu.
float3 VolumeInscatter(float3 vDeep, float3 c, float3 Kd, float fMu, float fT0, float fT1)
{
  const float3 k = c + Kd * fMu;
  return vDeep * (c + Kd) / k * (exp(-k * fT0) - exp(-k * fT1));
}

//! Where along the view's ray (m) the light it scatters ends its segment j of XR_VOLUME_TAPS out to fReach: their lengths
//! growing as (j + 1)^2, the nearer ones shorter (the light scattered near the surface counts most), the last at fReach.
float VolumeSegmentEnd(uint j, float fReach)
{
  const float f = (j + 1) / (float)XR_VOLUME_TAPS;
  return fReach * f * f;
}

//! The point of segment j whose sun it takes: half way through it in the square root of the distance.
float VolumeSegmentPoint(uint j, float fReach)
{
  const float f = (j + 0.5) / (float)XR_VOLUME_TAPS;
  return fReach * f * f;
}

//! How far along the view's ray (m) the segments reach: VOLUME_REACH over the least of the in-scatter's attenuations k, at
//! most the path to the scene behind fPath.
float VolumeReach(float3 k, float fPath)
{
  return min(fPath, VOLUME_REACH / max(min(k.r, min(k.g, k.b)), 1e-4));
}

//! Where the sun's ray to a point fDepth metres down left the surface (z of the point plus fDepth): from the point vPos up
//! along the sun's ray refracted into the water (L the sun's direction in the air, up).
float3 VolumeSunEntry(float3 vPos, float fDepth, float3 L)
{
  const float2 h = L.xy / VOLUME_ETA;
  const float z = sqrt(saturate(1 - dot(h, h)));
  return vPos + float3(h / max(z, 1e-3), 1) * fDepth;
}

//! The share of the light from the air that passes the surface towards a view inside the water at fCos (the cosine of the
//! view's ray's angle to the surface's normal, in the water): 1 less Fresnel's reflectance of unpolarized light from water
//! to air (0.98 straight up), none beyond the critical angle (cos 0.661: total internal reflection).
float SnellWindow(float fCos)
{
  const float ci = saturate(fCos), s2 = VOLUME_ETA * VOLUME_ETA * (1 - ci * ci);
  if (s2 >= 1)
    return 0;
  const float ct = sqrt(1 - s2);
  const float rs = (VOLUME_ETA * ci - ct) / (VOLUME_ETA * ci + ct), rp = (ci - VOLUME_ETA * ct) / (ci + VOLUME_ETA * ct);
  return 1 - 0.5 * (rs * rs + rp * rp);
}

#endif
