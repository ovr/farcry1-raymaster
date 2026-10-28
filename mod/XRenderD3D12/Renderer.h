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
//	The state of a subsystem lives in its own class (m_pTextures, m_pShaders, m_pScene, ...).
//	All calls come from one thread.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include "D3D12Pipeline.h"
#include "Shaders/Constants.h"

class CD3D12Device;
class CD3D12Upload;
class CD3D12Descriptors;
class CD3D12Pipeline;
class CTexture;
class CTextureManager;
class CShaderManager;
class CScene;
struct SRenderItem;

class CD3D12Renderer : public IRenderer
{
public:
  CD3D12Renderer();
  ~CD3D12Renderer();

  //! The console variables (PackageRenderConstructor, before SetType and Init).
  void RegisterVariables();
  //! The variables of the original renderer this one does not read (ConsoleVariables.cpp).
  void RegisterOtherVariables();

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
  //! The engine's r_Width / r_Height changed: new window and back buffer size (checked in Update).
  void CheckResolutionChange();
  //! The projection of the main view from the camera.
  static Matrix44 MakeProjection(const CCamera &cam);

  //////////////////////////////////////////////////////////////////////////
  // Draw.cpp: every draw goes through here

  //! b0 of the following draws is made again from the camera, sun, fog and screen (after SetCamera,
  //! a new sun, ...).
  void InvalidateFrameConstants() { m_nFrameCB = 0; }
  //! The frame constants of the current camera and settings.
  void MakeFrameConstants(SFrameConstants &fc) const;
  //! One draw: the pipeline state of Key, dc as b1, VB in slot 0 with the default stream in slot 1
  //! and pTexCoord1 in slot 2, the index buffer when pIB; nCount indices (or vertices) from nFirst.
  //! Viewport m_Viewport with the depth range m_fDepthMin..m_fDepthMax; scissor from SetScissor or
  //! m_bItemScissor.
  void DrawGeometry(const SPipelineKey &Key, const SDrawConstants &dc, D3D_PRIMITIVE_TOPOLOGY eTopology, const D3D12_VERTEX_BUFFER_VIEW &VB,
    const D3D12_INDEX_BUFFER_VIEW *pIB, uint32 nCount, uint32 nFirst = 0, int nBaseVertex = 0, const D3D12_VERTEX_BUFFER_VIEW *pTexCoord1 = nullptr);
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
  D3D12_GPU_VIRTUAL_ADDRESS m_nFrameCB = 0; //!< b0, 0 when it has to be made again
  float m_SunDir[3] = { 0, 0, 1 };          //!< towards the sun, world space (frame constants)
  float m_SunColor[3] = { 1, 1, 1 };

  //////////////////////////////////////////////////////////////////////////
  // Renderer2D.cpp helpers

  //! Key and constants from the render state of SetState, SetCullMode, SetTexture, SetColorOp,
  //! the texture matrix and the polygon mode. bVertexColor: the vertices have colours.
  void ApplyRenderState(SPipelineKey &Key, SDrawConstants &dc, int nVertexFormat, bool bVertexColor);
  //! The transform of immediate draws: the 2D mode's orthographic projection, else the camera.
  Matrix44 GetImmediateViewProj() const;

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
  //! The lights of an object besides the sun as the draw's own lights (SDrawConstants::vLightPos).
  void SetObjectLights(SDrawConstants &dc, const CCObject *pObj, const Matrix44 &World, CLeafBuffer *pLB);
  //! The detail texture of the terrain sectors (TerrainWithDefaultDetailTexture: always
  //! textures/terrain/detail/default, whatever r_DetailDefault and r_DetailTextures say); NULL
  //! without one.
  CTexture *GetDetailTexture();
  //! The sky box of a CRESky element drawn with a sky shader (CShaderManager::FindSkyBox).
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

  int m_nSceneFlags = 0;                    //!< SHDF_* of the EF_EndEf3D being drawn
  CTexture *m_pDetailTexture = nullptr;     //!< GetDetailTexture (one reference)
  //! The textures of a sky box: <name>_5 (the top), <name>_12 (the sides x = 32 and y = -32),
  //! <name>_34 (the sides x = -32 and y = 32); one reference each.
  struct SSkyBoxTextures
  {
    std::string Name;
    CTexture *pTex[3] = {};
  };
  std::vector<SSkyBoxTextures> m_SkyBoxes;  //!< DrawSkyItem, the sky boxes used so far
  int m_nSceneSkyBox = -1;                  //!< the m_SkyBoxes entry the scene's sky drew, -1 none (the water reflects it)
  float m_fSceneSkyZ1 = 0;                  //!< where its sides end (DrawSkyItem)
  float m_fSceneSkyAlpha = 1;
  Matrix44 m_NearestProj;                   //!< FOB_NEAREST objects

  //////////////////////////////////////////////////////////////////////////
  // Water.cpp

