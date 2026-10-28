//////////////////////////////////////////////////////////////////////
//
//	File: Scene.h
//	Description: The scene of a frame: render objects (CCObject), the render
//	items collected between EF_StartEf and EF_EndEf3D, the lights, the
//	particle sprites, the fog volumes and the screen effects.
//
//	EF_AddEf (also called by the leaf buffers: AddRE, AddRenderElements,
//	Render) adds a render item. EF_EndEf3D prepares the items (CheckUpdate
//	of their leaf buffers with the vertex format the draw needs, the skinning
//	of characters, the matrices of the objects), sorts them and hands the
//	three lists to CD3D12Renderer::DrawItems (Draw3D.cpp), every time and in
//	the order eRL_Opaque, eRL_Nearest, eRL_Blended.
//
//	Scenes nest: a render element drawn inside EF_EndEf3D may make the 3D
//	engine render a frame of its own (EF_StartEf .. EF_EndEf3D, e.g. the
//	water reflection). Every open scene has its own items, lights and
//	sprites, so a nested scene leaves the one being drawn as it was.
//	EFQ_RecurseLevel is the number of open scenes: 0 outside, 1 for the main
//	view (also while it is drawn), 2 inside a scene drawn from it.
//
//	Render objects: temporary objects (EF_GetObject(true, -1)) are valid
//	for the frame; the pool takes them back at the first EF_StartEf (or
//	BeginFrame) of the next frame. Permanent objects (EF_GetObject(false, n))
//	stay until RemovePermanent; the memory of every object stays with the
//	pool until Shutdown, so a pointer the engine still holds never dangles.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include <array>
#include <memory>
#include <span>
#include <vector>

#include "Shader.h"

class CREScreenProcess;

//! The lists of a frame, drawn in this order.
enum ERenderList
{
  eRL_Opaque = 0,                 //!< opaque and alpha tested, sorted by pass, sort group, shader, resources
  eRL_Nearest,                    //!< first person weapon (FOB_NEAREST / eS_Nearest): opaque first, then blended far to near
  eRL_Blended,                    //!< blended, far to near; then the screen elements (flares, screen quads, screen effects)
  eRL_Num
};

//! One element to draw.
struct SRenderItem
{
  CRendElement *pRE = nullptr;
  CCObject *pObj = nullptr;                 //!< never nullptr (the scene's identity object when EF_AddEf got none)
  CShader *pShader = nullptr;               //!< the shader to draw with (the template already applied)
  SRenderShaderResources *pRes = nullptr;   //!< may be nullptr
  IShader *pStateShader = nullptr;          //!< EF_AddEf's efState, may be nullptr
  int nFog = 0;                             //!< fog volume ID (0: none), see CScene::GetFogVolume
  int nSort = 0;                            //!< eS_* sort group | EFSLIST_* bits: EF_AddEf's nSort, the shader's when 0
  int nTempl = -1;                          //!< EF_AddEf's template ID
  uint64 nSortKey = 0;                      //!< order inside the list (ascending), set in EF_EndEf3D
  EDrawClass eDrawClass = eDC_NoDraw;       //!< the shader's draw class; alpha blended when the resources' opacity is below 1
  ERenderList eList = eRL_Opaque;           //!< the list the item is drawn in
  float fDistance = 0;                      //!< from the camera (CRendElement::mfDistanceToCameraSquared), set in EF_EndEf3D
  int nOrder = 0;                           //!< position in the scene (EF_AddEf order)
};

//! A particle sprite of EF_AddSpriteToScene (camera facing quads in world space).
struct SSceneSprite
{
  int nShaderID = 0;              //!< EF_AddSpriteToScene's Ef (IShader::GetID)
  CCObject *pObj = nullptr;       //!< m_NumCM the texture ID, m_RenderState the GS_* blending, m_AmbColor added to the vertex colour
  int nFog = 0;                   //!< fog volume ID
  int nFirstVert = 0, nVerts = 0;     //!< into CScene::GetSpriteVerts()
  int nFirstIndex = 0, nIndices = 0;  //!< into CScene::GetSpriteIndices(), triangle list relative to nFirstVert
};

//! A fog volume of EF_RegisterFogVolume.
struct SFogVolume
{
  float fMaxFogDist = 0;
  float fFogLayerZ = 0;
  CFColor Color = CFColor(0, 0, 0, 1);
  bool bCaustics = false;
};

//! What the screen effects (CREScreenProcess) ask for in this frame.
struct SScreenFxState
{
  bool bNightVision = false;      //!< NightVision Active
  bool bFlashBang = false;        //!< FlashBang Active
  float fFlashBang = 1;           //!< FlashBang parameter 24: the part of the flash still to come, 1 to 0
  float fFlashPos[2] = {};        //!< FlashPosX, FlashPosY (virtual 800 x 600 screen)
  float fFlashSize[2] = {};       //!< FlashSizeX, FlashSizeY
  bool bScreenFade = false;       //!< ScreenFade Active
  float fFadeAlpha = 0;           //!< alpha of the black quad over the screen, 0 to 1
  bool bScreenBlur = false;       //!< ScreenBlur Active
  float fBlurAmount = 0;
  float fBlurColor[3] = {};
};

