//////////////////////////////////////////////////////////////////////
//
//	File: DemoHost.h
//	Description: Host of the demo programs.
//
//	A demo is a DLL in the game's Bin64 folder that runs inside the
//	game's own FarCry.exe: the tracing renderer (XRenderD3D9Traced,
//	r_Driver "Direct3D8" in system.cfg) loads the DLL named by
//	XRENDERTRACED_DEMO and calls it at every frame (DemoPlugin.h); the
//	renderer behind the proxy is the one XRENDERTRACED_DLL names. From
//	game frame --start on, the host sets a fixed time step, lets the demo
//	draw each frame and saves chosen frames as TGA files, so that two
//	renderers can be compared frame by frame (DemoCompare). After the
//	last saved frame it ends the game the way a trace run is ended
//	(killed, so that the game does not write system.cfg back).
//	RunDemo.ps1 sets the variables and starts the game.
//
//	Arguments (XRENDERTRACED_DEMO_ARGS, every option is optional):
//	  --label <name>        folder name of the frames (default: the renderer
//	                        file without .dll; old for the original renderer)
//	  --out <dir>           folder of the frames (default: DemoShots\<label>)
//	  --frames 30,60,120    frames to save (default: the demo's own)
//	  --start <frame>       game frame after which the demo starts (default 30)
//	  --wait <frames>       frames the demo may wait to be ready, e.g. for a
//	                        level load (default 3000)
//	  --scene <file>        scene file (default: Demos\<demo>.ini)
//	  --until <frame>       the last frame of the run (default: the last
//	                        frame to save); a scene that ends before (a cut
//	                        scene) ends the run there
//	  --video <file.mp4>    also writes every frame of the run into a video
//	                        (Video.h: ffmpeg, on PATH)
//	  --fps <n>             frames per second of the video (default 50: one
//	                        frame of the fixed time step, real time)
//	  --trace               keep the tracing of XRenderD3D9Traced on
//	  --hold                keep the game running after the last frame
//
//	The exit code of the game is 0 when every frame to save was saved,
//	1 when the demo could not start, 2 for bad arguments and 3 when
//	frames are missing.
//
//////////////////////////////////////////////////////////////////////

#pragma once

// Same STL configuration as the engine modules (see CryGame/StdAfx.h).
#define _NOTHREADS
#define _STLP_NO_THREADS

// platform.h includes CryMemoryManager.h: like the engine modules, the demos allocate everything
// from CrySystem's heap, as objects cross into the engine.
#include <platform.h>

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

#include <vector>
#include <string>

#include <Cry_Math.h>
#include <Cry_Camera.h>
#include <ISystem.h>
#include <IConsole.h>
#include <ILog.h>
#include <ITimer.h>
#include <ICryPak.h>
#include <IRenderer.h>
#include <I3DEngine.h>
#include <IStatObj.h>

#include <DemoPlugin.h>

#include "SceneFile.h"

class CDemoHost;

//! One demo scene. The host calls Init once when the demo starts, then for every frame GetCamera
//! before the game draws it and Draw before it is presented, and Shutdown at the end.
class CDemo
{
public:
  virtual ~CDemo() {}

  //! Short name: the host log, the scene file and the saved frames are named after it.
  virtual const char *GetName() const = 0;
  //! Frames saved when --frames is not given, e.g. "30,60".
  virtual const char *GetDefaultFrames() const { return "30"; }
  //! Whether the host clears the screen before Draw; false draws over the game's own frame (a level).
  virtual bool ClearsFrame() const { return true; }

  //! Between two frames, before frame 1; false ends the demo.
  virtual bool Init(CDemoHost &Host) = 0;
  //! Between two frames from Init on, with the frames since Init: true starts frame 1 (after a level
  //! load, for example). The default starts at once.
  virtual bool IsReady(CDemoHost &Host, int nFramesWaited) { return true; }
  //! The view camera of frame nFrame (from 1), the game's one in Cam; true makes the engine draw
  //! the frame with Cam. The default keeps the game's camera.
  virtual bool GetCamera(CDemoHost &Host, int nFrame, CCamera &Cam) { return false; }
  //! Draws frame nFrame, after the game has drawn it and before it is saved and presented.
  virtual void Draw(CDemoHost &Host, int nFrame) = 0;
  //! After frame nFrame: true when the scene has come to its end (a cut scene played to its end);
  //! the run ends there. The default goes on to the last frame.
  virtual bool IsFinished(CDemoHost &Host, int nFrame) { return false; }
  //! Between frame nFrame and the next one, while the demo runs (with --hold too): console commands
  //! and other changes that must not happen inside a frame.
  virtual void AfterFrame(CDemoHost &Host, int nFrame) {}
  virtual void Shutdown(CDemoHost &Host) {}
};

