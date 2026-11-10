// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Renderer.cpp
//	Description: Window, device, frame, camera, matrices and queries of the
//	Direct3D 12 renderer.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "Geometry.h"
#include "D3D12Device.h"
#include "D3D12Upload.h"
#include "D3D12Descriptors.h"
#include "D3D12Pipeline.h"
#include "D3D12Compute.h"
#include "NrdHost.h"
#include "RayTracing.h"
#include "Texture.h"
#include "Shader.h"
#include "Scene.h"
#include "CpuSampler.h"
#include "CommandLine.h"

CD3D12Renderer *g_pRenderer = nullptr;

namespace
{

constexpr uint32 nFrameRingSize = 32 << 20;   //!< per frame memory per frame slot
constexpr float fPi = 3.14159265358979f;
char s_szVertexProfile[] = "vs_6_0";
char s_szPixelProfile[] = "ps_6_0";

//! XRENDERD3D12_DEBUG: 2 the debug layer alone, any other value but 0 with GPU based validation (1).
int DebugEnv()
{
  char szValue[16];
  const DWORD nLen = GetEnvironmentVariableA("XRENDERD3D12_DEBUG", szValue, sizeof(szValue));
  if (!nLen || nLen >= sizeof(szValue) || strcmp(szValue, "0") == 0)
    return 0;
  return strcmp(szValue, "2") == 0 ? 2 : 1;
}

//! XRENDERD3D12_DRED: 0, 1 or 2 (d3d12_DRED's values); -1 when it is not set.
int DredEnv()
{
  char szValue[16];
  const DWORD nLen = GetEnvironmentVariableA("XRENDERD3D12_DRED", szValue, sizeof(szValue));
  if (!nLen || nLen >= sizeof(szValue))
    return -1;
  return std::clamp(atoi(szValue), 0, 2);
}

int GetCVarInt(const char *szName, int nDefault)
{
  ICVar *pVar = iConsole ? iConsole->GetCVar(szName) : nullptr;
  return pVar ? pVar->GetIVal() : nDefault;
}

//! The styles of our own window: windowed (with a frame) and full screen (borderless).
constexpr DWORD nWindowedStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE;
constexpr DWORD nFullScreenStyle = WS_POPUP | WS_VISIBLE;

//! FarCry.exe declares no DPI awareness, so at a display scale above 100 % Windows would give the
//! window logical sizes and stretch it as a bitmap. Per monitor aware (v2), as XRenderD3D9Traced
//! makes the process before it loads a renderer (then this fails and changes nothing); looked up
//! at run time as there (Windows 10 1703). True when this call made the process aware.
bool MakeDpiAware()
{
  typedef BOOL (WINAPI *PFN_SetProcessDpiAwarenessContext)(HANDLE);
  HMODULE hUser32 = GetModuleHandleA("user32.dll");
  const auto pfnSet = hUser32 ? (PFN_SetProcessDpiAwarenessContext)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext") : nullptr;
  return pfnSet && pfnSet((HANDLE)-4);  // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
}

Matrix44 Identity44()
{
  Matrix44 m;
  m.SetIdentity();
  return m;
}

//! Rotation by fAngle degrees about the axis (x, y, z) for row vectors (v * M).
Matrix44 Rotation44(float fAngle, float x, float y, float z)
{
  Matrix44 m = Identity44();
  const float fLen = sqrtf(x * x + y * y + z * z);
  if (fLen <= 0)
    return m;
  x /= fLen; y /= fLen; z /= fLen;
  const float a = fAngle * fPi / 180.0f, c = cosf(a), s = sinf(a), t = 1 - c;
  // The transpose of the column vector rotation matrix.
  m.data[0] = t * x * x + c;     m.data[1] = t * x * y + s * z; m.data[2] = t * x * z - s * y;
  m.data[4] = t * x * y - s * z; m.data[5] = t * y * y + c;     m.data[6] = t * y * z + s * x;
  m.data[8] = t * x * z + s * y; m.data[9] = t * y * z - s * x; m.data[10] = t * z * z + c;
  return m;
}

//! v * M for a point (w = 1); returns w.
float TransformPoint(const Matrix44 &m, const float v[3], float out[3])
{
  float r[4];
  for (int j = 0; j < 4; j++)
    r[j] = v[0] * m.data[j] + v[1] * m.data[4 + j] + v[2] * m.data[8 + j] + m.data[12 + j];
  out[0] = r[0]; out[1] = r[1]; out[2] = r[2];
  return r[3];
}

}

CD3D12Renderer::CD3D12Renderer()
{
  g_pRenderer = this;
  m_CameraView = Identity44();
  m_ViewMatrix = Identity44();
  m_ProjMatrix = Identity44();
  m_SkyProjMatrix = Identity44();
  m_ReflectionViewProj = Identity44();
}

CD3D12Renderer::~CD3D12Renderer()
{
  if (g_pRenderer == this)
    g_pRenderer = nullptr;
}

