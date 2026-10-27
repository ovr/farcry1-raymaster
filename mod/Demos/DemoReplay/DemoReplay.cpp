//////////////////////////////////////////////////////////////////////
//
//	File: DemoReplay.cpp
//	Description: The game's own recordings (time demos), recorded in a
//	level and played back, so that the frames of two playbacks can be
//	compared.
//
//	The SDK's game records the player's position and angles and its
//	movement commands at every frame (Game:StartRecord and StopRecord, the
//	console commands record and stoprecording) into
//	Levels\<level>\<name>.tmd and plays them back (Game:StartDemoPlay, the
//	command demo); its recorder takes and plays frames in the game's
//	update for its dev mode only (CXGame::DevModeUpdate). The demo loads
//	the level like DemoLevel (it waits for the player, then settle frames,
//	stops the opening cut scene and runs the exec commands), then:
//	  record  starts the game's recording before frame 1, holds the keys
//	          and moves the mouse (SendInput, only while the game window
//	          is in front) for length frames, stops the recording and logs
//	          what the file holds; the run ends there. The demo records the
//	          player's position and angles after every frame itself, into
//	          Demos\<name>.tmd in the same format. It marks the game's files
//	          it made (in the header's reserved bytes) and never records
//	          over another one.
//	  play    starts the game's playback before frame 1. When the game's
//	          file has no frames or the game is not in its dev mode, the
//	          demo plays the recording itself: before frame n it puts the
//	          player at the position and angles of record n - 1, as the SDK's
//	          playback does (IEntity::SetPos, SetAngles, Game:SetViewAngles),
//	          from the game's file, or else from its own; the run ends with
//	          the recording.
//	The log has the player's position every 50 frames, while playing with
//	its distance from the recorded one. The frames are the game's (the
//	demo draws nothing): two playbacks with the same scene file are
//	compared with DemoCompare.
//
//	Scene file keys (Demos\DemoReplay.ini):
//	  level = <name>          the level (default: the first folder in levels/,
//	                          for play the first one with a <level>.tmd)
//	  mission = <name>        its mission (default: the level's default)
//	  settle = <frames>       frames after the player exists (default 300)
//	  exec = <commands>       console commands once the level is there,
//	                          separated by ';' or '|'; more as exec2, exec3,
//	                          ... (e.g. cl_display_hud 0)
//	  mode = play|record      (default play)
//	  name = <name>           the recording, Levels\<level>\<name>.tmd and
//	                          Demos\<name>.tmd (default: the level's name to
//	                          play, DemoReplay to record)
//	  length = <frames>       frames recorded (default 200); a run that
//	                          saves fewer frames needs --until <length>
//	  keys = <letters>        keys held while recording (default w)
//	  turn = <units>          mouse move to the right per frame while
//	                          recording (default 3)
//	  playback = game|demo    who plays it back (default game, see above)
//
//////////////////////////////////////////////////////////////////////

#include "DemoHost.h"
#include <IGame.h>
#include <IEntitySystem.h>
#include <IScriptSystem.h>

#pragma pack(push, 1)
//! A recording as the SDK's game writes it (CryGame/TimeDemoRecorder.cpp): this header, then the frames.
struct STimeDemoHeader
{
  char Signature[3];
  int nFileType;
  int nVersion;
  int nFrames;
  float fTotalTime;
  char Reserved[128];
};

struct STimeDemoFrame
{
  Vec3 vPos;
  Vec3 vAngles;
  float fFrameTime;
  unsigned int nActionFlags[2];
  float fLeaning;
  int nPolygons;
  char Reserved[28];
};
#pragma pack(pop)

static_assert(sizeof(STimeDemoHeader) == 147 && sizeof(STimeDemoFrame) == 72, "the SDK's file layout");

//! Written into the reserved bytes of the game's recordings the demo made: it records over those only.
static const char s_szTag[] = "DemoReplay";

static bool FileExists(const char *szFile)
{
  FILE *pFile = fopen(szFile, "rb");
  if (pFile)
    fclose(pFile);
  return pFile != NULL;
}

