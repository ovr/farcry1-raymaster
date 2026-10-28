//////////////////////////////////////////////////////////////////////
//
//	File: Shader.cpp
//	Description: Shaders (IShader), shader resources and the shader manager.
//
//	The values the shaders give the engine and their draw classes come from
//	two tables: the engine's own shaders by name and the templates materials
//	name. The SDK structures of IShader.h that the SDK only declares
//	(SRenderShaderResources' constructor and destructor, SEfResTexture::Update,
//	the SLightMaterial functions) are defined here.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "Shader.h"
#include "Texture.h"

#include <cctype>
#include <charconv>
#include <string_view>

namespace
{

constexpr int kOpaque2 = EF2_OPAQUE | EF2_FOGOVERLAY1;
constexpr float kAlphaRef128 = 128.0f / 255.0f;
constexpr float kAlphaRef2 = 2.0f / 255.0f;
//! Two-sided material: MTLFLAG_2SIDED of the CGF material, copied into m_ResFlags by the SDK's
//! material loader (MakMatInfoFromMAT_ENTITY.h).
constexpr int kResFlag2Sided = 0x2;
//! Material made with the Cry shader flag (MTLFLAG_CRYSHADER): only then is m_AlphaRef its alpha
//! reference.
constexpr int kResFlagCryShader = 0x40;
//! The texture slots EF_LoadShaderItem loads; the others keep only their names.
constexpr int kLoadedSlots[] = { EFTT_DIFFUSE };

//! A template a material can name.
struct STemplateEntry
{
  std::string_view Name;          //!< lower case; a trailing '*' matches every name starting with the rest
  std::string_view Resolved;      //!< GetName of the template; empty: the name as given; ending in '(':
                                  //!< the generation mask in hex and ')' appended ("terrainlayer(7)")
  EF_Sort eSort;
  int nFlags2;
  int nFlags3;
  EDrawClass eClass;
  ECull eCull = eCULL_Back;
  float fAlphaRef = 0.0f;
  float fScrollV = 0.0f;
  bool bLightmap = false;
  bool bNotFound = false;         //!< has the values of a template that does not exist
  bool bStealth = false;
};

//! The templates. Exact names are matched before the '*' entries, which are tried in this order.
constexpr STemplateEntry s_Templates[] =
{
  // name                              template                         sort                      flags2                                  flags3          class                 cull        alpha ref     scroll v  lightmap
  // Diffuse: texture * lighting, or the lightmap stages of lightmapped objects.
  { "tbd",                             "templdiffuse_fp",               eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back, 0.0f,         0.0f,     true },
  { "templlightdiffuse",               "",                              eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back, 0.0f,         0.0f,     true },
  { "templlightdiffuse_alphaglow",     "templdiffuse_fp",               eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back, 0.0f,         0.0f,     true },
  // Vegetation: no lighting; the leaves alpha tested (or blended by the object's colour), two-sided.
  { "templplantsbark",                 "templdecal_vcolors",            eS_Trees,                 EF2_OPAQUE,                             0,              eDC_VColor,           eCULL_Back },
  // Kept as named, opaque.
  { "templdecal",                      "",                              eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back },
  { "templdecalalphatest",             "",                              eS_Opaque,                kOpaque2,                               0,              eDC_AlphaTest,        eCULL_Back, kAlphaRef128 },
  { "templdecalalphatestforshadows",   "",                              eS_Opaque,                kOpaque2,                               0,              eDC_AlphaTest,        eCULL_None, kAlphaRef128 },
  { "templdecalglow",                  "",                              eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back },
  { "templdecalglowselfillum",         "",                              eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back },
  { "templdecalglowselfillumanimated", "",                              eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back },
  { "templreflcm",                     "",                              eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back },
  { "templreflcmdecal",                "",                              eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back },
  { "templreflcmdecal_reflamountopacity", "",                           eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back },
  { "templreflcmdecaladdsig",          "",                              eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back },
  { "templtextureshiftt02",            "",                              eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back, 0.0f,         0.2f },
  { "templtextureshiftt05",            "",                              eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back, 0.0f,         0.5f },
  { "lavavolume",                      "",                              eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back },
  // Glass: drawn blended so that what is behind shows (alpha: the opacity).
  { "templglasscm",                    "templglasscm_fp",               eS_Opaque,                kOpaque2,                               0,              eDC_AlphaBlend,       eCULL_Back },
  { "templglasscm_fp",                 "",                              eS_Opaque,                kOpaque2,                               0,              eDC_AlphaBlend,       eCULL_Back },
  // Not drawn: a light pass of dynamic lights, decals the original draws nothing for.
  { "templinvlight",                   "",                              eS_Opaque,                EF2_FOGOVERLAY1 | EF2_DONTSORTBYDIST,   0,              eDC_NoDraw,           eCULL_Back },
  { "templdecalmodulate",              "templdecalmodulate_fp",         eS_Decal,                 EF2_DONTSORTBYDIST,                     0,              eDC_NoDraw,           eCULL_Back },
  { "templdecalalphablend",            "",                              eS_Decal,                 EF2_DONTSORTBYDIST,                     0,              eDC_NoDraw,           eCULL_Back },
  // Blended (no depth writes).
  { "templalphablend",                 "",                              eS_Banner,                0,                                      0,              eDC_AlphaBlend,       eCULL_Back },
  { "templdecalopacity",               "",                              eS_Banner,                0,                                      0,              eDC_AlphaBlend,       eCULL_Back },
  { "templdecalopacityshift2",         "templdecalopacityshift2_fp",    eS_Banner,                0,                                      0,              eDC_AlphaBlend,       eCULL_None, 0.0f,         -0.35f },
  { "templdecalopacityshift3",         "templdecalopacityshift3_fp",    eS_Banner,                0,                                      0,              eDC_AlphaBlend,       eCULL_None, 0.0f,         -0.35f },
  { "templtexturev22rotate",           "",                              eS_Banner,                0,                                      0,              eDC_AlphaBlend,       eCULL_None, kAlphaRef2 },
  { "s_objectcolor",                   "",                              eS_Banner,                0,                                      0,              eDC_AlphaBlend,       eCULL_Back },
  { "templbeamproc",                   "",                              eS_Banner,                EF2_IGNORERESOURCESTATES,               0,              eDC_AlphaBlend,       eCULL_None },
  // Added (ONE, ONE).
  { "templdecaladd",                   "",                              eS_Banner,                0,                                      0,              eDC_Additive,         eCULL_Back },
  { "templdecaladd_vlight",            "",                              eS_Banner,                0,                                      0,              eDC_Additive,         eCULL_Back },
  { "templdecalalphaadd",              "",                              eS_Banner,                0,                                      0,              eDC_Additive,         eCULL_Back },
  { "templhologram",                   "templhologram_fp",              eS_Banner,                0,                                      0,              eDC_Additive,         eCULL_None },
  { "templmuzzleflash_auto",           "templmuzzleflash_auto_fp",      eS_MuzzleFlash,           0,                                      0,              eDC_Additive,         eCULL_Back },
  { "mutantstealth",                   "mutantstealth_fp",              eS_Banner,                0,                                      0,              eDC_Additive,         eCULL_Back, 0.0f,         0.0f,     false, false, true },
  // The terrain layers with a material of their own (bump mapped cliffs).
  { "terrainlayer",                    "terrainlayer(",                 eS_TerrainDetailTextures, EF2_FOGOVERLAY1 | EF2_DONTSORTBYDIST,   0,              eDC_TerrainLayer,     eCULL_Back },
  // Flares: no draw for the objects.
  { "flareproc",                       "",                              eS_Banner,                0,                                      0,              eDC_NoDraw,           eCULL_Back },
  // Not drawn: invisible surfaces, heat haze.
  { "nodraw",                          "",                              eS_Opaque,                kOpaque2,                               EF3_NODRAW,     eDC_NoDraw,           eCULL_Back },
  { "no_draw",                         "nodraw",                        eS_Opaque,                kOpaque2,                               EF3_NODRAW,     eDC_NoDraw,           eCULL_Back },
  { "particleminheathaze",             "",                              eS_HeatVision,            0,                                      0,              eDC_NoDraw,           eCULL_None },
  { "particlemedheathaze",             "",                              eS_HeatVision,            0,                                      0,              eDC_NoDraw,           eCULL_None },
  { "particlemaxheathaze",             "",                              eS_HeatVision,            0,                                      0,              eDC_NoDraw,           eCULL_None },
  // The glass of the scopes (drawn with r_ScopeLens_fx).
  { "ag36scoperefraction",             "",                              eS_Refractive,            0,                                      0,              eDC_ScopeLens,        eCULL_Back },
  { "aw50scoperefraction",             "",                              eS_Refractive,            0,                                      0,              eDC_ScopeLens,        eCULL_Back },
  { "oicwscoperefraction",             "",                              eS_Refractive,            0,                                      0,              eDC_ScopeLens,        eCULL_Back },
  { "rlscoperefraction",               "",                              eS_Refractive,            0,                                      0,              eDC_ScopeLens,        eCULL_Back },

  // Prefixes, longest first.
  { "templbumpreflcm*",                "templreflcm",                   eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back },
  { "templbump*",                      "templdiffuse_fp",               eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back, 0.0f,         0.0f,     true },
  { "templmodel*",                     "templdiffuse_fp",               eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back, 0.0f,         0.0f,     true },
  { "templplants*",                    "templdecalalphatest_vcolors",   eS_Trees,                 EF2_OPAQUE,                             EF3_DEPTHWRITE, eDC_AlphaTestVColor,  eCULL_None, kAlphaRef128 },
  { "templmuzzleflash*",               "",                              eS_MuzzleFlash,           0,                                      0,              eDC_Additive,         eCULL_Back },
  { "templdecaladd*",                  "",                              eS_Banner,                0,                                      0,              eDC_Additive,         eCULL_Back },
  { "flareproc *",                     "",                              eS_Banner,                0,                                      0,              eDC_NoDraw,           eCULL_Back },
  { "flareproc_*",                     "",                              eS_Opaque,                kOpaque2,                               EF3_NOTEMPLATE, eDC_NoDraw,           eCULL_Back, 0.0f,         0.0f,     false, true },
  // Water volumes (lakes, rivers): drawn as water.
  { "watervolume*",                    "",                              eS_Water,                 0,                                      0,              eDC_Water,            eCULL_None },
};

//! The templates lit otherwise than by 2 (ELighting::Overbright), those that add their gloss map
//! and those that show their cube map. Looked up like s_Templates, by the name the material gives.
struct STemplateLighting
{
  std::string_view Name;
  ELighting eLighting;
  EGlow eGlow = EGlow::None;
  EEnvMap eEnvMap = EEnvMap::None;
};

constexpr STemplateLighting s_TemplateLighting[] =
{
  // Texture * fixed function vertex lighting (ambient and lights), not modulated by 2.
  { "templdecal",                      ELighting::Vertex },
  { "templdecalalphatest",             ELighting::Vertex },
  { "templdecalglow",                  ELighting::Vertex,   EGlow::Glow },
  { "templdecalglowselfillum",         ELighting::Vertex,   EGlow::SelfIllum },
  { "templdecalglowselfillumanimated", ELighting::Vertex,   EGlow::Pulse },
  { "templtextureshiftt02",            ELighting::Vertex },
  { "templtextureshiftt05",            ELighting::Vertex },
  // The cube map alone (its material's ambient is white: the light saturates), the cube map lit
  // plus the texture, the glass unlit.
  { "templreflcm",                     ELighting::None,     EGlow::None, EEnvMap::Cube },
  { "templbumpreflcm*",                ELighting::None,     EGlow::None, EEnvMap::Cube },
  { "templreflcmdecal",                ELighting::Vertex,   EGlow::None, EEnvMap::CubeDecal },
  { "templreflcmdecal_reflamountopacity", ELighting::Vertex, EGlow::None, EEnvMap::CubeDecal },
  { "templreflcmdecaladdsig",          ELighting::Vertex,   EGlow::None, EEnvMap::CubeAddSigned },
  { "templglasscm",                    ELighting::None,     EGlow::None, EEnvMap::Glass },
  { "templglasscm_fp",                 ELighting::None,     EGlow::None, EEnvMap::GlassFixed },
  // No lights: the texture times the object's ambient colour, or the texture alone (also added).
  { "templalphablend",                 ELighting::Ambient },
  { "templdecalopacity",               ELighting::Ambient },
  { "templdecalalphatestforshadows",   ELighting::Ambient2 },
  { "templdecaladd_vlight",            ELighting::Ambient },
  { "templdecalopacityshift2",         ELighting::None },
  { "templdecalopacityshift3",         ELighting::None },
  { "templtexturev22rotate",           ELighting::None },
};

//! An engine shader (EF_LoadShader by name).
struct SEngineEntry
{
  std::string_view Name;          //!< lower case
  std::string_view Resolved;      //!< GetName; empty: the name; names resolving alike share one shader
  EF_Sort eSort;
  int nFlags;                     //!< besides EF_COMPILEDLAYERS (and EF_SYSTEM when the engine passes it)
  int nFlags2;
  int nLFlags;
  EDrawClass eClass;
};

constexpr int kTerrainLayer2 = EF2_FOGOVERLAY1 | EF2_DONTSORTBYDIST;

constexpr SEngineEntry s_EngineShaders[] =
{
  // name                              shader                  sort                      flags          flags2               lflags            class
  { "default",                         "",                     eS_Opaque,                0,             kOpaque2,            0,                eDC_Opaque },
  // Stencil shadows, shadow maps and preprocess passes: not supported.
  { "<stencil>",                       "",                     eS_Stencil,               0,             0,                   0,                eDC_NoDraw },
  { "stencilstate",                    "",                     eS_Stencil,               0,             0,                   0,                eDC_NoDraw },
  { "stencilstateinv",                 "",                     eS_Stencil,               0,             0,                   0,                eDC_NoDraw },
  { "clearstencil",                    "",                     eS_Stencil,               0,             0,                   0,                eDC_NoDraw },
  { "shadowmapgen",                    "",                     eS_ShadowMap,             0,             0,                   0,                eDC_NoDraw },
  { "rainmap",                         "",                     eS_PreProcess,            0,             0,                   0,                eDC_NoDraw },
  { "outspace",                        "",                     eS_PreProcess,            0,             0,                   0,                eDC_NoDraw },
  { "bumpsunglow",                     "",                     eS_Glare,                 0,             0,                   0,                eDC_NoDraw },
  // Drawn by their render elements or the scene's own paths.
  { "occlusiontest",                   "",                     eS_OcclusionTest,         0,             0,                   0,                eDC_Engine },
  { "particlelight",                   "particlelight_fp",     eS_Particles,             0,             0,                   0,                eDC_Engine },
  { "fartreesprites",                  "",                     eS_Sprites,               0,             EF2_OPAQUE,          0,                eDC_Engine },
  { "terrainparticles",                "",                     eS_Particles,             0,             0,                   0,                eDC_Engine },
  { "screenprocess",                   "",                     eS_Glare,                 0,             0,                   0,                eDC_Engine },
  { "screentexmap",                    "",                     eS_Glare,                 0,             0,                   0,                eDC_Engine },
  { "binoculardistortmask",            "",                     eS_Glare,                 0,             0,                   0,                eDC_Engine },
  { "sniperdistortmask",               "",                     eS_Glare,                 0,             0,                   0,                eDC_Engine },
  { "screendistort",                   "",                     eS_Glare,                 0,             0,                   0,                eDC_NoDraw },
  // Lights and flares (coronas are off by default), fog layers.
  { "crylight",                        "",                     eS_Nearest,               0,             0,                   0,                eDC_NoDraw },
  { "foglayer",                        "",                     eS_FogShader,             EF_FOGSHADER,  0,                   0,                eDC_FogLayer },
  // Decals with an owner (only added with r_Quality_BumpMapping above 0): one shader for both.
  { "decal_vp",                        "decal_fp",             eS_Banner,                0,             EF2_DONTSORTBYDIST,  LMF_DISABLE,      eDC_Decal },
  { "decal_2d_vp",                     "decal_fp",             eS_Banner,                0,             EF2_DONTSORTBYDIST,  LMF_DISABLE,      eDC_Decal },
  // Terrain.
  { "terrain",                         "",                     eS_Terrain,               0,             kOpaque2,            LMF_BUMPMATERIAL, eDC_Terrain },
  { "terrainwithfog",                  "",                     eS_Terrain,               0,             kOpaque2,            LMF_BUMPMATERIAL, eDC_Terrain },
  { "terrainwithdefaultdetailtexture", "",                     eS_Terrain,               0,             kOpaque2,            LMF_BUMPMATERIAL, eDC_Terrain },
  { "terrainlowlod",                   "",                     eS_Terrain,               0,             kOpaque2,            LMF_BUMPMATERIAL, eDC_Terrain },
  { "terraindetaillayers",             "",                     eS_TerrainDetailTextures, 0,             kTerrainLayer2,      0,                eDC_NoDraw },
  { "terraindetailtexturelayers",      "",                     eS_TerrainDetailTextures, 0,             kTerrainLayer2,      0,                eDC_NoDraw },
  { "terrainlayer",                    "",                     eS_TerrainDetailTextures, 0,             kTerrainLayer2,      0,                eDC_TerrainLayer },
  { "terrainlightpass",                "",                     eS_TerrainLightPass,      0,             EF2_DONTSORTBYDIST,  0,                eDC_NoDraw },
  { "terrainshadowpass",               "",                     eS_TerrainShadowPass,     0,             EF2_DONTSORTBYDIST,  0,                eDC_TerrainShadow },
  { "terraindetailobjects",            "",                     eS_TerrainDetailObjects,  0,             0,                   0,                eDC_AlphaTestVColor },
  // Water.
  { "terrainlake",                     "",                     eS_Water,                 0,             0,                   LMF_BUMPMATERIAL, eDC_Water },
  { "terrainwater_onlysky",            "",                     eS_Water,                 0,             0,                   LMF_BUMPMATERIAL, eDC_Water },
  { "terrainwaterbeach",               "",                     eS_WaterBeach,            0,             0,                   LMF_BUMPMATERIAL, eDC_WaterBeach },
  { "terrainwaterbottomsimple",        "",                     eS_UnderWater,            0,             0,                   0,                eDC_WaterBottom },
};

template <class T, size_t N>
const T *FindEntry(const T (&Table)[N], std::string_view Name)
{
  for (const T &E : Table)
    if (E.Name == Name)
      return &E;
  for (const T &E : Table)
    if (E.Name.ends_with('*') && Name.starts_with(E.Name.substr(0, E.Name.size() - 1)))
      return &E;
  return nullptr;
}

std::string Lower(const char *sz)
{
  std::string Out(sz ? sz : "");
  for (char &c : Out)
    c = (char)tolower((unsigned char)c);
  return Out;
}

std::string Hex(uint64 n)
{
  char sz[24];
  snprintf(sz, sizeof(sz), "%llx", (unsigned long long)n);
  return sz;
}

//! A template name resolved through s_Templates.
struct SResolvedTemplate
{
  std::string Name;
  SShaderProps Props;
};

SResolvedTemplate ResolveTemplate(const std::string &Name, uint64 nMaskGen)
{
  SResolvedTemplate R;
  const STemplateEntry *pE = FindEntry(s_Templates, Name);
  if (!pE)
  {
    // A template that does not exist: its own template, opaque.
    R.Name = Name;
    R.Props.nFlags |= EF_NOTFOUND;
    R.Props.nFlags3 = EF3_NOTEMPLATE;
    R.Props.bLightmap = true;
    return R;
  }
  if (pE->Resolved.empty())
    R.Name = Name;
  else if (pE->Resolved.back() == '(')
    R.Name = std::string(pE->Resolved) + Hex(nMaskGen) + ")";
  else
    R.Name = pE->Resolved;
  R.Props.eSort = pE->eSort;
  R.Props.nFlags2 = pE->nFlags2;
  R.Props.nFlags3 = pE->nFlags3;
  R.Props.eDrawClass = pE->eClass;
  R.Props.eCull = pE->eCull;
  R.Props.fAlphaRef = pE->fAlphaRef;
  R.Props.fScrollV = pE->fScrollV;
  R.Props.bLightmap = pE->bLightmap;
  R.Props.bStealth = pE->bStealth;
  if (pE->bNotFound)
    R.Props.nFlags |= EF_NOTFOUND;
  if (const STemplateLighting *pL = FindEntry(s_TemplateLighting, Name))
  {
    R.Props.eLighting = pL->eLighting;
    R.Props.eGlow = pL->eGlow;
    R.Props.eEnvMap = pL->eEnvMap;
  }
  return R;
}

bool SameLightMaterial(const SLightMaterial &a, const SLightMaterial &b)
{
  return a.side == b.side && a.Front == b.Front && a.Back == b.Back && a.bNeverReplace == b.bNeverReplace &&
    strncmp(a.name, b.name, sizeof(a.name)) == 0;
}

//! "<a>##<first>_<last>(<t>)<b>" (as many '#' as digits): the frames <a><n><b> for n from <first>
//! to <last>, each shown for <t> seconds.
struct SAnimTexName
{
  std::string Prefix, Suffix;
  int nDigits = 0, nFirst = 0, nLast = 0;
  float fTime = 0.0f;
};

bool ParseAnimTexName(std::string_view Name, SAnimTexName &Out)
{
  const size_t nHash = Name.find('#');
  if (nHash == std::string_view::npos)
    return false;
  size_t p = nHash;
  while (p < Name.size() && Name[p] == '#')
    p++;
  Out.nDigits = (int)(p - nHash);
  const char *pEnd = Name.data() + Name.size();
  auto [p1, e1] = std::from_chars(Name.data() + p, pEnd, Out.nFirst);
  if (e1 != std::errc() || p1 == pEnd || *p1 != '_')
    return false;
  auto [p2, e2] = std::from_chars(p1 + 1, pEnd, Out.nLast);
  if (e2 != std::errc() || p2 == pEnd || *p2 != '(')
    return false;
  auto [p3, e3] = std::from_chars(p2 + 1, pEnd, Out.fTime);
  if (e3 != std::errc() || p3 == pEnd || *p3 != ')')
    return false;
  if (Out.nLast < Out.nFirst || Out.nLast - Out.nFirst >= 256)
    return false;
  Out.Prefix = std::string(Name.substr(0, nHash));
  Out.Suffix = std::string(p3 + 1, pEnd);
  return true;
}

float CurrentTime()
{
  return iTimer ? iTimer->GetCurrTime() : 0.0f;
}

bool ReadTextFile(ICryPak *pPak, const char *szPath, std::string &Text)
{
  FILE *f = pPak->FOpen(szPath, "rb");
  if (!f)
    return false;
  Text.clear();
  if (pPak->FSeek(f, 0, SEEK_END) == 0)
  {
    const long nSize = pPak->FTell(f);
    if (nSize > 0 && pPak->FSeek(f, 0, SEEK_SET) == 0)
    {
      Text.resize((size_t)nSize);
      Text.resize(pPak->FRead(Text.data(), 1, (size_t)nSize, f));
    }
  }
  pPak->FClose(f);
  return true;
}

std::string_view Trim(std::string_view s)
{
  while (!s.empty() && isspace((unsigned char)s.front()))
    s.remove_prefix(1);
  while (!s.empty() && isspace((unsigned char)s.back()))
    s.remove_suffix(1);
  return s;
}

bool StartsWithWord(std::string_view Line, std::string_view Word)
{
  if (Line.size() < Word.size() || _strnicmp(Line.data(), Word.data(), Word.size()) != 0)
    return false;
  return Line.size() == Word.size() || !isalnum((unsigned char)Line[Word.size()]);
}

//! The "SkyBox = <name>" lines of a shader script, under the name of the "Shader '<name>'" block
//! they are in (both lower case, '\' as '/'). ';' and "//" start comments.
void ParseSkyBoxes(std::string_view Text, std::unordered_map<std::string, std::string> &SkyBoxes)
{
  std::string Shader;
  size_t nPos = 0;
  while (nPos < Text.size())
  {
    size_t nEnd = Text.find('\n', nPos);
    if (nEnd == std::string_view::npos)
      nEnd = Text.size();
    std::string_view Line = Text.substr(nPos, nEnd - nPos);
    nPos = nEnd + 1;
    Line = Line.substr(0, std::min(Line.find("//"), Line.find(';')));
    Line = Trim(Line);
    if (StartsWithWord(Line, "Shader"))
    {
      const size_t q1 = Line.find('\'');
      const size_t q2 = q1 == std::string_view::npos ? q1 : Line.find('\'', q1 + 1);
      if (q2 != std::string_view::npos)
        Shader = Lower(std::string(Line.substr(q1 + 1, q2 - q1 - 1)).c_str());
    }
    else if (StartsWithWord(Line, "SkyBox") && !Shader.empty())
    {
      std::string_view Value = Line.substr(6);
      const size_t nEq = Value.find('=');
      if (nEq == std::string_view::npos)
        continue;
      Value = Trim(Value.substr(nEq + 1));
      Value = Value.substr(0, std::min(Value.find_first_of(" \t"), Value.size()));
      std::string Name = Lower(std::string(Value).c_str());
      std::replace(Name.begin(), Name.end(), '\\', '/');
      if (!Name.empty())
        SkyBoxes[Shader] = std::move(Name);
    }
  }
}

} // namespace

