// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Shader.h
//	Description: Shaders (IShader), shader resources and the shader manager.
//
//	The renderer does not run the engine's shader scripts (it only reads
//	the sky boxes and the coronas they give). A CShader is a name with the getter values the
//	engine depends on and a draw class: how this renderer draws what uses it
//	(one of a few fixed ways). The IRenderer shader methods are in
//	RendererShaders.cpp.
//
//	Three kinds of shaders:
//	  - engine shaders, loaded by name with EF_LoadShader ("Terrain",
//	    "ScreenProcess", "ParticleLight", the level's sky and flares, ...);
//	  - templates: what a material names ("TemplBumpDiffuse", "TemplPlants1",
//	    "NoDraw", ...); also what IShader::AddTemplate adds as a user template
//	    (ids from EFT_USER_FIRST) and what EF_LoadShader of a template name gives;
//	  - materials (EF_LoadShaderItem with a template name): named after the
//	    material, with the getter values and the draw class of their template.
//	GetTemplate(-1) of every shader is the shader itself, so the hot
//	SShaderItem::IsTransparent (GetTemplate(-1)->GetFlags2() & EF2_OPAQUE)
//	costs two trivial virtual calls.
//
//	Lifetime: a shader stays in the manager until Shutdown. IShader::Release
//	only counts down: the engine keeps shader pointers (leaf buffer chunks,
//	render elements, lights) beyond its own references, and a name loaded
//	again gets the same shader back. Shader resources (SRenderShaderResources)
//	are made per EF_LoadShaderItem and deleted by their last Release.
//
//	Drawing an element: take the shader to draw with from
//	CShader::GetDrawShader(nTempl) (the template of EF_AddEf, CCObject::m_nTemplId
//	or SRendParams::nShaderTemplate applied), then pass it with the element's
//	resources to the accessors at the end of this file (GetDrawClass,
//	GetDiffuseTexture, GetOpacity, GetAlphaRef, GetCullMode, GetMaterialColor,
//	GetTexTransform, GetColorScale). Every IShader the engine passes in is a
//	CShader of this renderer (static_cast is safe).
//
//////////////////////////////////////////////////////////////////////

#pragma once

class CShaderManager;
class CTexture;

//! How the renderer draws an element with this shader.
enum EDrawClass
{
  eDC_NoDraw = 0,         //!< not drawn (unsupported, invisible surfaces, ...)
  eDC_Opaque,             //!< diffuse texture, lit (sun + ambient) or lightmapped
  eDC_AlphaTest,          //!< as opaque, alpha test
  eDC_AlphaTestVColor,    //!< vegetation leaves: texture * vertex colour * 2, alpha test
  eDC_VColor,             //!< vegetation bark: texture * vertex colour * 2
  eDC_AlphaBlend,         //!< blended, no depth writes, drawn back to front
  eDC_Additive,           //!< ONE, ONE (muzzle flashes, glow decals)
  eDC_Terrain,            //!< terrain sector: texture coordinates from the world position
  eDC_Sky,                //!< sky box
  eDC_Engine,             //!< drawn by its render element (2D quads, screen effects, ...)
  eDC_Water,              //!< water surface: the ocean ring, water volumes (refraction, reflection, waves)
  eDC_WaterBottom,        //!< the ocean floor ring beyond the terrain
  eDC_WaterBeach,         //!< surf along the shores
  eDC_TerrainLayer,       //!< a detail layer of the terrain near the camera, multiplied in twice
  eDC_Decal,              //!< a decal on a static object: over its owner's vertices, blended
  eDC_TerrainShadow,      //!< the shadow of static objects on the terrain (their shadow maps), blended
  eDC_Caustics,           //!< the light the ocean's waves focus on the terrain under it, multiplied in
  eDC_FogLayer,           //!< the cap of a fog volume (its layer), blended with the volume's fog
  eDC_ScopeLens,          //!< the glass of a weapon's scope (r_ScopeLens_fx): the scene seen through it, blended
  eDC_Flare,              //!< a flare (its Flare element; the sun's): drawn by the flare pass (Flares.cpp)
  eDC_Modulate,           //!< a decal multiplied into the frame (DESTCOLOR, ZERO): the texture, fading to white with the object; with a material opacity below 1 laid over by its alpha times it instead
  eDC_TerrainLight,       //!< the terrain under dynamic lights (triangles of a sector near them): their light alone, added
  eDC_ShadowOnly,         //!< not seen, only in the shadow maps: it casts its shadow (TemplRefrEnvCM)
  eDC_ProcFlare,          //!< a glow around the surface facing the camera, added (FlareProc; Flares.cpp)
  eDC_ScreenTex,          //!< the copy of the scene drawn so far over the model by its texture coordinates, unlit (TemplRefrEnvTex)
  eDC_ScreenBump,         //!< the copy of the scene behind the model moved by its bump map + its texture, opaque (TemplBumpOffsetEnv)
  eDC_Mirror,             //!< a mirror: the scene mirrored in its plane (Mirrors.cpp) * ReflectAmount + texture * (1 - it), blended
                          //!< (TemplGlassEnvTex)
  eDC_HeatHaze,           //!< particles of heat haze: the copy of the scene seen through a ripple (r_HeatHaze; DrawSprites)
  eDC_ShaderFx,           //!< a template's shader of its own (SShaderProps::eFx; TemplateFx.cpp), opaque: rainbow, cartoon shading
  eDC_ShaderFxBlend,      //!< as eDC_ShaderFx, among the blended objects: membrane, fur, iridescence, bubble
};