void CD3D12Renderer::RegisterVariables()
{
  // Looked up by name by the 3D engine; defaults as the original renderer's.
  iConsole->Register("r_Quality_BumpMapping", &CV_r_Quality_BumpMapping, 2, VF_DUMPTODISK | VF_REQUIRE_APP_RESTART);
  iConsole->Register("r_WaterReflections", &CV_r_WaterReflections, 1, VF_DUMPTODISK);
  iConsole->Register("r_WaterRefractions", &CV_r_WaterRefractions, 0, VF_DUMPTODISK);
  iConsole->Register("r_WaterUpdateFactor", &CV_r_WaterUpdateFactor, 0.01f, VF_DUMPTODISK);
  iConsole->Register("r_WaterUpdateDistance", &CV_r_WaterUpdateDistance, 2.0f, 0);
  iConsole->Register("r_WaterUpdateDeltaAngle", &CV_r_WaterUpdateDeltaAngle, 5.0f, 0);
  iConsole->Register("r_WaterUpdateMinInterval", &CV_r_WaterUpdateMinInterval, 1.0f / 120, 0);
  iConsole->Register("r_EnvCMResolution", &CV_r_EnvCMResolution, 2, VF_DUMPTODISK);
  iConsole->Register("r_EnvCMupdateInterval", &CV_r_EnvCMupdateInterval, 0.1f, VF_DUMPTODISK);
  iConsole->Register("r_DisableSfx", &CV_r_DisableSfx, 0, VF_DUMPTODISK);
  iConsole->Register("r_ResetScreenFx", &CV_r_ResetScreenFx, 0, 0);
  iConsole->Register("r_TexResolution", &CV_r_TexResolution, 0, VF_DUMPTODISK);
  iConsole->Register("r_TexBumpResolution", &CV_r_TexBumpResolution, 0, VF_DUMPTODISK);
  iConsole->Register("r_TexSkyResolution", &CV_r_TexSkyResolution, 0, VF_DUMPTODISK);
  iConsole->Register("r_DetailTextures", &CV_r_DetailTextures, 1, VF_DUMPTODISK);
  iConsole->Register("r_DetailScale", &CV_r_DetailScale, 8, 0);
  iConsole->Register("r_DetailDistance", &CV_r_DetailDistance, 4, VF_DUMPTODISK);
  iConsole->CreateVariable("r_DetailDefault", "Textures/Detail/rock", 0);
  iConsole->Register("r_VSync", &CV_r_vsync, 0, VF_DUMPTODISK);
  RegisterOtherVariables();
  TakeDisplayInfo();
  iConsole->Register("d3d12_Debug", &CV_d3d12_debug, 0, VF_DUMPTODISK, "Direct3D 12 debug layer (read at start-up): 1 with GPU based validation, 2 without it");
  iConsole->Register("d3d12_DRED", &CV_d3d12_DRED, 1, VF_DUMPTODISK,
                     "DRED, what the GPU did when the device is removed, in XRenderD3D12_dred.txt (read at start-up): 0 off, "
                     "1 the page faults and the renderer's sections, 2 also every command (slower)");
  iConsole->Register("d3d12_DredTest", &CV_d3d12_DredTest, 0, 0,
                     "Tests DRED's report in the next frame: 1 removes the device, 2 makes a GPU page fault (the driver resets the GPU); "
                     "the renderer draws nothing after either");
  iConsole->Register("d3d12_DumpFrame", &CV_d3d12_DumpFrame, 0, 0, "Writes the render items and draws of the next frame to XRenderD3D12_frame.txt");
  iConsole->Register("d3d12_WaterDebug", &CV_d3d12_WaterDebug, 0, 0,
                     "Water surfaces show one term: 1 refraction, 2 reflection, 3 water thickness (0-2 m: black-white), 4 normal, 5 screen space reflection hits, 6 their colour, 7 the planar reflection, "
                     "8 the scene behind, 9 view path (red, 0-32 m), depth (green, 0-16 m) and fresnel (blue), 10 the vertex colour, 11 its alpha, "
                     "12 the RMS of the wave slopes finer than a pixel (white: 0.1); with d3d12_RTReflections: 13 the rays' hits, 14 their distance "
                     "(blue near, red at d3d12_RTDistance), 15 how much they count; 16 the sun's glint, 17 its light through the crests, 18 the variance of its slopes (white: 0.05), 19 the sun's shadow on the water (black), "
                     "20 the height of the waves the crests' light reads (black -2, white 2 of its RMS); with d3d12_WaterFoam: 21 the foam's "
                     "share of the pixel, 22 the whitecaps' amount, 23 the shore's (all three without the beaches' foam strips); "
                     "with d3d12_WaterVolume: 24 the light the ocean's water scatters, 25 the share of the scene behind it left "
                     "(from below the share of Snell's window), 26 the share of the sun's light left in the light it scatters; "
                     "with d3d12_WaterWakes: 27 the wakes' slopes, 28 their foam; with d3d12_WaterGeometry: 29 the grid's height "
                     "(red up and blue down, 0.3 m from grey; green 3 cm), 30 its shares of the waves (red the depth's, green the "
                     "bodies', blue the camera's and the distance's); under the ocean with d3d12_WaterUnder: 31 the light its "
                     "water scatters towards the camera, 32 the sun's light in it (grey its mean: black its shadow, brighter "
                     "the caustics' shafts), 33 the share of the scene behind left (the other modes there without the water's "
                     "light); with d3d12_WaterFoam on the ocean's grid: 34 the floor behind against the level's terrain heights "
                     "(red above, blue below, 0.5 m full; green where the foam takes the heights: d3d12_WaterFoamTerrain), "
                     "35 the open sea's share (white; black on the ponds the terrain cuts off from it: d3d12_WaterFoamPonds)");
  iConsole->Register("d3d12_Caustics", &CV_d3d12_Caustics, 1, VF_DUMPTODISK, "Caustics on the terrain under the ocean; 0 off");
  // Not saved (d3d12_WaterSunGlint before it was, at 0).
  iConsole->Register("d3d12_WaterGlint", &CV_d3d12_WaterGlint, 1.0f, 0,
                     "The sun's glint on water surfaces, the facets of the waves that mirror it (the sun's path far away, in its "
                     "shadow none): its strength (0: none, as the game draws water)");
  iConsole->Register("d3d12_WaterGlintRoughness", &CV_d3d12_WaterGlintRoughness, 0.004f, 0,
                     "The variance of the slopes of the water's ripple finer than its wave texture, which widens the sun's glint "
                     "(added to that of the ripple finer than a pixel)");
  iConsole->Register("d3d12_WaterGlintSky", &CV_d3d12_WaterGlintSky, 1, VF_DUMPTODISK,
                     "1: the sun's glint on the water only where the sky box shows a sun towards it (its brightness there, from "
                     "0.12 none to 0.24 all): none under a storm's cloud, in smoke, under a night sky whose moon is painted "
                     "elsewhere; 0: everywhere");
  iConsole->Register("d3d12_WaterCrestLight", &CV_d3d12_WaterCrestLight, 1.0f, 0,
                     "The sun's light through the thin crests of the waves, turquoise, seen against the sun: its strength (0: none, "
                     "as the game draws water)");
  iConsole->Register("d3d12_WaterFFT", &CV_d3d12_WaterFFT, 1, 0,
                     "1: the waves of a wind sea on the water (Tessendorf's FFT ocean: JONSWAP's spectrum of d3d12_WaterWind, "
                     "three cascades of 250, 40.5 and 6.9 m), running at their own speeds and changing their shape; 0: the static "
                     "wave texture, as before");
  iConsole->Register("d3d12_WaterWind", &CV_d3d12_WaterWind, 3.5f, 0,
                     "The wind (m/s, at least 0.5) of the FFT ocean's sea (d3d12_WaterFFT): the length of its waves, how far "
                     "they follow the wind, their speeds");
  iConsole->Register("d3d12_WaterSlope", &CV_d3d12_WaterSlope, 0.0446f, 0,
                     "The RMS along an axis of the FFT ocean's slopes at the level's default bump amounts (0.03 and 0.02; "
                     "other amounts in proportion): 0.0446 the old wave texture's; 0: the wind sea's own (0.096 at 3.5 m/s)");
  iConsole->Register("d3d12_WaterChop", &CV_d3d12_WaterChop, 2.5f, 0,
                     "The FFT ocean's waves moved along the wind sea's horizontal displacement times this (Tessendorf's lambda; "
                     "1: each wave a trochoid of Gerstner's), narrow sharp crests and wide troughs: 2.5 the crests up to 3 times "
                     "narrower at 3.5 m/s, none folding over; 0: none");
  iConsole->Register("d3d12_WaterFoam", &CV_d3d12_WaterFoam, 1.0f, 0,
                     "The FFT ocean's foam (d3d12_WaterFFT): whitecaps where its crests break (Monahan's share of the sea at the "
                     "wind: hardly any below 5 m/s) and the foam of the waves breaking on the shore, the rocks and what stands in "
                     "the water (shallower than the waves' height over 0.78): its strength; 0: none, the frames before");
  iConsole->Register("d3d12_WaterWhitecapWind", &CV_d3d12_WaterWhitecapWind, 4.0f, VF_DUMPTODISK,
                     "With d3d12_WaterFoam: the wind (m/s) below which the FFT ocean has no whitecaps, the onset of "
                     "whitecapping (Beaufort's 3): none on the game's calm sea (3.5 m/s), no lone whitecap on Training's bay; "
                     "Monahan's share from it on; 0: at any wind (the frames before)");
  iConsole->Register("d3d12_WaterFoamTerrain", &CV_d3d12_WaterFoamTerrain, 0.2f, 0,
                     "With d3d12_WaterFoam on the ocean's grid: where the floor behind the water is the terrain as drawn (within "
                     "this many m of the level's heights there), the foam takes the water's depth over those heights, not over "
                     "the triangles of the terrain's level of detail, which change from frame to frame; 0: over what is drawn");
  iConsole->Register("d3d12_WaterFoamFar", &CV_d3d12_WaterFoamFar, 1, 0,
                     "With d3d12_WaterFoam on the ocean's grid, 1: no shore's foam where nothing is drawn behind the water or "
                     "the floor behind lies off the level's terrain (the engine's filler past the map's edge, which rises "
                     "through the water far out): no thin torn line of foam across the open sea (Training's bay, 700 m off); "
                     "0: the foam of that floor's depth (the frames before)");
  iConsole->Register("d3d12_WaterFoamPonds", &CV_d3d12_WaterFoamPonds, 1, 0,
                     "With d3d12_WaterFoam on the ocean's grid, 1: no whitecaps, shore's foam or surf on the ponds and lakes "
                     "the level's terrain cuts off from the sea (water under the level not joined to the map's edge through "
                     "water): Swamp's shallow ponds still, not white from bank to bank; 0: the ocean's foam on them too "
                     "(the frames before)");
  iConsole->Register("d3d12_WaterSurf", &CV_d3d12_WaterSurf, 0.8f, 0,
                     "With d3d12_WaterFoam: the height (m) of the swell (9 s) breaking on the ocean's shores, where the water "
                     "gets shallower than it over 0.78; its bores run to the shore, a white roller on each front and the lace "
                     "it leaves; 0: none (only the wind sea's)");
  iConsole->Register("d3d12_WaterSurfNight", &CV_d3d12_WaterSurfNight, 0.3f, VF_DUMPTODISK,
                     "The share of the surf's foam (d3d12_WaterSurf) shown under a moon, the engine's sun's light (its colour's "
                     "luminance) up to 0.36, all from 0.42 (by day 0.46 to 0.78): Steam's night shore no longer lies under a "
                     "white line 3.5 to 7 times brighter than its dark islands; 1: as by day");
  iConsole->Register("d3d12_WaterPondRipple", &CV_d3d12_WaterPondRipple, 0.05f, VF_DUMPTODISK,
                     "With d3d12_WaterGeometry and the level's terrain: the slopes' RMS (along an axis) of the original's ripple "
                     "on the ponds and lakes the terrain cuts off from the sea, in place of the FFT ocean's waves: its terrain "
                     "water's 32 frames (textures/animated/water/caust##, 20 a second) at a tile of 8 m drifting, a large slow "
                     "ripple with dark crests as on Swamp's ponds; 0: the waves");
  iConsole->Register("d3d12_WaterCaustics", &CV_d3d12_WaterCaustics, 1, 0,
                     "With d3d12_WaterFFT and d3d12_Caustics: the caustics on the sea floor made by the FFT ocean's waves (the "
                     "sun's rays refracted by its middle and short cascades, their light at the floor's depth), in the sun's "
                     "light only; 0: the static caustics texture, the frames before");
  iConsole->Register("d3d12_WaterVolume", &CV_d3d12_WaterVolume, 1, 0,
                     "The light in the ocean's water: each colour channel attenuated as fast as the level's water colour is "
                     "dark in it (turquoise shallows, the floor fading into deep water), the light it scatters fading with the "
                     "depth, in the sun's shadow less; from below Snell's window; 0: the fog volume's fog, the frames before");
  iConsole->Register("d3d12_WaterWakes", &CV_d3d12_WaterWakes, 1, 0,
                     "The waves of what moves through the ocean's surface (boats, swimmers, bodies: their part in the water), "
                     "in a window of 128 m around the camera (deep water's waves, Kelvin's wedge behind a boat, rings round "
                     "a swimmer), the foam of a fast hull and of the waves breaking; 0: none, the frames before");
  iConsole->Register("d3d12_WaterGeometry", &CV_d3d12_WaterGeometry, 1, 0,
                     "With d3d12_WaterFFT: the ocean drawn as a fine grid around the camera raised to the waves' height (the "
                     "FFT ocean's and the wakes'; their crests against the sky and the shore), lower in shallow water, flat "
                     "under the boats and at a camera at the water; 0: the game's flat disc, the frames before");
  iConsole->Register("d3d12_WaterDeferred", &CV_d3d12_WaterDeferred, 1, 0,
                     "With d3d12_WaterGeometry: the ocean's grid drawn into the depth alone, then its water shaded once a pixel "
                     "over the view where it is (its small triangles shaded the pixels along their edges twice); 0: the grid "
                     "shaded as it is drawn");
  iConsole->Register("d3d12_WaterAsync", &CV_d3d12_WaterAsync, 1, 0,
                     "The FFT ocean's waves, foam and caustics made on the compute queue beside the frame's shadows and opaque "
                     "surfaces, the frame waiting for them at the first draw that reads them; 0: in the frame's list");
  iConsole->Register("d3d12_WaterSSR", &CV_d3d12_WaterSSR, 1, 0,
                     "The water's reflection of what the screen shows (the scene's copy, marched along the reflected ray); 0: "
                     "none (the planar reflection, the sky or the rays in its place), to measure what it costs");
  iConsole->Register("d3d12_WaterUnder", &CV_d3d12_WaterUnder, 1, 0,
                     "With d3d12_WaterVolume, the camera under the ocean's surface: the light in its water over the scene (each "
                     "colour channel attenuated along the view and the light's way down, the light the water scatters fading "
                     "with the depth), the sun's shafts through the waves (its shadow and the FFT ocean's caustics along the "
                     "view), the glow round the sun (sea water scatters forwards); 0: the fog volume's fog, the frames before");
  iConsole->Register("d3d12_WaterUnderShare", &CV_d3d12_WaterUnderShare, 1, 0,
                     "With d3d12_WaterUnder: the sun's light in the water at one point of each pixel's view ray in a pass of its "
                     "own, each pixel taking the 16 of the 4 x 4 window round it as the 16 segments of its ray, the segments' "
                     "weights in closed form; 0: four points a pixel shared by its 2 x 2 quad, each segment's weights by its "
                     "own exponentials (the frames before)");
  iConsole->Register("d3d12_WaterUnderClear", &CV_d3d12_WaterUnderClear, 1, 0,
                     "With d3d12_WaterUnder: the light the water scatters from the scene's sun and sky coming down through the "
                     "surface, the water's scattering its attenuation less pure water's absorption (clear water: darker and "
                     "bluer than a floor the sun lights, darker with the depth and looking down), the surface beyond Snell's "
                     "window the light the water below sends up (meeting the water at the horizon); 0: the water's light "
                     "twice its fog volume's colour, as seen from above (the frames before)");
  iConsole->Register("d3d12_WaterUnderLine", &CV_d3d12_WaterUnderLine, 1, 0,
                     "With d3d12_WaterUnder: a camera just over the ocean's level whose near plane dips under it (diving in, "
                     "riding the waves) sees the light in the water where the near plane clips the surface away, from where "
                     "each pixel's ray goes under the level; 0: the floor there unfogged, a line across the view (the frames "
                     "before)");
  iConsole->Register("d3d12_WaterUnderAdapt", &CV_d3d12_WaterUnderAdapt, 1.0f, 0,
                     "With d3d12_WaterUnder and r_HDRRendering: the eye's white balance with the camera under the water, as a "
                     "diver's (or a camera's set at the depth): the light at the camera's depth, the water having taken its red "
                     "first, taken towards the light just under the surface by this much, 0 to 1, its luminance kept (none at the "
                     "surface, more the deeper); 0: none (the frames before)");
  iConsole->Register("d3d12_WaterUnderExposure", &CV_d3d12_WaterUnderExposure, 1.0f, 0,
                     "With d3d12_WaterUnder and r_HDRRendering: the eye's exposure with the camera under the water, this many EV "
                     "more than above it (the water's light dimmer than the day's: the clear water's blue lighter, less dense), "
                     "coming in over the first half metre under the surface; 0: none (the frames before)");
  iConsole->Register("d3d12_GpuTimes", &CV_d3d12_GpuTimes, 0, 0,
                     "N: every N frames, the average GPU time of the frame's sections (shadow maps, water reflection, opaque, "
                     "water, blended, ...) and the time between frames to the console and XRenderD3D12_gpu.txt; 0 off");
  iConsole->Register("d3d12_FrameLog", &CV_d3d12_FrameLog, 0, 0,
                     "1: a line per frame to XRenderD3D12_frames.txt: the time between frames, the CPU's waits and its time in the main "
                     "view, the GPU time of each section, the draws, the triangles and the camera; 2 also the engine's frame profiler (collected "
                     "without its overlay): the self time of each subsystem and of the profilers that took the most; 0 off");
  iConsole->Register("d3d12_BarrierBatch", &CV_d3d12_BarrierBatch, 1, 0,
                     "1: the frame's resource barriers queued and given before the next draw, dispatch or copy in one call (a resource's "
                     "chain of transitions merged, a round trip with nothing between left out, a UAV barrier next to a transition "
                     "left out); 2 the same, the queue given at each GPU section too (d3d12_GpuTimes as with 0); 0 each barrier at "
                     "once (read at the frame's start)");
  iConsole->Register("d3d12_BarrierCheck", &CV_d3d12_BarrierCheck, 0, 0,
                     "1: each transition's before checked against the state the resource's last transition gave, and the queue's "
                     "problems, to the log once each (with d3d12_Debug or d3d12_DRED the resources by name); 0 off");
  iConsole->Register("d3d12_DrawExt", &CV_d3d12_DrawExt, 1, 0,
                     "1: a draw uploads the rest of its constants (b3: lights, shadows, the other programs' parameters) only when "
                     "it reads other bytes than the default block's (DrawExt.h), else binds that block; 0 every draw uploads "
                     "it, as before the default block (read at each draw)");
  iConsole->Register("d3d12_DrawExtReuse", &CV_d3d12_DrawExtReuse, 1, 0,
                     "1: a draw whose rest of its constants (b3) has the same bytes as the last one uploaded in this frame's memory "
                     "binds that copy (the GPU reads the same bytes); 0 every such draw uploads its own (read at each draw)");
  iConsole->Register("d3d12_DrawBatch", &CV_d3d12_DrawBatch, 1, 0,
                     "1: a list's draws of one mesh for consecutive objects, alike but for their matrices, in one instanced draw "
                     "whose vertex shader reads each object's matrices from its constants (the same frames, fewer calls of the "
                     "driver; every such draw takes the instanced pipeline state, one instance or more); 2 held back the same way "
                     "but drawn one by one under the plain state (a check); 0 each draw at once (read at each draw)");
  iConsole->Register("d3d12_FrameMemoryVideo", &CV_d3d12_FrameMemoryVideo, 1, 0,
                     "1: the per frame memory (the draws' constants, the vertices drawn once, the rays' instances) in video memory "
                     "the CPU writes through the bus (the GPU upload heap: resizable BAR) where the device has it, so the GPU reads "
                     "it at its own speed; 0 in system memory (read when a frame slot starts)");
  iConsole->Register("d3d12_CpuSamples", &CV_d3d12_CpuSamples, 0, 0,
                     "N: samples the stack of the thread that draws every N microseconds (a profiler of the wall clock: the engine's "
                     "code, ours, the driver's); back to 0 writes the samples to XRenderD3D12_samples.txt (module and offset)");
  iConsole->Register("r_Stats", &CV_r_Stats, 0, 0,
                     "The renderer's statistics at the top left, averages of half a second: 1 the GPU time of each pass in the order they run "
                     "(as d3d12_GpuTimes), the time between frames, the GPU's and the CPU's waits, the draws and triangles; 2 also the memory");
  iConsole->Register("d3d12_WaterReflectionScale", &CV_d3d12_WaterReflectionScale, 0.5f, VF_DUMPTODISK,
                     "Size of the water reflection (r_WaterReflections), a fraction of the screen's");
  iConsole->Register("d3d12_WaterReflectionBlur", &CV_d3d12_WaterReflectionBlur, 0.1f, 0,
                     "How much the ripple finer than a pixel blurs the ocean's reflection, most of all up and down, far away "
                     "(a factor of the slopes' variance it hides; 0: a mirror, as the original renderer)");
  // Not saved: system.cfg kept the old default (4160), which would hide the new one.
  iConsole->Register("d3d12_WaterReflectionFlags", &CV_d3d12_WaterReflectionFlags,
                     DLD_TERRAIN | DLD_TERRAIN_FULLRES | DLD_STATIC_OBJECTS | DLD_FAR_SPRITES, 0,
                     "What the water reflection shows: DLD_* flags of I3DEngine::DrawLowDetail (4184: the sky, the terrain, the static objects "
                     "and the far sprites; 4160: the sky and the terrain, as the original renderer; + 2048 the entities, which changes "
                     "the animation of what the main view then draws)");
  iConsole->Register("d3d12_SpriteRes", &CV_d3d12_SpriteRes, 128, VF_DUMPTODISK,
                     "Size of the far vegetation sprites' textures, 32 to 256 (made when a level is loaded)");
  iConsole->Register("d3d12_SpriteBlend", &CV_d3d12_SpriteBlend, 0.2f, VF_DUMPTODISK,
                     "The part (0 to 1) of the 15 degrees between two pictures of a far vegetation sprite they are blended "
                     "over, about where the original changes from one to the other, so that the far trees do not change "
                     "their outline at once (0.2: 3 degrees); 0 the original's change at once");
  iConsole->Register("d3d12_SpriteDistFactor", &CV_d3d12_SpriteDistFactor, 1, 0,
                     "1: the far vegetation sprites shrink and end by their distance times the engine's factor (its object "
                     "manager's, 1.32 on River), as the original's: River's trees on the cliff tops 300 to 400 m away are not "
                     "drawn, as in the original; 0 by their distance");
  iConsole->Register("d3d12_Shadows", &CV_d3d12_Shadows, 1, VF_DUMPTODISK, "Shadow maps of the static objects (e_shadow_maps); 0 off");
  iConsole->Register("d3d12_ShadowMapSize", &CV_d3d12_ShadowMapSize, 1024, VF_DUMPTODISK,
                     "Size of the shadow maps, 128 to 4096 (0: the engine's); the maps made from then on");
  iConsole->Register("d3d12_ShadowDebug", &CV_d3d12_ShadowDebug, 0, 0,
                     "1: shows the shadow maps of the frame at the top left (depth in red), the sun's cascades below them (the full set, then the live set); "
                     "2: the receivers show the sun's visibility in grey; 3: the cascade of each point (red, green, blue, yellow), darker in its shadow; "
                     "4: the shadow of all that casts in red, of the live casters in green");
  iConsole->Register("d3d12_PixelLighting", &CV_d3d12_PixelLighting, 1, VF_DUMPTODISK,
                     "1: every lit material is lit per pixel, and the dynamic lights reach the objects with light maps; 0: lit per vertex "
                     "but for the bump templates, the light mapped objects by their light map alone");
  iConsole->Register("d3d12_LightmapDot3", &CV_d3d12_LightmapDot3, 1, VF_DUMPTODISK,
                     "1: a dot3 light map's light as the original's shaders take it: N'.D mixed towards 1 by the light map's alpha; "
                     "0: N'.D alone (darker where the bumps face away from its light)");
  iConsole->Register("d3d12_LightmapSpecular", &CV_d3d12_LightmapSpecular, 1, VF_DUMPTODISK,
                     "1: the specular bump templates over a dot3 light map get the highlights of the lamps baked into it, as the "
                     "original's (per pixel with their normal maps, the texture's alpha the gloss; the diffuse ones none); 0: none");
  iConsole->Register("d3d12_NormalMapHeight", &CV_d3d12_NormalMapHeight, 1, VF_DUMPTODISK,
                     "1: a height map in a material's normal map slot (its displacement map, Research's cave rock) is added to the "
                     "normal map of its bump slot, as the original makes them one texture (alone: made a normal map as a height bump "
                     "map is); 0: it is used as a normal map. For the materials loaded after it is set");
  iConsole->Register("d3d12_MuzzleFlashAuto", &CV_d3d12_MuzzleFlashAuto, 1, VF_DUMPTODISK,
                     "1: the TemplMuzzleFlash_Auto planes (Training's bunker's shafts of sunlight) as the original's shader draws "
                     "them: twice the texture, added, faded out seen edge on by min((2 |N.V|)^4, 1) at each vertex; 0: the texture "
                     "alone, added");
  iConsole->Register("d3d12_LightColorScale", &CV_d3d12_LightColorScale, 1, VF_DUMPTODISK,
                     "1: a bump template's light per pixel as the original's shaders take its colour: the light's colour times the "
                     "material's, scaled down by its largest channel where that is above 1 (the light's hue kept; Training's bunker's "
                     "lamp (1.81 2.22 1.31) on its concrete (0.59 0.65 0.65): 0.74 1 0.59), its term not clamped by itself; 0: each "
                     "channel of the light's colour clamped to 1, then times the material's (0.59 0.65 0.65). The highlight's "
                     "colour: the light's specular colour scaled down by its largest channel above 1, then times the material's; 0: "
                     "clamped");
  iConsole->Register("d3d12_ProjectorSpecular", &CV_d3d12_ProjectorSpecular, 1, VF_DUMPTODISK,
                     "1: a projected light's pass over a light-mapped specular bump template has the light's highlight, as the "
                     "original's light-map shaders of those templates add it (Training's bunker's rusty plates under its lamp); 0: "
                     "the pass's diffuse light alone");
  iConsole->Register("d3d12_LightImages", &CV_d3d12_LightImages, 1, VF_DUMPTODISK,
                     "1: a projected light's image as the engine asks for it: one picture on all six faces of its cube where it "
                     "asks for every side (FT2_REPLICATETOALLSIDES), and the frames <name>01, 02, ... after <name>00 cast in turn, "
                     "one every m_fAnimSpeed seconds, where it asks for the sequence (FT2_CHECKFORALLSEQUENCES): Training's "
                     "bunker's water caustics lamps, which light all round them; 0: the picture on +X alone and its first frame (the "
                     "faces and frames for the images loaded after it is set)");
  iConsole->Register("d3d12_RenderMode", &CV_d3d12_RenderMode, 1, VF_DUMPTODISK,
                     "1: the render mode of the video options (r_RenderMode, with r_Glare on) over the scene, as the original's "
                     "screen process draws it: 1 improved and 2 paradise a glare of the scene's bright parts (less where the "
                     "adapted luminance is high) and a little less saturation, 3 cold more contrast, a glare and less "
                     "saturation, 4 cartoon flatter, warmer colours with dark edges; 0: none");
  iConsole->Register("d3d12_RenderModeOutdoor", &CV_d3d12_RenderModeOutdoor, 1, VF_DUMPTODISK,
                     "1: the render modes' glare goes from its indoor threshold and gain to its outdoor ones as the "
                     "original's (2 % of the way every 1/50 s; indoors: the camera in a vis area the outdoor lights do not "
                     "reach): paradise outdoors fainter in a grey fog, improved and cold fainter too; 0: the indoor ones "
                     "everywhere, as before");
  iConsole->Register("d3d12_RenderModeScene", &CV_d3d12_RenderModeScene, 1, 0,
                     "1: with a render mode after the HDR resolve and no pass between (d3d12_AA taa, none or MSAA), the resolve "
                     "draws the frame into a target of the render mode's, which reads it there and draws all of the back buffer: "
                     "no copy of the back buffer (4K: 0.03 ms) and its passes read a render target (0.015 ms less); the same frames; "
                     "0: into the back buffer and copied");
  iConsole->Register("d3d12_ShadowCascades", &CV_d3d12_ShadowCascades, 4, VF_DUMPTODISK,
                     "With e_shadow_maps, the sun's cascaded shadow maps around the camera: how many, 1 to 4 (0: none, the shadow maps of the objects alone)");
  iConsole->Register("d3d12_ShadowDistance", &CV_d3d12_ShadowDistance, 120.0f, VF_DUMPTODISK,
                     "How far the sun's cascaded shadow maps reach from the camera (m); the shadow maps of the objects beyond");
  iConsole->Register("d3d12_ShadowCascadeSize", &CV_d3d12_ShadowCascadeSize, 2048, VF_DUMPTODISK, "The size of the sun's cascaded shadow maps, 512 to 4096");
  iConsole->Register("d3d12_CascadeInstancing", &CV_d3d12_CascadeInstancing, 3, 0,
                     "1: each caster of the full set of the sun's cascades drawn once into all the slices it reaches (instances); 2: the "
                     "live set's too; 3: also the parts of one mesh and material once for all their objects; 0: once per slice");
  iConsole->Register("d3d12_CascadeCull", &CV_d3d12_CascadeCull, 1, 0,
                     "Without rays (none read the scene of the camera towards the sun): 1 the parts that reach no slice of the sun's "
                     "cascades left out as the engine adds them (not prepared), 2 also that camera ending where nothing "
                     "can shade the cascades' receivers any more (the level's heights); 0: all of them prepared, as far as the view");
  iConsole->Register("d3d12_StaticCasters", &CV_d3d12_StaticCasters, 1, 0,
                     "1: the static objects the 3D engine makes shadow maps for (the vegetation that casts: the palms) shadow the terrain and "
                     "the light maps wherever the sun's cascades and rays reach; 0: only while the engine names them for a receiver of the "
                     "frame (a palm's shadow on the ground there while a character beside it is near the camera, gone farther off)");
  iConsole->Register("d3d12_DepthPrepass", &CV_d3d12_DepthPrepass, 1, 0,
                     "1: the main view's opaque surfaces that discard nothing drawn into the depth buffer before the opaque list, which "
                     "then shades only the pixels it keeps; 0: the list alone");
  iConsole->Register("d3d12_AlphaTestEarly", &CV_d3d12_AlphaTestEarly, 1, 0,
                     "1: an alpha tested draw's texels under its reference with room to spare (a leaf's empty parts) left out right "
                     "after its texture is read, before their light and the sun's shadows; the test at the end stays, the frames the "
                     "same (Swamp's jungle with the path tracer: the opaque list 1.62 -> 1.35 ms); 0: at the end alone");
  iConsole->Register("d3d12_FoliagePrepass", &CV_d3d12_FoliagePrepass, 2, 0,
                     "2: the leaves' and the grass's depth into the depth buffer after the depth prepass, where their alpha test "
                     "passes (a pixel shader of the texture's alpha alone), their colour then only there (the depth test EQUAL, before "
                     "the pixel shader): the opaque list shades only the leaves in front; the same frames; 1: with the path tracer "
                     "alone; 0: none");
  iConsole->Register("d3d12_CascadeCasterShader", &CV_d3d12_CascadeCasterShader, 1, 0,
                     "1: a caster of the sun's cascades that discards (an alpha tested leaf, the live set's casters) drawn by a pixel "
                     "shader of its discards alone, not the general one: the same maps, faster; 0: the general one");
  iConsole->Register("d3d12_ShadowSoftness", &CV_d3d12_ShadowSoftness, 0.05f, VF_DUMPTODISK,
                     "The softness of the sun's cascaded shadows: the radius of their filter (m; at least a texel and a half of each map)");
  iConsole->CreateVariable("d3d12_Upscaler", "none", VF_DUMPTODISK,
                           "The upscaler of the 3D scene: none, or fsr (AMD FSR Upscaling); with d3d12_UpscaleMode 0 anti-aliasing only");
  iConsole->Register("d3d12_UpscaleMode", &CV_d3d12_UpscaleMode, 0, VF_DUMPTODISK,
                     "The upscaler's render size: 0 Native AA (the display's: anti-aliasing only), 1 Quality (1/1.5), 2 Balanced (1/1.7), "
                     "3 Performance (1/2), 4 Ultra Performance (1/3)");
  iConsole->Register("d3d12_UpscaleSharpness", &CV_d3d12_UpscaleSharpness, 0.5f, VF_DUMPTODISK, "The upscaler's sharpening (and TAA's: AMD FidelityFX CAS's sharpness), 0 (none) to 1");
  iConsole->Register("d3d12_CutReset", &CV_d3d12_CutReset, 1, 0,
                     "Where the camera cuts to another shot (in a frame it moves more than 4 m or turns more than 45 degrees: a cut scene's "
                     "cuts), the histories of the screen start again: the upscaler's and TAA's, the rays' denoisers (the sun's shadow, the "
                     "AO and GI), the path tracer's light, the volumetric light, the frame before's picture in the reflections; 0: they "
                     "reproject the shot before (its light for some frames)");
  iConsole->Register("d3d12_BeachShadow", &CV_d3d12_BeachShadow, 1, 0,
                     "1: the foam running up the beach and the wet sand under it in the sun's shadow as the terrain under them (the "
                     "sun's cascades and rays, the terrain's shadow colour): no glowing border round the sand in the shadow of "
                     "Carrier's wreck; 0: lit in the shadow (the frames before)");
  iConsole->Register("d3d12_BeachResearch", &CV_d3d12_BeachResearch, 1, 0,
                     "1: the beach of the shore shader TerrainWaterBeachResearch (Steam, Research, Bunker, Dam, Rebellion, Archive) drawn as "
                     "the original's: the foam running up the sand fainter than TerrainWaterBeach's (textures/wave2 twice, water1's "
                     "detail, the wet sand); 0: not drawn (the frames before)");
  iConsole->Register("d3d12_WaterVolumeColor", &CV_d3d12_WaterVolumeColor, 1, 0,
                     "1: the water of a material with the shader WaterVolume or WaterVolumeBumpReflCM (pools, cave water) shows the "
                     "scene behind it times the material's diffuse colour, as the original's refraction pass; 0: untinted (the frames "
                     "before)");
  iConsole->Register("d3d12_FogVolumeDepth", &CV_d3d12_FogVolumeDepth, 1, 0,
                     "1: a fog volume's fog over a point under its layer, seen from above, at least the fog of the point's depth under "
                     "the layer, which every ray to it crosses (a dense volume, Training's bunker pool's 0.1 m, hides its floor); 0: "
                     "the gate of 5 m below the layer alone (the frames before)");
  iConsole->Register("d3d12_UnderwaterBodies", &CV_d3d12_UnderwaterBodies, 1, 0,
                     "1: with the camera above the ocean, the characters under its level (the fish; a body shorter than 1 m by its middle) drawn after the rest of the "
                     "opaque surfaces without writing the depth: the water over them takes the floor under them, its shore's foam no "
                     "longer lays white foam in the shape of a fish swimming just under the surface (Steam's night sea) and their colour "
                     "fades by the water down to that floor (the original's fog volume pass hides them); not with the path tracer; "
                     "0: drawn as the other surfaces (the frames before)");
  iConsole->Register("d3d12_SpriteSort", &CV_d3d12_SpriteSort, 1, 0,
                     "1: the particles (smoke, fire, sparks, heat haze) drawn back to front by the distance of each one's centre "
                     "from the camera, as the original sorts them: Volcano's smoke over the lava stays the same from frame to frame; "
                     "0: in the order the engine adds them (the frames before: a heat haze drawn after a nearer smoke wiped it out, "
                     "the smoke a step brighter at each burst of new particles, every 5 frames)");
  iConsole->Register("d3d12_DecalAddOpacity", &CV_d3d12_DecalAddOpacity, 1, 0,
                     "1: an added decal (TemplDecalAdd) whose material's opacity is below 1 laid over the frame by its texture's "
                     "alpha times the opacity instead, as the original draws it: Research's cave cobwebs (opacity 0.99) thin "
                     "strands; 0: added (the frames before: a bright card with straight edges over the dark rock)");
  iConsole->Register("d3d12_UpscaleDebug", &CV_d3d12_UpscaleDebug, 0, 0, "1: the upscaler's debug view; 2: the depth it gets (near black, far white)");
  iConsole->Register("d3d12_UpscaleVersion", &CV_d3d12_UpscaleVersion, 0, VF_DUMPTODISK,
                     "The upscaler's major version: 0 its own choice for the GPU, 3 or 4 (AMD FSR 3.1 or FSR 4)");
  iConsole->Register("d3d12_UpscaleHdr", &CV_d3d12_UpscaleHdr, 1, VF_DUMPTODISK,
                     "With r_HDRRendering: 1 the upscaler gets the scene's light (HDR), the bloom and the tone map after it at the display's "
                     "size; 0 the scene tone mapped at the render size (LDR)");
  // The anti-aliasing without an upscaler (PostAA.cpp).
  iConsole->CreateVariable("d3d12_AA", "taa", VF_DUMPTODISK,
                           "The anti-aliasing of the 3D scene without an upscaler (with d3d12_Upscaler fsr FSR anti-aliases; taa by default): none, "
                           "fxaa (FXAA over the finished picture), smaa (SMAA 1x over it), cmaa2 (CMAA2 over it), taa (TAA: the scene "
                           "jittered, its frames gathered) or msaa2, msaa4, msaa8 (MSAA: the scene drawn with 2, 4 or 8 samples a pixel)");
  iConsole->Register("d3d12_AAQuality", &CV_d3d12_AAQuality, 2, VF_DUMPTODISK,
                     "The anti-aliasing's quality, 0 (low) to 3 (ultra): FXAA's edge thresholds and how far it follows an edge, "
                     "SMAA's and CMAA2's presets, how many frames TAA gathers");
  iConsole->Register("d3d12_AADebug", &CV_d3d12_AADebug, 0, 0,
                     "1: the anti-aliasing's view of the edges (grey where it leaves the picture, red by how much it blends; SMAA and "
                     "CMAA2: also the edges they found; TAA: red where its history goes, yellow where it is clipped)");
  iConsole->Register("d3d12_AASceneLight", &CV_d3d12_AASceneLight, 1, 0,
                     "1: with TAA and the HDR scene's light (d3d12_UpscaleHdr) its resolve makes that light itself from the scene "
                     "(the same picture, no full-screen pass of its own); 0: the light's own pass, as the frames before");
  iConsole->Register("d3d12_AlphaToCoverage", &CV_d3d12_AlphaToCoverage, 1, VF_DUMPTODISK,
                     "With MSAA (d3d12_AA msaa2, msaa4, msaa8), 1: the alpha-tested surfaces (grass, leaves, gratings) cover the samples "
                     "their alpha sharpened to the test's edge covers (alpha to coverage): their edges smooth; 0: the alpha test, a step");
  // The RayMaster maps beside the game's textures (Texture.cpp's FindPbrCompanions, PbrCompanion.h): not this renderer's alone.
  iConsole->Register("r_Parallax", &CV_r_Parallax, 1, 0,
                     "1: parallax occlusion mapping: the lit opaque surfaces whose texture has a height in the game's folder RayMaster "
                     "(raymaster/<texture>_pbr_m.dds, its alpha) look hollow and raised by it, their texture, normal map and highlights "
                     "shifted along the view (r_ParallaxDepth; not the silhouettes, the depth, the shadows); 0: none (the frames before)");
  iConsole->Register("r_ParallaxDepth", &CV_r_ParallaxDepth, 0.03f, 0,
                     "r_Parallax's depth from the hollows to the bumps in the texture's coordinates (a fraction of its side; 0.03: 3 cm "
                     "of a texture a metre wide)");
  iConsole->Register("r_ParallaxMaterials", &CV_r_ParallaxMaterials, 1, 0,
                     "1: r_ParallaxDepth times each texture's share by its physical material (RayMaster/pbr_depth.tsv: stone and "
                     "brick 1, sand 0.7, wood 0.6, cloth 0.5, metal 0.25, leaves 0.3, glass and flesh 0: none); 0: the same depth for all");
  iConsole->Register("r_ParallaxFilter", &CV_r_ParallaxFilter, 4, 0,
                     "r_Parallax's march reads the height averaged over this many texels of the height map at least (and over its step "
                     "where that is longer): the blocks and joints in relief, the height's fine noise not cut deep (spikes at a slanting "
                     "view); 0: the height at the pixel (1 to 64)");
  iConsole->Register("r_PbrNormals", &CV_r_PbrNormals, 1, 0,
                     "1: a lit surface whose material has no normal map gets the normals made from its texture in the game's folder "
                     "RayMaster (raymaster/<texture>_pbr_n.dds) with bump mapping on (r_Quality_BumpMapping); 0: none (the frames before)");
  iConsole->Register("r_PbrRoughness", &CV_r_PbrRoughness, 1, 0,
                     "1: the highlights of the specular materials lit per pixel by the roughness of their texture's map in the game's "
                     "folder RayMaster (raymaster/<texture>_pbr_m.dds, its red): a rough stone's broad and faint, a smooth metal's narrow "
                     "and bright, as much light reflected; 0: the same lobe for all (the templates', the frames before)");
  iConsole->Register("r_PbrAO", &CV_r_PbrAO, 1, 0,
                     "1: the reflections by rays of the materials of the RayMaster maps (d3d12_RTPbrReflections) less where their "
                     "texture's ambient occlusion (raymaster/<texture>_pbr_m.dds, its blue) hides them: the pits between a metal's "
                     "treads and the holes of its mesh dark, not the scene's light; 0: none (the frames before)");
  iConsole->Register("r_Skin", &CV_r_Skin, 1, 0,
                     "1: the characters' faces and heads (their textures under objects/characters named face or head) lit as skin, "
                     "with the raster and the path tracer: a broad highlight that reflects little head on and more towards grazing "
                     "views (smoother where the texture's alpha, the gloss, is high), and the light under the skin reaching a little "
                     "past the shadow's line, red the farthest (r_SkinRoughness, r_SkinRoughnessOily, r_SkinScatter); 0: as the "
                     "templates light every material, a glossy plastic's highlight (the frames before)");
  iConsole->Register("r_SkinRoughness", &CV_r_SkinRoughness, 0.55f, 0,
                     "r_Skin: the perceptual roughness of the matte skin (no gloss in the texture's alpha; 0 a mirror to 1)");
  iConsole->Register("r_SkinRoughnessOily", &CV_r_SkinRoughnessOily, 0.35f, 0,
                     "r_Skin: the perceptual roughness of the oily skin (the texture's alpha 0.3: the lips, the nose; smoother on to "
                     "the alpha 1, the eyes' wet rims, 0.25 at the least)");
  iConsole->Register("r_SkinScatter", &CV_r_SkinScatter, 1, 0,
                     "r_Skin: how far the light under the skin reaches past the line where the light passes the face, times red 0.12, "
                     "green 0.04, blue 0.02 of N.L, w: ((N.L + w) / (1 + w))^(1 + w) (0: lambert's, the light ends at that line)");
  // The engine's distances, set for the level it has (DrawDistance.cpp): not this renderer's alone.
  iConsole->Register("r_DrawDistance", &CV_r_DrawDistance, 1, 0,
                     "1: the levels' view distances farther: the terrain drawn at its full detail as far as it is seen "
                     "(e_terrain_lod_ratio 0: the far cliffs and rocks keep their shape) and the vegetation a model up to 3 to 10 times "
                     "farther before it turns into its sprite (e_vegetation_sprites_distance_ratio, each level's own factor), on the "
                     "game's levels (not Control and Cooler); in the game's dev mode (-DEVMODE) also the objects seen farther "
                     "(e_obj_view_dist_ratio 75) and the characters' detailed models (ca_LodBias 0.015). The game's own values are "
                     "given back with 0 (the frames before) and are the ones system.cfg keeps");
  iConsole->Register("r_TexUpscaled", &CV_r_TexUpscaled, 1, 0,
                     "1: the textures of the game's paks take their upscaled pictures where the game's folder RayMaster has them "
                     "(raymaster/<texture>.dds beside the maps: twice the side, made by networks from the game's own, the compression's "
                     "blocks cleaned first): finer detail up close; read as a texture is loaded, so it takes effect from the next "
                     "level's load; 0: the paks' own pictures (the frames before)");
  // The engine sets the command line's variables after its start has loaded the menu's and the shaders' textures; this
  // one decides how they load, so its value there ("r_TexUpscaled 0") is taken now.
  CV_r_TexUpscaled = CommandLine::IntValue(GetCommandLineA(), "r_TexUpscaled", CV_r_TexUpscaled);
  iConsole->Register("r_TexUpscaledLight", &CV_r_TexUpscaledLight, 0, 0,
                     "With r_TexUpscaled, 1: the textures of the templates that add their texture as light (glows, flashes, the shafts "
                     "of sunlight of Training's bunker: TemplMuzzleFlash, TemplDecalAdd, TemplDecalAlphaAdd) take their upscaled "
                     "pictures too (the frames before); 0: their own, soft as they are (the upscaler's detail shows in them as streaks); "
                     "read as a material's textures load, from the next level's load");
  CV_r_TexUpscaledLight = CommandLine::IntValue(GetCommandLineA(), "r_TexUpscaledLight", CV_r_TexUpscaledLight);
  iConsole->Register("r_TexUpscaledSky", &CV_r_TexUpscaledSky, 0, 0,
                     "With r_TexUpscaled, 1: the sky boxes (and their cloud layers) take their upscaled pictures too (the frames "
                     "before); 0: their own, the game's uncompressed ones: the upscaled ones are compressed (BC1), which breaks a "
                     "sky's smooth gradients into steps (Swamp's fog-coloured horizon a rectangle with a straight edge) and its "
                     "clouds into blotches; read as a sky's textures load, from the next level's load");
  iConsole->Register("d3d12_SkyFogBand", &CV_d3d12_SkyFogBand, 1, VF_DUMPTODISK,
                     "1: with the frame's fog ending before 256 m (Swamp, Volcano), the band of the fog's colour over the sky box's "
                     "lower part as the original draws it, opaque up to a little above the horizon (the lower the nearer the fog's "
                     "end and the higher the camera) and fading out over half the box above; the water reflects it; 0: the bare box, "
                     "its edges and its bottom showing near the horizon (the frames before)");
  iConsole->Register("d3d12_FadeBlend", &CV_d3d12_FadeBlend, 1, 0,
                     "1: the objects the engine fades in or out by their distance (their alpha below 1: the crates, the gas "
                     "cylinders and the crane on Carrier's deck) drawn blended by that alpha among the blended surfaces, as the original draws them "
                     "(with the path tracer d3d12_PTFadeBlend); 0: drawn opaque, popping in and out at the far end of their fade (the frames before)");
  iConsole->Register("d3d12_PTFadeBlend", &CV_d3d12_PTFadeBlend, 1, 0,
                     "1: with the path tracer the objects the engine fades in or out by their distance blended as well (d3d12_FadeBlend), "
                     "after the trace with the raster's light, the scene for rays keeping them opaque; 0: kept whole in its G-buffer, "
                     "standing there from the far end of their fade (the frames before)");
  // The ambient occlusion (AmbientOcclusion.cpp).
  iConsole->CreateVariable("d3d12_AO", "cacao", VF_DUMPTODISK,
                           "Ambient occlusion of the 3D scene: none, gtao (Intel XeGTAO), cacao (AMD FidelityFX CACAO), both in screen "
                           "space, or rt (rays among the scene, a device with DXR 1.1)");
  iConsole->Register("d3d12_AOQuality", &CV_d3d12_AOQuality, 2, VF_DUMPTODISK,
                     "The ambient occlusion's quality, 0 (low) to 3 (XeGTAO ultra, CACAO highest; rt: 0 to 2 a ray for each 2 x 2 "
                     "pixels, 3 a ray a pixel)");
  iConsole->Register("d3d12_AORadius", &CV_d3d12_AORadius, 1.0f, VF_DUMPTODISK, "The ambient occlusion's radius (m): how far a surface occludes");
  iConsole->Register("d3d12_AOStrength", &CV_d3d12_AOStrength, 1.0f, VF_DUMPTODISK,
                     "The ambient occlusion's strength: the exponent of the visibility (1 as the backend computes it, 0 none)");
  iConsole->Register("d3d12_AODenoise", &CV_d3d12_AODenoise, 2, VF_DUMPTODISK,
                     "The ambient occlusion's denoising: XeGTAO 0 (none) to 3 passes, CACAO 1 to 8 blur passes, rt 0 (none) or more "
                     "(the frames before and a filter)");
  iConsole->Register("d3d12_AOApply", &CV_d3d12_AOApply, 1, VF_DUMPTODISK,
                     "What the ambient occlusion darkens: 0 the whole picture, 1 the ambient light alone (not the sun's and the lamps' direct light)");
  iConsole->Register("d3d12_AOBakedShare", &CV_d3d12_AOBakedShare, 0.5f, VF_DUMPTODISK,
                     "With d3d12_AOApply 1, the ambient light's share of the light baked into the terrain and the vegetation where the frame "
                     "has no shadows on the terrain to take it from, 0 to 1");
  iConsole->Register("d3d12_AODebug", &CV_d3d12_AODebug, 0, 0,
                     "1: the ambient occlusion's factor in grey over the whole picture; with d3d12_AOApply 1, 2: the ambient light, 3: the light it takes away");
  // The scene for rays (RayTracing.cpp), on a device with DXR 1.1.
  iConsole->Register("d3d12_RTDebug", &CV_d3d12_RTDebug, 0, 0,
                     "The scene for rays seen from the camera, one ray per pixel over the whole picture: 1 its instances in colours "
                     "(of the mesh and where it is to 0.25 m, the same from frame to frame), "
                     "2 the hits shaded on the right half (the picture on the left), 3 the distance to the hit (blue near, red far), "
                     "4 the mip level of the hit's texture (blue 0, red 8), 5 the kind of BLAS (static green, of the frame red, alpha tested "
                     "lighter), 6 the alpha tests along the ray, 7 the hits shaded over the whole picture; the path tracer's: 8 the albedo "
                     "of the hits, 9 their emission, 10 them lit by the sun and the lamps; 11 a colour per material the rays take "
                     "of the hits' instances (the same while it is); 12 the side of the hits' triangles as the raster's culling takes it "
                     "(the front green, the back red); 13 the way to the sun from the hits as the volumetric light's rays take it "
                     "(not the pieces of the sky): white reaching it, hidden the distance to what hides it (blue near, red far); "
                     "14 the air the sun reaches along the rays to the hits (the shafts as they are in the scene for rays, white, "
                     "without the volumetric light's froxels)");
  iConsole->Register("d3d12_RTDebugSky", &CV_d3d12_RTDebugSky, 1, 0,
                     "0: d3d12_RTDebug's rays leave out the pieces of the sky (the planes of a sky box's texture closing a room's openings) "
                     "as the volumetric light's sun rays do; 1 they see them, as the other rays");
  iConsole->Register("d3d12_RTDistance", &CV_d3d12_RTDistance, 150.0f, VF_DUMPTODISK, "The rays' length (m)");
  iConsole->Register("d3d12_RTMemory", &CV_d3d12_RTMemory, 384, VF_DUMPTODISK,
                     "The memory of the static geometry's acceleration structures (MB) beyond which the least recently used go");
  iConsole->Register("d3d12_RTMask", &CV_d3d12_RTMask, 31, 0,
                     "The kinds of geometry the rays see, bits: 1 static, 2 alpha tested, 4 changing every frame; a draw is seen when "
                     "the mask has all of its kinds (5: no alpha tested geometry)");
  iConsole->Register("d3d12_RTReflections", &CV_d3d12_RTReflections, 0, VF_DUMPTODISK,
                     "The reflections by rays (a device with DXR 1.1): the water's, 0 off, 1 the rays in place of the screen space "
                     "reflection, 2 both, the screen space reflection where it finds the scene before it passes behind an object and the "
                     "rays elsewhere; with 1 or 2 also those of the surfaces that reflect a cube map (d3d12_RTReflectionSurfaces)");
  iConsole->Register("d3d12_RTReflectionSurfaces", &CV_d3d12_RTReflectionSurfaces, 1, VF_DUMPTODISK,
                     "With d3d12_RTReflections, the surfaces that reflect a cube map (chrome, glass, screens, env cubes) trace rays "
                     "in its place, the cube where they hit nothing: 0 the cube alone, 1 rays, 2 rays with the hits tinted red and "
                     "the misses blue");
  iConsole->Register("d3d12_RTPbrReflections", &CV_d3d12_RTPbrReflections, 1, 0,
                     "With d3d12_RTReflections and d3d12_RTReflectionSurfaces, the specular materials whose RayMaster maps make them "
                     "a metal or smooth (raymaster/<texture>_pbr_m.dds green, _pbr_r.dds) reflect the scene by rays: one ray a pixel "
                     "by the roughness's lobe, weighted by the share the surface reflects (4 % head on for a dielectric, the "
                     "texture's colour for a metal), none on the rough; 0 off (the frames before)");
  iConsole->Register("d3d12_RTPbrMetalDiffuse", &CV_d3d12_RTPbrMetalDiffuse, 0.5f, 0,
                     "With d3d12_RTPbrReflections, the share of its diffuse light (the game's lighting of its texture) a metal keeps "
                     "beside its reflection: 1 all of it, 0 none (a metal's colour its reflection's alone)");
  iConsole->Register("d3d12_RTPbrScreen", &CV_d3d12_RTPbrScreen, 1, 0,
                     "With d3d12_RTPbrReflections, what their rays hit takes its light from the last frame's picture where that frame "
                     "saw it (its lamps, its fog, its own highlights; a copy of the picture and its depth at the end of a frame with such "
                     "reflections), else the rays' own (the light maps, the sun); 0 the rays' own alone");
  iConsole->Register("d3d12_RTPbrQuad", &CV_d3d12_RTPbrQuad, 0, 0,
                     "With d3d12_RTPbrReflections, one ray for each 2 x 2 pixels, its light shared by those of them that reflect "
                     "about the same way (each weighting it by its own share), the pixel that traces it another each frame: faster, "
                     "but the temporal anti-aliasing leaves a coarse pattern and sparkles on fine relief (the rays' noise the same "
                     "in a quad's pixels); 0 (the default) a ray a pixel");
  iConsole->Register("d3d12_RTSunShadows", &CV_d3d12_RTSunShadows, 0, VF_DUMPTODISK,
                     "The sun's shadow by rays (a device with DXR 1.1, d3d12_DepthPrepass and d3d12_ShadowCascades), towards the "
                     "sun's disc from the surfaces of the depth prepass up to the end of the cascades, in place of the cascades' "
                     "shadow (the other surfaces, alpha tested, leaves, blended, keep the cascades): 1 a ray for each 2 x 2 pixels, "
                     "from each of them in turn; 2 a ray a pixel");
  iConsole->Register("d3d12_RTLightShadows", &CV_d3d12_RTLightShadows, 0, VF_DUMPTODISK,
                     "The lamps' shadows by rays (a device with DXR 1.1): the main view's surfaces lit per pixel by point lights "
                     "(the draws' lights, the light passes, the projectors such as the flashlight) trace a ray towards each and "
                     "leave out the light of those it does not reach; 0 off, 1 on, 2 the light taken away red, 3 the light taken away "
                     "by where the ray hits: green next to the surface, red next to the lamp");
  iConsole->Register("d3d12_RTLightShadowsMax", &CV_d3d12_RTLightShadowsMax, 4, VF_DUMPTODISK,
                     "With d3d12_RTLightShadows, the lamps a pixel traces rays towards (1-15); those past them light it unshadowed");
  iConsole->Register("d3d12_RTLightBehind", &CV_d3d12_RTLightBehind, 1, VF_DUMPTODISK,
                     "With d3d12_RTLightShadows, the scene for rays has what is behind, above and below the camera (d3d12_PTBehind "
                     "m, as for the path tracer): what is out of the view shades a lamp too, not only once it comes into the view; "
                     "0 only the view's and the sun's parts");
  iConsole->Register("d3d12_RTLightShadowGap", &CV_d3d12_RTLightShadowGap, 0.1f, VF_DUMPTODISK,
                     "With d3d12_RTLightShadows, how far short of a lamp its rays end (m, 0-2.5): what is nearer to it, its own "
                     "fitting, casts no shadow");
  iConsole->Register("d3d12_RTLampSize", &CV_d3d12_RTLampSize, 0.05f, VF_DUMPTODISK,
                     "With d3d12_RTLightShadows, a lamp's radius as a share of its light's (0-0.5, as d3d12_PTLampSize for the "
                     "path tracer): each pixel's ray goes towards a point of that disc, another from frame to frame where a temporal "
                     "anti-aliasing or upscaler gathers the frames, so the shadow's edge is soft and a caster that hides part of a "
                     "big lamp takes part of its light; 0 a point, hard shadows");
  iConsole->Register("d3d12_RTLampRays", &CV_d3d12_RTLampRays, 4, VF_DUMPTODISK,
                     "With d3d12_RTLightShadows and d3d12_RTLampSize, the rays a pixel traces towards points of a lamp's disc, spread "
                     "over it, the share that reaches the lamp its light (1, 2, 4 or 8: fewer, a noisier edge where the frames are "
                     "not gathered, after a camera's cut)");
  iConsole->Register("d3d12_RTProjectorShadowGap", &CV_d3d12_RTProjectorShadowGap, 0.5f, VF_DUMPTODISK,
                     "How far short of a projected light (a lamp with a picture) the rays towards it end, of the lamps' shadows "
                     "(d3d12_RTLightShadows) and of the path tracer (m, at least d3d12_RTLightShadowGap): its picture draws the "
                     "shade of its fitting, which casts no shadow of its own within it; 0: as the other lamps");
  iConsole->Register("d3d12_RTFaceNormal", &CV_d3d12_RTFaceNormal, 1, 0,
                     "The side of their surface the main view's rays start from (the lamps' shadows, d3d12_RTLightShadows, and the "
                     "reflections, d3d12_RTReflectionSurfaces): 1 by the face of the triangle from the plane 1 / w makes over the "
                     "screen, precise where the camera is close, and towards a lamp the face is turned from where the vertices' "
                     "normal is not (by the line where the light passes a coarse mesh: a head's) along that normal; 2 that face "
                     "alone; 0 by the world position's differences from pixel to pixel (the frames before: they round in close "
                     "ups, and a lamp that grazes the surface stipples it with its shadow)");
  iConsole->Register("d3d12_RTSunAngle", &CV_d3d12_RTSunAngle, 0.53f, VF_DUMPTODISK,
                     "With d3d12_RTSunShadows, the sun's disc in degrees across (0.53 as the sun's; 0: hard shadows): the soft edges "
                     "of the shadow widen with the distance to what casts it");
  iConsole->Register("d3d12_RTSunDenoise", &CV_d3d12_RTSunDenoise, 1, 0,
                     "With d3d12_RTSunShadows, 1: the rays of the frames before blended in where the surface was and the soft edges "
                     "filtered; 2: a ray a pixel, denoised by AMD FidelityFX Denoiser's shadows denoiser (where it is built in, else 1); "
                     "0: this frame's ray as it is");
  iConsole->Register("d3d12_RTSunCrease", &CV_d3d12_RTSunCrease, 1, 0,
                     "With d3d12_RTSunShadows or d3d12_PathTracing, the sun's rays from a pixel where a floor meets a wall start off "
                     "its own face (not a cross of the two) and meet what is nearer than 1 cm: the seams of River's temple's floor "
                     "with its walls no longer a bright line in the path tracer's sun (0: as before)");
  iConsole->Register("d3d12_RTGI", &CV_d3d12_RTGI, 0, VF_DUMPTODISK,
                     "The ambient light by rays, one bounce (a device with DXR 1.1, d3d12_AOApply 1; in place of d3d12_AO): what the "
                     "rays from a surface find, lit by the sun and the light maps, darkens or lights its ambient light, what they miss "
                     "leaves it as it is: 1 a ray for each 2 x 2 pixels, from each of them in turn; 2 a ray a pixel");
  iConsole->Register("d3d12_RTGIDistance", &CV_d3d12_RTGIDistance, 4.0f, VF_DUMPTODISK,
                     "With d3d12_RTGI, how far its rays look (m); what they find beyond the first 0.385 of it counts less and less");
  iConsole->Register("d3d12_RTGIBounce", &CV_d3d12_RTGIBounce, 1.0f, VF_DUMPTODISK,
                     "With d3d12_RTGI, how much light what the rays find gives back (1 as it is lit, 0 none: it only occludes)");
  iConsole->Register("d3d12_RTShadows", &CV_d3d12_RTShadows, 1, VF_DUMPTODISK,
                     "1: the sun's shadow at the hits of the rays, by a ray towards the sun (the surfaces the sun's shadow falls on)");
  iConsole->Register("d3d12_RTAsync", &CV_d3d12_RTAsync, 1, 0,
                     "1: the acceleration structures for the rays built on the compute queue, beside the frame's shadows, opaque "
                     "geometry and ambient occlusion; 0: in the frame's command list, before the opaque geometry");
  iConsole->Register("d3d12_RTAlphaCoverage", &CV_d3d12_RTAlphaCoverage, 3, 0,
                     "The rays whose light is averaged take an alpha tested triangle (a leaf) at random by the share of it its alpha "
                     "test passes on, made once a mesh, in place of the test (faster in the foliage). Bits: 1 the path tracer's "
                     "bounces and radiance cache, 2 the rays' GI and AO, 4 the volumetric light's sun (blurs the shafts); 0 the "
                     "exact tests everywhere");
  iConsole->Register("d3d12_RTOffscreen", &CV_d3d12_RTOffscreen, 1, VF_DUMPTODISK,
                     "What is out of the main view in the scene for rays: the parts of the sun's cascades (the static objects "
                     "and entities towards the sun, the live casters). 1 for the effects that trace rays from the main view's "
                     "surfaces (d3d12_RTDebug; not the water's reflection), 2 always, 0 never (the main view's and the water "
                     "reflection's draws alone)");
  // The path tracer (PathTrace.cpp), on a device with DXR 1.1.
  iConsole->Register("d3d12_PathTracing", &CV_d3d12_PathTracing, 0, VF_DUMPTODISK,
                     "The main view's lighting by paths (a device with DXR 1.1, r_HDRRendering): the sun, the lamps, the sky and "
                     "what glows, with their bounces, in place of the light maps and the ambient light; 0 off, 1 in real time, "
                     "2 the reference (the paths of the frames added up while the view stays)");
  iConsole->Register("d3d12_PTFillLights", &CV_d3d12_PTFillLights, 0, VF_DUMPTODISK,
                     "With d3d12_PathTracing, the characters' fill lights (DLF_AMBIENT_LIGHT): 0 in their ambient light, which the "
                     "paths fill in with (unshadowed, as the raster's); 1 lamps that light the scene with a ray");
  iConsole->Register("d3d12_PTLightDistance", &CV_d3d12_PTLightDistance, 60.0f, VF_DUMPTODISK,
                     "With d3d12_PathTracing, its lights (the frame's and the level's static ones) within this distance of the camera "
                     "(to their sphere, m), the nearest 128 at most");
  iConsole->Register("d3d12_PTSun", &CV_d3d12_PTSun, 1.0f, VF_DUMPTODISK,
                     "With d3d12_PathTracing, the sun's light: times twice its colour (as light) on a face towards it");
  iConsole->Register("d3d12_PTLamps", &CV_d3d12_PTLamps, 1.0f, VF_DUMPTODISK, "With d3d12_PathTracing, the lamps' light: times twice their colour");
  iConsole->Register("d3d12_PTBakedFalloff", &CV_d3d12_PTBakedFalloff, 0.0f, VF_DUMPTODISK,
                     "With d3d12_PathTracing, the falloff of the lamps baked into the light maps: 0 the raster's dynamic light's "
                     "(1 - smoothstep(0, 0.9, d / r)), > 0 the light maps' (1 - d / r) to this power over the whole radius");
  iConsole->Register("d3d12_PTLampSize", &CV_d3d12_PTLampSize, 0.05f, VF_DUMPTODISK,
                     "With d3d12_PathTracing, a lamp's radius as a share of its light's radius (at most 0.5): the rays of its "
                     "shadow go towards points of a disc of that size, a soft shadow's edge; 0: a point, hard shadows (the frames "
                     "before)");
  iConsole->Register("d3d12_PTDecalLight", &CV_d3d12_PTDecalLight, 1, VF_DUMPTODISK,
                     "With d3d12_PathTracing, a draw lit by its light map after the paths (a decal blended over the hull it lies "
                     "on: Carrier's ship's name) takes the light the paths give the surface under it in the raster's ratio of "
                     "their lights, not the raster's light alone (dark over a hull the paths lit brighter); 0: the raster's light "
                     "(the frames before)");
  iConsole->Register("d3d12_PTVegetationGain", &CV_d3d12_PTVegetationGain, 1, 0,
                     "With d3d12_PathTracing (which bakes the vegetation's colours without light and lights them itself), the "
                     "vegetation's draws the paths do not light (the plants blended after the water, the water's reflection, the "
                     "cubes) take the light its colours had lit by the sun and the ambient, on the mean over each mesh's vertices; "
                     "0: unlit (the frames before)");
  iConsole->Register("d3d12_PTFaceNormal", &CV_d3d12_PTFaceNormal, 1, 0,
                     "With d3d12_PathTracing, the side of its triangle the camera sees, which its G-buffer turns the normals to and "
                     "the vegetation's opaque parts (a trunk) take for their normal, from the plane 1 / w makes over the screen, "
                     "normalised; 0: from the world position's differences from pixel to pixel, as long as they are (the frames "
                     "before: close to a trunk at 4K a normal so short the G-buffer took it for none, straight up, a stipple "
                     "on the trunk in the sun)");
  iConsole->Register("d3d12_PTLeafLight", &CV_d3d12_PTLeafLight, 1, VF_DUMPTODISK,
                     "With d3d12_PathTracing, a leaf (the vegetation's alpha tested parts) is lit no darker than the raster lights "
                     "it: its colours' bake, the ambient twice and the sun times |N.L| on both sides (the ambient and the sun twice "
                     "times N.L on one), with the sun's visibility the raster found; 0: by the sun and its paths alone (the frames "
                     "before: under a crown that hides the sky the foliage near black, a third of the raster's)");
  iConsole->Register("d3d12_PTShadeFloor", &CV_d3d12_PTShadeFloor, 1, VF_DUMPTODISK,
                     "With d3d12_PathTracing, where the raster finds the sun hidden (all of a frame without the sun) no darker "
                     "than the raster's shade: the terrain its texture's baked light (the shadows of what does not move are in "
                     "it), times the shadow's tint where a live caster hides the sun; a lit surface its light map's light and "
                     "its ambient; 0: the paths' light alone (the frames before: the sand under Carrier's wreck at half the "
                     "raster's colour, the wall of Training's bunker by the pipe's mouth at a sixth)");
  iConsole->Register("d3d12_PTSkyPieces", &CV_d3d12_PTSkyPieces, 1, 0,
                     "With d3d12_PathTracing, its rays pass the pieces of the sky (the surfaces of a sky box's texture: the planes "
                     "that close Training's bunker's windows): a path that reaches one has left the scene (the sky's light, "
                     "indoors the game's ambient light), a lamp's ray goes through; 0: they are surfaces that glow with their "
                     "picture's colour (the frames before: its bounces lit the boards and the concrete round the windows white "
                     "and blue, as if the roof were open)");
  iConsole->Register("d3d12_PTWaterPlants", &CV_d3d12_PTWaterPlants, 1, 0,
                     "With d3d12_PathTracing, the plants in the ocean (their bottom under its level) that the engine fades in or "
                     "out drawn blended after the water, as without the path tracer; 0: alpha tested among the opaque surfaces "
                     "with the plants on land (the frames before: their leaves under the surface made the water's foam, white "
                     "leaves round the plants)");
  iConsole->Register("d3d12_PTLightMaps", &CV_d3d12_PTLightMaps, 1.0f, VF_DUMPTODISK,
                     "With d3d12_PathTracing, where the frame has no sun (indoors), the light a surface's light map gives beyond the "
                     "path tracer's own light of its lamps (unshadowed) and the ambient light, times this: the light maps' flat "
                     "light in the halls they light, the path tracer's light and shadows by the lamps (0: its lamps alone)");
  iConsole->Register("d3d12_PTEmission", &CV_d3d12_PTEmission, 1.0f, VF_DUMPTODISK, "With d3d12_PathTracing, the light of what glows");
  iConsole->Register("d3d12_PTTerrainDelight", &CV_d3d12_PTTerrainDelight, 2.0f, VF_DUMPTODISK,
                     "With d3d12_PathTracing, the light the editor baked into the terrain's texture, taken off for its albedo: how "
                     "much brighter a face towards the sun is than one in the terrain's shadow (1 or less: none taken off)");
  iConsole->Register("d3d12_PTDebug", &CV_d3d12_PTDebug, 0, 0,
                     "With d3d12_PathTracing, its G-buffer over the picture: 1 the albedo, 2 the normal, 3 the fog's share, "
                     "4 the material (lit grey, the terrain green, the vegetation yellow; none black); 5 the light of its paths "
                     "(on grey), 6 one frame's paths, 7 the sun's visibility, 8 the game's ambient light the paths fill in with, "
                     "9 the highlights' colour (the material's specular colour times its gloss), 10 the highlights, 11 the "
                     "radiance cache's light at the surfaces (d3d12_PTCache; its cells), 12 the terrain's share of the light baked "
                     "into its texture (d3d12_PTTerrainDelight)");
  iConsole->Register("d3d12_PTSky", &CV_d3d12_PTSky, 1.0f, VF_DUMPTODISK,
                     "With d3d12_PathTracing, the sky's light where a path leaves the scene: times the level's outdoor ambient");
  iConsole->Register("d3d12_PTBounces", &CV_d3d12_PTBounces, 1, VF_DUMPTODISK,
                     "With d3d12_PathTracing 1, the bounces of its paths (0: the lamps and the sun on the surfaces alone)");
  iConsole->Register("d3d12_PTCache", &CV_d3d12_PTCache, 1, VF_DUMPTODISK,
                     "With d3d12_PathTracing 1, its radiance cache: a grid over the world of all the light that comes to a point, "
                     "kept up to date by paths from a pixel of 8 x 8, which the paths' last bounce takes for the light at its hit "
                     "(the sun's, a lamp's and the bounces after it, no rays towards them; 0: none)");
  iConsole->Register("d3d12_PTCheckerboard", &CV_d3d12_PTCheckerboard, 1, VF_DUMPTODISK,
                     "With d3d12_PathTracing 1, the paths of half the pixels each frame, in a checkerboard, the others' light "
                     "from their four neighbours (0: a path from each pixel)");
  iConsole->Register("d3d12_PTSunHistory", &CV_d3d12_PTSunHistory, 64, VF_DUMPTODISK,
                     "With d3d12_PathTracing, the frames the sun's shadow by rays (its mask, d3d12_RTSunDenoise 1) holds at "
                     "most (24 without it); where the shadow moves (crowns in the wind) the history keeps a quarter of them "
                     "and may stay half a step off what the rays round the pixel show: the light through moving leaves "
                     "smooth, the shadows of what moves lagging behind (0: as without the path tracer)");
  iConsole->Register("d3d12_PTBehind", &CV_d3d12_PTBehind, 40.0f, VF_DUMPTODISK,
                     "With d3d12_PathTracing, how far (m, at most d3d12_RTDistance) what is behind the camera is in its scene "
                     "for rays: the static objects and entities the 3D engine gives cameras at the view looking back, up and "
                     "down (0: only what the view and the sun's cascades have)");
  iConsole->Register("d3d12_PTReferenceBounces", &CV_d3d12_PTReferenceBounces, 8, VF_DUMPTODISK,
                     "With d3d12_PathTracing 2 (the reference), the bounces of its paths at most");
  iConsole->Register("d3d12_PTAmbient", &CV_d3d12_PTAmbient, 1.0f, VF_DUMPTODISK,
                     "With d3d12_PathTracing, the game's ambient light (what the raster adds to its light maps and lights) its "
                     "paths fill in with: a ray that finds nothing within d3d12_PTAmbientRange brings it, times this (0: the "
                     "light of the lamps, the sun and the sky alone; rooms are dark)");
  iConsole->Register("d3d12_PTAmbientRange", &CV_d3d12_PTAmbientRange, 2.0f, VF_DUMPTODISK,
                     "With d3d12_PathTracing, how far a ray finds nothing before it brings the game's ambient light (m): its "
                     "occlusion's reach");
  iConsole->Register("d3d12_PTSpecular", &CV_d3d12_PTSpecular, 1.0f, VF_DUMPTODISK,
                     "With d3d12_PathTracing, the highlights of the sun and the lamps on the materials with a specular colour, as "
                     "the raster's, the lamps' where a ray reaches them: times this (0: none)");
  m_pPTDenoiserVar = iConsole->CreateVariable("d3d12_PTDenoiser", CD3D12Nrd::IsBuilt() ? "nrd" : "simple", VF_DUMPTODISK,
                     "With d3d12_PathTracing 1, the denoiser of its light: nrd (NVIDIA NRD's REBLUR, when it is built in), simple "
                     "(its own history and filter) or none (the history alone)");
  // The HDR pipeline (r_HDRRendering, Hdr.cpp).
  iConsole->Register("d3d12_ToneMap", &CV_d3d12_ToneMap, 4, VF_DUMPTODISK,
                     "With r_HDRRendering, the tone map: 0 clamp, 1 soft shoulder (the game's look up to d3d12_ToneMapKnee, then each channel rolls off to 1), "
                     "2 ACES fitted, 3 AgX, 4 neutral (the game's look up to d3d12_ToneMapKnee, then the brightest channel rolls off to 1 and the colour "
                     "with it as a whole: the hue stays, a blue sky blue; the light far above white goes over to the soft shoulder of each channel "
                     "by d3d12_ToneMapDesaturation: the sun white, fire yellow)");
  iConsole->Register("d3d12_ToneMapKnee", &CV_d3d12_ToneMapKnee, 0.85f, VF_DUMPTODISK,
                     "Where the soft shoulder of d3d12_ToneMap 1 and the neutral one of 4 start: a display value, 0 to 0.99");
  iConsole->Register("d3d12_ToneMapDesaturation", &CV_d3d12_ToneMapDesaturation, 3.0f, VF_DUMPTODISK,
                     "With d3d12_ToneMap 4, how fast the light far above white goes over from the hue kept to the soft shoulder of each "
                     "channel (as the game clips it: the sun white, fire yellow), with the square of how far its brightest channel was "
                     "rolled off (0: never; 3: a sky just above the knee keeps its hue)");
  iConsole->Register("d3d12_ToneMapToe", &CV_d3d12_ToneMapToe, 0.02f, VF_DUMPTODISK,
                     "With d3d12_ToneMap 2, the ACES fit's soft toe: from 0 to this light (0.005 to 0.1) linear from black, smoothly into "
                     "the curve, not black below 0.00325 as the fit (its numerator crosses 0 there: a night scene 3 to 5 times darker than "
                     "the original); 0: the fit as it is (the frames before)");
  iConsole->Register("d3d12_Exposure", &CV_d3d12_Exposure, 0.0f, VF_DUMPTODISK, "With r_HDRRendering, the exposure's compensation in EV (0: none)");
  iConsole->Register("d3d12_AutoExposure", &CV_d3d12_AutoExposure, 1, VF_DUMPTODISK,
                     "With r_HDRRendering, the exposure adapts to the scene (between d3d12_ExposureMin and Max); 0: d3d12_Exposure alone");
  iConsole->Register("d3d12_ExposureMin", &CV_d3d12_ExposureMin, 0.0f, VF_DUMPTODISK,
                     "The least EV of the automatic exposure (0, the default: the game's daylight; below it bright views get darker)");
  iConsole->Register("d3d12_ExposureMax", &CV_d3d12_ExposureMax, 1.5f, VF_DUMPTODISK, "The most EV of the automatic exposure (dark scenes made brighter)");
  iConsole->Register("d3d12_ExposureSpeed", &CV_d3d12_ExposureSpeed, 1.5f, VF_DUMPTODISK,
                     "The automatic exposure's rate of adaptation per second (twice that towards a brighter scene)");
  iConsole->Register("d3d12_ExposureMeter", &CV_d3d12_ExposureMeter, 1, VF_DUMPTODISK,
                     "The automatic exposure's meter: 0 the mean log2 luminance of the scene (centre weighted); 1 by a histogram: the "
                     "mean between d3d12_ExposureLow and High, lowered where the sky is in the frame so that its bright part "
                     "(d3d12_ExposureHighlights) stays below the tone map's knee");
  iConsole->Register("d3d12_ExposureLow", &CV_d3d12_ExposureLow, 0.05f, VF_DUMPTODISK,
                     "d3d12_ExposureMeter 1: the share of the scene (by its light, centre weighted) below which the meter's mean leaves "
                     "it out (the dark corners), 0 to 1");
  iConsole->Register("d3d12_ExposureHigh", &CV_d3d12_ExposureHigh, 0.95f, VF_DUMPTODISK,
                     "d3d12_ExposureMeter 1: the share above which it leaves it out (single bright spots), 0 to 1");
  iConsole->Register("d3d12_ExposureHighlights", &CV_d3d12_ExposureHighlights, 0.9f, VF_DUMPTODISK,
                     "d3d12_ExposureMeter 1: the percentile of the sky's brightest channel that the exposure keeps at the tone map's "
                     "knee, the more the bigger the sky's share of the frame (0: none; a little sky through a window may go white)");
  iConsole->Register("d3d12_Bloom", &CV_d3d12_Bloom, 0.5f, VF_DUMPTODISK, "With r_HDRRendering, the strength of the bloom (0: none)");
  iConsole->Register("d3d12_BloomThreshold", &CV_d3d12_BloomThreshold, 1.0f, VF_DUMPTODISK,
                     "The light above which the bloom starts (exposed, linear: 1 is the display's white)");
  iConsole->Register("d3d12_HDRDebug", &CV_d3d12_HDRDebug, 0, 0, "With r_HDRRendering: 1 the luminance in EV bands from middle grey (green), 2 the bloom alone, 3 the sky mask (white where nothing but the sky is drawn)");
  // Brighter than the game draws them in HDR (Hdr.cpp): factors of the light, 1 the game's.
  iConsole->Register("d3d12_HDRSun", &CV_d3d12_HDRSun, 8.0f, VF_DUMPTODISK, "With r_HDRRendering, the light of the sun's flare in its core times this (1: the game's)");
  iConsole->Register("d3d12_HDRSky", &CV_d3d12_HDRSky, 2.0f, VF_DUMPTODISK,
                     "With r_HDRRendering, the light of the sky towards the sun times up to this (1: the game's)");
  iConsole->Register("d3d12_HDRSkyAngle", &CV_d3d12_HDRSkyAngle, 4.0f, VF_DUMPTODISK,
                     "With d3d12_HDRSky, the angle from the sun in degrees where the sky's glow is half way (0: the wide glow up to step 257, half "
                     "way at 11.9 degrees)");
  iConsole->Register("d3d12_HDRGlow", &CV_d3d12_HDRGlow, 3.0f, VF_DUMPTODISK,
                     "With r_HDRRendering, the light of what glows (in their cores: fire and the other additive particles, muzzle flashes and the other additive "
                     "materials, the coronas and flares of the lights, the glow maps of lamps and screens) times this (1: the game's)");
  iConsole->Register("d3d12_HDRGlowHue", &CV_d3d12_HDRGlowHue, 1.0f, VF_DUMPTODISK,
                     "With r_HDRRendering, a lamp's glow map and the coronas and procedural flares of the lights brighter only as a whole colour: "
                     "the brightest channel of what each adds at most this (1: the game's white; at least 0.25), the others scaled with it, so "
                     "that the tone map keeps its hue (a cyan tube and its corona not a white disc); 0: each channel boosted alone, the frames "
                     "before");
  iConsole->Register("d3d12_HDRSkyPieces", &CV_d3d12_HDRSkyPieces, 1, 0,
                     "With r_HDRRendering, a piece of the sky with a glow map (a texture of the sky boxes: the opening of Training's cave's shaft, "
                     "the bunker's windows) is the sky, not a lamp: its glow not made brighter (d3d12_HDRGlow), its light at most the game's white, "
                     "as the original clips it (0: as a lamp's glow, the frames before)");
  // The HDR output to the display (HdrOutput.cpp).
  iConsole->Register("d3d12_HDROutput", &CV_d3d12_HDROutput, 0, VF_DUMPTODISK,
                     "With r_HDRRendering on a display in HDR mode (Windows: Use HDR), the output: 0 SDR, 1 HDR10, 2 scRGB. The scene's highlights "
                     "go up to the display's peak, the rest of the picture, the HUD and the menu stay at paper white as SDR content");
  iConsole->Register("d3d12_HDRPaperWhite", &CV_d3d12_HDRPaperWhite, 0.0f, VF_DUMPTODISK,
                     "With d3d12_HDROutput, the light of the frame's white in nits (0: Windows' SDR content brightness of the display)");
  iConsole->Register("d3d12_HDRPeak", &CV_d3d12_HDRPeak, 0.0f, VF_DUMPTODISK,
                     "With d3d12_HDROutput, the most light in nits, where the highlights end (0: the display's peak)");
  iConsole->Register("d3d12_HDRHighlights", &CV_d3d12_HDRHighlights, 2.0f, VF_DUMPTODISK,
                     "With d3d12_HDROutput and d3d12_ToneMap 4, the exposed light in the game's whites from which the scene goes on to the peak: "
                     "up to its white the SDR picture (at paper white at most: the sky, the clouds, what the sun lights), from this light up the "
                     "highlights (the sun, fire, lamps), smoothly between; at most 1: the tone map to the peak from its knee, as up to step 257");
  iConsole->Register("d3d12_DisplayHDR", &CV_d3d12_DisplayHDR, 0, 0,
                     "With r_HDRRendering, a panel of the HDR pipeline at the right of the screen (below r_DisplayInfo's): the histogram of the "
                     "scene's light with the exposure's marks, the exposure and its target, the tone map, how much of the frame is in its shoulder "
                     "and above white, the output and the display's levels, the output's measured nits (of D3D12_FRAMES frames before)");
  iConsole->Register("d3d12_HDRLog", &CV_d3d12_HDRLog, 0, 0,
                     "With r_HDRRendering, a line of the HDR statistics per frame to XRenderD3D12_hdr.txt: the EV and its target, the meter, "
                     "percentiles of the scene's light, the shares in the shoulder and above white, the output's nits");
  iConsole->Register("d3d12_HDRFrameAverage", &CV_d3d12_HDRFrameAverage, 1.0f, VF_DUMPTODISK,
                     "With d3d12_HDROutput, the most average light of a frame (of each pixel's brightest channel, as HDR10's MaxFALL), times the "
                     "display's full frame light (d3d12_HDRFullFrame): above it the highlights (the sun, fire, lamps) come down towards the SDR "
                     "picture, quickly, and back slowly, so that the display (an OLED) does not dim the whole picture itself; 0: no limit");
  iConsole->Register("d3d12_HDRFullFrame", &CV_d3d12_HDRFullFrame, 0.0f, VF_DUMPTODISK,
                     "With d3d12_HDROutput, the light in nits the display holds over the whole screen, for d3d12_HDRFrameAverage (0: the "
                     "display's, none when it reports none)");
  iConsole->Register("d3d12_HDROutputDebug", &CV_d3d12_HDROutputDebug, 0, 0,
                     "With d3d12_HDROutput: 1 the highlights (yellow to red at the peak; blue: drawn over the scene), 2 patches at paper white, "
                     "twice and four times that and at the peak, 3 the test of the menu's HDR page in d3d12_HDROutputTestRect: a square at 10000 "
                     "nits around a smaller one at the peak (it disappears once the peak is the display's), patches at paper white, twice and "
                     "four times that and at the peak");
  m_HdrOut.pTestRect = iConsole->CreateVariable("d3d12_HDROutputTestRect", HDR_TEST_RECT, 0,
                                                "With d3d12_HDROutputDebug 3, where its test is: left, top, right and bottom in the screen's 0..1 "
                                                "(the free area of the menu's HDR page)");
  // What the HDR output is and the window's display, for the menu's HDR page (read only, not saved; HdrOutput.cpp).
  m_HdrOut.pState = iConsole->CreateVariable("d3d12_HDROutputState", "SDR", VF_READONLY,
                                             "The HDR output now (read only): SDR, HDR10 or scRGB; with d3d12_HDROutput, \"SDR: \" and why");
  m_HdrOut.pDisplay[0] = iConsole->CreateVariable("d3d12_HDRDisplayOn", "-1", VF_READONLY,
                                                  "The window's display (read only): 1 in HDR mode (Windows: Use HDR), 0 in SDR, -1 not found");
  m_HdrOut.pDisplay[1] = iConsole->CreateVariable("d3d12_HDRDisplayPeak", "0", VF_READONLY,
                                                  "The window's display (read only): its peak in nits, as it reports it (0: none)");
  m_HdrOut.pDisplay[2] = iConsole->CreateVariable("d3d12_HDRDisplayFullFrame", "0", VF_READONLY,
                                                  "The window's display (read only): the light in nits it holds over the whole screen, as it "
                                                  "reports it (0: none)");
  m_HdrOut.pDisplay[3] = iConsole->CreateVariable("d3d12_HDRDisplayWhite", "0", VF_READONLY,
                                                  "The window's display (read only): Windows' SDR content brightness in nits (0: none)");
  // The colour grading of the HDR pipeline's resolve (ColorGrading.cpp).
  iConsole->Register("d3d12_Saturation", &CV_d3d12_Saturation, 1.0f, VF_DUMPTODISK, "With r_HDRRendering, the saturation, 0 (grey) to 4 (1: none)");
  iConsole->Register("d3d12_Contrast", &CV_d3d12_Contrast, 1.0f, VF_DUMPTODISK, "With r_HDRRendering, the contrast around middle grey, 0.25 to 4 (1: none)");
  iConsole->Register("d3d12_Temperature", &CV_d3d12_Temperature, 0.0f, VF_DUMPTODISK,
                     "With r_HDRRendering, the white balance: -1 (cooler) to 1 (warmer), up to 100 mired from 6504 K (0: none)");
  iConsole->Register("d3d12_Tint", &CV_d3d12_Tint, 0.0f, VF_DUMPTODISK, "With r_HDRRendering, the tint: -1 (green) to 1 (magenta) (0: none)");
  iConsole->CreateVariable("d3d12_ColorLut", "", VF_DUMPTODISK,
                           "With r_HDRRendering, a 3D LUT of display colours (a .cube file, as DaVinci Resolve or Photoshop write it; a path in the "
                           "game's folder or its paks) the picture goes through; empty: none");
  iConsole->Register("d3d12_ColorLutAmount", &CV_d3d12_ColorLutAmount, 1.0f, VF_DUMPTODISK, "With r_HDRRendering, how much of d3d12_ColorLut, 0 to 1");
  // The volumetric light of the HDR pipeline's resolve (Volumetric.cpp).
  iConsole->Register("d3d12_VolumetricLight", &CV_d3d12_VolumetricLight, 1, VF_DUMPTODISK,
                     "With r_HDRRendering, the volumetric light: the air scatters the sun's light, in the shadows of the sun's cascades "
                     "(shafts of light), and the light from all round towards the camera, and hides what is behind it; none indoors where the outdoor lights do not reach (0: off)");
  iConsole->Register("d3d12_VolumetricLightDensity", &CV_d3d12_VolumetricLightDensity, 0.002f, VF_DUMPTODISK,
                     "With d3d12_VolumetricLight, the air's extinction near the water (1 / m: a part of the light it takes away and scatters a metre)");
  iConsole->Register("d3d12_VolumetricLightSun", &CV_d3d12_VolumetricLightSun, 1.0f, VF_DUMPTODISK,
                     "With d3d12_VolumetricLight, the sun's light the air scatters, times the sun's (0: none)");
  iConsole->Register("d3d12_VolumetricLightAmbient", &CV_d3d12_VolumetricLightAmbient, 1.0f, VF_DUMPTODISK,
                     "With d3d12_VolumetricLight and d3d12_VolumetricLightShafts 0, the light from all round the air scatters, times the fog's colour (a quarter of it in the sun's shadow; 0: none)");
  iConsole->Register("d3d12_VolumetricLightShafts", &CV_d3d12_VolumetricLightShafts, 4.0f, VF_DUMPTODISK,
                     "With d3d12_VolumetricLight, the shafts of the sun alone: the air scatters the sun's light as much as the air around it "
                     "(within this many metres across the sun's direction) is in shadow: none in the open air, all in a shaft into the shade of "
                     "the leaves or a room; no light from all round, and what is behind is not hidden (the game's fog is the haze). 0: all the "
                     "air, with d3d12_VolumetricLightAmbient (as before)");
  iConsole->Register("d3d12_VolumetricLightShaftsSun", &CV_d3d12_VolumetricLightShaftsSun, 16.0f, VF_DUMPTODISK,
                     "With d3d12_VolumetricLightShafts, the sun's light the air of the shafts scatters, times d3d12_VolumetricLightSun "
                     "(the shafts are a little of the air: brighter than all of it lit alike)");
  iConsole->Register("d3d12_VolumetricLightShaftsPower", &CV_d3d12_VolumetricLightShaftsPower, 3.0f, VF_DUMPTODISK,
                     "With d3d12_VolumetricLightShafts, the sun's light as much as this power of the part of the air around in shadow: "
                     "1 the edges of the wide shadows and the dappled light under the leaves glow too, higher only the narrow shafts");
  iConsole->Register("d3d12_VolumetricLightAnisotropy", &CV_d3d12_VolumetricLightAnisotropy, 0.3f, VF_DUMPTODISK,
                     "With d3d12_VolumetricLight, how much more of the sun's light the air scatters forwards: 0 the same all round, up to 0.95");
  iConsole->Register("d3d12_VolumetricLightDistance", &CV_d3d12_VolumetricLightDistance, 60.0f, VF_DUMPTODISK,
                     "With d3d12_VolumetricLight, how far it reaches (m; beyond the sun's cascades, d3d12_ShadowDistance, it has no shadows)");
  iConsole->Register("d3d12_VolumetricLightHeight", &CV_d3d12_VolumetricLightHeight, 40.0f, VF_DUMPTODISK,
                     "With d3d12_VolumetricLight, the height above the water over which the air thins to half (m; 0: the same everywhere)");
  iConsole->Register("d3d12_VolumetricLightRays", &CV_d3d12_VolumetricLightRays, 1, VF_DUMPTODISK,
                     "With d3d12_VolumetricLight and the sun's shadow by rays (d3d12_RTSunShadows) or the path tracer: 1 the air's "
                     "sun by a ray from each froxel's point through the scene for rays (its shafts through the leaves, the windows and "
                     "the holes of a roof, indoors as well: Training's bunker), and the planes that stand for the shafts of sunlight "
                     "(TemplMuzzleFlash_Auto) not drawn; 0: by the sun's cascades, none indoors, the planes drawn (the frames before); "
                     "2: as 1, the air lit by the states its rays found, in colours (red the sun reaches it, green in a shadow, "
                     "blue under a surface: not air; with d3d12_VolumetricLight 2 alone)");
  iConsole->Register("d3d12_VolumetricLightRaysHalf", &CV_d3d12_VolumetricLightRaysHalf, 1, 0,
                     "With the volumetric light's sun by rays (d3d12_VolumetricLightRays), 1: every other froxel traces its ray "
                     "towards the sun in a frame, the froxels between them in the next, each keeping the state it found for a "
                     "frame (half the rays; the froxels blend the frames before in anyway); 0: every froxel each frame (the "
                     "frames before)");
  iConsole->Register("d3d12_VolumetricLightIndoors", &CV_d3d12_VolumetricLightIndoors, 2.5f, VF_DUMPTODISK,
                     "With the volumetric light's sun by rays (d3d12_VolumetricLightRays), its air indoors (where the outdoor lights do "
                     "not reach: Training's bunker) d3d12_VolumetricLightDensity times this: a closed room's dust, which shows the "
                     "shafts through its openings (1: as outdoors); it fades in and out with the camera's way in and out");
  // The sun through the leaves and the grass (General.hlsl, DF2_FOLIAGE).
  iConsole->Register("d3d12_FoliageTranslucency", &CV_d3d12_FoliageTranslucency, 1.5f, VF_DUMPTODISK,
                     "The sun's light through the leaves and the grass seen against it, where the sun's cascades show the sun, times the "
                     "sun's colour and the leaf's (0: off)");
  iConsole->Register("d3d12_FoliageTranslucencyPower", &CV_d3d12_FoliageTranslucencyPower, 4.0f, VF_DUMPTODISK,
                     "With d3d12_FoliageTranslucency, how narrow the light around the sun is: the power of the cosine of the angle "
                     "between the view and the sun");
  iConsole->Register("d3d12_FoliageTranslucencyDistance", &CV_d3d12_FoliageTranslucencyDistance, 60.0f, VF_DUMPTODISK,
                     "With d3d12_FoliageTranslucency, where it ends (m; all of it up to half of that)");
}

