//////////////////////////////////////////////////////////////////////
//
//	File: DemoHost.cpp
//	Description: Host of the demo programs: runs a demo inside the game
//	at the frames the tracing renderer reports and saves the chosen ones.
//
//////////////////////////////////////////////////////////////////////

#include "DemoHost.h"
#include "Capture.h"
#include "TestTexture.h"
#include "Video.h"
#include <IEntitySystem.h>
#include <IScriptSystem.h>
#include <direct.h>
#include <algorithm>

// Routes new/malloc of this DLL to CrySystem's heap (CryMemoryManager.h), like every engine DLL.
// Initialized before the other globals of the DLL, which may allocate.
#pragma warning(disable: 4073) // initializers put in library initialization area
#pragma init_seg(lib)
_ACCESS_POOL;

static ISystem *s_pSystem = NULL;
static FILE *s_pHostLog = NULL;

ISystem *GetISystem()
{
  return s_pSystem;
}

//////////////////////////////////////////////////////////////////////////
// Logging

static void HostLogV(ISystem *pSystem, const char *szFormat, va_list Args)
{
  char szLine[2048];
  _vsnprintf(szLine, sizeof(szLine) - 1, szFormat, Args);
  szLine[sizeof(szLine) - 1] = 0;
  if (s_pHostLog)
  {
    fprintf(s_pHostLog, "%s\n", szLine);
    fflush(s_pHostLog);
  }
  if (pSystem && pSystem->GetILog())
    pSystem->GetILog()->Log("%s", szLine);
  OutputDebugStringA(szLine);
  OutputDebugStringA("\n");
}

static void HostLog(const char *szFormat, ...)
{
  va_list Args;
  va_start(Args, szFormat);
  HostLogV(s_pSystem, szFormat, Args);
  va_end(Args);
}

void CDemoHost::Log(const char *szFormat, ...)
{
  va_list Args;
  va_start(Args, szFormat);
  HostLogV(m_pSystem, szFormat, Args);
  va_end(Args);
}

void CDemoHost::Exec(const char *szCommand)
{
  HostLog("exec: %s", szCommand);
  GetConsole()->ExecuteString(szCommand, false, true);
}

void CDemoHost::ExecList(const std::string &List)
{
  for (size_t nBegin = 0; nBegin < List.size();)
  {
    size_t nEnd = List.find_first_of(";|", nBegin);
    if (nEnd == std::string::npos)
      nEnd = List.size();
    std::string Cmd = List.substr(nBegin, nEnd - nBegin);
    Cmd.erase(0, Cmd.find_first_not_of(" \t"));
    Cmd.erase(Cmd.find_last_not_of(" \t") + 1);
    if (!Cmd.empty())
      Exec(Cmd.c_str());
    nBegin = nEnd + 1;
  }
}

//////////////////////////////////////////////////////////////////////////
// Files

std::vector<std::string> CDemoHost::FindFiles(const char *szDir, const char *szMask)
{
  std::vector<std::string> Names;
  std::string Path = std::string(szDir) + szMask;
  _finddata_t fd;
  intptr_t hFind = GetPak()->FindFirst(Path.c_str(), &fd);
  if (hFind == -1)
    return Names;
  do
  {
    if (!(fd.attrib & _A_SUBDIR))
      Names.push_back(std::string(szDir) + fd.name);
  } while (GetPak()->FindNext(hFind, &fd) == 0);
  GetPak()->FindClose(hFind);
  std::sort(Names.begin(), Names.end(), [](const std::string &a, const std::string &b) { return stricmp(a.c_str(), b.c_str()) < 0; });
  Names.erase(std::unique(Names.begin(), Names.end(), [](const std::string &a, const std::string &b) { return !stricmp(a.c_str(), b.c_str()); }),
    Names.end());
  return Names;
}

std::vector<std::string> CDemoHost::FindDirs(const char *szDir)
{
  std::vector<std::string> Names;
  std::string Path = std::string(szDir) + "*";
  _finddata_t fd;
  intptr_t hFind = GetPak()->FindFirst(Path.c_str(), &fd);
  if (hFind == -1)
    return Names;
  do
  {
    if ((fd.attrib & _A_SUBDIR) && strcmp(fd.name, ".") && strcmp(fd.name, ".."))
      Names.push_back(std::string(szDir) + fd.name + "/");
  } while (GetPak()->FindNext(hFind, &fd) == 0);
  GetPak()->FindClose(hFind);
  std::sort(Names.begin(), Names.end(), [](const std::string &a, const std::string &b) { return stricmp(a.c_str(), b.c_str()) < 0; });
  Names.erase(std::unique(Names.begin(), Names.end(), [](const std::string &a, const std::string &b) { return !stricmp(a.c_str(), b.c_str()); }),
    Names.end());
  return Names;
}