class CDemoHost
{
public:
  ISystem *GetSystem() const { return m_pSystem; }
  //! The tracing renderer, which forwards to the renderer under test.
  IRenderer *GetRenderer() const { return m_pRenderer; }
  I3DEngine *Get3DEngine() const { return m_pSystem->GetI3DEngine(); }
  IConsole *GetConsole() const { return m_pSystem->GetIConsole(); }
  ICryPak *GetPak() const { return m_pSystem->GetIPak(); }
  const CSceneFile &GetScene() const { return m_Scene; }

  //! Writes a line to the engine log (and the host log).
  void Log(const char *szFormat, ...);
  //! Runs a console command (dev mode ignored); between two frames only.
  void Exec(const char *szCommand);
  //! Runs the console commands of a scene value like "a 1; b 2" (separated by ';' or '|').
  void ExecList(const std::string &List);

  //! Names of the files matching szMask in szDir (in the paks and on disk), sorted; szDir like
  //! "textures/", szMask like "*.dds".
  std::vector<std::string> FindFiles(const char *szDir, const char *szMask);
  //! The sub folders of szDir, sorted, like "levels/training/".
  std::vector<std::string> FindDirs(const char *szDir);
  //! The first file matching szMask in szDir or its sub folders (sorted, depth first), or "".
  std::string FindFirstFileDeep(const char *szDir, const char *szMask);
  //! Scene value szKey, or else the first file matching szDir + szMask, or else "".
  std::string SceneFileOrFirst(const char *szKey, const char *szDir, const char *szMask);
  //! The value of a console variable, "" if there is none.
  std::string GetCVarString(const char *szName);

  //! A camera of the screen's size at vPos looking at vTarget, with a field of view of fFov degrees
  //! across the screen (CCamera: the vertical one is fFov * height / width).
  CCamera LookAt(const Vec3 &vPos, const Vec3 &vTarget, float fFov = 60.0f);
  //! A camera looking at the middle of the box (vMin, vMax) from fYaw degrees around z (0: from -y)
  //! and fPitch degrees above, so far away that the box fits into the view (fFov as for LookAt).
  CCamera FrameBox(const Vec3 &vMin, const Vec3 &vMax, float fYaw, float fPitch, float fFov = 45.0f);
  //! A texture made by the demo itself (TestTexture.h), released with the demo; -1 if it failed.
  int GetTestTexture();

  //! The static object of the game named by the scene value szKey, or else the first .cgf under
  //! objects/; NULL if it can't be loaded. Released with I3DEngine::ReleaseObject.
  IStatObj *LoadObject(const char *szKey);
  //! Starts a 3D scene the way the game's menu draws its model: SetCamera, EF_StartEf, the lights
  //! (EF_ClearLightsList, EF_ADDDlight, EF_UpdateDLight; Params.nDLightMask gets all of them).
  void BeginScene(const CCamera &Cam, SRendParams &Params, CDLight *pLights, int nLights);
  //! Draws the scene: EF_EndEf3D(SHDF_SORT).
  void EndScene();
  //! A scene of one static object: BeginScene, IStatObj::Render, EndScene.
  void RenderObject(const CCamera &Cam, IStatObj *pObj, SRendParams &Params, CDLight *pLights, int nLights);

  //! Loads a level the way the game's menu does: the game message StartLevel (Game:SendMessage),
  //! which the game handles after the frame. szMission may be empty (the level's default one).
  //! Between two frames only.
  bool StartLevel(const char *szLevel, const char *szMission = "");
  //! Whether an entity of class Player exists: a level has been loaded and started.
  bool HasPlayer();

  //! Frame time of the fixed time step.
  float GetFrameTime() const { return m_fFrameTime; }

private:
  friend class CDemoPlugin;

  ISystem *m_pSystem = NULL;
  IRenderer *m_pRenderer = NULL;
  CSceneFile m_Scene;
  float m_fFrameTime = 0.02f;
  int m_nTestTexture = 0;
};

//! Creates the plugin that runs Demo (CreateDemoPlugin of DemoPlugin.h).
IDemoPlugin *DemoHost_Create(CDemo &Demo, int nVersion, ISystem *pSystem, IRenderer *pRenderer, const char *szArgs);

//! The export of a demo DLL.
#define DEMO_PLUGIN(DemoClass) \
  extern "C" __declspec(dllexport) IDemoPlugin *CreateDemoPlugin(int nVersion, ISystem *pSystem, IRenderer *pRenderer, const char *szArgs) \
  { \
    static DemoClass s_Demo; \
    return DemoHost_Create(s_Demo, nVersion, pSystem, pRenderer, szArgs); \
  }