//////////////////////////////////////////////////////////////////////////
// CShader

CShader::CShader(CShaderManager *pManager, int nID, std::string Name, EShaderKind eKind, const SShaderProps &Props, CShader *pTemplate, uint64 nMaskGen)
  : m_pManager(pManager), m_nID(nID), m_Name(std::move(Name)), m_eKind(eKind), m_Props(Props), m_pTemplate(pTemplate), m_nMaskGen(nMaskGen)
{
}

void CShader::Release(bool bForce)
{
  if (m_nRefCount > 0)
    m_nRefCount--;
}

bool CShader::AddTemplate(SRenderShaderResources *Res, int &TemplId, const char *Name, bool bSetPreferred, uint64 nMaskGen)
{
  const int nID = m_pManager->AddUserTemplate(TemplId, Name, nMaskGen);
  if (nID < 0)
    return false;
  TemplId = nID;
  return true;
}

ITexPic *CShader::GetBaseTexture(int *nPass, int *nTU)
{
  if (nPass)
    *nPass = 0;
  if (nTU)
    *nTU = 0;
  return nullptr;
}

int CShader::GetVertexFormat(void)
{
  if (m_Props.eDrawClass == eDC_VColor || m_Props.eDrawClass == eDC_AlphaTestVColor)
    return VERTEX_FORMAT_P3F_COL4UB_TEX2F;
  return VERTEX_FORMAT_P3F_N_TEX2F;
}