//////////////////////////////////////////////////////////////////////////
// Window and device

WIN_HWND CD3D12Renderer::Init(int x,int y,int width,int height,unsigned int cbpp, int zbpp, int sbits, bool fullscreen,WIN_HINSTANCE hinst, WIN_HWND Glhwnd, WIN_HDC Glhdc, WIN_HGLRC hGLrc, bool bReInit)
{
  if (m_pDevice)
    return m_hWnd;
  m_nWidth = std::max(width, 1);
  m_nHeight = std::max(height, 1);
  m_nCVarWidth = GetCVarInt("r_Width", width);
  m_nCVarHeight = GetCVarInt("r_Height", height);
  m_nColorBits = cbpp;
  m_nDepthBits = 24;
  m_nStencilBits = 8;
  m_bFullScreen = fullscreen;

  if (Glhwnd)
  {
    // Hosted (editor): draw into the given window.
    m_hWnd = (HWND)Glhwnd;
    m_bOwnWindow = false;
  }
  else
  {
    if (MakeDpiAware())
      iLog->Log("XRenderD3D12: the process is per monitor DPI aware");
    // The engine names its keys with the keyboard layout its input starts with, after the
    // renderer: with another one than English the bindings of W, A, S, D and the other letters
    // find no key (none in the controls menu). English for the game, however it was started.
    if (const HKL hEnglish = LoadKeyboardLayoutA("00000409", 0))
      ActivateKeyboardLayout(hEnglish, KLF_SETFORPROCESS);
    // FarCry.exe registers the class (RetailRenderer.h, "Initialization"); without it (another host)
    // a class of our own.
    const char *szClass = RetailRenderer::szWindowClass;
    WNDCLASSA wc = {};
    if (!GetClassInfoA((HINSTANCE)hinst, szClass, &wc) && !GetClassInfoA(GetModuleHandleA(nullptr), szClass, &wc))
    {
      wc.lpfnWndProc = DefWindowProcA;
      wc.hInstance = (HINSTANCE)hinst;
      wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
      wc.lpszClassName = szClass;
      RegisterClassA(&wc);
    }
    // Windowed, the window's outer size is the requested size, as the engine expects; full screen,
    // borderless over the monitor, which ShowFullScreen gives the display mode of that size.
    m_hWnd = CreateWindowExA(fullscreen ? WS_EX_TOPMOST : 0, szClass, "Far Cry", fullscreen ? nFullScreenStyle : nWindowedStyle,
      fullscreen ? 0 : x, fullscreen ? 0 : y, m_nWidth, m_nHeight, nullptr, nullptr, (HINSTANCE)hinst, nullptr);
    if (!m_hWnd)
    {
      iLog->LogError("XRenderD3D12: can't create the window (error %u)", (unsigned)GetLastError());
      return 0;
    }
    m_bOwnWindow = true;
    m_pfnGameWndProc = (WNDPROC)SetWindowLongPtrA(m_hWnd, GWLP_WNDPROC, (LONG_PTR)&CD3D12Renderer::WindowProc);
    if (fullscreen)
      ShowFullScreen(m_nWidth, m_nHeight);
    ShowWindow(m_hWnd, SW_SHOWNORMAL);
    UpdateWindow(m_hWnd);
    SetForegroundWindow(m_hWnd);
    SetFocus(m_hWnd);
  }

  const int nDebug = CV_d3d12_debug != 0 ? (CV_d3d12_debug == 2 ? 2 : 1) : DebugEnv();
  const int nDredEnv = DredEnv();
  const int nDred = nDredEnv >= 0 ? nDredEnv : std::clamp(CV_d3d12_DRED, 0, 2);
  m_pDevice = std::make_unique<CD3D12Device>();
  m_pDescriptors = std::make_unique<CD3D12Descriptors>();
  m_pUpload = std::make_unique<CD3D12Upload>();
  m_LastExt.nGPU = 0;  // an address of another upload's memory
  m_pPipeline = std::make_unique<CD3D12Pipeline>();
  m_pCompute = std::make_unique<CD3D12Compute>();
  m_pRayScene = std::make_unique<CD3D12RayScene>();
  m_pTextures = std::make_unique<CTextureManager>();
  m_pShaders = std::make_unique<CShaderManager>();
  m_pScene = std::make_unique<CScene>();
  StartWaterTextures();
  if (!m_pDevice->Init(m_hWnd, m_nWidth, m_nHeight, nDebug, nDred)
    || !m_pDescriptors->Init(m_pDevice->GetDevice())
    || !m_pUpload->Init(m_pDevice->GetDevice(), nFrameRingSize, CV_d3d12_FrameMemoryVideo > 0)
    || !m_pPipeline->Init(m_pDevice->GetDevice())
    || !m_pCompute->Init(m_pDevice->GetDevice())
    || !m_pTextures->Init()
    || !m_pShaders->Init()
    || !m_pScene->Init())
  {
    iLog->LogError("XRenderD3D12: initialization failed");
    return 0;
  }
  // Without rays the scene for them stays empty (RayTracing.h), and the water has no reflection by
  // them: no d3d12_RTReflections, which greys out the menu's list (04-video-ray-tracing.patch).
  if (!m_pRayScene->Init(m_pDevice->GetDevice()) && iConsole->GetCVar("d3d12_RTReflections"))
  {
    iConsole->UnregisterVariable("d3d12_RTReflections", true);
    CV_d3d12_RTReflections = 0;
    iConsole->UnregisterVariable("d3d12_RTReflectionSurfaces", true);
    CV_d3d12_RTReflectionSurfaces = 0;
    iConsole->UnregisterVariable("d3d12_RTPbrReflections", true);
    CV_d3d12_RTPbrReflections = 0;
    iConsole->UnregisterVariable("d3d12_RTPbrMetalDiffuse", true);
    iConsole->UnregisterVariable("d3d12_RTPbrScreen", true);
    CV_d3d12_RTPbrScreen = 0;
    iConsole->UnregisterVariable("d3d12_RTPbrQuad", true);
    iConsole->UnregisterVariable("d3d12_RTSunShadows", true);
    CV_d3d12_RTSunShadows = 0;
    iConsole->UnregisterVariable("d3d12_RTLightShadows", true);
    CV_d3d12_RTLightShadows = 0;
    iConsole->UnregisterVariable("d3d12_RTLightShadowsMax", true);
    iConsole->UnregisterVariable("d3d12_RTLightBehind", true);
    iConsole->UnregisterVariable("d3d12_RTLightShadowGap", true);
    iConsole->UnregisterVariable("d3d12_RTLampSize", true);
    iConsole->UnregisterVariable("d3d12_RTLampRays", true);
    iConsole->UnregisterVariable("d3d12_RTProjectorShadowGap", true);
    iConsole->UnregisterVariable("d3d12_RTFaceNormal", true);
    iConsole->UnregisterVariable("d3d12_RTGI", true);
    CV_d3d12_RTGI = 0;
    iConsole->UnregisterVariable("d3d12_PathTracing", true);
    CV_d3d12_PathTracing = 0;
    iConsole->UnregisterVariable("d3d12_PTFillLights", true);
    iConsole->UnregisterVariable("d3d12_PTLightDistance", true);
    iConsole->UnregisterVariable("d3d12_PTSun", true);
    iConsole->UnregisterVariable("d3d12_PTLamps", true);
    iConsole->UnregisterVariable("d3d12_PTBakedFalloff", true);
    iConsole->UnregisterVariable("d3d12_PTLightMaps", true);
    iConsole->UnregisterVariable("d3d12_PTEmission", true);
    iConsole->UnregisterVariable("d3d12_PTTerrainDelight", true);
    iConsole->UnregisterVariable("d3d12_PTDebug", true);
    CV_d3d12_PTDebug = 0;
    iConsole->UnregisterVariable("d3d12_PTSky", true);
    iConsole->UnregisterVariable("d3d12_PTBounces", true);
    iConsole->UnregisterVariable("d3d12_PTCache", true);
    iConsole->UnregisterVariable("d3d12_PTSunHistory", true);
    iConsole->UnregisterVariable("d3d12_PTBehind", true);
    iConsole->UnregisterVariable("d3d12_PTCheckerboard", true);
    iConsole->UnregisterVariable("d3d12_PTReferenceBounces", true);
    iConsole->UnregisterVariable("d3d12_PTAmbient", true);
    iConsole->UnregisterVariable("d3d12_PTAmbientRange", true);
    iConsole->UnregisterVariable("d3d12_PTSpecular", true);
    iConsole->UnregisterVariable("d3d12_PTDenoiser", true);
    m_pPTDenoiserVar = nullptr;
  }
  m_Viewport[0] = 0;
  m_Viewport[1] = 0;
  m_Viewport[2] = m_nWidth;
  m_Viewport[3] = m_nHeight;
  iLog->Log("XRenderD3D12: %d x %d%s", m_nWidth, m_nHeight, fullscreen ? ", full screen" : "");
  return (WIN_HWND)m_hWnd;
}

