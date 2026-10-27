//////////////////////////////////////////////////////////////////////
//
//	File: RendererProxy.h
//	Description: IRenderer that traces every call and forwards it to the
//	original renderer.
//
//	The methods are implemented in
//	  RendererProxy.cpp        - device, frame, 2D/3D drawing, matrices, textures, fonts
//	  RendererProxyShaders.cpp - EF_* (shaders, render elements, objects, lights)
//	  RendererProxyBuffers.cpp - vertex/index/leaf buffers
//	Objects the original renderer returns (shaders, textures, leaf buffers,
//	render elements, CCObject) are handed to the engine unwrapped, so calls
//	made directly on them are not traced.
//
//////////////////////////////////////////////////////////////////////

#pragma once

//! Formats the arguments of a variadic IRenderer method into szBuf, so they can be forwarded with "%s".
#define PROXY_VFORMAT(szBuf, szFormat)   char szBuf[2048];   {     va_list args;     va_start(args, szFormat);     if (szFormat)       _vsnprintf(szBuf, sizeof(szBuf) - 1, szFormat, args);     else       szBuf[0] = 0;     va_end(args);     szBuf[sizeof(szBuf) - 1] = 0;   }

class CRendererProxy : public IRenderer
{
public:
  //! nRealType is the R_*_RENDERER type of the original renderer, -1 if unknown.
  CRendererProxy(IRenderer *pReal, int nRealType);
  ~CRendererProxy();

  IRenderer *GetReal() const { return m_pReal; }

  //! The original renderer behind pRenderer, if it is a proxy.
  static IRenderer *Unwrap(IRenderer *pRenderer);