std::string CDemoHost::FindFirstFileDeep(const char *szDir, const char *szMask)
{
  std::vector<std::string> Names = FindFiles(szDir, szMask);
  if (!Names.empty())
    return Names[0];
  std::vector<std::string> Dirs = FindDirs(szDir);
  for (size_t i = 0; i < Dirs.size(); i++)
  {
    std::string Name = FindFirstFileDeep(Dirs[i].c_str(), szMask);
    if (!Name.empty())
      return Name;
  }
  return std::string();
}

std::string CDemoHost::SceneFileOrFirst(const char *szKey, const char *szDir, const char *szMask)
{
  if (m_Scene.Has(szKey))
    return m_Scene.Get(szKey);
  std::vector<std::string> Names = FindFiles(szDir, szMask);
  return Names.empty() ? std::string() : Names[0];
}

std::string CDemoHost::GetCVarString(const char *szName)
{
  ICVar *pVar = GetConsole()->GetCVar(szName);
  const char *szValue = pVar ? pVar->GetString() : NULL;
  return szValue ? szValue : "";
}

//////////////////////////////////////////////////////////////////////////
// Scene

CCamera CDemoHost::LookAt(const Vec3 &vPos, const Vec3 &vTarget, float fFov)
{
  CCamera Cam;
  Cam.Init(m_pRenderer->GetWidth(), m_pRenderer->GetHeight(), DEG2RAD(fFov));
  Cam.SetPos(vPos);
  Cam.SetAngle(ConvertVectorToCameraAngles(vTarget - vPos));
  Cam.Update();
  return Cam;
}

CCamera CDemoHost::FrameBox(const Vec3 &vMin, const Vec3 &vMax, float fYaw, float fPitch, float fFov)
{
  const Vec3 vCenter = (vMin + vMax) * 0.5f;
  const float fRadius = max(GetLength(vMax - vMin) * 0.5f, 0.1f);
  // The narrower of the two angles of view: the vertical one on a wide screen.
  const float fRatio = (float)m_pRenderer->GetHeight() / max(m_pRenderer->GetWidth(), 1);
  const float fDist = fRadius / sinf(DEG2RAD(fFov) * min(fRatio, 1.0f) * 0.5f) * 1.05f;
  const float fYawRad = DEG2RAD(fYaw), fPitchRad = DEG2RAD(fPitch);
  const Vec3 vDir(sinf(fYawRad) * cosf(fPitchRad), -cosf(fYawRad) * cosf(fPitchRad), sinf(fPitchRad));
  return LookAt(vCenter + vDir * fDist, vCenter, fFov);
}

IStatObj *CDemoHost::LoadObject(const char *szKey)
{
  std::string Name = m_Scene.Has(szKey) ? m_Scene.Get(szKey) : FindFirstFileDeep("objects/", "*.cgf");
  IStatObj *pObj = Name.empty() ? NULL : Get3DEngine()->MakeObject(Name.c_str());
  if (pObj)
  {
    const Vec3 vMin = pObj->GetBoxMin(), vMax = pObj->GetBoxMax();
    HostLog("object %s: box (%g %g %g) - (%g %g %g)", Name.c_str(), vMin.x, vMin.y, vMin.z, vMax.x, vMax.y, vMax.z);
  }
  else
    HostLog("object '%s' not loaded", Name.c_str());
  return pObj;
}

void CDemoHost::BeginScene(const CCamera &Cam, SRendParams &Params, CDLight *pLights, int nLights)
{
  m_pRenderer->SetCamera(Cam);
  m_pRenderer->EF_StartEf();
  m_pRenderer->EF_ClearLightsList();
  for (int i = 0; i < nLights; i++)
  {
    m_pRenderer->EF_ADDDlight(&pLights[i]);
    m_pRenderer->EF_UpdateDLight(&pLights[i]);
    Params.nDLightMask |= 1 << pLights[i].m_Id;
  }
}

void CDemoHost::EndScene()
{
  m_pRenderer->EF_EndEf3D(SHDF_SORT);
}

