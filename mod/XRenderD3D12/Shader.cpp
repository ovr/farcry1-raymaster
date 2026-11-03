// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
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
#include "Scene.h"
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
//! The texture slots EF_LoadShaderItem loads for every material that is drawn; of the others it
//! loads those the material's shader draws with (LoadDrawnTextures), the rest keep their names.
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
  float fFlareSize = 0.0f;        //!< eDC_ProcFlare: its FlareSize when the name has no number
};

//! The game's shader aliases (Shaders/Aliases.txt): a template named by an alias is the template it
//! stands for, as if the material named that (lower case).
struct SAlias
{
  std::string_view Name, Template;
};

constexpr SAlias s_Aliases[] =
{
  { "tbumpspec",                       "templbumpspec" },
  { "tbumpspec_nocm",                  "templbumpspec_nocm" },
  { "tbumpdiffuse_nocm",               "templbumpdiffuse_nocm" },
  { "tbd",                             "templbumpdiffuse" },
  { "tbs_hp",                          "templbumpspec_hp" },
  { "tbd_nocm",                        "templbumpdiffuse_nocm" },
  { "no_draw",                         "nodraw" },
  { "tbs_ga_cm",                       "templbumpspec_glossalpha_envcmamb" },
};

//! The template a lower case name stands for: its alias's, or the name itself.
std::string Unalias(std::string Name)
{
  for (const SAlias &A : s_Aliases)
    if (A.Name == Name)
      return std::string(A.Template);
  return Name;
}

