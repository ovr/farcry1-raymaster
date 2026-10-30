// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Renderer.h
//	Description: CD3D12Renderer, the IRenderer of the Direct3D 12 renderer.
//
//	The methods are declared by the file that implements them (the
//	vtable order is IRenderer's, whatever the order here):
//	  Renderer.cpp          - window, device, frame, camera, matrices, queries
//	  Renderer2D.cpp        - render states, 2D, lines, text, immediate drawing
//	  RendererTextures.cpp  - textures
//	  RendererShaders.cpp   - shaders and templates
//	  RendererBuffers.cpp   - vertex, index and leaf buffers
//	  Scene.cpp             - render objects, render elements, lights, EF_StartEf .. EF_EndEf3D
//	  Draw3D.cpp            - drawing the render items of EF_EndEf3D (no IRenderer methods)
//	  Upscale.cpp           - the upscaler of the main view (no IRenderer methods)
//	  AmbientOcclusion.cpp  - the ambient occlusion of the main view (no IRenderer methods)
//	The state of a subsystem lives in its own class (m_pTextures, m_pShaders, m_pScene, ...).
//	All calls come from one thread.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include "D3D12Pipeline.h"
#include "Upscaler.h"
#include "AmbientOcclusion.h"
#include "Shaders/Constants.h"

struct SDisplayInfo;
enum class EDisplayOutput;

class CD3D12Device;
class CD3D12Upload;
class CD3D12Texture;
class CD3D12Descriptors;
class CD3D12Pipeline;
class CD3D12Compute;
class CTexture;
class CTextureManager;
class CShaderManager;
class CScene;
struct SRenderItem;
struct SScreenFxState;

class CD3D12Renderer : public IRenderer
{
public:
  CD3D12Renderer();
  ~CD3D12Renderer();

  //! The console variables (PackageRenderConstructor, before SetType and Init).
  void RegisterVariables();
  //! The variables of the original renderer this one does not read (ConsoleVariables.cpp).
  void RegisterOtherVariables();
  //! r_VolumetricFog (one of them): the fog volume passes are drawn.
  bool IsVolumetricFogOn();
  //! r_ScopeLens_fx (one of them): the glass of the scopes is drawn.
  bool IsScopeLensOn();
  //! r_HDRRendering (one of them) not 0: the main view is drawn through the HDR pipeline (Hdr.cpp).
  bool IsHdrOn();

  //////////////////////////////////////////////////////////////////////////
  // Renderer.cpp

  virtual WIN_HWND Init(int x,int y,int width,int height,unsigned int cbpp, int zbpp, int sbits, bool fullscreen,WIN_HINSTANCE hinst, WIN_HWND Glhwnd=0, WIN_HDC Glhdc=0, WIN_HGLRC hGLrc=0, bool bReInit=false) override;
  virtual bool SetCurrentContext(WIN_HWND hWnd) override;
  virtual bool CreateContext(WIN_HWND hWnd, bool bAllowFSAA=false) override;
  virtual bool DeleteContext(WIN_HWND hWnd) override;
  virtual int GetFeatures() override;
  virtual int GetMaxTextureMemory() override;
  virtual void ShutDown(bool bReInit=false) override;
  virtual int EnumDisplayFormats(TArray<SDispFormat>& Formats, bool bReset) override;
  virtual bool ChangeResolution(int nNewWidth, int nNewHeight, int nNewColDepth, int nNewRefreshHZ, bool bFullScreen) override;
  virtual void Release() override;
  virtual void FreeResources(int nFlags) override;
  virtual void RefreshResources(int nFlags) override;
  virtual void PreLoad (void) override;
  virtual void PostLoad (void) override;
  virtual void BeginFrame (void) override;
  virtual void Update (void) override;
  virtual void ShareResources( IRenderer *renderer ) override;
  virtual void GetViewport(int *x, int *y, int *width, int *height) override;
  virtual void SetViewport(int x=0, int y=0, int width=0, int height=0) override;
  virtual void SetScissor(int x=0, int y=0, int width=0, int height=0) override;
  virtual void MakeCurrent() override;
  virtual void CheckError(const char *comment) override;
  virtual void SetCamera(const CCamera &cam) override;
  virtual const CCamera& GetCamera() override;
  virtual bool SetGammaDelta(const float fGamma) override;
  virtual bool ChangeDisplay(unsigned int width,unsigned int height,unsigned int cbpp) override;
  virtual void ChangeViewport(unsigned int x,unsigned int y,unsigned int width,unsigned int height) override;
  virtual int SetPolygonMode(int mode) override;
  virtual int GetWidth() override;
  virtual int GetHeight() override;
  virtual void GetMemoryUsage(ICrySizer* Sizer) override;
  virtual void ScreenShot(const char *filename=NULL) override;
  virtual int GetColorBpp() override;
  virtual int GetDepthBpp() override;
  virtual int GetStencilBpp() override;
  virtual void ProjectToScreen( float ptx, float pty, float ptz, float *sx, float *sy, float *sz ) override;
  virtual int UnProject(float sx, float sy, float sz, float *px, float *py, float *pz, const float modelMatrix[16], const float projMatrix[16], const int viewport[4]) override;
  virtual int UnProjectFromScreen( float sx, float sy, float sz, float *px, float *py, float *pz) override;
  virtual void GetModelViewMatrix(float *mat) override;
  virtual void GetModelViewMatrix(double *mat) override;
  virtual void GetProjectionMatrix(double *mat) override;
  virtual void GetProjectionMatrix(float *mat) override;
  virtual Vec3 GetUnProject(const Vec3 &WindowCoords,const CCamera &cam) override;
  virtual void RenderToViewport(const CCamera &cam, float x, float y, float width, float height) override;
  virtual void *EF_Query(int Query, int Param=0) override;
  virtual int GetPolyCount() override;
  virtual void GetPolyCount(int &nPolygons,int &nShadowVolPolys) override;
  virtual void SetClearColor(const Vec3 & vColor) override;
  virtual int GetFrameID(bool bIncludeRecursiveCalls=true) override;
  virtual void MakeMatrix(const Vec3 & pos, const Vec3 & angles,const Vec3 & scale, Matrix44* mat) override;
  virtual void EnableVSync(bool enable) override;
  virtual void PushMatrix() override;
  virtual void RotateMatrix(float a,float x,float y,float z) override;
  virtual void RotateMatrix(const Vec3 & angels) override;
  virtual void TranslateMatrix(float x,float y,float z) override;
  virtual void ScaleMatrix(float x,float y,float z) override;
  virtual void TranslateMatrix(const Vec3 &pos) override;
  virtual void MultMatrix(float * mat) override;
  virtual void LoadMatrix(const Matrix44 *src=0) override;
  virtual void PopMatrix() override;
  virtual void ResetToDefault() override;
  virtual void ClearDepthBuffer() override;
  virtual void ClearColorBuffer(const Vec3 vColor) override;
  virtual void ReadFrameBuffer(unsigned char * pRGB, int nSizeX, int nSizeY, bool bBackBuffer, bool bRGBA, int nScaledX=-1, int nScaledY=-1) override;
  virtual char GetType() override;
  virtual char* GetVertexProfile(bool bSupportedProfile) override;
  virtual char* GetPixelProfile(bool bSupportedProfile) override;
  virtual void SetType(char type) override;
  virtual unsigned int MakeSprite(float object_scale, int tex_size, float angle, IStatObj * pStatObj, uchar * pTmpBuffer, uint def_tid) override;
  virtual unsigned int Make3DSprite(int nTexSize, float fAngleStep, IStatObj * pStatObj) override;
  virtual ShadowMapFrustum * MakeShadowMapFrustum(ShadowMapFrustum * lof, ShadowMapLightSource * pLs, const Vec3 & obj_pos, list2<IStatObj*> * pStatObjects, int shadow_type) override;
  virtual void DrawObjSprites(list2<CStatObjInst*> *pList, float fMaxViewDist, CObjManager *pObjMan) override;
  virtual void EnableSwapBuffers(bool bEnable) override;
  virtual WIN_HWND GetHWND() override;
  virtual void OnEntityDeleted(IEntityRender * pEntityRender) override;
  virtual int EnumAAFormats(TArray<SAAFormat>& Formats, bool bReset) override;
  virtual int CreateRenderTarget (int nWidth, int nHeight, ETEX_Format eTF) override;
  virtual bool DestroyRenderTarget (int nHandle) override;
  virtual bool SetRenderTarget (int nHandle) override;
  virtual float EF_GetWaterZElevation(float fX, float fY) override;
  virtual void EF_PolygonOffset(bool bEnable, float fFactor, float fUnits) override;

  //////////////////////////////////////////////////////////////////////////
  // Renderer2D.cpp

