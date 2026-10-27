//////////////////////////////////////////////////////////////////////
//
//	File: FieldWatch.cpp
//	Description: Which members of the interface structures the engine and
//	the renderer write (see FieldWatch.h).
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include <CREPolyMesh.h>
#include "RenderTrace.h"
#include "FieldWatch.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

bool g_bWatch = false;

//////////////////////////////////////////////////////////////////////////
// Member tables

struct SFieldDef
{
  const char *szName;
  size_t nOffset86;
  size_t nOffset64;
};

#ifdef WIN64
#define XRC_PLATFORM(n86, n64) (n64)
#else
#define XRC_PLATFORM(n86, n64) (n86)
#endif

// The offsets come from the class layout report; the sizes check that it matches the headers.
#define XRC_STRUCT(T, nSize86, nSize64)   static_assert(sizeof(T) == XRC_PLATFORM(nSize86, nSize64), #T " does not match StructFields.inl");   static const SFieldDef s_Fields_##T[] = {
#define XRC_STRUCT_END(T) };
#define XRC_FIELD(szName, nOffset86, nOffset64) { szName, nOffset86, nOffset64 },
#include <StructFields.inl>
#undef XRC_STRUCT
#undef XRC_STRUCT_END
#undef XRC_FIELD

struct SStructDef
{
  const char *szName;
  size_t nSize;
  const SFieldDef *pFields;
  int nFields;
  bool bVTable;
};

#define XRC_STRUCT(T, nSize86, nSize64)   { #T, sizeof(T), s_Fields_##T, (int)(sizeof(s_Fields_##T) / sizeof(s_Fields_##T[0])), std::is_polymorphic<T>::value },
#define XRC_STRUCT_END(T)
#define XRC_FIELD(szName, nOffset86, nOffset64)
static const SStructDef s_Structs[WT_Num] =
{
#include <StructFields.inl>
};
#undef XRC_STRUCT
#undef XRC_STRUCT_END
#undef XRC_FIELD

//! A member, or the members of a union, with the padding after it.
struct SRange
{
  size_t nOffset;
  size_t nSize;
  std::string sName;
};

static std::vector<SRange> s_Ranges[WT_Num];

static void BuildRanges(int t)
{
  const SStructDef &S = s_Structs[t];
  std::vector<std::pair<size_t, int> > Order;
  for (int i = 0; i < S.nFields; i++)
    Order.push_back(std::make_pair(XRC_PLATFORM(S.pFields[i].nOffset86, S.pFields[i].nOffset64), i));
  std::stable_sort(Order.begin(), Order.end());
  std::vector<SRange> &R = s_Ranges[t];
  for (size_t i = 0; i < Order.size(); i++)
  {
    const SFieldDef &F = S.pFields[Order[i].second];
    if (!R.empty() && R.back().nOffset == Order[i].first)
    {
      R.back().sName += "|";
      R.back().sName += F.szName;
      continue;
    }
    SRange r;
    r.nOffset = Order[i].first;
    r.nSize = 0;
    r.sName = F.szName;
    R.push_back(r);
  }
  for (size_t i = 0; i < R.size(); i++)
    R[i].nSize = (i + 1 < R.size() ? R[i + 1].nOffset : S.nSize) - R[i].nOffset;
}

//////////////////////////////////////////////////////////////////////////
// Memory access that survives freed objects

static bool SafeCopy(void *pDst, const void *pSrc, size_t nSize)
{
  __try
  {
    memcpy(pDst, pSrc, nSize);
    return true;
  }
  __except (EXCEPTION_EXECUTE_HANDLER)
  {
    return false;
  }
}

//////////////////////////////////////////////////////////////////////////
// Recording

static std::map<std::string, int> s_Counts;     // "who\1type\1context\1member" -> count
static std::map<std::string, int> s_Logged;     // "who\1type\1context" -> objects logged
static const int nLogPerKey = 3;
static int s_nMaxPerType = 64;
static int s_nDepth = 0;
static const char *s_szMethod = "";
//! Methods the renderer called on itself during the current call of the engine (CLeafBuffer
//! methods and the IRenderer methods other modules call back with).
static std::vector<const char *> s_Nested;

static void AppendValue(std::string &s, const unsigned char *p, size_t n)
{
  char szBuf[32];
  unsigned long long v = 0;
  for (size_t i = 0; i < n && i < 8; i++)
    v |= (unsigned long long)p[i] << (8 * i);
  sprintf_s(szBuf, sizeof(szBuf), "%llx", v);
  s += szBuf;
}