void CDemoHost::RenderObject(const CCamera &Cam, IStatObj *pObj, SRendParams &Params, CDLight *pLights, int nLights)
{
  BeginScene(Cam, Params, pLights, nLights);
  pObj->Render(Params, Vec3(0, 0, 0), 0);
  EndScene();
}

bool CDemoHost::StartLevel(const char *szLevel, const char *szMission)
{
  std::string Script = std::string("Game:SendMessage(\"StartLevel ") + szLevel;
  if (szMission && *szMission)
    Script += std::string(" ") + szMission;
  Script += "\")";
  HostLog("level: %s", Script.c_str());
  IScriptSystem *pScript = m_pSystem->GetIScriptSystem();
  return pScript && pScript->ExecuteBuffer(Script.c_str(), Script.size());
}

bool CDemoHost::HasPlayer()
{
  IEntitySystem *pEntities = m_pSystem->GetIEntitySystem();
  IEntityIt *pIt = pEntities ? pEntities->GetEntityIterator() : NULL;
  bool bFound = false;
  while (pIt && !pIt->IsEnd() && !bFound)
  {
    IEntity *pEntity = pIt->Next();
    const char *szClass = pEntity ? pEntity->GetEntityClassName() : NULL;
    bFound = szClass && !strcmp(szClass, "Player");
  }
  if (pIt)
    pIt->Release();
  return bFound;
}

int CDemoHost::GetTestTexture()
{
  if (!m_nTestTexture)
  {
    m_nTestTexture = TestTexture_Create(m_pRenderer, 256);
    HostLog("test texture: id %d", m_nTestTexture);
  }
  return m_nTestTexture;
}

//! Makes the folder szPath and the ones above it; with bFile, szPath names a file and its folder is made.
static void MakeDirs(const char *szPath, bool bFile = false)
{
  std::string Path = szPath;
  for (size_t i = 1; i <= Path.size(); i++)
  {
    if ((i == Path.size() && !bFile) || Path[i] == '\\' || Path[i] == '/')
      _mkdir(Path.substr(0, i).c_str());
  }
}

//! Logs access violations and other hardware exceptions (first chance, the first few only) with the
//! module they happened in, before the engine's handler takes over.
static LONG CALLBACK ExceptionLogger(EXCEPTION_POINTERS *pInfo)
{
  static int s_nLogged = 0;
  const EXCEPTION_RECORD *pRec = pInfo->ExceptionRecord;
  if ((pRec->ExceptionCode & 0xF0000000) != 0xC0000000 || s_nLogged >= 8)
    return EXCEPTION_CONTINUE_SEARCH;
  s_nLogged++;
  char szModule[MAX_PATH] = "?";
  HMODULE hModule = NULL;
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
    (LPCSTR)pRec->ExceptionAddress, &hModule))
    GetModuleFileNameA(hModule, szModule, sizeof(szModule));
  const char *szName = strrchr(szModule, '\\') ? strrchr(szModule, '\\') + 1 : szModule;
  if (pRec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && pRec->NumberParameters >= 2)
    HostLog("exception 0x%08X in %s: %s 0x%p", pRec->ExceptionCode, szName,
      pRec->ExceptionInformation[0] == 0 ? "read of" : pRec->ExceptionInformation[0] == 1 ? "write to" : "execute at",
      (void *)pRec->ExceptionInformation[1]);
  else
    HostLog("exception 0x%08X in %s", pRec->ExceptionCode, szName);
  return EXCEPTION_CONTINUE_SEARCH;
}

//////////////////////////////////////////////////////////////////////////
// Arguments

struct SDemoArgs
{
  std::string Label;
  std::string Out;
  std::string Frames;
  std::string Scene;
  std::string Video;
  int nStart = 30;
  int nWait = 3000;
  int nUntil = 0;
  int nFps = 0;
  int nExecAt = 0;
  std::string ExecAt;
  bool bTrace = false;
  bool bHold = false;
};

static std::vector<std::string> SplitCommandLine(const char *szCmdLine)
{
  std::vector<std::string> Args;
  const char *p = szCmdLine;
  while (*p)
  {
    while (*p == ' ' || *p == '\t')
      p++;
    if (!*p)
      break;
    std::string Arg;
    bool bQuoted = false;
    while (*p && (bQuoted || (*p != ' ' && *p != '\t')))
    {
      if (*p == '"')
        bQuoted = !bQuoted;
      else
        Arg += *p;
      p++;
    }
    Args.push_back(Arg);
  }
  return Args;
}