static void TagRecording(const char *szFile)
{
  FILE *pFile = fopen(szFile, "r+b");
  if (!pFile)
    return;
  fseek(pFile, offsetof(STimeDemoHeader, Reserved), SEEK_SET);
  fwrite(s_szTag, sizeof(s_szTag), 1, pFile);
  fclose(pFile);
}

//! The frames of a recording; false if the file can't be read or isn't one.
static bool ReadRecording(const char *szFile, STimeDemoHeader &Header, std::vector<STimeDemoFrame> &Frames)
{
  Frames.clear();
  FILE *pFile = fopen(szFile, "rb");
  if (!pFile)
    return false;
  const bool bOk = fread(&Header, sizeof(Header), 1, pFile) == 1 && !memcmp(Header.Signature, "CRY", 3);
  for (int i = 0; bOk && i < Header.nFrames; i++)
  {
    STimeDemoFrame Frame;
    if (fread(&Frame, sizeof(Frame), 1, pFile) != 1)
      break;
    Frames.push_back(Frame);
  }
  fclose(pFile);
  return bOk;
}

static bool WriteRecording(const char *szFile, const std::vector<STimeDemoFrame> &Frames, float fFrameTime)
{
  FILE *pFile = fopen(szFile, "wb");
  if (!pFile)
    return false;
  STimeDemoHeader Header;
  memset(&Header, 0, sizeof(Header));
  memcpy(Header.Signature, "CRY", 3);
  Header.nFileType = 150;
  Header.nVersion = 1;
  Header.nFrames = (int)Frames.size();
  Header.fTotalTime = fFrameTime * Frames.size();
  bool bOk = fwrite(&Header, sizeof(Header), 1, pFile) == 1;
  if (!Frames.empty())
    bOk = bOk && fwrite(Frames.data(), sizeof(STimeDemoFrame), Frames.size(), pFile) == Frames.size();
  fclose(pFile);
  return bOk;
}

//! The player: the first entity of class Player.
static IEntity *FindPlayer(CDemoHost &Host)
{
  IEntitySystem *pEntities = Host.GetSystem()->GetIEntitySystem();
  IEntityIt *pIt = pEntities ? pEntities->GetEntityIterator() : NULL;
  IEntity *pPlayer = NULL;
  while (pIt && !pIt->IsEnd() && !pPlayer)
  {
    IEntity *pEntity = pIt->Next();
    const char *szClass = pEntity ? pEntity->GetEntityClassName() : NULL;
    if (szClass && !strcmp(szClass, "Player"))
      pPlayer = pEntity;
  }
  if (pIt)
    pIt->Release();
  return pPlayer;
}

static bool RunScript(CDemoHost &Host, const char *szScript)
{
  Host.Log("script: %s", szScript);
  IScriptSystem *pScript = Host.GetSystem()->GetIScriptSystem();
  return pScript && pScript->ExecuteBuffer(szScript, strlen(szScript));
}

class CDemoReplay : public CDemo
{
public:
  virtual const char *GetName() const { return "DemoReplay"; }
  virtual const char *GetDefaultFrames() const { return "1,50,100,150,200"; }
  virtual bool ClearsFrame() const { return false; }

