// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: DemoLevel.cpp
//	Description: A level of the game, drawn by the engine.
//
//	The demo loads a level the way the game's menu does (the game message
//	StartLevel, which the game handles after the frame) and waits until
//	the player has been made and then a number of frames more. Frame 1
//	starts after that, drawn by the engine itself: from the game's own
//	camera, from a fixed camera, from a camera moving along a path of
//	keyframes (a flyover), or by a sequence of the movie system (a cut
//	scene) played from its start. The level's sequences are listed in the
//	demo's log. The demo does not clear the screen: the frames are the
//	game's.
//
//	Scene file keys (Demos\DemoLevel.ini):
//	  level = <name>             the level (default: the first folder in levels/)
//	  mission = <name>           its mission (default: the level's default)
//	  settle = <frames>          frames after the player exists (default 300:
//	                             the level fades in after its start)
//	  exec = <commands>          console commands once the level is there,
//	                             separated by ';' or '|' (e.g. cl_display_hud 0)
//	  sky = <shader>             a sky shader of the shader scripts instead of the
//	                             level's own (I3DEngine::SetSkyBox, e.g. CloudsSky1)
//	  camera = x y z ax ay az    a fixed camera (position, angles in degrees; the log
//	                             has the game's camera when frame 1 starts). The
//	                             angles as CCamera::SetAngle takes them: ax 0 looks
//	                             level, 90 straight down, az turns about z
//	  path =x y z ax ay az frame | x y z ax ay az frame | ...
//	                             a camera path, used instead of camera: keyframes
//	                             of position, angles and the demo frame (from 1)
//	                             of the camera there, separated by '|' or ','.
//	                             Between keyframes the camera is interpolated,
//	                             each angle the short way round; before the first
//	                             and after the last it stays. The run ends after
//	                             the last keyframe (with --until beyond it, e.g.
//	                             --until 100000 --video fly.mp4)
//	  path2, path3, ... = ...    more keyframes (a line of the scene file holds
//	                             about 1000 characters)
//	  interpolation = linear     straight from keyframe to keyframe (default:
//	                             catmull-rom, a smooth curve through them)
//	  above = <m>                the path's camera at least that high above the
//	                             terrain (a flyover at a fixed height would go
//	                             through the hills: from inside a hill the
//	                             engine draws what no player sees)
//	  sequence = <name>          a sequence of the movie system to play (the
//	                             level's own cut scene is stopped first, and
//	                             without a sequence too). For the level's own
//	                             opening cut scene settle = 0: it starts after
//	                             the player exists, and stopped after it has
//	                             run for a while, it may leave its actors in
//	                             the level, so that they are there twice.
//	                             The run ends when the sequence has played
//	                             (with --until beyond its length: a whole cut
//	                             scene, e.g. into a video with --video)
//	  actor = <entity name>      an entity moved along actorpath each frame (a
//	                             boat, a character; player: the player), its
//	                             position and angles set and its physics' velocity
//	                             the path's (a character's left to it)
//	  actorpath = x y z ax ay az frame | ...
//	                             its keyframes as the camera path's (actorpath2,
//	                             ... more; interpolated as it is), the angles as
//	                             the entity's (az its heading)
//
//////////////////////////////////////////////////////////////////////

#include "DemoHost.h"
#include <IMovieSystem.h>
#include <algorithm>
// The engine's headers call min and max unqualified.
using std::max;
using std::min;
#include <IEntitySystem.h>
#include <physinterface.h>

class CDemoLevel : public CDemo
{
public:
  virtual const char *GetName() const { return "DemoLevel"; }
  virtual const char *GetDefaultFrames() const { return "1,50,100"; }
  virtual bool ClearsFrame() const { return false; }

  virtual bool Init(CDemoHost &Host)
  {
    std::string Level = Host.GetScene().Get("level");
    if (Level.empty())
    {
      std::vector<std::string> Levels = Host.FindDirs("levels/");
      if (!Levels.empty())
        Level = Levels[0].substr(7, Levels[0].size() - 8);
    }
    if (Level.empty())
    {
      Host.Log("DemoLevel: no level");
      return false;
    }
    ReadPath(Host);
    m_ActorPath = ReadKeys(Host, "actorpath");
    return Host.StartLevel(Level.c_str(), Host.GetScene().Get("mission").c_str());
  }

