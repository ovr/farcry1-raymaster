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
//! Writes every call of the next frame (up to the next Trace_FrameEnd), regardless of xrt_TraceCalls.
void Trace_CaptureFrame();

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
  STraceSite *m_pNext;
};

void Trace_Call(const STraceSite &Site, const char *szFormat, ...);

//! Logs the call of the enclosing method (printf-style arguments) and defines bTraced, which tells
//! whether the call was written out (and so its structures and result may be written too).
#define TRACE_CALL(...) \
  static STraceSite s_TraceSite(__FUNCTION__, __FUNCSIG__); \
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
void Dump_RendParams(const SRendParams &rp);