int CShader::Size(int Flags)
{
  return (int)(sizeof(*this) + m_Name.capacity());
}

CShader *CShader::GetUserTemplate(int nTempl)
{
  CShader *pTempl = m_pManager->GetUserTemplate(nTempl);
  return pTempl ? pTempl : this;
}

//////////////////////////////////////////////////////////////////////////
// CShaderManager

CShaderManager::CShaderManager() = default;

CShaderManager::~CShaderManager()
{
  Shutdown();
}

bool CShaderManager::Init()
{
  GetDefault();
  return true;
}

void CShaderManager::Shutdown()
{
  m_ByName.clear();
  m_TemplateNames.clear();
  m_Materials.clear();
  m_MaterialCounts.clear();
  m_UserTemplates.clear();
  m_Shaders.clear();
  m_LightMaterials.clear();
  m_SkyBoxes.clear();
  m_bSkyBoxesRead = false;
  m_pDefault = nullptr;
  m_nNextUserTemplate = EFT_USER_FIRST;
  m_nGlobalTemplate = -2;
}

const std::string *CShaderManager::FindSkyBox(const char *szShader)
{
  if (!m_bSkyBoxesRead)
  {
    ICryPak *pPak = iSystem ? iSystem->GetIPak() : nullptr;
    if (!pPak)
      return nullptr;
    m_bSkyBoxesRead = true;
    const std::string Dir = "Shaders/Scripts/CryShaders/";
    std::vector<std::string> Files;
    _finddata_t fd;
    const intptr_t hFind = pPak->FindFirst((Dir + "*.csl").c_str(), &fd);
    if (hFind != -1)
    {
      do
      {
        if (!(fd.attrib & _A_SUBDIR))
          Files.push_back(Dir + fd.name);
      } while (pPak->FindNext(hFind, &fd) == 0);
      pPak->FindClose(hFind);
    }
    std::string Text;
    for (const std::string &File : Files)
      if (ReadTextFile(pPak, File.c_str(), Text))
        ParseSkyBoxes(Text, m_SkyBoxes);
  }
  const auto it = m_SkyBoxes.find(Lower(szShader));
  return it == m_SkyBoxes.end() ? nullptr : &it->second;
}