//! The templates. Exact names are matched before the '*' entries, which are tried in this order.
constexpr STemplateEntry s_Templates[] =
{
  // name                              template                         sort                      flags2                                  flags3          class                 cull        alpha ref     scroll v  lightmap
  // Diffuse: texture * lighting, or the lightmap stages of lightmapped objects (TBD is an alias of
  // TemplBumpDiffuse: the templbump* entry below). TemplDiffuseSpec_FP adds the specular of projected
  // lights only.
  { "templlightdiffuse",               "",                              eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back, 0.0f,         0.0f,     true },
  { "templlightdiffuse_alphaglow",     "templdiffuse_fp",               eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back, 0.0f,         0.0f,     true },
  { "templdiffusespec_fp",             "",                              eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back, 0.0f,         0.0f,     true },
  // TemplDecalLight_SpecAdd: the texture plus its gloss map, lit together, with the highlights (s_TemplateLighting).
  { "templdecallight_specadd",         "",                              eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back },
  // Shaders of their own (s_TemplateFx; TemplateFx.cpp): the rainbow and the cartoon shading opaque, the membrane
  // added, the fur's shells, the wings of iridescence and the bubble blended. The fur and the iridescence are light
  // passes the original draws nothing of (its fur's vertex program outside its passes, the wing's not drawn either):
  // drawn as their scripts mean them, lit by the sun.
  { "templrainbow",                    "",                              eS_Opaque,                kOpaque2,                               0,              eDC_ShaderFx,         eCULL_Back },
  { "templcartoonshading",             "",                              eS_Opaque,                kOpaque2,                               0,              eDC_ShaderFx,         eCULL_Back },
  { "templmembrane",                   "",                              eS_Banner,                0,                                      0,              eDC_ShaderFxBlend,    eCULL_Back },
  { "templfurrendering",               "",                              eS_Banner,                0,                                      0,              eDC_ShaderFxBlend,    eCULL_Back },
  { "templiridescence_ps20",           "",                              eS_Banner,                0,                                      0,              eDC_ShaderFxBlend,    eCULL_Back },
  { "templbubble_ps20",                "",                              eS_Banner,                0,                                      0,              eDC_ShaderFxBlend,    eCULL_Back },
  // Vegetation: no lighting; the leaves alpha tested (or blended by the object's colour), two-sided.
  // TemplDecal_VColors (the bark's, also named by a model): the texture times the object's colour.
  { "templplantsbark",                 "templdecal_vcolors",            eS_Trees,                 EF2_OPAQUE,                             0,              eDC_VColor,           eCULL_Back },
  { "templdecal_vcolors",              "",                              eS_Trees,                 EF2_OPAQUE,                             0,              eDC_VColor,           eCULL_Back },
  // Kept as named, opaque (TemplDecal_HP, a template of the renderer's own as the _HP bump ones, the
  // original draws as TemplDecal).
  { "templdecal",                      "",                              eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back },
  { "templdecal_hp",                   "templdecal",                    eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back },
  { "templdecalalphatest",             "",                              eS_Opaque,                kOpaque2,                               0,              eDC_AlphaTest,        eCULL_Back, kAlphaRef128 },
  { "templdecalalphatestforshadows",   "",                              eS_Opaque,                kOpaque2,                               0,              eDC_AlphaTest,        eCULL_None, kAlphaRef128 },
  { "templdecalglow",                  "",                              eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back },
  { "templdecalglowselfillum",         "",                              eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back },
  { "templdecalglowselfillumanimated", "",                              eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back },
  { "templdecalglowselfillumanimatedslow", "",                          eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back },
  { "templreflcm",                     "",                              eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back },
  { "templreflcmdecal",                "",                              eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back },
  { "templreflcmdecal_reflamountopacity", "",                           eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back },
  { "templreflcmdecaladdsig",          "",                              eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back },
  { "templreflcmdecal_reflamountalpha", "",                             eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back },
  { "templreflcmdecalmod",             "",                              eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back },
  // The refraction and glass templates of vertex and pixel shaders (s_TemplateLighting): opaque in
  // the original (its blend off). Exact, ahead of the prefix templbump*.
  { "templrefrcm",                     "",                              eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back },
  { "templbumprefrcm",                 "",                              eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back },
  { "templglasscm_ps20",               "",                              eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back },
  { "templglassdispersioncm",          "",                              eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back },
  { "templbumpglasscm",                "",                              eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back },
  { "templbumpglassenvcm",             "",                              eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back },
  { "templbumpglassfresnelcm",         "",                              eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back },
  { "templbumpglassfresnelenvcm",      "",                              eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back },
  { "templreflenvcm",                  "",                              eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back },
  { "templbumpreflenvcm",              "templreflenvcm",                eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back },
  { "templglassdispersionenvcm",       "templreflenvcm",                eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back },
  { "templtextureshiftt02",            "",                              eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back, 0.0f,         0.2f },
  { "templtextureshiftt05",            "",                              eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back, 0.0f,         0.5f },
  // The other texture shifts (s_TemplateMotion). TemplDeform's vertex wave (DeformVertexes, Div 100)
  // moves nothing that shows in the original's frames: drawn as it stands.
  { "templtextureshifts05",            "",                              eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back },
  { "templtextureshiftst05",           "",                              eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back },
  { "templtextureshiftst_2t",          "",                              eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back },
  { "templtextureshiftst_2t_alphablend", "",                            eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back },
  { "templdeform",                     "",                              eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back },
  // LavaVolume (the lava of Volcano): a shader of its own (TemplateFx.cpp), opaque and unlit.
  { "lavavolume",                      "",                              eS_Opaque,                kOpaque2,                               0,              eDC_ShaderFx,         eCULL_Back },
  // Glass: drawn blended so that what is behind shows (alpha: the opacity).
  { "templglasscm",                    "templglasscm_fp",               eS_Opaque,                kOpaque2,                               0,              eDC_AlphaBlend,       eCULL_Back },
  { "templglasscm_fp",                 "",                              eS_Opaque,                kOpaque2,                               0,              eDC_AlphaBlend,       eCULL_Back },
  { "templglassenvcm",                 "",                              eS_Opaque,                kOpaque2,                               0,              eDC_AlphaBlend,       eCULL_Back },
  // Not drawn: a light pass of dynamic lights.
  { "templinvlight",                   "",                              eS_Opaque,                EF2_FOGOVERLAY1 | EF2_DONTSORTBYDIST,   0,              eDC_NoDraw,           eCULL_Back },
  // TemplRefrEnvCM: the texture alone, opaque (its pixel shader's cube weight is 0; the swatch shows
  // the red "Replace Me" of a missing texture). TemplBumpRefrEnvCM is an alias by name, but draws the
  // bump refraction of the scene's cube (s_TemplateLighting).
  { "templrefrenvcm",                  "",                              eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back },
  { "templbumprefrenvcm",              "templrefrenvcm",                eS_Opaque,                kOpaque2,                               0,              eDC_Opaque,           eCULL_Back },
  // Decals over surfaces, after the opaque ones: multiplied in, blended.
  { "templdecalmodulate",              "templdecalmodulate_fp",         eS_Decal,                 EF2_DONTSORTBYDIST,                     0,              eDC_Modulate,         eCULL_Back },
  { "templdecalalphablend",            "",                              eS_Decal,                 EF2_DONTSORTBYDIST,                     0,              eDC_AlphaBlend,       eCULL_Back },
  // Blended (no depth writes). TemplDecalAlphaBlend_NoSort: lit, among the blended objects, no decal
  // offset. The opacity shifts (waterfalls): the texture times the vertex colours, its v running
  // down, both faces, not the material's opacity (bNoOpacity).
  { "templalphablend",                 "",                              eS_Banner,                0,                                      0,              eDC_AlphaBlend,       eCULL_Back },
  { "templdecalalphablend_nosort",     "",                              eS_Banner,                0,                                      0,              eDC_AlphaBlend,       eCULL_Back },
  { "templtexturerotate",              "",                              eS_Banner,                0,                                      0,              eDC_AlphaBlend,       eCULL_None },
  { "communication_island_river",      "",                              eS_Banner,                0,                                      0,              eDC_AlphaBlend,       eCULL_Back },
  { "templdecalopacity",               "",                              eS_Banner,                0,                                      0,              eDC_AlphaBlend,       eCULL_Back },
  { "templdecalopacityshift",          "templdecalopacityshift_fp",     eS_Banner,                0,                                      0,              eDC_AlphaBlend,       eCULL_None, 0.0f,         -0.35f },
  { "templdecalopacityshift1",         "templdecalopacityshift1_fp",    eS_Banner,                0,                                      0,              eDC_AlphaBlend,       eCULL_None, 0.0f,         -0.35f },
  { "templdecalopacityshift2",         "templdecalopacityshift2_fp",    eS_Banner,                0,                                      0,              eDC_AlphaBlend,       eCULL_None, 0.0f,         -0.35f },
  { "templdecalopacityshift3",         "templdecalopacityshift3_fp",    eS_Banner,                0,                                      0,              eDC_AlphaBlend,       eCULL_None, 0.0f,         -0.35f },
  { "templdecalopacityshift4",         "templdecalopacityshift4_fp",    eS_Banner,                0,                                      0,              eDC_AlphaBlend,       eCULL_None, 0.0f,         -0.65f },
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
  // Procedural flares: a glow around the surface (FlareSize 1, or the number after the name; two of
  // the scripts' named ones have their own).
  { "flareproc",                       "",                              eS_Banner,                0,                                      0,              eDC_ProcFlare,        eCULL_Back },
  { "flareproc_warmyellow2",           "",                              eS_Banner,                0,                                      0,              eDC_ProcFlare,        eCULL_Back, 0.0f,         0.0f,     false, false, false, 0.025f },
  { "flareproc_025",                   "",                              eS_Banner,                0,                                      0,              eDC_ProcFlare,        eCULL_Back, 0.0f,         0.0f,     false, false, false, 0.25f },
  // Not drawn: invisible surfaces (no_draw is an alias), heat haze.
  { "nodraw",                          "",                              eS_Opaque,                kOpaque2,                               EF3_NODRAW,     eDC_NoDraw,           eCULL_Back },
  // Not drawn: the names of the game's models (and a swatch) that no script defines (typos), which the
  // original draws nothing with (as the flareproc_* below; templdecalalphaglowselfillum is an alias only
  // for NV1X). Exact, ahead of the prefixes templbump* and templmodel*.
  { "templdecalglow_selfillum",        "",                              eS_Opaque,                kOpaque2,                               EF3_NOTEMPLATE, eDC_NoDraw,           eCULL_Back, 0.0f,         0.0f,     false, true },
  { "templdecalglowseflillum",         "",                              eS_Opaque,                kOpaque2,                               EF3_NOTEMPLATE, eDC_NoDraw,           eCULL_Back, 0.0f,         0.0f,     false, true },
  { "templdecalalphaglowselfillum",    "",                              eS_Opaque,                kOpaque2,                               EF3_NOTEMPLATE, eDC_NoDraw,           eCULL_Back, 0.0f,         0.0f,     false, true },
  { "tempdecalalphablend",             "",                              eS_Opaque,                kOpaque2,                               EF3_NOTEMPLATE, eDC_NoDraw,           eCULL_Back, 0.0f,         0.0f,     false, true },
  { "templbumpdefuse",                 "",                              eS_Opaque,                kOpaque2,                               EF3_NOTEMPLATE, eDC_NoDraw,           eCULL_Back, 0.0f,         0.0f,     false, true },
  { "templbumpspechp",                 "",                              eS_Opaque,                kOpaque2,                               EF3_NOTEMPLATE, eDC_NoDraw,           eCULL_Back, 0.0f,         0.0f,     false, true },
  { "templmodfelcommon",               "",                              eS_Opaque,                kOpaque2,                               EF3_NOTEMPLATE, eDC_NoDraw,           eCULL_Back, 0.0f,         0.0f,     false, true },
  { "tempmodelcommon",                 "",                              eS_Opaque,                kOpaque2,                               EF3_NOTEMPLATE, eDC_NoDraw,           eCULL_Back, 0.0f,         0.0f,     false, true },
  { "templdiffuse",                    "",                              eS_Opaque,                kOpaque2,                               EF3_NOTEMPLATE, eDC_NoDraw,           eCULL_Back, 0.0f,         0.0f,     false, true },
  { "templbark",                       "",                              eS_Opaque,                kOpaque2,                               EF3_NOTEMPLATE, eDC_NoDraw,           eCULL_Back, 0.0f,         0.0f,     false, true },
  { "templlightglow",                  "",                              eS_Opaque,                kOpaque2,                               EF3_NOTEMPLATE, eDC_NoDraw,           eCULL_Back, 0.0f,         0.0f,     false, true },
  { "templbrushedmetal",               "",                              eS_Opaque,                kOpaque2,                               EF3_NOTEMPLATE, eDC_NoDraw,           eCULL_Back, 0.0f,         0.0f,     false, true },
  { "wew",                             "",                              eS_Opaque,                kOpaque2,                               EF3_NOTEMPLATE, eDC_NoDraw,           eCULL_Back, 0.0f,         0.0f,     false, true },
  // Heat haze: particles that ripple the scene behind them with r_HeatHaze, as the original's
  // (DrawSprites); not drawn on meshes.
  { "particleminheathaze",             "",                              eS_HeatVision,            0,                                      0,              eDC_HeatHaze,         eCULL_None },
  { "particlemedheathaze",             "",                              eS_HeatVision,            0,                                      0,              eDC_HeatHaze,         eCULL_None },
  { "particlemaxheathaze",             "",                              eS_HeatVision,            0,                                      0,              eDC_HeatHaze,         eCULL_None },
  // The scene behind: TemplRefrEnvTex the copy of the screen over the model by its own texture
  // coordinates (the original's fixed function stage, TexGen Base), opaque, unlit; TemplGlassEnvTex a
  // mirror of the scene mirrored in its plane (Mirrors.cpp), blended, both faces; TemplBumpOffsetEnv
  // the copy of the scene behind the model moved by its bump map, + its texture (the original draws
  // nothing of its swatch).
  { "templrefrenvtex",                 "",                              eS_Refractive,            0,                                      0,              eDC_ScreenTex,        eCULL_Back },
  { "templbumpoffsetenv",              "",                              eS_Refractive,            0,                                      0,              eDC_ScreenBump,       eCULL_Back },
  { "templglassenvtex",                "",                              eS_Banner,                0,                                      0,              eDC_Mirror,           eCULL_None },
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
  { "flareproc *",                     "",                              eS_Banner,                0,                                      0,              eDC_ProcFlare,        eCULL_Back },
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
  { "templdecal_hp",                   ELighting::Vertex },
  { "templdecalalphatest",             ELighting::Vertex },
  { "templdecalglow",                  ELighting::Vertex,   EGlow::Glow },
  { "templdecalglowselfillum",         ELighting::Vertex,   EGlow::SelfIllum },
  { "templdecalglowselfillumanimated", ELighting::Vertex,   EGlow::Pulse },
  { "templdecalglowselfillumanimatedslow", ELighting::Vertex, EGlow::PulseSlow },
  { "templdecallight_specadd",         ELighting::Vertex,   EGlow::LitGloss },
  // Lit by 2 (also by their light map), the texture's alpha its glow (plants blended by it).
  { "templbumpdiffuse_alphaglow",      ELighting::Overbright, EGlow::Alpha },
  { "templlightdiffuse_alphaglow",     ELighting::Overbright, EGlow::Alpha },
  { "templtextureshiftt02",            ELighting::Vertex },
  { "templtextureshiftt05",            ELighting::Vertex },
  { "templtextureshifts05",            ELighting::Vertex },
  { "templtextureshiftst05",           ELighting::Vertex },
  { "templtextureshiftst_2t",          ELighting::Vertex },
  { "templtextureshiftst_2t_alphablend", ELighting::Vertex },
  { "templtexturerotate",              ELighting::Vertex },
  { "templdeform",                     ELighting::Vertex },
  { "communication_island_river",      ELighting::None },
  // The cube map alone (its material's ambient is white: the light saturates), the cube map lit
  // plus the texture, the glass unlit.
  { "templreflcm",                     ELighting::None,     EGlow::None, EEnvMap::Cube },
  { "templbumpreflcm*",                ELighting::None,     EGlow::None, EEnvMap::Cube },
  { "templreflcmdecal",                ELighting::Vertex,   EGlow::None, EEnvMap::CubeDecal },
  { "templreflcmdecal_reflamountopacity", ELighting::Vertex, EGlow::None, EEnvMap::CubeDecalAmount },
  { "templreflcmdecaladdsig",          ELighting::Vertex,   EGlow::None, EEnvMap::CubeAddSigned },
  { "templreflcmdecal_reflamountalpha", ELighting::Vertex,  EGlow::None, EEnvMap::CubeDecalAlpha },
  { "templreflcmdecalmod",             ELighting::Vertex,   EGlow::None, EEnvMap::CubeModulate },
  // The scene around the object (rendered into a cube) times the vertex lighting.
  { "templreflenvcm",                  ELighting::Vertex,   EGlow::None, EEnvMap::Auto },
  { "templbumpreflenvcm",              ELighting::Vertex,   EGlow::None, EEnvMap::Auto },
  { "templglasscm",                    ELighting::None,     EGlow::None, EEnvMap::Glass },
  { "templglasscm_fp",                 ELighting::None,     EGlow::None, EEnvMap::GlassFixed },
  // The templates of vertex and pixel shaders, unlit; the *EnvCM ones with the scene's cube
  // (SetTemplateLighting): the refraction (+ the texture), the bump refraction and bump glass (times
  // the vertex colours), the glass, the dispersion. TemplRefrEnvCM: the texture alone.
  { "templrefrcm",                     ELighting::None,     EGlow::None, EEnvMap::Refract },
  { "templrefrenvcm",                  ELighting::None },
  { "templbumprefrcm",                 ELighting::VColor,   EGlow::None, EEnvMap::BumpRefract },
  { "templbumprefrenvcm",              ELighting::VColor,   EGlow::None, EEnvMap::BumpRefract },
  { "templglassenvcm",                 ELighting::None,     EGlow::None, EEnvMap::Glass },
  { "templglasscm_ps20",               ELighting::None,     EGlow::None, EEnvMap::GlassPS20 },
  { "templglassdispersioncm",          ELighting::None,     EGlow::None, EEnvMap::Dispersion },
  { "templglassdispersionenvcm",       ELighting::None,     EGlow::None, EEnvMap::Dispersion },
  { "templbumpglasscm",                ELighting::VColor,   EGlow::None, EEnvMap::BumpGlass },
  { "templbumpglassenvcm",             ELighting::VColor,   EGlow::None, EEnvMap::BumpGlass },
  { "templbumpglassfresnelcm",         ELighting::VColor,   EGlow::None, EEnvMap::BumpGlassFresnel },
  { "templbumpglassfresnelenvcm",      ELighting::VColor,   EGlow::None, EEnvMap::BumpGlassFresnel },
  // No lights: the texture times the object's ambient colour, or the texture alone (also added).
  { "templalphablend",                 ELighting::Ambient },
  { "templdecalopacity",               ELighting::Ambient },
  { "templdecalalphatestforshadows",   ELighting::Ambient2 },
  { "templdecaladd_vlight",            ELighting::Ambient },
  // The texture times the mesh's vertex colours (white without them).
  { "templdecalopacityshift",          ELighting::VColor },
  { "templdecalopacityshift1",         ELighting::VColor },
  { "templdecalopacityshift2",         ELighting::VColor },
  { "templdecalopacityshift3",         ELighting::VColor },
  { "templdecalopacityshift4",         ELighting::VColor },
  // The texture alone.
  { "templtexturev22rotate",           ELighting::None },
  // Texture * fixed function vertex lighting, blended.
  { "templdecalalphablend_nosort",     ELighting::Vertex },
  // No lights: the texture times the vertex colour (also added by its alpha).
  { "templdecalalphablend",            ELighting::VColor },
  { "templdecalalphaadd",              ELighting::VColor },
};