//! Compares a copy of an array of structures with the current bytes and records the members that
//! differ.
static void Compare(const char *szWho, EWatchType eType, const void *pObject, const char *szContext, const unsigned char *pOld,
  const unsigned char *pNew, int nCount)
{
  const SStructDef &S = s_Structs[eType];
  if (!memcmp(pOld, pNew, S.nSize * nCount))
    return;
  // The summary counts by the call of the engine; the log lines also name the nested calls.
  std::string sCall = szContext;
  size_t nNested = sCall.find(" > ");
  if (nNested != std::string::npos)
    sCall.resize(nNested);
  std::string sKey = std::string(szWho) + "\1" + S.szName + "\1" + sCall;
  std::string sLine;
  for (int e = 0; e < nCount; e++)
  {
    const unsigned char *pO = pOld + S.nSize * e;
    const unsigned char *pN = pNew + S.nSize * e;
    const std::vector<SRange> &R = s_Ranges[eType];
    for (size_t i = 0; i < R.size(); i++)
    {
      if (!memcmp(pO + R[i].nOffset, pN + R[i].nOffset, R[i].nSize))
        continue;
      s_Counts[sKey + "\1" + R[i].sName]++;
      if (!sLine.empty())
        sLine += ", ";
      if (nCount > 1)
      {
        char szIndex[16];
        sprintf_s(szIndex, sizeof(szIndex), "[%d].", e);
        sLine += szIndex;
      }
      sLine += R[i].sName;
      if (R[i].nSize <= 8)
      {
        sLine += " ";
        AppendValue(sLine, pO + R[i].nOffset, R[i].nSize);
        sLine += "->";
        AppendValue(sLine, pN + R[i].nOffset, R[i].nSize);
      }
    }
  }
  int &nLogged = s_Logged[sKey];
  if (nLogged < nLogPerKey && CV_xrt_Trace > 0)
  {
    nLogged++;
    Trace_Write("  [watch] %s wrote %s %p %s %s: %s", szWho, S.szName, pObject, *szWho == 'e' ? "before" : "in", szContext, sLine.c_str());
  }
}

//////////////////////////////////////////////////////////////////////////
// Watched objects of the renderer

//! Part of a watched object: the object itself, or an array the object points to.
struct SRegion
{
  EWatchType eType;
  const void *p;
  int nCount;

  bool operator==(const SRegion &r) const { return eType == r.eType && p == r.p && nCount == r.nCount; }
};

struct SWatched
{
  EWatchType eType;
  const void *p;
  const void *pVTable;
  std::vector<SRegion> Regions;
  std::vector<unsigned char> Copy;
  //! Render elements a leaf buffer brought along: forgotten with it or when its materials drop them
  //! (the engine releases them itself, and a new render element of the same type can take their
  //! memory).
  std::vector<const void *> Owned;
  bool bDrop;
};

static std::vector<SWatched *> s_Watched;
static std::map<const void *, SWatched *> s_ByPtr;
static int s_nWatched[WT_Num];

static EWatchType RenderElementType(int nType)
{
  switch (nType)
  {
  case eDATA_OcLeaf:          return WT_CREOcLeaf;
  case eDATA_2DQuad:          return WT_CRE2DQuad;
  case eDATA_Sky:             return WT_CRESky;
  case eDATA_ScreenProcess:   return WT_CREScreenProcess;
  case eDATA_OcclusionQuery:  return WT_CREOcclusionQuery;
  case eDATA_Glare:           return WT_CREGlare;
  case eDATA_FlashBang:       return WT_CREFlashBang;
  case eDATA_TriMeshShadow:   return WT_CRETriMeshShadow;
  case eDATA_Poly:            return WT_CREPolyMesh;
  default:                    return WT_CRendElement;
  }
}

static void Watch_Add(EWatchType eType, const void *p);

