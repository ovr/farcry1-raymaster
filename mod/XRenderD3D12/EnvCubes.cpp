// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
//////////////////////////////////////////////////////////////////////
//
//	File: EnvCubes.cpp
//	Description: The environment cubes of the templates that reflect the
//	scene around the object (TemplReflEnvCM and its aliases,
//	EEnvMap::Auto).
//
//	Before the main view is drawn, the objects of its items with such a
//	template get a cube seen from their centre (objects close together
//	share one). The 3D engine renders the scene into a face (DrawLowDetail
//	with the template's scene flags, the system's view camera at the cube's
//	point, 90 degrees wide along the face's axis) as a scene nested in the
//	main one. A new cube gets its six faces at once; then one face is
//	redrawn every r_EnvCMupdateInterval seconds, in turn. The faces are
//	64 << r_EnvCMResolution texels wide and in world axes, laid out as the
//	D3D cube faces: seen from inside, a face's picture is mirrored, so the
//	culling is turned round. The objects with such a template are not drawn
//	into the cubes (an object does not see itself).
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "D3D12Device.h"
#include "D3D12Descriptors.h"
#include "Shader.h"
#include "Scene.h"

namespace
{

//! What a face shows (EDrawLowDetailFlags): the sky, the terrain at full detail with its detail
//! textures and its water, the static objects, the far vegetation sprites, the particles, the
//! entities and the player.
constexpr int kEnvCubeDrawFlags = DLD_DETAIL_TEXTURES | DLD_TERRAIN_WATER | DLD_FAR_SPRITES | DLD_STATIC_OBJECTS | DLD_PARTICLES |
                                  DLD_TERRAIN_FULLRES | DLD_FIRST_PERSON_CAMERA_OWNER | DLD_ENTITIES | DLD_TERRAIN;
constexpr int ENV_CUBE_MAX = 8;               //!< cubes kept
constexpr float ENV_CUBE_SHARE_DIST = 2.0f;   //!< an object this close to a cube of the frame uses it
constexpr float ENV_CUBE_FOLLOW_DIST = 8.0f;  //!< a cube of an earlier frame this close follows the object
constexpr float ENV_CUBE_NEAR = 0.25f;

//! A face in the order of D3D12_TEXTURECUBE_FACE: the axis it looks along, and the directions of
//! its picture's right and up, in world axes.
struct SCubeFace
{
  Vec3 vLook, vRight, vUp;
};

const SCubeFace s_CubeFaces[6] =
{
  { Vec3( 1,  0,  0), Vec3( 0,  0, -1), Vec3( 0,  1,  0) },
  { Vec3(-1,  0,  0), Vec3( 0,  0,  1), Vec3( 0,  1,  0) },
  { Vec3( 0,  1,  0), Vec3( 1,  0,  0), Vec3( 0,  0, -1) },
  { Vec3( 0, -1,  0), Vec3( 1,  0,  0), Vec3( 0,  0,  1) },
  { Vec3( 0,  0,  1), Vec3( 1,  0,  0), Vec3( 0,  1,  0) },
  { Vec3( 0,  0, -1), Vec3(-1,  0,  0), Vec3( 0,  1,  0) },
};

bool UsesEnvCube(const SRenderItem &it)
{
  return it.pShader && it.pShader->GetProps().eEnvMap == EEnvMap::Auto;
}

//! The point an item's cube is seen from: the centre of its element in the world.
Vec3 EnvCubeCenter(const SRenderItem &it)
{
  Vec3 v(0, 0, 0);
  it.pRE->mfCenter(v, it.pObj);
  return v;
}

}