//! The texture motion of the game's fixed function templates (FixedPipeline.csl: the Matrix
//! 'GL_TEXTURE' of the texture and of a second layer) beyond s_Templates' fScrollV. The original
//! renderer (D3D) takes a matrix's steps in the order written, on the row vector (u, v):
//! TemplTextureRotate's Translate 0.5, RotateZ, Translate -0.5 turns the texture about (-0.5, -0.5);
//! TemplTextureV22Rotate writes them the other way round for D3D (#ifdef OTHER): about the middle.
//! Their RgbGen 'Fixed' (1 1 1 0.5) does not make the original's blend see-through: the alpha is the
//! texture's and the opacity's. TemplTextureShiftST_2T's texture stands still in the original: its
//! stage 0 matrix has the offset in the fourth row only, which a COUNT2 transform of (u, v, 1) does
//! not reach; its gloss layer moves.
struct STemplateMotion
{
  std::string_view Name;
  float fScrollU, fScrollV;       //!< a second (Translate (Comp 'time n'))
  float fRotate;                  //!< degrees a second (RotateZ (Comp 'time n'))
  float fPivot;                   //!< about (fPivot, fPivot)
  bool bSpeedParam;               //!< fRotate is the material's public float "Speed" (UsrTime 'Speed'), when it has it
  ELayer2 eLayer2;
  float fLayer2U, fLayer2V;       //!< the second layer's motion, a second
};