//! The regions of an object as it is now; render elements of its materials are watched too.
static bool GetRegions(SWatched &W, std::vector<SRegion> &Regions)
{
  Regions.clear();
  SRegion r = { W.eType, W.p, 1 };
  Regions.push_back(r);
  if (W.eType != WT_CLeafBuffer)
    return true;
  unsigned char LB[sizeof(CLeafBuffer)];
  if (!SafeCopy(LB, W.p, sizeof(LB)))
    return false;
  const CLeafBuffer *pLB = (const CLeafBuffer *)LB;
  std::vector<const void *> Owned;
  unsigned char Mats[sizeof(list2<CMatInfo>)];
  if (pLB->m_pMats && SafeCopy(Mats, pLB->m_pMats, sizeof(Mats)))
  {
    list2<CMatInfo> *pMats = (list2<CMatInfo> *)Mats;
    if (pMats->Count() > 0 && pMats->Count() < 1024)
    {
      SRegion m = { WT_CMatInfo, &(*pMats)[0], pMats->Count() };
      Regions.push_back(m);
      for (int i = 0; i < pMats->Count(); i++)
      {
        CRendElement *pRE = NULL;
        if (SafeCopy(&pRE, &(*pMats)[i].pRE, sizeof(pRE)) && pRE)
        {
          Watch_RenderElement(pRE);
          if (std::find(Owned.begin(), Owned.end(), pRE) == Owned.end())
            Owned.push_back(pRE);
        }
      }
    }
  }
  // A render element the materials no longer reference has been released.
  for (size_t i = 0; i < W.Owned.size(); i++)
  {
    std::map<const void *, SWatched *>::iterator it = s_ByPtr.find(W.Owned[i]);
    if (it != s_ByPtr.end() && std::find(Owned.begin(), Owned.end(), W.Owned[i]) == Owned.end())
      it->second->bDrop = true;
  }
  W.Owned.swap(Owned);
  if (pLB->m_pVertexBuffer)
  {
    SRegion v = { WT_CVertexBuffer, pLB->m_pVertexBuffer, 1 };
    Regions.push_back(v);
  }
  if (pLB->m_pSecVertBuffer)
  {
    SRegion v = { WT_CVertexBuffer, pLB->m_pSecVertBuffer, 1 };
    Regions.push_back(v);
  }
  return true;
}

static bool TakeCopy(SWatched &W, const std::vector<SRegion> &Regions, std::vector<unsigned char> &Copy)
{
  size_t nTotal = 0;
  for (size_t i = 0; i < Regions.size(); i++)
    nTotal += s_Structs[Regions[i].eType].nSize * Regions[i].nCount;
  Copy.resize(nTotal);
  size_t nPos = 0;
  for (size_t i = 0; i < Regions.size(); i++)
  {
    size_t nSize = s_Structs[Regions[i].eType].nSize * Regions[i].nCount;
    if (!SafeCopy(&Copy[nPos], Regions[i].p, nSize))
      return false;
    nPos += nSize;
  }
  return true;
}

//! Records what changed since the last copy and takes a new one.
static void Rescan(SWatched &W, const char *szWho, const char *szContext)
{
  if (W.bDrop)
    return;
  std::vector<SRegion> Regions;
  std::vector<unsigned char> Copy;
  if (!GetRegions(W, Regions) || !TakeCopy(W, Regions, Copy))
  {
    W.bDrop = true;
    return;
  }
  // A different vtable: the object was freed and its memory reused.
  if (s_Structs[W.eType].bVTable && *(const void *const *)&Copy[0] != W.pVTable)
  {
    W.bDrop = true;
    return;
  }
  if (szWho)
  {
    size_t nOld = 0;
    for (size_t i = 0; i < W.Regions.size(); i++)
    {
      const SRegion &r = W.Regions[i];
      size_t nSize = s_Structs[r.eType].nSize * r.nCount;
      size_t nNew = 0;
      for (size_t j = 0; j < Regions.size(); j++)
      {
        if (Regions[j] == r)
        {
          Compare(szWho, r.eType, r.p, szContext, &W.Copy[nOld], &Copy[nNew], r.nCount);
          break;
        }
        nNew += s_Structs[Regions[j].eType].nSize * Regions[j].nCount;
      }
      nOld += nSize;
    }
  }
  W.Regions.swap(Regions);
  W.Copy.swap(Copy);
}

static void Scan(const char *szWho, const char *szContext)
{
  // Rescanning a leaf buffer can add render elements: index, not iterator.
  for (size_t i = 0; i < s_Watched.size(); i++)
    Rescan(*s_Watched[i], szWho, szContext);
  size_t nKept = 0;
  for (size_t i = 0; i < s_Watched.size(); i++)
  {
    SWatched *pW = s_Watched[i];
    if (pW->bDrop)
    {
      s_ByPtr.erase(pW->p);
      s_nWatched[pW->eType]--;
      delete pW;
    }
    else
      s_Watched[nKept++] = pW;
  }
  s_Watched.resize(nKept);
}

static void Watch_Add(EWatchType eType, const void *p)
{
  if (!g_bWatch || !p || s_ByPtr.count(p) || s_nWatched[eType] >= s_nMaxPerType)
    return;
  SWatched *pW = new SWatched;
  pW->eType = eType;
  pW->p = p;
  pW->pVTable = NULL;
  pW->bDrop = false;
  if (s_Structs[eType].bVTable && !SafeCopy(&pW->pVTable, p, sizeof(void *)))
  {
    delete pW;
    return;
  }
  s_Watched.push_back(pW);
  s_ByPtr[p] = pW;
  s_nWatched[eType]++;
  Rescan(*pW, NULL, NULL);
}