//! The templates of shaders of their own (General.hlsl, ShaderFx; TemplateFx.cpp).
enum class ETemplateFx : uint8
{
  None,
  Rainbow,                //!< TemplRainbow: Defaults/colors2 at (N.E, N.H), unlit
  Membrane,               //!< TemplMembrane: a row of colours at E.N + 0.2 t, added
  Cartoon,                //!< TemplCartoonShading: the steps of the sun's light times the dark outline, times 1 0.8 0.2
  Fur,                    //!< TemplFurRendering: the model and 8 shells 1 to 8 cm out along the normal, blended
  Iridescence,            //!< TemplIridescence_PS20: a wing lit from both sides, see-through by its alpha, a rainbow gloss
  Bubble,                 //!< TemplBubble_PS20: a wobbling soap bubble, the cube reflected, see-through but at its edges
  Lava,                   //!< LavaVolume: the material's lava colour over its texture flowing with time, unlit, opaque
};

//! The corona of a flare shader: the "ClientEffect 'Corona'" block of its script.
struct SCorona
{
  bool bFromLight = false;            //!< RGBStyle = FromLight: the colour of the light, else Color
  float Color[4] = { 1, 1, 1, 1 };    //!< RGBStyle 'Fixed' (r g b a)
  float fScale = 1;                   //!< Scale: the half side of the square at a distance of 1
  float fDistFactor = 1;              //!< DistFactor: the half side grows with the distance to this power
  std::string Map;                    //!< Map: the texture
};

//! The beam of a light shader: the "ClientEffect 'Beam'" block of its script (Lights.csl, the
//! LightBeam* shaders): a cone of light from the light along its forward axis, drawn by the beam
//! pass at every light with the shader (Beams.cpp).
struct SBeam
{
  std::string Model;                  //!< Model: the cone (objects/indoor/lights/beam.cgf), '\' as '/'
  float fStartRadius = 0.1f;          //!< StartRadius: the radius at the light
  float fEndRadius = 1;               //!< EndRadius: the radius at the far end
  float fLength = 1;                  //!< Length: from the light to the far end
  float StartColor[4] = { 1, 1, 1, 1 };   //!< StartColor (r g b a): at the light
  float EndColor[4] = { 1, 1, 1, 0 };     //!< EndColor (r g b a): at the far end (alpha below 0 fades it out before)
  int nLightStyle = 0;                //!< LightStyle: read, not used (the original's beams draw the colours as given)
};

//! A layer of a sky shader drawn over its box: a "Layer" block after the first one (the box) of its
//! script that names a Map (CloudsSky1).
struct SSkyLayer
{
  std::string Map;                    //!< Map: the texture ('\' as '/')
  //! TexGen 'HW_ObjectLinear': u = PlaneS . (x, y, z, 1), v = PlaneT . (x, y, z, 1) of the position
  //! around the camera in the sky's axes (DrawSkyItem); a component 'time k' is k *
  //! ITimer::GetCurrTime() (TimeS, TimeT).
  float PlaneS[4] = { 1, 0, 0, 0 }, PlaneT[4] = { 0, 1, 0, 0 };
  float TimeS[4] = {}, TimeT[4] = {};
  int nBlend = 0;                     //!< Blend 'src dst' as GS_BLSRC_* | GS_BLDST_*; 0 without one
};

//! A sky shader of the scripts: a "Shader '<name>'" block with a "SkyBox = <name>" line.
struct SSkyShader
{
  //! The base name of its three textures <name>_5 (top), <name>_12 and <name>_34 (sides), lower case.
  std::string Box;
  std::vector<SSkyLayer> Layers;
};