constexpr STemplateMotion s_TemplateMotion[] =
{
  // name                              u      v      turn    pivot  speed  second layer           u      v
  { "templtextureshifts05",            0.5f,  0.0f,  0.0f,   0.5f,  false, ELayer2::None,         0.0f,  0.0f },
  { "templtextureshiftst05",           0.5f,  0.5f,  0.0f,   0.5f,  false, ELayer2::None,         0.0f,  0.0f },
  { "templtextureshiftst_2t",          0.0f,  0.0f,  0.0f,   0.5f,  false, ELayer2::AddGloss,     0.25f, 0.25f },
  { "templtextureshiftst_2t_alphablend", 0.1f, 0.0f, 0.0f,   0.5f,  false, ELayer2::BlendGloss,   0.25f, 0.0f },
  { "templtexturerotate",              0.0f,  0.0f,  200.0f, -0.5f, false, ELayer2::None,         0.0f,  0.0f },
  { "templtexturev22rotate",           0.0f,  0.0f,  200.0f, 0.5f,  true,  ELayer2::None,         0.0f,  0.0f },
  { "communication_island_river",      0.1f,  0.0f,  0.0f,   0.5f,  false, ELayer2::BlendOpacity, 0.08f, 0.0f },
};

//! The templates of shaders of their own (eDC_ShaderFx, eDC_ShaderFxBlend).
struct STemplateFx
{
  std::string_view Name;
  ETemplateFx eFx;
};

constexpr STemplateFx s_TemplateFx[] =
{
  { "templrainbow",                    ETemplateFx::Rainbow },
  { "templmembrane",                   ETemplateFx::Membrane },
  { "templcartoonshading",             ETemplateFx::Cartoon },
  { "templfurrendering",               ETemplateFx::Fur },
  { "templiridescence_ps20",           ETemplateFx::Iridescence },
  { "templbubble_ps20",                ETemplateFx::Bubble },
  { "lavavolume",                      ETemplateFx::Lava },
};

//! The public parameters of a template (GetPublicParams): the engine sets a material's values only for
//! the names a template lists, so a template whose drawing reads them lists them, with their defaults.
struct SPublicParam
{
  std::string_view Template;
  const char *szName;
  float fDefault;
};

constexpr SPublicParam s_PublicParams[] =
{
  { "lavavolume", "TillingAmountX", 2.0f },
  { "lavavolume", "TillingAmountY", 2.0f },
  { "lavavolume", "BumpAmountX", 0.02f },
  { "lavavolume", "BumpAmountY", 0.02f },
  { "lavavolume", "MoveSpeedX", 0.2f },
  { "lavavolume", "MoveSpeedY", 0.2f },
  { "lavavolume", "ColorR", 0.8f },
  { "lavavolume", "ColorG", 0.55f },
  { "lavavolume", "ColorB", 0.0f },
  { "lavavolume", "OverlayAmount", 0.32f },
  { "lavavolume", "DetailTillingAmountX", 1.0f },
  { "lavavolume", "DetailTillingAmountY", 1.0f },
  { "lavavolume", "DetailMoveSpeedX", 0.2f },
  { "lavavolume", "DetailMoveSpeedY", 0.2f },
  { "lavavolume", "WaveAmplitude", 0.1f },
};

void AddPublicParams(TArray<SShaderParam> &Params, std::string_view Template)
{
  for (const SPublicParam &P : s_PublicParams)
  {
    if (P.Template != Template)
      continue;
    SShaderParam sp;
    strncpy(sp.m_Name, P.szName, sizeof(sp.m_Name) - 1);
    sp.m_Name[sizeof(sp.m_Name) - 1] = 0;
    sp.m_Type = eType_FLOAT;
    sp.m_Value.m_Float = P.fDefault;
    Params.AddElem(sp);
  }
}

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
  { "terrainlightpass",                "",                     eS_TerrainLightPass,      0,             EF2_DONTSORTBYDIST,  0,                eDC_TerrainLight },
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