  virtual bool IsReady(CDemoHost &Host, int nFramesWaited)
  {
    if (m_nPlayerFrame < 0)
    {
      if (!Host.HasPlayer())
        return false;
      m_nPlayerFrame = nFramesWaited;
      Host.Log("DemoLevel: the player is there after %d frames", nFramesWaited);
      ListSequences(Host);
    }
    if (nFramesWaited - m_nPlayerFrame < Host.GetScene().GetInt("settle", 300))
      return false;

    // The end of a cut scene turns the HUD back on: the commands come after it.
    IMovieSystem *pMovie = Host.GetSystem()->GetIMovieSystem();
    std::string Sequence = Host.GetScene().Get("sequence");
    if (pMovie && !Sequence.empty())
    {
      // Most levels start with a cut scene of their own, which may run by now.
      Host.Exec("SkipCutScene");
      m_pSequence = pMovie->FindSequence(Sequence.c_str());
      if (m_pSequence)
      {
        const float fLength = m_pSequence->GetTimeRange().Length();
        Host.Log("DemoLevel: sequence '%s' played, %g s (%d frames)", Sequence.c_str(), fLength,
          (int)(fLength / Host.GetFrameTime() + 0.5f));
        pMovie->PlaySequence(m_pSequence, true);
      }
      else
        Host.Log("DemoLevel: sequence '%s' not found", Sequence.c_str());
    }
    else
      Host.Exec("SkipCutScene");
    Host.ExecList(Host.GetScene().Get("exec"));
    const std::string Sky = Host.GetScene().Get("sky");
    if (!Sky.empty())
    {
      Host.Get3DEngine()->SetSkyBox(Sky.c_str());
      Host.Log("DemoLevel: sky shader '%s'", Sky.c_str());
    }
    m_Camera = Host.GetScene().GetFloats("camera");
    FindActor(Host);
    const CCamera &View = Host.GetSystem()->GetViewCamera();
    Host.Log("DemoLevel: the game's camera %g %g %g %g %g %g", View.GetPos().x, View.GetPos().y, View.GetPos().z,
      View.GetAngles().x, View.GetAngles().y, View.GetAngles().z);
    return true;
  }

  virtual bool GetCamera(CDemoHost &Host, int nFrame, CCamera &Cam)
  {
    MoveActor(Host, nFrame);
    if (!m_Path.empty())
    {
      Vec3 vPos = PathValue(m_Path, &SKey::vPos, (float)nFrame);
      if (m_fAbove >= 0)
      {
        const float fLow = Host.Get3DEngine()->GetTerrainElevation(vPos.x, vPos.y) + m_fAbove;
        if (vPos.z < fLow)
        {
          m_nRaised++;
          m_fMaxRaise = fLow - vPos.z > m_fMaxRaise ? fLow - vPos.z : m_fMaxRaise;
          vPos.z = fLow;
        }
      }
      Cam.SetPos(vPos);
      Cam.SetAngle(PathValue(m_Path, &SKey::vAngles, (float)nFrame));
      Cam.Update();
      return true;
    }
    if (m_Camera.size() < 6)
      return false;
    Cam.SetPos(Vec3(m_Camera[0], m_Camera[1], m_Camera[2]));
    Cam.SetAngle(Vec3(m_Camera[3], m_Camera[4], m_Camera[5]));
    Cam.Update();
    return true;
  }

  virtual void Draw(CDemoHost &Host, int nFrame) {}

  virtual bool IsFinished(CDemoHost &Host, int nFrame)
  {
    if (!m_Path.empty() && nFrame >= m_Path.back().fFrame)
    {
      if (m_fAbove >= 0 && !m_bRaiseLogged)
      {
        m_bRaiseLogged = true;
        Host.Log("DemoLevel: the camera raised above the terrain in %d frames, by up to %.1f m", m_nRaised, m_fMaxRaise);
      }
      return true;
    }
    return m_pSequence && !Host.GetSystem()->GetIMovieSystem()->IsPlaying(m_pSequence);
  }

private:
  //! A keyframe of the camera path.
  struct SKey
  {
    Vec3 vPos;
    Vec3 vAngles;
    float fFrame;
  };