//! Unused CRendElement::m_Flags bit: the element was made by EF_CreateRE for the engine, and
//! CScene::Shutdown destroys it if the engine left it alive.
constexpr uint nREFlag_EngineOwned = 0x40000000;

//! CRendElement::mfDraw takes an SShader, a type the SDK only declares: this renderer passes its
//! CShader in it (the element's item shader).
inline SShader *RE_ShaderArg(IShader *pShader) { return reinterpret_cast<SShader *>(pShader); }
inline IShader *RE_Shader(SShader *pShader) { return reinterpret_cast<IShader *>(pShader); }

//! EF_CreateRE: a new element of the SDK class of that type (marked nREFlag_EngineOwned), nullptr
//! for the types without one (Beam, Prefab, Flare, Ocean, ...).
CRendElement *RE_Create(EDataType edt);
//! A render element of a type without an SDK class (flares and beams of the shaders): a bare
//! CRendElement of that type, destroyed by Release. Not marked: its creator releases it.
CRendElement *RE_CreateGeneric(EDataType edt);

//! The value of a wave of CCObject::m_Waves at time t: level + amp * f(t * freq + phase) with
//! f the wave form over one period (eWF_Sin: sin(2 pi x)).
float EvalWave(const SWaveForm2 &wf, float t);

class CScene
{
public:
  CScene();
  ~CScene();
  CScene(const CScene &) = delete;
  CScene &operator=(const CScene &) = delete;

  bool Init();
  //! Frees the object pools, the tables of CCObject and the render elements the engine left
  //! (EF_CreateRE elements; the leaf buffers and shaders free their own).
  void Shutdown();

  //! At the start of a frame (optional: EF_StartEf and EF_GetObject do it on their own): closes
  //! a scene left open and gives the temporary objects and waves of the last frame back.
  void BeginFrame();

  //////////////////////////////////////////////////////////////////////////
  // Scene (Scene.cpp; the IRenderer EF_* methods forward here)

  void StartEf();
  void AddEf(int nFog, CRendElement *pRE, IShader *pEf, SRenderShaderResources *pRes, CCObject *pObj, int nTempl, IShader *pStateEf, int nSort);
  void EndEf3D(int nFlags);
  CCObject *AddSprite(int nShaderID, int nPts, const SColorVert *pVerts, CCObject *pObj, const byte *pInds, int nInds, int nFog);

  //! Render items added in this frame so far (EFQ_NumRenderItems).
  int GetNumItems() const { return m_nFrameItems; }
  //! Open scenes (EFQ_RecurseLevel).
  int GetDepth() const { return m_nDepth; }
  //! True while EF_EndEf3D draws.
  bool IsDrawing() const { return m_nDrawing > 0; }

  //! The particle sprites of the scene being drawn (cleared after its EF_EndEf3D).
  std::span<const SSceneSprite> GetSprites() const { return Cur().Sprites; }
  std::span<const SColorVert> GetSpriteVerts() const { return Cur().SpriteVerts; }
  std::span<const ushort> GetSpriteIndices() const { return Cur().SpriteIndices; }

  //////////////////////////////////////////////////////////////////////////
  // Lights (Scene.cpp): the lights of the current scene, numbered by EF_ADDDlight

  void AddLight(CDLight *pLight);
  void ClearLights();
  //! The n-th light (CDLight::m_Id == n), nullptr past the end (EFQ_LightSource).
  CDLight *GetLight(int n);
  int GetNumLights() const { return (int)Cur().Lights.size(); }
  std::span<CDLight *const> GetLights() const { return Cur().Lights; }
  //! The DLF_FAKE lights (m_Id -1: only flares or beams).
  std::span<CDLight *const> GetFakeLights() const { return Cur().FakeLights; }
  //! The sun (DLF_SUN) of the current scene, nullptr if none was added.
  CDLight *GetSun() const;
  //! Unit vector from the camera towards the sun ((0, 0, 1) without a sun).
  Vec3 GetSunDirection() const;
  //! The sun's colour (CDLight::m_Color; black without a sun).
  CFColor GetSunColor() const;
  //! EF_UpdateDLight: m_Orientation and m_TextureMatrix of a DLF_PROJECT light; always false.
  static bool UpdateLight(CDLight *pLight);

  //////////////////////////////////////////////////////////////////////////
  // Fog volumes (Scene.cpp)

  //! IDs from 1 in the order of registration; nIndex >= 1 changes that volume.
  int RegisterFogVolume(float fMaxFogDist, float fFogLayerZ, const CFColor &Color, int nIndex, bool bCaustics);
  //! nullptr for 0 and unknown IDs.
  const SFogVolume *GetFogVolume(int nId) const;
  //! Forgets the volumes (a new level registers its own; call at PreLoad).
  void ResetFogVolumes() { m_FogVolumes.clear(); }