static bool ParseArgs(const char *szCmdLine, SDemoArgs &Args)
{
  std::vector<std::string> List = SplitCommandLine(szCmdLine);
  for (size_t i = 0; i < List.size(); i++)
  {
    const std::string &Arg = List[i];
    bool bValue = i + 1 < List.size();
    if (Arg == "--trace")
      Args.bTrace = true;
    else if (Arg == "--hold")
      Args.bHold = true;
    else if (Arg == "--label" && bValue)
      Args.Label = List[++i];
    else if (Arg == "--out" && bValue)
      Args.Out = List[++i];
    else if (Arg == "--frames" && bValue)
      Args.Frames = List[++i];
    else if (Arg == "--start" && bValue)
      Args.nStart = atoi(List[++i].c_str());
    else if (Arg == "--wait" && bValue)
      Args.nWait = atoi(List[++i].c_str());
    else if (Arg == "--scene" && bValue)
      Args.Scene = List[++i];
    else if (Arg == "--video" && bValue)
      Args.Video = List[++i];
    else if (Arg == "--until" && bValue)
      Args.nUntil = atoi(List[++i].c_str());
    else if (Arg == "--fps" && bValue)
      Args.nFps = atoi(List[++i].c_str());
    else if (Arg == "--exec-at" && i + 2 < List.size())
    {
      Args.nExecAt = atoi(List[++i].c_str());
      Args.ExecAt = List[++i];
    }
    else
    {
      HostLog("unknown argument '%s'", Arg.c_str());
      return false;
    }
  }
  return true;
}

static std::vector<int> ParseFrames(const std::string &Frames)
{
  std::vector<int> List;
  const char *p = Frames.c_str();
  while (*p)
  {
    char *pEnd = NULL;
    long n = strtol(p, &pEnd, 10);
    if (pEnd == p)
    {
      p++;
      continue;
    }
    if (n > 0)
      List.push_back((int)n);
    p = pEnd;
  }
  std::sort(List.begin(), List.end());
  return List;
}

//! The renderer under test, as the tracing renderer loads it: "old" for the original one.
static std::string RendererLabel()
{
  char szDll[MAX_PATH];
  DWORD nLen = GetEnvironmentVariableA("XRENDERTRACED_DLL", szDll, sizeof(szDll));
  if (!nLen || nLen >= sizeof(szDll) || !stricmp(szDll, "XRenderD3D9.dll"))
    return "old";
  std::string Label = szDll;
  size_t nSlash = Label.find_last_of("\\/");
  if (nSlash != std::string::npos)
    Label.erase(0, nSlash + 1);
  size_t nDot = Label.rfind('.');
  if (nDot != std::string::npos)
    Label.erase(nDot);
  return Label;
}

//////////////////////////////////////////////////////////////////////////
// Plugin

class CDemoPlugin : public IDemoPlugin
{
public:
  CDemoPlugin(CDemo &Demo, ISystem *pSystem, IRenderer *pRenderer, const SDemoArgs &Args)
    : m_Demo(Demo), m_Args(Args)
  {
    m_Host.m_pSystem = pSystem;
    m_Host.m_pRenderer = pRenderer;
    m_Frames = ParseFrames(m_Args.Frames.empty() ? Demo.GetDefaultFrames() : m_Args.Frames);
    m_nLastFrame = max(m_Frames.empty() ? 1 : m_Frames.back(), m_Args.nUntil);
  }

  virtual void BeginFrame(int nFrame)
  {
    int nDemoFrame = DemoFrame(nFrame);
    if (nDemoFrame < 1)
      return;
    if (nDemoFrame == m_Args.nExecAt)
      m_Host.ExecList(m_Args.ExecAt);
    ISystem *pSystem = m_Host.m_pSystem;
    CCamera Cam = pSystem->GetViewCamera();
    if (m_Demo.GetCamera(m_Host, nDemoFrame, Cam))
      pSystem->SetViewCamera(Cam);
  }

