// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: BarrierTests.cpp
//	Description: The barrier tracker (D3D12Barriers.h) on the CPU: what
//	each request gives in the immediate and the batched mode, the merging
//	of chains and round trips, the UAV barriers left out, subresources,
//	the decay of buffers, the census, and a long random sequence of
//	requests checked against a model of the GPU's states (every
//	transition's before the state it has, no subresource twice in a call,
//	the same states at the end as the immediate mode's). The resources
//	are fake pointers: the tracker never calls them. On the GPU: a
//	buffer's state across two command lists (GpuHarness.h's
//	RunBarrierSteps; the ocean's, the wakes' and CMAA2's batches are in
//	their own tests).
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "GpuHarness.h"

#include "D3D12Barriers.h"

#include "Test_BarrierStep.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace Barriers;

namespace
{
  constexpr D3D12_RESOURCE_STATES UAV = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  constexpr D3D12_RESOURCE_STATES NPSR = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  constexpr D3D12_RESOURCE_STATES PSR = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  constexpr D3D12_RESOURCE_STATES RT = D3D12_RESOURCE_STATE_RENDER_TARGET;
  constexpr D3D12_RESOURCE_STATES SRC = D3D12_RESOURCE_STATE_COPY_SOURCE;
  constexpr D3D12_RESOURCE_STATES DST = D3D12_RESOURCE_STATE_COPY_DEST;
  constexpr D3D12_RESOURCE_STATES COMMON = D3D12_RESOURCE_STATE_COMMON;
  constexpr D3D12_RESOURCE_STATES DEPTH_READ = D3D12_RESOURCE_STATE_DEPTH_READ;

  ID3D12Resource *Res(int n) { return reinterpret_cast<ID3D12Resource *>((uintptr_t)0x10000 * (uintptr_t)n); }

  using SCall = std::vector<D3D12_RESOURCE_BARRIER>;

  //! A call the debug layer takes: no transition to the state it names as before, no subresource twice (nor the whole
  //! resource and one of its subresources).
  bool ValidCall(const SCall &c, std::string *pWhy = nullptr)
  {
    std::set<std::pair<ID3D12Resource *, UINT>> Seen;
    for (const D3D12_RESOURCE_BARRIER &b : c)
    {
      if (b.Type != D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)
        continue;
      const auto &t = b.Transition;
      if (t.StateBefore == t.StateAfter)
      {
        if (pWhy)
          *pWhy = "before == after";
        return false;
      }
      const bool bClash = Seen.count({ t.pResource, t.Subresource }) || (t.Subresource == ALL ? std::any_of(Seen.begin(), Seen.end(), [&](const auto &k) { return k.first == t.pResource; })
                                                                                              : Seen.count({ t.pResource, ALL }) != 0);
      if (bClash)
      {
        if (pWhy)
          *pWhy = "a subresource twice";
        return false;
      }
      Seen.insert({ t.pResource, t.Subresource });
    }
    return true;
  }

  //! Flushes the tracker, the calls it gave (each checked valid).
  std::vector<SCall> Flush(CBarrierTracker &t, bool bWork = true)
  {
    std::vector<SCall> Calls;
    t.Flush([&](UINT n, const D3D12_RESOURCE_BARRIER *p) { Calls.emplace_back(p, p + n); }, bWork);
    for (const SCall &c : Calls)
    {
      std::string Why;
      CHECK_MSG(ValidCall(c, &Why), Why);
    }
    return Calls;
  }

  bool IsTransition(const D3D12_RESOURCE_BARRIER &b, ID3D12Resource *p, D3D12_RESOURCE_STATES Before, D3D12_RESOURCE_STATES After,
                    UINT nSub = ALL)
  {
    return b.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION && b.Transition.pResource == p && b.Transition.StateBefore == Before &&
           b.Transition.StateAfter == After && b.Transition.Subresource == nSub;
  }
  bool IsUav(const D3D12_RESOURCE_BARRIER &b, ID3D12Resource *p) { return b.Type == D3D12_RESOURCE_BARRIER_TYPE_UAV && b.UAV.pResource == p; }

  size_t CountKind(const std::vector<SProblem> &v, EProblem e)
  {
    return (size_t)std::count_if(v.begin(), v.end(), [&](const SProblem &p) { return p.eKind == e; });
  }

  CBarrierTracker Batched()
  {
    CBarrierTracker t;
    t.SetMode(MODE_BATCHED);
    return t;
  }
}