  // IRenderer, in the order of the SDK header.
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
  virtual void DrawTriStrip(CVertexBuffer *src, int vert_num=4) override;
  virtual void *GetDynVBPtr(int nVerts, int &nOffs, int Pool) override;
  virtual void DrawDynVB(int nOffs, int Pool, int nVerts) override;
  virtual void DrawDynVB(struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F *pBuf, ushort *pInds, int nVerts, int nInds, int nPrimType) override;
  virtual void SetFenceCompleted(CVertexBuffer * buffer) override;
  virtual CVertexBuffer *CreateBuffer(int vertexcount,int vertexformat, const char *szSource, bool bDynamic=false) override;
  virtual void ReleaseBuffer(CVertexBuffer *bufptr) override;
  virtual void DrawBuffer(CVertexBuffer *src,SVertexStream *indicies,int numindices, int offsindex, int prmode,int vert_start=0,int vert_stop=0, CMatInfo *mi=NULL) override;
  virtual void UpdateBuffer(CVertexBuffer *dest,const void *src,int vertexcount, bool bUnLock, int nOffs=0, int Type=0) override;
  virtual void CreateIndexBuffer(SVertexStream *dest,const void *src,int indexcount) override;
  virtual void UpdateIndexBuffer(SVertexStream *dest,const void *src, int indexcount, bool bUnLock=true) override;
  virtual void ReleaseIndexBuffer(SVertexStream *dest) override;
  virtual void CheckError(const char *comment) override;
  virtual void Draw3dBBox(const Vec3 &mins,const Vec3 &maxs, int nPrimType=DPRIM_WHIRE_BOX) override;
  virtual void Draw3dPrim(const Vec3 &mins,const Vec3 &maxs, int nPrimType=DPRIM_WHIRE_BOX, const float* fRGBA = NULL) override;
  virtual void SetCamera(const CCamera &cam) override;
  virtual const CCamera& GetCamera() override;
  virtual bool SetGammaDelta(const float fGamma) override;
  virtual bool ChangeDisplay(unsigned int width,unsigned int height,unsigned int cbpp) override;
  virtual void ChangeViewport(unsigned int x,unsigned int y,unsigned int width,unsigned int height) override;
  virtual bool SaveTga(unsigned char *sourcedata,int sourceformat,int w,int h,const char *filename,bool flip) override;
  virtual void SetTexture(int tnum, ETexType Type=eTT_Base) override;
  virtual void SetWhiteTexture() override;
  virtual void WriteXY(CXFont *currfont,int x,int y, float xscale,float yscale,float r,float g,float b,float a,const char *message, ...) override;
  virtual void Draw2dText( float posX,float posY,const char *szText,SDrawTextInfo &info ) override;
  virtual void Draw2dImage (float xpos,float ypos,float w,float h,int texture_id,float s0=0,float t0=0,float s1=1,float t1=1,float angle=0,float r=1,float g=1,float b=1,float a=1,float z=1) override;
  virtual void DrawImage(float xpos,float ypos,float w,float h,int texture_id,float s0,float t0,float s1,float t1,float r,float g,float b,float a) override;
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
  virtual void WriteDDS(byte *dat, int wdt, int hgt, int Size, const char *name, EImFormat eF, int NumMips) override;
  virtual void WriteTGA(byte *dat, int wdt, int hgt, const char *name, int bits) override;
  virtual void WriteJPG(byte *dat, int wdt, int hgt, char *name) override;
  virtual bool FontUploadTexture(class CFBitmap*, ETEX_Format eTF=eTF_8888) override;
  virtual int FontCreateTexture(int Width, int Height, byte *pData, ETEX_Format eTF=eTF_8888) override;
  virtual bool FontUpdateTexture(int nTexId, int X, int Y, int USize, int VSize, byte *pData) override;
  virtual void FontReleaseTexture(class CFBitmap *pBmp) override;
  virtual void FontSetTexture(class CFBitmap*, int nFilterMode) override;
  virtual void FontSetTexture(int nTexId, int nFilterMode) override;
  virtual void FontSetRenderingState(unsigned long nVirtualScreenWidth, unsigned long nVirtualScreenHeight) override;
  virtual void FontSetBlending(int src, int dst) override;
  virtual void FontRestoreRenderingState() override;
  virtual bool EF_PrecacheResource(IShader *pSH, float fDist, float fTimeToReady, int Flags) override;
  virtual bool EF_PrecacheResource(ITexPic *pTP, float fDist, float fTimeToReady, int Flags) override;
  virtual bool EF_PrecacheResource(CLeafBuffer *pPB, float fDist, float fTimeToReady, int Flags) override;
  virtual bool EF_PrecacheResource(CDLight *pLS, float fDist, float fTimeToReady, int Flags) override;
  virtual void EF_EnableHeatVision(bool bEnable) override;
  virtual bool EF_GetHeatVision() override;
  virtual void EF_PolygonOffset(bool bEnable, float fFactor, float fUnits) override;
  virtual void EF_AddPolyToScene3D(int Ef, int numPts, SColorVert *verts, CCObject *obj=NULL, int nFogID=0) override;
  virtual CCObject *EF_AddSpriteToScene(int Ef, int numPts, SColorVert *verts, CCObject *obj, byte *inds=NULL, int ninds=0, int nFogID=0) override;
  virtual void EF_AddPolyToScene2D(int Ef, int numPts, SColorVert2D *verts) override;
  virtual void EF_AddPolyToScene2D(SShaderItem si, int nTempl, int numPts, SColorVert2D *verts) override;
  virtual IShader *EF_LoadShader (const char *name, EShClass Class, int flags=0, uint64 nMaskGen=0) override;
  virtual SShaderItem EF_LoadShaderItem (const char *name, EShClass Class, bool bShare, const char *templName, int flags=0, SInputShaderResources *Res=NULL, uint64 nMaskGen=0) override;
  virtual bool EF_ReloadFile (const char *szFileName) override;
  virtual void EF_ReloadShaderFiles (int nCategory) override;
  virtual void EF_ReloadTextures () override;
  virtual IShader *EF_CopyShader(IShader *ef) override;
  virtual ITexPic *EF_GetTextureByID(int Id) override;
  virtual ITexPic *EF_LoadTexture(const char* nameTex, uint flags, uint flags2, byte eTT, float fAmount1=-1.0f, float fAmount2=-1.0f, int Id=-1, int BindId=0) override;
  virtual int EF_LoadLightmap (const char *name) override;
  virtual bool EF_ScanEnvironmentCM (const char *name, int size, Vec3& Pos) override;
  virtual int EF_ReadAllImgFiles(IShader *ef, SShaderTexUnit *tl, STexAnim *ta, char *name) override;
  virtual char **EF_GetShadersForFile(const char *File, int num) override;
  virtual SLightMaterial *EF_GetLightMaterial(char *Str) override;
  virtual bool EF_RegisterTemplate(int nTemplId, char *Name, bool bReplace) override;
  virtual void EF_AddSplash (Vec3 Pos, eSplashType eST, float fForce, int Id=-1) override;
  virtual bool EF_HideTemplate(const char *name) override;
  virtual bool EF_UnhideTemplate(const char *name) override;
  virtual bool EF_UnhideAllTemplates() override;
  virtual bool EF_SetLightHole(Vec3 vPos, Vec3 vNormal, int idTex, float fScale=1.0f, bool bAdditive=true) override;
  virtual CRendElement *EF_CreateRE (EDataType edt) override;
  virtual void EF_StartEf () override;
  virtual CCObject *EF_GetObject (bool bTemp=false, int num=-1) override;
  virtual void EF_AddEf (int NumFog, CRendElement *re, IShader *ef, SRenderShaderResources *sr, CCObject *obj, int nTempl, IShader *efState=0, int nSort=0) override;
  virtual void EF_EndEf3D (int nFlags) override;
  virtual bool EF_IsFakeDLight (CDLight *Source) override;
  virtual void EF_ADDDlight(CDLight *Source) override;
  virtual void EF_ClearLightsList() override;
  virtual bool EF_UpdateDLight(CDLight *pDL) override;
  virtual void EF_EndEf2D(bool bSort) override;
  virtual bool EF_DrawEfForName(char *name, float x, float y, float width, float height, CFColor& col, int nTempl=-1) override;
  virtual bool EF_DrawEfForNum(int num, float x, float y, float width, float height, CFColor& col, int nTempl=-1) override;
  virtual bool EF_DrawEf(IShader *ef, float x, float y, float width, float height, CFColor& col, int nTempl=-1) override;
  virtual bool EF_DrawEf(SShaderItem si, float x, float y, float width, float height, CFColor& col, int nTempl=-1) override;
  virtual bool EF_DrawPartialEfForName(char *name, SVrect *vr, SVrect *pr, CFColor& col) override;
  virtual bool EF_DrawPartialEfForNum(int num, SVrect *vr, SVrect *pr, CFColor& col) override;
  virtual bool EF_DrawPartialEf(IShader *ef, SVrect *vr, SVrect *pr, CFColor& col, float iwdt=0, float ihgt=0) override;
  virtual void *EF_Query(int Query, int Param=0) override;
  virtual void EF_ConstructEf(IShader *Ef) override;
  virtual void EF_SetWorldColor(float r, float g, float b, float a=1.0f) override;
  virtual int EF_RegisterFogVolume(float fMaxFogDist, float fFogLayerZ, CFColor color, int nIndex=-1, bool bCaustics=false) override;
  virtual int GetPolyCount() override;
  virtual void GetPolyCount(int &nPolygons,int &nShadowVolPolys) override;
  virtual void SetClearColor(const Vec3 & vColor) override;
  virtual CLeafBuffer * CreateLeafBuffer(bool bDynamic, const char *szSource="Unknown", class CIndexedMesh * pIndexedMesh=0) override;
  virtual CLeafBuffer * CreateLeafBufferInitialized( void * pVertBuffer, int nVertCount, int nVertFormat, ushort* pIndices, int nIndices, int nPrimetiveType, const char *szSource, EBufferType eBufType = eBT_Dynamic, int nMatInfoCount=1, int nClientTextureBindID=0, bool (*PrepareBufferCallback)(CLeafBuffer *, bool)=NULL, void *CustomData=NULL, bool bOnlyVideoBuffer=false, bool bPrecache=true) override;
  virtual void DeleteLeafBuffer(CLeafBuffer * pLBuffer) override;
  virtual int GetFrameID(bool bIncludeRecursiveCalls=true) override;
  virtual void MakeMatrix(const Vec3 & pos, const Vec3 & angles,const Vec3 & scale, Matrix44* mat) override;
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
  virtual void EnableTMU(bool enable) override;
  virtual void SelectTMU(int tnum) override;
  virtual unsigned int DownLoadToVideoMemory(unsigned char *data,int w, int h, ETEX_Format eTFSrc, ETEX_Format eTFDst, int nummipmap, bool repeat=true, int filter=FILTER_BILINEAR, int Id=0, char *szCacheName=NULL, int flags=0) override;
  virtual void UpdateTextureInVideoMemory(uint tnum, unsigned char *newdata,int posx,int posy,int w,int h,ETEX_Format eTFSrc=eTF_0888) override;
  virtual unsigned int LoadTexture(const char * filename,int *tex_type=NULL,unsigned int def_tid=0,bool compresstodisk=true,bool bWarn=true) override;
  virtual bool DXTCompress( byte *raw_data,int nWidth,int nHeight,ETEX_Format eTF, bool bUseHW, bool bGenMips, int nSrcBytesPerPix, MIPDXTcallback callback=0) override;
  virtual bool DXTDecompress(byte *srcData,byte *dstData, int nWidth,int nHeight,ETEX_Format eSrcTF, bool bUseHW, int nDstBytesPerPix) override;
  virtual void RemoveTexture(unsigned int TextureId) override;
  virtual void RemoveTexture(ITexPic * pTexPic) override;
  virtual void TextToScreen(float x, float y, const char * format, ...) override;
  virtual void TextToScreenColor(int x, int y, float r, float g, float b, float a, const char * format, ...) override;
  virtual void ResetToDefault() override;
  virtual int GenerateAlphaGlowTexture(float k) override;
  virtual void SetMaterialColor(float r, float g, float b, float a) override;
  virtual int LoadAnimatedTexture(const char * format,const int nCount) override;
  virtual void RemoveAnimatedTexture(AnimTexInfo * pInfo) override;
  virtual AnimTexInfo * GetAnimTexInfoFromId(int nId) override;
  virtual void Draw2dLine (float x1,float y1,float x2,float y2) override;
  virtual void SetLineWidth(float fWidth) override;
  virtual void DrawLine(const Vec3 & vPos1, const Vec3 & vPos2) override;
  virtual void DrawLineColor(const Vec3 & vPos1, const CFColor & vColor1, const Vec3 & vPos2, const CFColor & vColor2) override;
  virtual void Graph(byte *g, int x, int y, int wdt, int hgt, int nC, int type, char *text, CFColor& color, float fScale) override;
  virtual void DrawBall(float x, float y, float z, float radius) override;
  virtual void DrawBall(const Vec3 & pos, float radius ) override;
  virtual void DrawPoint(float x, float y, float z, float fSize = 0.0f) override;
  virtual void FlushTextMessages() override;
  virtual void DrawObjSprites(list2<CStatObjInst*> *pList, float fMaxViewDist, CObjManager *pObjMan) override;
  virtual void DrawQuad(const Vec3 &right, const Vec3 &up, const Vec3 &origin,int nFlipMode=0) override;
  virtual void DrawQuad(float dy,float dx, float dz, float x, float y, float z) override;
  virtual void ClearDepthBuffer() override;
  virtual void ClearColorBuffer(const Vec3 vColor) override;
  virtual void ReadFrameBuffer(unsigned char * pRGB, int nSizeX, int nSizeY, bool bBackBuffer, bool bRGBA, int nScaledX=-1, int nScaledY=-1) override;
  virtual void SetFogColor(float * color) override;
  virtual void TransformTextureMatrix(float x, float y, float angle, float scale) override;
  virtual void ResetTextureMatrix() override;
  virtual char GetType() override;
  virtual char* GetVertexProfile(bool bSupportedProfile) override;
  virtual char* GetPixelProfile(bool bSupportedProfile) override;
  virtual void SetType(char type) override;
  virtual unsigned int MakeSprite(float object_scale, int tex_size, float angle, IStatObj * pStatObj, uchar * pTmpBuffer, uint def_tid) override;
  virtual unsigned int Make3DSprite(int nTexSize, float fAngleStep, IStatObj * pStatObj) override;
  virtual ShadowMapFrustum * MakeShadowMapFrustum(ShadowMapFrustum * lof, ShadowMapLightSource * pLs, const Vec3 & obj_pos, list2<IStatObj*> * pStatObjects, int shadow_type) override;
  virtual void Set2DMode(bool enable, int ortox, int ortoy) override;
  virtual int ScreenToTexture() override;
  virtual void SetTexClampMode(bool clamp) override;
  virtual void EnableSwapBuffers(bool bEnable) override;
  virtual WIN_HWND GetHWND() override;
  virtual void OnEntityDeleted(IEntityRender * pEntityRender) override;
  virtual void SetGlobalShaderTemplateId(int nTemplateId) override;
  virtual int GetGlobalShaderTemplateId() override;
  virtual int EnumAAFormats(TArray<SAAFormat>& Formats, bool bReset) override;
  virtual int CreateRenderTarget (int nWidth, int nHeight, ETEX_Format eTF) override;
  virtual bool DestroyRenderTarget (int nHandle) override;
  virtual bool SetRenderTarget (int nHandle) override;
  virtual float EF_GetWaterZElevation(float fX, float fY) override;

private:
  //! Runs console commands separated by ';' (outside any traced call).
  void ExecuteCommands(const char *szCommands);
  //! "screenfx Effect 1", "screenfx Effect.Param 0.5" (or i5 for an integer), "screenfx ?Effect",
  //! "screenfx ?Effect.Param": I3DEngine::SetScreenFx, SetScreenFxParam, GetScreenFx, GetScreenFxParam.
  void ScreenFxCommand(const char *szArgs);
  void ParticlesCommand(const char *szArgs);
  //! "cvars prefix": the console variables whose names start with prefix (all without one).
  void CVarsCommand(const char *szArgs);
  //! "veggroup id|* field value": changes a member of vegetation groups (I3DEngine::GetStatInstGroup,
  //! SetStatInstGroup); fields fadeSize, useSprites, maxViewDistRatio, spriteDistRatio, brightness.
  void VegGroupCommand(const char *szArgs);
  //! "decal size texture [x y [lifetime]]": I3DEngine::CreateDecal where the ray through a point of
  //! the screen (fractions of its size, the centre by default) meets the world. "decal size texture
  //! terrain x y": on the terrain at world (x, y), without an owner.
  void DecalCommand(const char *szArgs);
  //! "goto x y z [ax ay az]": puts the first entity of class "Player" and the view camera there (the
  //! camera at that point, the entity lowered by its eye height, as the SDK game code does).
  void GotoCommand(const char *szArgs);