//! How the colour of a lit draw class (opaque, alpha test, alpha blend) is lit.
enum class ELighting : uint8
{
  Overbright,             //!< the lightmap, or texture * 2 * (saturate(ambient / 2 + sun + light 0) + saturate(each other
                          //!< light)); per pixel for bPixelLighting with bump mapping on
  Vertex,                 //!< texture * (saturate(ambient + sun + light 0) + saturate(each other light)): modulated, not by 2
  Ambient,                //!< texture * the object's ambient colour, no lights
  Ambient2,               //!< texture * 2 * the object's ambient colour, no lights
  None,                   //!< the texture alone
  VColor,                 //!< texture * the vertex colour (white without one), no lights
};

//! The material's gloss map (EFTT_GLOSS) added, unlit: the glowing parts of the glow templates;
//! for the alpha glow ones the texture times its alpha.
enum class EGlow : uint8
{
  None,
  Glow,                   //!< + gloss map
  Pulse,                  //!< + gloss map * saturate(0.75 + 0.75 * sin(pi * t)): the game's wave of level 0.75, amplitude
                          //!< 0.75 and frequency 0.5 (TemplDecalGlowSelfIllumAnimated)
  PulseSlow,              //!< + gloss map * saturate(0.95 + 0.75 * sin(0.2 * pi * t)): level 0.95, frequency 0.1
                          //!< (TemplDecalGlowSelfIllumAnimatedSLOW)
  SelfIllum,              //!< bump mapping on: the texture unlit + 2 * gloss map; off: + gloss map
  Alpha,                  //!< + texture * its alpha: the texture's alpha is the glow mask (no gloss map)
  LitGloss,               //!< + gloss map, lit as the texture (TemplDecalLight_SpecAdd's second pass, ONE ONE: its gloss map
                          //!< times the fixed function light), with the material's highlights
};

//! A second texture layer of the game's fixed function templates, its texture moving on its own
//! (SShaderProps::fLayer2Scroll).
enum class ELayer2 : uint8
{
  None,
  AddGloss,               //!< + gloss map, unlit (TemplTextureShiftST_2T: layer 1, TexColorOp Add)
  BlendGloss,             //!< the gloss map over the texture by its alpha, lit as the texture (TemplTextureShiftST_2T_AlphaBlend:
                          //!< a second pass, SRC_ALPHA ONE_MINUS_SRC_ALPHA)
  BlendOpacity,           //!< the opacity map over the texture by its alpha (communication_island_river: a second pass)
};

//! The environment cube map (CD3D12Renderer::GetEnvCubeMap; the cube of the scene around the
//! object with SShaderProps::bAutoCube) in place of the material's texture, looked up with the view
//! vector reflected about the normal, in world space; the modes from CubeDecalAlpha on as the
//! original's vertex and pixel shaders of their templates (General.hlsl, DF2_CUBE_SHADER).
enum class EEnvMap : uint8
{
  None,
  Cube,                   //!< cube map * light
  CubeDecal,              //!< cube map * light + texture
  CubeAddSigned,          //!< cube map * light + texture - 0.5
  CubeDecalAmount,        //!< (texture + cube map * gloss map) * light: the gloss map is the reflect amount
  Glass,                  //!< the cube map seen through (refracted) and reflected, by a fresnel term, + 0.1 * texture
  GlassFixed,             //!< cube map + 0.098 * texture
  Auto,                   //!< the scene around the object rendered into a cube (EnvCubes.cpp) * light
  CubeDecalAlpha,         //!< (texture + cube map * the texture's alpha) * light (TemplReflCMDecal_ReflAmountAlpha)
  CubeModulate,           //!< cube map * texture * light (TemplReflCMDecalMod)
  Refract,                //!< the cube map seen through (the view vector refracted by the material's "Refraction")
                          //!< * (1 - k) + texture * k, k its "DiffuseAmount" (TemplRefrCM)
  Dispersion,             //!< seen through with the ratios 1.10 / 1.08 / 1.06 for red, green, blue, and reflected,
                          //!< by the fresnel term; no texture (TemplGlassDispersionCM)
  GlassPS20,              //!< TemplGlassCM_PS20: seen through (ratio 1.14) * base colour * (1 - sin t) + reflected * sin i
                          //!< + a rainbow (the texture at cos^0.18) * 0.2 * base colour * sin t + 0.2
  BumpRefract,            //!< seen through the normal map (TemplBumpRefrCM: the original's sum with the map's unsigned
                          //!< values; "BumpScale", "Refraction") * the vertex colours; no texture
  BumpGlass,              //!< BumpRefract * "RefractAmount" + reflected about the bumped normal * "ReflectAmount"
  BumpGlassFresnel,       //!< BumpRefract * (1 - F) + reflected about the bumped normal * F (the fresnel term)
};

