// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: CacheTests.cpp
//	Description: Unit tests of the path tracer's radiance cache
//	(Shaders/PathCache.hlsli: its cells, its hash table, its samples) and
//	of its resolve, the renderer's own kernel CSCacheResolve of
//	Shaders/PathTrace.hlsl compiled as the renderer compiles it, run on
//	the GPU over the harness's cache buffer (u3, XR_PT_CACHE_BYTES).
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "GpuHarness.h"
#include "TestMath.h"

#include "Test_CacheCell.h"
#include "Test_CacheLookup.h"
#include "Test_CacheAdd.h"
#include "Test_CacheRead.h"
#include "Test_CachePoke.h"
#include "Test_PathCacheResolve.h"

#include <set>

using namespace XrTest;

namespace
{
  //! The cache's layout (PathCache.hlsli): the keys, then 4 words of samples an entry, then 2 of light (from this byte on).
  constexpr uint32 CACHE_LIGHT = XR_PT_CACHE_ENTRIES * 20;
  constexpr uint32 CACHE_MAX_AGE = 120;     //!< PATH_CACHE_MAX_AGE
  constexpr uint32 CACHE_MAX_SAMPLES = 255; //!< PATH_CACHE_MAX_SAMPLES

  //! The renderer's settings (the frame dump's "cells 0.25 m and 0.020 m per metre", 32 frames, a jitter of half a cell).
  const float s_vCache[4] = { 0.25f, 0.02f, 32, 0.5f };

  struct SCell
  {
    SVec3 vPos, vN;
  };

  struct SCellOut
  {
    uint32 nSlot, nKey;
    float fSize;
  };

  void SetCacheConstants(SDispatch &d, SVec3 vCamera)
  {
    memcpy(d.Test.vTest, s_vCache, sizeof(s_vCache));
    d.Test.vTest2[0] = vCamera.x;
    d.Test.vTest2[1] = vCamera.y;
    d.Test.vTest2[2] = vCamera.z;
  }

  void PushCells(SDispatch &d, const std::vector<SCell> &Cells)
  {
    d.Test.nTest[0] = (uint32)Cells.size();
    d.nGroups[0] = Groups64(Cells.size());
    for (const SCell &c : Cells)
    {
      Push(d.In, c.vPos, 0);
      Push(d.In, c.vN, 0);
    }
    d.nOut = Cells.size();
  }

  std::vector<SCellOut> CellsOf(const std::vector<SCell> &Cells, SVec3 vCamera)
  {
    SDispatch d;
    d.Shader = XR_BYTECODE(Test_CacheCell);
    SetCacheConstants(d, vCamera);
    PushCells(d, Cells);
    const std::vector<uint32> Out = Gpu().Run(d);
    std::vector<SCellOut> r(Cells.size());
    for (size_t i = 0; i < Cells.size(); i++)
      r[i] = { Out[i * 4], Out[i * 4 + 1], Float(Out[i * 4 + 2]) };
    return r;
  }

  //! PathCacheInsert (bFind false) or PathCacheFind of each cell: (found, entry) a cell.
  std::vector<std::pair<bool, uint32>> Lookup(const std::vector<SCell> &Cells, SVec3 vCamera, bool bFind)
  {
    SDispatch d;
    d.Shader = XR_BYTECODE(Test_CacheLookup);
    SetCacheConstants(d, vCamera);
    d.Test.nTest[1] = bFind ? 1 : 0;
    PushCells(d, Cells);
    const std::vector<uint32> Out = Gpu().Run(d);
    std::vector<std::pair<bool, uint32>> r(Cells.size());
    for (size_t i = 0; i < Cells.size(); i++)
      r[i] = { Out[i * 4] != 0, Out[i * 4 + 1] };
    return r;
  }

  void Add(uint32 nEntry, uint32 nSamples, float r, float g, float b)
  {
    SDispatch d;
    d.Shader = XR_BYTECODE(Test_CacheAdd);
    d.Test.nTest[0] = nSamples;
    d.Test.nTest[1] = nEntry;
    d.Test.vTest[0] = r;
    d.Test.vTest[1] = g;
    d.Test.vTest[2] = b;
    d.nGroups[0] = Groups64(nSamples);
    Gpu().Run(d);
  }