CShader *CShaderManager::GetDefault()
{
  if (!m_pDefault)
  {
    if (m_LightMaterials.empty())
      NewLightMaterial("default", nullptr);
    const SEngineEntry *pE = FindEntry(s_EngineShaders, "default");
    SShaderProps Props;
    Props.eSort = pE->eSort;
    Props.nFlags2 = pE->nFlags2;
    Props.eDrawClass = pE->eClass;
    Props.bLightmap = true;
    m_pDefault = Create("default", EShaderKind::Engine, Props, nullptr, 0);
    m_ByName.emplace("default", m_pDefault);
  }
  return m_pDefault;
}

CShader *CShaderManager::Create(std::string Name, EShaderKind eKind, const SShaderProps &Props, CShader *pTemplate, uint64 nMaskGen)
{
  const int nID = (int)m_Shaders.size();
  m_Shaders.push_back(std::make_unique<CShader>(this, nID, std::move(Name), eKind, Props, pTemplate, nMaskGen));
  return m_Shaders.back().get();
}

CShader *CShaderManager::LoadShader(const char *szName, EShClass eClass, int nFlags, uint64 nMaskGen)
{
  CShader *pSH = GetDefault();
  if (szName && szName[0])
  {
    std::string Name = Lower(szName);
    const SEngineEntry *pE = FindEntry(s_EngineShaders, Name);
    if (!pE && FindEntry(s_Templates, Name))
      pSH = GetTemplate(szName, nMaskGen);
    else
    {
      std::string Resolved = pE && !pE->Resolved.empty() ? std::string(pE->Resolved) : Name;
      if (auto it = m_ByName.find(Resolved); it != m_ByName.end())
        pSH = it->second;
      else
      {
        SShaderProps Props;
        if (pE)
        {
          Props.eSort = pE->eSort;
          Props.nFlags |= pE->nFlags;
          Props.nFlags2 = pE->nFlags2;
          Props.nLFlags = pE->nLFlags;
          Props.eDrawClass = pE->eClass;
        }
        else if (FindSkyBox(Name.c_str()))
        {
          // A sky shader of the scripts (I3DEngine::SetSkyBox): the level's CRESky element is
          // drawn with it as a sky box.
          Props.eSort = eS_Sky;
          Props.nFlags |= EF_SKY;
          Props.eDrawClass = eDC_Sky;
        }
        else
        {
          // Flares and lights, screen shaders, ...: nothing this renderer can draw from the name
          // alone.
          Props.eDrawClass = eDC_NoDraw;
        }
        Props.nFlags |= nFlags & EF_SYSTEM;
        pSH = Create(Resolved, EShaderKind::Engine, Props, nullptr, nMaskGen);
        m_ByName.emplace(std::move(Resolved), pSH);
      }
    }
  }
  pSH->AddRef();
  return pSH;
}