//! What a shader is.
enum class EShaderKind
{
  Engine,                 //!< EF_LoadShader of a name that is no template
  Template,               //!< a template (a material's, a user template, EF_LoadShader of a template name)
  Material,               //!< EF_LoadShaderItem with a template name
};

//! The fixed values of a shader: what the engine reads through IShader and how it is drawn.
struct SShaderProps
{
  EF_Sort eSort = eS_Opaque;                      //!< GetSort
  int nFlags = EF_COMPILEDLAYERS;                 //!< GetFlags (EF_*)
  int nFlags2 = EF2_OPAQUE | EF2_FOGOVERLAY1;     //!< GetFlags2 (EF2_*); EF2_OPAQUE: not transparent
  int nFlags3 = 0;                                //!< GetFlags3 (EF3_*)
  int nLFlags = 0;                                //!< GetLFlags (LMF_*)
  ECull eCull = eCULL_Back;                       //!< GetCull
  EDrawClass eDrawClass = eDC_Opaque;
  ELighting eLighting = ELighting::Overbright;
  EGlow eGlow = EGlow::None;
  EEnvMap eEnvMap = EEnvMap::None;
  bool bAutoCube = false;                         //!< eEnvMap looks up the cube of the scene around the object (*EnvCM)
  bool bAmbientCube = false;                      //!< *_EnvCMAmb: the ambient part 0.75 of itself + 0.25 * the cube map
                                                  //!< reflected about the vertex normal
  float fAlphaRef = 0.0f;                       //!< alpha test reference (0..1, texture alpha >= it passes); 0: none
  float fScrollV = 0.0f;                          //!< texture v moves by fScrollV * ITimer::GetCurrTime()
  float fScrollU = 0.0f;                          //!< texture u moves by fScrollU * ITimer::GetCurrTime()
  float fRotate = 0.0f;                           //!< the texture turns by fRotate degrees a second (the material's
                                                  //!< public float "Speed" in its place with bRotateSpeedParam)
  float fRotatePivot = 0.5f;                      //!< about the point (fRotatePivot, fRotatePivot) of the texture
  bool bRotateSpeedParam = false;
  ELayer2 eLayer2 = ELayer2::None;
  float fLayer2Scroll[2] = {};                    //!< the second layer's u and v move by these * ITimer::GetCurrTime()
  bool bNoOpacity = false;                        //!< blended by the texture's and the vertices' alpha, not the material's opacity
  float fFlareSize = 0.0f;                        //!< eDC_ProcFlare: how far the glow reaches beyond the surface, in metres
  bool bLightmap = false;                         //!< combines the lightmap of lightmapped objects
  bool bStealth = false;                          //!< colour scaled by the object's "refraction" (GetColorScale)
  bool bEdgeFade = false;                         //!< TemplMuzzleFlash_Auto: twice the texture, its planes fading out seen edge on (d3d12_MuzzleFlashAuto)
  bool bPixelLighting = false;                    //!< a material of a bump, model, light diffuse or glass template: with bump
                                                  //!< mapping on it is lit per pixel, with its normal map when it has one
  bool bSpecular = false;                         //!< with bPixelLighting: a specular template (TemplBumpSpec*, TemplModelBumpSpec*)
  bool bGlossAlpha = false;                       //!< with bSpecular: the specular is times the texture's alpha (*GlossAlpha)
  ETemplateFx eFx = ETemplateFx::None;            //!< eDC_ShaderFx, eDC_ShaderFxBlend: which
  bool bBeachFaint = false;                       //!< eDC_WaterBeach: TerrainWaterBeachResearch's fainter waves (SetupBeachDraw)
  std::string Template;                           //!< materials: the template the material names (lower case)

  //! The cube of the scene around the object (EnvCubes.cpp) in place of the material's cube map.
  bool UsesAutoCube() const { return eEnvMap == EEnvMap::Auto || bAutoCube; }
  //! The cube map looked up through the material's normal map (the bump refraction templates).
  bool BumpCube() const { return eEnvMap == EEnvMap::BumpRefract || eEnvMap == EEnvMap::BumpGlass || eEnvMap == EEnvMap::BumpGlassFresnel; }
};

//! A shader of this renderer (an engine shader, a template or a material; see above).
class CShader : public IShader
{
public:
  CShader(CShaderManager *pManager, int nID, std::string Name, EShaderKind eKind, const SShaderProps &Props, CShader *pTemplate, uint64 nMaskGen);
  ~CShader() = default;
  CShader(const CShader &) = delete;
  CShader &operator=(const CShader &) = delete;

  //////////////////////////////////////////////////////////////////////////
  // IShader

