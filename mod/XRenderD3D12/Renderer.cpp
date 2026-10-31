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
#include "RayTracing.h"
#include "Texture.h"
#include "Shader.h"
#include "Scene.h"
#include "CpuSampler.h"

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
  iConsole->Register("d3d12_DumpFrame", &CV_d3d12_DumpFrame, 0, 0, "Writes the render items and draws of the next frame to XRenderD3D12_frame.txt");
  iConsole->Register("d3d12_WaterDebug", &CV_d3d12_WaterDebug, 0, 0,
                     "Water surfaces show one term: 1 refraction, 2 reflection, 3 water thickness (0-2 m: black-white), 4 normal, 5 screen space reflection hits, 6 their colour, 7 the planar reflection, "
                     "8 the scene behind, 9 view path (red, 0-32 m), depth (green, 0-16 m) and fresnel (blue), 10 the vertex colour, 11 its alpha, "
                     "12 the RMS of the wave slopes finer than a pixel (white: 0.1); with d3d12_RTReflections: 13 the rays' hits, 14 their distance "
                     "(blue near, red at d3d12_RTDistance), 15 how much they count");
  iConsole->Register("d3d12_Caustics", &CV_d3d12_Caustics, 1, VF_DUMPTODISK, "Caustics on the terrain under the ocean; 0 off");
  iConsole->Register("d3d12_WaterSunGlint", &CV_d3d12_WaterSunGlint, 0.0f, VF_DUMPTODISK,
                     "The sun's highlight on water surfaces: its strength (0: none, as the game draws water)");
  iConsole->Register("d3d12_GpuTimes", &CV_d3d12_GpuTimes, 0, 0,
                     "N: every N frames, the average GPU time of the frame's sections (shadow maps, water reflection, opaque, "
                     "water, blended, ...) and the time between frames to the console and XRenderD3D12_gpu.txt; 0 off");
  iConsole->Register("d3d12_FrameLog", &CV_d3d12_FrameLog, 0, 0,
                     "1: a line per frame to XRenderD3D12_frames.txt: the time between frames, the CPU's waits and its time in the main "
                     "view, the GPU time of each section, the draws, the triangles and the camera; 2 also the engine's frame profiler (collected "
                     "without its overlay): the self time of each subsystem and of the profilers that took the most; 0 off");
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
  iConsole->Register("d3d12_ShadowCascades", &CV_d3d12_ShadowCascades, 4, VF_DUMPTODISK,
                     "With e_shadow_maps, the sun's cascaded shadow maps around the camera: how many, 1 to 4 (0: none, the shadow maps of the objects alone)");
  iConsole->Register("d3d12_ShadowDistance", &CV_d3d12_ShadowDistance, 120.0f, VF_DUMPTODISK,
                     "How far the sun's cascaded shadow maps reach from the camera (m); the shadow maps of the objects beyond");
  iConsole->Register("d3d12_ShadowCascadeSize", &CV_d3d12_ShadowCascadeSize, 2048, VF_DUMPTODISK, "The size of the sun's cascaded shadow maps, 512 to 4096");
  iConsole->Register("d3d12_CascadeInstancing", &CV_d3d12_CascadeInstancing, 1, 0,
                     "1: each caster of the sun's cascades drawn once into all the slices it reaches (instances); 0: once per slice");
  iConsole->Register("d3d12_ShadowSoftness", &CV_d3d12_ShadowSoftness, 0.05f, VF_DUMPTODISK,
                     "The softness of the sun's cascaded shadows: the radius of their filter (m; at least a texel and a half of each map)");
  iConsole->CreateVariable("d3d12_Upscaler", "none", VF_DUMPTODISK,
                           "The upscaler of the 3D scene: none, or fsr (AMD FSR Upscaling); with d3d12_UpscaleMode 0 anti-aliasing only");
  iConsole->Register("d3d12_UpscaleMode", &CV_d3d12_UpscaleMode, 0, VF_DUMPTODISK,
                     "The upscaler's render size: 0 Native AA (the display's: anti-aliasing only), 1 Quality (1/1.5), 2 Balanced (1/1.7), "
                     "3 Performance (1/2), 4 Ultra Performance (1/3)");
  iConsole->Register("d3d12_UpscaleSharpness", &CV_d3d12_UpscaleSharpness, 0.5f, VF_DUMPTODISK, "The upscaler's sharpening, 0 (none) to 1");
  iConsole->Register("d3d12_UpscaleDebug", &CV_d3d12_UpscaleDebug, 0, 0, "1: the upscaler's debug view; 2: the depth it gets (near black, far white)");
  // The ambient occlusion (AmbientOcclusion.cpp).
  iConsole->CreateVariable("d3d12_AO", "none", VF_DUMPTODISK,
                           "Screen space ambient occlusion of the 3D scene: none, gtao (Intel XeGTAO) or cacao (AMD FidelityFX CACAO)");
  iConsole->Register("d3d12_AOQuality", &CV_d3d12_AOQuality, 2, VF_DUMPTODISK,
                     "The ambient occlusion's quality, 0 (low) to 3 (XeGTAO ultra, CACAO highest)");
  iConsole->Register("d3d12_AORadius", &CV_d3d12_AORadius, 1.0f, VF_DUMPTODISK, "The ambient occlusion's radius (m): how far a surface occludes");
  iConsole->Register("d3d12_AOStrength", &CV_d3d12_AOStrength, 1.0f, VF_DUMPTODISK,
                     "The ambient occlusion's strength: the exponent of the visibility (1 as the backend computes it, 0 none)");
  iConsole->Register("d3d12_AODenoise", &CV_d3d12_AODenoise, 2, VF_DUMPTODISK,
                     "The ambient occlusion's denoising: XeGTAO 0 (none) to 3 passes, CACAO 1 to 8 blur passes");
  iConsole->Register("d3d12_AOApply", &CV_d3d12_AOApply, 1, VF_DUMPTODISK,
                     "What the ambient occlusion darkens: 0 the whole picture, 1 the ambient light alone (not the sun's and the lamps' direct light)");
  iConsole->Register("d3d12_AOBakedShare", &CV_d3d12_AOBakedShare, 0.5f, VF_DUMPTODISK,
                     "With d3d12_AOApply 1, the ambient light's share of the light baked into the terrain and the vegetation where the frame "
                     "has no shadows on the terrain to take it from, 0 to 1");
  iConsole->Register("d3d12_AODebug", &CV_d3d12_AODebug, 0, 0,
                     "1: the ambient occlusion's factor in grey over the whole picture; with d3d12_AOApply 1, 2: the ambient light, 3: the light it takes away");
  // The scene for rays (RayTracing.cpp), on a device with DXR 1.1.
  iConsole->Register("d3d12_RTDebug", &CV_d3d12_RTDebug, 0, 0,
                     "The scene for rays seen from the camera, one ray per pixel over the whole picture: 1 its instances in colours, "
                     "2 the hits shaded on the right half (the picture on the left), 3 the distance to the hit (blue near, red far), "
                     "4 the mip level of the hit's texture (blue 0, red 8), 5 the kind of BLAS (static green, of the frame red, alpha tested "
                     "lighter), 6 the alpha tests along the ray, 7 the hits shaded over the whole picture");
  iConsole->Register("d3d12_RTDistance", &CV_d3d12_RTDistance, 150.0f, VF_DUMPTODISK, "The rays' length (m)");
  iConsole->Register("d3d12_RTMemory", &CV_d3d12_RTMemory, 384, VF_DUMPTODISK,
                     "The memory of the static geometry's acceleration structures (MB) beyond which the least recently used go");
  iConsole->Register("d3d12_RTMask", &CV_d3d12_RTMask, 31, 0,
                     "The kinds of geometry the rays see, bits: 1 static, 2 alpha tested, 4 changing every frame; a draw is seen when "
                     "the mask has all of its kinds (5: no alpha tested geometry)");
  iConsole->Register("d3d12_RTReflections", &CV_d3d12_RTReflections, 0, VF_DUMPTODISK,
                     "The water's reflection by rays: 0 off, 1 the rays in place of the screen space reflection, 2 both, the screen space "
                     "reflection where it finds the scene and the rays elsewhere (a device with DXR 1.1)");
  iConsole->Register("d3d12_RTShadows", &CV_d3d12_RTShadows, 1, VF_DUMPTODISK,
                     "1: the sun's shadow at the hits of the rays, by a ray towards the sun (the surfaces the sun's shadow falls on)");
  // The HDR pipeline (r_HDRRendering, Hdr.cpp).
  iConsole->Register("d3d12_ToneMap", &CV_d3d12_ToneMap, 1, VF_DUMPTODISK,
                     "With r_HDRRendering, the tone map: 0 clamp, 1 soft shoulder (the game's look up to d3d12_ToneMapKnee, then each channel rolls off to 1), 2 ACES fitted, 3 AgX");
  iConsole->Register("d3d12_ToneMapKnee", &CV_d3d12_ToneMapKnee, 0.85f, VF_DUMPTODISK,
                     "Where the soft shoulder of d3d12_ToneMap 1 starts: a display value, 0 to 0.99");
  iConsole->Register("d3d12_Exposure", &CV_d3d12_Exposure, 0.0f, VF_DUMPTODISK, "With r_HDRRendering, the exposure's compensation in EV (0: none)");
  iConsole->Register("d3d12_AutoExposure", &CV_d3d12_AutoExposure, 1, VF_DUMPTODISK,
                     "With r_HDRRendering, the exposure adapts to the scene (between d3d12_ExposureMin and Max); 0: d3d12_Exposure alone");
  iConsole->Register("d3d12_ExposureMin", &CV_d3d12_ExposureMin, 0.0f, VF_DUMPTODISK,
                     "The least EV of the automatic exposure (0, the default: the game's daylight; below it bright views get darker)");
  iConsole->Register("d3d12_ExposureMax", &CV_d3d12_ExposureMax, 1.5f, VF_DUMPTODISK, "The most EV of the automatic exposure (dark scenes made brighter)");
  iConsole->Register("d3d12_ExposureSpeed", &CV_d3d12_ExposureSpeed, 1.5f, VF_DUMPTODISK,
                     "The automatic exposure's rate of adaptation per second (twice that towards a brighter scene)");
  iConsole->Register("d3d12_Bloom", &CV_d3d12_Bloom, 0.5f, VF_DUMPTODISK, "With r_HDRRendering, the strength of the bloom (0: none)");
  iConsole->Register("d3d12_BloomThreshold", &CV_d3d12_BloomThreshold, 1.0f, VF_DUMPTODISK,
                     "The light above which the bloom starts (exposed, linear: 1 is the display's white)");
  iConsole->Register("d3d12_HDRDebug", &CV_d3d12_HDRDebug, 0, 0, "With r_HDRRendering: 1 the luminance in EV bands from middle grey (green), 2 the bloom alone");
  // Brighter than the game draws them in HDR (Hdr.cpp): factors of the light, 1 the game's.
  iConsole->Register("d3d12_HDRSun", &CV_d3d12_HDRSun, 8.0f, VF_DUMPTODISK, "With r_HDRRendering, the light of the sun's flare in its core times this (1: the game's)");
  iConsole->Register("d3d12_HDRSky", &CV_d3d12_HDRSky, 2.0f, VF_DUMPTODISK,
                     "With r_HDRRendering, the light of the sky towards the sun times up to this (1: the game's)");
  iConsole->Register("d3d12_HDRGlow", &CV_d3d12_HDRGlow, 3.0f, VF_DUMPTODISK,
                     "With r_HDRRendering, the light of what glows (in their cores: fire and the other additive particles, muzzle flashes and the other additive "
                     "materials, the coronas and flares of the lights, the glow maps of lamps and screens) times this (1: the game's)");
  // The HDR output to the display (HdrOutput.cpp).
  iConsole->Register("d3d12_HDROutput", &CV_d3d12_HDROutput, 0, VF_DUMPTODISK,
                     "With r_HDRRendering on a display in HDR mode (Windows: Use HDR), the output: 0 SDR, 1 HDR10, 2 scRGB. The scene's highlights "
                     "go up to the display's peak, the rest of the picture, the HUD and the menu stay at paper white as SDR content");
  iConsole->Register("d3d12_HDRPaperWhite", &CV_d3d12_HDRPaperWhite, 0.0f, VF_DUMPTODISK,
                     "With d3d12_HDROutput, the light of the frame's white in nits (0: Windows' SDR content brightness of the display)");
  iConsole->Register("d3d12_HDRPeak", &CV_d3d12_HDRPeak, 0.0f, VF_DUMPTODISK,
                     "With d3d12_HDROutput, the most light in nits, where the highlights end (0: the display's peak)");
  iConsole->Register("d3d12_HDROutputDebug", &CV_d3d12_HDROutputDebug, 0, 0,
                     "With d3d12_HDROutput: 1 the highlights (yellow to red at the peak; blue: drawn over the scene), 2 patches at paper white, "
                     "twice and four times that and at the peak");
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
  m_pDevice = std::make_unique<CD3D12Device>();
  m_pDescriptors = std::make_unique<CD3D12Descriptors>();
  m_pUpload = std::make_unique<CD3D12Upload>();
  m_pPipeline = std::make_unique<CD3D12Pipeline>();
  m_pCompute = std::make_unique<CD3D12Compute>();
  m_pRayScene = std::make_unique<CD3D12RayScene>();
  m_pTextures = std::make_unique<CTextureManager>();
  m_pShaders = std::make_unique<CShaderManager>();
  m_pScene = std::make_unique<CScene>();
  StartWaterTextures();
  if (!m_pDevice->Init(m_hWnd, m_nWidth, m_nHeight, nDebug)
    || !m_pDescriptors->Init(m_pDevice->GetDevice())
    || !m_pUpload->Init(m_pDevice->GetDevice(), nFrameRingSize)
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
  // them: no d3d12_RTReflections, which greys out the menu's list (video-ray-tracing.patch).
  if (!m_pRayScene->Init(m_pDevice->GetDevice()) && iConsole->GetCVar("d3d12_RTReflections"))
  {
    iConsole->UnregisterVariable("d3d12_RTReflections", true);
    CV_d3d12_RTReflections = 0;
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
  return CallWindowProcA(pRenderer->m_pfnGameWndProc, hWnd, nMsg, wParam, lParam);
}

void CD3D12Renderer::Release()
{
  CpuSampler::Update(0, 0, "XRenderD3D12_samples.txt");
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
  ReleaseEnvCubes();
  ReleaseSprites();
  ReleaseShadowMaps();
  ReleaseCascades();
  ReleaseScreenEffects();
  ReleaseFlares();
  ReleaseAO();
  ReleaseUpscaler();
  ReleaseHdr();
  ReleaseHdrOutput();
  m_pDetailTexture = nullptr;   // released with every texture
  m_pDefaultCube = nullptr;
  m_pBeachTexture = m_pBeachWetTexture = m_pBeachBumpTexture = nullptr;
  m_nWaveTexture = 0;
  m_nCausticsTexture = 0;
  m_SkyBoxes.clear();
  m_nSceneSkyBox = -1;
  if (m_pTextures)
    m_pTextures->Shutdown();
  if (m_pRayScene)
    m_pRayScene->Shutdown();
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
}

void CD3D12Renderer::PostLoad (void)
{
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
  // Shown only when swapping is on: the hidden frames of a level load are drawn but never seen.
  if (m_bSwapBuffers)
    DrawHdrOutput();
  m_pDevice->EndFrame(m_bSwapBuffers, m_bVSync || CV_r_vsync != 0);
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
  snprintf(sz, sizeof(sz), " total %.2f; time between frames %.2f ms", fTotal / n, m_nTimedFrames ? m_fFrameMs / m_nTimedFrames : 0.0);
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
  // The camera of the water reflection (the frame's mirrored in the water plane): nothing under the water.
  if (m_bReflectionPass)
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
  if (EnsureFrame())
    m_pDevice->ClearDepth(1.0f, 0, true, true);
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