CShader *CShaderManager::GetTemplate(const char *szName, uint64 nMaskGen)
{
  std::string Name = Lower(szName);
  std::string Key = Name + '\n' + Hex(nMaskGen);
  if (auto it = m_TemplateNames.find(Key); it != m_TemplateNames.end())
    return it->second;

  SResolvedTemplate R = ResolveTemplate(Name, nMaskGen);
  CShader *pSH;
  if (auto it = m_ByName.find(R.Name); it != m_ByName.end())
    pSH = it->second;
  else
  {
    pSH = Create(R.Name, EShaderKind::Template, R.Props, nullptr, nMaskGen);
    m_ByName.emplace(R.Name, pSH);
  }
  m_TemplateNames.emplace(std::move(Key), pSH);
  return pSH;
}

CShader *CShaderManager::LoadMaterial(const char *szName, const char *szTemplate, uint64 nMaskGen)
{
  CShader *pTempl = GetTemplate(szTemplate, nMaskGen);
  std::string Name = Lower(szName);
  std::string Key = Name + '\n' + pTempl->GetName();
  if (auto it = m_Materials.find(Key); it != m_Materials.end())
    return it->second;

  // Another template for a name that has a shader already: "<name>_<n>".
  int &nMade = m_MaterialCounts[Name];
  std::string ShaderName = nMade ? Name + '_' + std::to_string(nMade) : Name;
  nMade++;

  SShaderProps Props = pTempl->GetProps();
  Props.nFlags = EF_COMPILEDLAYERS | EF_NEEDNORMALS | EF_NOTFOUND;
  // The templates with vertex and pixel shaders when bump mapping is on (their names stay then).
  Props.Template = Lower(szTemplate);
  const std::string &Templ = Props.Template;
  Props.bPixelLighting = Templ.starts_with("templbump") || Templ.starts_with("templmodel") || Templ.starts_with("templlightdiffuse");
  Props.bSpecular = Props.bPixelLighting && Templ.find("spec") != std::string::npos;
  Props.bGlossAlpha = Props.bSpecular && Templ.find("glossalpha") != std::string::npos;
  // Their bump mapping needs tangent frames: the tangent stream of a character is filled for the
  // materials that report EF_NEEDTANGENTS.
  if (Props.bPixelLighting && g_pRenderer && g_pRenderer->CV_r_Quality_BumpMapping > 0)
    Props.nFlags |= EF_NEEDTANGENTS;
  CShader *pSH = Create(std::move(ShaderName), EShaderKind::Material, Props, pTempl, nMaskGen);
  m_Materials.emplace(std::move(Key), pSH);
  return pSH;
}