  int GetID() override { return m_nID; }
  void AddRef() override { m_nRefCount++; }
  //! Counts down only; the shader stays until CShaderManager::Shutdown.
  void Release(bool bForce = false) override;
  int GetRefCount() override { return m_nRefCount; }
  //! Lower case: the engine shader's name, the template's resolved name ("templdiffuse_fp"), the
  //! material's name ("<name>_<n>" for the n-th further template of one material name).
  const char *GetName() override { return m_Name.c_str(); }
  EF_Sort GetSort() override { return m_Props.eSort; }
  int GetFlags() override { return m_Props.nFlags; }
  int GetFlags2() override { return m_Props.nFlags2; }
  int GetFlags3() override { return m_Props.nFlags3; }
  int GetRenderFlags() override { return m_nRenderFlags; }
  void SetRenderFlags(int nFlags) override { m_nRenderFlags = nFlags; }
  int GetLFlags() override { return m_Props.nLFlags; }
  int GetCull() override { return m_Props.eCull; }
  uint GetPreprocessFlags() override { return 0; }
  void SetFlags3(int Flags) override { m_Props.nFlags3 = Flags; }
  //! Nothing to reload (no scripts); true.
  bool Reload(int nFlags) override { return true; }
  //! Empty, except for a flare shader (SetCorona): one Flare element (eDATA_Flare).
  TArray<CRendElement *> *GetREs() override { return &m_REs; }
  //! Registers the template Name under TemplId (-1: a new id from EFT_USER_FIRST, written back;
  //! otherwise the id is kept). The ids are global (CShaderManager::AddUserTemplate): GetTemplate of
  //! that id gives the template on every shader. False when there is no name and no template for
  //! the id.
  bool AddTemplate(SRenderShaderResources *Res, int &TemplId, const char *Name = NULL, bool bSetPreferred = false, uint64 nMaskGen = 0) override;
  //! Does nothing: user template ids stay valid.
  void RemoveTemplate(int TemplId) override {}
  //! This shader for num below EFT_USER_FIRST (-1, -2, the system templates, EFT_HEATVISION
  //! included); the user template of that id otherwise (this shader when the id has none).
  IShader *GetTemplate(int num) override { return num < EFT_USER_FIRST ? this : GetUserTemplate(num); }
  //! An empty list.
  SEfTemplates *GetTemplates() override { return &m_Templates; }
  //! Always empty (no script parameters).
  //! The public parameters of the template (names and defaults; empty for most): the engine sets a
  //! material's values (its PublicParams) only for the names in this list.
  TArray<SShaderParam> &GetPublicParams() override { return m_PublicParams; }
  int GetTexId() override { return -1; }
  ITexPic *GetBaseTexture(int *nPass, int *nTU) override;
  unsigned int GetUsedTextureTypes(void) override { return 1 << eTT_Base; }
  //! The vertex format the draw class reads: VERTEX_FORMAT_P3F_COL4UB_TEX2F for the vertex
  //! colour classes, VERTEX_FORMAT_P3F_N_TEX2F otherwise.
  int GetVertexFormat(void) override;
  int Size(int Flags) override;
  uint64 GetGenerationMask() override { return m_nMaskGen; }
  SShaderGen *GetGenerationParams() override { return nullptr; }

  //////////////////////////////////////////////////////////////////////////
  // Renderer

  //! How elements with this shader are drawn; for a material with resources use the free function
  //! GetDrawClass(pShader, pRes) below (opacity).
  EDrawClass GetDrawClass() const { return m_Props.eDrawClass; }
  //! Overrides the draw class, e.g. eDC_Sky for the shader the engine adds a CRESky element with
  //! (sky shader names cannot be told from other names).
  void SetDrawClass(EDrawClass eClass) { m_Props.eDrawClass = eClass; }
  //! The shader to draw with for a template id (EF_AddEf nTempl, CCObject::m_nTemplId,
  //! SRendParams::nShaderTemplate): as GetTemplate, typed.
  CShader *GetDrawShader(int nTempl) { return nTempl < EFT_USER_FIRST ? this : GetUserTemplate(nTempl); }
  const SShaderProps &GetProps() const { return m_Props; }
  EShaderKind GetKind() const { return m_eKind; }
  //! True when lightmapped objects combine their lightmap ("templdiffuse_fp" and unknown templates;
  //! "templdecal" and the other kept templates draw without it).
  bool UsesLightmap() const { return m_Props.bLightmap; }
  //! The template a material was made from (NULL for engine shaders and templates).
  CShader *GetSourceTemplate() const { return m_pTemplate; }
  //! The name of the template that is used ("templdiffuse_fp"); the shader's own name if it is no material.
  const char *GetTemplateName() const { return m_pTemplate ? m_pTemplate->m_Name.c_str() : m_Name.c_str(); }
  //! Makes this a flare shader: the corona and a Flare element in GetREs.
  void SetCorona(const SCorona &Corona);
  //! The corona of a flare shader; NULL for the others.
  const SCorona *GetCorona() const { return m_pCorona.get(); }
  //! The Flare element of a flare shader; NULL for the others.
  CRendElement *GetFlare() const { return m_pFlare.get(); }
  //! Gives a light shader its beam.
  void SetBeam(const SBeam &Beam) { m_pBeam = std::make_unique<SBeam>(Beam); }
  //! The beam of a light shader; NULL for the others.
  const SBeam *GetBeam() const { return m_pBeam.get(); }

private:
  CShader *GetUserTemplate(int nTempl);