  //! Key and constants of a water surface (eDC_Water): the water program over the copy of the
  //! scene drawn so far (made before the first surface of the list). False: not drawn.
  bool SetupWaterDraw(SPipelineKey &Key, SDrawConstants &dc, const SRenderItem &it);
  //! The foam on the beach (eDC_WaterBeach), wave nWave (0, 1). False: not drawn.
  bool SetupBeachDraw(SPipelineKey &Key, SDrawConstants &dc, int nWave);
  //! The sea floor under the ocean (eDC_WaterBottom) with the element's texture pTex (may be NULL).
  void SetupWaterBottomDraw(SPipelineKey &Key, SDrawConstants &dc, CTexture *pTex);
  //! Copies the back buffer and the depth buffer as drawn so far (m_bSceneCopied).
  bool CopySceneForWater();
  void ReleaseWaterResources();
  //! The texture index of the wave slopes the water program samples (made on first use).
  int GetWaveTexture();
  //! Before the main view is drawn (Items: its render items): with r_WaterReflections and an
  //! ocean surface among the items, the scene mirrored in the water plane into m_pReflectionColor,
  //! when the last one is out of date (r_WaterUpdate*): the 3D engine renders it with
  //! DrawLowDetail (the sky, the terrain), seen by the camera mirrored in the water plane.
  void UpdateWaterReflection(std::span<const SRenderItem> Items);
  void ReleaseWaterReflection();
  //! The reflection pass's projection: its near plane replaced by the water plane, so that nothing
  //! under the water is drawn into the reflection.
  void ClipProjectionToWater(Matrix44 &Proj, const Matrix44 &View) const;

  ComPtr<ID3D12Resource> m_pSceneColorCopy, m_pSceneDepthCopy;
  int m_nSceneColorSRV = -1, m_nSceneDepthSRV = -1;
  int m_nSceneCopyWidth = 0, m_nSceneCopyHeight = 0;
  bool m_bSceneCopied = false;              //!< the copies hold the current list's scene
  CTexture *m_pBeachTexture = nullptr;      //!< textures/wave1 (one reference)
  int m_nWaveTexture = 0;                   //!< GetWaveTexture: its ID, 0 not made yet, -1 failed
  ComPtr<ID3D12Resource> m_pReflectionColor, m_pReflectionDepth;
  D3D12_CPU_DESCRIPTOR_HANDLE m_hReflectionRTV = {}, m_hReflectionDSV = {};
  int m_nReflectionSRV = -1;
  int m_nReflectionWidth = 0, m_nReflectionHeight = 0;
  bool m_bReflectionPass = false;           //!< the reflection's scene is being rendered
  bool m_bReflectionValid = false;          //!< m_pReflectionColor holds a reflection for this frame's ocean
  float m_fReflectionWaterZ = 0;            //!< the water level it mirrors
  float m_fReflectionTime = 0;              //!< ITimer time it was drawn at
  Vec3 m_vReflectionCamPos, m_vReflectionCamDir;  //!< the frame's camera it was drawn for
  Matrix44 m_ReflectionViewProj;            //!< world to clip of the mirrored camera (without the water clip)

  //////////////////////////////////////////////////////////////////////////
  // State (public: the subsystems' files use it)

  std::unique_ptr<CD3D12Device> m_pDevice;
  std::unique_ptr<CD3D12Upload> m_pUpload;
  std::unique_ptr<CD3D12Descriptors> m_pDescriptors;
  std::unique_ptr<CD3D12Pipeline> m_pPipeline;
  std::unique_ptr<CTextureManager> m_pTextures;
  std::unique_ptr<CShaderManager> m_pShaders;
  std::unique_ptr<CScene> m_pScene;

  HWND m_hWnd = nullptr;
  bool m_bOwnWindow = false;
  int m_nWidth = 0, m_nHeight = 0;    //!< GetWidth / GetHeight
  int m_nColorBits = 32, m_nDepthBits = 24, m_nStencilBits = 8;
  bool m_bFullScreen = false;
  int m_nCVarWidth = 0, m_nCVarHeight = 0;  //!< r_Width / r_Height last applied
  char m_cType = R_DX9_RENDERER;      //!< SetType
  bool m_bSwapBuffers = true;         //!< EnableSwapBuffers
  bool m_bVSync = false;
  int m_nFrameID = 65535;             //!< GetFrameID, from 65535; the engine keeps a pointer to it (EFQ_Pointer2FrameID)
  int m_nFrames = 0;                  //!< frames ended by Update
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
  bool m_bHeatVision = false;
  float m_fLineWidth = 1;

  // Console variables
  int CV_r_vsync = 0;
  int CV_r_Quality_BumpMapping = 2;
  int CV_r_WaterReflections = 1;
  int CV_r_WaterRefractions = 0;
  float CV_r_WaterUpdateFactor = 0.01f;     //!< the water reflection is redrawn after this many seconds per metre to the water, at most 0.3 s,
  float CV_r_WaterUpdateDistance = 2;       //!< or when the camera moved this far,
  float CV_r_WaterUpdateDeltaAngle = 5;     //!< or turned by this many degrees
  int CV_r_DisableSfx = 0;
  int CV_r_ResetScreenFx = 0;
  int CV_r_TexResolution = 0;         //!< mip levels dropped from loaded textures
  int CV_r_TexBumpResolution = 0;     //!< the same for bump maps
  int CV_r_TexSkyResolution = 0;
  int CV_r_DetailTextures = 1;        //!< the terrain's detail layers (not drawn)
  float CV_r_DetailScale = 8;         //!< metres per repeat of the detail texture
  int CV_d3d12_debug = 0;             //!< debug layer (also XRENDERD3D12_DEBUG=1); only read at Init
  int CV_d3d12_DumpFrame = 0;         //!< non-zero: the next frame's 3D draws go to XRenderD3D12_frame.txt, then 0
  int CV_d3d12_WaterDebug = 0;        //!< non-zero: the water shows one term of its colour (Water.hlsl)
  float CV_d3d12_WaterReflectionScale = 0.5f;   //!< size of the water reflection, a fraction of the screen's
  int CV_d3d12_WaterReflectionFlags = DLD_TERRAIN | DLD_TERRAIN_FULLRES;   //!< what the water reflects (EDrawLowDetailFlags)
};

extern CD3D12Renderer *g_pRenderer;