bool CD3D12Renderer::SetCurrentContext(WIN_HWND hWnd)
{
  return true;
}

bool CD3D12Renderer::CreateContext(WIN_HWND hWnd, bool bAllowFSAA)
{
  return true;
}

bool CD3D12Renderer::DeleteContext(WIN_HWND hWnd)
{
  return true;
}

int CD3D12Renderer::GetFeatures()
{
  // No RFT_RGBA: vertex colours are B, G, R, A. RFT_OCCLUSIONTEST, or the engine makes no
  // occlusion test elements; RFT_SHADOWMAP_SELFSHADOW, or objects get no shadow casters (only the
  // terrain receives shadow maps).
  return RFT_MULTITEXTURE | RFT_BUMP | RFT_HWGAMMA | RFT_COMPRESSTEXTURE | RFT_ALLOWANISOTROPIC | RFT_SUPPORTZBIAS
    | RFT_DETAILTEXTURE | RFT_OCCLUSIONTEST | RFT_HW_RADEON | RFT_HW_VS | RFT_HW_PS20 | RFT_HW_PS30
    | RFT_DIRECTACCESSTOVIDEOMEMORY | RFT_SHADOWMAP_SELFSHADOW;
}

int CD3D12Renderer::GetMaxTextureMemory()
{
  return 512 << 20;
}

