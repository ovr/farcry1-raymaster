// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Wake.cpp
//	Description: The wakes (d3d12_WaterWakes): the waves of the bodies
//	moving through the ocean's surface, the boats' wakes, the rings round a
//	swimmer, their foam.
//
//	A window of XR_WAKE_SIZE x XR_WAKE_SIZE cells of XR_WAKE_CELL metres
//	(1024 of 12.5 cm: 128 m; a swimmer's waves are under a metre long)
//	around the camera holds the waves (Shaders/Wake.hlsli's
//	eWave: the height and the filtered velocity potential, RG32F), each
//	texel the cell of its index modulo the window's size, so that the
//	window follows the camera by cells with nothing copied (the cells new
//	to it cleared) and a sponge at its edge takes the waves before they
//	wrap. Once a frame before the main view's scenes, when its list has
//	water (UpdateWakes):
//	  - the bodies: the entities with physics (but static ones and
//	    particles) whose box crosses the ocean's level over water, in the
//	    window, as ellipses (WakeBodies.h: a box's across, its depth below
//	    the level; a character's its own size), at most XR_WAKE_BODIES
//	    of those that moved since the last step, nearest the camera first,
//	    none inside a larger one (a crew in its boat); one first seen
//	    makes no waves (it is only stored);
//	  - one to WAKE_MOST_STEPS steps over the frame's time (at most
//	    WAKE_LONGEST_STEP; a body moving at most WAKE_STEP_MOVE a step),
//	    each three kernels (Shaders/Wake.hlsl): the rows (the bodies'
//	    change of displacement, the sponge, the FFT along x), the columns
//	    (the FFT along y, each wave number turned and damped, the inverse
//	    FFT), the rows' inverse FFT back into the window;
//	  - the view: the slopes, the foam (its share of the surface white:
//	    at least the bodies' where they are, faster than WAKE_FOAM_SPEED,
//	    and the waves' breaking where steeper than WAKE_BREAK_SLOPE, but
//	    under a body; fading over WAKE_FOAM_LIFE and spreading, R32F in two
//	    textures taking turns), RGBA16F with XR_WAKE_MIPS levels;
//	  - with the ocean's grid (d3d12_WaterGeometry; OceanGrid.cpp) the
//	    heights its vertices take the wakes' at (Water.hlsl's
//	    WakeGridHeight): the window's height, R16F with XR_WAKE_MIPS levels
//	    (each the means of 2 x 2 texels of the one before).
//	The water's program (Water.hlsl's WaterWake) adds the view's slopes to
//	the ocean's waves (their lost variance to the reflection's blur) and
//	its foam to the ocean's, torn into clumps (WaterFoam.hlsli's
//	WakeClumps; SetWakeConstants), fading out over the sponge. A pause longer than WAKE_GAP clears the window; a frame after
//	frames without a step stores the bodies again without waves.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "D3D12Device.h"
#include "D3D12Upload.h"
#include "D3D12Descriptors.h"
#include "D3D12Compute.h"
#include "Scene.h"

// The engine's headers call min and max unqualified.
using std::max;
using std::min;
#include <IEntitySystem.h>
#include <physinterface.h>

namespace
{
  using namespace WakeBodies;

  static_assert(XR_WAKE_SIZE == 1024 && XR_WAKE_SIZE % 16 == 0 && (16 >> (XR_WAKE_MIPS - 1)) >= 2, "Shaders/Wake.hlsl's groups and levels");

  constexpr int TABLE = CD3D12Compute::COMPUTE_SRVS + CD3D12Compute::COMPUTE_UAVS;
  //! The rows, the columns, the rows' inverse; the view into each of the foam's two textures; the heights: a table each.
  constexpr int KERNELS = 6;
  constexpr int VIEW_TABLE = 3;
  constexpr int HEIGHT_TABLE = 5;
  constexpr DXGI_FORMAT STATE_FORMAT = DXGI_FORMAT_R32G32_FLOAT;
  constexpr DXGI_FORMAT FOAM_FORMAT = DXGI_FORMAT_R32_FLOAT;
  constexpr DXGI_FORMAT VIEW_FORMAT = DXGI_FORMAT_R16G16B16A16_FLOAT;
  constexpr DXGI_FORMAT HEIGHT_FORMAT = DXGI_FORMAT_R16_FLOAT;
  //! The longest pause between steps (s) the waves stay over (a longer one: a level loaded, a cut scene, a game saved).
  constexpr double WAKE_GAP = 0.5;
  constexpr D3D12_RESOURCE_STATES SRV_STATE = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  constexpr D3D12_RESOURCE_STATES UAV_STATE = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

