// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: OceanGrid.cpp
//	Description: The ocean's grid (d3d12_WaterGeometry): in place of the
//	game's flat disc of the ocean around the camera, a polar grid of the
//	same extent (OceanGrid.h: 12.5 cm apart near the camera, its cells
//	square out to XR_GRID_DISPLACED) whose vertices the water's vertex
//	program (Water.hlsl's OceanGridVertex) raises to the waves' height:
//	the FFT ocean's cascades and the wakes' (Wake.cpp's heights), each at
//	the mip level of the grid's spacing there, so that the crests stand
//	against the sky and the shore and the troughs hide what is behind
//	them. The height is limited by the depth under the level (the
//	terrain's heights, a texture made for each level), none under the
//	bodies in the water (the game floats them on the level), none around
//	a camera at the water (the game takes it as under the water by the
//	level alone) and far off. The disc's vertex alpha (the game's fade of
//	the water towards the shore and with the distance) is read from its
//	vertices each frame into a table (OceanGrid::ReadDisc) that the
//	vertex program interpolates, so that the shore stays as it was. Its
//	rings are spaced for the frame (OceanGrid::FrameRings: none finer on
//	the screen than XR_GRID_PIXELS far off). It writes the depth; with
//	d3d12_WaterDeferred it is drawn into the depth alone and the water
//	program shades the view where it is, each pixel once (DrawOceanGrid,
//	Water.hlsl's PSDeferred).
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "D3D12Device.h"
#include "D3D12Descriptors.h"
#include "D3D12Upload.h"
#include "Geometry.h"
#include "Scene.h"
#include "OceanGrid.h"

namespace
{
  using namespace WakeBodies;

  static_assert(XR_GRID_BODIES <= XR_MAX_DRAW_LIGHTS, "the bodies in vLightColor and vLightAtten");
  static_assert(sizeof(OceanGrid::SVertex) == sizeof(struct_VERTEX_FORMAT_P3F_COL4UB), "the game's disc's vertices");

  void Set4(float v[4], float x, float y, float z, float w)
  {
    v[0] = x;
    v[1] = y;
    v[2] = z;
    v[3] = w;
  }

  void SetUint(float &f, uint32 n)
  {
    memcpy(&f, &n, sizeof(n));
  }

  //! The ocean (one of the terrain's water shaders, as Water.cpp's IsOcean), not a water volume.
  bool IsOceanItem(const SRenderItem &it)
  {
    return it.eDrawClass == eDC_Water && _strnicmp(it.pShader->GetName(), "terrain", 7) == 0;
  }
}

bool CD3D12Renderer::EnsureOceanGrid()
{
  SOceanGridState &g = m_OceanGrid;
  if (g.pVB)
    return true;
  if (g.bFailed || !m_pUpload)
    return false;
  std::vector<OceanGrid::SVertex> V;
  std::vector<uint32_t> I;
  OceanGrid::Build(0xffffffffu, V, I);
  const uint32 nVertices = (uint32)V.size(), nIndices = (uint32)I.size();
  // After it a triangle over the view (its xy the view's x and y: Water.hlsl's VSDeferred).
  for (const auto &xy : { std::pair(-1.0f, -1.0f), std::pair(3.0f, -1.0f), std::pair(-1.0f, 3.0f) })
  {
    I.push_back((uint32_t)V.size());
    V.push_back({ xy.first, xy.second, 0, 0xffffffffu });
  }
  g.pVB = m_pUpload->CreateBuffer(V.data(), (uint32)(V.size() * sizeof(V[0])));
  g.pIB = m_pUpload->CreateBuffer(I.data(), (uint32)(I.size() * sizeof(I[0])));
  if (!g.pVB || !g.pIB)
  {
    iLog->LogError("XRenderD3D12: no ocean grid (its buffers)");
    ReleaseOceanGrid();
    g.bFailed = true;
    return false;
  }
  g.nVertices = nVertices;
  g.nIndices = nIndices;
  if (iLog)
    iLog->Log("XRenderD3D12: the ocean's grid: %u vertices, %u triangles (%d rings)", g.nVertices, g.nIndices / 3,
              (int)OceanGrid::Rings().size());
  return true;
}