  virtual void DrawTriStrip(CVertexBuffer *src, int vert_num=4) override;
  virtual void *GetDynVBPtr(int nVerts, int &nOffs, int Pool) override;
  virtual void DrawDynVB(int nOffs, int Pool, int nVerts) override;
  virtual void DrawDynVB(struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F *pBuf, ushort *pInds, int nVerts, int nInds, int nPrimType) override;
  virtual void Draw3dBBox(const Vec3 &mins,const Vec3 &maxs, int nPrimType=DPRIM_WHIRE_BOX) override;
  virtual void Draw3dPrim(const Vec3 &mins,const Vec3 &maxs, int nPrimType=DPRIM_WHIRE_BOX, const float* fRGBA = NULL) override;
  virtual void SetTexture(int tnum, ETexType Type=eTT_Base) override;
  virtual void SetWhiteTexture() override;
  virtual void WriteXY(CXFont *currfont,int x,int y, float xscale,float yscale,float r,float g,float b,float a,const char *message, ...) override;
  virtual void Draw2dText( float posX,float posY,const char *szText,SDrawTextInfo &info ) override;
  virtual void Draw2dImage (float xpos,float ypos,float w,float h,int texture_id,float s0=0,float t0=0,float s1=1,float t1=1,float angle=0,float r=1,float g=1,float b=1,float a=1,float z=1) override;
  virtual void DrawImage(float xpos,float ypos,float w,float h,int texture_id,float s0,float t0,float s1,float t1,float r,float g,float b,float a) override;
  virtual bool FontUploadTexture(class CFBitmap*, ETEX_Format eTF=eTF_8888) override;
  virtual void FontReleaseTexture(class CFBitmap *pBmp) override;
  virtual void FontSetTexture(class CFBitmap*, int nFilterMode) override;
  virtual void FontSetTexture(int nTexId, int nFilterMode) override;
  virtual void FontSetRenderingState(unsigned long nVirtualScreenWidth, unsigned long nVirtualScreenHeight) override;
  virtual void FontSetBlending(int src, int dst) override;
  virtual void FontRestoreRenderingState() override;
  virtual void EF_AddPolyToScene2D(int Ef, int numPts, SColorVert2D *verts) override;
  virtual void EF_AddPolyToScene2D(SShaderItem si, int nTempl, int numPts, SColorVert2D *verts) override;
  virtual void EF_EndEf2D(bool bSort) override;
  virtual bool EF_DrawEfForName(char *name, float x, float y, float width, float height, CFColor& col, int nTempl=-1) override;
  virtual bool EF_DrawEfForNum(int num, float x, float y, float width, float height, CFColor& col, int nTempl=-1) override;
  virtual bool EF_DrawEf(IShader *ef, float x, float y, float width, float height, CFColor& col, int nTempl=-1) override;
  virtual bool EF_DrawEf(SShaderItem si, float x, float y, float width, float height, CFColor& col, int nTempl=-1) override;
  virtual bool EF_DrawPartialEfForName(char *name, SVrect *vr, SVrect *pr, CFColor& col) override;
  virtual bool EF_DrawPartialEfForNum(int num, SVrect *vr, SVrect *pr, CFColor& col) override;
  virtual bool EF_DrawPartialEf(IShader *ef, SVrect *vr, SVrect *pr, CFColor& col, float iwdt=0, float ihgt=0) override;
  virtual void DrawLabelImage(const Vec3 &vPos,float fSize,int nTextureId) override;
  virtual void DrawLabel(Vec3 pos, float font_size, const char * label_text, ...) override;
  virtual void DrawLabelEx(Vec3 pos, float font_size, float * pfColor, bool bFixedSize, bool bCenter, const char * label_text, ...) override;
  virtual void Draw2dLabel( float x,float y, float font_size, float * pfColor, bool bCenter, const char * label_text, ...) override;
  virtual float ScaleCoordX(float value) override;
  virtual float ScaleCoordY(float value) override;
  virtual void SetState(int State) override;
  virtual void SetCullMode (int mode=R_CULL_BACK) override;
  virtual bool EnableFog (bool enable) override;
  virtual void SetFog (float density,float fogstart,float fogend,const float *color,int fogmode) override;
  virtual void EnableTexGen(bool enable) override;
  virtual void SetTexgen (float scaleX,float scaleY,float translateX=0,float translateY=0) override;
  virtual void SetTexgen3D(float x1, float y1, float z1, float x2, float y2, float z2) override;
  virtual void SetLodBias (float value=R_DEFAULT_LODBIAS) override;
  virtual void SetColorOp(byte eCo, byte eAo, byte eCa, byte eAa) override;
  virtual void EnableTMU(bool enable) override;
  virtual void SelectTMU(int tnum) override;
  virtual void TextToScreen(float x, float y, const char * format, ...) override;
  virtual void TextToScreenColor(int x, int y, float r, float g, float b, float a, const char * format, ...) override;
  virtual void SetMaterialColor(float r, float g, float b, float a) override;
  virtual void Draw2dLine (float x1,float y1,float x2,float y2) override;
  virtual void SetLineWidth(float fWidth) override;
  virtual void DrawLine(const Vec3 & vPos1, const Vec3 & vPos2) override;
  virtual void DrawLineColor(const Vec3 & vPos1, const CFColor & vColor1, const Vec3 & vPos2, const CFColor & vColor2) override;
  virtual void Graph(byte *g, int x, int y, int wdt, int hgt, int nC, int type, char *text, CFColor& color, float fScale) override;
  virtual void DrawBall(float x, float y, float z, float radius) override;
  virtual void DrawBall(const Vec3 & pos, float radius ) override;
  virtual void DrawPoint(float x, float y, float z, float fSize = 0.0f) override;
  virtual void FlushTextMessages() override;
  virtual void DrawQuad(const Vec3 &right, const Vec3 &up, const Vec3 &origin,int nFlipMode=0) override;
  virtual void DrawQuad(float dy,float dx, float dz, float x, float y, float z) override;
  virtual void SetFogColor(float * color) override;
  virtual void TransformTextureMatrix(float x, float y, float angle, float scale) override;
  virtual void ResetTextureMatrix() override;
  virtual void Set2DMode(bool enable, int ortox, int ortoy) override;
  virtual void SetTexClampMode(bool clamp) override;

  //////////////////////////////////////////////////////////////////////////
  // RendererTextures.cpp

  virtual bool SaveTga(unsigned char *sourcedata,int sourceformat,int w,int h,const char *filename,bool flip) override;
  virtual void WriteDDS(byte *dat, int wdt, int hgt, int Size, const char *name, EImFormat eF, int NumMips) override;
  virtual void WriteTGA(byte *dat, int wdt, int hgt, const char *name, int bits) override;
  virtual void WriteJPG(byte *dat, int wdt, int hgt, char *name) override;
  virtual int FontCreateTexture(int Width, int Height, byte *pData, ETEX_Format eTF=eTF_8888) override;
  virtual bool FontUpdateTexture(int nTexId, int X, int Y, int USize, int VSize, byte *pData) override;
  virtual bool EF_PrecacheResource(ITexPic *pTP, float fDist, float fTimeToReady, int Flags) override;
  virtual void EF_ReloadTextures () override;
  virtual ITexPic *EF_GetTextureByID(int Id) override;
  virtual ITexPic *EF_LoadTexture(const char* nameTex, uint flags, uint flags2, byte eTT, float fAmount1=-1.0f, float fAmount2=-1.0f, int Id=-1, int BindId=0) override;
  virtual int EF_LoadLightmap (const char *name) override;
  virtual bool EF_ScanEnvironmentCM (const char *name, int size, Vec3& Pos) override;
  virtual bool EF_SetLightHole(Vec3 vPos, Vec3 vNormal, int idTex, float fScale=1.0f, bool bAdditive=true) override;
  virtual unsigned int DownLoadToVideoMemory(unsigned char *data,int w, int h, ETEX_Format eTFSrc, ETEX_Format eTFDst, int nummipmap, bool repeat=true, int filter=FILTER_BILINEAR, int Id=0, char *szCacheName=NULL, int flags=0) override;
  virtual void UpdateTextureInVideoMemory(uint tnum, unsigned char *newdata,int posx,int posy,int w,int h,ETEX_Format eTFSrc=eTF_0888) override;
  virtual unsigned int LoadTexture(const char * filename,int *tex_type=NULL,unsigned int def_tid=0,bool compresstodisk=true,bool bWarn=true) override;
  virtual bool DXTCompress( byte *raw_data,int nWidth,int nHeight,ETEX_Format eTF, bool bUseHW, bool bGenMips, int nSrcBytesPerPix, MIPDXTcallback callback=0) override;
  virtual bool DXTDecompress(byte *srcData,byte *dstData, int nWidth,int nHeight,ETEX_Format eSrcTF, bool bUseHW, int nDstBytesPerPix) override;
  virtual void RemoveTexture(unsigned int TextureId) override;
  virtual void RemoveTexture(ITexPic * pTexPic) override;
  virtual int GenerateAlphaGlowTexture(float k) override;
  virtual int LoadAnimatedTexture(const char * format,const int nCount) override;
  virtual void RemoveAnimatedTexture(AnimTexInfo * pInfo) override;
  virtual AnimTexInfo * GetAnimTexInfoFromId(int nId) override;
  virtual int ScreenToTexture() override;

  //////////////////////////////////////////////////////////////////////////
  // RendererShaders.cpp

  virtual bool EF_PrecacheResource(IShader *pSH, float fDist, float fTimeToReady, int Flags) override;
  virtual IShader *EF_LoadShader (const char *name, EShClass Class, int flags=0, uint64 nMaskGen=0) override;
  virtual SShaderItem EF_LoadShaderItem (const char *name, EShClass Class, bool bShare, const char *templName, int flags=0, SInputShaderResources *Res=NULL, uint64 nMaskGen=0) override;
  virtual bool EF_ReloadFile (const char *szFileName) override;
  virtual void EF_ReloadShaderFiles (int nCategory) override;
  virtual IShader *EF_CopyShader(IShader *ef) override;
  virtual int EF_ReadAllImgFiles(IShader *ef, SShaderTexUnit *tl, STexAnim *ta, char *name) override;
  virtual char **EF_GetShadersForFile(const char *File, int num) override;
  virtual SLightMaterial *EF_GetLightMaterial(char *Str) override;
  virtual bool EF_RegisterTemplate(int nTemplId, char *Name, bool bReplace) override;
  virtual bool EF_HideTemplate(const char *name) override;
  virtual bool EF_UnhideTemplate(const char *name) override;
  virtual bool EF_UnhideAllTemplates() override;
  virtual void EF_ConstructEf(IShader *Ef) override;
  virtual void SetGlobalShaderTemplateId(int nTemplateId) override;
  virtual int GetGlobalShaderTemplateId() override;

  //////////////////////////////////////////////////////////////////////////
  // RendererBuffers.cpp

  virtual void SetFenceCompleted(CVertexBuffer * buffer) override;
  virtual CVertexBuffer *CreateBuffer(int vertexcount,int vertexformat, const char *szSource, bool bDynamic=false) override;
  virtual void ReleaseBuffer(CVertexBuffer *bufptr) override;
  virtual void DrawBuffer(CVertexBuffer *src,SVertexStream *indicies,int numindices, int offsindex, int prmode,int vert_start=0,int vert_stop=0, CMatInfo *mi=NULL) override;
  virtual void UpdateBuffer(CVertexBuffer *dest,const void *src,int vertexcount, bool bUnLock, int nOffs=0, int Type=0) override;
  virtual void CreateIndexBuffer(SVertexStream *dest,const void *src,int indexcount) override;
  virtual void UpdateIndexBuffer(SVertexStream *dest,const void *src, int indexcount, bool bUnLock=true) override;
  virtual void ReleaseIndexBuffer(SVertexStream *dest) override;
  virtual bool EF_PrecacheResource(CLeafBuffer *pPB, float fDist, float fTimeToReady, int Flags) override;
  virtual CLeafBuffer * CreateLeafBuffer(bool bDynamic, const char *szSource="Unknown", class CIndexedMesh * pIndexedMesh=0) override;
  virtual CLeafBuffer * CreateLeafBufferInitialized( void * pVertBuffer, int nVertCount, int nVertFormat, ushort* pIndices, int nIndices, int nPrimetiveType, const char *szSource, EBufferType eBufType = eBT_Dynamic, int nMatInfoCount=1, int nClientTextureBindID=0, bool (*PrepareBufferCallback)(CLeafBuffer *, bool)=NULL, void *CustomData=NULL, bool bOnlyVideoBuffer=false, bool bPrecache=true) override;
  virtual void DeleteLeafBuffer(CLeafBuffer * pLBuffer) override;

