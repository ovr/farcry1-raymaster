//////////////////////////////////////////////////////////////////////
//
//	File: DemoPlugin.h
//	Description: Interface between the tracing renderer and a demo DLL.
//
//	The demos run inside the game's own FarCry.exe: with
//	XRENDERTRACED_DEMO=<dll> the proxy loads that DLL (a bare name from
//	its own folder), creates the demo with CreateDemoPlugin and calls it
//	at three points of every frame. XRENDERTRACED_DEMO_ARGS is handed to
//	the demo as its command line (see mod/Demos/DemoHost/DemoHost.h).
//
//////////////////////////////////////////////////////////////////////

#pragma once

//! Frame numbers are the proxy's: the frame being drawn is n, frames are counted from 1 when
//! IRenderer::Update presents them. None of the calls is made inside a traced call.
class IDemoPlugin
{
public:
  //! In IRenderer::BeginFrame, before the engine draws frame n (e.g. to set the view camera).
  virtual void BeginFrame(int nFrame) = 0;
  //! In IRenderer::Update, before frame n is presented: draws over it and saves it.
  virtual void EndFrame(int nFrame) = 0;
  //! After frame n was presented, between two frames: console commands, level loads, the end.
  virtual void AfterFrame(int nFrame) = 0;
  //! In IRenderer::Release, before the original renderer is released.
  virtual void Release() = 0;

protected:
  virtual ~IDemoPlugin() {}
};

//! Raised when the interface changes; a demo built for another version returns NULL.
#define DEMO_PLUGIN_VERSION 1

//! Export of the demo DLL. pRenderer is the proxy, so the demo's calls are traced like the engine's.
typedef IDemoPlugin *(*PFN_CreateDemoPlugin)(int nVersion, ISystem *pSystem, IRenderer *pRenderer, const char *szArgs);
#define DEMO_PLUGIN_CREATE "CreateDemoPlugin"
