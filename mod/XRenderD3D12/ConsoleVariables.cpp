// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: ConsoleVariables.cpp
//	Description: The other console variables of a renderer: names, defaults
//	and flags as the original renderer registers them. Modules look renderer
//	variables up by name (IConsole::GetCVar) and do not all check for NULL
//	(r_VolumetricFog with a screen effect), so every one exists, even when
//	this renderer does not read it. The few it reads are looked up by name
//	too (IsVolumetricFogOn, IsScopeLensOn, IsHdrOn).
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"

namespace
{

struct SIntVar
{
  const char *szName;
  int nDefault;
  int nFlags;
};

struct SFloatVar
{
  const char *szName;
  float fDefault;
  int nFlags;
};

struct SStringVar
{
  const char *szName;
  const char *szDefault;
  int nFlags;
};

constexpr SIntVar s_IntVars[] =
{
  { "r_AccurateParticles", 1, 0 },
  { "r_Beams", 1, VF_DUMPTODISK },
  { "r_BumpSelfShadow", 0, VF_REQUIRE_LEVEL_RELOAD },
  { "r_Character_Debug", 0, 0 },
  { "r_Character_NoAnim", 0, 0 },
  { "r_Character_NoDeform", 0, 0 },
  { "r_Character_NoPhys", 0, 0 },
  { "r_Character_Shadow_Volume", 0, 0 },
  { "r_checkSunVis", 1, VF_DUMPTODISK },
  { "r_Coronas", 1, VF_DUMPTODISK },
  { "r_Cryvision", 0, 0 },
  { "r_CryvisionType", 1, VF_DUMPTODISK },
  { "r_CullByClipPlanes", 1, 0 },
  { "r_CullGeometryForLights", 0, 0 },
  { "r_DebugLights", 0, 0 },
  { "r_DebugScreenEffects", 0, 0 },
  { "r_DecalTextures", 1, 0 },
  { "r_DetailNumLayers", 1, VF_DUMPTODISK },
  { "r_DisplayInfo", 0, 0 },
  { "r_Dof", 0, 0 },
  { "r_DofFocalSource", 0, 0 },
  { "r_draw_phys_only", 0, 0 },
  { "r_EfMultiTex", 1, 0 },
  { "r_EnhanceImage", 1, VF_DUMPTODISK },
  { "r_EnvCMWrite", 0, 0 },
  { "r_EnvLightCMDebug", 0, 0 },
  { "r_EnvLightCMSize", 8, VF_DUMPTODISK },
  { "r_EnvLighting", 0, VF_DUMPTODISK },
  { "r_EnvTexResolution", 3, VF_DUMPTODISK },
  { "r_Flares", 1, VF_DUMPTODISK },
  { "r_FlashBangSize", 128, 0 },
  { "r_FlipScreen", 0, 0 },
  { "r_Flush", 1, 0 },
  { "r_FSAA", 0, VF_DUMPTODISK | VF_REQUIRE_APP_RESTART },
  { "r_FSAA_quality", 0, VF_DUMPTODISK | VF_REQUIRE_APP_RESTART },
  { "r_FSAA_samples", 4, VF_DUMPTODISK | VF_REQUIRE_APP_RESTART },
  { "r_FullBrightness", 0, VF_CHEAT },
  { "r_GeomInstancing", 1, 0 },
  { "r_GetScreenShot", 0, 0 },
  { "r_Glare", 0, VF_DUMPTODISK },
  { "r_GlareQuality", 2, VF_DUMPTODISK },
  { "r_GraphStyle", 0, 0 },
  { "r_HDRDebug", 0, 0 },
  { "r_HDRFake", 1, 0 },
  { "r_HDRRendering", 0, VF_DUMPTODISK },
  { "r_HeatHaze", 1, VF_DUMPTODISK },
  { "r_HeatMapMips", 0, 0 },
  { "r_HeatMapSave", 0, 0 },
  { "r_HeatSize", 256, 0 },
  { "r_HeatType", 1, 0 },
  { "r_HWLights", 1, VF_CHEAT },
  { "r_IndexingWithTangents", 1, VF_REQUIRE_LEVEL_RELOAD },
  { "r_LightSourcesAsHeatSources", 0, 0 },
  { "r_Log", 0, 0 },
  { "r_LogLoadShaders", 0, 0 },
  { "r_LogTexStreaming", 0, 0 },
  { "r_LogTextureUsage", 0, 0 },
  { "r_LogUsedShaders", 0, 0 },
  { "r_LogUsedTextures", 0, 0 },
  { "r_LogVBuffers", 0, 0 },
  { "r_MeasureOverdraw", 0, 0 },
  { "r_MotionBlur", 0, VF_DUMPTODISK },
  { "r_MotionBlurDisplace", 4, 0 },
  { "r_NightMapSave", 0, 0 },
  { "r_NightSize", 512, 0 },
  { "r_NightType", 1, 0 },
  { "r_NoBumpmap", 0, 0 },
  { "r_NoDrawNear", 0, 0 },
  { "r_NoDrawShaders", 0, 0 },
  { "r_NoHWGamma", 0, 0 },
  { "r_NoLightCalc", 0, 0 },
  { "r_NoLoadTextures", 0, 0 },
  { "r_NoParticles", 0, 0 },
  { "r_NoPreprocess", 0, 0 },
  { "r_NoPS20", 0, VF_REQUIRE_APP_RESTART },
  { "r_NoPS2X", 0, 0 },
  { "r_NoPS30", 0, 0 },
  { "r_NoSWGamma", 1, 0 },
  { "r_OceanHeightScale", 4, 0 },
  { "r_OceanLodDist", 180, 0 },
  { "r_OceanMaxSplashes", 8, 0 },
  { "r_OceanRendType", 0, 0 },
  { "r_OceanSectorSize", 128, 0 },
  { "r_OceanTexUpdate", 1, 0 },
  { "r_OffsetBump", 1, VF_REQUIRE_APP_RESTART },
  { "r_OffsetBumpForce", 0, VF_REQUIRE_APP_RESTART },
  { "r_PolygonMode", 1, VF_CHEAT },
  { "r_PortalsRecursive", 0, 0 },
  { "r_PrecacheMesh", 1, 0 },
  { "r_PrintMemoryLeaks", 0, 0 },
  { "r_ProcFlares", 1, VF_DUMPTODISK },
  { "r_ProfileShaders", 0, 0 },
  { "r_Quality_Reflection", 0, VF_DUMPTODISK },
  { "r_RainMapSize", 256, 0 },
  { "r_RB_Merge", 0, 0 },
  { "r_ReloadShaders", 0, 0 },
  { "r_RenderMode", 0, VF_DUMPTODISK },
  { "r_ReplaceCubemap", 1, 0 },
  { "r_ScopeLens_fx", 1, VF_DUMPTODISK },
  { "r_ScreenRefract", 0, 0 },
  { "r_SelfRefract", 0, 0 },
  { "r_SelfShadow", 0, VF_DUMPTODISK | VF_REQUIRE_LEVEL_RELOAD },
  { "r_ShaderDetailObjectsBending", 0, 0 },
  { "r_ShadersPrecache", 1, VF_DUMPTODISK },
  { "r_ShadersSave", 0, VF_DUMPTODISK },
  { "r_ShaderTerrainDOT3", 0, 0 },
  { "r_ShaderTerrainSpecular", 0, 0 },
  { "r_shadow_maps_debug", 0, 0 },
  { "r_ShadowBlur", 1, VF_DUMPTODISK },
  { "r_ShadowType", 0, VF_REQUIRE_APP_RESTART },
  { "r_ShowLines", 0, VF_CHEAT },
  { "r_ShowNormals", 0, VF_CHEAT },
  { "r_ShowTangents", 0, VF_CHEAT },
  { "r_ShowTexTimeGraph", 0, 0 },
  { "r_ShowTimeGraph", 0, 0 },
  { "r_ShowVideoMemoryStats", 0, 0 },
  { "r_SM2XPATH", 1, 0 },
  { "r_SM30PATH", 1, 0 },
  { "r_SmoothTangents", 1, VF_REQUIRE_LEVEL_RELOAD },
  { "r_SpecAntialias", 0, 0 },
  { "r_SSE", 1, 0 },
  { "r_Stats", 0, 0 },
  { "r_StripMesh", 1, 0 },
  { "r_SubSurfaceType", 1, 0 },
  { "r_SunStyleCoronas", 0, 0 },
  { "r_SupportCompressedTextures", 1, 0 },
  { "r_SupportPalettedTextures", 1, 0 },
  { "r_SyncVBuf", 1, 0 },
  { "r_TexBindMode", 0, 0 },
  { "r_TexBumpQuality", 0, 0 },
  { "r_TexForceSquare", 0, 0 },
  { "r_TexGrayOverage", 1, 0 },
  { "r_TexHWDXTCompression", 1, 0 },
  { "r_TexHWMipsGeneration", 1, 0 },
  { "r_TexLMResolution", 0, VF_DUMPTODISK },
  { "r_TexMaxSize", 0, 0 },
  { "r_TexMinSize", 64, 0 },
  { "r_TexNormalMapCompressed", 0, 0 },
  { "r_TexNormalMapType", 1, VF_REQUIRE_LEVEL_RELOAD },
  { "r_TexProcedures", 1, 0 },
  { "r_TexQuality", 0, VF_DUMPTODISK },
  { "r_TexQuantizeDither", 0, 0 },
  { "r_TexSimpleMips", 1, 0 },
  { "r_TexSkyQuality", 0, 0 },
  { "r_Texture_Anisotropic_Level", 1, VF_DUMPTODISK },
  { "r_TexturesBaseMip", 0, 0 },
  { "r_TexturesPixelSize", 32, 0 },
  { "r_TexturesStreaming", 0, VF_REQUIRE_APP_RESTART },
  { "r_TexturesStreamingOnlyVideo", 0, 0 },
  { "r_TexturesStreamingSync", 0, 0 },
  { "r_TexturesStreamPoolSize", 0, VF_DUMPTODISK },
  { "r_UnifyTangentNormals", 1, VF_REQUIRE_LEVEL_RELOAD },
  { "r_UseHWShaders", 2, VF_REQUIRE_APP_RESTART },
  { "r_Vegetation_IgnoreVertColors", 0, 0 },
  { "r_Vegetation_PerpixelLight", 1, VF_DUMPTODISK | VF_REQUIRE_APP_RESTART },
  { "r_VegetationSpritesAlphaBlend", 0, 0 },
  { "r_VegetationSpritesNoBend", 2, 0 },
  { "r_VolumetricFog", 2, VF_DUMPTODISK },
  { "r_VPFog", 1, VF_CHEAT },
  { "r_WaterBeachRefractions", 1, 0 },
};

constexpr SFloatVar s_FloatVars[] =
{
  { "r_Brightness", 0.5f, VF_DUMPTODISK },
  { "r_Character_LOD_Bias", 0.5f, 0 },
  { "r_Contrast", 0.5f, VF_DUMPTODISK },
  { "r_CoronaColorScale", 1.0f, 0 },
  { "r_CoronaFade", 0.125f, VF_DUMPTODISK },
  { "r_CoronaSizeScale", 1.0f, VF_DUMPTODISK },
  { "r_DofBlurAmount", 1.0f, 0 },
  { "r_DofFocalArea", 5.0f, 0 },
  { "r_DofFocalAreaCurr", 5.0f, 0 },
  { "r_DofFocalDist", 20.0f, 0 },
  { "r_DofFocalDist_entity", 20.0f, 0 },
  { "r_DofFocalDist_tag", 20.0f, 0 },
  { "r_EMBM", 1.0f, 0 },
  { "r_EnhanceImageAmount", 1.0f, VF_DUMPTODISK },
  { "r_EnvLCMupdateInterval", 0.1f, VF_DUMPTODISK },
  { "r_EnvTexUpdateInterval", 0.001f, VF_DUMPTODISK },
  { "r_FadeAmount", 1.0f, 0 },
  { "r_FlareSize", 1.0f, 0 },
  { "r_Gamma", 1.0f, VF_DUMPTODISK },
  { "r_GlareTransition", 10.0f, 0 },
  { "r_HDRBrightOffset", 6.0f, VF_DUMPTODISK },
  { "r_HDRBrightThreshold", 3.0f, VF_DUMPTODISK },
  { "r_HDRLevel", 0.6f, VF_DUMPTODISK },
  { "r_MaxTexLodBias", 0.0f, 0 },
  { "r_MotionBlurAmount", 0.9f, 0 },
  { "r_NormalsLength", 0.1f, 0 },
  { "r_OceanSplashScale", 1.0f, 0 },
  { "r_pp_Brightness", 0.0f, 0 },
  { "r_pp_CMYK_C", 0.0f, 0 },
  { "r_pp_CMYK_K", 0.0f, 0 },
  { "r_pp_CMYK_M", 0.0f, 0 },
  { "r_pp_CMYK_Y", 0.0f, 0 },
  { "r_pp_Contrast", 1.0f, 0 },
  { "r_pp_Gamma", 1.0f, 0 },
  { "r_pp_GlareIntensity", 1.0f, 0 },
  { "r_pp_Saturation", 1.0f, 0 },
  { "r_pp_SharpenAmount", 1.0f, 0 },
  { "r_Shininess", 15.0f, 0 },
  { "r_WaveScale", 1.0f, 0 },
};

constexpr SStringVar s_StringVars[] =
{
  { "r_ExcludeShader", "0", VF_CHEAT },
  { "r_GlossDefault", "Defaults/gloss", 0 },
  { "r_OpacityDefault", "Textures/white", 0 },
  { "r_ShaderDefault", "0", 0 },
  { "r_ShowLight", "0", 0 },
  { "r_ShowOnlyShader", "0", VF_CHEAT },
};

//! The bound values live as long as the DLL: the console may read them after IRenderer::Release
//! (e.g. when it writes system.cfg).
int s_IntValues[std::size(s_IntVars)];
float s_FloatValues[std::size(s_FloatVars)];

} // namespace