  //////////////////////////////////////////////////////////////////////////
  // Scene.cpp

  virtual bool EF_PrecacheResource(CDLight *pLS, float fDist, float fTimeToReady, int Flags) override;
  virtual void EF_EnableHeatVision(bool bEnable) override;
  virtual bool EF_GetHeatVision() override;
  virtual void EF_AddPolyToScene3D(int Ef, int numPts, SColorVert *verts, CCObject *obj=NULL, int nFogID=0) override;
  virtual CCObject *EF_AddSpriteToScene(int Ef, int numPts, SColorVert *verts, CCObject *obj, byte *inds=NULL, int ninds=0, int nFogID=0) override;
  virtual void EF_AddSplash (Vec3 Pos, eSplashType eST, float fForce, int Id=-1) override;
  virtual CRendElement *EF_CreateRE (EDataType edt) override;
  virtual void EF_StartEf () override;
  virtual CCObject *EF_GetObject (bool bTemp=false, int num=-1) override;
  virtual void EF_AddEf (int NumFog, CRendElement *re, IShader *ef, SRenderShaderResources *sr, CCObject *obj, int nTempl, IShader *efState=0, int nSort=0) override;
  virtual void EF_EndEf3D (int nFlags) override;
  virtual bool EF_IsFakeDLight (CDLight *Source) override;
  virtual void EF_ADDDlight(CDLight *Source) override;
  virtual void EF_ClearLightsList() override;
  virtual bool EF_UpdateDLight(CDLight *pDL) override;
  virtual void EF_SetWorldColor(float r, float g, float b, float a=1.0f) override;
  virtual int EF_RegisterFogVolume(float fMaxFogDist, float fFogLayerZ, CFColor color, int nIndex=-1, bool bCaustics=false) override;

  //////////////////////////////////////////////////////////////////////////
  // Renderer.cpp, helpers