SShaderItem CShaderManager::LoadShaderItem(const char *szName, EShClass eClass, bool bShare, const char *szTemplate, int nFlags, SInputShaderResources *pRes, uint64 nMaskGen)
{
  GetDefault();
  CShader *pSH;
  if (szTemplate && szTemplate[0])
  {
    pSH = LoadMaterial(szName, szTemplate, nMaskGen);
    pSH->AddRef();
  }
  else
    pSH = LoadShader(szName, eClass, nFlags, nMaskGen);

  SShaderItem Item;
  Item.m_pShader = pSH;
  if (pRes)
  {
    if (!pRes->m_LMaterial)
      pRes->m_LMaterial = GetDefaultLightMaterial();
    SRenderShaderResources *pRR = new SRenderShaderResources(pRes);
    pRR->m_Id = ++m_nLastResourceID;
    pRR->m_LMaterial = OwnLightMaterial(pRes->m_LMaterial);
    // Materials that are never drawn keep their texture names only (LoadResourceTexture loads them).
    if (pSH->GetDrawClass() != eDC_NoDraw)
      for (int nSlot : kLoadedSlots)
        LoadResourceTexture(pRR, nSlot);
    AddAttenuationTextures(pRR);
    Item.m_pShaderResources = pRR;
  }
  return Item;
}

CShader *CShaderManager::CopyShader(IShader *pSrcShader)
{
  CShader *pSrc = pSrcShader ? static_cast<CShader *>(pSrcShader) : GetDefault();
  CShader *pSH = Create(pSrc->GetName(), pSrc->GetKind(), pSrc->GetProps(), pSrc->GetSourceTemplate(), pSrc->GetGenerationMask());
  pSH->AddRef();
  return pSH;
}

bool CShaderManager::RegisterTemplate(int nTemplId, const char *szName, bool bReplace)
{
  if (nTemplId < 0 || !szName || !szName[0])
    return false;
  if (!bReplace && m_UserTemplates.contains(nTemplId))
    return false;
  m_UserTemplates[nTemplId] = GetTemplate(szName, 0);
  if (nTemplId >= m_nNextUserTemplate)
    m_nNextUserTemplate = nTemplId + 1;
  return true;
}

int CShaderManager::AddUserTemplate(int nTemplId, const char *szName, uint64 nMaskGen)
{
  CShader *pTempl = szName && szName[0] ? GetTemplate(szName, nMaskGen) : nullptr;
  if (nTemplId < 0)
  {
    if (!pTempl)
      return -1;
    nTemplId = m_nNextUserTemplate++;
  }
  else if (!pTempl)
    return m_UserTemplates.contains(nTemplId) ? nTemplId : -1;
  m_UserTemplates[nTemplId] = pTempl;
  if (nTemplId >= m_nNextUserTemplate)
    m_nNextUserTemplate = nTemplId + 1;
  return nTemplId;
}

CShader *CShaderManager::GetUserTemplate(int nTemplId) const
{
  auto it = m_UserTemplates.find(nTemplId);
  return it != m_UserTemplates.end() ? it->second : nullptr;
}

CShader *CShaderManager::GetByID(int nID) const
{
  if (nID < 0 || nID >= (int)m_Shaders.size())
    return nullptr;
  return m_Shaders[nID].get();
}