  virtual bool Init(CDemoHost &Host)
  {
    const CSceneFile &Scene = Host.GetScene();
    m_bRecord = stricmp(Scene.Get("mode", "play").c_str(), "record") == 0;
    m_Level = Scene.Get("level");
    // By default a level of its own recording to play, or the first level to record in.
    std::vector<std::string> Levels = Host.FindDirs("levels/");
    for (size_t i = 0; i < Levels.size() && m_Level.empty(); i++)
    {
      const std::string Level = Levels[i].substr(7, Levels[i].size() - 8);
      if (m_bRecord || FileExists(("Levels/" + Level + "/" + Level + ".tmd").c_str()))
        m_Level = Level;
    }
    if (m_Level.empty())
    {
      Host.Log("DemoReplay: no level");
      return false;
    }
    m_Name = Scene.Get("name", m_bRecord ? "DemoReplay" : m_Level.c_str());
    m_nLength = max(Scene.GetInt("length", 200), 1);
    m_Keys = Scene.Get("keys", "w");
    m_nTurn = Scene.GetInt("turn", 3);
    // The game writes its recording into the level's folder (Levels/<level>), the demo its own next to the scene files.
    m_GameFile = "Levels/" + m_Level + "/" + m_Name + ".tmd";
    m_OwnFile = "Demos/" + m_Name + ".tmd";
    Host.Log("DemoReplay: %s '%s' in %s, the game's file %s, the demo's %s", m_bRecord ? "record" : "play", m_Name.c_str(),
      m_Level.c_str(), m_GameFile.c_str(), m_OwnFile.c_str());

    STimeDemoHeader Header;
    if (m_bRecord && FileExists(m_GameFile.c_str())
      && !(ReadRecording(m_GameFile.c_str(), Header, m_GameFrames) && !memcmp(Header.Reserved, s_szTag, sizeof(s_szTag))))
    {
      Host.Log("DemoReplay: %s was not made by the demo (%d frames): it is not recorded over, choose another name", m_GameFile.c_str(),
        (int)m_GameFrames.size());
      return false;
    }
    if (!m_bRecord)
    {
      ReadRecording(m_GameFile.c_str(), Header, m_GameFrames);
      ReadRecording(m_OwnFile.c_str(), Header, m_OwnFrames);
      Host.Log("DemoReplay: %d frames in %s, %d in %s", (int)m_GameFrames.size(), m_GameFile.c_str(), (int)m_OwnFrames.size(),
        m_OwnFile.c_str());
      if (m_GameFrames.empty() && m_OwnFrames.empty())
      {
        Host.Log("DemoReplay: nothing to play: record first (mode = record)");
        return false;
      }
    }
    return Host.StartLevel(m_Level.c_str(), Scene.Get("mission").c_str());
  }

  virtual bool IsReady(CDemoHost &Host, int nFramesWaited)
  {
    if (m_nPlayerFrame < 0)
    {
      if (!Host.HasPlayer())
        return false;
      m_nPlayerFrame = nFramesWaited;
      Host.Log("DemoReplay: the player is there after %d frames", nFramesWaited);
    }
    if (nFramesWaited - m_nPlayerFrame < Host.GetScene().GetInt("settle", 300))
      return false;

    Host.Exec("SkipCutScene");
    std::vector<std::string> Exec = Host.GetScene().GetKeys("exec");
    for (size_t i = 0; i < Exec.size(); i++)
      Host.ExecList(Host.GetScene().Get(Exec[i].c_str()));
    IGame *pGame = Host.GetSystem()->GetIGame();
    m_bDevMode = pGame && pGame->GetModuleState(EGameDevMode);
    Host.Log("DemoReplay: the game's dev mode is %s", m_bDevMode ? "on" : "off");
    return m_bRecord ? StartRecording(Host) : StartPlayback(Host);
  }

  virtual void Draw(CDemoHost &Host, int nFrame) {}

  virtual void AfterFrame(CDemoHost &Host, int nFrame)
  {
    if (nFrame % 50 == 0)
      LogPlayer(Host, nFrame);
    if (m_bRecording)
    {
      // Record n - 1 is the player as frame n drew it; the playback puts it there before frame n.
      AddOwnFrame(Host);
      if (nFrame >= m_nLength)
        StopRecording(Host);
      else if (m_nTurn)
        SendMouseMove(Host, m_nTurn);
    }
    else if (m_bOwnPlayback && nFrame < (int)m_Playback.size())
      PlacePlayer(Host, m_Playback[nFrame]);
  }

  virtual bool IsFinished(CDemoHost &Host, int nFrame)
  {
    return m_bRecord ? m_bRecorded : m_bOwnPlayback && nFrame >= (int)m_Playback.size();
  }