  void Set4(float v[4], float x, float y, float z, float w)
  {
    v[0] = x;
    v[1] = y;
    v[2] = z;
    v[3] = w;
  }

  //! The ocean (one of the terrain's water shaders, as Water.cpp's IsOcean), not a water volume.
  bool IsOceanItem(const SRenderItem &it)
  {
    return it.eDrawClass == eDC_Water && _strnicmp(it.pShader->GetName(), "terrain", 7) == 0;
  }

  D3D12_SHADER_RESOURCE_VIEW_DESC TextureSRV(DXGI_FORMAT Format, int nMips)
  {
    D3D12_SHADER_RESOURCE_VIEW_DESC SRV = {};
    SRV.Format = Format;
    SRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    SRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    SRV.Texture2D.MipLevels = (UINT)nMips;
    return SRV;
  }

  D3D12_UNORDERED_ACCESS_VIEW_DESC TextureUAV(DXGI_FORMAT Format, int nMip)
  {
    D3D12_UNORDERED_ACCESS_VIEW_DESC UAV = {};
    UAV.Format = Format;
    UAV.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    UAV.Texture2D.MipSlice = (UINT)nMip;
    return UAV;
  }

  //! Whether an entity's physics can make waves: what moves (rigid bodies, vehicles, characters, ragdolls); not the static
  //! (they never move), particles (bullets, grenades in flight), ropes, cloth.
  bool MakesWaves(IPhysicalEntity *pPhysics, bool &bCharacter)
  {
    const pe_type eType = pPhysics ? pPhysics->GetType() : PE_NONE;
    bCharacter = eType == PE_LIVING;
    return eType == PE_RIGID || eType == PE_WHEELEDVEHICLE || eType == PE_LIVING || eType == PE_ARTICULATED;
  }
}