  virtual void EndFrame(int nFrame)
  {
    int nDemoFrame = DemoFrame(nFrame);
    if (nDemoFrame < 1)
      return;
    IRenderer *pRenderer = m_Host.m_pRenderer;
    if (m_Demo.ClearsFrame())
    {
      pRenderer->ClearColorBuffer(Vec3(0, 0, 0));
      pRenderer->ClearDepthBuffer();
    }
    m_Demo.Draw(m_Host, nDemoFrame);
    while (m_nNextSave < m_Frames.size() && m_Frames[m_nNextSave] < nDemoFrame)
      HostLog("frame %d was not drawn", m_Frames[m_nNextSave++]);
    if (m_nNextSave < m_Frames.size() && m_Frames[m_nNextSave] == nDemoFrame)
    {
      char szFile[MAX_PATH];
      _snprintf(szFile, sizeof(szFile) - 1, "%s\\%s_%04d.tga", m_Args.Out.c_str(), m_Demo.GetName(), nDemoFrame);
      szFile[sizeof(szFile) - 1] = 0;
      bool bOk = Capture_SaveFrame(pRenderer, szFile);
      HostLog("frame %d (game frame %d) -> %s: %s", nDemoFrame, nFrame, szFile, bOk ? "saved" : "FAILED");
      m_nSaved += bOk;
      m_nNextSave++;
    }
    if (!m_Args.Video.empty())
      WriteVideoFrame(nDemoFrame);
  }

  virtual void AfterFrame(int nFrame)
  {
    // The engine may draw frames inside a call of the demo (a loading screen); they only count.
    if (m_bBusy)
      return;
    m_bBusy = true;
    if (m_eState == eWaiting && nFrame >= m_Args.nStart)
    {
      m_eState = eStarting;
      m_nInitFrame = nFrame;
      // End does not return.
      if (!Start())
        End(1);
    }
    if (m_eState == eStarting)
    {
      int nWaited = nFrame - m_nInitFrame;
      if (m_Demo.IsReady(m_Host, nWaited))
      {
        m_eState = eRunning;
        m_nFirstFrame = nFrame + 1;
        HostLog("%s: frame 1 is game frame %d", m_Demo.GetName(), m_nFirstFrame);
      }
      else if (nWaited >= m_Args.nWait)
      {
        HostLog("%s: not ready after %d frames", m_Demo.GetName(), nWaited);
        End(1);
      }
    }
    else if (m_eState == eRunning)
    {
      m_Demo.AfterFrame(m_Host, DemoFrame(nFrame));
      if (!m_Args.bHold && DemoFrame(nFrame) >= m_nLastFrame)
        End(m_nSaved == (int)m_Frames.size() ? 0 : 3);
      if (!m_Args.bHold && m_Demo.IsFinished(m_Host, DemoFrame(nFrame)))
      {
        HostLog("%s: the scene ended with frame %d", m_Demo.GetName(), DemoFrame(nFrame));
        End(m_nSaved == (int)m_Frames.size() ? 0 : 3);
      }
    }
    m_bBusy = false;
  }

  virtual void Release()
  {
    // The game was closed (--hold).
    if (m_eState == eStarting || m_eState == eRunning)
    {
      HostLog("%s: %d of %d frames saved, the game was closed", m_Demo.GetName(), m_nSaved, (int)m_Frames.size());
      Shutdown();
    }
    if (s_pHostLog)
      fclose(s_pHostLog);
    s_pHostLog = NULL;
    delete this;
  }

private:
  //! Frame of the demo (from 1) for frame nFrame of the game, 0 before the start.
  int DemoFrame(int nFrame) const { return m_eState == eRunning ? nFrame - m_nFirstFrame + 1 : 0; }

  bool Start()
  {
    HostLog("%s: started after game frame %d, frames to %s, scene %s", m_Demo.GetName(), m_Args.nStart, m_Args.Out.c_str(),
      m_Args.Scene.c_str());
    if (!m_Args.bTrace)
      m_Host.Exec("xrt_Trace 0");
    char szStep[64];
    sprintf(szStep, "fixed_time_step %g", m_Host.m_fFrameTime);
    m_Host.Exec(szStep);
    if (!m_Host.m_Scene.Load(m_Args.Scene.c_str()))
      HostLog("no scene file %s: the demo uses its defaults", m_Args.Scene.c_str());
    MakeDirs(m_Args.Out.c_str());
    if (!m_Demo.Init(m_Host))
    {
      HostLog("%s: Init failed", m_Demo.GetName());
      return false;
    }
    return true;
  }

  //! Ends the demo and the game, which is killed like at the end of a trace run, so that it does
  //! not write system.cfg back.
  void End(int nExitCode)
  {
    HostLog("%s: %d of %d frames saved, exit code %d", m_Demo.GetName(), m_nSaved, (int)m_Frames.size(), nExitCode);
    Shutdown();
    m_eState = eDone;
    if (s_pHostLog)
      fclose(s_pHostLog);
    s_pHostLog = NULL;
    TerminateProcess(GetCurrentProcess(), nExitCode);
  }