  CShaderManager *m_pManager;
  int m_nID;
  int m_nRefCount = 0;
  int m_nRenderFlags = 0;
  friend class CShaderManager;              // fills m_PublicParams
  std::string m_Name;
  EShaderKind m_eKind;
  SShaderProps m_Props;
  CShader *m_pTemplate;                       //!< materials: their template
  uint64 m_nMaskGen;
  TArray<CRendElement *> m_REs;
  TArray<SShaderParam> m_PublicParams;
  SEfTemplates m_Templates;
  std::unique_ptr<SCorona> m_pCorona;
  std::unique_ptr<CRendElement> m_pFlare;     //!< the Flare element of a flare shader (in m_REs)
  std::unique_ptr<SBeam> m_pBeam;
};

//! All shaders, the user templates and the light materials (one per renderer, CD3D12Renderer::m_pShaders).
class CShaderManager
{
public:
  CShaderManager();
  ~CShaderManager();
  CShaderManager(const CShaderManager &) = delete;
  CShaderManager &operator=(const CShaderManager &) = delete;

  //! Makes the default shader and the default light material.
  bool Init();
  //! Releases every shader (IRenderer::Release).
  void Shutdown();

  //! The default shader, "default" (ID 0; EF_LoadShader of NULL, "" and "Default"; draw class
  //! eDC_Opaque). Other names that are neither engine shaders nor templates get a shader of their
  //! own with draw class eDC_NoDraw.
  CShader *GetDefault();

  //////////////////////////////////////////////////////////////////////////
  // IRenderer (RendererShaders.cpp)

  //! EF_LoadShader: the shader of this name (not case sensitive; NULL and "" give the default
  //! shader), made on first use; one reference more per call. A template name gives the template.
  CShader *LoadShader(const char *szName, EShClass eClass, int nFlags, uint64 nMaskGen);
  //! EF_LoadShaderItem: the material shader of (szName, the template szTemplate resolves to), or
  //! LoadShader(szName) without a template name, with one reference more; and new resources made
  //! from pRes (reference count 1; NULL without pRes) with a light material of the renderer
  //! (pRes->m_LMaterial is set to the default one when it is NULL) and, unless the shader is
  //! eDC_NoDraw, their diffuse texture loaded.
  SShaderItem LoadShaderItem(const char *szName, EShClass eClass, bool bShare, const char *szTemplate, int nFlags, SInputShaderResources *pRes, uint64 nMaskGen);
  //! EF_CopyShader: a new shader with the values of pSrc (one reference), found only by its ID.
  CShader *CopyShader(IShader *pSrc);
  //! EF_RegisterTemplate: the template szName under nTemplId; false if the id has one and !bReplace.
  bool RegisterTemplate(int nTemplId, const char *szName, bool bReplace);
  void SetGlobalTemplateId(int nTemplId) { m_nGlobalTemplate = nTemplId; }
  //! -2 until SetGlobalTemplateId is called.
  int GetGlobalTemplateId() const { return m_nGlobalTemplate; }

  //////////////////////////////////////////////////////////////////////////
  // Shaders

  //! The shader with this ID (IShader::GetID; EF_AddSpriteToScene, EF_DrawEfForNum, ...); NULL if none.
  CShader *GetByID(int nID) const;
  //! The shader of this name (engine shaders and templates first, then materials); NULL if none.
  //! No reference is added.
  CShader *Find(const char *szName) const;
  //! Number of shaders (IDs are 0 .. GetCount() - 1).
  int GetCount() const { return (int)m_Shaders.size(); }
  //! The template shader for a template name (as a material names it), made on first use.
  CShader *GetTemplate(const char *szName, uint64 nMaskGen);
  //! The sky box and layers a sky shader's script gives it ("SkyBox = Skys/Training/Box" in a
  //! "Shader '01_Training'" block of Shaders/Scripts/CryShaders/*.csl, read on first use); NULL when
  //! the shader has no sky box.
  const SSkyShader *FindSky(const char *szShader);
  //! The corona a flare shader's script gives it ("ClientEffect 'Corona'" in its "Shader" block of
  //! the same scripts); NULL when the shader has none.
  const SCorona *FindCorona(const char *szShader);
  //! The beam a light shader's script gives it ("ClientEffect 'Beam'" in its "Shader" block of the
  //! same scripts); NULL when the shader has none.
  const SBeam *FindBeam(const char *szShader);