  virtual void Shutdown(CDemoHost &Host)
  {
    if (m_bRecording)
      StopRecording(Host);
  }

private:
  bool StartRecording(CDemoHost &Host)
  {
    std::string Script = "Game:StartRecord(\"" + m_Name + "\")";
    if (!RunScript(Host, Script.c_str()))
      Host.Log("DemoReplay: Game:StartRecord failed");
    m_bRecording = true;
    m_Own.clear();
    const bool bFront = IsInFront(Host);
    Host.Log("DemoReplay: recording %d frames, keys '%s', mouse %d per frame; the game window is %sin front%s", m_nLength,
      m_Keys.c_str(), m_nTurn, bFront ? "" : "not ", bFront ? "" : ": no input is sent, the player stands still");
    for (size_t i = 0; i < m_Keys.size(); i++)
      SendKey(Host, m_Keys[i], true);
    return true;
  }

  void StopRecording(CDemoHost &Host)
  {
    m_bRecording = false;
    m_bRecorded = true;
    for (size_t i = 0; i < m_Keys.size(); i++)
      SendKey(Host, m_Keys[i], false);
    if (!RunScript(Host, "Game:StopRecord()"))
      Host.Log("DemoReplay: Game:StopRecord failed");
    const bool bOwn = WriteRecording(m_OwnFile.c_str(), m_Own, Host.GetFrameTime());
    Host.Log("DemoReplay: the demo's recording: %d frames -> %s: %s", (int)m_Own.size(), m_OwnFile.c_str(), bOwn ? "written" : "NOT written");

    STimeDemoHeader Header;
    std::vector<STimeDemoFrame> Frames;
    if (!ReadRecording(m_GameFile.c_str(), Header, Frames))
    {
      Host.Log("DemoReplay: the game's recording %s is not there or not a recording", m_GameFile.c_str());
      return;
    }
    TagRecording(m_GameFile.c_str());
    Host.Log("DemoReplay: the game's recording %s: type %d, version %d, %d frames (%d read), %g s", m_GameFile.c_str(), Header.nFileType,
      Header.nVersion, Header.nFrames, (int)Frames.size(), Header.fTotalTime);
    if (Frames.empty())
    {
      Host.Log("DemoReplay: the game recorded no frames: its recorder takes frames only in the game's dev mode (%s)",
        m_bDevMode ? "on, yet no frames" : "off");
      return;
    }
    // The game's frames and the demo's are taken at the same points of the frame: they should be the same.
    float fMaxApart = 0;
    for (size_t i = 0; i < Frames.size() && i < m_Own.size(); i++)
      fMaxApart = max(fMaxApart, GetLength(Frames[i].vPos - m_Own[i].vPos));
    const Vec3 &First = Frames.front().vPos, &Last = Frames.back().vPos;
    Host.Log("DemoReplay: the game's frames from (%g %g %g) to (%g %g %g), at most %g m from the demo's", First.x, First.y, First.z,
      Last.x, Last.y, Last.z, fMaxApart);
  }

  bool StartPlayback(CDemoHost &Host)
  {
    const bool bGame = stricmp(Host.GetScene().Get("playback", "game").c_str(), "demo") != 0;
    if (bGame && !m_GameFrames.empty() && m_bDevMode)
    {
      m_Playback = m_GameFrames;
      std::string Script = "Game:StartDemoPlay(\"" + m_Name + "\")";
      if (!RunScript(Host, Script.c_str()))
        Host.Log("DemoReplay: Game:StartDemoPlay failed");
      Host.Log("DemoReplay: the game plays %d frames", (int)m_Playback.size());
      return true;
    }
    if (bGame)
      Host.Log("DemoReplay: the game can't play it back (%s): the demo plays it",
        m_GameFrames.empty() ? "no frames in its file" : "its recorder plays frames only in its dev mode");
    m_Playback = m_GameFrames.empty() ? m_OwnFrames : m_GameFrames;
    m_bOwnPlayback = true;
    Host.Log("DemoReplay: the demo plays %d frames of %s", (int)m_Playback.size(),
      m_GameFrames.empty() ? m_OwnFile.c_str() : m_GameFile.c_str());
    PlacePlayer(Host, m_Playback[0]);
    return true;
  }