CShader *CShaderManager::Find(const char *szName) const
{
  std::string Name = Lower(szName);
  if (auto it = m_ByName.find(Name); it != m_ByName.end())
    return it->second;
  for (const auto &pSH : m_Shaders)
    if (Name == pSH->GetName())
      return pSH.get();
  return nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Resource textures

CTexture *CShaderManager::LoadResourceTexture(SRenderShaderResources *pRes, int nSlot)
{
  if (!pRes || nSlot < 0 || nSlot >= EFTT_MAX)
    return nullptr;
  SEfResTexture *pTex = pRes->m_Textures[nSlot];
  if (!pTex)
    return nullptr;
  SShaderTexUnit &TU = pTex->m_TU;
  if (!TU.m_ITexPic && !TU.m_AnimInfo && !pTex->m_Name.empty() && g_pRenderer && g_pRenderer->m_pTextures)
  {
    // The cube map slot names a face of the cube (<base>_posx), as a plain texture.
    if (nSlot == EFTT_CUBEMAP)
      TU.m_eTexType = eTT_Cubemap;
    const char *szPath = pRes->m_TexturePath.c_str();
    SAnimTexName Anim;
    if (ParseAnimTexName(pTex->m_Name.c_str(), Anim))
    {
      // The animation owns the frames; the unit's texture is the first one (SShaderTexUnit::mfFree
      // releases the animation's frames and not m_ITexPic).
      STexAnim *pAnim = new STexAnim;
      for (int n = Anim.nFirst; n <= Anim.nLast; n++)
      {
        char szNum[16];
        snprintf(szNum, sizeof(szNum), "%0*d", Anim.nDigits, n);
        std::string Frame = Anim.Prefix + szNum + Anim.Suffix;
        ITexPic *pFrame = LoadTextureFile(Frame.c_str(), szPath, TU);
        pAnim->m_TexPics.AddElem(reinterpret_cast<STexPic *>(pFrame));
      }
      pAnim->m_NumAnimTexs = pAnim->m_TexPics.Num();
      pAnim->m_bLoop = true;
      pAnim->m_Time = Anim.fTime;
      TU.m_AnimInfo = pAnim;
      TU.m_ITexPic = reinterpret_cast<ITexPic *>(pAnim->m_TexPics[0]);
    }
    else
      TU.m_ITexPic = LoadTextureFile(pTex->m_Name.c_str(), szPath, TU);
  }
  return GetResourceTexture(pRes, nSlot);
}

void CShaderManager::AddAttenuationTextures(SRenderShaderResources *pRes)
{
  // Every resource set gets the renderer's attenuation textures, which the engine reads:
  // EFTT_ATTENUATION2D the file "textures/defaults/pointlight2d.dds", EFTT_ATTENUATION1D one
  // without a name. Each unit holds a reference of its own (SShaderTexUnit::mfFree).
  CTextureManager *pTextures = g_pRenderer ? g_pRenderer->m_pTextures.get() : nullptr;
  if (!pTextures)
    return;
  if (!pRes->m_Textures[EFTT_ATTENUATION2D])
  {
    pRes->AddTextureMap(EFTT_ATTENUATION2D);
    SEfResTexture *pTex = pRes->m_Textures[EFTT_ATTENUATION2D];
    pTex->m_Name = "textures/defaults/pointlight2d.dds";
    pTex->m_TU.m_ITexPic = pTextures->LoadTexture(pTex->m_Name.c_str(), FT_CLAMP, 0, eTT_Base, -1.0f, -1.0f, -1, 0);
  }
  if (!pRes->m_Textures[EFTT_ATTENUATION1D])
  {
    pRes->AddTextureMap(EFTT_ATTENUATION1D);
    CTexture *pWhite = pTextures->GetWhite();
    if (pWhite)
      pWhite->AddRef();
    pRes->m_Textures[EFTT_ATTENUATION1D]->m_TU.m_ITexPic = pWhite;
  }
}

CTexture *CShaderManager::LoadTextureFile(const char *szName, const char *szPath, const SShaderTexUnit &TU)
{
  CTextureManager *pTextures = g_pRenderer->m_pTextures.get();
  const uint nFlags = TU.GetTexFlags();
  const byte eTT = TU.m_eTexType;
  auto Load = [&](const char *sz) { return pTextures->LoadTexture(sz, nFlags, 0, eTT, -1.0f, -1.0f, -1, 0); };
  auto Found = [](CTexture *p) { return p && !(p->GetFlags() & FT_NOTFOUND) && p->IsTextureLoaded(); };

  CTexture *pFirst = Load(szName);
  if (Found(pFirst) || !szPath || !szPath[0])
    return pFirst;

  // Names with the folders of the machine the material was made on: the material's folder with the
  // whole name, then with the file name.
  const char *szFile = szName;
  for (const char *s = szName; *s; s++)
    if (*s == '/' || *s == '\\' || *s == ':')
      szFile = s + 1;
  std::string Path(szPath);
  if (Path.back() != '/' && Path.back() != '\\')
    Path += '/';
  const std::string Tries[2] = { Path + szName, Path + szFile };
  for (int i = 0; i < 2; i++)
  {
    if (Tries[i] == szName || (i == 1 && Tries[1] == Tries[0]))
      continue;
    CTexture *pTex = Load(Tries[i].c_str());
    if (Found(pTex))
    {
      pFirst->Release();
      return pTex;
    }
    if (pTex)
      pTex->Release();
  }
  return pFirst;
}

//////////////////////////////////////////////////////////////////////////
// Light materials

SLightMaterial *CShaderManager::NewLightMaterial(const char *szName, const SLightMaterial *pValues)
{
  auto pLM = std::make_unique<SLightMaterial>();
  if (pValues)
    *pLM = *pValues;
  else
    pLM->side = SLightMaterial::FRONT;
  strncpy(pLM->name, szName ? szName : "", sizeof(pLM->name) - 1);
  pLM->name[sizeof(pLM->name) - 1] = 0;
  pLM->Id = (int)m_LightMaterials.size();
  pLM->m_nRefCounter = 1;
  m_LightMaterials.push_back(std::move(pLM));
  return m_LightMaterials.back().get();
}

SLightMaterial *CShaderManager::FindLightMaterial(const char *szName) const
{
  if (!szName)
    return nullptr;
  for (const auto &pLM : m_LightMaterials)
    if (!_strnicmp(pLM->name, szName, sizeof(pLM->name)))
      return pLM.get();
  return nullptr;
}

SLightMaterial *CShaderManager::GetLightMaterial(const char *szName)
{
  if (SLightMaterial *pLM = FindLightMaterial(szName))
    return pLM;
  return NewLightMaterial(szName, nullptr);
}

SLightMaterial *CShaderManager::AddLightMaterial(const char *szName, const SLightMaterial *pCompare)
{
  if (!pCompare)
    return GetLightMaterial(szName);
  for (const auto &pLM : m_LightMaterials)
    if (pLM->side == pCompare->side && pLM->Front == pCompare->Front && pLM->Back == pCompare->Back &&
        !_strnicmp(pLM->name, szName ? szName : "", sizeof(pLM->name)))
      return pLM.get();
  return NewLightMaterial(szName, pCompare);
}

SLightMaterial *CShaderManager::OwnLightMaterial(const SLightMaterial *pLM)
{
  if (!pLM)
    return GetDefaultLightMaterial();
  for (const auto &pOwn : m_LightMaterials)
    if (pOwn.get() == pLM)
      return pOwn.get();
  for (const auto &pOwn : m_LightMaterials)
    if (SameLightMaterial(*pOwn, *pLM))
      return pOwn.get();
  char szName[sizeof(pLM->name)];
  strncpy(szName, pLM->name, sizeof(szName) - 1);
  szName[sizeof(szName) - 1] = 0;
  return NewLightMaterial(szName, pLM);
}

//////////////////////////////////////////////////////////////////////////
// Drawing

EDrawClass GetDrawClass(const CShader *pShader, const SRenderShaderResources *pRes)
{
  if (!pShader)
    return eDC_NoDraw;
  const SShaderProps &Props = pShader->GetProps();
  const EDrawClass eClass = Props.eDrawClass;
  if (pRes && pRes->m_Opacity != 1.0f && Props.eSort <= eS_Opaque && (eClass == eDC_Opaque || eClass == eDC_AlphaTest))
    return eDC_AlphaBlend;
  return eClass;
}

bool IsBlendedVColor(EDrawClass eDC, const SRenderShaderResources *pRes, const CCObject *pObj)
{
  if (eDC != eDC_AlphaTestVColor && eDC != eDC_VColor)
    return false;
  return GetOpacity(pRes) < 1.0f || (eDC == eDC_AlphaTestVColor && pObj && pObj->m_Color.a < 1.0f);
}

CTexture *GetResourceTexture(const SRenderShaderResources *pRes, int nSlot)
{
  if (!pRes || nSlot < 0 || nSlot >= EFTT_MAX || !pRes->m_Textures[nSlot])
    return nullptr;
  const SShaderTexUnit &TU = pRes->m_Textures[nSlot]->m_TU;
  ITexPic *pTP = TU.m_ITexPic;
  const STexAnim *pAnim = TU.m_AnimInfo;
  if (pAnim && pAnim->m_TexPics.Num() > 1 && pAnim->m_Time > 0.0f)
  {
    const int nFrames = pAnim->m_TexPics.Num();
    int n = (int)(CurrentTime() / pAnim->m_Time) % nFrames;
    if (n < 0)
      n += nFrames;
    pTP = reinterpret_cast<ITexPic *>(pAnim->m_TexPics[n]);
  }
  return static_cast<CTexture *>(pTP);
}

CTexture *GetDiffuseTexture(const SRenderShaderResources *pRes)
{
  return GetResourceTexture(pRes, EFTT_DIFFUSE);
}

float GetOpacity(const SRenderShaderResources *pRes)
{
  if (!pRes)
    return 1.0f;
  return std::clamp(pRes->m_Opacity, 0.0f, 1.0f);
}

float GetAlphaRef(const CShader *pShader, const SRenderShaderResources *pRes)
{
  if (pRes && (pRes->m_ResFlags & kResFlagCryShader) && pRes->m_AlphaRef > 0)
    return floorf(std::min(pRes->m_AlphaRef, 1.0f) * 255.0f) / 255.0f;
  return pShader ? pShader->GetProps().fAlphaRef : 0.0f;
}

ECull GetCullMode(const CShader *pShader, const SRenderShaderResources *pRes)
{
  if (pRes && (pRes->m_ResFlags & kResFlag2Sided))
    return eCULL_None;
  return pShader ? pShader->GetProps().eCull : eCULL_Back;
}

CFColor GetMaterialColor(const SRenderShaderResources *pRes)
{
  if (pRes && pRes->m_LMaterial)
    return pRes->m_LMaterial->Front.m_Diffuse;
  return CFColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void GetTexTransform(const CShader *pShader, const SRenderShaderResources *pRes, int nSlot, float fScale[2], float fOffset[2])
{
  fScale[0] = fScale[1] = 1.0f;
  fOffset[0] = fOffset[1] = 0.0f;
  if (pRes && nSlot >= 0 && nSlot < EFTT_MAX && pRes->m_Textures[nSlot])
  {
    const SEfTexModificator &Mod = pRes->m_Textures[nSlot]->m_TexModificator;
    for (int i = 0; i < 2; i++)
    {
      if (Mod.m_Tiling[i] != 0.0f)
        fScale[i] = Mod.m_Tiling[i];
      fOffset[i] = Mod.m_Offs[i];
    }
  }
  if (pShader && pShader->GetProps().fScrollV != 0.0f)
    fOffset[1] += fmodf(pShader->GetProps().fScrollV * CurrentTime(), 1.0f);
}

float GetColorScale(const CShader *pShader, const CCObject *pObj)
{
  if (!pShader || !pShader->GetProps().bStealth)
    return 1.0f;
  int nFactor = 12;
  if (pObj && pObj->m_ShaderParams)
  {
    TArray<SShaderParam> &Params = *pObj->m_ShaderParams;
    for (int i = 0; i < Params.Num(); i++)
    {
      const SShaderParam &P = Params[i];
      if (_strnicmp(P.m_Name, "refraction", sizeof(P.m_Name)))
        continue;
      if (P.m_Type == eType_FLOAT)
        nFactor = (int)(P.m_Value.m_Float * 255.0f);
      else if (P.m_Type == eType_INT)
        nFactor = P.m_Value.m_Int * 255;
      break;
    }
  }
  return 2.0f * std::clamp(nFactor, 0, 255) / 255.0f;
}

//////////////////////////////////////////////////////////////////////////
// SDK structures declared in IShader.h

SRenderShaderResources::SRenderShaderResources(SInputShaderResources *pSrc)
{
  Reset();
  m_Id = 0;
  m_nRefCounter = 1;
  m_nCheckedTemplates = 0;
  m_bNeedNormals = false;
  m_nLastTexture = 0;
  m_nFrameLoad = 0;
  m_fMinDistanceLoad = 0.0f;
  if (!pSrc)
    return;

  m_ResFlags = pSrc->m_ResFlags;
  m_Opacity = pSrc->m_Opacity;
  m_AlphaRef = pSrc->m_AlphaRef;
  m_LMaterial = pSrc->m_LMaterial;
  // As SBaseShaderResources::operator= does: the array is copied, its strings are shared.
  m_ShaderParams.Copy(pSrc->m_ShaderParams);
  m_TexturePath = pSrc->m_TexturePath;
  for (int i = 0; i < EFTT_MAX; i++)
  {
    const SEfResTexture &Src = pSrc->m_Textures[i];
    if (Src.m_Name.empty() && !Src.m_TU.m_ITexPic && !Src.m_TU.m_AnimInfo)
      continue;
    AddTextureMap(i);
    // Name, flags, modificator and unit; a texture the unit already holds gets a reference more.
    *m_Textures[i] = Src;
  }
  PostLoad();
}

SRenderShaderResources::~SRenderShaderResources()
{
  // Each texture's unit releases its texture (SShaderTexUnit::mfFree). The light material belongs
  // to the shader manager.
  for (SEfResTexture *&pTex : m_Textures)
  {
    delete pTex;
    pTex = nullptr;
  }
}

void SEfResTexture::Update(int nTU)
{
  // Nothing to prepare: the draw code reads the modificator (GetTexTransform).
}

void SLightMaterial::mfApply(int Flags)
{
  // The draw code reads the light material (GetMaterialColor); there is no device state to set.
}

void SLightMaterial::Release()
{
  // The shader manager owns every light material until Shutdown.
  if (m_nRefCounter > 0)
    m_nRefCounter--;
}

SLightMaterial *SLightMaterial::mfAdd(char *name, SLightMaterial *Compare)
{
  if (!g_pRenderer || !g_pRenderer->m_pShaders)
    return nullptr;
  return g_pRenderer->m_pShaders->AddLightMaterial(name, Compare);
}

SLightMaterial *SLightMaterial::mfGet(char *name)
{
  if (!g_pRenderer || !g_pRenderer->m_pShaders)
    return nullptr;
  return g_pRenderer->m_pShaders->FindLightMaterial(name);
}