//////////////////////////////////////////////////////////////////////////
// Interface

void Watch_Init()
{
  char szValue[16];
  if (!GetEnvironmentVariableA("XRENDERTRACED_WATCH", szValue, sizeof(szValue)) || szValue[0] != '1')
    return;
  if (GetEnvironmentVariableA("XRENDERTRACED_WATCH_MAX", szValue, sizeof(szValue)) && atoi(szValue) > 0)
    s_nMaxPerType = atoi(szValue);
  for (int t = 0; t < WT_Num; t++)
    BuildRanges(t);
  g_bWatch = true;
  Trace_Write("Field watch on: up to %d objects of each type", s_nMaxPerType);
}

void Watch_EnterRenderer(const char *szMethod)
{
  if (!g_bWatch)
  {
    s_nDepth++;
    return;
  }
  if (s_nDepth++)
  {
    if (std::find(s_Nested.begin(), s_Nested.end(), szMethod) == s_Nested.end())
      s_Nested.push_back(szMethod);
    return;
  }
  s_szMethod = szMethod;
  s_Nested.clear();
  Scan("engine", szMethod);
}

void Watch_LeaveRenderer(const char *szMethod)
{
  if (--s_nDepth || !g_bWatch)
    return;
  // "Update > CLeafBuffer::CheckUpdate, ...": the renderer wrote in one of these.
  std::string sContext = szMethod;
  for (size_t i = 0; i < s_Nested.size(); i++)
  {
    sContext += i ? ", " : " > ";
    sContext += s_Nested[i];
  }
  Scan("renderer", sContext.c_str());
}

void Watch_RendererObject(EWatchType eType, const void *p)
{
  Watch_Add(eType, p);
}

void Watch_RenderElement(CRendElement *pRE)
{
  if (!g_bWatch || !pRE || s_ByPtr.count(pRE))
    return;
  EDataType eType;
  if (SafeCopy(&eType, &pRE->m_Type, sizeof(eType)))
    Watch_Add(RenderElementType(eType), pRE);
}

void Watch_Forget(const void *p)
{
  std::map<const void *, SWatched *>::iterator it = s_ByPtr.find(p);
  if (it == s_ByPtr.end())
    return;
  it->second->bDrop = true;
  for (size_t i = 0; i < it->second->Owned.size(); i++)
  {
    std::map<const void *, SWatched *>::iterator re = s_ByPtr.find(it->second->Owned[i]);
    if (re != s_ByPtr.end())
      re->second->bDrop = true;
  }
}

void Watch_NewFrame()
{
  for (size_t i = 0; i < s_Watched.size(); i++)
  {
    if (s_Watched[i]->eType == WT_CCObject)
      s_Watched[i]->bDrop = true;
  }
}

CWatchArg::CWatchArg(EWatchType eType, const void *p)
{
  m_eType = eType;
  m_p = p;
  m_pCopy = NULL;
  // Only arguments of the calls the engine makes (not of the renderer's own nested calls).
  if (!g_bWatch || !p || s_nDepth != 1)
    return;
  m_pCopy = new char[s_Structs[eType].nSize];
  if (!SafeCopy(m_pCopy, p, s_Structs[eType].nSize))
  {
    delete[] m_pCopy;
    m_pCopy = NULL;
  }
}

CWatchArg::~CWatchArg()
{
  if (!m_pCopy)
    return;
  std::vector<unsigned char> Now(s_Structs[m_eType].nSize);
  if (SafeCopy(&Now[0], m_p, Now.size()))
    Compare("renderer (argument)", m_eType, m_p, s_szMethod, (const unsigned char *)m_pCopy, &Now[0], 1);
  delete[] m_pCopy;
}

void Watch_WriteSummary(const char *szReason)
{
  if (!g_bWatch)
    return;
  Trace_Write("==== field writes (%s): who, structure, call -> members (times) ====", szReason);
  std::string sGroup, sLine;
  for (std::map<std::string, int>::const_iterator it = s_Counts.begin(); it != s_Counts.end(); ++it)
  {
    size_t nLast = it->first.rfind('\1');
    std::string sKey = it->first.substr(0, nLast);
    if (sKey != sGroup)
    {
      if (!sLine.empty())
        Trace_Write("%s", sLine.c_str());
      sGroup = sKey;
      sLine = "  " + sKey + ":";
      std::replace(sLine.begin(), sLine.end(), '\1', ' ');
    }
    char szCount[16];
    sprintf_s(szCount, sizeof(szCount), " (%d)", it->second);
    sLine += " " + it->first.substr(nLast + 1) + szCount;
  }
  if (!sLine.empty())
    Trace_Write("%s", sLine.c_str());
}