  struct SEntry
  {
    uint32 nKey, nCount, nLight[2], nSums[3];
    float vLight[3];
    uint32 Frames() const { return (nLight[1] >> 16) & 0xff; }
    uint32 Age() const { return nLight[1] >> 24; }
  };

  std::vector<SEntry> Read(const std::vector<uint32> &Entries)
  {
    SDispatch d;
    d.Shader = XR_BYTECODE(Test_CacheRead);
    d.Test.nTest[0] = (uint32)Entries.size();
    d.nGroups[0] = Groups64(Entries.size());
    for (uint32 e : Entries)
      PushWords(d.In, e, 0, 0, 0);
    d.nOut = Entries.size() * 3;
    const std::vector<uint32> Out = Gpu().Run(d);
    std::vector<SEntry> r(Entries.size());
    for (size_t i = 0; i < Entries.size(); i++)
    {
      const uint32 *p = &Out[i * 12];
      r[i] = { p[0], p[1], { p[2], p[3] }, { p[4], p[5], p[6] }, { Float(p[8]), Float(p[9]), Float(p[10]) } };
    }
    return r;
  }

  SEntry Read(uint32 nEntry) { return Read(std::vector<uint32>{ nEntry })[0]; }

  //! The cache's words set (byte offset, value pairs).
  void Poke(const std::vector<std::pair<uint32, uint32>> &Words)
  {
    SDispatch d;
    d.Shader = XR_BYTECODE(Test_CachePoke);
    d.Test.nTest[0] = (uint32)Words.size();
    d.nGroups[0] = Groups64(Words.size());
    for (const auto &w : Words)
      PushWords(d.In, w.first, w.second, 0, 0);
    Gpu().Run(d);
  }

  //! One frame of the renderer's CSCacheResolve over the whole cache (a thread an entry); vCache.z the frames the light holds at
  //! most, bRestart nCache.y (its light starts again).
  void Resolve(float fFrames, bool bRestart)
  {
    if (!Gpu().HasShaderModel65())
      XR_SKIP("the device has no Shader Model 6.5 (PathTrace.hlsl's kernels)");
    SPathTraceConstants pt = {};
    memcpy(pt.vCache, s_vCache, sizeof(s_vCache));
    pt.vCache[2] = fFrames;
    pt.nCache[1] = bRestart ? 1 : 0;
    SDispatch d;
    d.Shader = XR_BYTECODE(Test_PathCacheResolve);
    d.pPathTrace = &pt;
    d.nGroups[0] = XR_PT_CACHE_ENTRIES / 256;
    Gpu().Run(d);
  }

  //! Whether an entry's light is (r, g, b): it is stored as halves (f32tof16 rounds to the nearest or toward zero, by the
  //! device: 2^-10 of the value), from a division the GPU makes to about a float's precision.
  bool LightNear(const SEntry &e, float r, float g, float b)
  {
    const float Want[3] = { r, g, b };
    for (int c = 0; c < 3; c++)
    {
      if (std::fabs(e.vLight[c] - Want[c]) > 2e-3f * std::fabs(Want[c]) + 1e-3f)
      {
        XrTest::Fail(__FILE__, __LINE__, "light " + std::to_string(e.vLight[0]) + " " + std::to_string(e.vLight[1]) + " " +
                                             std::to_string(e.vLight[2]) + ", wanted " + std::to_string(r) + " " + std::to_string(g) + " " +
                                             std::to_string(b));
        return false;
      }
    }
    return true;
  }

  //! A cell's centre: the point (c + 0.5) * size of the grid of cells of that size.
  SVec3 Centre(int x, int y, int z, float fSize) { return { (x + 0.5f) * fSize, (y + 0.5f) * fSize, (z + 0.5f) * fSize }; }