  //! Adds the frame to the video; ffmpeg is started at the first one, when the size is known.
  void WriteVideoFrame(int nDemoFrame)
  {
    if (m_bVideoFailed)
      return;
    int nWidth = 0, nHeight = 0;
    if (!Capture_ReadFrame(m_Host.m_pRenderer, nWidth, nHeight, m_VideoPixels))
    {
      HostLog("video: frame %d can't be read, the video ends", nDemoFrame);
      m_bVideoFailed = true;
      return;
    }
    if (!m_Video.IsOpen())
    {
      const int nFps = m_Args.nFps > 0 ? m_Args.nFps : (int)(1.0f / m_Host.m_fFrameTime + 0.5f);
      MakeDirs(m_Args.Video.c_str(), true);
      m_bVideoFailed = !m_Video.Open(m_Args.Video.c_str(), nWidth, nHeight, nFps);
      HostLog("video: %s, %dx%d, %d frames per second, from frame %d: %s", m_Args.Video.c_str(), nWidth, nHeight, nFps,
        nDemoFrame, m_bVideoFailed ? "ffmpeg not started (is it on PATH?)" : "ffmpeg started");
      if (m_bVideoFailed)
        return;
    }
    if (nWidth != m_Video.GetWidth() || nHeight != m_Video.GetHeight())
    {
      HostLog("video: frame %d is %dx%d, the video %dx%d: the video ends", nDemoFrame, nWidth, nHeight, m_Video.GetWidth(),
        m_Video.GetHeight());
      m_bVideoFailed = true;
      return;
    }
    if (!m_Video.Write(m_VideoPixels.data()))
    {
      HostLog("video: ffmpeg took no more frames after %d (see %s.log)", m_Video.GetFrames(), m_Args.Video.c_str());
      m_bVideoFailed = true;
    }
  }

  void Shutdown()
  {
    if (m_Video.IsOpen())
    {
      const int nFrames = m_Video.GetFrames();
      const int nExitCode = m_Video.Close();
      HostLog("video: %d frames, ffmpeg exit code %d%s", nFrames, nExitCode, nExitCode ? " (see the .log next to the video)" : "");
    }
    m_Demo.Shutdown(m_Host);
    if (m_Host.m_nTestTexture > 0)
      m_Host.m_pRenderer->RemoveTexture(m_Host.m_nTestTexture);
    m_Host.m_nTestTexture = 0;
  }

  enum EState
  {
    eWaiting,
    eStarting,
    eRunning,
    eDone,
  };

  CDemo &m_Demo;
  CDemoHost m_Host;
  SDemoArgs m_Args;
  EState m_eState = eWaiting;
  //! Inside AfterFrame.
  bool m_bBusy = false;
  //! Game frame after which Init ran.
  int m_nInitFrame = 0;
  //! Game frame of frame 1 of the demo.
  int m_nFirstFrame = 0;
  std::vector<int> m_Frames;
  int m_nLastFrame = 1;
  size_t m_nNextSave = 0;
  int m_nSaved = 0;
  CVideo m_Video;
  std::vector<unsigned char> m_VideoPixels;
  bool m_bVideoFailed = false;
};

IDemoPlugin *DemoHost_Create(CDemo &Demo, int nVersion, ISystem *pSystem, IRenderer *pRenderer, const char *szArgs)
{
  s_pSystem = pSystem;
  std::string HostLogFile = std::string(Demo.GetName()) + ".host.log";
  s_pHostLog = fopen(HostLogFile.c_str(), "wt");
  AddVectoredExceptionHandler(1, ExceptionLogger);
  if (nVersion != DEMO_PLUGIN_VERSION)
  {
    HostLog("%s: built for demo interface %d, the renderer has %d", Demo.GetName(), DEMO_PLUGIN_VERSION, nVersion);
    return NULL;
  }

  SDemoArgs Args;
  if (!ParseArgs(szArgs ? szArgs : "", Args))
  {
    TerminateProcess(GetCurrentProcess(), 2);
    return NULL;
  }
  if (Args.Label.empty())
    Args.Label = RendererLabel();
  if (Args.Out.empty())
    Args.Out = "DemoShots\\" + Args.Label;
  if (Args.Scene.empty())
    Args.Scene = std::string("Demos\\") + Demo.GetName() + ".ini";
  HostLog("%s: arguments '%s', label %s", Demo.GetName(), szArgs ? szArgs : "", Args.Label.c_str());
  return new CDemoPlugin(Demo, pSystem, pRenderer, Args);
}