void CD3D12Renderer::RegisterOtherVariables()
{
  for (size_t i = 0; i < std::size(s_IntVars); i++)
    iConsole->Register(s_IntVars[i].szName, &s_IntValues[i], (float)s_IntVars[i].nDefault, s_IntVars[i].nFlags);
  for (size_t i = 0; i < std::size(s_FloatVars); i++)
    iConsole->Register(s_FloatVars[i].szName, &s_FloatValues[i], s_FloatVars[i].fDefault, s_FloatVars[i].nFlags);
  for (const SStringVar &v : s_StringVars)
    iConsole->CreateVariable(v.szName, v.szDefault, v.nFlags);
}

bool CD3D12Renderer::IsVolumetricFogOn()
{
  if (!m_pVolumetricFog && iConsole)
    m_pVolumetricFog = iConsole->GetCVar("r_VolumetricFog");
  return m_pVolumetricFog && m_pVolumetricFog->GetIVal() != 0;
}

bool CD3D12Renderer::IsScopeLensOn()
{
  if (!m_pScopeLens && iConsole)
    m_pScopeLens = iConsole->GetCVar("r_ScopeLens_fx");
  return m_pScopeLens && m_pScopeLens->GetIVal() != 0;
}

bool CD3D12Renderer::IsShadowMapsOn()
{
  if (!m_pShadowMapsVar && iConsole)
    m_pShadowMapsVar = iConsole->GetCVar("e_shadow_maps");
  return m_pShadowMapsVar && m_pShadowMapsVar->GetIVal() != 0;
}

bool CD3D12Renderer::IsHdrOn()
{
  if (!m_pHdrRendering && iConsole)
    m_pHdrRendering = iConsole->GetCVar("r_HDRRendering");
  return m_pHdrRendering && m_pHdrRendering->GetIVal() != 0;
}