  //! IShader::AddTemplate: registers the template szName under nTemplId (-1: a new id from
  //! EFT_USER_FIRST up); returns the id, -1 when there is nothing to register.
  int AddUserTemplate(int nTemplId, const char *szName, uint64 nMaskGen);
  //! The template registered for a user template id; NULL if none.
  CShader *GetUserTemplate(int nTemplId) const;

  //////////////////////////////////////////////////////////////////////////
  // Resources

  //! Loads the texture of slot nSlot (EFTT_*) of the resources if it is named and not loaded yet
  //! (EF_LoadShaderItem loads EFTT_DIFFUSE and the slots its shader draws with); returns it, NULL
  //! when there is none.
  CTexture *LoadResourceTexture(SRenderShaderResources *pRes, int nSlot);
  //! The normal map of the resources: EFTT_NORMALMAP, else EFTT_BUMP; NULL when neither is a 2D
  //! texture that is loaded.
  CTexture *LoadNormalMap(SRenderShaderResources *pRes);

  //////////////////////////////////////////////////////////////////////////
  // Light materials (SLightMaterial; owned here, never deleted before Shutdown)

  //! The light material every material without one gets (white, shininess 10).
  SLightMaterial *GetDefaultLightMaterial() { return m_LightMaterials.empty() ? nullptr : m_LightMaterials[0].get(); }
  //! EF_GetLightMaterial: the light material of this name (not case sensitive), made white if new.
  SLightMaterial *GetLightMaterial(const char *szName);
  //! SLightMaterial::mfGet: the light material of this name; NULL if none.
  SLightMaterial *FindLightMaterial(const char *szName) const;
  //! SLightMaterial::mfAdd: one equal to pCompare (its name szName) or, without pCompare, the one of
  //! that name, made if new.
  SLightMaterial *AddLightMaterial(const char *szName, const SLightMaterial *pCompare);
  //! A light material of the renderer with the values of pLM, which may be the engine's own (one on
  //! its stack); pLM itself if it is one of ours, the default one for NULL.
  SLightMaterial *OwnLightMaterial(const SLightMaterial *pLM);

private:
  CShader *Create(std::string Name, EShaderKind eKind, const SShaderProps &Props, CShader *pTemplate, uint64 nMaskGen);
  CShader *LoadMaterial(const char *szName, const char *szTemplate, uint64 nMaskGen);
  SLightMaterial *NewLightMaterial(const char *szName, const SLightMaterial *pValues);
  //! One reference to the texture of a material texture name (tried as named, then in the
  //! material's folder); the placeholder of the first try when no file is found.
  CTexture *LoadTextureFile(const char *szName, const char *szPath, const SShaderTexUnit &TU);
  void AddAttenuationTextures(SRenderShaderResources *pRes);
  //! Loads the textures besides the diffuse one that the drawing of pSH reads (the normal map, the
  //! gloss map, the cube map), so that the first frame drawing the material does not read files.
  void LoadDrawnTextures(const CShader *pSH, SRenderShaderResources *pRes);
  //! Reads the skies, the coronas and the beams of the shader scripts, once.
  void ReadScripts();

  std::vector<std::unique_ptr<CShader>> m_Shaders;              //!< indexed by ID
  std::unordered_map<std::string, CShader *> m_ByName;          //!< engine shaders and templates by name
  std::unordered_map<std::string, CShader *> m_TemplateNames;   //!< templates by the name a material gives (lower case, mask)
  std::unordered_map<std::string, CShader *> m_Materials;       //!< materials by name and template
  std::unordered_map<std::string, int> m_MaterialCounts;        //!< shaders made per material name
  std::unordered_map<int, CShader *> m_UserTemplates;           //!< user template ids
  std::vector<std::unique_ptr<SLightMaterial>> m_LightMaterials; //!< [0] the default one
  std::unordered_map<std::string, SSkyShader> m_Skies;          //!< FindSky: sky by shader name
  std::unordered_map<std::string, SCorona> m_Coronas;           //!< FindCorona: corona by shader name
  std::unordered_map<std::string, SBeam> m_Beams;               //!< FindBeam: beam by shader name
  bool m_bScriptsRead = false;
  CShader *m_pDefault = nullptr;
  int m_nNextUserTemplate = EFT_USER_FIRST;
  int m_nGlobalTemplate = -2;
  int m_nLastResourceID = 0;
};

