//////////////////////////////////////////////////////////////////////
//
//	File: RenderTrace.h
//	Description: Trace log of everything the engine passes to the renderer.
//
//	The log, XRenderD3D9Traced.log in the game's working directory
//	(farcry1_testing1, the only place tracing is done), shows how the engine
//	uses the renderer interface: every IRenderer method writes its
//	arguments and results, and with xrt_Trace 2 also dumps the structures
//	behind them. Every method writes out only its first xrt_TraceCalls calls;
//	the rest are counted and shown in the per-frame call statistics.
//
//////////////////////////////////////////////////////////////////////

#pragma once

//! 0 - off, 1 - calls with arguments, 2 - calls and the structures passed with them.
extern int CV_xrt_Trace;
//! Number of calls of each method that are written to the log.
extern int CV_xrt_TraceCalls;
//! Call statistics are written for the first N frames and then every 500th frame.
extern int CV_xrt_TraceFrames;

void Trace_Open(const char *szFileName);
void Trace_Close();
void Trace_RegisterCVars();
void Trace_Write(const char *szFormat, ...);
void Trace_FrameEnd(int nFrameID);
//! Starts counting the calls of every method from zero again, so the next xrt_TraceCalls calls of
//! each are written out, followed by the statistics of xrt_TraceFrames frames (e.g. on level load).
void Trace_Restart(const char *szReason);
//! Writes every call of the next frame (up to the next Trace_FrameEnd), regardless of xrt_TraceCalls;
//! called within a frame, every call of the rest of it.
void Trace_CaptureFrame(const char *szReason = "capture of the next frame");
//! Writes every call from now on, through all later frames, until the renderer is released (the
//! shutdown).
void Trace_CaptureRest(const char *szReason);
//! Whether the current frame is the captured one.
bool Trace_IsCapturing();
//! The thread that loaded the renderer (PackageRenderConstructor).
DWORD Trace_MainThread();

struct STraceSite
{
  STraceSite(const char *szName, const char *szSignature);
  //! Counts the call; returns true if it has to be written out.
  bool Hit();

  const char *m_szName;
  //! Name in the log: "Update" for IRenderer methods, "CLeafBuffer::AddRE" for the hooked classes.
  char m_szDisplay[64];
  char m_szSignature[192];
  int m_nCalls;
  int m_nFrameCalls;
  //! Calls from other threads than the one that loaded the renderer (logged once per method).
  int m_nOtherThreadCalls;
  //! Every call is written out (XRENDERTRACED_TRACE_METHODS).
  bool m_bAlways;
  //! The written out calls are followed by the Direct3D state after them (XRENDERTRACED_D3D_STATE).
  bool m_bD3DState;
  STraceSite *m_pNext;
};

void Trace_Call(const STraceSite &Site, const char *szFormat, ...);

//! Whether szName is one of the names in the environment variable szVar, separated by ',' (e.g.
//! XRENDERTRACED_TRACE_METHODS="DownLoadToVideoMemory,CLeafBuffer::CreateBufferFromSource").
bool Trace_IsListed(const char *szVar, const char *szName);

//! Marks the time spent in a traced method; the outermost one is a call of the engine into the
//! renderer (see FieldWatch.h).
struct STraceScope
{
  explicit STraceScope(STraceSite &Site);
  ~STraceScope();

  STraceSite &m_Site;
  bool m_bMainThread;
};

//! Logs the call of the enclosing method (printf-style arguments) and defines bTraced, which tells
//! whether the call was written out (and so its structures and result may be written too).
#define TRACE_CALL(...) \
  static STraceSite s_TraceSite(__FUNCTION__, __FUNCSIG__); \
  STraceScope TraceScope(s_TraceSite); \
  const bool bTraced = s_TraceSite.Hit() && (Trace_Call(s_TraceSite, __VA_ARGS__), true)

#define TRACE_DUMP (bTraced && CV_xrt_Trace >= 2)

//! Writes a line belonging to the traced call (result, details); only if the call was written out.
#define TRACE_RESULT(...) \
  do { if (bTraced) Trace_Write("    -> " __VA_ARGS__); } while (0)