  //! The six axes a cell's normal is taken by (PathCacheCellOf).
  const SVec3 s_Axes[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
}

//! PathCacheCellSize: vCache.x m up to vCache.x / vCache.y m from the camera (12.5 m), then twice as large each time the
//! distance doubles, 2^15 times at most. Why: the head comment's law; the cache's detail near the camera and its reach far
//! from it both come from it.
XR_TEST(PathCache_CellSize)
{
  const SVec3 vCamera = { 500, 500, 30 };
  const struct
  {
    float fDistance, fSize;
  } Cases[] = { { 0, 0.25f }, { 1, 0.25f }, { 12, 0.25f }, { 13, 0.25f }, { 26, 0.5f }, { 49, 0.5f }, { 60, 1 }, { 120, 2 }, { 300, 4 },
                { 1e6f, 0.25f * 32768 } };
  std::vector<SCell> Cells;
  for (const auto &c : Cases)
    Cells.push_back({ vCamera + SVec3{ c.fDistance, 0, 0 }, { 0, 0, 1 } });
  const std::vector<SCellOut> Out = CellsOf(Cells, vCamera);
  for (size_t i = 0; i < Out.size(); i++)
    CHECK_MSG(Out[i].fSize == Cases[i].fSize, "at " + std::to_string(Cases[i].fDistance) + " m: " + std::to_string(Out[i].fSize));
}

//! PathCacheCellOf: every point of a cell (within 0.45 of its size from the centre on each axis) and every normal leaning
//! along the same axis most give the cell's slot and key; the six neighbours across its faces have other keys; a key is never
//! 0 (0 marks a free entry). Cells on both sides of the world's origin (negative coordinates) included. Why: a cell that
//! changes under a sub-cell jitter of the point would make the cache's light flicker; neighbours that share a key would bleed
//! light through walls and corners.
XR_TEST(PathCache_CellStable)
{
  const SVec3 vCamera = { 3, -2, 1 };
  const float s = s_vCache[0];
  CRandom Rng(5);
  struct SBase
  {
    SVec3 vCentre;
    int nAxis;
  };
  std::vector<SBase> Bases;
  std::vector<SCell> Cells;
  const int JITTER = 6;  // jittered copies of each cell, then the 6 neighbours
  for (int i = 0; i < 1500; i++)
  {
    // Cells within 10 m of the camera: all of the smallest size.
    const SVec3 p = vCamera + Rng.Direction() * Rng.Range(0, 9.5f);
    const SBase b = { Centre((int)std::floor(p.x / s), (int)std::floor(p.y / s), (int)std::floor(p.z / s), s), (int)(Rng.Next() % 6) };
    Bases.push_back(b);
    Cells.push_back({ b.vCentre, s_Axes[b.nAxis] });
    for (int j = 0; j < JITTER; j++)
    {
      // A point of the cell, a normal still nearest its axis (the other components at most 0.6 of it).
      const SVec3 vJ = { Rng.Range(-0.45f, 0.45f) * s, Rng.Range(-0.45f, 0.45f) * s, Rng.Range(-0.45f, 0.45f) * s };
      SVec3 vN = { Rng.Range(-0.6f, 0.6f), Rng.Range(-0.6f, 0.6f), Rng.Range(-0.6f, 0.6f) };
      const SVec3 a = s_Axes[b.nAxis];
      vN = a.x ? SVec3{ a.x, vN.y, vN.z } : a.y ? SVec3{ vN.x, a.y, vN.z } : SVec3{ vN.x, vN.y, a.z };
      Cells.push_back({ b.vCentre + vJ, Normalize(vN) });
    }
    for (int k = 0; k < 6; k++)
      Cells.push_back({ b.vCentre + s_Axes[k] * s, s_Axes[b.nAxis] });
  }
  const std::vector<SCellOut> Out = CellsOf(Cells, vCamera);
  const size_t nPer = 1 + JITTER + 6;
  for (size_t i = 0; i < Bases.size(); i++)
  {
    const SCellOut &c = Out[i * nPer];
    CHECK(c.fSize == s);
    CHECK((c.nKey & 1) == 1);
    for (int j = 1; j <= JITTER; j++)
      CHECK_MSG(Out[i * nPer + j].nKey == c.nKey && Out[i * nPer + j].nSlot == c.nSlot, "cell " + std::to_string(i) + " jitter " + std::to_string(j));
    for (int k = 0; k < 6; k++)
      CHECK_MSG(Out[i * nPer + 1 + JITTER + k].nKey != c.nKey, "cell " + std::to_string(i) + " neighbour " + std::to_string(k));
  }
}

//! The axis of the normal is part of the cell: one point with the six axes' normals is six cells, a normal leaning mostly
//! along an axis is that axis's; the same world cell at another size (the camera farther) is another cell. Why: "no light
//! through a thin wall from its other side", and no light of a coarse cell taken for a fine one.
XR_TEST(PathCache_CellAxisAndLevel)
{
  const SVec3 vCamera = { 100, 100, 10 };
  const SVec3 p = Centre(401, 397, 41, 0.25f);  // 0.25 m cells, near the camera
  std::vector<SCell> Cells;
  for (const SVec3 &a : s_Axes)
    Cells.push_back({ p, a });
  Cells.push_back({ p, Normalize({ 0.8f, 0.5f, 0.3f }) });    // +x
  Cells.push_back({ p, Normalize({ 0.3f, -0.9f, 0.2f }) });   // -y
  Cells.push_back({ p, Normalize({ 0.1f, 0.2f, -0.97f }) });  // -z
  const std::vector<SCellOut> Out = CellsOf(Cells, vCamera);
  std::set<uint32> Keys;
  for (int k = 0; k < 6; k++)
    Keys.insert(Out[k].nKey);
  CHECK(Keys.size() == 6);
  CHECK(Out[6].nKey == Out[0].nKey);
  CHECK(Out[7].nKey == Out[3].nKey);
  CHECK(Out[8].nKey == Out[5].nKey);
  // A point 1 m cells (60 m away) hold, seen from near (0.25 m cells) and from far: two cells.
  const SVec3 q = Centre(100, 100, 10, 1);
  const SCellOut Near = CellsOf({ { q, { 0, 0, 1 } } }, q + SVec3{ 2, 0, 0 })[0];
  const SCellOut Far = CellsOf({ { q, { 0, 0, 1 } } }, q + SVec3{ 60, 0, 0 })[0];
  CHECK(Near.fSize == 0.25f && Far.fSize == 1);
  CHECK(Near.nKey != Far.nKey);
}

//! PathCacheInsert / PathCacheFind over the hash table (all threads at once, as CSCacheUpdate inserts): 4096 cells get 4096
//! entries, each found again at its entry, inserted again at the same one; cells never inserted are not found; 1024 threads
//! inserting one cell all get one entry and take one slot. Why: an insert that races (two entries for a cell) splits its
//! samples; a find that returns another cell's entry gives a surface the light of somewhere else.
XR_TEST(PathCache_InsertFind)
{
  Gpu().ClearCache();
  const SVec3 vCamera = { 0, 0, 0 };
  std::vector<SCell> Cells, Others;
  for (int i = 0; i < 4096; i++)
  {
    Cells.push_back({ Centre(i % 16, (i / 16) % 16, i / 256, 0.25f), { 0, 0, 1 } });
    Others.push_back({ Centre(i % 16, (i / 16) % 16, 20 + i / 256, 0.25f), { 0, 0, 1 } });
  }
  const auto Ins = Lookup(Cells, vCamera, false);
  const auto Found = Lookup(Cells, vCamera, true);
  const auto Again = Lookup(Cells, vCamera, false);
  const auto Missing = Lookup(Others, vCamera, true);
  std::set<uint32> Entries;
  for (size_t i = 0; i < Cells.size(); i++)
  {
    CHECK(Ins[i].first && Ins[i].second < XR_PT_CACHE_ENTRIES);
    CHECK(Found[i].first && Found[i].second == Ins[i].second);
    CHECK(Again[i].first && Again[i].second == Ins[i].second);
    CHECK(!Missing[i].first);
    Entries.insert(Ins[i].second);
  }
  CHECK(Entries.size() == Cells.size());

  // One cell from 1024 threads at once.
  const std::vector<SCell> Same(1024, SCell{ Centre(-7, 3, 5, 0.25f), { 0, 1, 0 } });
  const auto Racing = Lookup(Same, vCamera, false);
  for (const auto &r : Racing)
    CHECK(r.first && r.second == Racing[0].second);
  const SCellOut c = CellsOf({ Same[0] }, vCamera)[0];
  std::vector<uint32> Probes;
  for (uint32 i = 0; i < 8; i++)
    Probes.push_back((c.nSlot + i) & (XR_PT_CACHE_ENTRIES - 1));
  int nTaken = 0;
  for (const SEntry &e : Read(Probes))
    nTaken += e.nKey == c.nKey;
  CHECK(nTaken == 1);
}

//! PathCacheAdd: each sample adds round(clamp(light, 0, 1024) * 256) to its entry's sums and 1 to its count; past
//! PATH_CACHE_MAX_SAMPLES (255) samples a frame only the count grows. Why: the fixed point and its cap keep the sums from
//! wrapping round 2^32 when many paths end in one cell.
XR_TEST(PathCache_AddSamples)
{
  Gpu().ClearCache();
  Add(1000, 300, 0.5f, 1.25f, 2);
  Add(2000, 10, 2000, -1, 0.001f);
  const SEntry a = Read(1000), b = Read(2000);
  CHECK(a.nCount == 300);
  CHECK(a.nSums[0] == CACHE_MAX_SAMPLES * 128 && a.nSums[1] == CACHE_MAX_SAMPLES * 320 && a.nSums[2] == CACHE_MAX_SAMPLES * 512);
  CHECK(b.nCount == 10);
  CHECK(b.nSums[0] == 10 * 1024 * 256 && b.nSums[1] == 0 && b.nSums[2] == 0);
}

//! The renderer's CSCacheResolve (PathTrace.hlsl), frame by frame: the frame's samples become the entry's light (their mean,
//! blended in at 1 / frames, vCache.z frames at most) and are cleared; a frame without samples ages the entry; past
//! PATH_CACHE_MAX_AGE (120) frames without one the entry is free again (key and light 0, not found); nCache.y starts the
//! light again from the frame's samples alone. Why: this is what keeps the cache's light from lagging or sticking forever.
XR_TEST(PathCache_Resolve)
{
  Gpu().ClearCache();
  const SVec3 vCamera = { 0, 0, 0 };
  const SCell Cell = { Centre(2, 3, 4, 0.25f), { 0, 0, 1 } };
  const auto Ins = Lookup({ Cell }, vCamera, false);
  CHECK(Ins[0].first);
  const uint32 e = Ins[0].second;

  Add(e, 4, 1, 2, 3);
  Resolve(32, false);
  SEntry s = Read(e);
  CHECK(s.nKey != 0 && s.nCount == 0 && s.nSums[0] == 0 && s.nSums[1] == 0 && s.nSums[2] == 0);
  LightNear(s, 1, 2, 3);
  CHECK(s.Frames() == 1 && s.Age() == 0);

  Add(e, 2, 3, 2, 1);
  Resolve(32, false);
  s = Read(e);
  LightNear(s, 2, 2, 2);
  CHECK(s.Frames() == 2 && s.Age() == 0);

  Resolve(32, false);  // no samples: older, the same light
  s = Read(e);
  LightNear(s, 2, 2, 2);
  CHECK(s.Frames() == 2 && s.Age() == 1);

  // vCache.z 4: the frames stop at 4, the blend at 1 / 4.
  const float Want[] = { 2 + 4.0f / 3, 2 + 4.0f / 3 + (6 - 2 - 4.0f / 3) / 4 };
  Add(e, 1, 6, 6, 6);
  Resolve(4, false);
  s = Read(e);
  LightNear(s, Want[0], Want[0], Want[0]);
  CHECK(s.Frames() == 3);
  Add(e, 1, 6, 6, 6);
  Resolve(4, false);
  s = Read(e);
  LightNear(s, Want[1], Want[1], Want[1]);
  CHECK(s.Frames() == 4);
  Add(e, 1, 6, 6, 6);
  Resolve(4, false);
  s = Read(e);
  LightNear(s, Want[1] + (6 - Want[1]) / 4, Want[1] + (6 - Want[1]) / 4, Want[1] + (6 - Want[1]) / 4);
  CHECK(s.Frames() == 4);

  // Starting again: the frame's samples alone.
  Add(e, 3, 10, 10, 10);
  Resolve(32, true);
  s = Read(e);
  LightNear(s, 10, 10, 10);
  CHECK(s.Frames() == 1);

  // The age: at PATH_CACHE_MAX_AGE - 1 one frame more keeps it, the next frees it.
  Poke({ { CACHE_LIGHT + e * 8 + 4, (s.nLight[1] & 0xffffff) | ((CACHE_MAX_AGE - 1) << 24) } });
  Resolve(32, false);
  s = Read(e);
  CHECK(s.nKey != 0 && s.Age() == CACHE_MAX_AGE);
  CHECK(Lookup({ Cell }, vCamera, true)[0].first);
  Resolve(32, false);
  s = Read(e);
  CHECK(s.nKey == 0 && s.nLight[0] == 0 && s.nLight[1] == 0);
  CHECK(!Lookup({ Cell }, vCamera, true)[0].first);
}