//////////////////////////////////////////////////////////////////////////
// Drawing an element. pShader is the shader to draw with (CShader::GetDrawShader of the element's
// shader and template), pRes the element's resources (NULL for most engine shaders). All accept NULL.

//! The draw class of an element: the shader's, except that a material with an opacity other than 1
//! on a template sorted with the opaque surfaces (sort eS_Opaque or lower; class eDC_Opaque or
//! eDC_AlphaTest) is drawn eDC_AlphaBlend, as SShaderItem::GetSort moves it to eS_Banner.
//! eDC_NoDraw for a NULL shader.
EDrawClass GetDrawClass(const CShader *pShader, const SRenderShaderResources *pRes);
//! Whether an element of a vertex coloured class (eDC_AlphaTestVColor, eDC_VColor: vegetation) is
//! drawn blended (SRCALPHA, INVSRCALPHA, alpha above 0, no depth writes) instead: its material's
//! opacity is below 1, or, alpha tested, the object fades in or out (m_Color.a below 1). The class
//! and its vertex colours (the engine's lighting of the plant) stay.
bool IsBlendedVColor(EDrawClass eDC, const SRenderShaderResources *pRes, const CCObject *pObj);
//! The diffuse texture (EFTT_DIFFUSE); for an animated one ("name##01_04(.25).dds") the frame
//! of ITimer::GetCurrTime(). NULL when the material has none: draw white (an element whose
//! leaf buffer chunk carries a texture of its own, CLeafBuffer::SetShader, uses that).
CTexture *GetDiffuseTexture(const SRenderShaderResources *pRes);
//! The loaded texture of slot nSlot (EFTT_*), animated as GetDiffuseTexture; NULL when the slot is
//! not loaded (CShaderManager::LoadResourceTexture).
CTexture *GetResourceTexture(const SRenderShaderResources *pRes, int nSlot);
//! The material's opacity, 0..1 (1 without resources): blended draws use texture alpha * opacity.
float GetOpacity(const SRenderShaderResources *pRes);
//! Alpha test reference 0..1 (texture alpha >= it passes), 0 for no alpha test: the material's
//! SBaseShaderResources::m_AlphaRef where it is above 0, rounded down to 1 / 255 steps, as the
//! original tests every material by it; else the template's (128 / 255 for vegetation and the alpha
//! tested decals). The SDK's material loader (MakMatInfoFromMAT_ENTITY.h) takes it from the CGF
//! material's alpharef with the Cry shader flag (MTLFLAG_CRYSHADER), else from Dyn_SlidingFriction;
//! the original tests by it either way (the manhole of Training's ruin: 0.09 without the flag, its
//! texture's corners cut).
float GetAlphaRef(const CShader *pShader, const SRenderShaderResources *pRes);
//! Face culling (eCULL_Back for most, eCULL_None for vegetation leaves, additive decals,
//! holograms, ...); eCULL_None also for a two-sided material (bit 0x2 of m_ResFlags, the
//! two-sided flag of the CGF material, which the SDK's loader copies there).
ECull GetCullMode(const CShader *pShader, const SRenderShaderResources *pRes);
//! The front diffuse colour of the material's light material (white without one): the material
//! colour of lit draws (characters use it as the diffuse material colour).
CFColor GetMaterialColor(const SRenderShaderResources *pRes);
//! The material's public float named szName (its shader params, case ignored), fDefault without it.
float GetResourceFloat(const SRenderShaderResources *pRes, const char *szName, float fDefault);
//! The material's public colour or vector named szName (rgb, xyz), vDefault without it.
void GetResourceColor(const SRenderShaderResources *pRes, const char *szName, const float vDefault[3], float vOut[3]);
//! Texture transform of slot nSlot, the rows of a 2x3 matrix: u' = dot(float3(u, v, 1), vRow0), v' =
//! dot(.., vRow1). The material's tiling and offset (SEfTexModificator; a tiling of 0 counts as 1),
//! then the template's motion: turned about its pivot (fRotate degrees a second) and moved
//! (fScrollU, fScrollV, times ITimer::GetCurrTime()). Identity without resources, texture or motion.
void GetTexTransform(const CShader *pShader, const SRenderShaderResources *pRes, int nSlot, float vRow0[3], float vRow1[3]);
//! Colour factor of the template for an object: 1, except for "mutantstealth_fp" (a cloaked
//! character: texture * 2 * (int)(refraction * 255) / 255, added) where "refraction" is the float
//! of the object's m_ShaderParams (12 / 255 without it).
float GetColorScale(const CShader *pShader, const CCObject *pObj);