void CD3D12Renderer::UpdateEnvCubes(std::span<const SRenderItem> Items)
{
  // The main view of the game.
  I3DEngine *p3D = iSystem ? iSystem->GetI3DEngine() : nullptr;
  if (!(m_nSceneFlags & SHDF_ALLOWHDR) || m_nRecurseLevel > 1 || m_bReflectionPass || m_bEnvCubePass || m_bSpritePass || m_bShadowMapPass ||
      !p3D || !m_pDevice || !m_pDescriptors || std::none_of(Items.begin(), Items.end(), UsesEnvCube) || !EnsureFrame())
    return;
  const int nSize = 64 << std::clamp(CV_r_EnvCMResolution, 0, 2);

  // The cubes of the frame, one per object or group of objects close together: the nearest cube
  // of an earlier frame follows the object, else it gets a new one (or the one unused the longest).
  std::vector<int> Claimed;
  for (const SRenderItem &it : Items)
  {
    if (!UsesEnvCube(it))
      continue;
    const Vec3 vPos = EnvCubeCenter(it);
    auto Dist = [&vPos](const SEnvCube &c) { return (c.vPos - vPos).GetLength(); };
    if (std::any_of(Claimed.begin(), Claimed.end(), [&](int i) { return Dist(m_EnvCubes[i]) < ENV_CUBE_SHARE_DIST; }))
      continue;
    int nCube = -1;
    for (int i = 0; i < (int)m_EnvCubes.size(); i++)
    {
      const SEnvCube &c = m_EnvCubes[i];
      if (c.nUsedFrame != m_nFrames && Dist(c) < ENV_CUBE_FOLLOW_DIST && (nCube < 0 || Dist(c) < Dist(m_EnvCubes[nCube])))
        nCube = i;
    }
    if (nCube < 0 && (int)m_EnvCubes.size() < ENV_CUBE_MAX)
    {
      m_EnvCubes.emplace_back();
      nCube = (int)m_EnvCubes.size() - 1;
    }
    if (nCube < 0)
    {
      for (int i = 0; i < (int)m_EnvCubes.size(); i++)
      {
        if (m_EnvCubes[i].nUsedFrame != m_nFrames && (nCube < 0 || m_EnvCubes[i].nUsedFrame < m_EnvCubes[nCube].nUsedFrame))
          nCube = i;
      }
      if (nCube < 0)
        continue;
      FreeEnvCube(m_EnvCubes[nCube]);
    }
    SEnvCube &c = m_EnvCubes[nCube];
    if (c.pColor && c.nSize != nSize)
      FreeEnvCube(c);
    if (!c.pColor)
    {
      for (D3D12_CPU_DESCRIPTOR_HANDLE &h : c.hRTV)
        h = m_pDescriptors->AllocRTV();
      c.hDSV = m_pDescriptors->AllocDSV();
      D3D12_SHADER_RESOURCE_VIEW_DESC Desc = {};
      Desc.Format = D3D12_BACKBUFFER_FORMAT;
      Desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
      Desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      Desc.TextureCube.MipLevels = 1;
      if (!m_pDevice->CreateCubeTargets(nSize, c.pColor, c.pDepth, c.hRTV, c.hDSV) || (c.nSRV = m_pDescriptors->AllocSRV(c.pColor.Get(), &Desc)) < 0)
      {
        FreeEnvCube(c);
        continue;
      }
      c.nSize = nSize;
    }
    c.vPos = vPos;
    c.nUsedFrame = m_nFrames;
    Claimed.push_back(nCube);
  }

  // The faces due: all of a new cube, else the next one when the last is older than
  // r_EnvCMupdateInterval.
  const float fTime = iTimer ? iTimer->GetCurrTime() : 0.0f;
  std::vector<std::pair<int, int>> Faces;
  for (int i : Claimed)
  {
    SEnvCube &c = m_EnvCubes[i];
    if (c.nFaces < 6)
    {
      for (int f = c.nFaces; f < 6; f++)
        Faces.emplace_back(i, f);
      c.nFaces = 6;
      c.nNextFace = 0;
    }
    else if (fTime - c.fFaceTime >= CV_r_EnvCMupdateInterval || fTime < c.fFaceTime)
    {
      Faces.emplace_back(i, c.nNextFace);
      c.nNextFace = (c.nNextFace + 1) % 6;
    }
    else
      continue;
    c.fFaceTime = fTime;
  }
  if (Faces.empty())
    return;

  // What the faces' scenes change.
  CCamera ViewCamera = iSystem->GetViewCamera();
  const CCamera Camera = m_Camera;
  const Matrix44 CameraView = m_CameraView, ViewMatrix = m_ViewMatrix, ProjMatrix = m_ProjMatrix, SkyProjMatrix = m_SkyProjMatrix;
  const int nMatrixDepth = m_nMatrixDepth;
  int Viewport[4];
  memcpy(Viewport, m_Viewport, sizeof(Viewport));
  const bool bFog = m_bFog;
  const float fFogStart = m_fFogStart, fFogEnd = m_fFogEnd;
  float FogColor[4], WorldColor[4];
  memcpy(FogColor, m_FogColor, sizeof(FogColor));
  memcpy(WorldColor, m_WorldColor, sizeof(WorldColor));
  const Vec3 vClearColor = m_vClearColor;
  const int nState = m_nState, nCullMode = m_nCullMode;

  for (const auto &[nCube, nFace] : Faces)
  {
    if (m_pFrameDump)
      fprintf(m_pFrameDump.get(), "environment cube %d (%d): face %d from (%.2f %.2f %.2f)\n", nCube, m_EnvCubes[nCube].nSize, nFace,
              m_EnvCubes[nCube].vPos.x, m_EnvCubes[nCube].vPos.y, m_EnvCubes[nCube].vPos.z);
    DrawEnvCubeFace(m_EnvCubes[nCube], nFace);
  }
  m_pDevice->SetBackBufferTargets();

  // The frame as it was.
  iSystem->SetViewCamera(ViewCamera);
  m_Camera = Camera;
  m_CameraView = CameraView;
  m_ViewMatrix = ViewMatrix;
  m_ProjMatrix = ProjMatrix;
  m_SkyProjMatrix = SkyProjMatrix;
  m_nMatrixDepth = nMatrixDepth;
  memcpy(m_Viewport, Viewport, sizeof(Viewport));
  m_bFog = bFog;
  m_fFogStart = fFogStart;
  m_fFogEnd = fFogEnd;
  memcpy(m_FogColor, FogColor, sizeof(FogColor));
  memcpy(m_WorldColor, WorldColor, sizeof(WorldColor));
  m_vClearColor = vClearColor;
  m_nState = nState;
  m_nCullMode = nCullMode;
  InvalidateFrameConstants();
}