//! The lighting, glow and cube map of the template a material names (s_TemplateLighting; the
//! templates of one shader light each their own way). The cube of the *EnvCM templates is the
//! scene around the object, as the original's render target cube; the *_EnvCMAmb ones take a quarter
//! of their ambient light from the material's cube (a cube file in the original).
void SetTemplateLighting(SShaderProps &Props, std::string_view Name)
{
  const STemplateLighting *pL = FindEntry(s_TemplateLighting, Name);
  Props.eLighting = pL ? pL->eLighting : ELighting::Overbright;
  Props.eGlow = pL ? pL->eGlow : EGlow::None;
  Props.eEnvMap = pL ? pL->eEnvMap : EEnvMap::None;
  Props.bAutoCube = Props.eEnvMap != EEnvMap::None && Props.eEnvMap != EEnvMap::Auto && Name.ends_with("envcm");
  Props.bAmbientCube = Name.find("_envcmamb") != std::string_view::npos;
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
  if (pE->eClass == eDC_ProcFlare)
  {
    // "flareproc <size>"; 1 for "flareproc", the entry's own for the named ones.
    R.Props.fFlareSize = pE->fFlareSize > 0 ? pE->fFlareSize : 1.0f;
    if (const size_t nSpace = Name.find(' '); nSpace != std::string::npos)
    {
      float fSize = 0;
      if (std::from_chars(Name.data() + nSpace + 1, Name.data() + Name.size(), fSize).ec == std::errc() && fSize > 0)
        R.Props.fFlareSize = fSize;
    }
  }
  if (pE->bNotFound)
    R.Props.nFlags |= EF_NOTFOUND;
  // The opacity shifts' pixel shader is the texture times the vertex colour (CGVProgWaterFall passes
  // the mesh's colours, white without them): the material's opacity does not reach it.
  R.Props.bNoOpacity = Name.starts_with("templdecalopacityshift");
  if (const STemplateMotion *pM = FindEntry(s_TemplateMotion, Name))
  {
    R.Props.fScrollU = pM->fScrollU;
    if (pM->fScrollV != 0.0f)
      R.Props.fScrollV = pM->fScrollV;
    R.Props.fRotate = pM->fRotate;
    R.Props.fRotatePivot = pM->fPivot;
    R.Props.bRotateSpeedParam = pM->bSpeedParam;
    R.Props.eLayer2 = pM->eLayer2;
    R.Props.fLayer2Scroll[0] = pM->fLayer2U;
    R.Props.fLayer2Scroll[1] = pM->fLayer2V;
  }
  if (const STemplateFx *pFx = FindEntry(s_TemplateFx, Name))
    R.Props.eFx = pFx->eFx;
  SetTemplateLighting(R.Props, Name);
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

//! The text without its "/* */" comments.
std::string StripBlockComments(std::string_view Text)
{
  std::string Out;
  Out.reserve(Text.size());
  size_t nPos = 0;
  while (nPos < Text.size())
  {
    const size_t nStart = Text.find("/*", nPos);
    if (nStart == std::string_view::npos)
    {
      Out.append(Text.substr(nPos));
      break;
    }
    Out.append(Text.substr(nPos, nStart - nPos));
    const size_t nEnd = Text.find("*/", nStart + 2);
    if (nEnd == std::string_view::npos)
      break;
    nPos = nEnd + 2;
  }
  return Out;
}

//! The numbers of a string, in order (as many as fit in pOut).
int ParseFloats(std::string_view s, float *pOut, int nMax)
{
  int n = 0;
  const char *p = s.data(), *pEnd = s.data() + s.size();
  while (p < pEnd && n < nMax)
  {
    if (isdigit((unsigned char)*p) || ((*p == '-' || *p == '.') && p + 1 < pEnd && (isdigit((unsigned char)p[1]) || p[1] == '.')))
    {
      auto [pNext, ec] = std::from_chars(p, pEnd, pOut[n]);
      if (ec == std::errc())
      {
        n++;
        p = pNext;
        continue;
      }
    }
    p++;
  }
  return n;
}

//! The value after the '=' of "Key = value" (empty without one).
std::string_view ValueOf(std::string_view Line)
{
  const size_t nEq = Line.find('=');
  return nEq == std::string_view::npos ? std::string_view() : Trim(Line.substr(nEq + 1));
}

//! The first word of the value of "Key = value" ('\' as '/').
std::string FirstWordOf(std::string_view Line)
{
  std::string_view Value = ValueOf(Line);
  std::string Word(Value.substr(0, std::min(Value.find_first_of(" \t"), Value.size())));
  std::replace(Word.begin(), Word.end(), '\\', '/');
  return Word;
}

//! The GS_BLSRC_* (bDst false) or GS_BLDST_* value of a blend factor of a script ("SRC_ALPHA",
//! "ONE_MINUS_SRC_ALPHA", ..., "GL_" before it or not); 0 if there is none of that name.
int BlendFactor(std::string_view Name, bool bDst)
{
  if (Name.size() > 3 && _strnicmp(Name.data(), "GL_", 3) == 0)
    Name.remove_prefix(3);
  struct SFactor
  {
    std::string_view Name;
    int nSrc, nDst;
  };
  static const SFactor s_Factors[] =
  {
    { "ZERO",                GS_BLSRC_ZERO,             GS_BLDST_ZERO },
    { "ONE",                 GS_BLSRC_ONE,              GS_BLDST_ONE },
    { "DST_COLOR",           GS_BLSRC_DSTCOL,           0 },
    { "ONE_MINUS_DST_COLOR", GS_BLSRC_ONEMINUSDSTCOL,   0 },
    { "SRC_COLOR",           0,                         GS_BLDST_SRCCOL },
    { "ONE_MINUS_SRC_COLOR", 0,                         GS_BLDST_ONEMINUSSRCCOL },
    { "SRC_ALPHA",           GS_BLSRC_SRCALPHA,         GS_BLDST_SRCALPHA },
    { "ONE_MINUS_SRC_ALPHA", GS_BLSRC_ONEMINUSSRCALPHA, GS_BLDST_ONEMINUSSRCALPHA },
    { "DST_ALPHA",           GS_BLSRC_DSTALPHA,         GS_BLDST_DSTALPHA },
    { "ONE_MINUS_DST_ALPHA", GS_BLSRC_ONEMINUSDSTALPHA, GS_BLDST_ONEMINUSDSTALPHA },
    { "SRC_ALPHA_SATURATE",  GS_BLSRC_ALPHASATURATE,    0 },
  };
  for (const SFactor &f : s_Factors)
  {
    if (Name.size() == f.Name.size() && _strnicmp(Name.data(), f.Name.data(), Name.size()) == 0)
      return bDst ? f.nDst : f.nSrc;
  }
  return 0;
}

//! The four components of a TexGen plane ("PlaneS (Comp = 0.25 Comp = 0 Comp = 0 Comp 'time
//! 0.02')"): a number goes to Plane, the k of 'time k' to Time (the other one 0).
void ParsePlane(std::string_view Line, float *pPlane, float *pTime)
{
  const std::string Low = Lower(std::string(Line).c_str());
  size_t nPos = 0;
  for (int n = 0; n < 4 && (nPos = Low.find("comp", nPos)) != std::string::npos; n++)
  {
    nPos = Low.find_first_not_of(" \t", nPos + 4);
    if (nPos == std::string::npos)
      break;
    pPlane[n] = pTime[n] = 0;
    if (Low[nPos] == '=')
      ParseFloats(std::string_view(Low).substr(nPos + 1), &pPlane[n], 1);
    else if (Low[nPos] == '\'')
    {
      const size_t nEnd = std::min(Low.find('\'', nPos + 1), Low.size());
      const std::string_view Expr = Trim(std::string_view(Low).substr(nPos + 1, nEnd - nPos - 1));
      if (StartsWithWord(Expr, "time"))
        ParseFloats(Expr.substr(4), &pTime[n], 1);
      nPos = nEnd;
    }
  }
}

//! The sky shaders of a shader script: each "Shader '<name>'" block with a "SkyBox = <name>" line
//! (both lower case, '\' as '/') under its name, with the layers after the first one ("Layer '0'",
//! the box) that name a Map other than $None: Map, Blend and the planes of TexGen (PlaneS, PlaneT);
//! the other keys are not read. ';', "//" and "/* */" start comments.
void ParseSkies(std::string_view Text, std::unordered_map<std::string, SSkyShader> &Skies)
{
  const std::string Clean = StripBlockComments(Text);
  std::string Shader;
  SSkyShader Sky;
  SSkyLayer Layer;
  int nLayers = 0;          // "Layer" blocks of the shader so far
  const auto EndLayer = [&]()
  {
    if (nLayers > 1 && !Layer.Map.empty())
      Sky.Layers.push_back(Layer);
    Layer = SSkyLayer();
  };
  const auto EndShader = [&]()
  {
    EndLayer();
    if (!Shader.empty() && !Sky.Box.empty())
      Skies[Shader] = std::move(Sky);
    Sky = SSkyShader();
    nLayers = 0;
  };
  size_t nPos = 0;
  while (nPos < Clean.size())
  {
    size_t nEnd = Clean.find('\n', nPos);
    if (nEnd == std::string::npos)
      nEnd = Clean.size();
    std::string_view Line(Clean.data() + nPos, nEnd - nPos);
    nPos = nEnd + 1;
    Line = Trim(Line.substr(0, std::min(Line.find("//"), Line.find(';'))));
    if (StartsWithWord(Line, "Shader"))
    {
      EndShader();
      const size_t q1 = Line.find('\'');
      const size_t q2 = q1 == std::string_view::npos ? q1 : Line.find('\'', q1 + 1);
      Shader = q2 == std::string_view::npos ? std::string() : Lower(std::string(Line.substr(q1 + 1, q2 - q1 - 1)).c_str());
    }
    else if (StartsWithWord(Line, "SkyBox"))
      Sky.Box = Lower(FirstWordOf(Line).c_str());
    else if (StartsWithWord(Line, "Layer"))
    {
      EndLayer();
      nLayers++;
    }
    else if (nLayers <= 1)
      continue;
    else if (StartsWithWord(Line, "Map"))
    {
      std::string Map = FirstWordOf(Line);
      Layer.Map = _stricmp(Map.c_str(), "$None") ? std::move(Map) : std::string();
    }
    else if (StartsWithWord(Line, "Blend"))
    {
      const size_t q1 = Line.find('\'');
      const size_t q2 = q1 == std::string_view::npos ? q1 : Line.find('\'', q1 + 1);
      if (q2 == std::string_view::npos)
        continue;
      const std::string_view Factors = Trim(Line.substr(q1 + 1, q2 - q1 - 1));
      const size_t nSpace = Factors.find_first_of(" \t");
      if (nSpace == std::string_view::npos)
        continue;
      const int nSrc = BlendFactor(Factors.substr(0, nSpace), false);
      const int nDst = BlendFactor(Trim(Factors.substr(nSpace)), true);
      Layer.nBlend = nSrc && nDst ? nSrc | nDst : 0;
    }
    else if (StartsWithWord(Line, "PlaneS"))
      ParsePlane(Line, Layer.PlaneS, Layer.TimeS);
    else if (StartsWithWord(Line, "PlaneT"))
      ParsePlane(Line, Layer.PlaneT, Layer.TimeT);
  }
  EndShader();
}

//! Walks the "ClientEffect '<Effect>'" blocks of a shader script (Effect in lower case, quoted):
//! OnStart() at a block's start, OnLine(Line) for each of its lines (the rest of the ClientEffect
//! line first, without "//" comments, trimmed), OnEnd(Shader) at its closing brace, Shader the name
//! of the "Shader '<name>'" block it is in (lower case). "/* */" and "//" start comments.
template <class FStart, class FLine, class FEnd>
void ParseClientEffects(std::string_view Text, std::string_view Effect, FStart OnStart, FLine OnLine, FEnd OnEnd)
{
  const std::string Clean = StripBlockComments(Text);
  std::string Shader;
  int nDepth = -1;          // braces open in the effect's block; -1: not in one
  bool bOpened = false;
  size_t nPos = 0;
  while (nPos < Clean.size())
  {
    size_t nEnd = Clean.find('\n', nPos);
    if (nEnd == std::string::npos)
      nEnd = Clean.size();
    std::string_view Line(Clean.data() + nPos, nEnd - nPos);
    nPos = nEnd + 1;
    Line = Trim(Line.substr(0, std::min(Line.find("//"), Line.size())));
    if (nDepth < 0)
    {
      if (StartsWithWord(Line, "Shader"))
      {
        const size_t q1 = Line.find('\'');
        const size_t q2 = q1 == std::string_view::npos ? q1 : Line.find('\'', q1 + 1);
        if (q2 != std::string_view::npos)
          Shader = Lower(std::string(Line.substr(q1 + 1, q2 - q1 - 1)).c_str());
        continue;
      }
      if (!StartsWithWord(Line, "ClientEffect") || Shader.empty() || Lower(std::string(Line).c_str()).find(Effect) == std::string::npos)
        continue;
      OnStart();
      nDepth = 0;
      bOpened = false;
      Line = Line.substr(Line.find('\'', Line.find('\'') + 1) + 1);
    }
    OnLine(Line);
    for (char c : Line)
    {
      if (c == '{')
      {
        nDepth++;
        bOpened = true;
      }
      else if (c == '}' && nDepth > 0)
        nDepth--;
    }
    if (bOpened && nDepth == 0)
    {
      OnEnd(Shader);
      nDepth = -1;
    }
  }
}

//! The "ClientEffect 'Corona'" blocks of a shader script, under the name of the "Shader '<name>'"
//! block they are in (lower case): RGBStyle ('Fixed' (r g b a) or = FromLight), Scale (a number,
//! or 'Comp' with a sine wave of Level, Amp, Phase and Freq: its Level), DistFactor and Map; the
//! other keys are not read.
void ParseCoronas(std::string_view Text, std::unordered_map<std::string, SCorona> &Coronas)
{
  SCorona Corona;
  bool bScaleComp = false;  // Scale 'Comp': the next Level is the scale
  auto OnStart = [&]
  {
    Corona = SCorona();
    bScaleComp = false;
  };
  auto OnLine = [&](std::string_view Line)
  {
    if (StartsWithWord(Line, "RGBStyle"))
    {
      const std::string Low = Lower(std::string(Line).c_str());
      if (Low.find("fromlight") != std::string::npos)
        Corona.bFromLight = true;
      else if (Low.find("'fixed'") != std::string::npos)
        ParseFloats(Line.substr(Line.find('(') == std::string_view::npos ? Line.size() : Line.find('(')), Corona.Color, 4);
    }
    else if (StartsWithWord(Line, "Scale"))
    {
      float f;
      if (!ValueOf(Line).empty() && ParseFloats(ValueOf(Line), &f, 1) == 1)
        Corona.fScale = f;
      else
        bScaleComp = true;
    }
    else if (StartsWithWord(Line, "DistFactor"))
    {
      float f;
      if (ParseFloats(ValueOf(Line), &f, 1) == 1)
        Corona.fDistFactor = f;
    }
    else if (StartsWithWord(Line, "Map"))
    {
      std::string_view Value = ValueOf(Line);
      Value = Value.substr(0, std::min(Value.find_first_of(" \t"), Value.size()));
      Corona.Map = std::string(Value);
      std::replace(Corona.Map.begin(), Corona.Map.end(), '\\', '/');
    }
    if (bScaleComp)
    {
      const std::string Low = Lower(std::string(Line).c_str());
      if (const size_t nLevel = Low.find("level"); nLevel != std::string::npos)
      {
        float f;
        if (ParseFloats(Line.substr(nLevel), &f, 1) == 1)
        {
          Corona.fScale = f;
          bScaleComp = false;
        }
      }
    }
  };
  ParseClientEffects(Text, "'corona'", OnStart, OnLine, [&](const std::string &Shader) { Coronas[Shader] = Corona; });
}

//! The "ClientEffect 'Beam'" blocks of a shader script, under the name of the "Shader '<name>'"
//! block they are in (lower case): Model, LightStyle, StartRadius, EndRadius, Length, StartColor
//! (r g b a) and EndColor (r g b a).
void ParseBeams(std::string_view Text, std::unordered_map<std::string, SBeam> &Beams)
{
  SBeam Beam;
  auto OnLine = [&](std::string_view Line)
  {
    float f;
    if (StartsWithWord(Line, "Model"))
      Beam.Model = FirstWordOf(Line);
    else if (StartsWithWord(Line, "LightStyle") && ParseFloats(ValueOf(Line), &f, 1) == 1)
      Beam.nLightStyle = (int)f;
    else if (StartsWithWord(Line, "StartRadius") && ParseFloats(ValueOf(Line), &f, 1) == 1)
      Beam.fStartRadius = f;
    else if (StartsWithWord(Line, "EndRadius") && ParseFloats(ValueOf(Line), &f, 1) == 1)
      Beam.fEndRadius = f;
    else if (StartsWithWord(Line, "Length") && ParseFloats(ValueOf(Line), &f, 1) == 1)
      Beam.fLength = f;
    else if (StartsWithWord(Line, "StartColor"))
      ParseFloats(Line.substr(std::min(Line.find('('), Line.size())), Beam.StartColor, 4);
    else if (StartsWithWord(Line, "EndColor"))
      ParseFloats(Line.substr(std::min(Line.find('('), Line.size())), Beam.EndColor, 4);
  };
  ParseClientEffects(Text, "'beam'", [&] { Beam = SBeam(); }, OnLine, [&](const std::string &Shader) { Beams[Shader] = Beam; });
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

void CShader::SetCorona(const SCorona &Corona)
{
  m_pCorona = std::make_unique<SCorona>(Corona);
  if (!m_pFlare)
  {
    // The engine adds it with the sun's object; the renderer draws it in its flare pass.
    m_pFlare.reset(RE_CreateGeneric(eDATA_Flare));
    m_REs.AddElem(m_pFlare.get());
  }
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
  m_Skies.clear();
  m_Coronas.clear();
  m_Beams.clear();
  m_bScriptsRead = false;
  m_pDefault = nullptr;
  m_nNextUserTemplate = EFT_USER_FIRST;
  m_nGlobalTemplate = -2;
}

void CShaderManager::ReadScripts()
{
  if (m_bScriptsRead)
    return;
  ICryPak *pPak = iSystem ? iSystem->GetIPak() : nullptr;
  if (!pPak)
    return;
  m_bScriptsRead = true;
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
  {
    if (ReadTextFile(pPak, File.c_str(), Text))
    {
      ParseSkies(Text, m_Skies);
      ParseCoronas(Text, m_Coronas);
      ParseBeams(Text, m_Beams);
    }
  }
}

const SSkyShader *CShaderManager::FindSky(const char *szShader)
{
  ReadScripts();
  const auto it = m_Skies.find(Lower(szShader));
  return it == m_Skies.end() ? nullptr : &it->second;
}

const SCorona *CShaderManager::FindCorona(const char *szShader)
{
  ReadScripts();
  const auto it = m_Coronas.find(Lower(szShader));
  return it == m_Coronas.end() ? nullptr : &it->second;
}

const SBeam *CShaderManager::FindBeam(const char *szShader)
{
  ReadScripts();
  const auto it = m_Beams.find(Lower(szShader));
  return it == m_Beams.end() ? nullptr : &it->second;
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
        else if (FindSky(Name.c_str()))
        {
          // A sky shader of the scripts (I3DEngine::SetSkyBox): the level's CRESky element is
          // drawn with it as a sky box.
          Props.eSort = eS_Sky;
          Props.nFlags |= EF_SKY;
          Props.eDrawClass = eDC_Sky;
        }
        else
        {
          // Lights without a corona, screen shaders, ...: nothing this renderer can draw from the
          // name alone.
          Props.eDrawClass = eDC_NoDraw;
        }
        // A flare (a corona in its script: the sun's, the lights'): sorted with the nearest, its
        // Flare element drawn by the flare pass.
        const SCorona *pCorona = FindCorona(Name.c_str());
        if (pCorona)
        {
          Props.eSort = eS_Nearest;
          Props.eDrawClass = eDC_Flare;
        }
        Props.nFlags |= nFlags & EF_SYSTEM;
        pSH = Create(Resolved, EShaderKind::Engine, Props, nullptr, nMaskGen);
        if (pCorona)
          pSH->SetCorona(*pCorona);
        // A light's beam (drawn at the lights with the shader by the beam pass, whatever the
        // shader's draw class).
        if (const SBeam *pBeam = FindBeam(Name.c_str()))
          pSH->SetBeam(*pBeam);
        m_ByName.emplace(std::move(Resolved), pSH);
      }
    }
  }
  pSH->AddRef();
  return pSH;
}

