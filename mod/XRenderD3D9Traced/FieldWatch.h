//////////////////////////////////////////////////////////////////////
//
//	File: FieldWatch.h
//	Description: Which members of the interface structures the engine and
//	the renderer write.
//
//	The renderer runs only inside the calls the engine makes into it (the
//	IRenderer and CLeafBuffer methods traced by the proxy). An object of the
//	renderer the engine holds (a CCObject from EF_GetObject, a leaf buffer
//	with its materials and vertex buffers, a render element, shader
//	resources) is compared with a copy taken when the outermost call
//	returned: whatever changed until the next call was written by the
//	engine; whatever changed during a call by the renderer. Objects of the
//	engine passed to a call (CDLight, SRendParams, CIndexedMesh, ...) are
//	compared before and after it. Changes are named by the members of the
//	SDK structures (StructFields.inl), logged for the first occurrences and
//	counted for the summary.
//
//	Off unless XRENDERTRACED_WATCH=1 (the copies and comparisons slow the
//	game down). At most XRENDERTRACED_WATCH_MAX (default 64) objects of each
//	type are watched at a time.
//
//////////////////////////////////////////////////////////////////////

#pragma once

enum EWatchType
{
#define XRC_STRUCT(T, nSize86, nSize64) WT_##T,
#define XRC_STRUCT_END(T)
#define XRC_FIELD(szName, nOffset86, nOffset64)
#include <StructFields.inl>
#undef XRC_STRUCT
#undef XRC_STRUCT_END
#undef XRC_FIELD
  WT_Num
};

extern bool g_bWatch;

void Watch_Init();

//! The engine calls into the renderer (outermost call) / the call returns to the engine.
void Watch_EnterRenderer(const char *szMethod);
void Watch_LeaveRenderer(const char *szMethod);

//! An object of the renderer the engine holds. A leaf buffer brings its materials, vertex buffers
//! and render elements along. Objects whose vtable changes are dropped (freed and reused).
void Watch_RendererObject(EWatchType eType, const void *p);
//! The render element type of a CRendElement.
void Watch_RenderElement(CRendElement *pRE);
void Watch_Forget(const void *p);
//! Forgets the CCObjects of the previous frame (they are taken from a pool every frame).
void Watch_NewFrame();

//! An object of the engine passed to the current call: compared before and after it.
class CWatchArg
{
public:
  CWatchArg(EWatchType eType, const void *p);
  ~CWatchArg();

private:
  EWatchType m_eType;
  const void *m_p;
  char *m_pCopy;
};

//! Writes how often each member was written, by whom and in which call.
void Watch_WriteSummary(const char *szReason);