void CD3D12Renderer::ShutDown(bool bReInit)
{
}

int CD3D12Renderer::EnumDisplayFormats(TArray<SDispFormat>& Formats, bool bReset)
{
  if (bReset)
  {
    Formats.Free();
    return 0;
  }
  DEVMODEA dm = {};
  dm.dmSize = sizeof(dm);
  for (DWORD i = 0; EnumDisplaySettingsA(nullptr, i, &dm); i++)
  {
    if (dm.dmBitsPerPel < 16 || dm.dmPelsWidth < 640)
      continue;
    bool bKnown = false;
    for (int j = 0; j < Formats.Num() && !bKnown; j++)
      bKnown = Formats[j].m_Width == (int)dm.dmPelsWidth && Formats[j].m_Height == (int)dm.dmPelsHeight && Formats[j].m_BPP == (int)dm.dmBitsPerPel;
    if (!bKnown)
    {
      SDispFormat f;
      f.m_Width = dm.dmPelsWidth;
      f.m_Height = dm.dmPelsHeight;
      f.m_BPP = dm.dmBitsPerPel;
      Formats.AddElem(f);
    }
  }
  return Formats.Num();
}

bool CD3D12Renderer::ChangeResolution(int nNewWidth, int nNewHeight, int nNewColDepth, int nNewRefreshHZ, bool bFullScreen)
{
  if (!m_pDevice || nNewWidth <= 0 || nNewHeight <= 0)
    return false;
  if (m_bOwnWindow && !m_bFullScreen)
    SetWindowPos(m_hWnd, nullptr, 0, 0, nNewWidth, nNewHeight, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  m_nWidth = nNewWidth;
  m_nHeight = nNewHeight;
  m_Viewport[2] = nNewWidth;
  m_Viewport[3] = nNewHeight;
  return m_pDevice->Resize(nNewWidth, nNewHeight);
}

void CD3D12Renderer::CheckResolutionChange()
{
  // The engine calls nothing when r_Width, r_Height or r_Fullscreen change (the video options
  // set them while the game runs): windowed, the window gets the new outer size and the back
  // buffer the client size; full screen, the display mode and the back buffer get that size.
  // FarCry.exe then sets the viewport with ChangeViewport.
  const int nW = GetCVarInt("r_Width", m_nCVarWidth), nH = GetCVarInt("r_Height", m_nCVarHeight);
  const bool bFullScreen = m_bOwnWindow ? GetCVarInt("r_Fullscreen", m_bFullScreen) != 0 : m_bFullScreen;
  if ((nW == m_nCVarWidth && nH == m_nCVarHeight && bFullScreen == m_bFullScreen) || nW <= 0 || nH <= 0 || !m_pDevice)
    return;
  m_nCVarWidth = nW;
  m_nCVarHeight = nH;
  if (m_bOwnWindow)
    SetWindowMode(bFullScreen, nW, nH);
  if (m_bOwnWindow && !m_bFullScreen)
  {
    RECT rc;
    GetClientRect(m_hWnd, &rc);
    m_nWidth = std::max<int>(rc.right - rc.left, 1);
    m_nHeight = std::max<int>(rc.bottom - rc.top, 1);
  }
  else
  {
    m_nWidth = nW;
    m_nHeight = nH;
  }
  m_pDevice->Resize(m_nWidth, m_nHeight);
  m_Viewport[0] = m_Viewport[1] = 0;
  m_Viewport[2] = m_nWidth;
  m_Viewport[3] = m_nHeight;
}

void CD3D12Renderer::SetWindowMode(bool bFullScreen, int nWidth, int nHeight)
{
  if (bFullScreen)
  {
    if (!m_bFullScreen)
      GetWindowRect(m_hWnd, &m_rcWindowed);
    m_bFullScreen = true;
    SetWindowLongPtrA(m_hWnd, GWL_STYLE, nFullScreenStyle);
    SetWindowLongPtrA(m_hWnd, GWL_EXSTYLE, WS_EX_TOPMOST);
    // Without the focus (minimised) the mode and the place come with it (OnActivateApp).
    if (m_bAppActive)
      ShowFullScreen(nWidth, nHeight);
    return;
  }
  RestoreDisplayMode();
  m_bFullScreen = false;
  SetWindowLongPtrA(m_hWnd, GWL_STYLE, nWindowedStyle);
  SetWindowLongPtrA(m_hWnd, GWL_EXSTYLE, 0);
  // Where the window was before full screen; started full screen, the middle of the monitor.
  int nX = m_rcWindowed.left, nY = m_rcWindowed.top;
  if (IsRectEmpty(&m_rcWindowed))
  {
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoA(MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTOPRIMARY), &mi);
    nX = std::max<int>(mi.rcWork.left, (mi.rcWork.left + mi.rcWork.right - nWidth) / 2);
    nY = std::max<int>(mi.rcWork.top, (mi.rcWork.top + mi.rcWork.bottom - nHeight) / 2);
  }
  if (IsIconic(m_hWnd))
    ShowWindow(m_hWnd, SW_RESTORE);
  SetWindowPos(m_hWnd, HWND_NOTOPMOST, nX, nY, nWidth, nHeight, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

void CD3D12Renderer::ShowFullScreen(int nWidth, int nHeight)
{
  SetDisplayMode(nWidth, nHeight);
  MONITORINFO mi = {};
  mi.cbSize = sizeof(mi);
  GetMonitorInfoA(MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTOPRIMARY), &mi);
  SetWindowPos(m_hWnd, HWND_TOPMOST, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
    mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

bool CD3D12Renderer::SetDisplayMode(int nWidth, int nHeight)
{
  MONITORINFOEXA mi = {};
  mi.cbSize = sizeof(mi);
  if (!GetMonitorInfoA(MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTOPRIMARY), &mi))
    return false;
  if (m_szDisplayDevice[0] && strcmp(m_szDisplayDevice, mi.szDevice) != 0)
    RestoreDisplayMode();
  // The desktop's own mode: nothing to change.
  DEVMODEA dm = {};
  dm.dmSize = sizeof(dm);
  if (EnumDisplaySettingsA(mi.szDevice, ENUM_REGISTRY_SETTINGS, &dm) && (int)dm.dmPelsWidth == nWidth && (int)dm.dmPelsHeight == nHeight)
  {
    RestoreDisplayMode();
    return true;
  }
  DEVMODEA Best = {};
  for (DWORD i = 0; EnumDisplaySettingsA(mi.szDevice, i, &dm); i++)
  {
    if ((int)dm.dmPelsWidth == nWidth && (int)dm.dmPelsHeight == nHeight && dm.dmBitsPerPel == 32
      && !(dm.dmDisplayFlags & DM_INTERLACED) && dm.dmDisplayFrequency > Best.dmDisplayFrequency)
      Best = dm;
  }
  if (!Best.dmSize)
  {
    iLog->LogWarning("XRenderD3D12: the display has no mode %d x %d; the picture is stretched over its own", nWidth, nHeight);
    RestoreDisplayMode();
    return false;
  }
  Best.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY;
  const LONG nResult = ChangeDisplaySettingsExA(mi.szDevice, &Best, nullptr, CDS_FULLSCREEN, nullptr);
  if (nResult != DISP_CHANGE_SUCCESSFUL)
  {
    iLog->LogWarning("XRenderD3D12: the display mode %d x %d failed (%d); the picture is stretched over the display's own", nWidth, nHeight, (int)nResult);
    RestoreDisplayMode();
    return false;
  }
  strcpy_s(m_szDisplayDevice, mi.szDevice);
  iLog->Log("XRenderD3D12: display mode %d x %d, %u Hz", nWidth, nHeight, (unsigned)Best.dmDisplayFrequency);
  return true;
}

void CD3D12Renderer::RestoreDisplayMode()
{
  if (!m_szDisplayDevice[0])
    return;
  ChangeDisplaySettingsExA(m_szDisplayDevice, nullptr, nullptr, 0, nullptr);
  m_szDisplayDevice[0] = 0;
}

void CD3D12Renderer::OnActivateApp(bool bActive)
{
  if (bActive == m_bAppActive)
    return;
  m_bAppActive = bActive;
  // The display is looked up on a timer only in the background, and again with the focus back (Windows' SDR white
  // level is set in its Settings, out of the game).
  if (m_pDevice)
  {
    m_pDevice->SetAppActive(bActive);
    if (bActive)
      m_pDevice->InvalidateDisplayInfo();
  }
  if (!m_bFullScreen)
    return;
  if (bActive)
  {
    if (IsIconic(m_hWnd))
      ShowWindow(m_hWnd, SW_RESTORE);
    ShowFullScreen(m_nCVarWidth, m_nCVarHeight);
  }
  else
  {
    // As the original's full screen: the desktop's mode back, and the window out of the way (it
    // is topmost).
    RestoreDisplayMode();
    ShowWindow(m_hWnd, SW_SHOWMINNOACTIVE);
  }
}

LRESULT CALLBACK CD3D12Renderer::WindowProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam)
{
  CD3D12Renderer *pRenderer = g_pRenderer;
  if (!pRenderer || pRenderer->m_hWnd != hWnd || !pRenderer->m_pfnGameWndProc)
    return DefWindowProcA(hWnd, nMsg, wParam, lParam);
  if (nMsg == WM_ACTIVATEAPP)
    pRenderer->OnActivateApp(wParam != 0);
  else if (nMsg == WM_DISPLAYCHANGE && pRenderer->m_pDevice)
    pRenderer->m_pDevice->InvalidateDisplayInfo();
  return CallWindowProcA(pRenderer->m_pfnGameWndProc, hWnd, nMsg, wParam, lParam);
}

void CD3D12Renderer::Release()
{
  CpuSampler::Update(0, 0, "XRenderD3D12_samples.txt");
  ReleaseDrawDistance();
  if (m_DisplayInfo.pVar && iConsole)
    iConsole->RemoveConsoleVarSink(&m_DisplayInfo);
  m_DisplayInfo.pVar = nullptr;
  // The engine still asks GetType while this runs.
  if (m_pDevice)
    m_pDevice->WaitForGPU();
  if (m_pScene)
    m_pScene->Shutdown();
  Geometry::Shutdown();     // the leaf buffers the engine left, before the upload heap goes
  if (m_pShaders)
    m_pShaders->Shutdown();
  EndWaterTextures();
  ReleaseWaterResources();
  ReleaseWaterReflection();
  ReleaseOcean();
  ReleaseWakes();
  ReleaseOceanGrid();
  ReleaseUnderwater();
  FreeSceneCopy(m_ReflCopy);
  m_nReflCopyFrame = -2;
  ReleaseMirror();
  ReleaseEnvCubes();
  ReleaseSprites();
  ReleaseShadowMaps();
  ReleaseCascades();
  ReleaseScreenEffects();
  ReleaseFlares();
  ReleaseAO();
  ReleasePathTargets();
  ReleaseUpscaler();
  ReleaseHdr();
  ReleaseVolumetricLight();
  ReleaseRaySun();
  ReleaseHdrOutput();
  ReleaseHdrStats();
  m_pDetailTexture = nullptr;   // released with every texture
  m_pDefaultCube = nullptr;
  m_pBeachTexture = m_pBeachWetTexture = m_pBeachBumpTexture = m_pBeachFaintTexture = m_pBeachDetailTexture = nullptr;
  m_pHazeMask = m_pHazeBump = nullptr;
  for (CTexture *&pTex : m_pFxTextures)
    pTex = nullptr;
  m_nWaveTexture = 0;
  m_nCausticsTexture = 0;
  m_nFoamTexture = 0;
  m_nPondRippleTexture = 0;
  m_SkyBoxes.clear();
  m_nSceneSkyBox = -1;
  if (m_pTextures)
    m_pTextures->Shutdown();
  if (m_pRayScene)
    m_pRayScene->Shutdown();
  m_pNrd.reset();
  if (m_pCompute)
    m_pCompute->Shutdown();
  if (m_pPipeline)
    m_pPipeline->Shutdown();
  if (m_pUpload)
    m_pUpload->Shutdown();
  if (m_pDescriptors)
    m_pDescriptors->Shutdown();
  if (m_pDevice)
    m_pDevice->Shutdown();
  m_pScene.reset();
  m_pShaders.reset();
  m_pTextures.reset();
  m_pRayScene.reset();
  m_pCompute.reset();
  m_pPipeline.reset();
  m_pUpload.reset();
  m_pDescriptors.reset();
  m_pDevice.reset();
  if (m_bOwnWindow && m_hWnd)
  {
    RestoreDisplayMode();
    if (m_pfnGameWndProc)
      SetWindowLongPtrA(m_hWnd, GWLP_WNDPROC, (LONG_PTR)m_pfnGameWndProc);
    DestroyWindow(m_hWnd);
  }
  m_hWnd = nullptr;
  delete this;
}

void CD3D12Renderer::FreeResources(int nFlags)
{
}

void CD3D12Renderer::RefreshResources(int nFlags)
{
}

void CD3D12Renderer::PreLoad (void)
{
  // A new level registers its own fog volumes and makes its own sprites; the objects of a load
  // that never got drawn may be gone, and so may the textures of the water reflection's draws
  // kept for the rays.
  if (m_pScene)
    m_pScene->ResetFogVolumes();
  if (m_pRayScene)
    m_pRayScene->ClearReflection();
  m_SpritesToBake.clear();
  PurgeSprites();
  // Its static casters (MakeShadowMapFrustum, while it loads).
  m_StaticCasterBuffers.clear();
  // Its terrain under the ocean's grid (OceanGrid.cpp) and its heights (the camera towards the sun, Cascades.cpp).
  m_OceanGrid.bTerrainStale = true;
  m_TerrainRange.bStale = true;
}

void CD3D12Renderer::PostLoad (void)
{
  m_OceanGrid.bTerrainStale = true;
  m_TerrainRange.bStale = true;
  PreloadTextures();
  PrepareOcean();
}

//////////////////////////////////////////////////////////////////////////
// Frame

bool CD3D12Renderer::EnsureFrame()
{
  if (!m_pDevice)
    return false;
  if (!m_pDevice->IsInFrame())
  {
    m_pDevice->SetGpuTiming(CV_d3d12_GpuTimes > 0 || CV_r_Stats > 0 || CV_d3d12_FrameLog > 0);
    m_pDevice->SetBarrierMode(std::clamp(CV_d3d12_BarrierBatch, 0, 2), CV_d3d12_BarrierCheck > 0);
    m_pDevice->BeginFrame();
    m_pLastPSO = nullptr;         // a new command list
    InvalidateFrameConstants();   // the frame memory of the slot starts again
    ReadFlareTests();
  }
  return true;
}

void CD3D12Renderer::BeginFrame (void)
{
  FUNCTION_PROFILER(iSystem, PROFILE_RENDERER);
  EnsureFrame();
  UpdateDrawDistance();
  if (m_pScene)
    m_pScene->BeginFrame();
  m_b2DMode = false;
  m_nMatrixDepth = 0;
  m_ViewMatrix = m_CameraView;
  if (CV_d3d12_DumpFrame && !m_pFrameDump)
  {
    m_pFrameDump.reset(fopen("XRenderD3D12_frame.txt", "w"));
    if (m_pFrameDump)
    {
      const Vec3 vPos = m_Camera.GetPos(), vAngles = m_Camera.GetAngles();
      fprintf(m_pFrameDump.get(), "frame %d: camera (%.2f %.2f %.2f) angles (%.2f %.2f %.2f) fov %.3f near %.3f far %.1f\n", m_nFrameID,
              vPos.x, vPos.y, vPos.z, vAngles.x, vAngles.y, vAngles.z, m_Camera.GetFov(), m_Camera.GetZMin(), m_Camera.GetZMax());
      for (const Matrix44 *pM : { &m_CameraView, &m_ProjMatrix })
      {
        fprintf(m_pFrameDump.get(), pM == &m_CameraView ? "view" : "projection");
        for (float f : pM->data)
          fprintf(m_pFrameDump.get(), " %g", f);
        fprintf(m_pFrameDump.get(), "\n");
      }
      for (int i = 1; m_pScene && m_pScene->GetFogVolume(i); i++)
      {
        const SFogVolume *pFV = m_pScene->GetFogVolume(i);
        fprintf(m_pFrameDump.get(), "fog volume %d: max distance %g, layer z %g, colour (%.3f %.3f %.3f), caustics %d\n", i, pFV->fMaxFogDist,
                pFV->fFogLayerZ, pFV->Color.r, pFV->Color.g, pFV->Color.b, (int)pFV->bCaustics);
      }
      if (m_pTextures)
      {
        int nUpscaled, nTextures;
        uint64 nUpscaledBytes, nBytes;
        m_pTextures->UpscaledStats(nUpscaled, nTextures, nUpscaledBytes, nBytes);
        fprintf(m_pFrameDump.get(), "upscaled textures (r_TexUpscaled): %d of %d textures, %.1f MB of %.1f MB\n", nUpscaled, nTextures,
                nUpscaledBytes / 1048576.0, nBytes / 1048576.0);
      }
    }
  }
}

void CD3D12Renderer::Update (void)
{
  FUNCTION_PROFILER(iSystem, PROFILE_RENDERER);
  if (!m_pDevice)
    return;
  EnsureFrame();
  EndMainScene();               // a main view whose EF_EndEf3D did not end (none expected)
  DrawQueuedBoxes();
  DrawStats();
  m_fTopLeftText[1] = m_fTopLeftText[0];
  m_fTopLeftText[0] = 0;
  DumpHdrOutput();
  DumpHdrTargets();
  // Shown only when swapping is on: the hidden frames of a level load are drawn but never seen.
  if (m_bSwapBuffers)
    DrawHdrOutput();
  EndHdrStats();
  if (CV_d3d12_DredTest)
  {
    m_pDevice->DredTest(CV_d3d12_DredTest);
    CV_d3d12_DredTest = 0;
  }
  ParkOcean();
  m_pDevice->EndFrame(m_bSwapBuffers, m_bVSync || CV_r_vsync != 0);
  m_pPipeline->SaveKeys();
  // The game asks for the triangles after the frame is shown (the time demo's Tri/Frame).
  m_nPolygons = m_nFramePolygons;
  m_nFramePolygons = 0;
  CollectStats();
  m_nFrameSunLights = m_nFrameDynLights = 0;
  ReportGpuTimes();
  if (m_pFrameDump)
  {
    m_pFrameDump.reset();
    CV_d3d12_DumpFrame = 0;
    iLog->Log("XRenderD3D12: frame %d written to XRenderD3D12_frame.txt", m_nFrameID);
  }
  m_nFrameID++;
  m_nFrames++;
  CheckResolutionChange();
  UpdateHdrOutput();
}

void CD3D12Renderer::GpuSection(const char *szSection)
{
  if (m_pDevice && m_nRecurseLevel <= 1)
    m_pDevice->GpuSection(szSection);
}

CD3D12Renderer::CGpuSubSection::CGpuSubSection(CD3D12Renderer &r, const char *szSection)
  : m_r(r), m_szBefore(r.m_pDevice ? r.m_pDevice->GetGpuSection() : nullptr)
{
  m_r.GpuSection(szSection);
}

CD3D12Renderer::CGpuSubSection::~CGpuSubSection()
{
  if (m_szBefore)
    m_r.GpuSection(m_szBefore);
}

void CD3D12Renderer::ReportGpuTimes()
{
  LARGE_INTEGER Now, Frequency;
  QueryPerformanceCounter(&Now);
  QueryPerformanceFrequency(&Frequency);
  if (CV_d3d12_GpuTimes <= 0)
  {
    m_GpuReport.Reset();
    m_nTimedFrames = 0;
    m_fFrameMs = 0;
    m_nLastFrameTicks = 0;
    m_pGpuTimes.reset();
    return;
  }
  if (m_nLastFrameTicks)
  {
    m_fFrameMs += (double)(Now.QuadPart - m_nLastFrameTicks) * 1000.0 / (double)Frequency.QuadPart;
    m_nTimedFrames++;
  }
  m_nLastFrameTicks = Now.QuadPart;
  const int n = m_GpuReport.nFrames;
  if (n < CV_d3d12_GpuTimes)
    return;
  std::string Line = "frame " + std::to_string(m_nFrameID) + ", GPU ms per frame (" + std::to_string(n) + " frames):";
  double fTotal = 0;
  char sz[96];
  for (const auto &[szSection, fMs] : m_GpuReport.Sections)
  {
    snprintf(sz, sizeof(sz), " %s %.2f,", szSection, fMs / n);
    Line += sz;
    fTotal += fMs;
  }
  snprintf(sz, sizeof(sz), " total %.2f;", fTotal / n);
  Line += sz;
  for (const auto &[szSection, fMs] : m_GpuReport.Compute)
  {
    snprintf(sz, sizeof(sz), " beside it %s %.2f;", szSection, fMs / n);
    Line += sz;
  }
  snprintf(sz, sizeof(sz), " time between frames %.2f ms", m_nTimedFrames ? m_fFrameMs / m_nTimedFrames : 0.0);
  Line += sz;
  iLog->LogToConsole("XRenderD3D12: %s", Line.c_str());
  if (!m_pGpuTimes)
    m_pGpuTimes.reset(fopen("XRenderD3D12_gpu.txt", "a"));
  if (m_pGpuTimes)
  {
    fprintf(m_pGpuTimes.get(), "%s\n", Line.c_str());
    fflush(m_pGpuTimes.get());
  }
  m_GpuReport.Reset();
  m_nTimedFrames = 0;
  m_fFrameMs = 0;
}

void CD3D12Renderer::ShareResources( IRenderer *renderer )
{
}

void CD3D12Renderer::GetViewport(int *x, int *y, int *width, int *height)
{
  *x = m_Viewport[0];
  *y = m_Viewport[1];
  *width = m_Viewport[2];
  *height = m_Viewport[3];
}

void CD3D12Renderer::SetViewport(int x, int y, int width, int height)
{
  if (!width || !height)
  {
    x = y = 0;
    width = m_nWidth;
    height = m_nHeight;
  }
  m_Viewport[0] = x;
  m_Viewport[1] = y;
  m_Viewport[2] = width;
  m_Viewport[3] = height;
  InvalidateFrameConstants();
}

void CD3D12Renderer::SetScissor(int x, int y, int width, int height)
{
  m_bScissor = width > 0 && height > 0;
  m_Scissor[0] = x;
  m_Scissor[1] = y;
  m_Scissor[2] = width;
  m_Scissor[3] = height;
}

void CD3D12Renderer::MakeCurrent()
{
}

void CD3D12Renderer::CheckError(const char *comment)
{
}

//////////////////////////////////////////////////////////////////////////
// Camera and matrices

Matrix44 CD3D12Renderer::MakeProjection(const CCamera &cam)
{
  // Right-handed, D3D depth 0..1: m11 = 1 / tan(fov * ratio / 2), m00 = m11 * ratio, where the
  // camera's fov is across the screen and ratio is height / width.
  const float fRatio = cam.GetProjRatio() > 0 ? cam.GetProjRatio() : 0.75f;
  const float fNear = cam.GetZMin() > 0 ? cam.GetZMin() : 0.25f;
  const float fFar = cam.GetZMax() > fNear ? cam.GetZMax() : fNear + 1000.0f;
  const float m11 = 1.0f / tanf(cam.GetFov() * fRatio * 0.5f);
  Matrix44 m;
  memset(m.data, 0, sizeof(m.data));
  m.data[0] = m11 * fRatio;
  m.data[5] = m11;
  m.data[10] = fFar / (fNear - fFar);
  m.data[11] = -1;
  m.data[14] = fNear * fFar / (fNear - fFar);
  return m;
}

void CD3D12Renderer::SetCamera(const CCamera &cam)
{
  m_Camera = cam;
  m_CameraView = cam.GetVCMatrixD3D9();
  m_ViewMatrix = m_CameraView;
  m_nMatrixDepth = 0;
  m_ProjMatrix = MakeProjection(cam);
  m_SkyProjMatrix = m_ProjMatrix;
  // The camera of a mirror's picture (the frame's mirrored in its plane): nothing behind the mirror.
  if (m_bMirrorPass)
  {
    m_MirrorViewProj = m_CameraView * m_ProjMatrix;
    ClipProjectionToPlane(m_ProjMatrix, m_CameraView, m_vMirrorNormal, m_vMirrorPoint);
  }
  // The camera of the water reflection (the frame's mirrored in the water plane): nothing under the water.
  else if (m_bReflectionPass)
  {
    m_ReflectionViewProj = m_CameraView * m_ProjMatrix;
    ClipProjectionToWater(m_ProjMatrix, m_CameraView);
  }
  if (m_bUpscaling)
    UpdateReprojection();
  InvalidateFrameConstants();
}

const CCamera& CD3D12Renderer::GetCamera()
{
  return m_Camera;
}

bool CD3D12Renderer::SetGammaDelta(const float fGamma)
{
  return true;
}

bool CD3D12Renderer::ChangeDisplay(unsigned int width,unsigned int height,unsigned int cbpp)
{
  return false;
}

void CD3D12Renderer::ChangeViewport(unsigned int x,unsigned int y,unsigned int width,unsigned int height)
{
  // From FarCry.exe after a resolution change: from now on this is the size of the frame.
  if (!width || !height)
    return;
  m_nWidth = width;
  m_nHeight = height;
  m_Viewport[0] = x;
  m_Viewport[1] = y;
  m_Viewport[2] = width;
  m_Viewport[3] = height;
  if (m_pDevice && (m_pDevice->GetWidth() != (int)width || m_pDevice->GetHeight() != (int)height))
    m_pDevice->Resize(width, height);
}

int CD3D12Renderer::SetPolygonMode(int mode)
{
  const int nOld = m_nPolygonMode;
  m_nPolygonMode = mode;
  return nOld;
}

int CD3D12Renderer::GetWidth()
{
  return m_nWidth;
}

int CD3D12Renderer::GetHeight()
{
  return m_nHeight;
}

void CD3D12Renderer::GetMemoryUsage(ICrySizer* Sizer)
{
}

void CD3D12Renderer::ScreenShot(const char *filename)
{
  if (!m_pDevice)
    return;
  const int w = m_pDevice->GetWidth(), h = m_pDevice->GetHeight();
  std::vector<uint8> Pixels((size_t)w * h * 4);
  if (!m_pDevice->ReadBackBuffer(Pixels.data(), w, h))
    return;
  char szName[MAX_PATH];
  if (filename && filename[0])
    _snprintf(szName, sizeof(szName), "%s", filename);
  else
  {
    CreateDirectoryA("ScreenShots", nullptr);
    for (int i = 0; i < 10000; i++)
    {
      _snprintf(szName, sizeof(szName), "ScreenShots/FarCry%04d.tga", i);
      if (GetFileAttributesA(szName) == INVALID_FILE_ATTRIBUTES)
        break;
    }
  }
  szName[sizeof(szName) - 1] = 0;
  WriteTGA(Pixels.data(), w, h, szName, 32);
  iLog->Log("XRenderD3D12: screenshot %s", szName);
}

int CD3D12Renderer::GetColorBpp()
{
  return m_nColorBits;
}

int CD3D12Renderer::GetDepthBpp()
{
  return m_nDepthBits;
}

int CD3D12Renderer::GetStencilBpp()
{
  return m_nStencilBits;
}

void CD3D12Renderer::ProjectToScreen( float ptx, float pty, float ptz, float *sx, float *sy, float *sz )
{
  // Percent of the viewport (0..100, y from the top, not clamped) and the depth z / w.
  const float v[3] = { ptx, pty, ptz };
  float e[3], c[3];
  const float we = TransformPoint(m_ViewMatrix, v, e);
  float r[4];
  for (int j = 0; j < 4; j++)
    r[j] = e[0] * m_ProjMatrix.data[j] + e[1] * m_ProjMatrix.data[4 + j] + e[2] * m_ProjMatrix.data[8 + j] + we * m_ProjMatrix.data[12 + j];
  const float w = fabsf(r[3]) > 1e-12f ? r[3] : 1e-12f;
  c[0] = r[0] / w;
  c[1] = r[1] / w;
  c[2] = r[2] / w;
  *sx = (c[0] * 0.5f + 0.5f) * 100.0f;
  *sy = (0.5f - c[1] * 0.5f) * 100.0f;
  *sz = c[2];
}

int CD3D12Renderer::UnProject(float sx, float sy, float sz, float *px, float *py, float *pz, const float modelMatrix[16], const float projMatrix[16], const int viewport[4])
{
  // As gluUnProject, with D3D depth (0..1) and row vector matrices: window to the space before
  // modelMatrix * projMatrix.
  Matrix44 m, mv, pr;
  memcpy(mv.data, modelMatrix, sizeof(mv.data));
  memcpy(pr.data, projMatrix, sizeof(pr.data));
  m = mv * pr;
  m.Invert44();
  if (!viewport[2] || !viewport[3])
    return 0;
  const float ndc[3] = { (sx - viewport[0]) * 2.0f / viewport[2] - 1.0f, (sy - viewport[1]) * 2.0f / viewport[3] - 1.0f, sz };
  float r[3];
  const float w = TransformPoint(m, ndc, r);
  if (fabsf(w) < 1e-12f)
    return 0;
  *px = r[0] / w;
  *py = r[1] / w;
  *pz = r[2] / w;
  return 1;
}

int CD3D12Renderer::UnProjectFromScreen( float sx, float sy, float sz, float *px, float *py, float *pz)
{
  return UnProject(sx, sy, sz, px, py, pz, m_ViewMatrix.data, m_ProjMatrix.data, m_Viewport);
}

void CD3D12Renderer::GetModelViewMatrix(float *mat)
{
  memcpy(mat, m_ViewMatrix.data, sizeof(m_ViewMatrix.data));
}

void CD3D12Renderer::GetModelViewMatrix(double *mat)
{
  for (int i = 0; i < 16; i++)
    mat[i] = m_ViewMatrix.data[i];
}

void CD3D12Renderer::GetProjectionMatrix(double *mat)
{
  for (int i = 0; i < 16; i++)
    mat[i] = m_ProjMatrix.data[i];
}

void CD3D12Renderer::GetProjectionMatrix(float *mat)
{
  memcpy(mat, m_ProjMatrix.data, sizeof(m_ProjMatrix.data));
}

Vec3 CD3D12Renderer::GetUnProject(const Vec3 &WindowCoords,const CCamera &cam)
{
  Matrix44 View = cam.GetVCMatrixD3D9(), Proj = MakeProjection(cam);
  Vec3 v(0, 0, 0);
  UnProject(WindowCoords.x, WindowCoords.y, WindowCoords.z, &v.x, &v.y, &v.z, View.data, Proj.data, m_Viewport);
  return v;
}

void CD3D12Renderer::RenderToViewport(const CCamera &cam, float x, float y, float width, float height)
{
}

void CD3D12Renderer::MakeMatrix(const Vec3 & pos, const Vec3 & angles,const Vec3 & scale, Matrix44* mat)
{
  // Leaves mat as it is.
}

void CD3D12Renderer::EnableVSync(bool enable)
{
  m_bVSync = enable;
}

void CD3D12Renderer::PushMatrix()
{
  if (m_nMatrixDepth < MATRIX_STACK)
    m_MatrixStack[m_nMatrixDepth] = m_ViewMatrix;
  m_nMatrixDepth++;
}

void CD3D12Renderer::PopMatrix()
{
  if (m_nMatrixDepth <= 0)
    return;
  m_nMatrixDepth--;
  if (m_nMatrixDepth < MATRIX_STACK)
    m_ViewMatrix = m_MatrixStack[m_nMatrixDepth];
}

// The stack transforms the vertices first, then the current modelview (row vectors: M' = T * M).

void CD3D12Renderer::RotateMatrix(float a,float x,float y,float z)
{
  m_ViewMatrix = Rotation44(a, x, y, z) * m_ViewMatrix;
}

void CD3D12Renderer::RotateMatrix(const Vec3 & angels)
{
  RotateMatrix(angels.z, 0, 0, 1);
  RotateMatrix(angels.y, 0, 1, 0);
  RotateMatrix(angels.x, 1, 0, 0);
}

void CD3D12Renderer::TranslateMatrix(float x,float y,float z)
{
  Matrix44 t = Identity44();
  t.data[12] = x;
  t.data[13] = y;
  t.data[14] = z;
  m_ViewMatrix = t * m_ViewMatrix;
}

void CD3D12Renderer::ScaleMatrix(float x,float y,float z)
{
  Matrix44 s = Identity44();
  s.data[0] = x;
  s.data[5] = y;
  s.data[10] = z;
  m_ViewMatrix = s * m_ViewMatrix;
}

void CD3D12Renderer::TranslateMatrix(const Vec3 &pos)
{
  TranslateMatrix(pos.x, pos.y, pos.z);
}

void CD3D12Renderer::MultMatrix(float * mat)
{
  Matrix44 m;
  memcpy(m.data, mat, sizeof(m.data));
  m_ViewMatrix = m * m_ViewMatrix;
}

void CD3D12Renderer::LoadMatrix(const Matrix44 *src)
{
  m_ViewMatrix = src ? *src : Identity44();
}

void CD3D12Renderer::ResetToDefault()
{
  m_nState = GS_DEPTHWRITE;
  m_nCullMode = R_CULL_BACK;
  m_nCurTexture[0] = m_nCurTexture[1] = -1;
  m_nCurTMU = 0;
  m_bTexClamp = false;
  m_MaterialColor[0] = m_MaterialColor[1] = m_MaterialColor[2] = m_MaterialColor[3] = 1;
  memset(m_ColorOp, 0, sizeof(m_ColorOp));
  m_bTexMatrix = false;
  m_bTexGen = false;
  m_bDepthBias = false;
  m_nPolygonMode = R_SOLID_MODE;
}

//////////////////////////////////////////////////////////////////////////
// Frame buffer

void CD3D12Renderer::SetClearColor(const Vec3 & vColor)
{
  m_vClearColor = vColor;
}

void CD3D12Renderer::ClearDepthBuffer()
{
  // d3d12_RTPbrScreen: the picture before the engine clears the depth for what it draws near (the weapon in the hands),
  // not an offscreen target's; with MSAA its depth resolved first (the colour CopyTargets resolves).
  if (m_pDevice && !m_pDevice->IsOffscreen() && WantsSceneCopyForReflections())
  {
    ResolveMsaaDepth();
    CopySceneForReflections();
  }
  if (EnsureFrame())
    m_pDevice->ClearDepth(1.0f, 0, true, true);
  m_bMsaaDepthDirty |= m_pDevice && m_pDevice->IsInMsaa();
}

void CD3D12Renderer::ClearColorBuffer(const Vec3 vColor)
{
  if (!EnsureFrame())
    return;
  const float rgba[4] = { vColor.x, vColor.y, vColor.z, 0 };
  m_pDevice->ClearColor(rgba);
}

void CD3D12Renderer::ReadFrameBuffer(unsigned char * pRGB, int nSizeX, int nSizeY, bool bBackBuffer, bool bRGBA, int nScaledX, int nScaledY)
{
  // nSizeX x nSizeY pixels, rows from the top, R, G, B (and A with bRGBA); scaled to
  // nScaledX x nScaledY (point sampled) when given.
  if (!m_pDevice || !pRGB || nSizeX <= 0 || nSizeY <= 0)
    return;
  // A frame read before Update (a demo's) has the statistics Update would draw.
  if (m_pDevice->IsInFrame())
    DrawStats();
  const int w = m_pDevice->GetWidth(), h = m_pDevice->GetHeight();
  std::vector<uint8> Pixels((size_t)w * h * 4);
  if (!m_pDevice->ReadBackBuffer(Pixels.data(), w, h))
    return;
  const int nOutW = nScaledX > 0 ? nScaledX : nSizeX, nOutH = nScaledY > 0 ? nScaledY : nSizeY;
  const int nBytes = bRGBA ? 4 : 3;
  for (int y = 0; y < nOutH; y++)
  {
    const int sy = nScaledY > 0 ? y * std::min(nSizeY, h) / nOutH : y;
    uint8 *pDst = pRGB + (size_t)y * nOutW * nBytes;
    for (int x = 0; x < nOutW; x++, pDst += nBytes)
    {
      const int sx = nScaledX > 0 ? x * std::min(nSizeX, w) / nOutW : x;
      if (sx >= w || sy >= h)
      {
        memset(pDst, 0, nBytes);
        continue;
      }
      const uint8 *pSrc = &Pixels[((size_t)sy * w + sx) * 4];
      pDst[0] = pSrc[2];
      pDst[1] = pSrc[1];
      pDst[2] = pSrc[0];
      if (bRGBA)
        pDst[3] = pSrc[3];
    }
  }
}

//////////////////////////////////////////////////////////////////////////
// Queries and the rest

void *CD3D12Renderer::EF_Query(int Query, int Param)
{
  switch (Query)
  {
  case EFQ_Pointer2FrameID:
    return &m_nFrameID;
  case EFQ_RecurseLevel:
    return (void *)(INT_PTR)m_nRecurseLevel;
  case EFQ_DeviceLost:
    return &m_nDeviceLost;
  case EFQ_LightSource:
    return m_pScene ? m_pScene->GetLight(Param) : nullptr;
  case EFQ_NUMEFS:
    return m_pShaders ? (void *)(INT_PTR)m_pShaders->GetCount() : nullptr;
  case EFQ_NumRenderItems:
    return m_pScene ? (void *)(INT_PTR)m_pScene->GetNumItems() : nullptr;
  default:
    // EFQ_D3DDevice and EFQ_glReadPixels: there is no D3D9 device or GL context.
    return nullptr;
  }
}

int CD3D12Renderer::GetPolyCount()
{
  return m_nPolygons;
}

void CD3D12Renderer::GetPolyCount(int &nPolygons,int &nShadowVolPolys)
{
  // No stencil shadow volumes are drawn (CRETriMeshShadow).
  nPolygons = m_nPolygons;
  nShadowVolPolys = 0;
}

int CD3D12Renderer::GetFrameID(bool bIncludeRecursiveCalls)
{
  return m_nFrameID;
}

char CD3D12Renderer::GetType()
{
  return m_cType;
}

char* CD3D12Renderer::GetVertexProfile(bool bSupportedProfile)
{
  return s_szVertexProfile;
}

char* CD3D12Renderer::GetPixelProfile(bool bSupportedProfile)
{
  return s_szPixelProfile;
}

void CD3D12Renderer::SetType(char type)
{
  m_cType = type;
}

unsigned int CD3D12Renderer::Make3DSprite(int nTexSize, float fAngleStep, IStatObj * pStatObj)
{
  return 0;
}

void CD3D12Renderer::EnableSwapBuffers(bool bEnable)
{
  m_bSwapBuffers = bEnable;
}

WIN_HWND CD3D12Renderer::GetHWND()
{
  return (WIN_HWND)m_hWnd;
}

void CD3D12Renderer::OnEntityDeleted(IEntityRender * pEntityRender)
{
}

int CD3D12Renderer::EnumAAFormats(TArray<SAAFormat>& Formats, bool bReset)
{
  if (bReset)
    Formats.Free();
  return 0;
}

int CD3D12Renderer::CreateRenderTarget (int nWidth, int nHeight, ETEX_Format eTF)
{
  return 0;
}

bool CD3D12Renderer::DestroyRenderTarget (int nHandle)
{
  return false;
}

bool CD3D12Renderer::SetRenderTarget (int nHandle)
{
  return false;
}

float CD3D12Renderer::EF_GetWaterZElevation(float fX, float fY)
{
  I3DEngine *p3DEngine = iSystem ? iSystem->GetI3DEngine() : nullptr;
  return p3DEngine ? p3DEngine->GetWaterLevel() : 0;
}

void CD3D12Renderer::EF_PolygonOffset(bool bEnable, float fFactor, float fUnits)
{
  m_bDepthBias = bEnable;
}