  IRenderer *m_pReal;
  int m_nRealType;
  //! Console commands executed once after frame m_nExecFrame (XRENDERTRACED_EXEC, separated by ';').
  char m_szExec[2048];
  int m_nExecFrame;
  //! Console commands executed m_nExecLevelAfterLoad frames after each level load
  //! (XRENDERTRACED_EXEC_LEVEL and XRENDERTRACED_EXEC_LEVEL_FRAME), e.g. to move the player.
  char m_szExecLevel[2048];
  int m_nExecLevelAfterLoad;
  int m_nExecLevelFrame;
  //! "lightradius r [direct] [colour scale]": what EF_ADDDlight changes in the point lights (0: the
  //! engine's value).
  float m_fLightRadius = 0, m_fLightDirect = 0, m_fLightColor = 0;
  //! "sky water|- stretching|- [alpha]": what EF_AddEf writes into the sky element ('-': the engine's
  //! value).
  bool m_bSkyWater = false, m_bSkyStretching = false, m_bSkyAlpha = false;
  float m_fSkyWater = 0, m_fSkyStretching = 0, m_fSkyAlpha = 0;
  //! Every call of the m_nCaptureAfterLoad-th frame after a level load is written out
  //! (XRENDERTRACED_CAPTURE, 0 - off); m_nCaptureFrame is that frame of the current level.
  int m_nCaptureAfterLoad;
  int m_nCaptureFrame;
  //! Frames finished by Update (the engine's frame ID also counts recursive scene renders).
  int m_nFrames;
  //! XRENDERTRACED_FLARES: the object of the flare added this frame (written again after EF_EndEf3D).
  CCObject *m_pFlareObj = NULL;
  void WriteFlareObject(const char *szWhen, const char *szShader);
  //! XRENDERTRACED_SCREENFX_WATCH="8.24,8.30": after every frame the values of these screen effect
  //! parameters (process.param) are read from the ScreenProcess element the engine adds, and
  //! written when they change.
  void WatchScreenFx();
  CRendElement *m_pScreenRE = NULL;
  int m_nScreenWatch = 0;
  int m_ScreenWatch[8][2];
  int m_ScreenWatchLast[8];
  int m_nScreenWatchLines = 0;
  //! "screenshot name [frames]": the back buffer of the frame that many frames on (0: the next one)
  //! is saved as a TGA file before it is presented (D3DDraws_SaveBackBuffer).
  char m_szShot[MAX_PATH] = "";
  int m_nShotFrame = -1;
  //! "texsave name": the pixels of the next DownLoadToVideoMemory or UpdateTextureInVideoMemory
  //! call with eTF_0888 or eTF_8888 are saved as a 32-bit TGA file, 4 bytes per pixel as they are
  //! and the rows from the top.
  char m_szTexSave[MAX_PATH] = "";
  void SaveTexPixels(const unsigned char *pData, int nWidth, int nHeight);
  //! "after frames command": the command runs that many frames later (32 at most waiting).
  struct SDeferred
  {
    int nFrame;
    char szCmd[256];
  };
  SDeferred m_Deferred[32];
  int m_nDeferred = 0;
};
