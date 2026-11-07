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