bool CD3D12Renderer::EnsureWakeTargets()
{
  SWakeState &w = m_Wakes;
  if (w.pView)
    return true;
  if (w.bFailed || !m_pDevice || !m_pUpload || !m_pDescriptors || !m_pCompute)
    return false;
  constexpr int N = XR_WAKE_SIZE;
  w.pState = m_pUpload->CreateComputeTarget(N, N, 1, STATE_FORMAT, L"XRenderD3D12 wakes");
  w.pRows = m_pUpload->CreateComputeTarget(N, N, 1, STATE_FORMAT, L"XRenderD3D12 wakes' rows");
  w.pColumns = m_pUpload->CreateComputeTarget(N, N, 1, STATE_FORMAT, L"XRenderD3D12 wakes' columns");
  w.pFoam[0] = m_pUpload->CreateComputeTarget(N, N, 1, FOAM_FORMAT, L"XRenderD3D12 wakes' foam 0");
  w.pFoam[1] = m_pUpload->CreateComputeTarget(N, N, 1, FOAM_FORMAT, L"XRenderD3D12 wakes' foam 1");
  w.pView = m_pUpload->CreateComputeTarget(N, N, XR_WAKE_MIPS, VIEW_FORMAT, L"XRenderD3D12 wakes' view");
  w.pHeights = m_pUpload->CreateComputeTarget(N, N, XR_WAKE_MIPS, HEIGHT_FORMAT, L"XRenderD3D12 wakes' heights");
  w.nTables = m_pDescriptors->AllocRange(KERNELS * TABLE);
  const D3D12_SHADER_RESOURCE_VIEW_DESC ViewSRV = TextureSRV(VIEW_FORMAT, XR_WAKE_MIPS), HeightSRV = TextureSRV(HEIGHT_FORMAT, XR_WAKE_MIPS);
  if (w.pView)
    w.pView->m_nSRV = m_pDescriptors->AllocSRV(w.pView->m_pResource, &ViewSRV);
  if (w.pHeights)
    w.pHeights->m_nSRV = m_pDescriptors->AllocSRV(w.pHeights->m_pResource, &HeightSRV);
  if (!w.pState || !w.pRows || !w.pColumns || !w.pFoam[0] || !w.pFoam[1] || !w.pView || !w.pHeights || w.nTables < 0 || w.pView->m_nSRV < 0 ||
      w.pHeights->m_nSRV < 0)
  {
    iLog->LogError("XRenderD3D12: no wakes (their targets)");
    ReleaseWakes();
    w.bFailed = true;
    return false;
  }

  // The tables (Shaders/Wake.hlsl's bindings): 0 the rows (the window into the rows), 1 the columns (the rows into the
  // columns), 2 the rows' inverse (the columns into the window); VIEW_TABLE + f the view with the foam into pFoam[f] from
  // the other; HEIGHT_TABLE the heights from the window. The views the kernels do not use null.
  const D3D12_SHADER_RESOURCE_VIEW_DESC StateSRV = TextureSRV(STATE_FORMAT, 1), FoamSRV = TextureSRV(FOAM_FORMAT, 1);
  for (int k = 0; k < HEIGHT_TABLE; k++)
  {
    const int nTable = w.nTables + k * TABLE;
    const bool bView = k >= VIEW_TABLE;
    const int f = bView ? k - VIEW_TABLE : 0;
    CD3D12Texture *const pIns[3] = { w.pState, w.pRows, w.pColumns };
    for (int i = 0; i < CD3D12Compute::COMPUTE_SRVS; i++)
    {
      ID3D12Resource *pIn = nullptr;
      const D3D12_SHADER_RESOURCE_VIEW_DESC *pSRV = &StateSRV;
      if (i == 0)
        pIn = (bView ? w.pState : pIns[k])->m_pResource;
      else if (bView && i == 1)
        pIn = w.pFoam[1 - f]->m_pResource, pSRV = &FoamSRV;
      m_pDescriptors->WriteSRV(nTable + i, pIn, pSRV);
    }
    CD3D12Texture *const pOuts[3] = { w.pRows, w.pColumns, w.pState };
    for (int i = 0; i < CD3D12Compute::COMPUTE_UAVS; i++)
    {
      D3D12_UNORDERED_ACCESS_VIEW_DESC UAV = TextureUAV(STATE_FORMAT, 0);
      ID3D12Resource *pOut = nullptr;
      if (!bView)
        pOut = i == 0 ? pOuts[k]->m_pResource : nullptr;
      else if (i == 0)
      {
        UAV = TextureUAV(FOAM_FORMAT, 0);
        pOut = w.pFoam[f]->m_pResource;
      }
      else
      {
        UAV = TextureUAV(VIEW_FORMAT, i - 1);
        pOut = w.pView->m_pResource;
      }
      m_pDescriptors->WriteUAV(nTable + CD3D12Compute::COMPUTE_SRVS + i, pOut, &UAV);
    }
  }
  {
    const int nTable = w.nTables + HEIGHT_TABLE * TABLE;
    for (int i = 0; i < CD3D12Compute::COMPUTE_SRVS; i++)
      m_pDescriptors->WriteSRV(nTable + i, i == 0 ? w.pState->m_pResource : nullptr, &StateSRV);
    for (int i = 0; i < CD3D12Compute::COMPUTE_UAVS; i++)
    {
      const D3D12_UNORDERED_ACCESS_VIEW_DESC UAV = TextureUAV(HEIGHT_FORMAT, std::min(i, XR_WAKE_MIPS - 1));
      m_pDescriptors->WriteUAV(nTable + CD3D12Compute::COMPUTE_SRVS + i, i < XR_WAKE_MIPS ? w.pHeights->m_pResource : nullptr, &UAV);
    }
  }
  // Made in UNORDERED_ACCESS: the window, the rows and the columns rest there; the foam's textures where the view's
  // kernel reads them, the view where the water's program does, the heights where its vertex program does.
  for (CD3D12Texture *pTex : { w.pFoam[0], w.pFoam[1] })
    m_pDevice->Transition(pTex->m_pResource, UAV_STATE, SRV_STATE);
  m_pDevice->Transition(w.pView->m_pResource, UAV_STATE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  m_pDevice->Transition(w.pHeights->m_pResource, UAV_STATE, SRV_STATE);
  w.nHeightsFrame = -1;
  w.nFrame = -1;
  w.Seen.clear();
  if (iLog)
    iLog->Log("XRenderD3D12: the wakes: a window of %d x %d cells of %g m", N, N, (double)XR_WAKE_CELL);
  return true;
}

bool CD3D12Renderer::GatherWaterBodies(std::vector<std::pair<int, WakeBodies::SBody>> &Now) const
{
  Now.clear();
  I3DEngine *p3D = iSystem ? iSystem->GetI3DEngine() : nullptr;
  IEntitySystem *pEntities = iSystem ? iSystem->GetIEntitySystem() : nullptr;
  IEntityIt *pIt = pEntities ? pEntities->GetEntityIterator() : nullptr;
  if (!p3D || !pIt)
  {
    if (pIt)
      pIt->Release();
    return false;
  }
  const float fWater = p3D->GetWaterLevel();
  const Vec3 vCamera = m_Camera.GetPos();
  const float fReach = WAKE_HALF_WINDOW;
  while (!pIt->IsEnd())
  {
    IEntity *pEntity = pIt->Next();
    if (!pEntity || pEntity->IsHidden() || pEntity->IsGarbage())
      continue;
    bool bCharacter = false;
    if (!MakesWaves(pEntity->GetPhysics(), bCharacter))
      continue;
    Vec3 vMin, vMax;
    pEntity->GetBBox(vMin, vMax);
    if (!(vMin.z < fWater && vMax.z > fWater))
      continue;
    const Vec3 vPos = pEntity->GetPos();
    if (fabsf(vPos.x - vCamera.x) > fReach || fabsf(vPos.y - vCamera.y) > fReach || p3D->GetTerrainElevation(vPos.x, vPos.y) >= fWater)
      continue;
    Vec3 vLocalMin, vLocalMax;
    pEntity->GetLocalBBox(vLocalMin, vLocalMax);
    const float pMin[3] = { vLocalMin.x, vLocalMin.y, vLocalMin.z }, pMax[3] = { vLocalMax.x, vLocalMax.y, vLocalMax.z };
    const float pPos[3] = { vPos.x, vPos.y, vPos.z };
    SBody Body;
    if (MakeBody(pMin, pMax, pPos, pEntity->GetAngles().z, vMin.z, vMax.z, fWater, bCharacter, Body))
      Now.emplace_back((int)pEntity->GetId(), Body);
  }
  pIt->Release();
  return true;
}

void CD3D12Renderer::GatherWakeBodies(std::vector<WakeBodies::SStepBody> &Bodies, float dt)
{
  SWakeState &w = m_Wakes;
  Bodies.clear();
  // The bodies crossing the surface now, each where the last step saw it (none: first seen, it is only stored).
  std::vector<std::pair<int, SBody>> Now;
  if (!GatherWaterBodies(Now))
  {
    w.Seen.clear();
    return;
  }
  std::vector<SMove> Moves;
  for (const auto &[nId, Body] : Now)
  {
    const int nBody = nId;
    const auto Before = std::find_if(w.Seen.begin(), w.Seen.end(), [nBody](const std::pair<int, SBody> &s) { return s.first == nBody; });
    if (Before != w.Seen.end())
      Moves.push_back({ nId, Before->second, Body });
  }
  w.Seen = std::move(Now);
  const Vec3 vCamera = m_Camera.GetPos();
  for (const SMove &m : Choose(std::move(Moves), vCamera.x, vCamera.y, XR_WAKE_BODIES))
  {
    SStepBody b;
    b.Before = m.Before;
    b.After = m.After;
    b.fSpeed = dt > 0 ? std::hypot(m.After.fX - m.Before.fX, m.After.fY - m.Before.fY) / dt : 0.0f;
    Bodies.push_back(b);
  }
}

void CD3D12Renderer::UpdateWakes(std::span<const SRenderItem> Items)
{
  if (!CV_d3d12_WaterWakes)
  {
    if (m_Wakes.pView || m_Wakes.bFailed)
      ReleaseWakes();
    return;
  }
  SWakeState &w = m_Wakes;
  if (w.nFrame == m_nFrames || !std::any_of(Items.begin(), Items.end(), IsOceanItem))
    return;
  if (!EnsureFrame() || !EnsureWakeTargets())
    return;

  // The frame's time since the last step: none (the game paused) leaves the waves as they are; a long pause clears them.
  const double fTime = iTimer ? (double)iTimer->GetCurrTime() : 0.0;
  const bool bFirst = w.nFrame < 0 || fTime - w.fTime > WAKE_GAP || fTime < w.fTime;
  const float dt = bFirst ? 0.0f : (float)std::min(fTime - w.fTime, (double)WAKE_LONGEST_STEP);
  if (!bFirst && dt <= 0)
    return;
  // After frames without a step (no water seen) the bodies only stored again.
  if (bFirst || w.nFrame != m_nFrames - 1)
    w.Seen.clear();
  std::vector<SStepBody> &Bodies = w.Bodies;
  GatherWakeBodies(Bodies, dt);

  // The calm: nothing stirring the window for WAKE_QUIET s, no steps (the bodies still stored); the first stir clears it.
  // A cleared window no one has stirred yet is calm (a level where nothing moves through the water costs nothing).
  if (Stirred(Bodies))
    w.fStirred = fTime;
  else if (bFirst)
    w.fStirred = -1e30;
  w.fShare = CalmShare(fTime - w.fStirred);
  if (w.fShare <= 0)
  {
    w.bCalm = true;
    w.fTime = fTime;
    w.nFrame = m_nFrames;
    return;
  }
  const bool bClear = bFirst || w.bCalm;
  w.bCalm = false;

  // The window around the camera's cell; a cleared one's around cells nowhere near it (all of it new).
  const Vec3 vCamera = m_Camera.GetPos();
  const int vCell[2] = { WindowCell(vCamera.x), WindowCell(vCamera.y) };
  int vBefore[2] = { w.nCell[0], w.nCell[1] };
  if (bClear)
    vBefore[0] = vCell[0] + XR_WAKE_SIZE, vBefore[1] = vCell[1] + XR_WAKE_SIZE;
  const bool bMoved = vBefore[0] != vCell[0] || vBefore[1] != vCell[1];

  // The steps: each body at the same share of its move.
  std::vector<SMove> Moves(Bodies.size());
  for (size_t i = 0; i < Bodies.size(); i++)
    Moves[i] = { (int)i, Bodies[i].Before, Bodies[i].After };
  const int nSteps = bFirst ? 1 : Steps(Moves);
  const float fStep = dt / (float)nSteps;
  std::vector<SStepBody> Step(Bodies.size());

  GpuSection("wakes");
  ID3D12GraphicsCommandList *pList = m_pDevice->GetCmdList();
  ID3D12Resource *pState = w.pState->m_pResource, *pRows = w.pRows->m_pResource, *pColumns = w.pColumns->m_pResource;
  const auto Table = [&](int k, bool bUAVs) { return w.nTables + k * TABLE + (bUAVs ? CD3D12Compute::COMPUTE_SRVS : 0); };
  constexpr UINT N = XR_WAKE_SIZE;
  m_pDevice->EndOffscreen();
  for (int s = 0; s < nSteps; s++)
  {
    for (size_t i = 0; i < Bodies.size(); i++)
    {
      Step[i].Before = Between(Bodies[i].Before, Bodies[i].After, (float)s / nSteps);
      Step[i].After = Between(Bodies[i].Before, Bodies[i].After, (float)(s + 1) / nSteps);
      Step[i].fSpeed = Bodies[i].fSpeed;
    }
    SWakeConstants wc;
    const int *pBefore = s == 0 ? vBefore : vCell;
    StepConstants(wc, vCell, pBefore, s == 0 && bMoved, fStep, false, Step.data(), (int)Step.size());
    const SD3D12FrameAlloc CB = m_pUpload->AllocFrame(sizeof(wc), 256);
    memcpy(CB.pCPU, &wc, sizeof(wc));
    m_pDevice->Transition(pState, UAV_STATE, SRV_STATE);
    m_pCompute->Dispatch(pList, eD3D12K_WakeRows, CB.nGPU, Table(0, false), Table(0, true), 1, N, 1);
    m_pDevice->Transition(pState, SRV_STATE, UAV_STATE);
    m_pDevice->Transition(pRows, UAV_STATE, SRV_STATE);
    m_pCompute->Dispatch(pList, eD3D12K_WakeColumns, CB.nGPU, Table(1, false), Table(1, true), 1, N, 1);
    m_pDevice->Transition(pRows, SRV_STATE, UAV_STATE);
    m_pDevice->Transition(pColumns, UAV_STATE, SRV_STATE);
    m_pCompute->Dispatch(pList, eD3D12K_WakeRowsInverse, CB.nGPU, Table(2, false), Table(2, true), 1, N, 1);
    m_pDevice->Transition(pColumns, SRV_STATE, UAV_STATE);
  }

  // The view: the slopes and the foam at the frame's end (the bodies where they are now), the foam into the other texture
  // than the frame before's, from it (none before in a cleared window); the cells new to the window since the last view.
  {
    const int f = bFirst ? 0 : 1 - w.nFoam;
    SWakeConstants wc;
    StepConstants(wc, vCell, vBefore, bMoved, dt, !bClear, Bodies.data(), (int)Bodies.size());
    const SD3D12FrameAlloc CB = m_pUpload->AllocFrame(sizeof(wc), 256);
    memcpy(CB.pCPU, &wc, sizeof(wc));
    ID3D12Resource *pFoam = w.pFoam[f]->m_pResource, *pView = w.pView->m_pResource;
    m_pDevice->Transition(pState, UAV_STATE, SRV_STATE);
    m_pDevice->Transition(pFoam, SRV_STATE, UAV_STATE);
    m_pDevice->Transition(pView, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, UAV_STATE);
    m_pCompute->Dispatch(pList, eD3D12K_WakeView, CB.nGPU, Table(VIEW_TABLE + f, false), Table(VIEW_TABLE + f, true), N / 16, N / 16, 1);
    // The heights for the ocean's grid (d3d12_WaterGeometry; OceanGrid.cpp), of the window as the view sees it.
    if (CV_d3d12_WaterGeometry > 0)
    {
      ID3D12Resource *pHeights = w.pHeights->m_pResource;
      m_pDevice->Transition(pHeights, SRV_STATE, UAV_STATE);
      m_pCompute->Dispatch(pList, eD3D12K_WakeHeight, CB.nGPU, Table(HEIGHT_TABLE, false), Table(HEIGHT_TABLE, true), N / 16, N / 16, 1);
      m_pDevice->Transition(pHeights, UAV_STATE, SRV_STATE);
      w.nHeightsFrame = m_nFrames;
    }
    m_pDevice->Transition(pState, SRV_STATE, UAV_STATE);
    m_pDevice->Transition(pFoam, UAV_STATE, SRV_STATE);
    m_pDevice->Transition(pView, UAV_STATE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    w.nFoam = f;
  }
  w.nCell[0] = vCell[0];
  w.nCell[1] = vCell[1];
  w.fTime = fTime;
  w.nFrame = m_nFrames;
  if (m_pFrameDump)
  {
    fprintf(m_pFrameDump.get(), "wakes: window (%d %d)%s, dt %.4f, %d steps, %d bodies of %d seen, shown %.2f\n", vCell[0], vCell[1],
            bClear ? " cleared" : bMoved ? " moved" : "", dt, nSteps, (int)Bodies.size(), (int)w.Seen.size(), w.fShare);
    for (const SStepBody &b : Bodies)
      fprintf(m_pFrameDump.get(), "  body (%.2f %.2f) -> (%.2f %.2f), axes %.2f %.2f, depth %.2f, %.2f m/s\n", b.Before.fX, b.Before.fY,
              b.After.fX, b.After.fY, b.After.fHalfA, b.After.fHalfB, b.After.fDepth, b.fSpeed);
  }
}

bool CD3D12Renderer::SetWakeConstants(SDrawConstants &dc, bool bFoam) const
{
  const SWakeState &w = m_Wakes;
  memset(dc.vLightPos[0], 0, sizeof(dc.vLightPos[0]));
  memset(dc.vLightPos[1], 0, sizeof(dc.vLightPos[1]));
  if (!CV_d3d12_WaterWakes || !w.pView || w.nFrame < 0 || w.bCalm)
    return false;
  // Water.hlsl's WaterWake: the window's centre point (m), its texel there (its cell modulo the window's size, over it);
  // 1 / the sponge's width (m: the wakes fade out over it), their foam (0: none), the view's SRV, the share shown (the
  // calm's fade).
  constexpr int N = XR_WAKE_SIZE;
  const uint32 nSRV = (uint32)w.pView->m_nSRV;
  Set4(dc.vLightPos[0], (float)(w.nCell[0] * XR_WAKE_CELL), (float)(w.nCell[1] * XR_WAKE_CELL), (float)(w.nCell[0] & (N - 1)) / N,
       (float)(w.nCell[1] & (N - 1)) / N);
  Set4(dc.vLightPos[1], 1 / (WAKE_SPONGE_CELLS * (float)XR_WAKE_CELL), bFoam ? 1.0f : 0.0f, 0, w.fShare);
  memcpy(&dc.vLightPos[1][2], &nSRV, sizeof(nSRV));
  return true;
}

void CD3D12Renderer::ReleaseWakes()
{
  SWakeState &w = m_Wakes;
  if (m_pUpload)
  {
    for (CD3D12Texture *pTex : { w.pState, w.pRows, w.pColumns, w.pFoam[0], w.pFoam[1], w.pView, w.pHeights })
      m_pUpload->ReleaseTexture(pTex);
  }
  if (m_pDescriptors && w.nTables >= 0)
    m_pDescriptors->FreeRange(w.nTables, KERNELS * TABLE);
  w = SWakeState();
}