  //////////////////////////////////////////////////////////////////////////
  // Render objects (RenderObjects.cpp)

  //! EF_GetObject(true, -1): a temporary object, Init()ed, valid for this frame.
  CCObject *GetTempObject();
  //! EF_GetObject(false, n): the permanent object n, a new one for n < 0 or a free n.
  CCObject *GetPermanentObject(int nId);
  //! CCObject::RemovePermanent: the slot is free again (the memory stays with the pool).
  void RemovePermanent(CCObject *pObj);
  //! True for a permanent object of the pool that RemovePermanent freed.
  bool IsRemovedPermanent(const CCObject *pObj) const;
  //! The identity object of items added without one (not handed to the engine).
  CCObject *GetDefaultObject() { return m_pDefaultObject.get(); }
  //! CCObject::AddWaves: two new waves of CCObject::m_Waves (indices from 1 every frame).
  void AddWaves(CCObject *pObj, SWaveForm2 **ppWaves);
  //! The bending waves of an object (m_NumWFX / m_NumWFY, a union with m_nLod and m_nTemplId):
  //! false unless AddWaves gave the object its waves in this frame. Evaluate them with EvalWave
  //! at ITimer::GetCurrTime(); m_fBending scales the bending.
  bool GetWaves(const CCObject *pObj, const SWaveForm2 **ppX, const SWaveForm2 **ppY) const;
  //! Changes whenever the camera of the matrices may have changed (every scene and after a
  //! nested scene); CCObject::m_VPMatrixFrame holds the value its matrices were derived with.
  short GetMatrixSerial() const { return m_nMatrixSerial; }

  //////////////////////////////////////////////////////////////////////////
  // Screen effects (RenderElements.cpp)

  void RegisterScreenProcess(CREScreenProcess *pRE);
  void UnregisterScreenProcess(CREScreenProcess *pRE);
  //! Runs the FlashBang and ScreenFade timing of the screen effect elements (once per frame:
  //! EF_EndEf3D of the main view calls it).
  void UpdateScreenFx();
  //! The effects of the (first) screen effect element; all off with r_DisableSfx.
  SScreenFxState GetScreenFx() const;

private:
  struct SSceneSlot
  {
    std::vector<SRenderItem> Items;
    std::array<std::vector<SRenderItem>, eRL_Num> Lists;
    std::vector<CDLight *> Lights;
    std::vector<CDLight *> FakeLights;
    std::vector<SSceneSprite> Sprites;
    std::vector<SColorVert> SpriteVerts;
    std::vector<ushort> SpriteIndices;

    void ClearItems();
  };
  static constexpr int MAX_SCENE_DEPTH = 8;

  //! The slot of the current scene; outside a scene the one of the main view.
  SSceneSlot &Cur() { return m_Slots[(m_nDepth > 0 ? m_nDepth : 1) - 1]; }
  const SSceneSlot &Cur() const { return m_Slots[(m_nDepth > 0 ? m_nDepth : 1) - 1]; }
  void CheckNewFrame();
  void ResetFrameObjects();
  void BumpMatrixSerial();
  //! mfPrepare, CheckUpdate, skinning, matrices and distances of the items; returns their number.
  int PrepareItems(SSceneSlot &Slot);
  void BuildLists(SSceneSlot &Slot, int nItems, bool bSort);
  void CheckCorona(CDLight *pLight);
  void DestroyEngineElements();

  std::array<SSceneSlot, MAX_SCENE_DEPTH> m_Slots;
  int m_nDepth = 0;               //!< open scenes
  int m_nDrawing = 0;             //!< EF_EndEf3D calls in progress
  int m_nOverflow = 0;            //!< scenes started beyond MAX_SCENE_DEPTH (ignored)
  int m_nFrameItems = 0;
  int m_nResetFrame = -1;         //!< frame ID of the last ResetFrameObjects
  int m_nScreenFxFrame = -1;      //!< frame ID of the last UpdateScreenFx
  short m_nMatrixSerial = 0;
  bool m_bShutdown = false;

  // RenderObjects.cpp
  struct SPermanent
  {
    std::unique_ptr<CCObject> pObj;
    bool bUsed = false;
  };
  std::vector<std::unique_ptr<CCObject>> m_TempObjects;
  int m_nTempUsed = 0;
  std::vector<SPermanent> m_Permanent;
  std::unique_ptr<CCObject> m_pDefaultObject;
  std::vector<const CCObject *> m_WaveOwners;     //!< the object of each wave of CCObject::m_Waves

  std::vector<SFogVolume> m_FogVolumes;           //!< ID n at [n - 1]
  std::vector<CREScreenProcess *> m_ScreenProcesses;
  std::vector<CCObject *> m_Skinned;              //!< scratch of PrepareItems
};