  //! Opens the device's frame if the engine draws outside BeginFrame / Update; false without a device.
  bool EnsureFrame();
  //! The engine's r_Width / r_Height / r_Fullscreen changed: new window, display mode and back
  //! buffer size (checked in Update).
  void CheckResolutionChange();
  //! Our window windowed (outer size nWidth x nHeight, where it was before full screen) or full
  //! screen (ShowFullScreen).
  void SetWindowMode(bool bFullScreen, int nWidth, int nHeight);
  //! Full screen: the display mode nWidth x nHeight (SetDisplayMode), and the window over the monitor.
  void ShowFullScreen(int nWidth, int nHeight);
  //! The window's display to the mode nWidth x nHeight (32 bits, the highest refresh rate of that
  //! size) as the original's full screen does; the desktop's own mode when it is that size. False
  //! when the display has no such mode: it keeps its own and the back buffer is stretched over it.
  bool SetDisplayMode(int nWidth, int nHeight);
  //! The display's mode back to the desktop's, if SetDisplayMode changed it.
  void RestoreDisplayMode();
  //! WM_ACTIVATEAPP: full screen, losing the focus (Alt+Tab) minimises the window with the
  //! desktop's mode back; getting it again shows it full screen.
  void OnActivateApp(bool bActive);
  //! Our window procedure in front of FarCry.exe's (m_pfnGameWndProc): WM_ACTIVATEAPP.
  static LRESULT CALLBACK WindowProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam);
  //! d3d12_GpuTimes: from here on the GPU time counts for szSection (static storage), in the main
  //! view only (a nested scene counts for the section that renders it).
  void GpuSection(const char *szSection);
  //! d3d12_GpuTimes: after each frame; every so many frames the averages to the log.
  void ReportGpuTimes();
  //! The projection of the main view from the camera.
  static Matrix44 MakeProjection(const CCamera &cam);

  //////////////////////////////////////////////////////////////////////////
  // Draw.cpp: every draw goes through here

  //! b0 of the following draws is made again from the camera, sun, fog and screen (after SetCamera,
  //! a new sun, ...).
  void InvalidateFrameConstants() { m_nFrameCB = 0; }
  //! The frame constants of the current camera and settings; bScene: for the draws into an
  //! upscaler's scene (its size, jitter and mip bias).
  void MakeFrameConstants(SFrameConstants &fc, bool bScene) const;
  //! One draw: the pipeline state of Key, dc as b1, VB in slot 0 with the default stream in slot 1
  //! and pTexCoord1 in slot 2, pTangents in slot 3, the index buffer when pIB; nCount indices (or
  //! vertices) from nFirst. Viewport m_Viewport with the depth range m_fDepthMin..m_fDepthMax;
  //! scissor from SetScissor or m_bItemScissor (in an upscaler's scene both scaled to its size,
  //! and the key's target 0 made 3).
  void DrawGeometry(const SPipelineKey &Key, const SDrawConstants &dc, D3D_PRIMITIVE_TOPOLOGY eTopology, const D3D12_VERTEX_BUFFER_VIEW &VB,
    const D3D12_INDEX_BUFFER_VIEW *pIB, uint32 nCount, uint32 nFirst = 0, int nBaseVertex = 0, const D3D12_VERTEX_BUFFER_VIEW *pTexCoord1 = nullptr,
    const D3D12_VERTEX_BUFFER_VIEW *pTangents = nullptr);
  //! The same with the vertices (and indices) copied into per frame memory. Fans are not supported
  //! by Direct3D 12: the callers make lists.
  void DrawImmediate(const SPipelineKey &Key, const SDrawConstants &dc, D3D_PRIMITIVE_TOPOLOGY eTopology, int nVertexFormat,
    const void *pVerts, int nVerts, const ushort *pInds = nullptr, int nInds = 0);
  //! Constants with identity matrices, white colour and nothing switched on.
  static SDrawConstants DefaultDrawConstants();
  //! An orthographic projection of a w x h pixel screen with the origin at the top left (row vectors).
  static Matrix44 MakeOrtho(float w, float h);

  float m_fDepthMin = 0, m_fDepthMax = 1;   //!< viewport depth range of the next draws
  bool m_bItemScissor = false;              //!< the scissor rectangle of the object being drawn
  RECT m_ItemScissor = {};
  bool m_bTargetRect = false;               //!< drawing off screen: the viewport is m_TargetRect, not the whole target
  RECT m_TargetRect = {};
  D3D12_GPU_VIRTUAL_ADDRESS m_nFrameCB = 0; //!< b0, 0 when it has to be made again
  bool m_bFrameCBScene = false;             //!< m_nFrameCB was made for an upscaler's scene
  bool m_bFrameCBHdr = false;               //!< m_nFrameCB was made for a target of D3D12_HDR_FORMAT
  float m_SunDir[3] = { 0, 0, 1 };          //!< towards the sun, world space (frame constants)
  float m_SunColor[3] = { 1, 1, 1 };
  float m_SunSpec[3] = { 0, 0, 0 };          //!< the sun's specular colour

  //////////////////////////////////////////////////////////////////////////
  // Renderer2D.cpp helpers

  //! Key and constants from the render state of SetState, SetCullMode, SetTexture, SetColorOp,
  //! the texture matrix and the polygon mode. bVertexColor: the vertices have colours.
  void ApplyRenderState(SPipelineKey &Key, SDrawConstants &dc, int nVertexFormat, bool bVertexColor);
  //! The transform of immediate draws: the 2D mode's orthographic projection, else the camera.
  Matrix44 GetImmediateViewProj() const;
  //! mWorldViewProj of an immediate draw (GetImmediateViewProj), and its motion vectors (static in
  //! the world, none in the 2D mode).
  void SetImmediateTransform(SDrawConstants &dc) const;

  //! Draw3dBBox: drawn by Update just before Present, with the camera and viewport of that moment.
  void DrawQueuedBoxes();
  //! A box (or line) of Draw3dBBox / Draw3dPrim. pRGBA NULL: the material colour.
  void DrawBox(const Vec3 &vMin, const Vec3 &vMax, int nPrimType, const float *pRGBA);
  //! Text in the 800 x 600 virtual screen with the engine's default font.
  void DrawText800(float x, float y, float fSize, const float *pRGBA, bool bCenter, const char *szText);

  struct SQueuedBox
  {
    Vec3 vMin, vMax;
    int nPrimType;
  };
  std::vector<SQueuedBox> m_QueuedBoxes;
  bool m_bFontState = false;                //!< between FontSetRenderingState and FontRestoreRenderingState
  int m_nFontTexture = -1;
  float m_fFontScreenW = 0, m_fFontScreenH = 0;
  std::vector<struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F> m_DynVB; //!< GetDynVBPtr

  //////////////////////////////////////////////////////////////////////////
  // Draw3D.cpp

  //! Draws the sorted render items of one list of EF_EndEf3D.
  void DrawItems(SRenderItem *pItems, int nItems, int eList);
  //! Before the first list: the sun, the first person projection, the clear of the main view.
  void BeginScene3D();
  void DrawLeafItem(const SRenderItem &it);
  //! Heat vision: how hot an item is, 0 to 1 (FOB_HOTAMBIENT, a heat source among its lights).
  float ItemHeat(const SRenderItem &it) const;
  //! The lights of an object besides the sun as the draw's own lights (SDrawConstants::vLightPos).
  void SetObjectLights(SDrawConstants &dc, const CCObject *pObj, const Matrix44 &World, CLeafBuffer *pLB);
  //! The detail texture of the terrain sectors (TerrainWithDefaultDetailTexture: always
  //! textures/terrain/detail/default, whatever r_DetailDefault and r_DetailTextures say); NULL
  //! without one.
  CTexture *GetDetailTexture();
  //! The normal map of a material (EFTT_NORMALMAP, else EFTT_BUMP; loaded on first use); NULL
  //! without one or when its file is missing.
  CTexture *GetNormalMap(SRenderShaderResources *pRes);
  //! The gloss map (EFTT_GLOSS) a glow template adds, nullptr without a usable one.
  CTexture *GetGlowMap(SRenderShaderResources *pRes);
  //! The environment cube map of a cube map template: the material's EFTT_CUBEMAP, else the
  //! default cube textures/defaults/defaultcm; nullptr without either.
  CTexture *GetEnvCubeMap(SRenderShaderResources *pRes);
  //! The sky box and layers of a CRESky element drawn with a sky shader (CShaderManager::FindSky).
  void DrawSkyItem(const SRenderItem &it);
  //! The particle sprites of the scene (EF_AddSpriteToScene).
  void DrawSprites();
  //! d3d12_DumpFrame: one line per render item, then one per draw with what the draw got.
  void DumpItem(int nIndex, const SRenderItem &it, int eList);
  void DumpDraw(const SDrawConstants &dc, const SPipelineKey &Key, const CTexture *pTex);

  struct SFileClose
  {
    void operator()(FILE *f) const { fclose(f); }
  };
  std::unique_ptr<FILE, SFileClose> m_pFrameDump; //!< open for the frame being dumped (d3d12_DumpFrame)
  std::unique_ptr<FILE, SFileClose> m_pGpuTimes;  //!< XRenderD3D12_gpu.txt, open while d3d12_GpuTimes is on

  int m_nSceneFlags = 0;                    //!< SHDF_* of the EF_EndEf3D being drawn
  CTexture *m_pDetailTexture = nullptr;     //!< GetDetailTexture (one reference)
  CTexture *m_pDefaultCube = nullptr;       //!< GetEnvCubeMap without a cube of the material (one reference)
  //! The textures of a sky shader: its box's <name>_5 (the top), <name>_12 (the sides x = 32 and
  //! y = -32), <name>_34 (the sides x = -32 and y = 32), and one per layer; one reference each.
  struct SSkyBoxTextures
  {
    std::string Name;                       //!< the sky shader
    CTexture *pTex[3] = {};
    std::vector<CTexture *> Layers;         //!< SSkyShader::Layers
  };
  std::vector<SSkyBoxTextures> m_SkyBoxes;  //!< DrawSkyItem, the sky shaders used so far
  int m_nSceneSkyBox = -1;                  //!< the m_SkyBoxes entry the scene's sky drew, -1 none (the water reflects it)
  float m_fSceneSkyZ1 = 0;                  //!< where its sides end (DrawSkyItem)
  float m_fSceneSkyAlpha = 1;
  Matrix44 m_NearestProj;                   //!< FOB_NEAREST objects

  //////////////////////////////////////////////////////////////////////////
  // Upscale.cpp: the upscaler of the main view (d3d12_Upscaler, Upscaler.h)

  //! At the start of the main view's 3D scene (BeginScene3D): with an upscaler, its scene targets
  //! bound, the frame's jitter and the camera of the frame before for the motion vectors.
  void BeginUpscale();
  //! After the main view's 3D scene (EF_EndEf3D), before the screen effects: its picture upscaled
  //! into the back buffer, which is bound again (its depth buffer cleared).
  void UpscaleScene();
  //! The upscaler of d3d12_Upscaler for the back buffer's size with the scene targets of
  //! d3d12_UpscaleMode, made when they changed; false: none.
  bool UpdateUpscaler();
  void ReleaseUpscaler();
  //! The scene's pixels per pixel of the back buffer while the main view draws into the scene
  //! targets, else 1.
  void GetSceneScale(float &sx, float &sy) const;
  //! The view of the frame without its translation times the sky's projection (DrawSkyItem).
  Matrix44 GetSkyViewProj() const;
  //! m_Reproject from the current camera and the frame before's.
  void UpdateReprojection();
  //! mPrevWorldViewProj of a draw whose vertices stay where they are in the world: its
  //! mWorldViewProj with the camera of the frame before (while upscaling).
  void SetStaticMotion(SDrawConstants &dc) const;
  //! The same for an object at World: where it was in the frame before when it has an identity
  //! that lasts (a character, a permanent object), else as static; the first person objects
  //! (bNearest) move with the camera, so not on the screen.
  void SetObjectMotion(SDrawConstants &dc, const CCObject *pObj, const Matrix44 &World, bool bNearest);
  //! CCObject::RemovePermanent: the matrices kept of pObj go.
  void ForgetObjectMotion(const CCObject *pObj);

  std::unique_ptr<IUpscaler> m_pUpscaler;
  std::string m_UpscalerName;               //!< the d3d12_Upscaler m_pUpscaler was made for (or not)
  int m_nUpscalerWidth = 0, m_nUpscalerHeight = 0;   //!< the back buffer's size it was made for
  int m_nUpscaleMode = -1;                  //!< EUpscaleMode of the scene targets, -1 none
  bool m_bUpscaleHdr = false;               //!< their colour is D3D12_HDR_FORMAT
  int m_nUpscaleOutputSRV = -1;             //!< the upscaler's output in the texture table
  int m_nUpscaleColorSRV = -1;              //!< the scene's colour (shown stretched when the upscale fails)
  int m_nUpscaleDepthSRV = -1;              //!< the scene's depth as R32_FLOAT (d3d12_UpscaleDebug 2)
  bool m_bUpscaling = false;                //!< BeginUpscale .. UpscaleScene
  bool m_bUpscaleReset = true;              //!< the next upscale starts without history (a new upscaler or mode)
  bool m_bUpscaleHistoryReset = false;      //!< this frame's starts without history
  int m_nUpscaleFrames = 0;                 //!< the frames upscaled (the jitter's index)
  int m_nUpscaledFrame = -1;                //!< m_nFrames of the last one
  float m_UpscaleJitter[2] = {};            //!< this frame's jitter, render pixels
  Matrix44 m_PrevViewProj;                  //!< the main view's camera in the frame upscaled before
  Matrix44 m_PrevSkyViewProj;               //!< its sky's (GetSkyViewProj)
  Vec3 m_vPrevCameraPos = Vec3(0, 0, 0);
  float m_fPrevFovY = 0;                    //!< the vertical field of view then
  Matrix44 m_Reproject;                     //!< a clip position of this frame's camera to the frame before's
  //! The world matrices of an object with an identity that lasts, by m_pCharInstance or the
  //! permanent CCObject.
  struct SObjectMotion
  {
    Matrix44 Prev, Cur;                     //!< in the frame nFrame and the one before
    int nFrame = -1;                        //!< m_nFrames of Cur
    bool bPrev = false;                     //!< Prev is of the frame upscaled before
  };
  std::unordered_map<const void *, SObjectMotion> m_ObjectMotion;
  ICVar *m_pUpscalerVar = nullptr;          //!< d3d12_Upscaler

  //////////////////////////////////////////////////////////////////////////
  // AmbientOcclusion.cpp: the ambient occlusion of the main view (d3d12_AO, AmbientOcclusion.h)

  //! After the main view's opaque list, before the first person objects: the backend's occlusion of
  //! what the depth buffer holds, as a factor (AoResolve.hlsl) the scene drawn so far is multiplied by.
  void DrawAmbientOcclusion();
  //! d3d12_AODebug: the factor over the whole picture in grey (after EndMainScene).
  void DrawAODebug();
  //! The backend of d3d12_AO for the main view's depth buffer and size, made when they changed;
  //! false: none.
  bool UpdateAO();
  //! bForget false: what it was made for stays known, so that a backend that failed is not made
  //! again every frame.
  void ReleaseAO(bool bForget = true);

  std::unique_ptr<IAmbientOcclusion> m_pAO;
  std::string m_AOName;                     //!< the d3d12_AO m_pAO was made for (or not)
  ComPtr<ID3D12Resource> m_pAODepth;        //!< the depth buffer it reads (held, so that a new one is never at its address)
  int m_nAOWidth = 0, m_nAOHeight = 0;
  CD3D12Texture *m_pAOOutput = nullptr;     //!< the backend's visibility (R8_TYPELESS)
  CD3D12Texture *m_pAOFactor = nullptr;     //!< the resolve's factor (R8G8B8A8_UNORM, an SRV in the texture table)
  int m_nAOResolveTable = -1;               //!< the resolve's descriptor tables (CD3D12Descriptors::AllocRange)
  int m_nAOFrames = 0;                      //!< the frames with ambient occlusion (the noise's index)
  int m_nAODrawnFrame = -1;                 //!< m_nFrames of the last one (DrawAODebug)
  ICVar *m_pAOVar = nullptr;                //!< d3d12_AO

  //////////////////////////////////////////////////////////////////////////
  // Water.cpp

  //! Key and constants of a water surface (eDC_Water): the water program over the copy of the
  //! scene drawn so far (made before the first surface of the list). False: not drawn.
  bool SetupWaterDraw(SPipelineKey &Key, SDrawConstants &dc, const SRenderItem &it);
  //! The waves of foam on the beach drawn at a time.
  static constexpr int BEACH_WAVES = 3;
  //! The beach (eDC_WaterBeach): pass nPass, a wave of foam (0 to BEACH_WAVES - 1) or the wet sand
  //! under them (BEACH_WAVES). False: not drawn.
  bool SetupBeachDraw(SPipelineKey &Key, SDrawConstants &dc, int nPass);
  //! The sea floor under the ocean (eDC_WaterBottom) with the element's texture pTex (may be NULL).
  void SetupWaterBottomDraw(SPipelineKey &Key, SDrawConstants &dc, CTexture *pTex);
  //! Copies the main view's target and its depth buffer as drawn so far (m_bSceneCopied).
  bool CopySceneForWater();
  void ReleaseWaterResources();
  //! The texture index of the wave slopes the water program samples (made on first use).
  int GetWaveTexture();
  //! The caustics on the terrain under the ocean (eDC_Caustics). False: not drawn.
  bool SetupCausticsDraw(SPipelineKey &Key, SDrawConstants &dc, const SRenderItem &it);
  //! The texture index of the caustics pattern (made on first use), -1 without one.
  int GetCausticsTexture();
  //! The glass of a scope (eDC_ScopeLens). False: not drawn.
  bool SetupScopeLensDraw(SPipelineKey &Key, SDrawConstants &dc);
  //! Before the main view is drawn (Items: its render items): with r_WaterReflections and an
  //! ocean surface among the items, the scene mirrored in the water plane into m_pReflectionColor,
  //! when the last one is out of date (r_WaterUpdate*): the 3D engine renders it with
  //! DrawLowDetail (d3d12_WaterReflectionFlags: the sky, the terrain, the static objects, the far
  //! sprites), seen by the camera mirrored in the water plane; then its mip levels.
  void UpdateWaterReflection(std::span<const SRenderItem> Items);
  void ReleaseWaterReflection();
  //! The reflection pass's projection: its near plane replaced by the water plane, so that nothing
  //! under the water is drawn into the reflection.
  void ClipProjectionToWater(Matrix44 &Proj, const Matrix44 &View) const;

  //! A copy of the main view's target and of its depth buffer (CopySceneForWater).
  struct SSceneCopy
  {
    ComPtr<ID3D12Resource> pColor, pDepth;
    int nColorSRV = -1, nDepthSRV = -1;
    int nWidth = 0, nHeight = 0;
    DXGI_FORMAT ColorFormat = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT DepthFormat = DXGI_FORMAT_UNKNOWN;    //!< how the depth copy is read (CD3D12Device::GetMainDepthSRVFormat)
  };
  void FreeSceneCopy(SSceneCopy &Copy);
  //! [0] of the back buffer (the screen effects), [1] of the HDR target or an upscaler's scene (the
  //! water, in the scene): apart, so that a frame with both does not make them again.
  SSceneCopy m_SceneCopies[2];
  int m_nSceneColorSRV = -1, m_nSceneDepthSRV = -1;   //!< the texture table indices of the copy made last
  bool m_bSceneCopied = false;              //!< the copies hold the current list's scene
  CTexture *m_pBeachTexture = nullptr;      //!< textures/wave1 (one reference)
  CTexture *m_pBeachWetTexture = nullptr;   //!< textures/water2 (one reference)
  CTexture *m_pBeachBumpTexture = nullptr;  //!< textures/fire_noise as a normal map (one reference)
  int m_nWaveTexture = 0;                   //!< GetWaveTexture: its ID, 0 not made yet, -1 failed
  int m_nCausticsTexture = 0;               //!< GetCausticsTexture: its ID, 0 not made yet, -1 failed
  float m_fCausticsMinMean = 1;             //!< the mean of the smaller of two texels of it (its mean 1)
  static constexpr int REFLECTION_MIPS = 8;  //!< the most mip levels of m_pReflectionColor
  ComPtr<ID3D12Resource> m_pReflectionColor, m_pReflectionDepth;
  D3D12_CPU_DESCRIPTOR_HANDLE m_hReflectionRTV = {}, m_hReflectionDSV = {};
  D3D12_CPU_DESCRIPTOR_HANDLE m_hReflectionMipRTV[REFLECTION_MIPS - 1] = {};   //!< the RTVs of levels 1 on
  int m_nReflectionMipSRV[REFLECTION_MIPS - 1] = { -1, -1, -1, -1, -1, -1, -1 }; //!< the SRVs of levels 0, 1, ... alone (the next is made from each)
  int m_nReflectionSRV = -1;                //!< all its levels
  int m_nReflectionMips = 0;
  int m_nReflectionWidth = 0, m_nReflectionHeight = 0;
  bool m_bReflectionPass = false;           //!< the reflection's scene is being rendered
  bool m_bReflectionValid = false;          //!< m_pReflectionColor holds a reflection for this frame's ocean
  float m_fReflectionWaterZ = 0;            //!< the water level it mirrors
  float m_fReflectionTime = 0;              //!< ITimer time it was drawn at
  Vec3 m_vReflectionCamPos, m_vReflectionCamDir;  //!< the frame's camera it was drawn for
  Matrix44 m_ReflectionViewProj;            //!< world to clip of the mirrored camera (without the water clip)

  //////////////////////////////////////////////////////////////////////////
  // EnvCubes.cpp: the cubes of the scene around the objects of TemplReflEnvCM (EEnvMap::Auto)

  //! A cube of the scene seen from a point.
  struct SEnvCube
  {
    ComPtr<ID3D12Resource> pColor, pDepth;
    D3D12_CPU_DESCRIPTOR_HANDLE hRTV[6] = {}, hDSV = {};
    int nSRV = -1;                          //!< the texture table index (a TEXTURECUBE view)
    int nSize = 0;
    DXGI_FORMAT Format = DXGI_FORMAT_UNKNOWN; //!< of the faces: D3D12_BACKBUFFER_FORMAT, or D3D12_HDR_FORMAT with r_HDRRendering
    Vec3 vPos = Vec3(0, 0, 0);              //!< where it is seen from
    int nFaces = 0;                         //!< faces drawn since it was made (6: all)
    int nNextFace = 0;                      //!< the face redrawn next
    float fFaceTime = 0;                    //!< ITimer time of the last face drawn
    int nUsedFrame = -1;                    //!< m_nFrames of the last frame an object used it
  };
  //! Before the main view is drawn (Items: its render items): the cubes of its objects with an auto
  //! cube map, their faces redrawn as r_EnvCMupdateInterval says.
  void UpdateEnvCubes(std::span<const SRenderItem> Items);
  //! Face nFace of a cube: the 3D engine renders the scene (DrawLowDetail) from the cube's point.
  void DrawEnvCubeFace(SEnvCube &Cube, int nFace);
  //! The texture table index of the cube of an item with an auto cube map, -1 without one.
  int GetEnvCubeSRV(const SRenderItem &it) const;
  void FreeEnvCube(SEnvCube &Cube);
  void ReleaseEnvCubes();

  std::vector<SEnvCube> m_EnvCubes;
  bool m_bEnvCubePass = false;              //!< a face of a cube is being rendered

  //////////////////////////////////////////////////////////////////////////
  // FarSprites.cpp: the far vegetation sprites (MakeSprite, DrawObjSprites)

  //! Before the main view is drawn: the pictures of the objects MakeSprite got since the last time.
  void BakeSprites();
  //! Draws the pictures of one object (24 directions) into the off screen target, from tile nFirstTile on.
  void BakeSpriteObject(IStatObj *pObj, int nFirstTile, int nTilesPerRow, int nTileSize);
  //! Forgets the objects whose sprite IDs the engine has all removed, with their arrays.
  void PurgeSprites();
  void ReleaseSprites();

  static constexpr int SPRITE_ANGLES = 24;  //!< MakeSprite's angles: 90 to 435 degrees in steps of 15
  //! The sprites of one object (the one MakeSprite got).
  struct SSpriteSet
  {
    int nID[SPRITE_ANGLES] = {};            //!< MakeSprite's texture IDs (no GPU texture), 0 where it was not called
    CTexture *pTex[SPRITE_ANGLES] = {};     //!< the textures of the IDs (for a check: the engine may remove them)
    int nArrayID = 0;                       //!< the pictures: a texture array of 24 slices (one reference)
    CTexture *pArray = nullptr;
    bool bBaked = false;                    //!< the array holds the pictures
    Vec3 vBoxMin, vBoxMax;                  //!< the object's box when baked
    float fRadius = 0;                      //!< the largest horizontal distance of a box corner from the object's z axis
  };
  std::unordered_map<IStatObj *, SSpriteSet> m_Sprites;
  std::vector<IStatObj *> m_SpritesToBake;  //!< MakeSprite got them, not baked yet
  bool m_bSpritePass = false;               //!< a sprite's picture is being drawn
  struct SSpriteQuad
  {
    int nSRV;                               //!< the texture array's table index
    struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F v[4];   //!< top left, top right, bottom left, bottom right
  };
  std::vector<SSpriteQuad> m_SpriteQuads;   //!< DrawObjSprites' quads of the call

  //////////////////////////////////////////////////////////////////////////
  // Shadows.cpp: the shadow maps of the static objects

  //! A shadow map of a frustum of the engine (MakeShadowMapFrustum).
  struct SShadowMap
  {
    const void *pFrustum = nullptr;         //!< the frustum it was drawn for
    ComPtr<ID3D12Resource> pDepth;
    D3D12_CPU_DESCRIPTOR_HANDLE hDSV = {};
    int nSRV = -1;                          //!< the texture table index
    int nSize = 0;
    float fRadius = 0;                      //!< the radius of its circle in the caster's space
    Matrix44 LocalToMap;                    //!< the caster's space to the map: x, y -1..1 across the circle, z 0..1 away from the sun
    bool bEntities = false;                 //!< an entity's map: drawn in every frame its caster is seen
    int nLastUsedFrame = 0;                 //!< m_nFrames
    int nDrawnFrame = 0;
  };
  //! EF_AddEf: the casters whose shadows fall on an object (CCObject::m_pShadowCasters), for
  //! UpdateShadowMaps.
  void NoteShadowCasters(const CCObject *pObj);
  //! Before the main view is drawn: the maps of the frustums of the casters noted since the last
  //! time, where they have none.
  void UpdateShadowMaps();
  //! The map of a frustum whose caster instance is at vOrigin (an entity's map is drawn there).
  bool DrawShadowMap(void *pFrustum, const Vec3 &vOrigin);
  //! Adds to the scene being started what a frustum shows: its static objects at a caster instance
  //! at *pPos scaled by fScale (pPos nullptr: in the caster's space, for the frustum's own map), or
  //! its entities as they are. The number of objects and entities added.
  int AddFrustumCasters(const void *pFrustum, const Vec3 *pPos, float fScale);
  //! The same for every frustum of the casters noted in the frame (m_FrameCasterLists), once each.
  int AddLiveCasters();
  //! In a shadow map pass: the key and constants of a caster's part (its depth, the alpha test
  //! with its texture's alpha). False: not drawn (blended and other parts).
  bool SetupShadowCasterDraw(SPipelineKey &Key, SDrawConstants &dc, const SRenderItem &it);
  //! The maps of the casters of pObj into dc (DF_SHADOWS). False: none is drawn.
  bool SetupShadowReceiver(SDrawConstants &dc, const CCObject *pObj);
  //! The map of a frustum, nullptr when it has none.
  SShadowMap *FindShadowMap(const void *pFrustum);
  //! The same when the map holds what the frustum shows now (nullptr: it has to be drawn).
  SShadowMap *FindDrawnShadowMap(const void *pFrustum);
  void FreeShadowMap(SShadowMap &Map);
  void ReleaseShadowMaps();
  //! d3d12_ShadowDebug: the maps of the frame over the main view.
  void DrawShadowMapDebug();

  std::unordered_map<int, SShadowMap> m_ShadowMaps;   //!< by the ID written into the frustum
  std::vector<const void *> m_ShadowCasterLists;      //!< NoteShadowCasters: the list2<ShadowMapLightSourceInstance> of the frame
  std::vector<const void *> m_FrameCasterLists;       //!< the same, taken by UpdateShadowMaps for the rest of the frame
  int m_nNextShadowMapID = 0x40000000;                //!< the maps' IDs, apart from the texture IDs
  bool m_bShadowMapPass = false;                      //!< a shadow map is being drawn

  //////////////////////////////////////////////////////////////////////////
  // Cascades.cpp: the sun's cascaded shadow maps (d3d12_ShadowCascades)

  //! One cascade of the frame.
  struct SCascade
  {
    Matrix44 WorldToMap;      //!< world to its slice: x, y -1..1 across it, z 0..1 away from the sun
    float fEnd = 0;           //!< the view depth where it ends
    float fCenter[2] = {};    //!< the middle of its square along the sun's x and y axes
    float fHalfSize = 0;      //!< half the width of its square (m)
    float fTexel = 0;         //!< the width of a texel (m)
    float fZMin = 0, fZMax = 0; //!< its range along the sun's z axis (towards the sun): depth 1 at fZMin, 0 at fZMax
    int nCasters[2] = {};     //!< parts drawn into the full and the live slice
  };
  //! A set of cascades' maps: a texture array of a slice per cascade.
  struct SCascadeMaps
  {
    ComPtr<ID3D12Resource> pDepth;
    D3D12_CPU_DESCRIPTOR_HANDLE hDSV[XR_MAX_CASCADES] = {};
    int nSRV = -1;            //!< the texture table index (a TEXTURE2DARRAY view)
  };
  //! Before the main view is drawn, its items prepared (MainItems): with d3d12_ShadowCascades and
  //! e_shadow_maps in a view with the sun, the cascades of the frame and their casters.
  void UpdateSunCascades(std::span<const SRenderItem> MainItems);
  //! The cascades of the main view: the split, the squares, the matrices. False: none.
  bool SetupCascades(int nCascades);
  //! The maps of both sets in the size of d3d12_ShadowCascadeSize.
  bool EnsureCascadeMaps();
  //! Draws the casting parts of Items into every slice of the set m_nCascadeSet that their bounds
  //! reach. With pDrawn: the parts drawn into the set so far (PartKey), the ones in it left out,
  //! the others added.
  void DrawCascadeItems(std::span<const SRenderItem> Items, std::unordered_set<uint64> *pDrawn);
  //! The nested scene of a casters' pass (CScene::EndEf3D with m_bCascadePass): its items into the
  //! cascades' slices of the set being drawn.
  void DrawCascadeCasters(std::span<const SRenderItem> Items);
  //! SetupShadowCasterDraw in a cascade's slice: a caster of the live set only where it is not
  //! behind the full set's map.
  void SetupCascadeCaster(SDrawConstants &dc) const;
  //! The flags of a receiver of the cascades for a draw of the main view (0 elsewhere or without
  //! them): DF2_SUN_CASCADES, with bLive DF2_CASCADES_LIVE.
  uint32 CascadeReceiverFlags(bool bLive) const;
  //! MakeFrameConstants: the cascades' constants where the receivers get them (else left 0).
  void SetCascadeConstants(SFrameConstants &fc) const;
  //! d3d12_ShadowDebug 1: the slices of the frame below the shadow maps.
  void DrawCascadeDebug(int nRow);
  void ReleaseCascades();
  //! e_shadow_maps (of the 3D engine) not 0: the game's shadows are on.
  bool IsShadowMapsOn();

  SCascadeMaps m_CascadeMaps[2];                      //!< the full set, the live set
  int m_nCascadeSize = 0;                             //!< the size of their slices
  SCascade m_Cascades[XR_MAX_CASCADES];
  int m_nCascades = 0;                                //!< the cascades of the frame (0: none; the frame constants)
  Vec3 m_vCascadeAxes[3] = { Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1) };   //!< the sun's frame: x, y across the maps, z towards the sun
  float m_ShadowTint[3] = { 0.5f, 0.5f, 0.5f };       //!< the terrain in the shadow (the engine's terrain shadows' colour, the last seen)
  bool m_bCascadePass = false;                        //!< the casters of the cascades are being drawn
  int m_nCascadeSet = 0;                              //!< the set being drawn: 0 the full set, 1 the live set
  int m_nCascadeSlice = 0;                            //!< the slice being drawn
  int m_nCascadeFrame = -1;                           //!< m_nFrames of the cascades (m_nCascades), when drawn
  std::unordered_set<uint64> m_CascadeDrawn;          //!< the parts drawn into the full set (DrawCascadeItems)
  ICVar *m_pShadowMapsVar = nullptr;                  //!< e_shadow_maps, looked up on first use

  //////////////////////////////////////////////////////////////////////////
  // Hdr.cpp: the HDR pipeline of the main view (r_HDRRendering)

  //! The targets of the HDR passes, for the scene's size.
  struct SHdrState
  {
    std::vector<CD3D12Texture *> Meter;     //!< the meter's levels, the last 1 x 1 (the sum of the log2 luminance, the weight)
    CD3D12Texture *pExposure[2] = {};       //!< 1 x 1 R32_FLOAT, the adapted EV: this frame's and the frame before's
    int nExposure = 0;                      //!< pExposure's current one
    std::vector<CD3D12Texture *> Bloom;     //!< the bloom's levels, the first half the scene's size
    CD3D12Texture *pLdr = nullptr;          //!< the tone mapped scene for an upscaler (R8G8B8A8)
    CD3D12Texture *pRatio = nullptr;        //!< with it and the highlight layer, the layer's ratio at the scene's size (D3D12_HIGHLIGHT_FORMAT)
    int nWidth = 0, nHeight = 0;            //!< the scene's size they are for
    bool bAdapted = false;                  //!< pExposure[nExposure] holds an adapted EV
    int nResolvedFrame = -1;                //!< m_nFrames of the last resolve
    ComPtr<ID3D12Resource> pHdrColor;       //!< the device's HDR target nHdrColorSRV is of (kept: a new one is never at its address)
    int nHdrColorSRV = -1;
  };

  //! At the start of the main view's 3D scene (BeginScene3D): its targets bound, HDR (with
  //! r_HDRRendering) or not, with an upscaler or not (BeginUpscale).
  void BeginMainScene();
  //! After its EF_EndEf3D, before the screen effects: the scene tone mapped (HDR) and upscaled
  //! (UpscaleScene) into the back buffer, which is bound again.
  void EndMainScene();
  //! The HDR passes over the scene (nSceneSRV, nWidth x nHeight): the meter, the adaptation, the
  //! bloom and the resolve, into the back buffer, or with bToTexture into m_Hdr.pLdr, which it
  //! returns (nullptr: the back buffer, or no targets).
  CD3D12Texture *ResolveHdr(int nSceneSRV, int nWidth, int nHeight, bool bToTexture);
  //! The resolve's pass over the whole back buffer; with the highlight layer it writes the layer
  //! too (HdrOutput.cpp).
  void DrawHdrToBackBuffer(const SDrawConstants &dc);
  //! A full screen pass of the HDR program into what is bound, whatever the engine's viewport and
  //! scissors; nTarget and nColorFormat as SPipelineKey's.
  void DrawHdrFullScreen(const SDrawConstants &dc, uint8 nTarget, uint8 nColorFormat);
  //! A pass of the HDR program into pTarget, with the blend of nState; with pRatio (the layer's
  //! ratio at pTarget's size) PSHighlight writes it too.
  void DrawHdrPass(CD3D12Texture *pTarget, const SDrawConstants &dc, uint32 nState = 0, CD3D12Texture *pRatio = nullptr);
  //! m_Hdr's targets for a scene of nWidth x nHeight (and pLdr with bLdr, pRatio with it and the
  //! highlight layer), made when it changes. False: none.
  bool EnsureHdrTargets(int nWidth, int nHeight, bool bLdr);
  void ReleaseHdrTargets();
  //! The device's HDR target and its texture table entry.
  void ReleaseHdrColor();
  void ReleaseHdr();
  //! The frame dump's lines: the settings, the meter and the EV read back.
  void DumpHdr();
  //! With r_HDRRendering, drawing into a target of D3D12_HDR_FORMAT (the main scene, the water's
  //! reflection, the environment cubes) outside heat vision.
  bool IsHdrTarget() const;
  //! There: the factor of a colour that makes white's light fLight times brighter (HdrEncode in
  //! Shaders/Common.hlsli, as the resolve decodes it); else 1. For what the HDR pipeline draws
  //! brighter than the game: the glow maps (d3d12_HDRGlow).
  float HdrBoost(float fLight) const;
  //! There: a light added (a flare, a corona, an additive sprite or surface, drawn
  //! with the blend nState) fLight times brighter in its core (DF2_HDR_CORE), its soft edge as the
  //! game's: the sun (d3d12_HDRSun), what glows (d3d12_HDRGlow). Else nothing.
  void SetHdrCore(SDrawConstants &dc, float fLight, uint32 nState) const;
  //! There: the light of the sky towards the sun times d3d12_HDRSky (DF2_SKY_GLOW,
  //! SFrameConstants::vHdr), with a sun in the scene; else 1.
  float HdrSkyGlow() const;

  SHdrState m_Hdr;
  bool m_bHdrScene = false;                 //!< BeginMainScene .. EndMainScene: the main scene draws in D3D12_HDR_FORMAT

  //////////////////////////////////////////////////////////////////////////
  // HdrOutput.cpp: the HDR output to the display (d3d12_HDROutput)

  //! XRenderD3D12_ReadFrameHdr10 (HdrOutput.cpp): the current frame through the output pass in
  //! HDR10 (PQ, Rec. 2020), R10G10B10A2 rows of nPitch bytes into pDst. The highlight layer is on
  //! from the first call on (pDst NULL: only that, and the size and the levels).
  bool ReadFrameHdr10(void *pDst, int nPitch, int *pWidth, int *pHeight, float *pPaperWhite, float *pPeak);
  //! The output for the next frames (after EndFrame): what d3d12_HDROutput asks for, if
  //! r_HDRRendering is on and the display is in HDR; the highlight layer also for ReadFrameHdr10.
  void UpdateHdrOutput();
  //! The light of paper white (the frame's white) and of the peak, in nits, of the display as it is.
  void GetHdrLevels(const SDisplayInfo &Info, float &fPaperWhite, float &fPeak) const;
  //! The device's frame target and highlight layer in the texture table (again when made anew);
  //! false: there are none.
  bool UpdateLayerSRVs();
  //! The output pass's constants (Hdr.hlsl, HDR_OUTPUT) for Output.
  SDrawConstants HdrOutputConstants(EDisplayOutput Output) const;
  //! Before EndFrame with an HDR output: the frame and its highlights into the swap chain's buffer.
  void DrawHdrOutput();
  //! The frame dump's line (and the share of the frame the highlight layer has the scene of).
  void DumpHdrOutput();
  void ReleaseHdrOutput();
  //! In the constants of the resolve and the upscaler's picture: the highlight layer's.
  void SetHighlightConstants(SDrawConstants &dc) const;

  struct SHdrOutputState
  {
    float fPaperWhite = 203, fPeak = 1000;  //!< the levels in use (nits)
    std::string Reason;                     //!< why the output is SDR (logged when it changes)
    int nLogged = -1;                       //!< the EDisplayOutput logged
    bool bCapture = false;                  //!< ReadFrameHdr10 was called: the highlight layer stays on
    CD3D12Texture *pCapture = nullptr;      //!< ReadFrameHdr10's target (D3D12_HDR10_FORMAT)
    std::vector<uint8> Pixels;              //!< its pixels read back
    ComPtr<ID3D12Resource> pFrame, pHighlight;   //!< the device's the SRVs are of
    int nFrameSRV = -1, nHighlightSRV = -1;
  };
  SHdrOutputState m_HdrOut;

  //////////////////////////////////////////////////////////////////////////
  // ColorGrading.cpp: the colour grading of the HDR pipeline's resolve

  //! The table of the graded display colours and the settings it was made with.
  struct SColorGrading
  {
    CD3D12Texture *pLut = nullptr;          //!< HDR_COLOR_LUT_SIZE^2 x HDR_COLOR_LUT_SIZE, R16G16B16A16_UNORM; nullptr: none (neutral)
    bool bMade = false;                     //!< pLut is of the settings below
    float fSaturation = 1, fContrast = 1, fTemperature = 0, fTint = 0, fLutAmount = 1;
    std::string LutFile;                    //!< d3d12_ColorLut
    std::string LoadedFile;                 //!< the .cube file Cube was read from (an error leaves Cube empty)
    int nCubeSize = 0;                      //!< its LUT_3D_SIZE, 0: none
    std::vector<float> Cube;                //!< its rows, r g b, red changing fastest
    float CubeMin[3] = { 0, 0, 0 }, CubeMax[3] = { 1, 1, 1 };   //!< its domain
  };

  //! The table for the current settings, made when they changed (ColorGrading.cpp): its texture
  //! table index, -1 when every setting is neutral (or it could not be made).
  int UpdateColorGrading();
  //! Reads the .cube file szPath into m_Grading.Cube; false with Error.
  bool LoadColorCube(const char *szPath, std::string &Error);
  void ReleaseColorGrading();

  SColorGrading m_Grading;
  ICVar *m_pColorLutVar = nullptr;          //!< d3d12_ColorLut

  //////////////////////////////////////////////////////////////////////////
  // PostFx.cpp: the screen effects (CScene::GetScreenFx)

  //! A blur of the scene: the copy of the back buffer reduced by a whole factor, then blurred.
  struct SPostBlur
  {
    CD3D12Texture *pTex[2] = {};  //!< the result in [0], and a scratch
    int nWidth = 0, nHeight = 0;  //!< their size
    int nFactor = 1;              //!< the back buffer's size over theirs
  };

  //! After the main view's 3D scene: the screen effects asked for, over the scene drawn.
  void DrawScreenEffects();
  //! Night vision, the screen blur, the flash bang and (bFade) the fade in one pass over a copy of the scene.
  void DrawScreenComposite(const SScreenFxState &fx, bool bBlur, bool bFade);
  //! The targets of Blur for the back buffer reduced by nFactor (made when it changes). False: none.
  bool EnsurePostBlur(SPostBlur &Blur, int nFactor);
  //! The scene into Blur.pTex[0]: reduced, then a gaussian of fSigma of its texels across and down.
  void DrawPostBlur(SPostBlur &Blur, float fSigma);
  //! A pass of the post program into pTarget (one of the blur targets).
  void DrawPostPass(CD3D12Texture *pTarget, const SDrawConstants &dc);
  void ReleasePostBlur(SPostBlur &Blur);
  //! The share of the frame before in this one (r_MotionBlur, r_MotionBlurAmount, the frame time); 0: none.
  float MotionBlurShare();
  //! A full screen draw blended (SRCALPHA, INVSRCALPHA) over the back buffer: the texture nTexSRV
  //! times vColor; its alpha too with bAlpha, else the back buffer's alpha stays.
  void DrawPostBlend(int nTexSRV, const float vColor[4], bool bAlpha);
  void ReleaseMotionHistory();
  void ReleaseScreenEffects();

  SPostBlur m_PostGlow;                     //!< about POST_BLUR_ROWS rows: night vision's glow, the flash's afterimage
  SPostBlur m_PostSoft;                     //!< the screen blur's (its own radius)
  CD3D12Texture *m_pPostGhost = nullptr;    //!< the flash bang's afterimage: the glow blur at the start of the flash
  bool m_bFlashGhost = false;               //!< m_pPostGhost holds the current flash's afterimage
  ComPtr<ID3D12Resource> m_pMotionHistory;  //!< motion blur: the frame before, as the screen effects left it before the fade
  int m_nMotionHistorySRV = -1;
  int m_nMotionHistoryWidth = 0, m_nMotionHistoryHeight = 0;
  int m_nMotionHistoryFrame = -1;           //!< the m_nFrames it was copied in
  ICVar *m_pMotionBlur = nullptr;           //!< r_MotionBlur, looked up on first use
  ICVar *m_pMotionBlurAmount = nullptr;     //!< r_MotionBlurAmount

  //////////////////////////////////////////////////////////////////////////
  // Flares.cpp: the sun's flare and the coronas of the lights

  //! In the main view's blended list, after the rest of the 3D scene: tests and draws the flares
  //! of the scene (its Flare items, the coronas of its lights), with r_Coronas.
  void DrawFlares(std::span<const SRenderItem> Items);
  //! The glow of a procedural flare (eDC_ProcFlare) around the vertices of its element's chunk,
  //! placed in the world by World.
  void DrawProcFlare(const SRenderItem &it, const Matrix44 &World);
  //! After the BeginFrame of a frame slot: the results of its last frame's tests into the flares'
  //! objects.
  void ReadFlareTests();
  //! CCObject::RemovePermanent: the results still to come for pObj are dropped.
  void ForgetFlare(const CCObject *pObj);
  //! The texture of a corona's map (loaded on first use), nullptr when it is not found.
  CTexture *GetFlareTexture(const std::string &Map);
  //! With r_checkSunVis 2: the mask of how much sky there is around the sun at the pixel (sx, sy)
  //! of the screen (m_pSunMask); false when it could not be made.
  bool DrawSunMask(float sx, float sy);
  void ReleaseFlares();

  //! A visibility test waiting for its result: the flare's object, the area of its square on the
  //! screen in pixels.
  struct SFlareTest
  {
    CCObject *pObj;
    float fArea;
  };
  static constexpr int FLARE_SLOTS = 2;     //!< D3D12_FRAMES
  std::vector<SFlareTest> m_FlareTests[FLARE_SLOTS];  //!< by frame slot, in the order of its occlusion queries
  std::vector<std::pair<std::string, CTexture *>> m_FlareTextures;  //!< GetFlareTexture's maps so far (released with every texture)
  ICVar *m_pCoronas = nullptr;              //!< r_Coronas, looked up on first use
  ICVar *m_pCoronaSizeScale = nullptr;      //!< r_CoronaSizeScale
  ICVar *m_pCoronaFade = nullptr;           //!< r_CoronaFade
  ICVar *m_pCheckSunVis = nullptr;          //!< r_checkSunVis
  CD3D12Texture *m_pSunPatch = nullptr;     //!< DrawSunMask: the sky around the sun
  CD3D12Texture *m_pSunMask = nullptr;      //!< DrawSunMask: the mask

  //////////////////////////////////////////////////////////////////////////
  // State (public: the subsystems' files use it)

  std::unique_ptr<CD3D12Device> m_pDevice;
  std::unique_ptr<CD3D12Upload> m_pUpload;
  std::unique_ptr<CD3D12Descriptors> m_pDescriptors;
  std::unique_ptr<CD3D12Pipeline> m_pPipeline;
  std::unique_ptr<CD3D12Compute> m_pCompute;
  std::unique_ptr<CTextureManager> m_pTextures;
  std::unique_ptr<CShaderManager> m_pShaders;
  std::unique_ptr<CScene> m_pScene;

  HWND m_hWnd = nullptr;
  bool m_bOwnWindow = false;
  int m_nWidth = 0, m_nHeight = 0;    //!< GetWidth / GetHeight
  int m_nColorBits = 32, m_nDepthBits = 24, m_nStencilBits = 8;
  bool m_bFullScreen = false;
  int m_nCVarWidth = 0, m_nCVarHeight = 0;  //!< r_Width / r_Height last applied
  RECT m_rcWindowed = {};             //!< the window's outer rect when full screen began (empty: started full screen)
  char m_szDisplayDevice[CCHDEVICENAME] = {};  //!< the display whose mode SetDisplayMode changed (empty: none)
  bool m_bAppActive = true;           //!< WM_ACTIVATEAPP: the game has the focus
  WNDPROC m_pfnGameWndProc = nullptr; //!< the window procedure before ours (FarCry.exe's)
  char m_cType = R_DX9_RENDERER;      //!< SetType
  bool m_bSwapBuffers = true;         //!< EnableSwapBuffers
  bool m_bVSync = false;
  int m_nFrameID = 65535;             //!< GetFrameID, from 65535; the engine keeps a pointer to it (EFQ_Pointer2FrameID)
  int m_nFrames = 0;                  //!< frames ended by Update
  int64 m_nLastFrameTicks = 0;        //!< ReportGpuTimes: when the last frame ended (0: timing off)
  double m_fFrameMs = 0;              //!< ReportGpuTimes: the time between frames added up
  int m_nTimedFrames = 0;
  int m_nRecurseLevel = 1;            //!< EFQ_RecurseLevel: 1 for the main view
  int m_nDeviceLost = 0;              //!< EFQ_DeviceLost
  int m_nPolygonMode = R_SOLID_MODE;

  // Camera and matrices
  CCamera m_Camera;
  Matrix44 m_CameraView;              //!< the view of SetCamera
  Matrix44 m_ViewMatrix;              //!< the current modelview (GetModelViewMatrix): m_CameraView and the matrix stack
  Matrix44 m_ProjMatrix;              //!< GetProjectionMatrix
  Matrix44 m_SkyProjMatrix;           //!< the sky box's: m_ProjMatrix without the water clip of the reflection pass
  static constexpr int MATRIX_STACK = 32;
  Matrix44 m_MatrixStack[MATRIX_STACK];
  int m_nMatrixDepth = 0;
  int m_Viewport[4] = {};             //!< x, y, width, height
  bool m_bScissor = false;
  int m_Scissor[4] = {};              //!< SetScissor: x, y, width, height
  Vec3 m_vClearColor = Vec3(0, 0, 0);

  // Render state (Renderer2D.cpp)
  int m_nState = GS_DEPTHWRITE;       //!< GS_*
  int m_nCullMode = R_CULL_BACK;      //!< R_CULL_*
  int m_nCurTexture[2] = { -1, -1 };  //!< SetTexture per TMU, -1 none
  int m_nCurTMU = 0;
  bool m_bTexClamp = false;
  bool m_b2DMode = false;
  float m_fOrthoWidth = 800, m_fOrthoHeight = 600;
  float m_MaterialColor[4] = { 1, 1, 1, 1 };
  byte m_ColorOp[4] = {};
  bool m_bTexMatrix = false;
  float m_TexMatrix[6] = { 1, 0, 0, 0, 1, 0 };  //!< 2x3, TransformTextureMatrix
  bool m_bTexGen = false;
  float m_TexGen[8] = {};             //!< SetTexgen / SetTexgen3D: the u and v planes
  bool m_bFog = false;
  float m_fFogStart = 0, m_fFogEnd = 1;
  float m_FogColor[4] = {};
  float m_WorldColor[4] = { 1, 1, 1, 1 };  //!< EF_SetWorldColor
  bool m_bDepthBias = false;
  bool m_bHeatVision = false;               //!< EF_EnableHeatVision
  bool m_bHeatPass = false;                 //!< the main view's items are drawn in heat vision (DF2_HEAT)
  float m_fDrawHeat = 0;                    //!< the heat of the item being drawn (vHeat.x)
  float m_fLineWidth = 1;

  // Console variables
  int CV_r_vsync = 0;
  int CV_r_Quality_BumpMapping = 2;
  int CV_r_WaterReflections = 1;
  int CV_r_WaterRefractions = 0;
  float CV_r_WaterUpdateFactor = 0.01f;     //!< the water reflection is redrawn after this many seconds per metre to the water, at most 0.3 s,
  float CV_r_WaterUpdateDistance = 2;       //!< or when the camera moved this far,
  float CV_r_WaterUpdateDeltaAngle = 5;     //!< or turned by this many degrees
  int CV_r_EnvCMResolution = 2;             //!< the environment cubes' faces are 64 << this texels wide (0 to 2)
  float CV_r_EnvCMupdateInterval = 0.1f;    //!< seconds between two faces of an environment cube
  int CV_r_DisableSfx = 0;
  int CV_r_ResetScreenFx = 0;
  int CV_r_TexResolution = 0;         //!< mip levels dropped from loaded textures
  int CV_r_TexBumpResolution = 0;     //!< the same for bump maps
  int CV_r_TexSkyResolution = 0;
  int CV_r_DetailTextures = 1;        //!< 0: the terrain's detail layers are not drawn
  float CV_r_DetailScale = 8;         //!< metres per repeat of the detail texture
  float CV_r_DetailDistance = 4;      //!< the terrain's detail layers fade out from here to twice this far
  int CV_d3d12_debug = 0;             //!< debug layer (also XRENDERD3D12_DEBUG=1); only read at Init
  int CV_d3d12_DumpFrame = 0;         //!< non-zero: the next frame's 3D draws go to XRenderD3D12_frame.txt, then 0
  ICVar *m_pVolumetricFog = nullptr;  //!< r_VolumetricFog, looked up on first use
  ICVar *m_pScopeLens = nullptr;      //!< r_ScopeLens_fx, looked up on first use
  ICVar *m_pHdrRendering = nullptr;   //!< r_HDRRendering, looked up on first use
  int CV_d3d12_WaterDebug = 0;        //!< non-zero: the water shows one term of its colour (Water.hlsl)
  int CV_d3d12_Caustics = 1;          //!< 0: no caustics under the ocean
  float CV_d3d12_WaterSunGlint = 0;   //!< strength of the sun's highlight on water (0: none)
  int CV_d3d12_GpuTimes = 0;          //!< N: the GPU time of the frame's sections to the log every N frames
  float CV_d3d12_WaterReflectionScale = 0.5f;   //!< size of the water reflection, a fraction of the screen's
  float CV_d3d12_WaterReflectionBlur = 0.1f; //!< how much the ripple finer than a pixel blurs the water's reflection (0: none)
  int CV_d3d12_WaterReflectionFlags = DLD_TERRAIN | DLD_TERRAIN_FULLRES | DLD_STATIC_OBJECTS | DLD_FAR_SPRITES;   //!< what the water reflects (EDrawLowDetailFlags)
  int CV_d3d12_SpriteRes = 128;             //!< size of the far vegetation sprites' textures (made at the level load)
  int CV_d3d12_Shadows = 1;                 //!< 0: no shadow maps
  int CV_d3d12_ShadowMapSize = 1024;        //!< size of the shadow maps (0: the engine's, 512)
  int CV_d3d12_ShadowDebug = 0;             //!< non-zero: the shadow maps of the frame are shown
  int CV_d3d12_PixelLighting = 1;           //!< 1: every lit material is lit per pixel, the light mapped ones by the dynamic lights too (DrawLeafItem)
  int CV_d3d12_ShadowCascades = 4;          //!< the sun's cascaded shadow maps: how many (0: none)
  float CV_d3d12_ShadowDistance = 120;      //!< how far they reach from the camera (m)
  int CV_d3d12_ShadowCascadeSize = 2048;    //!< the size of their maps
  float CV_d3d12_ShadowSoftness = 0.05f;    //!< the radius of their filter (m)
  int CV_d3d12_UpscaleMode = 0;             //!< EUpscaleMode of the upscaler (d3d12_Upscaler, a string)
  float CV_d3d12_UpscaleSharpness = 0.5f;   //!< the upscaler's sharpening, 0 (none) to 1
  int CV_d3d12_UpscaleDebug = 0;            //!< non-zero: the upscaler's debug view
  int CV_d3d12_AOQuality = 2;               //!< the ambient occlusion's (d3d12_AO, a string) quality, 0 to 3
  float CV_d3d12_AORadius = 1;              //!< its radius (m)
  float CV_d3d12_AOStrength = 1;            //!< the exponent of its visibility
  int CV_d3d12_AODenoise = 2;               //!< its denoise passes (XeGTAO) or blur passes (CACAO)
  int CV_d3d12_AODebug = 0;                 //!< 1: its factor in grey
  int CV_d3d12_ToneMap = 1;                 //!< the HDR pipeline's tone map: 0 clamp, 1 soft shoulder, 2 ACES fitted, 3 AgX
  float CV_d3d12_ToneMapKnee = 0.85f;       //!< where the soft shoulder starts (a display value)
  float CV_d3d12_Exposure = 0;              //!< the exposure's compensation, EV
  int CV_d3d12_AutoExposure = 1;            //!< 0: the exposure is 2^d3d12_Exposure alone
  float CV_d3d12_ExposureMin = 0;           //!< the automatic exposure's range, EV
  float CV_d3d12_ExposureMax = 1.5f;
  float CV_d3d12_ExposureSpeed = 1.5f;      //!< its adaptation rate per second (twice that towards a brighter scene)
  float CV_d3d12_Bloom = 0.5f;              //!< the bloom's strength (0: none)
  float CV_d3d12_BloomThreshold = 1;        //!< the light above which it blooms (exposed, linear)
  int CV_d3d12_HDRDebug = 0;                //!< 1: the luminance in EV bands, 2: the bloom alone
  float CV_d3d12_HDRSun = 8;                //!< the light of the sun's flare in HDR, times the game's
  float CV_d3d12_HDRSky = 2;                //!< the light of the sky towards the sun in HDR, times the game's
  float CV_d3d12_HDRGlow = 3;               //!< the light of what glows in HDR, times the game's
  int CV_d3d12_HDROutput = 0;               //!< the output to the display: 0 SDR, 1 HDR10, 2 scRGB (HdrOutput.cpp)
  float CV_d3d12_HDRPaperWhite = 0;         //!< its paper white in nits (0: Windows' SDR white level of the display)
  float CV_d3d12_HDRPeak = 0;               //!< its peak in nits (0: the display's)
  int CV_d3d12_HDROutputDebug = 0;          //!< 1: the highlight mask, 2: test patches
  float CV_d3d12_Saturation = 1;            //!< the colour grading (ColorGrading.cpp): 1, 1, 0, 0 neutral
  float CV_d3d12_Contrast = 1;
  float CV_d3d12_Temperature = 0;
  float CV_d3d12_Tint = 0;
  float CV_d3d12_ColorLutAmount = 1;        //!< how much of the .cube file d3d12_ColorLut (a string) is mixed in
};

extern CD3D12Renderer *g_pRenderer;

//! A half float (IEEE 754 binary16) as a float (Hdr.cpp).
float HalfToFloat(uint16 h);