CShader *CShaderManager::GetTemplate(const char *szName, uint64 nMaskGen)
{
  std::string Name = Unalias(Lower(szName));
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
    AddPublicParams(pSH->m_PublicParams, R.Name);
    m_ByName.emplace(R.Name, pSH);
  }
  m_TemplateNames.emplace(std::move(Key), pSH);
  return pSH;
}

CShader *CShaderManager::LoadMaterial(const char *szName, const char *szTemplate, uint64 nMaskGen)
{
  CShader *pTempl = GetTemplate(szTemplate, nMaskGen);
  std::string Name = Lower(szName);
  // An alias (TBD, tbs_HP) as the template it stands for.
  std::string Templ = Unalias(Lower(szTemplate));
  // The templates with vertex and pixel shaders when bump mapping is on: their names stay then
  // (GetTemplate(-1) is the template as named), each a shader of its own, so that materials of one
  // name keep their own lighting (all those of a level's material library are "MaterialContainer").
  const bool bPixelLighting = Templ.starts_with("templbump") || Templ.starts_with("templmodel") || Templ.starts_with("templlightdiffuse");
  const bool bBump = bPixelLighting && g_pRenderer && g_pRenderer->CV_r_Quality_BumpMapping > 0;
  // The lighting of the template as named, not of the shader it shares with other templates (those
  // that light otherwise make shaders of their own too).
  const SShaderProps &Shared = pTempl->GetProps();
  SShaderProps Props = Shared;
  SetTemplateLighting(Props, Templ);
  const bool bOwnLighting = Props.eLighting != Shared.eLighting || Props.eGlow != Shared.eGlow || Props.eEnvMap != Shared.eEnvMap ||
                            Props.bAutoCube != Shared.bAutoCube || Props.bAmbientCube != Shared.bAmbientCube;
  std::string Key = Name + '\n' + (bBump || bOwnLighting ? Templ : std::string(pTempl->GetName()));
  if (auto it = m_Materials.find(Key); it != m_Materials.end())
    return it->second;

  // Another template for a name that has a shader already: "<name>_<n>".
  int &nMade = m_MaterialCounts[Name];
  std::string ShaderName = nMade ? Name + '_' + std::to_string(nMade) : Name;
  nMade++;

  Props.nFlags = EF_COMPILEDLAYERS | EF_NEEDNORMALS | EF_NOTFOUND;
  Props.Template = std::move(Templ);
  Props.bPixelLighting = bPixelLighting;
  Props.bSpecular = bPixelLighting && Props.Template.find("spec") != std::string::npos;
  Props.bGlossAlpha = Props.bSpecular && Props.Template.find("glossalpha") != std::string::npos;
  // Their bump mapping needs tangent frames: the tangent stream of a character is filled for the
  // materials that report EF_NEEDTANGENTS.
  if (bBump)
    Props.nFlags |= EF_NEEDTANGENTS;
  CShader *pSH = Create(std::move(ShaderName), EShaderKind::Material, Props, pTempl, nMaskGen);
  AddPublicParams(pSH->m_PublicParams, pSH->m_Props.Template);
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
    {
      for (int nSlot : kLoadedSlots)
        LoadResourceTexture(pRR, nSlot);
      LoadDrawnTextures(pSH, pRR);
    }
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

CTexture *CShaderManager::LoadNormalMap(SRenderShaderResources *pRes)
{
  auto Usable = [](CTexture *p) { return p && !p->IsCube() && !(p->GetFlags() & FT_NOTFOUND) && p->IsTextureLoaded(); };
  // d3d12_NormalMapHeight: a height map in EFTT_NORMALMAP (the material's displacement map, its name no normal map's;
  // Research's cave rock) is no normal map. With a normal map in EFTT_BUMP the slot gets one texture of both, as the
  // original's (CTextureManager::LoadNormalWithHeight); alone it is made a normal map as a height bump map is. Once,
  // when the slot has no texture yet.
  SEfResTexture *pNormal = pRes ? pRes->m_Textures[EFTT_NORMALMAP] : nullptr;
  if (pNormal && !pNormal->m_TU.m_ITexPic && !pNormal->m_TU.m_AnimInfo && !pNormal->m_Name.empty() && g_pRenderer &&
      g_pRenderer->CV_d3d12_NormalMapHeight && CTextureManager::IsHeightMapName(pNormal->m_Name.c_str()))
  {
    const SEfResTexture *pBump = pRes->m_Textures[EFTT_BUMP];
    if (pBump && !pBump->m_Name.empty() && g_pRenderer->m_pTextures)
      pNormal->m_TU.m_ITexPic = g_pRenderer->m_pTextures->LoadNormalWithHeight(pBump->m_Name.c_str(), pNormal->m_Name.c_str(),
                                                                               pRes->m_TexturePath.c_str(), pBump->m_TU.GetTexFlags());
    if (!pNormal->m_TU.m_ITexPic)
      pNormal->m_TU.m_eTexType = eTT_Bumpmap;
  }
  CTexture *pMap = LoadResourceTexture(pRes, EFTT_NORMALMAP);
  if (!Usable(pMap))
    pMap = LoadResourceTexture(pRes, EFTT_BUMP);
  return Usable(pMap) ? pMap : nullptr;
}

void CShaderManager::LoadDrawnTextures(const CShader *pSH, SRenderShaderResources *pRes)
{
  // As CD3D12Renderer::DrawLeafItem reads them: the normal map for the bump mapping of the pixel lit
  // templates and for the terrain's detail layers, the gloss map for the glow and the reflection
  // amount, the material's cube for the cube map templates (and their ambient's); the wings of iridescence
  // their normal and gloss maps, the bubble its cube.
  const SShaderProps &Props = pSH->GetProps();
  if ((Props.bPixelLighting && g_pRenderer && g_pRenderer->CV_r_Quality_BumpMapping > 0) || Props.eDrawClass == eDC_TerrainLayer ||
      Props.BumpCube() || Props.eFx == ETemplateFx::Iridescence)
    LoadNormalMap(pRes);
  if ((Props.eGlow != EGlow::None && Props.eGlow != EGlow::Alpha) || Props.eEnvMap == EEnvMap::CubeDecalAmount ||
      Props.eLayer2 == ELayer2::AddGloss || Props.eLayer2 == ELayer2::BlendGloss || Props.eFx == ETemplateFx::Iridescence)
    LoadResourceTexture(pRes, EFTT_GLOSS);
  if (Props.eLayer2 == ELayer2::BlendOpacity)
    LoadResourceTexture(pRes, EFTT_OPACITY);
  if ((Props.eEnvMap != EEnvMap::None && !Props.UsesAutoCube()) || Props.bAmbientCube || Props.eFx == ETemplateFx::Bubble)
    LoadResourceTexture(pRes, EFTT_CUBEMAP);
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
  if (pRes && pRes->m_AlphaRef > 0)
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

float GetResourceFloat(const SRenderShaderResources *pRes, const char *szName, float fDefault)
{
  if (!pRes)
    return fDefault;
  const TArray<SShaderParam> &Params = pRes->m_ShaderParams;
  for (int i = 0; i < Params.Num(); i++)
  {
    const SShaderParam &P = Params[i];
    if (_strnicmp(P.m_Name, szName, sizeof(P.m_Name)))
      continue;
    if (P.m_Type == eType_FLOAT)
      return P.m_Value.m_Float;
    if (P.m_Type == eType_INT)
      return (float)P.m_Value.m_Int;
  }
  return fDefault;
}

void GetResourceColor(const SRenderShaderResources *pRes, const char *szName, const float vDefault[3], float vOut[3])
{
  for (int i = 0; i < 3; i++)
    vOut[i] = vDefault[i];
  if (!pRes)
    return;
  const TArray<SShaderParam> &Params = pRes->m_ShaderParams;
  for (int i = 0; i < Params.Num(); i++)
  {
    const SShaderParam &P = Params[i];
    if (_strnicmp(P.m_Name, szName, sizeof(P.m_Name)))
      continue;
    if (P.m_Type == eType_FCOLOR)
      vOut[0] = P.m_Value.m_Color[0], vOut[1] = P.m_Value.m_Color[1], vOut[2] = P.m_Value.m_Color[2];
    else if (P.m_Type == eType_VECTOR)
      vOut[0] = P.m_Value.m_Vector[0], vOut[1] = P.m_Value.m_Vector[1], vOut[2] = P.m_Value.m_Vector[2];
    return;
  }
}

void GetTexTransform(const CShader *pShader, const SRenderShaderResources *pRes, int nSlot, float vRow0[3], float vRow1[3])
{
  vRow0[0] = 1.0f, vRow0[1] = 0.0f, vRow0[2] = 0.0f;
  vRow1[0] = 0.0f, vRow1[1] = 1.0f, vRow1[2] = 0.0f;
  if (pRes && nSlot >= 0 && nSlot < EFTT_MAX && pRes->m_Textures[nSlot])
  {
    const SEfTexModificator &Mod = pRes->m_Textures[nSlot]->m_TexModificator;
    if (Mod.m_Tiling[0] != 0.0f)
      vRow0[0] = Mod.m_Tiling[0];
    if (Mod.m_Tiling[1] != 0.0f)
      vRow1[1] = Mod.m_Tiling[1];
    vRow0[2] = Mod.m_Offs[0];
    vRow1[2] = Mod.m_Offs[1];
  }
  if (!pShader)
    return;
  const SShaderProps &Props = pShader->GetProps();
  const float fRotate = Props.bRotateSpeedParam ? GetResourceFloat(pRes, "speed", Props.fRotate) : Props.fRotate;
  if (fRotate != 0.0f)
  {
    // (u, v) about the pivot p: u' = c (u - p) - s (v - p) + p, v' = s (u - p) + c (v - p) + p.
    const float a = fmodf(fRotate * CurrentTime(), 360.0f) * (gf_PI / 180.0f);
    const float c = cosf(a), s = sinf(a), p = Props.fRotatePivot;
    float r0[3], r1[3];
    for (int i = 0; i < 3; i++)
    {
      r0[i] = c * vRow0[i] - s * vRow1[i];
      r1[i] = s * vRow0[i] + c * vRow1[i];
    }
    r0[2] += p * (1.0f - c + s);
    r1[2] += p * (1.0f - s - c);
    for (int i = 0; i < 3; i++)
      vRow0[i] = r0[i], vRow1[i] = r1[i];
  }
  if (Props.fScrollU != 0.0f)
    vRow0[2] += fmodf(Props.fScrollU * CurrentTime(), 1.0f);
  if (Props.fScrollV != 0.0f)
    vRow1[2] += fmodf(Props.fScrollV * CurrentTime(), 1.0f);
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