bool CD3D12Renderer::EnsureGridTerrain()
{
  SOceanGridState &g = m_OceanGrid;
  if (g.pTerrain && !g.bTerrainStale)
    return true;
  if (!g.bTerrainStale || !m_pUpload || !m_pDevice)
    return false;
  g.bTerrainStale = false;
  if (g.pTerrain)
    m_pUpload->ReleaseTexture(g.pTerrain);
  g.pTerrain = nullptr;
  I3DEngine *p3D = iSystem ? iSystem->GetI3DEngine() : nullptr;
  const int nUnit = p3D ? p3D->GetHeightMapUnitSize() : 0, nSize = p3D ? p3D->GetTerrainSize() : 0;
  if (nUnit <= 0 || nSize <= 0 || nSize % nUnit)
    return false;
  // A texel a unit of the height map (the texel i at i units: its centre at x = i * unit), its heights as the engine
  // gives them there.
  const int n = nSize / nUnit;
  std::vector<float> H((size_t)n * n);
  for (int y = 0; y < n; y++)
    for (int x = 0; x < n; x++)
      H[(size_t)y * n + x] = p3D->GetTerrainElevation((float)(x * nUnit), (float)(y * nUnit));
  const SD3D12SubData Sub = { H.data(), (uint32)(n * sizeof(float)) };
  g.pTerrain = m_pUpload->CreateTexture(n, n, 1, DXGI_FORMAT_R32_FLOAT, false, &Sub);
  if (!g.pTerrain)
    return false;
  // Read by the vertex program: from the upload's state into both shader stages' (the upload batch runs before the frame;
  // its state kept by the device from here on, never written again).
  m_pDevice->TrackState(g.pTerrain->m_pResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  if (EnsureFrame())
    m_pDevice->Require(g.pTerrain->m_pResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  g.fTerrainScale = 1.0f / (float)(nUnit * n);
  g.fTerrainOffset = 0.5f / (float)n;
  if (iLog)
    iLog->Log("XRenderD3D12: the ocean grid's terrain: %d x %d heights, %d m apart", n, n, nUnit);
  return true;
}

std::optional<SGeomDraw> CD3D12Renderer::SetupOceanGrid(SDrawConstants &dc, const SRenderItem &it, CLeafBuffer *pLB)
{
  memset(dc.vLightSpec[0], 0, sizeof(dc.vLightSpec[0]));
  SOceanGridState &g = m_OceanGrid;
  if (!CV_d3d12_WaterGeometry)
  {
    if (g.pVB || g.bFailed || g.pTerrain)
      ReleaseOceanGrid();
    return {};
  }
  // The ocean of the main view (and the planar reflection's scene behind the water: none), with the FFT ocean's waves.
  if (m_bEnvCubePass || m_bReflectionPass || !IsOceanItem(it) || dc.vTexGenU[3] == 0 || !pLB)
    return {};
  const CLeafBuffer *pVC = pLB->GetVertexContainer();
  const CVertexBuffer *pVB = pVC && pVC->m_pSecVertBuffer && pVC->m_pSecVertBuffer->m_VS[VSF_GENERAL].m_VData ? pVC->m_pSecVertBuffer
                             : pVC ? pVC->m_pVertexBuffer : nullptr;
  if (!pVB || pVB->m_vertexformat != VERTEX_FORMAT_P3F_COL4UB || !pVB->m_VS[VSF_GENERAL].m_VData || pVB->m_NumVerts <= 0)
    return {};
  OceanGrid::SDisc Disc;
  if (!OceanGrid::ReadDisc((const OceanGrid::SVertex *)pVB->m_VS[VSF_GENERAL].m_VData, (size_t)pVB->m_NumVerts, Disc) || !EnsureOceanGrid())
    return {};
  const bool bTerrain = EnsureGridTerrain();

  // The frame's rings (OceanGrid::FrameRings): seen from the camera's height over the level, the pixels a radian across
  // the target's height (the projection's y scale over 2), the long cascade's slopes as drawn.
  const SOceanState &o = m_Ocean;
  const float fLevel = (float)Disc.fZ;
  const float fPixelsPerRadian = 0.5f * m_ProjMatrix.data[5] * (float)m_pDevice->GetMainHeight();
  OceanGrid::FrameRings(m_Camera.GetPos().z - fLevel, o.fSlopeRMS0 * dc.vParams0[0], fPixelsPerRadian, g.Rings);
  // The disc's table (OceanGrid.hlsli's GridDiscAlpha), the rings' (GridRing), the disc's alpha by radius for the
  // deferred ocean (GridDiscAlphaTable), in this frame's memory.
  const size_t nDisc = OceanGrid::DiscTableSize(Disc), nAlpha = nDisc + g.Rings.size();
  const SD3D12FrameAlloc Table = m_pUpload->AllocFrame((uint32)(sizeof(float) * (nAlpha + OceanGrid::AlphaTableSize())), 16);
  if (!Table.pCPU)
    return {};
  OceanGrid::DiscTable(Disc, (float *)Table.pCPU);
  std::copy(g.Rings.begin(), g.Rings.end(), (float *)Table.pCPU + nDisc);
  OceanGrid::AlphaTable(Disc, (float *)Table.pCPU + nAlpha);
  uint64 nTableOffset = 0;
  const int nTableSRV = m_pUpload->GetRawSRV(Table.nGPU, nTableOffset);
  if (nTableSRV < 0)
    return {};

  // The bodies in the water: the wakes' (all of them crossing the surface at their last step) or the frame's; not the
  // characters (a swimmer, one wading: the waves around them as the game's), the nearest XR_GRID_BODIES; their boxes'
  // ellipses (WakeBodies' half axes are WAKE_HALF_SHARE of the box's).
  const std::vector<std::pair<int, SBody>> *pBodies = &m_Wakes.Seen;
  if (!CV_d3d12_WaterWakes || !m_Wakes.pView)
  {
    if (g.nBodiesFrame != m_nFrames)
    {
      GatherWaterBodies(g.Bodies);
      g.nBodiesFrame = m_nFrames;
    }
    pBodies = &g.Bodies;
  }
  std::vector<SBody> Bodies;
  for (const auto &b : *pBodies)
    if (b.second.fHalfA >= WAKE_LEAST_HALF && b.second.fHalfB >= WAKE_LEAST_HALF)
      Bodies.push_back(b.second);
  const auto Distance = [&](const SBody &b) { return (b.fX - Disc.fX) * (b.fX - Disc.fX) + (b.fY - Disc.fY) * (b.fY - Disc.fY); };
  std::stable_sort(Bodies.begin(), Bodies.end(), [&](const SBody &a, const SBody &b) { return Distance(a) < Distance(b); });
  if (Bodies.size() > XR_GRID_BODIES)
    Bodies.resize(XR_GRID_BODIES);
  for (size_t n = 0; n < Bodies.size(); n++)
  {
    const SBody &b = Bodies[n];
    Set4(dc.vLightColor[n], b.fX, b.fY, b.fCos, b.fSin);
    Set4(dc.vLightAtten[n], b.fHalfA / WAKE_HALF_SHARE, b.fHalfB / WAKE_HALF_SHARE, 0, 0);
  }

  // Water.hlsl's OceanGridVertex: x 1 (the grid), the level, the disc's radius, the bodies; the grid's centre, the
  // terrain's heights, the camera's height over the level; each cascade's height (m) for its texels' height channel and
  // the alpha table's rows a doubling; the wakes' heights (as their view, SetWakeConstants's vLightPos), the terrain's uv
  // of the world; the table (its rings of the disc, its number of the grid's rings).
  Set4(dc.vLightSpec[0], 1, fLevel, Disc.fRadius[Disc.nRings - 1], (float)Bodies.size());
  Set4(dc.vLightSpec[1], (float)Disc.fX, (float)Disc.fY, 0, m_Camera.GetPos().z - fLevel);
  SetUint(dc.vLightSpec[1][2], bTerrain ? (uint32)g.pTerrain->m_nSRV : 0u);
  for (int c = 0; c < XR_OCEAN_CASCADES; c++)
    dc.vLightSpec[2][c] = o.fHeightWeight[c] > 0 ? dc.vParams0[0] / o.fHeightWeight[c] : 0.0f;
  dc.vLightSpec[2][3] = (float)OceanGrid::AlphaScale(Disc);
  const SWakeState &w = m_Wakes;
  const bool bWakes = dc.vLightPos[1][3] > 0 && w.pHeights && w.nHeightsFrame >= 0 && w.nHeightsFrame == w.nFrame;
  Set4(dc.vLightSpec[3], bWakes ? 1.0f : 0.0f, 0, bTerrain ? g.fTerrainScale : 0.0f, bTerrain ? g.fTerrainOffset : -1.0f);
  SetUint(dc.vLightSpec[3][1], bWakes ? (uint32)w.pHeights->m_nSRV : 0u);
  Set4(dc.vLightSpec[4], 0, 0, (float)Disc.nRings, (float)g.Rings.size());
  SetUint(dc.vLightSpec[4][0], (uint32)nTableSRV);
  SetUint(dc.vLightSpec[4][1], (uint32)nTableOffset);
  // The foam's floor (Water.hlsl's FoamFloorZ): within this of the terrain's heights, those heights.
  Set4(dc.vLightSpec[5], bTerrain ? std::max(CV_d3d12_WaterFoamTerrain, 0.0f) : 0.0f, 0, 0, 0);

  // Its vertices across the water from the disc's centre at the level.
  Matrix44 World;
  memset(World.data, 0, sizeof(World.data));
  World.data[0] = World.data[5] = World.data[10] = World.data[15] = 1;
  World.data[12] = (float)Disc.fX;
  World.data[13] = (float)Disc.fY;
  World.data[14] = fLevel;
  const Matrix44 WorldViewProj = World * m_CameraView * m_ProjMatrix;
  memcpy(dc.mWorld, World.data, sizeof(dc.mWorld));
  memcpy(dc.mWorldViewProj, WorldViewProj.data, sizeof(dc.mWorldViewProj));

  SGeomDraw Draw;
  Draw.VB = { g.pVB->m_nGPU, (g.nVertices + 3) * (uint32)sizeof(OceanGrid::SVertex), (UINT)sizeof(OceanGrid::SVertex) };
  Draw.nVertexFormat = VERTEX_FORMAT_P3F_COL4UB;
  Draw.IB = { g.pIB->m_nGPU, (g.nIndices + 3) * (uint32)sizeof(uint32), DXGI_FORMAT_R32_UINT };
  Draw.Topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
  Draw.nFirstIndex = 0;
  Draw.nNumIndices = g.nIndices;
  Draw.nNumVertices = g.nVertices;
  return Draw;
}

void CD3D12Renderer::DrawOceanGrid(const SPipelineKey &Key, const SDrawConstants &dc, const SGeomDraw &Geom)
{
  SOceanGridState &g = m_OceanGrid;
  const auto DrawGrid = [&](const SPipelineKey &GridKey)
  { DrawGeometry(GridKey, dc, Geom.Topology, Geom.VB, &Geom.IB, Geom.nNumIndices, Geom.nFirstIndex, Geom.nBaseVertex); };
  // Shaded as drawn: in the passes that only take the draws (the depth prepass, the rays' capture), with MSAA's targets
  // (their depth multisampled), off screen, in the debug views of its vertices' shares (Water.hlsl's 29 and 30), without
  // the scene's copy of this list.
  ID3D12Resource *pDepth = m_pDevice ? m_pDevice->GetMainDepth() : nullptr;
  const float fDebug = dc.vParams0[2];
  const bool bDeferred = CV_d3d12_WaterDeferred && pDepth && m_pDescriptors && !m_bDepthPrepass && !m_bRayCaptureWalk &&
                         !m_pDevice->IsOffscreen() && m_pDevice->GetBoundSamples() <= 1 && fDebug != 29 && fDebug != 30 &&
                         m_bSceneCopied && m_nSceneDepthSRV >= 0 && dc.nTex1 == (uint32)m_nSceneDepthSRV;
  if (!bDeferred)
  {
    DrawGrid(Key);
    return;
  }
  // The depth buffer's view (a new one for the upscaler's scene depth and back).
  const DXGI_FORMAT DepthFormat = m_pDevice->GetMainDepthSRVFormat();
  if (pDepth != g.pDepth.Get() || DepthFormat != g.DepthFormat)
  {
    if (g.nDepthSRV >= 0)
      m_pDescriptors->FreeSRV(g.nDepthSRV);
    g.pDepth = pDepth;
    g.DepthFormat = DepthFormat;
    D3D12_SHADER_RESOURCE_VIEW_DESC SRV = {};
    SRV.Format = DepthFormat;
    SRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    SRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    SRV.Texture2D.MipLevels = 1;
    g.nDepthSRV = m_pDescriptors->AllocSRV(pDepth, &SRV);
  }
  if (g.nDepthSRV < 0)
  {
    DrawGrid(Key);
    return;
  }
  // The waves' depth alone (the grid as drawn, without its pixel shader).
  SPipelineKey DepthKey = Key;
  DepthKey.bDepthOnly = 1;
  DrawGrid(DepthKey);
  // The water over the view (the triangle after the grid's), the depth buffer read only and read: Water.hlsl's
  // PSDeferred, vLightPos[2..4] the view's rays, vLightPos[2].w the depth buffer, vLightPos[3].w the triangle's depth:
  // the farthest water's (the disc's edge beyond the camera, 20 m further), the test GREATER, so that the pixels of what
  // is further off (the sky, the islands beyond the disc) are none of the program's (it ran over them to find no water:
  // Bunker's water 0.2 ms more with the rays).
  SDrawConstants Full = dc;
  float vCamera[4];
  GetRayView(vCamera, Full.vLightPos[2], Full.vLightPos[3], Full.vLightPos[4]);
  SetUint(Full.vLightPos[2][3], (uint32)g.nDepthSRV);
  const Vec3 vCam = m_Camera.GetPos();
  const double fFar = dc.vLightSpec[0][2] + std::hypot(vCam.x - dc.vLightSpec[1][0], vCam.y - dc.vLightSpec[1][1]) +
                      std::fabs(dc.vLightSpec[1][3]) + 20.0;
  Full.vLightPos[3][3] = (float)std::clamp(m_ProjMatrix.data[14] / fFar - m_ProjMatrix.data[10], 0.0, 1.0);
  SPipelineKey FullKey = Key;
  FullKey.bWaterDeferred = 1;
  FullKey.nState = GS_DEPTHFUNC_GREAT;
  FullKey.nCull = R_CULL_NONE;
  SetDepthRead(pDepth, true);
  DrawGeometry(FullKey, Full, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, Geom.VB, &Geom.IB, 3, g.nIndices, 0);
  SetDepthRead(pDepth, false);
}

void CD3D12Renderer::ReleaseOceanGrid()
{
  SOceanGridState &g = m_OceanGrid;
  if (m_pUpload)
  {
    m_pUpload->ReleaseBuffer(g.pVB);
    m_pUpload->ReleaseBuffer(g.pIB);
    m_pUpload->ReleaseTexture(g.pTerrain);
  }
  if (m_pDescriptors && g.nDepthSRV >= 0)
    m_pDescriptors->FreeSRV(g.nDepthSRV);
  g = SOceanGridState();
}