// The immediate mode: each request as it came, in a call of its own, in its order (the renderer's barriers before).
XR_TEST(Barrier_ImmediateAsItCame)
{
  CBarrierTracker t;
  CHECK(t.GetMode() == MODE_IMMEDIATE);
  t.Transition(Res(1), UAV, NPSR);
  t.Uav(Res(1));
  t.UavAll();
  t.Transition(Res(1), NPSR, UAV);
  t.Transition(Res(2), PSR, RT, 3);
  t.Uav(nullptr);  // nothing, as the device's UavBarrier(nullptr)
  const std::vector<SCall> c = Flush(t);
  CHECK(c.size() == 5);
  if (c.size() == 5)
  {
    CHECK(c[0].size() == 1 && IsTransition(c[0][0], Res(1), UAV, NPSR));
    CHECK(c[1].size() == 1 && IsUav(c[1][0], Res(1)));
    CHECK(c[2].size() == 1 && IsUav(c[2][0], nullptr));
    CHECK(c[3].size() == 1 && IsTransition(c[3][0], Res(1), NPSR, UAV));
    CHECK(c[4].size() == 1 && IsTransition(c[4][0], Res(2), PSR, RT, 3));
  }
  CHECK(t.Stats().nCalls == 5 && t.Stats().nTransitions == 3 && t.Stats().nUav == 1 && t.Stats().nUavAll == 1);
  CHECK(!t.HasPending());
  // An array of barriers: one call, as it is.
  D3D12_RESOURCE_BARRIER Two[2] = {};
  Two[0].Type = Two[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  Two[1].UAV.pResource = Res(3);
  t.Barriers(Two, 2);
  const std::vector<SCall> c2 = Flush(t);
  CHECK(c2.size() == 1 && c2[0].size() == 2);
}

// A -> B -> C given as A -> C; transitions of different resources share one call.
XR_TEST(Barrier_MergeChain)
{
  CBarrierTracker t = Batched();
  t.Transition(Res(1), PSR, RT);
  t.Transition(Res(1), RT, UAV);
  t.Transition(Res(2), UAV, NPSR);
  const std::vector<SCall> c = Flush(t);
  CHECK(c.size() == 1);
  if (c.size() == 1)
  {
    CHECK(c[0].size() == 2);
    CHECK(IsTransition(c[0][0], Res(1), PSR, UAV));
    CHECK(IsTransition(c[0][1], Res(2), UAV, NPSR));
  }
  CHECK(t.Stats().nRequests == 3 && t.Stats().nTransitions == 2 && t.Stats().nCalls == 1);
}

// A pass's exit barriers and the next pass's entry barriers in one call (the main merge: the wakes' ping-pong).
XR_TEST(Barrier_ExitAndEntry)
{
  CBarrierTracker t = Batched();
  t.Transition(Res(1), UAV, NPSR);
  CHECK(Flush(t).size() == 1);  // the dispatch reading it
  t.Transition(Res(1), NPSR, UAV);  // its exit
  t.Transition(Res(2), UAV, NPSR);  // the next one's entry
  const std::vector<SCall> c = Flush(t);
  CHECK(c.size() == 1 && c[0].size() == 2);
  CHECK(t.Stats().nCalls == 2 && t.Stats().nDrains == 1);
}

// Round trips with no work between: back to a read state is nothing, back to UNORDERED_ACCESS a UAV barrier, back to
// another write state two calls (two copies into a resource are ordered by its transitions).
XR_TEST(Barrier_RoundTrips)
{
  CBarrierTracker t = Batched();
  t.Transition(Res(1), PSR, UAV);
  t.Transition(Res(1), UAV, PSR);
  CHECK(Flush(t).empty());

  t.Transition(Res(2), UAV, NPSR);
  t.Transition(Res(2), NPSR, UAV);
  std::vector<SCall> c = Flush(t);
  CHECK(c.size() == 1 && c[0].size() == 1 && IsUav(c[0][0], Res(2)));

  t.Transition(Res(3), DST, SRC);
  t.Transition(Res(3), SRC, DST);
  c = Flush(t);
  CHECK(c.size() == 2);
  if (c.size() == 2)
  {
    CHECK(c[0].size() == 1 && IsTransition(c[0][0], Res(3), DST, SRC));
    CHECK(c[1].size() == 1 && IsTransition(c[1][0], Res(3), SRC, DST));
  }

  // Registered: back to UNORDERED_ACCESS needs its barrier only after work.
  t.Register(Res(4), UAV, 1);
  t.Require(Res(4), NPSR);
  t.Require(Res(4), UAV);
  CHECK(Flush(t, false).empty());  // no work since it was made
  t.Require(Res(4), NPSR);
  t.Require(Res(4), UAV);
  CHECK(Flush(t).empty());  // still none: the flush's work comes after it
  t.Require(Res(4), NPSR);
  t.Require(Res(4), UAV);
  c = Flush(t);
  CHECK(c.size() == 1 && c[0].size() == 1 && IsUav(c[0][0], Res(4)));
}

// The water's mip chain: the whole target, then one mip of it; never the whole and a mip in one call.
XR_TEST(Barrier_WaterMips)
{
  CBarrierTracker t = Batched();
  ID3D12Resource *w = Res(1);
  t.Transition(w, PSR, RT);
  CHECK(Flush(t).size() == 1);
  t.Transition(w, RT, PSR);      // EndOffscreen
  t.Transition(w, PSR, RT, 1);   // the next mip bound
  std::vector<SCall> c = Flush(t);
  CHECK(c.size() == 2);
  if (c.size() == 2)
  {
    CHECK(IsTransition(c[0][0], w, RT, PSR));
    CHECK(IsTransition(c[1][0], w, PSR, RT, 1));
  }
  t.Transition(w, RT, PSR, 1);
  t.Transition(w, PSR, RT, 2);
  c = Flush(t);
  CHECK(c.size() == 1 && c[0].size() == 2);
  t.Transition(w, RT, PSR, 2);
  t.Transition(w, PSR, RT, 2);  // a round trip back to a write state of one mip: two calls
  CHECK(Flush(t).size() == 2);
}

// The UAV barriers left out: next to a transition of the whole resource into or out of UNORDERED_ACCESS, twice the same,
// under one of all resources, and of a registered resource with no work since its last barrier.
XR_TEST(Barrier_UavLeftOut)
{
  CBarrierTracker t = Batched();
  t.Transition(Res(1), NPSR, UAV);
  t.Uav(Res(1));
  std::vector<SCall> c = Flush(t);
  CHECK(c.size() == 1 && c[0].size() == 1 && c[0][0].Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION);

  t.Uav(Res(1));
  t.Transition(Res(1), UAV, NPSR);
  c = Flush(t);
  CHECK(c.size() == 1 && c[0].size() == 1 && c[0][0].Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION);

  t.Uav(Res(2));
  t.Uav(Res(2));
  c = Flush(t);
  CHECK(c.size() == 1 && c[0].size() == 1 && IsUav(c[0][0], Res(2)));

  t.Uav(Res(3));
  t.UavAll();
  t.Uav(Res(4));
  c = Flush(t);
  CHECK(c.size() == 1 && c[0].size() == 1 && IsUav(c[0][0], nullptr));

  // One of all resources twice with no work between: the second left out; after work given again.
  t.UavAll();
  c = Flush(t, false);
  CHECK(c.size() == 1);
  t.UavAll();
  t.Uav(Res(5));  // nothing since all were waited for
  CHECK(Flush(t, false).empty());
  t.UavAll();
  CHECK(Flush(t).empty());
  t.UavAll();
  CHECK(Flush(t).size() == 1);

  // A registered resource: none until work was recorded after its last barrier.
  t.Register(Res(6), UAV, 1);
  t.Uav(Res(6));
  CHECK(Flush(t).empty());
  t.Uav(Res(6));
  c = Flush(t, false);
  CHECK(c.size() == 1 && IsUav(c[0][0], Res(6)));
  t.Uav(Res(6));
  CHECK(Flush(t).empty());  // no work since that one
  // A transition of only one of its subresources does not wait for the others' writes.
  t.Register(Res(7), UAV, 3);
  Flush(t);
  t.Require(Res(7), NPSR, 1);
  t.Uav(Res(7));
  c = Flush(t);
  CHECK(c.size() == 1 && c[0].size() == 2);
  t.Uav(nullptr);
  CHECK(Flush(t).empty());
}

// A transition whose before is not the queued state: given as it came, after the queued one (as the renderer did).
XR_TEST(Barrier_BeforeMismatch)
{
  CBarrierTracker t = Batched();
  t.Transition(Res(1), PSR, RT);
  t.Transition(Res(1), NPSR, UAV);
  const std::vector<SCall> c = Flush(t);
  CHECK(c.size() == 2);
  if (c.size() == 2)
  {
    CHECK(IsTransition(c[0][0], Res(1), PSR, RT));
    CHECK(IsTransition(c[1][0], Res(1), NPSR, UAV));
  }
  const std::vector<SProblem> p = t.TakeProblems();
  CHECK(CountKind(p, PROBLEM_BEFORE) == 1);
  CHECK(t.TakeProblems().empty());
  // Before == after: left out in the batched mode.
  t.Transition(Res(2), PSR, PSR);
  CHECK(Flush(t).empty());
  CHECK(CountKind(t.TakeProblems(), PROBLEM_SAME_STATE) == 1);
  t.Transition(Res(3), PSR, UAV | NPSR);
  Flush(t);
  CHECK(CountKind(t.TakeProblems(), PROBLEM_MIXED_WRITE) == 1);
}

// Many barriers in one batch: none lost (the TAA's CBarriers kept four).
XR_TEST(Barrier_Many)
{
  CBarrierTracker t = Batched();
  for (int i = 1; i <= 100; i++)
    t.Transition(Res(i), UAV, NPSR);
  const std::vector<SCall> c = Flush(t);
  CHECK(c.size() == 1 && c[0].size() == 100);
  bool bAll = c.size() == 1;
  for (int i = 1; bAll && i <= 100; i++)
    bAll = IsTransition(c[0][i - 1], Res(i), UAV, NPSR);
  CHECK(bAll);
}

// NRD's pattern: the states a pass wants queued, then the pass left (an error): the queue is given at the list's end
// all the same, so the states kept are the states the resources have.
XR_TEST(Barrier_LeftPass)
{
  CBarrierTracker t = Batched();
  t.Register(Res(1), UAV, 1);
  t.Register(Res(2), NPSR, 1);
  t.UavAll();
  t.Require(Res(1), NPSR);
  t.Require(Res(2), UAV);
  // no dispatch: the pass fails; the list ends
  const std::vector<SCall> c = Flush(t, false);
  CHECK(c.size() == 1 && c[0].size() == 3);
  CHECK(t.GetState(Res(1)) == NPSR && t.GetState(Res(2)) == UAV);
  // the restore after the passes
  t.Require(Res(1), UAV);
  t.Require(Res(2), UAV);
  const std::vector<SCall> c2 = Flush(t, false);
  CHECK(c2.size() == 1 && c2[0].size() == 1 && IsTransition(c2[0][0], Res(1), NPSR, UAV));
  CHECK(t.TakeProblems().empty());
}

// Why: NRD's host (NrdHost.cpp, before step 318) noted the state a pass wanted for each texture and gave the pass's
// barriers only when the pass got its descriptors and constants; a pass given up on half way (no table, no frame
// memory) dropped them, and the next frame's transitions named states the textures never had. Queued as they are
// noted (the device's TransitionOn), the barriers are given at the list's end all the same: the census sees the next
// frame's befores right. The old way is shown failing under the census too.
XR_TEST(Barrier_LeftPassExplicit)
{
  for (int nMode = 0; nMode < 2; nMode++)
  {
    // The barriers queued as the states are noted; the pass given up on.
    CBarrierTracker t;
    t.SetMode((EMode)nMode);
    t.SetCensus(true);
    D3D12_RESOURCE_STATES State[2] = { UAV, NPSR };
    const D3D12_RESOURCE_STATES Want[2] = { NPSR, UAV };
    t.UavAll();
    for (int i = 0; i < 2; i++)
    {
      t.Transition(Res(i + 1), State[i], Want[i]);
      State[i] = Want[i];
    }
    Flush(t, false);  // the list's end
    // The next frame (the same list here: the census keeps textures' states over lists).
    for (int i = 0; i < 2; i++)
      t.Transition(Res(i + 1), State[i], D3D12_RESOURCE_STATE_COPY_SOURCE);
    Flush(t);
    CHECK(t.TakeProblems().empty());

    // The old way: the states noted, the pass's array never given.
    CBarrierTracker u;
    u.SetMode((EMode)nMode);
    u.SetCensus(true);
    u.Transition(Res(1), PSR, UAV);
    Flush(u);
    u.Transition(Res(1), NPSR, SRC);  // NPSR noted for the pass, its barrier (UAV to NPSR) dropped with it
    Flush(u);
    CHECK(CountKind(u.TakeProblems(), PROBLEM_BEFORE) == 1);
  }
}

// A registered buffer's state decays to COMMON when its list has run and comes back by an explicit transition when
// the next one opens; a texture's does not decay.
XR_TEST(Barrier_BufferDecay)
{
  CBarrierTracker t = Batched();
  ID3D12Resource *b = Res(1), *b0 = Res(2), *tex = Res(3);
  t.Register(b, COMMON, 1, true);
  t.Register(b0, COMMON, 1, true);
  t.Register(tex, UAV, 1, false);
  t.Require(b, UAV);
  std::vector<SCall> c = Flush(t);
  CHECK(c.size() == 1 && IsTransition(c[0][0], b, COMMON, UAV));
  Flush(t, false);
  t.OnListExecuted();
  CHECK(t.GetState(b) == COMMON && t.GetState(b0) == COMMON && t.GetState(tex) == UAV);
  CHECK(!t.HasPending());
  t.OnListOpened();
  CHECK(t.HasPending());
  c = Flush(t);
  CHECK(c.size() == 1 && c[0].size() == 1 && IsTransition(c[0][0], b, COMMON, UAV));
  t.Require(b, NPSR);
  c = Flush(t);
  CHECK(c.size() == 1 && IsTransition(c[0][0], b, UAV, NPSR));
  // Queued at the list's execution: a problem, and dropped.
  t.Require(tex, NPSR);
  t.OnListExecuted();
  CHECK(CountKind(t.TakeProblems(), PROBLEM_PENDING_AT_EXECUTE) == 1);
  CHECK(!t.HasPending());
}

// Why: the HDR statistics' words (HdrStats.cpp) named the state before each transition by the device's list count:
// COMMON where the frame's list had run since their last kernel (a readback's FlushFrame, WaitAsyncCompute), else the
// state left. Kept by the tracker instead, each frame's clearing copy, the two kernels and the copy out name no state
// before: every transition's before is the state the buffer has on the GPU (COMMON at a list's start) in both modes, the
// list split between the kernels or not, two frames in a row; the UAV barrier between the kernels is given where they
// share a list, left out in batches where the list ran between them.
XR_TEST(Barrier_HdrStatsWords)
{
  ID3D12Resource *w = Res(1);
  for (EMode eMode : { MODE_IMMEDIATE, MODE_BATCHED })
    for (bool bSplit : { false, true })
    {
      CBarrierTracker t;
      t.SetMode(eMode);
      t.Register(w, COMMON, 1, true);
      D3D12_RESOURCE_STATES Gpu = COMMON;
      size_t nWrong = 0, nUav = 0;
      const auto Give = [&](bool bWork) {
        for (const SCall &c : Flush(t, bWork))
          for (const D3D12_RESOURCE_BARRIER &b : c)
            if (b.Type == D3D12_RESOURCE_BARRIER_TYPE_UAV)
              nUav++;
            else if (b.Transition.pResource == w)
            {
              nWrong += b.Transition.StateBefore != Gpu;
              Gpu = b.Transition.StateAfter;
            }
      };
      const auto Execute = [&] {
        Give(false);
        t.OnListExecuted();
        Gpu = COMMON;
        t.OnListOpened();
      };
      for (int nFrame = 0; nFrame < 2; nFrame++)
      {
        nUav = 0;
        t.Require(w, DST);
        Give(true);  // the copy of zeros
        t.Require(w, UAV);
        Give(true);  // the histogram
        if (bSplit)
          Execute();
        t.Require(w, UAV);
        t.Uav(w);
        Give(true);  // the output's statistics
        t.Require(w, SRC);
        Give(true);  // the copy out
        Execute();   // the frame's end
        // The immediate mode gives it as it came.
        CHECK_MSG(nUav == (bSplit && eMode == MODE_BATCHED ? 0u : 1u), std::to_string(nUav) + " UAV barriers, frame " + std::to_string(nFrame));
      }
      CHECK_MSG(nWrong == 0, std::to_string(nWrong) + (eMode == MODE_BATCHED ? " befores wrong, batched" : " befores wrong, immediate") +
                               (bSplit ? ", split" : ""));
      CHECK(t.TakeProblems().empty());
    }
}

// Why: SMAA's targets and precomputed textures (PostAA.cpp's DrawSmaaPasses) were moved from PIXEL_SHADER_RESOURCE to
// what each kernel reads and back every frame. Kept by the tracker, each kernel and the pass asking for its own: every
// before is the state on the GPU, each piece of work finds the states it needs, in both modes and three frames; after
// the first frame the area and search textures have no transitions, and a frame has 5 transitions fewer.
XR_TEST(Barrier_SmaaStates)
{
  ID3D12Resource *const pCopy = Res(1), *pEdges = Res(2), *pWeights = Res(3), *pArea = Res(4), *pSearch = Res(5);
  constexpr D3D12_RESOURCE_STATES READ = PSR | NPSR;
  for (EMode eMode : { MODE_IMMEDIATE, MODE_BATCHED })
  {
    CBarrierTracker t;
    t.SetMode(eMode);
    for (ID3D12Resource *p : { pEdges, pWeights, pArea, pSearch })
      t.Register(p, PSR, 1, false);
    std::map<ID3D12Resource *, D3D12_RESOURCE_STATES> Gpu = { { pCopy, PSR }, { pEdges, PSR }, { pWeights, PSR }, { pArea, PSR }, { pSearch, PSR } };
    size_t nWrong = 0, nMissing = 0, nPrecomputed = 0, nTransitions = 0;
    // The queue given before a piece of work, which then needs each of Needs (a read state holding it, or it).
    const auto Work = [&](std::initializer_list<std::pair<ID3D12Resource *, D3D12_RESOURCE_STATES>> Needs) {
      for (const SCall &c : Flush(t, true))
        for (const D3D12_RESOURCE_BARRIER &b : c)
          if (b.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)
          {
            ID3D12Resource *p = b.Transition.pResource;
            nWrong += b.Transition.StateBefore != Gpu[p];
            Gpu[p] = b.Transition.StateAfter;
            nPrecomputed += p == pArea || p == pSearch;
            nTransitions++;
          }
      for (const auto &n : Needs)
        nMissing += n.second == UAV ? Gpu[n.first] != UAV : (Gpu[n.first] & n.second) != n.second;
    };
    size_t nFirst = 0;
    for (int nFrame = 0; nFrame < 3; nFrame++)
    {
      nPrecomputed = nTransitions = 0;
      t.Transition(pCopy, PSR, READ);
      t.Require(pEdges, UAV);
      Work({ { pCopy, NPSR }, { pEdges, UAV } });  // the edges
      t.Require(pEdges, READ);
      t.Require(pArea, READ);
      t.Require(pSearch, READ);
      t.Require(pWeights, UAV);
      Work({ { pEdges, NPSR }, { pArea, NPSR }, { pSearch, NPSR }, { pWeights, UAV } });  // the weights
      t.Require(pWeights, PSR);
      t.Transition(pCopy, READ, PSR);
      Work({ { pCopy, PSR }, { pEdges, PSR }, { pWeights, PSR } });  // the pass
      t.OnListExecuted();
      t.OnListOpened();
      if (nFrame == 0)
        nFirst = nTransitions;
      else
        CHECK_MSG(nPrecomputed == 0, std::to_string(nPrecomputed) + " transitions of the area and search textures, frame " + std::to_string(nFrame));
    }
    // Before: the copy 2, the edges 3 (to UAV, READ, PS), the area and search 2 each, the weights 2.
    CHECK_MSG(nTransitions == 6, std::to_string(nTransitions) + " transitions in a frame after the first (11 before)");
    CHECK_MSG(nFirst == 8, std::to_string(nFirst) + " transitions in the first frame");
    CHECK_MSG(nWrong == 0, std::to_string(nWrong) + (eMode == MODE_BATCHED ? " befores wrong, batched" : " befores wrong, immediate"));
    CHECK_MSG(nMissing == 0, std::to_string(nMissing) + (eMode == MODE_BATCHED ? " states missing, batched" : " states missing, immediate"));
    CHECK(t.TakeProblems().empty());
  }
}

// Why: the bloom's levels (Hdr.cpp) went PIXEL_SHADER_RESOURCE -> UNORDERED_ACCESS -> PIXEL_SHADER_RESOURCE for their
// kernel, PIXEL_SHADER_RESOURCE -> READ -> PIXEL_SHADER_RESOURCE for the next level's, and through the device's
// offscreen target (PIXEL_SHADER_RESOURCE <-> RENDER_TARGET) for the pass that adds the level below. Kept by the tracker,
// the kernels and the offscreen target asking for theirs (SetOffscreenTargets, EndOffscreen): every before is the state on
// the GPU, each piece of work finds its states, in both modes and three frames; a frame of N levels has 4N - 2
// transitions in batches (5N - 3 before).
XR_TEST(Barrier_BloomChain)
{
  constexpr int N = 6;
  constexpr D3D12_RESOURCE_STATES READ = PSR | NPSR;
  std::vector<ID3D12Resource *> Levels;
  for (int i = 0; i < N; i++)
    Levels.push_back(Res(10 + i));
  for (EMode eMode : { MODE_IMMEDIATE, MODE_BATCHED })
  {
    CBarrierTracker t;
    t.SetMode(eMode);
    std::map<ID3D12Resource *, D3D12_RESOURCE_STATES> Gpu;
    for (ID3D12Resource *p : Levels)
    {
      t.Register(p, PSR, 1, false);
      Gpu[p] = PSR;
    }
    size_t nWrong = 0, nMissing = 0, nTransitions = 0;
    const auto Work = [&](std::initializer_list<std::pair<ID3D12Resource *, D3D12_RESOURCE_STATES>> Needs) {
      for (const SCall &c : Flush(t, true))
        for (const D3D12_RESOURCE_BARRIER &b : c)
          if (b.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)
          {
            nWrong += b.Transition.StateBefore != Gpu[b.Transition.pResource];
            Gpu[b.Transition.pResource] = b.Transition.StateAfter;
            nTransitions++;
          }
      for (const auto &n : Needs)
        nMissing += n.second == UAV || n.second == RT ? Gpu[n.first] != n.second : (Gpu[n.first] & n.second) != n.second;
    };
    for (int nFrame = 0; nFrame < 3; nFrame++)
    {
      nTransitions = 0;
      // Down: each level's kernel from the one before (the first from the EV, not tracked).
      for (int i = 0; i < N; i++)
      {
        if (i > 0)
          t.Require(Levels[i - 1], READ);
        t.Require(Levels[i], UAV);
        if (i > 0)
          Work({ { Levels[i - 1], NPSR }, { Levels[i], UAV } });
        else
          Work({ { Levels[i], UAV } });
        t.Require(Levels[i], PSR);
      }
      // Up: each level drawn into, the one below added (SetOffscreenTargets, the draw, EndOffscreen).
      for (int i = N - 1; i-- > 0;)
      {
        t.Require(Levels[i], RT);
        Work({ { Levels[i], RT }, { Levels[i + 1], PSR } });
        t.Require(Levels[i], PSR);
      }
      Work({ { Levels[0], PSR } });  // the resolve
      t.OnListExecuted();
      t.OnListOpened();
      // The immediate mode gives each request as it came: the level's PIXEL_SHADER_RESOURCE after its kernel, then READ.
      const size_t nWant = eMode == MODE_BATCHED ? 4 * N - 2 : 5 * N - 3;
      CHECK_MSG(nTransitions == nWant, std::to_string(nTransitions) + " transitions, frame " + std::to_string(nFrame));
    }
    CHECK_MSG(nWrong == 0, std::to_string(nWrong) + (eMode == MODE_BATCHED ? " befores wrong, batched" : " befores wrong, immediate"));
    CHECK_MSG(nMissing == 0, std::to_string(nMissing) + (eMode == MODE_BATCHED ? " states missing, batched" : " states missing, immediate"));
    CHECK(t.TakeProblems().empty());
  }
}

// Why: the path tracer's targets (PathTrace.cpp) were moved back to UNORDERED_ACCESS at each frame's end, the history the
// next frame reads too (simple: READ -> UAV -> READ for nothing); NRD's host kept its textures' states and its inputs'
// and output's by itself (UNORDERED_ACCESS before and after it), and lost them when it gave a pass up half way (fixed at
// step 318). Kept by the tracker, each kernel and NRD's passes asking for theirs: every before is the state on the GPU
// and each kernel finds its states over frames of NRD, NRD giving up half way, simple and a switch back, in both modes; a
// frame of simple has 10 transitions instead of 14, of NRD 26 as before.
XR_TEST(Barrier_PathTraceStates)
{
  constexpr D3D12_RESOURCE_STATES KREAD = NPSR, READ = PSR | NPSR;
  ID3D12Resource *const pRays = Res(20), *pHit = Res(21), *pLight = Res(22), *pHighlights = Res(23);
  ID3D12Resource *const pHistory[2] = { Res(24), Res(25) }, *pFrames[2] = { Res(26), Res(27) };
  // NRD's inputs and output (PathTrace's), two of its own textures (NRD's host's pool).
  ID3D12Resource *const pNrdIn[4] = { Res(28), Res(29), Res(30), Res(31) }, *pNrdOut = Res(32), *pPool[2] = { Res(33), Res(34) };
  for (EMode eMode : { MODE_IMMEDIATE, MODE_BATCHED })
  {
    CBarrierTracker t;
    t.SetMode(eMode);
    std::map<ID3D12Resource *, D3D12_RESOURCE_STATES> Gpu;
    for (ID3D12Resource *p : { pRays, pHit, pLight, pHighlights, pHistory[0], pHistory[1], pFrames[0], pFrames[1], pNrdIn[0], pNrdIn[1],
                               pNrdIn[2], pNrdIn[3], pNrdOut, pPool[0], pPool[1] })
    {
      t.Register(p, UAV, 1, false);
      Gpu[p] = UAV;
    }
    size_t nWrong = 0, nMissing = 0, nTransitions = 0;
    const auto Work = [&](std::initializer_list<std::pair<ID3D12Resource *, D3D12_RESOURCE_STATES>> Needs) {
      for (const SCall &c : Flush(t, true))
        for (const D3D12_RESOURCE_BARRIER &b : c)
          if (b.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)
          {
            nWrong += b.Transition.StateBefore != Gpu[b.Transition.pResource];
            Gpu[b.Transition.pResource] = b.Transition.StateAfter;
            nTransitions++;
          }
      for (const auto &n : Needs)
        nMissing += n.second == UAV ? Gpu[n.first] != UAV : (Gpu[n.first] & n.second) != n.second;
    };
    // Frames: NRD, NRD, NRD (the steady one counted), NRD giving up at its second pass, simple, simple, simple (counted),
    // NRD again.
    enum { NRD, NRD_FAILS, SIMPLE };
    const int Nrd[] = { NRD, NRD, NRD, NRD_FAILS, SIMPLE, SIMPLE, SIMPLE, NRD };
    int nCur = 0;
    for (size_t f = 0; f < std::size(Nrd); f++)
    {
      nTransitions = 0;
      nCur ^= 1;
      ID3D12Resource *pH = pHistory[nCur], *pPrevH = pHistory[nCur ^ 1], *pF = pFrames[nCur], *pPrevF = pFrames[nCur ^ 1];
      t.Require(pRays, UAV);
      t.Require(pHit, UAV);
      Work({ { pRays, UAV }, { pHit, UAV } });  // the cache's kernels
      Work({ { pRays, UAV }, { pHit, UAV } });  // CSTrace
      t.Require(pRays, KREAD);
      if (Nrd[f] != SIMPLE)
      {
        t.Require(pHit, KREAD);
        for (ID3D12Resource *p : pNrdIn)
          t.Require(p, UAV);
        Work({ { pRays, KREAD }, { pHit, KREAD }, { pNrdIn[0], UAV }, { pNrdIn[3], UAV } });  // NRD's inputs
        // NRD's passes: the inputs read into its first texture, that one read into its second and the output.
        for (ID3D12Resource *p : pNrdIn)
          t.Require(p, KREAD);
        t.Require(pPool[0], UAV);
        Work({ { pNrdIn[0], KREAD }, { pNrdIn[3], KREAD }, { pPool[0], UAV } });
        t.UavAll();
        t.Require(pPool[0], KREAD);
        t.Require(pPool[1], UAV);
        t.Require(pNrdOut, UAV);
        if (Nrd[f] == NRD)
        {
          Work({ { pPool[0], KREAD }, { pPool[1], UAV }, { pNrdOut, UAV } });
          t.Require(pPool[1], KREAD);
          Work({ { pPool[1], KREAD } });
        }
        t.Require(pNrdOut, KREAD);
        t.Require(pH, UAV);
        t.Require(pF, UAV);
        Work({ { pNrdOut, KREAD }, { pH, UAV }, { pF, UAV } });  // NRD's light into the history
      }
      else
      {
        t.Require(pPrevH, KREAD);
        t.Require(pPrevF, KREAD);
        t.Require(pH, UAV);
        t.Require(pF, UAV);
        Work({ { pRays, KREAD }, { pPrevH, KREAD }, { pPrevF, KREAD }, { pH, UAV }, { pF, UAV } });  // CSTemporal
      }
      t.Require(pH, KREAD);
      t.Require(pF, KREAD);
      t.Require(pLight, UAV);
      t.Require(pHighlights, UAV);
      Work({ { pRays, KREAD }, { pH, KREAD }, { pF, KREAD }, { pLight, UAV } });  // CSFilter
      Work({ { pHighlights, UAV } });                                              // the highlights
      t.Require(pLight, READ);
      t.Require(pHighlights, READ);
      Work({ { pLight, PSR }, { pHighlights, PSR } });  // the composite
      t.OnListExecuted();
      t.OnListOpened();
      if (f == 2)
        CHECK_MSG(nTransitions == 26, std::to_string(nTransitions) + " transitions in a frame of NRD (26 before)");
      if (f == 6)
        CHECK_MSG(nTransitions == 10, std::to_string(nTransitions) + " transitions in a frame of simple (14 before)");
    }
    CHECK_MSG(nWrong == 0, std::to_string(nWrong) + (eMode == MODE_BATCHED ? " befores wrong, batched" : " befores wrong, immediate"));
    CHECK_MSG(nMissing == 0, std::to_string(nMissing) + (eMode == MODE_BATCHED ? " states missing, batched" : " states missing, immediate"));
    CHECK(t.TakeProblems().empty());
  }
}

// Why: the sun's shadow by rays (RaySun.cpp) moved its rays back to UNORDERED_ACCESS at each frame's end, and with
// FidelityFX Denoiser every texture its kernels read, the frame before's depth too (read by this frame: UAV -> READ ->
// UAV for nothing). Kept by the tracker, each kernel asking for its own: every before is the state on the GPU and each
// kernel finds its states over frames of FidelityFX with two sets of casters and one, of the renderer's own denoise
// (FidelityFX's textures released) and FidelityFX again (made anew), in both modes; a frame of FidelityFX with two sets
// has 28 transitions instead of 34, of the renderer's own 6 as before.
XR_TEST(Barrier_RaySunStates)
{
  constexpr D3D12_RESOURCE_STATES READ = NPSR;
  ID3D12Resource *const pRays = Res(40), *pMask = Res(41), *pHist[2] = { Res(42), Res(43) };
  // FidelityFX's: normals, depth of each frame, reprojected, filtered, moments of each set and frame, history and
  // denoised of each set.
  ID3D12Resource *const pNormals = Res(50), *pDepth[2] = { Res(51), Res(52) }, *pRepro = Res(53), *pFiltered = Res(54);
  ID3D12Resource *const pMoments[2][2] = { { Res(55), Res(56) }, { Res(57), Res(58) } }, *pFHist[2] = { Res(59), Res(60) },
                        *pDenoised[2] = { Res(61), Res(62) };
  const std::vector<ID3D12Resource *> Ffx = { pNormals, pDepth[0], pDepth[1], pRepro, pFiltered, pMoments[0][0], pMoments[0][1],
                                              pMoments[1][0], pMoments[1][1], pFHist[0], pFHist[1], pDenoised[0], pDenoised[1] };
  for (EMode eMode : { MODE_IMMEDIATE, MODE_BATCHED })
  {
    CBarrierTracker t;
    t.SetMode(eMode);
    std::map<ID3D12Resource *, D3D12_RESOURCE_STATES> Gpu;
    const auto Make = [&](ID3D12Resource *p) {
      t.Register(p, UAV, 1, false);
      Gpu[p] = UAV;
    };
    for (ID3D12Resource *p : { pRays, pMask, pHist[0], pHist[1] })
      Make(p);
    for (ID3D12Resource *p : Ffx)
      Make(p);
    size_t nWrong = 0, nMissing = 0, nTransitions = 0;
    const auto Work = [&](std::initializer_list<std::pair<ID3D12Resource *, D3D12_RESOURCE_STATES>> Needs) {
      for (const SCall &c : Flush(t, true))
        for (const D3D12_RESOURCE_BARRIER &b : c)
          if (b.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)
          {
            nWrong += b.Transition.StateBefore != Gpu[b.Transition.pResource];
            Gpu[b.Transition.pResource] = b.Transition.StateAfter;
            nTransitions++;
          }
      for (const auto &n : Needs)
        nMissing += n.second == UAV ? Gpu[n.first] != UAV : (Gpu[n.first] & n.second) != n.second;
    };
    // Frames: the number of FidelityFX's sets (0: the renderer's own denoise).
    const int Sets[] = { 2, 2, 2, 1, 0, 0, 0, 2, 2 };
    int nCur = 0, nFfxCur = 0;
    for (size_t f = 0; f < std::size(Sets); f++)
    {
      nTransitions = 0;
      if (f == 7)
        for (ID3D12Resource *p : Ffx)
          Make(p);  // made anew (Forget at the frame of the renderer's own)
      t.Require(pRays, UAV);
      Work({ { pRays, UAV } });  // CSTrace
      t.Require(pRays, READ);
      if (Sets[f])
      {
        const int c = nFfxCur ^= 1, p = c ^ 1;
        t.Require(pNormals, UAV);
        t.Require(pDepth[c], UAV);
        Work({ { pRays, READ }, { pNormals, UAV }, { pDepth[c], UAV } });  // CSInputs
        t.Require(pNormals, READ);
        t.Require(pDepth[c], READ);
        t.Require(pDepth[p], READ);
        for (int s = 0; s < Sets[f]; s++)
        {
          Work({ { pNormals, READ }, { pDepth[c], READ } });  // CSPrepare (its masks: UAVs only)
          t.Require(pFHist[s], READ);
          t.Require(pMoments[s][p], READ);
          t.Require(pRepro, UAV);
          t.Require(pMoments[s][c], UAV);
          Work({ { pDepth[c], READ }, { pDepth[p], READ }, { pFHist[s], READ }, { pMoments[s][p], READ }, { pRepro, UAV }, { pMoments[s][c], UAV } });
          t.Require(pRepro, READ);
          t.Require(pFiltered, UAV);
          Work({ { pRepro, READ }, { pFiltered, UAV } });  // the filter's first pass
          t.Require(pFiltered, READ);
          t.Require(pFHist[s], UAV);
          Work({ { pFiltered, READ }, { pFHist[s], UAV } });  // its second, into the next frame's history
          t.Require(pFHist[s], READ);
          t.Require(pDenoised[s], UAV);
          Work({ { pFHist[s], READ }, { pDenoised[s], UAV } });  // its third
          t.Require(pDenoised[s], READ);
        }
        t.Require(pMask, UAV);
        Work({ { pDenoised[0], READ }, { pMask, UAV } });  // CSPack
      }
      else
      {
        if (f == 4)
          for (ID3D12Resource *p : Ffx)
          {
            t.Forget(p);  // FidelityFX's textures released (ReleaseRaySunFfx)
            Gpu.erase(p);
          }
        nCur ^= 1;
        t.Require(pHist[nCur ^ 1], READ);
        t.Require(pHist[nCur], UAV);
        Work({ { pRays, READ }, { pHist[nCur ^ 1], READ }, { pHist[nCur], UAV } });  // CSTemporal
        t.Require(pHist[nCur], READ);
        t.Require(pMask, UAV);
        Work({ { pHist[nCur], READ }, { pMask, UAV } });  // CSFilter
      }
      t.Require(pMask, PSR);
      Work({ { pMask, PSR } });  // the opaque list
      t.OnListExecuted();
      t.OnListOpened();
      if (f == 2)
        CHECK_MSG(nTransitions == 28, std::to_string(nTransitions) + " transitions in a frame of FidelityFX, two sets (34 before)");
      if (f == 6)
        CHECK_MSG(nTransitions == 6, std::to_string(nTransitions) + " transitions in a frame of the renderer's own (6 before)");
    }
    CHECK_MSG(nWrong == 0, std::to_string(nWrong) + (eMode == MODE_BATCHED ? " befores wrong, batched" : " befores wrong, immediate"));
    CHECK_MSG(nMissing == 0, std::to_string(nMissing) + (eMode == MODE_BATCHED ? " states missing, batched" : " states missing, immediate"));
    CHECK(t.TakeProblems().empty());
  }
}

// Read states: one that holds the wanted one already does, two shader reads combine, the others as asked.
XR_TEST(Barrier_ReadStates)
{
  CBarrierTracker t = Batched();
  t.Register(Res(1), PSR | NPSR, 1);
  t.Require(Res(1), NPSR);
  CHECK(Flush(t).empty());
  t.Register(Res(2), DEPTH_READ, 1);
  t.Require(Res(2), NPSR);
  std::vector<SCall> c = Flush(t);
  CHECK(c.size() == 1 && IsTransition(c[0][0], Res(2), DEPTH_READ, DEPTH_READ | NPSR));
  t.Register(Res(3), NPSR, 1);
  t.Require(Res(3), SRC);
  c = Flush(t);
  CHECK(c.size() == 1 && IsTransition(c[0][0], Res(3), NPSR, SRC));
  t.Register(Res(4), D3D12_RESOURCE_STATE_GENERIC_READ, 1);
  t.Require(Res(4), NPSR);
  t.Require(Res(4), SRC);
  CHECK(Flush(t).empty());
  t.Require(Res(4), UAV);
  c = Flush(t);
  CHECK(c.size() == 1 && IsTransition(c[0][0], Res(4), D3D12_RESOURCE_STATE_GENERIC_READ, UAV));
  // Not registered: a problem, nothing given.
  t.Require(Res(9), UAV);
  CHECK(Flush(t).empty());
  CHECK(CountKind(t.TakeProblems(), PROBLEM_NOT_REGISTERED) == 1);
}

// An address given again (a placed resource): registered again, the new resource's state; what was queued for the
// forgotten one is still given.
XR_TEST(Barrier_AddressReuse)
{
  CBarrierTracker t = Batched();
  t.Register(Res(1), UAV, 1);
  t.Require(Res(1), NPSR);
  t.Forget(Res(1));
  CHECK(!t.IsRegistered(Res(1)));
  std::vector<SCall> c = Flush(t);
  CHECK(c.size() == 1 && IsTransition(c[0][0], Res(1), UAV, NPSR));
  t.Register(Res(1), RT, 1);
  CHECK(t.GetState(Res(1)) == RT);
  t.Require(Res(1), PSR);
  c = Flush(t);
  CHECK(c.size() == 1 && IsTransition(c[0][0], Res(1), RT, PSR));
}

// A registered resource's subresources: one of them, then all (different subresources share a call), then all from
// one state (one barrier of the whole).
XR_TEST(Barrier_Subresources)
{
  CBarrierTracker t = Batched();
  ID3D12Resource *m = Res(1);
  t.Register(m, PSR, 4);
  t.Require(m, RT, 1);
  std::vector<SCall> c = Flush(t);
  CHECK(c.size() == 1 && c[0].size() == 1 && IsTransition(c[0][0], m, PSR, RT, 1));
  t.Require(m, UAV);
  c = Flush(t);
  CHECK(c.size() == 1 && c[0].size() == 4);
  t.Require(m, NPSR);
  c = Flush(t);
  CHECK(c.size() == 1 && c[0].size() == 1 && IsTransition(c[0][0], m, UAV, NPSR));
  CHECK(t.GetState(m, 2) == NPSR);
  t.Require(m, UAV, 7);
  CHECK(Flush(t).empty());
  CHECK(CountKind(t.TakeProblems(), PROBLEM_SUBRESOURCE) == 1);
  // The renderer's own transition of a registered resource: checked against its state, made from it.
  t.Transition(m, PSR, UAV);
  c = Flush(t);
  CHECK(c.size() == 1 && IsTransition(c[0][0], m, NPSR, UAV));
  CHECK(CountKind(t.TakeProblems(), PROBLEM_BEFORE) == 1);
}

// The immediate mode keeps the registered states too (each request a call of its own).
XR_TEST(Barrier_ImmediateRegistered)
{
  CBarrierTracker t;
  t.Register(Res(1), UAV, 1);
  t.Require(Res(1), NPSR);
  t.Require(Res(1), PSR);
  t.Require(Res(1), PSR);
  const std::vector<SCall> c = Flush(t);
  CHECK(c.size() == 2);
  if (c.size() == 2)
  {
    CHECK(IsTransition(c[0][0], Res(1), UAV, NPSR));
    CHECK(IsTransition(c[1][0], Res(1), NPSR, NPSR | PSR));
  }
  CHECK(t.GetState(Res(1)) == (NPSR | PSR));
}

// A third party left a resource in a state of its own: no barrier, the next request from there.
XR_TEST(Barrier_Assume)
{
  CBarrierTracker t = Batched();
  t.Register(Res(1), UAV, 1);
  t.Assume(Res(1), D3D12_RESOURCE_STATE_GENERIC_READ);
  CHECK(Flush(t).empty());
  t.Require(Res(1), UAV);
  const std::vector<SCall> c = Flush(t);
  CHECK(c.size() == 1 && IsTransition(c[0][0], Res(1), D3D12_RESOURCE_STATE_GENERIC_READ, UAV));
  t.Require(Res(1), NPSR);
  t.Assume(Res(1), UAV);  // queued and not given before the third party: a problem, given first
  const std::vector<SCall> c2 = Flush(t);
  CHECK(c2.size() == 1 && IsTransition(c2[0][0], Res(1), UAV, NPSR));
  CHECK(CountKind(t.TakeProblems(), PROBLEM_ASSUME_PENDING) == 1);
  CHECK(t.GetState(Res(1)) == UAV);
}

// The census of the renderer's own transitions: a before that is not what the resource's last transition gave; a
// buffer's state forgotten when its list has run, a texture's kept.
XR_TEST(Barrier_Census)
{
  for (int nMode = 0; nMode < 2; nMode++)
  {
    CBarrierTracker t;
    t.SetMode((EMode)nMode);
    t.SetCensus(true, [](ID3D12Resource *p) { return p == Res(2); });
    t.Transition(Res(1), PSR, UAV);
    t.Transition(Res(2), COMMON, UAV);
    Flush(t);
    t.Transition(Res(1), PSR, NPSR);
    Flush(t);
    CHECK(CountKind(t.TakeProblems(), PROBLEM_BEFORE) == 1);
    t.Transition(Res(1), NPSR, UAV);
    Flush(t, false);
    t.OnListExecuted();
    t.Transition(Res(2), COMMON, UAV);  // decayed: fine
    t.Transition(Res(1), NPSR, PSR);    // the texture is UNORDERED_ACCESS
    Flush(t);
    CHECK(CountKind(t.TakeProblems(), PROBLEM_BEFORE) == 1);
    // One subresource of a resource set as a whole, then the whole again.
    t.Transition(Res(3), PSR, RT);
    Flush(t);
    t.Transition(Res(3), RT, PSR, 2);
    Flush(t);
    t.Transition(Res(3), RT, PSR);  // subresource 2 is PSR
    Flush(t);
    CHECK(CountKind(t.TakeProblems(), PROBLEM_BEFORE) == 1);
    t.Forget(Res(3));
    t.Transition(Res(3), UAV, PSR);  // another resource at the address
    Flush(t);
    CHECK(t.TakeProblems().empty());
  }
}

// Why: a transition that one call cannot hold with what is queued (the whole resource and then one of its subresources,
// a round trip through a write state with no work in it) seals the queue and is queued again after it; the census
// checked it the second time too, against the after it had just kept, and step 318's census in the game logged the
// water reflection's mips (SetOffscreenTargets) and a shadow map with no draws (DEPTH_WRITE and back) as wrong. Each
// transition is counted and checked once; a wrong before is still found, once.
XR_TEST(Barrier_CensusSealedOnce)
{
  constexpr D3D12_RESOURCE_STATES DW = D3D12_RESOURCE_STATE_DEPTH_WRITE;
  CBarrierTracker t = Batched();
  t.SetCensus(true);
  // The reflection: drawn as a whole, then each mip from the one before.
  t.Transition(Res(1), PSR, RT);
  Flush(t);
  t.Transition(Res(1), RT, PSR);
  t.Transition(Res(1), PSR, RT, 1);
  Flush(t);
  for (UINT i = 2; i < 5; i++)
  {
    t.Transition(Res(1), RT, PSR, i - 1);
    t.Transition(Res(1), PSR, RT, i);
    Flush(t);
  }
  t.Transition(Res(1), RT, PSR, 4);
  Flush(t, false);
  // A shadow map drawn, then left and set again with no draws between.
  t.Transition(Res(2), PSR, DW);
  Flush(t);
  const auto nRequests = t.Stats().nRequests;
  t.Transition(Res(2), DW, PSR);
  t.Transition(Res(2), PSR, DW);
  const std::vector<SCall> c = Flush(t);
  CHECK(c.size() == 2);  // a round trip through a write state: not merged
  CHECK(t.Stats().nRequests == nRequests + 2);
  CHECK(t.TakeProblems().empty());
  // A wrong before of a subresource after the whole: found once.
  t.Transition(Res(3), PSR, RT);
  Flush(t);
  t.Transition(Res(3), RT, PSR);
  t.Transition(Res(3), NPSR, RT, 1);
  Flush(t);
  CHECK(CountKind(t.TakeProblems(), PROBLEM_BEFORE) == 1);
}

// Why: the renderer releases resources by more ways than DeferredRelease (the scene targets made again for another
// size, NRD's pool), so a new resource at a released one's address was taken for it: step 317's census in the game
// logged the scene targets' first transitions from SCENE_READ (the state they are made in) as wrong, the state kept
// that of a texture released before. The census tells them apart by an identity the device gives (its private data).
namespace
{
std::map<ID3D12Resource *, uint64_t> s_CensusIds;
uint64_t CensusId(ID3D12Resource *p) { return s_CensusIds[p]; }
}

XR_TEST(Barrier_CensusIdentity)
{
  for (int nMode = 0; nMode < 2; nMode++)
  {
    s_CensusIds.clear();
    const D3D12_RESOURCE_STATES SCENE = PSR | NPSR;  // the scene targets' state when they are made
    CBarrierTracker t;
    t.SetMode((EMode)nMode);
    t.SetCensus(true, nullptr, &CensusId);
    t.Transition(Res(1), RT, PSR);
    t.Transition(Res(2), COMMON, UAV, 1);
    Flush(t);
    // Released without Forget, another resource made there: its first transition from the state it was made in.
    s_CensusIds[Res(1)] = 7;
    s_CensusIds[Res(2)] = 7;
    t.Transition(Res(1), SCENE, RT);
    t.Transition(Res(2), DST, UAV);
    Flush(t);
    CHECK(t.TakeProblems().empty());
    // The new one's states are kept and checked.
    t.Transition(Res(1), SCENE, PSR);
    Flush(t);
    CHECK(CountKind(t.TakeProblems(), PROBLEM_BEFORE) == 1);
    // A library's state of a new resource at an address (Assume).
    s_CensusIds[Res(1)] = 8;
    t.Assume(Res(1), UAV);
    s_CensusIds[Res(1)] = 9;
    t.Transition(Res(1), RT, PSR);
    Flush(t);
    CHECK(t.TakeProblems().empty());
    // The same identity: a wrong before is still one.
    t.Transition(Res(1), UAV, NPSR);
    Flush(t);
    CHECK(CountKind(t.TakeProblems(), PROBLEM_BEFORE) == 1);
  }
}

// The counts: calls, the calls after work (each a wait for it), barriers by kind.
XR_TEST(Barrier_Stats)
{
  CBarrierTracker t = Batched();
  t.Transition(Res(1), PSR, UAV);
  Flush(t);
  t.Transition(Res(1), UAV, PSR);
  t.Uav(Res(2));
  Flush(t);
  t.UavAll();
  Flush(t, false);
  const SStats &s = t.Stats();
  CHECK(s.nRequests == 4 && s.nCalls == 3 && s.nDrains == 2 && s.nTransitions == 2 && s.nUav == 1 && s.nUavAll == 1);
  t.ResetStats();
  CHECK(t.Stats().nCalls == 0 && t.Stats().nRequests == 0);
}

// A long random sequence of the renderer's transitions (named befores, whole resources and single subresources), UAV
// barriers and work, in both modes, on a model of the GPU's states: every transition given from the state the model has,
// no call the debug layer refuses, at the end the same states as the requests set. Then the same with the resources
// registered and Require.
XR_TEST(Barrier_RandomAgainstModel)
{
  constexpr int RES = 5, SUBS = 3, STEPS = 20000;
  const D3D12_RESOURCE_STATES States[] = { UAV, NPSR, PSR, SRC, DST, RT, PSR | NPSR };
  int nVariantCalls[4] = {};
  for (int nVariant = 0; nVariant < 4; nVariant++)
  {
    const bool bBatched = (nVariant & 1) != 0, bRegistered = (nVariant & 2) != 0;
    CBarrierTracker t;
    t.SetMode(bBatched ? MODE_BATCHED : MODE_IMMEDIATE);
    uint32_t nSeed = 12345u + (uint32_t)nVariant;
    auto Rand = [&](uint32_t n) {
      nSeed = nSeed * 1664525u + 1013904223u;
      return (nSeed >> 8) % n;
    };
    // What the requests set (Truth) and what the GPU has from the barriers given (Gpu).
    D3D12_RESOURCE_STATES Truth[RES + 1][SUBS], Gpu[RES + 1][SUBS];
    for (int r = 1; r <= RES; r++)
      for (int s = 0; s < SUBS; s++)
        Truth[r][s] = Gpu[r][s] = PSR;
    if (bRegistered)
      for (int r = 1; r <= RES; r++)
        t.Register(Res(r), PSR, SUBS);
    int nBad = 0, nCalls = 0;
    std::string Why;
    auto Apply = [&](bool bWork) {
      std::vector<SCall> Calls;
      t.Flush([&](UINT n, const D3D12_RESOURCE_BARRIER *p) { Calls.emplace_back(p, p + n); }, bWork);
      for (const SCall &c : Calls)
      {
        nCalls++;
        if (!ValidCall(c, &Why))
          nBad++;
        for (const D3D12_RESOURCE_BARRIER &b : c)
        {
          if (b.Type != D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)
            continue;
          const int r = (int)((uintptr_t)b.Transition.pResource / 0x10000);
          for (int s = 0; s < SUBS; s++)
            if (b.Transition.Subresource == ALL || b.Transition.Subresource == (UINT)s)
            {
              if (Gpu[r][s] != b.Transition.StateBefore)
              {
                nBad++;
                Why = "a before that is not the GPU's state";
              }
              Gpu[r][s] = b.Transition.StateAfter;
            }
        }
      }
    };
    for (int i = 0; i < STEPS; i++)
    {
      const uint32_t nOp = Rand(10);
      const int r = 1 + (int)Rand(RES);
      if (nOp < 6)
      {
        const D3D12_RESOURCE_STATES After = States[Rand(7)];
        const bool bWhole = Rand(3) != 0;
        bool bUniform = true;
        for (int s = 1; s < SUBS; s++)
          bUniform &= Truth[r][s] == Truth[r][0];
        if (bWhole && bUniform)
        {
          if (After == Truth[r][0])
            continue;
          if (bRegistered)
            t.Require(Res(r), After);
          else
            t.Transition(Res(r), Truth[r][0], After);
          for (int s = 0; s < SUBS; s++)
            Truth[r][s] = bRegistered && Truth[r][s] != 0 && !IsWrite(Truth[r][s]) && !IsWrite(After) && (Truth[r][s] & After) == After
                            ? Truth[r][s]
                            : bRegistered && IsShaderRead(Truth[r][s]) && IsShaderRead(After) ? Truth[r][s] | After : After;
        }
        else
        {
          const UINT s = Rand(SUBS);
          if (After == Truth[r][s])
            continue;
          if (bRegistered)
            t.Require(Res(r), After, s);
          else
            t.Transition(Res(r), Truth[r][s], After, s);
          Truth[r][s] = bRegistered && Truth[r][s] != 0 && !IsWrite(Truth[r][s]) && !IsWrite(After) && (Truth[r][s] & After) == After
                          ? Truth[r][s]
                          : bRegistered && IsShaderRead(Truth[r][s]) && IsShaderRead(After) ? Truth[r][s] | After : After;
        }
      }
      else if (nOp == 6)
        t.Uav(Res(r));
      else if (nOp == 7)
        t.UavAll();
      else
        Apply(true);
    }
    Apply(false);
    int nDiffer = 0;
    for (int r = 1; r <= RES; r++)
      for (int s = 0; s < SUBS; s++)
        nDiffer += Gpu[r][s] != Truth[r][s];
    CHECK_MSG(nBad == 0, std::to_string(nBad) + " bad (" + Why + "), variant " + std::to_string(nVariant));
    CHECK_MSG(nDiffer == 0, std::to_string(nDiffer) + " states differ, variant " + std::to_string(nVariant));
    CHECK(t.TakeProblems().empty());
    CHECK(nCalls > 0);
    nVariantCalls[nVariant] = nCalls;
  }
  // Batched: far fewer calls than the requests' (immediate: a call each).
  CHECK_MSG(nVariantCalls[1] * 2 < nVariantCalls[0], std::to_string(nVariantCalls[1]) + " calls batched, " + std::to_string(nVariantCalls[0]) + " immediate");
  CHECK_MSG(nVariantCalls[3] * 2 < nVariantCalls[2], std::to_string(nVariantCalls[3]) + " calls batched, " + std::to_string(nVariantCalls[2]) + " immediate");
}

// A pass's own array given as it is (Verbatim) after what is queued, in a call of its own, in the batched mode too; the
// census checks its befores and learns its afters; a third party's state (Assume) of a resource not registered goes to
// the census; the problems carry the context they were found in.
XR_TEST(Barrier_Verbatim)
{
  CBarrierTracker t = Batched();
  t.SetCensus(true);
  t.SetContext("pass");
  t.Transition(Res(1), PSR, RT);
  D3D12_RESOURCE_BARRIER Own[2] = {};
  Own[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  Own[0].Transition = { Res(2), ALL, COMMON, UAV };
  Own[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  t.Verbatim(Own, 2);
  std::vector<SCall> c = Flush(t);
  CHECK(c.size() == 2 && c[0].size() == 1 && IsTransition(c[0][0], Res(1), PSR, RT));
  CHECK(c.size() == 2 && c[1].size() == 2 && IsTransition(c[1][0], Res(2), COMMON, UAV) && IsUav(c[1][1], nullptr));
  // The census learnt Res(2)'s UNORDERED_ACCESS: a transition from another state is a problem (given as asked).
  t.Transition(Res(2), NPSR, SRC);
  c = Flush(t);
  CHECK(c.size() == 1 && IsTransition(c[0][0], Res(2), NPSR, SRC));
  std::vector<SProblem> p = t.TakeProblems();
  CHECK(p.size() == 1 && p[0].eKind == PROBLEM_BEFORE && p[0].Expected == UAV && p[0].Got == NPSR && p[0].szContext && std::string(p[0].szContext) == "pass");
  // A third party left Res(3) in GENERIC_READ: the next transition from there is no problem, one from elsewhere is.
  t.Assume(Res(3), D3D12_RESOURCE_STATE_GENERIC_READ);
  t.Transition(Res(3), D3D12_RESOURCE_STATE_GENERIC_READ, UAV);
  Flush(t);
  CHECK(t.TakeProblems().empty());
  t.Assume(Res(3), D3D12_RESOURCE_STATE_GENERIC_READ);
  t.Transition(Res(3), PSR, UAV);
  Flush(t);
  CHECK(CountKind(t.TakeProblems(), PROBLEM_BEFORE) == 1);
}

// Why: the passes' own arrays (the device's RawBarrier and CopyTargets, XeGTAO, RayAO, CACAO, PostAA's UAV barrier,
// the ray scene's) were given in calls of their own before step 318, each a drain of its own. Queued as the device's
// ResourceBarriers queues them in the batched mode, they share the call with the transitions around them, and a chain
// through an array is one transition.
XR_TEST(Barrier_ArrayQueued)
{
  CBarrierTracker t = Batched();
  t.SetCensus(true);
  t.Transition(Res(1), PSR, RT);
  D3D12_RESOURCE_BARRIER Own[2] = {};
  Own[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  Own[0].Transition = { Res(2), ALL, UAV, NPSR };
  Own[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  t.Barriers(Own, 2);
  t.Transition(Res(3), NPSR, UAV);
  std::vector<SCall> c = Flush(t);
  CHECK(c.size() == 1);
  if (c.size() == 1)
  {
    CHECK(c[0].size() == 4);
    int nFound = 0;
    for (const D3D12_RESOURCE_BARRIER &b : c[0])
      nFound += IsTransition(b, Res(1), PSR, RT) || IsTransition(b, Res(2), UAV, NPSR) || IsTransition(b, Res(3), NPSR, UAV) ||
                IsUav(b, nullptr);
    CHECK(nFound == 4);
  }
  // A copy's way out and the next pass's transition: one transition, COPY_SOURCE to the pass's state.
  D3D12_RESOURCE_BARRIER Back = {};
  Back.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  Back.Transition = { Res(1), ALL, RT, SRC };
  t.Barriers(&Back, 1);
  Flush(t);  // the copy
  std::swap(Back.Transition.StateBefore, Back.Transition.StateAfter);
  t.Barriers(&Back, 1);
  t.Transition(Res(1), RT, PSR);
  c = Flush(t);
  CHECK(c.size() == 1 && c[0].size() == 1 && IsTransition(c[0][0], Res(1), SRC, PSR));
  CHECK(t.TakeProblems().empty());
  // The immediate mode: the array as it is, in a call of its own.
  CBarrierTracker u;
  u.Transition(Res(1), PSR, RT);
  u.Barriers(Own, 2);
  c = Flush(u);
  CHECK(c.size() == 2 && c[1].size() == 2);
}

// Why: the device's bound render targets and depth buffer are asked for again before each draw and clear (a pass may have
// read or copied one meanwhile), and only when a state the tracker keeps changed since the last time: nothing where
// they are as bound, a target read since back to RENDER_TARGET, one subresource of it alone, a depth buffer bound read
// only left where its state holds DEPTH_READ (or is DEPTH_WRITE), the resources not kept left to their owners.
XR_TEST(Barrier_BoundTargets)
{
  CBarrierTracker t = Batched();
  constexpr D3D12_RESOURCE_STATES DEPTH_WRITE = D3D12_RESOURCE_STATE_DEPTH_WRITE;
  // Changes counted where a state or a record changes, not by a request the state holds.
  const uint64_t n0 = t.GetChanges();
  t.Register(Res(1), RT, 1);
  t.Register(Res(2), DEPTH_WRITE, 1);
  t.Register(Res(3), PSR, 4);
  CHECK(t.GetChanges() == n0 + 3);
  t.Require(Res(1), RT);
  CHECK(t.GetChanges() == n0 + 3);

  CBoundTargets Bound;
  Bound.Add(Res(1), RT);
  Bound.Add(Res(2), DEPTH_WRITE);
  Bound.Add(Res(9), RT);  // not kept: its owner's
  Bound.Add(nullptr, RT);  // a null view: no resource
  CHECK(Bound.Count() == 3);
  Bound.Require(t);
  CHECK(Flush(t).empty());
  // A pass read the target: back to RENDER_TARGET before the next draw, then nothing more while nothing changes.
  t.Require(Res(1), PSR);
  Flush(t);
  Bound.Require(t);
  std::vector<SCall> c = Flush(t);
  CHECK(c.size() == 1 && c[0].size() == 1 && IsTransition(c[0][0], Res(1), PSR, RT));
  const uint64_t n1 = t.GetChanges();
  Bound.Require(t);
  CHECK(Flush(t).empty());
  CHECK(t.GetChanges() == n1);

  // A depth buffer bound read only: DEPTH_WRITE and a read state with DEPTH_READ kept, another read state gets it.
  Bound.Clear();
  CHECK(Bound.Count() == 0);
  Bound.Add(Res(2), DEPTH_READ);
  Bound.Require(t);
  CHECK(Flush(t).empty());
  t.Require(Res(2), DEPTH_READ | NPSR);
  Flush(t);
  Bound.Require(t);
  CHECK(Flush(t).empty());
  CHECK(t.GetState(Res(2)) == (DEPTH_READ | NPSR));
  t.Require(Res(2), D3D12_RESOURCE_STATE_COPY_SOURCE);
  Flush(t);
  Bound.Require(t);
  c = Flush(t);
  CHECK(c.size() == 1 && c[0].size() == 1 && IsTransition(c[0][0], Res(2), D3D12_RESOURCE_STATE_COPY_SOURCE, DEPTH_READ));

  // One subresource bound (a level of the water's reflection): that one alone.
  Bound.Clear();
  Bound.Add(Res(3), RT, 2);
  Bound.Require(t);
  c = Flush(t);
  CHECK(c.size() == 1 && c[0].size() == 1 && IsTransition(c[0][0], Res(3), PSR, RT, 2));
  CHECK(t.GetState(Res(3), 1) == PSR && t.GetState(Res(3), 2) == RT);

  // Removed (released while bound) and forgotten: nothing asked for.
  Bound.Add(Res(1), RT);
  Bound.Remove(Res(3));
  CHECK(Bound.Count() == 1);
  t.Require(Res(1), PSR);
  Flush(t);
  t.Forget(Res(1));
  Bound.Require(t);
  CHECK(Flush(t).empty());
  CHECK(t.TakeProblems().empty());

  // At most MAX bound (the device binds 8 targets and a depth buffer).
  Bound.Clear();
  for (int i = 0; i < CBoundTargets::MAX + 2; i++)
    Bound.Add(Res(20 + i), RT);
  CHECK(Bound.Count() == CBoundTargets::MAX);
}

// Why: a buffer whose state the tracker keeps decays to COMMON when its list has run, and the next list gives the state
// back by an explicit transition (an implicit promotion past the tracker would leave its state wrong). Each step reads
// what the step before wrote, a UAV barrier between them (left out where a transition into UNORDERED_ACCESS waits
// already): the values are the steps' in both modes, the list split anywhere, the debug layer quiet (ctest
// XRenderD3D12Tests.Debug), and the batches fewer calls.
XR_TEST(Barrier_GpuBufferDecay)
{
  const std::vector<uint32_t> Adds = { 1, 2, 3, 4, 5, 6, 7, 8 };
  const uint32_t N = 1000;
  uint32_t v = 0;
  for (uint32_t a : Adds)
    v = v * 3 + a;
  for (size_t nSplit : { (size_t)0, (size_t)1, (size_t)3, (size_t)7 })
  {
    SStats Stats[2];
    for (EMode eMode : { MODE_IMMEDIATE, MODE_BATCHED })
    {
      std::vector<uint32_t> Out;
      Stats[eMode] = XrTest::InBarrierMode(eMode, [&] { Out = XrTest::Gpu().RunBarrierSteps(XR_BYTECODE(Test_BarrierStep), N, Adds, nSplit); });
      size_t nWrong = 0;
      for (uint32_t w : Out)
        nWrong += w != v;
      CHECK_MSG(Out.size() == (size_t)N * 4 && nWrong == 0, std::to_string(nWrong) + " words wrong, split at " + std::to_string(nSplit) +
                                                            (eMode == MODE_BATCHED ? ", batched" : ", immediate"));
    }
    if (nSplit == 3)
      XrTest::CheckBatchedStats("buffer", Stats[0], Stats[1]);
  }
}

// Why: with d3d12_BarrierBatch the frame's barriers queue up until the next work on its list (CD3D12Device::FlushBarriers):
// work recorded on a command list past the device (a draw, a dispatch, a copy, a clear, a resolve, a build of the rays'
// structures, or barriers of its own) would run before the barriers it needs. Each such call in the renderer's sources
// says in its statement that what is queued was given first ("barriers flushed"); D3D12Upload.cpp (the uploads' own
// list) and D3D12Dred.cpp (DRED's markers, no resource states) aside.
XR_TEST(Barrier_SourceLint)
{
  namespace fs = std::filesystem;
  const fs::path Dir = XR_RENDERER_SOURCE_DIR;
  if (!fs::is_directory(Dir))
    XR_SKIP("the renderer's sources are not where the tests were built from");
  static const char *const szMethods[] = { "ResourceBarrier", "Dispatch", "DrawInstanced", "DrawIndexedInstanced", "ExecuteIndirect", "CopyResource",
                                           "CopyBufferRegion", "CopyTextureRegion", "CopyTiles", "ClearRenderTargetView", "ClearDepthStencilView",
                                           "ClearUnorderedAccessView", "ResolveSubresource", "BuildRaytracingAccelerationStructure",
                                           "CopyRaytracingAccelerationStructure", "DiscardResource" };
  // A command list's own call (the device's wrappers and CD3D12Compute's take a list, they are not one).
  const std::regex Command(R"((\w*List\w*|GetCmdList\(\)|GetDrawCmdList\(\))\s*->\s*(ResourceBarrier|Dispatch|DispatchRays|DrawInstanced|)"
                           R"(DrawIndexedInstanced|ExecuteIndirect|CopyResource|CopyBufferRegion|CopyTextureRegion|CopyTiles|ClearRenderTargetView|)"
                           R"(ClearDepthStencilView|ClearUnorderedAccessViewFloat|ClearUnorderedAccessViewUint|ResolveSubresource|)"
                           R"(ResolveSubresourceRegion|BuildRaytracingAccelerationStructure|CopyRaytracingAccelerationStructure|DiscardResource)\s*\()");
  int nFiles = 0, nCalls = 0;
  for (const fs::directory_entry &e : fs::directory_iterator(Dir))
  {
    const std::string Name = e.path().filename().string();
    if (e.path().extension() != ".cpp" || Name == "D3D12Upload.cpp" || Name == "D3D12Dred.cpp")
      continue;
    std::ifstream f(e.path());
    std::vector<std::string> Lines;
    for (std::string s; std::getline(f, s);)
      Lines.push_back(s);
    nFiles++;
    const auto Code = [&](size_t i) { return Lines[i].substr(0, Lines[i].find("//")); };
    for (size_t i = 0; i < Lines.size(); i++)
    {
      const std::string c = Code(i);
      bool bAny = false;
      for (const char *sz : szMethods)
        bAny |= c.find(sz) != std::string::npos;
      if (!bAny || !std::regex_search(c, Command))
        continue;
      nCalls++;
      // The statement's lines, to its semicolon.
      bool bMarked = false;
      for (size_t j = i; j < Lines.size() && j < i + 8; j++)
      {
        bMarked |= Lines[j].find("barriers flushed") != std::string::npos;
        if (Code(j).find(';') != std::string::npos)
          break;
      }
      CHECK_MSG(bMarked, Name + ":" + std::to_string(i + 1) + ":" + Lines[i]);
    }
  }
  std::printf("  %d sources, %d calls of a command list's own\n", nFiles, nCalls);
  CHECK(nFiles > 20 && nCalls > 10);
}