  void AddOwnFrame(CDemoHost &Host)
  {
    STimeDemoFrame Frame;
    memset(&Frame, 0, sizeof(Frame));
    IEntity *pPlayer = FindPlayer(Host);
    if (pPlayer)
    {
      Frame.vPos = pPlayer->GetPos();
      Frame.vAngles = pPlayer->GetAngles();
    }
    Frame.fFrameTime = Host.GetFrameTime();
    int nShadowPolygons = 0;
    Host.GetRenderer()->GetPolyCount(Frame.nPolygons, nShadowPolygons);
    m_Own.push_back(Frame);
  }

  void PlacePlayer(CDemoHost &Host, const STimeDemoFrame &Frame)
  {
    IEntity *pPlayer = FindPlayer(Host);
    if (!pPlayer)
      return;
    pPlayer->SetPos(Frame.vPos);
    pPlayer->SetAngles(Frame.vAngles);
    char szScript[192];
    _snprintf(szScript, sizeof(szScript) - 1, "Game:SetViewAngles({x=%.6f,y=%.6f,z=%.6f})", Frame.vAngles.x, Frame.vAngles.y,
      Frame.vAngles.z);
    szScript[sizeof(szScript) - 1] = 0;
    IScriptSystem *pScript = Host.GetSystem()->GetIScriptSystem();
    if (pScript)
      pScript->ExecuteBuffer(szScript, strlen(szScript));
  }

  void LogPlayer(CDemoHost &Host, int nFrame)
  {
    IEntity *pPlayer = FindPlayer(Host);
    if (!pPlayer)
      return;
    const Vec3 Pos = pPlayer->GetPos(), Angles = pPlayer->GetAngles();
    char szApart[96] = "";
    // Record n - 1 was put before frame n (record 0 before frame 1), and the game has moved the player for one frame since.
    if (!m_bRecord && nFrame >= 1 && nFrame <= (int)m_Playback.size())
      _snprintf(szApart, sizeof(szApart) - 1, ", %.3f m from record %d", GetLength(Pos - m_Playback[nFrame - 1].vPos), nFrame - 1);
    Host.Log("DemoReplay: after frame %d the player is at (%g %g %g), angles (%g %g %g)%s", nFrame, Pos.x, Pos.y, Pos.z, Angles.x,
      Angles.y, Angles.z, szApart);
  }

  //! Input goes to the window in front, as with the tracing renderer's key and mouse commands.
  static bool IsInFront(CDemoHost &Host)
  {
    const HWND hWnd = (HWND)Host.GetRenderer()->GetHWND();
    return hWnd && GetForegroundWindow() == hWnd;
  }

  //! A key as a scan code, which the game's input reads. A key up is sent in any case, so that no key stays down.
  static void SendKey(CDemoHost &Host, char cKey, bool bDown)
  {
    if (!isalnum((unsigned char)cKey) || (bDown && !IsInFront(Host)))
      return;
    INPUT Input = {};
    Input.type = INPUT_KEYBOARD;
    Input.ki.wScan = (WORD)MapVirtualKeyA((UINT)toupper((unsigned char)cKey), MAPVK_VK_TO_VSC);
    Input.ki.dwFlags = KEYEVENTF_SCANCODE | (bDown ? 0 : KEYEVENTF_KEYUP);
    SendInput(1, &Input, sizeof(Input));
  }

  static void SendMouseMove(CDemoHost &Host, int nDx)
  {
    if (!IsInFront(Host))
      return;
    INPUT Input = {};
    Input.type = INPUT_MOUSE;
    Input.mi.dx = nDx;
    Input.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &Input, sizeof(Input));
  }

  std::string m_Level, m_Name, m_Keys, m_GameFile, m_OwnFile;
  bool m_bRecord = true;
  int m_nLength = 200;
  int m_nTurn = 3;
  int m_nPlayerFrame = -1;
  bool m_bDevMode = false;
  bool m_bRecording = false;
  bool m_bRecorded = false;
  //! The demo's own recording while recording.
  std::vector<STimeDemoFrame> m_Own;
  //! For play: the frames of the game's file and of the demo's.
  std::vector<STimeDemoFrame> m_GameFrames, m_OwnFrames;
  //! The frames played back, by the game or by the demo.
  std::vector<STimeDemoFrame> m_Playback;
  bool m_bOwnPlayback = false;
};

DEMO_PLUGIN(CDemoReplay)