void CD3D12Renderer::DrawEnvCubeFace(SEnvCube &Cube, int nFace)
{
  I3DEngine *p3D = iSystem->GetI3DEngine();
  const SCubeFace &F = s_CubeFaces[nFace];

  // The engine's camera: at the cube's point, looking along the face's axis, 90 degrees wide and
  // high, as far as the engine draws.
  CCamera Cam;
  Cam.Init(Cube.nSize, Cube.nSize, gf_PI * 0.5f, std::max(p3D->GetMaxViewDistance(), ENV_CUBE_NEAR + 1.0f), 1.0f, ENV_CUBE_NEAR);
  Cam.SetPos(Cube.vPos);
  Cam.SetAngle(ConvertVectorToCameraAngles(F.vLook));
  Cam.Update();

  // The face's view: x along the picture's right, y along its up, looking along -z (the projection
  // is right-handed).
  Matrix44 View;
  float *v = View.data;
  const Vec3 Axes[3] = { F.vRight, F.vUp, -F.vLook };
  for (int c = 0; c < 3; c++)
  {
    v[c] = Axes[c].x;
    v[4 + c] = Axes[c].y;
    v[8 + c] = Axes[c].z;
    v[12 + c] = -Cube.vPos.Dot(Axes[c]);
  }
  v[3] = v[7] = v[11] = 0;
  v[15] = 1;

  m_pDevice->SetOffscreenTargets(Cube.pColor.Get(), Cube.hRTV[nFace], Cube.hDSV, Cube.nSize, Cube.nSize);
  const float Clear[4] = { m_vClearColor.x, m_vClearColor.y, m_vClearColor.z, 0 };
  m_pDevice->ClearColor(Clear);
  m_pDevice->ClearDepth(1.0f, 0, true, true);
  m_Camera = Cam;
  m_CameraView = m_ViewMatrix = View;
  m_nMatrixDepth = 0;
  m_ProjMatrix = m_SkyProjMatrix = MakeProjection(Cam);
  InvalidateFrameConstants();

  // The 3D engine renders the scene (a frame of its own, nested in this one).
  m_bEnvCubePass = true;
  iSystem->SetViewCamera(Cam);
  p3D->DrawLowDetail(kEnvCubeDrawFlags);
  m_bEnvCubePass = false;
}

int CD3D12Renderer::GetEnvCubeSRV(const SRenderItem &it) const
{
  // The cube of the frame nearest to the item's point.
  const Vec3 vPos = EnvCubeCenter(it);
  int nSRV = -1;
  float fBest = ENV_CUBE_SHARE_DIST;
  for (const SEnvCube &c : m_EnvCubes)
  {
    const float d = (c.vPos - vPos).GetLength();
    if (c.nUsedFrame == m_nFrames && c.nSRV >= 0 && c.nFaces == 6 && d < fBest)
    {
      fBest = d;
      nSRV = c.nSRV;
    }
  }
  return nSRV;
}

void CD3D12Renderer::FreeEnvCube(SEnvCube &Cube)
{
  if (m_pDescriptors)
  {
    if (Cube.nSRV >= 0)
      m_pDescriptors->FreeSRV(Cube.nSRV);
    for (const D3D12_CPU_DESCRIPTOR_HANDLE &h : Cube.hRTV)
    {
      if (h.ptr)
        m_pDescriptors->FreeRTV(h);
    }
    if (Cube.hDSV.ptr)
      m_pDescriptors->FreeDSV(Cube.hDSV);
  }
  if (m_pDevice)
  {
    m_pDevice->DeferredRelease(Cube.pColor);
    m_pDevice->DeferredRelease(Cube.pDepth);
  }
  Cube = SEnvCube();
}

void CD3D12Renderer::ReleaseEnvCubes()
{
  for (SEnvCube &c : m_EnvCubes)
    FreeEnvCube(c);
  m_EnvCubes.clear();
}
