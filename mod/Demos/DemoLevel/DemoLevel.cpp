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
//
//////////////////////////////////////////////////////////////////////

#include "DemoHost.h"
#include <IMovieSystem.h>
#include <algorithm>

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
    const CCamera &View = Host.GetSystem()->GetViewCamera();
    Host.Log("DemoLevel: the game's camera %g %g %g %g %g %g", View.GetPos().x, View.GetPos().y, View.GetPos().z,
      View.GetAngles().x, View.GetAngles().y, View.GetAngles().z);
    return true;
  }

  virtual bool GetCamera(CDemoHost &Host, int nFrame, CCamera &Cam)
  {
    if (!m_Path.empty())
    {
      Cam.SetPos(PathValue(&SKey::vPos, (float)nFrame));
      Cam.SetAngle(PathValue(&SKey::vAngles, (float)nFrame));
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
      return true;
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

  //! The keyframes of path, path2, ...: seven numbers each, sorted by their frames, every angle
  //! within 180 degrees of the one before.
  void ReadPath(CDemoHost &Host)
  {
    std::vector<std::string> Keys = Host.GetScene().GetKeys("path");
    std::vector<float> Numbers;
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
      Host.Log("DemoLevel: the path has %d numbers, not 7 per keyframe: the last %d are left out", (int)Numbers.size(),
        (int)(Numbers.size() % 7));
    for (size_t i = 0; i + 7 <= Numbers.size(); i += 7)
    {
      SKey Key;
      Key.vPos = Vec3(Numbers[i], Numbers[i + 1], Numbers[i + 2]);
      Key.vAngles = Vec3(Numbers[i + 3], Numbers[i + 4], Numbers[i + 5]);
      Key.fFrame = Numbers[i + 6];
      m_Path.push_back(Key);
    }
    if (m_Path.empty())
      return;
    std::stable_sort(m_Path.begin(), m_Path.end(), [](const SKey &a, const SKey &b) { return a.fFrame < b.fFrame; });
    for (size_t i = 1; i < m_Path.size(); i++)
    {
      for (int c = 0; c < 3; c++)
      {
        float fTurn = m_Path[i].vAngles[c] - m_Path[i - 1].vAngles[c];
        fTurn -= 360.0f * floorf((fTurn + 180.0f) / 360.0f);
        m_Path[i].vAngles[c] = m_Path[i - 1].vAngles[c] + fTurn;
      }
    }
    m_bLinear = !stricmp(Host.GetScene().Get("interpolation").c_str(), "linear");
    Host.Log("DemoLevel: camera path of %d keyframes, frames %g to %g, %s", (int)m_Path.size(), m_Path.front().fFrame,
      m_Path.back().fFrame, m_bLinear ? "linear" : "catmull-rom");
  }

  //! The velocity of a member of the keyframes at keyframe i, per frame: Catmull-Rom, from the
  //! keyframes before and after (at the ends half the one of the first or last segment).
  Vec3 PathVelocity(Vec3 SKey::*pValue, size_t i) const
  {
    const size_t nPrev = i > 0 ? i - 1 : i, nNext = i + 1 < m_Path.size() ? i + 1 : i;
    const float fFrames = m_Path[nNext].fFrame - m_Path[nPrev].fFrame;
    if (fFrames <= 0)
      return Vec3(0, 0, 0);
    const float fScale = nPrev == i || nNext == i ? 0.5f : 1.0f;
    return (m_Path[nNext].*pValue - m_Path[nPrev].*pValue) * (fScale / fFrames);
  }

  //! A member of the keyframes at frame fFrame.
  Vec3 PathValue(Vec3 SKey::*pValue, float fFrame) const
  {
    if (fFrame <= m_Path.front().fFrame)
      return m_Path.front().*pValue;
    if (fFrame >= m_Path.back().fFrame)
      return m_Path.back().*pValue;
    size_t i = 0;
    while (m_Path[i + 1].fFrame <= fFrame)
      i++;
    const SKey &A = m_Path[i], &B = m_Path[i + 1];
    const float fFrames = B.fFrame - A.fFrame, t = (fFrame - A.fFrame) / fFrames;
    if (m_bLinear)
      return A.*pValue + (B.*pValue - A.*pValue) * t;
    // Cubic Hermite curve between the two keyframes.
    const float t2 = t * t, t3 = t2 * t;
    return A.*pValue * (2 * t3 - 3 * t2 + 1) + PathVelocity(pValue, i) * (fFrames * (t3 - 2 * t2 + t)) +
      B.*pValue * (3 * t2 - 2 * t3) + PathVelocity(pValue, i + 1) * (fFrames * (t3 - t2));
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
  bool m_bLinear = false;
};

DEMO_PLUGIN(CDemoLevel)