  //! The keyframes of the keys szPrefix, szPrefix2, ... (path, actorpath): seven numbers each,
  //! sorted by their frames, every angle within 180 degrees of the one before.
  static std::vector<SKey> ReadKeys(CDemoHost &Host, const char *szPrefix)
  {
    std::vector<std::string> Keys = Host.GetScene().GetKeys(szPrefix);
    std::vector<float> Numbers;
    std::vector<SKey> Path;
    for (size_t i = 0; i < Keys.size(); i++)
    {
      bool bRead = false;
      for (size_t j = 0; j < i; j++)
        bRead = bRead || !stricmp(Keys[i].c_str(), Keys[j].c_str());
      if (bRead)
        continue;
      std::vector<float> Line = Host.GetScene().GetFloats(Keys[i].c_str());
      Numbers.insert(Numbers.end(), Line.begin(), Line.end());
    }
    if (Numbers.size() % 7)
      Host.Log("DemoLevel: the %s has %d numbers, not 7 per keyframe: the last %d are left out", szPrefix, (int)Numbers.size(),
        (int)(Numbers.size() % 7));
    for (size_t i = 0; i + 7 <= Numbers.size(); i += 7)
    {
      SKey Key;
      Key.vPos = Vec3(Numbers[i], Numbers[i + 1], Numbers[i + 2]);
      Key.vAngles = Vec3(Numbers[i + 3], Numbers[i + 4], Numbers[i + 5]);
      Key.fFrame = Numbers[i + 6];
      Path.push_back(Key);
    }
    std::stable_sort(Path.begin(), Path.end(), [](const SKey &a, const SKey &b) { return a.fFrame < b.fFrame; });
    for (size_t i = 1; i < Path.size(); i++)
    {
      for (int c = 0; c < 3; c++)
      {
        float fTurn = Path[i].vAngles[c] - Path[i - 1].vAngles[c];
        fTurn -= 360.0f * floorf((fTurn + 180.0f) / 360.0f);
        Path[i].vAngles[c] = Path[i - 1].vAngles[c] + fTurn;
      }
    }
    return Path;
  }

  //! The camera's keyframes (path, path2, ...) and how they are interpolated.
  void ReadPath(CDemoHost &Host)
  {
    m_Path = ReadKeys(Host, "path");
    m_bLinear = !stricmp(Host.GetScene().Get("interpolation").c_str(), "linear");
    if (m_Path.empty())
      return;
    m_fAbove = Host.GetScene().GetFloat("above", -1);
    Host.Log("DemoLevel: camera path of %d keyframes, frames %g to %g, %s", (int)m_Path.size(), m_Path.front().fFrame,
      m_Path.back().fFrame, m_bLinear ? "linear" : "catmull-rom");
    if (m_fAbove >= 0)
      Host.Log("DemoLevel: the camera at least %g m above the terrain", m_fAbove);
  }

  //! The velocity of a member of the keyframes at keyframe i, per frame: Catmull-Rom, from the
  //! keyframes before and after (at the ends half the one of the first or last segment).
  static Vec3 PathVelocity(const std::vector<SKey> &Path, Vec3 SKey::*pValue, size_t i)
  {
    const size_t nPrev = i > 0 ? i - 1 : i, nNext = i + 1 < Path.size() ? i + 1 : i;
    const float fFrames = Path[nNext].fFrame - Path[nPrev].fFrame;
    if (fFrames <= 0)
      return Vec3(0, 0, 0);
    const float fScale = nPrev == i || nNext == i ? 0.5f : 1.0f;
    return (Path[nNext].*pValue - Path[nPrev].*pValue) * (fScale / fFrames);
  }

  //! A member of the keyframes at frame fFrame.
  Vec3 PathValue(const std::vector<SKey> &Path, Vec3 SKey::*pValue, float fFrame) const
  {
    if (fFrame <= Path.front().fFrame)
      return Path.front().*pValue;
    if (fFrame >= Path.back().fFrame)
      return Path.back().*pValue;
    size_t i = 0;
    while (Path[i + 1].fFrame <= fFrame)
      i++;
    const SKey &A = Path[i], &B = Path[i + 1];
    const float fFrames = B.fFrame - A.fFrame, t = (fFrame - A.fFrame) / fFrames;
    if (m_bLinear)
      return A.*pValue + (B.*pValue - A.*pValue) * t;
    // Cubic Hermite curve between the two keyframes.
    const float t2 = t * t, t3 = t2 * t;
    return A.*pValue * (2 * t3 - 3 * t2 + 1) + PathVelocity(Path, pValue, i) * (fFrames * (t3 - 2 * t2 + t)) +
      B.*pValue * (3 * t2 - 2 * t3) + PathVelocity(Path, pValue, i + 1) * (fFrames * (t3 - t2));
  }