//! printf helpers for vectors: Trace_Write("pos " TRACE_V3, TRACE_V3_ARGS(vPos)).
#define TRACE_V3 "(%g %g %g)"
#define TRACE_V3_ARGS(v) (v).x, (v).y, (v).z

//! "Module+0xRVA" of an address; the returned buffer is reused after a few calls.
const char *Trace_Addr(const void *pAddr);
//! "Module+0xRVA" of the vtable of a C++ object (identifies its class in the module).
const char *Trace_VTable(const void *pObject);
//! Whether the address is inside executable code of a loaded module.
bool Trace_IsCode(const void *pAddr);
//! Whether nSize bytes at p are committed, readable memory.
bool Trace_IsReadable(const void *p, size_t nSize);
//! The 16 bytes in front of a heap block as four hex dwords (the heap's block header).
const char *Trace_HeapPrefix(const void *pBlock);
//! Size of a CrySystem heap block (the dword in front of it), or -1.
int Trace_HeapBlockSize(const void *pBlock);

//////////////////////////////////////////////////////////////////////////
// Structure dumps (guard them with TRACE_DUMP).

struct SInputShaderResources;
struct SRenderShaderResources;
class CIndexedMesh;
struct CLeafBuffer;
struct VertexBufferSource;

const char *Trace_RETypeName(int nType);
const char *Trace_VertexFormatName(int nFormat);

void Dump_Hex(const char *szName, const void *pData, int nSize);
//! The bytes on one line, grouped by dwords (for comparing many objects of one type).
void Dump_HexLine(const char *szName, const void *pData, int nSize);
//! A structure of unknown layout, one dword per line: as integer, as float where plausible, and
//! where 8 bytes point to readable memory, the heap block size and the module of its first member.
void Dump_Unknown(const char *szName, const void *pData, int nSize);
//! A structure the SDK only declares, as large as its heap block; the first nMaxDumps per name.
//! Data blocks it points to are dumped too, nDepth levels deep.
void Dump_Opaque(const char *szName, const void *p, int nMaxDumps = 3, int nDepth = 2);
//! A list2 of such structures: the element size from the heap block of the elements.
void Dump_OpaqueList(const char *szName, const void *pList, int nMaxDumps = 3, int nDepth = 3);
//! The last CCObject::m_pShadowCasters dumped (to see what drawing the frame changed in it).
extern const void *g_pTraceLastShadowCasters;
void Dump_Matrix(const char *szName, const float *pM);
void Dump_Camera(const CCamera &Cam);
void Dump_Object(const CCObject *pObj);
void Dump_RE(CRendElement *pRE);
void Dump_Shader(IShader *pSH);
void Dump_ShaderItem(const SShaderItem &Item);
void Dump_TexPic(ITexPic *pTex);
void Dump_InputResources(const SInputShaderResources *pRes);
void Dump_RenderResources(const SRenderShaderResources *pRes);
void Dump_Light(const CDLight *pLight);
void Dump_IndexedMesh(const CIndexedMesh *pMesh);
void Dump_LeafBuffer(CLeafBuffer *pLB);
void Dump_VertexBuffer(const char *szName, const CVertexBuffer *pVB, int nMax = 4);
void Dump_VertexBufferSource(const VertexBufferSource *pSrc);
void Dump_Vertices(const char *szName, const void *pData, int nFormat, int nVerts, int nMax = 4);
void Dump_Indices(const char *szName, const ushort *pInds, int nInds, int nMax = 12);
void Dump_ColorVerts(const SColorVert *pVerts, int nVerts);
void Dump_ColorVerts2D(const SColorVert2D *pVerts, int nVerts);
void Dump_MatInfo(const CMatInfo *pMI, int nIndex);
//! An engine material (IMatInfo, e.g. the one passed to CLeafBuffer::AddRenderElements).
void Dump_IMatInfo(IMatInfo *pMtl);
void Dump_RendParams(const SRendParams &rp);