  //! The entity of actor (player: the first of class Player), when there is an actor path.
  void FindActor(CDemoHost &Host)
  {
    const std::string Name = Host.GetScene().Get("actor");
    IEntitySystem *pEntities = Host.GetSystem()->GetIEntitySystem();
    if (Name.empty() || m_ActorPath.empty() || !pEntities)
      return;
    IEntity *pActor = NULL;
    if (!stricmp(Name.c_str(), "player"))
    {
      IEntityIt *pIt = pEntities->GetEntityIterator();
      while (pIt && !pIt->IsEnd() && !pActor)
      {
        IEntity *pEntity = pIt->Next();
        if (pEntity && pEntity->GetEntityClassName() && !strcmp(pEntity->GetEntityClassName(), "Player"))
          pActor = pEntity;
      }
      if (pIt)
        pIt->Release();
    }
    else
      pActor = pEntities->GetEntity(Name.c_str());
    if (!pActor)
    {
      Host.Log("DemoLevel: no actor '%s'", Name.c_str());
      return;
    }
    m_nActor = (int)pActor->GetId();
    Host.Log("DemoLevel: actor '%s' (class '%s') along a path of %d keyframes, frames %g to %g", Name.c_str(),
      pActor->GetEntityClassName(), (int)m_ActorPath.size(), m_ActorPath.front().fFrame, m_ActorPath.back().fFrame);
  }

  //! The actor where its path is at frame nFrame; its physics' velocity the path's (a rigid body
  //! or a vehicle would else fall and drift between the frames; a character's moves itself).
  void MoveActor(CDemoHost &Host, int nFrame)
  {
    IEntitySystem *pEntities = Host.GetSystem()->GetIEntitySystem();
    IEntity *pActor = m_nActor >= 0 && pEntities ? pEntities->GetEntity((EntityId)m_nActor) : NULL;
    if (!pActor)
      return;
    const Vec3 vPos = PathValue(m_ActorPath, &SKey::vPos, (float)nFrame);
    pActor->SetPos(vPos);
    pActor->SetAngles(PathValue(m_ActorPath, &SKey::vAngles, (float)nFrame));
    IPhysicalEntity *pPhysics = pActor->GetPhysics();
    if (pPhysics && pPhysics->GetType() != PE_LIVING && Host.GetFrameTime() > 0)
    {
      pe_action_set_velocity Velocity;
      Velocity.v = (PathValue(m_ActorPath, &SKey::vPos, (float)nFrame + 1) - vPos) * (1 / Host.GetFrameTime());
      Velocity.w = vectorf(0, 0, 0);
      pPhysics->Action(&Velocity);
    }
  }

  static void ListSequences(CDemoHost &Host)
  {
    IMovieSystem *pMovie = Host.GetSystem()->GetIMovieSystem();
    ISequenceIt *pIt = pMovie ? pMovie->GetSequences() : NULL;
    for (IAnimSequence *pSeq = pIt ? pIt->first() : NULL; pSeq; pSeq = pIt->next())
      Host.Log("DemoLevel: sequence '%s', flags 0x%x", pSeq->GetName(), pSeq->GetFlags());
    if (pIt)
      pIt->Release();
  }

  int m_nPlayerFrame = -1;
  //! The sequence played, if any.
  IAnimSequence *m_pSequence = NULL;
  std::vector<float> m_Camera;
  std::vector<SKey> m_Path;
  //! The actor's keyframes and its entity's id (-1: none).
  std::vector<SKey> m_ActorPath;
  int m_nActor = -1;
  bool m_bLinear = false;
  //! above, or -1 without it; the frames it raised the camera in and the most it did.
  float m_fAbove = -1;
  int m_nRaised = 0;
  float m_fMaxRaise = 0;
  bool m_bRaiseLogged = false;
};

DEMO_PLUGIN(CDemoLevel)
