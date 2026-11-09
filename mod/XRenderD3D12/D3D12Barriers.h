// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: D3D12Barriers.h
//	Description: The resource barriers of a command list gathered and given
//	to it in batches (CBarrierTracker; the unit tests). Without the
//	renderer's types: a resource is an opaque pointer, never called.
//
//	The requests queue up and are given as one ResourceBarrier call
//	where the list needs them: before a draw, a dispatch, a copy, a clear
//	or the list's end (Flush). In a batch a resource has one record, the
//	state it had at the last call and the state it is wanted in now, so
//	that A -> B -> C is given as A -> C, a round trip through other states
//	back to a read state is nothing and back to UNORDERED_ACCESS is a UAV
//	barrier; no two transitions of one subresource ever share a call (the
//	debug layer refuses them). What cannot be one record (a round trip
//	back to a write state other than UNORDERED_ACCESS: two copies into a
//	resource are ordered only by the transitions between them; a
//	transition whose before is not the queued state; the whole resource
//	and one of its subresources of a resource whose subresources are not
//	known) closes the batch and starts another: two calls in a row.
//
//	A resource may be registered (Register): its state is then kept from
//	batch to batch, Require(state) asks for it without naming the state
//	before, a read state that holds the wanted one already does (and two
//	shader reads combine), and its UAV barriers are left out where no work
//	was recorded since its last barrier. A buffer's state decays to
//	COMMON when its list has run (OnListExecuted); the next list gives it
//	its state back by an explicit transition (OnListOpened), so that no
//	implicit promotion goes by the tracker. UAV barriers are never added
//	on their own: two dispatches may write different mip levels of a
//	texture without one between them.
//
//	The Immediate mode gives every request as it came, in a call of its
//	own (the renderer's barriers before the tracker).
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_D3D12BARRIERS_H
#define XRENDERD3D12_D3D12BARRIERS_H

#include <d3d12.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Barriers
{
  constexpr UINT ALL = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

  enum EMode
  {
    MODE_IMMEDIATE = 0,  //!< every request as it came, a call of its own
    MODE_BATCHED = 1,    //!< gathered, merged, given before the next work
  };

  //! The states a resource is written in; the others are read (COMMON is neither: nothing uses a resource in it).
  constexpr D3D12_RESOURCE_STATES WRITE_STATES =
    D3D12_RESOURCE_STATE_RENDER_TARGET | D3D12_RESOURCE_STATE_UNORDERED_ACCESS | D3D12_RESOURCE_STATE_DEPTH_WRITE |
    D3D12_RESOURCE_STATE_STREAM_OUT | D3D12_RESOURCE_STATE_COPY_DEST | D3D12_RESOURCE_STATE_RESOLVE_DEST;
  //! The read states Require combines (a texture read by pixel and other shaders, a depth read as depth and texture);
  //! the others (a copy's or a resolve's source, indirect arguments) are given as they are asked for.
  constexpr D3D12_RESOURCE_STATES SHADER_READS =
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_DEPTH_READ;

  inline bool IsWrite(D3D12_RESOURCE_STATES s) { return (s & WRITE_STATES) != 0; }
  inline bool IsShaderRead(D3D12_RESOURCE_STATES s) { return s != 0 && (s & ~SHADER_READS) == 0; }
  //! A write state together with another state (the debug layer refuses it).
  inline bool IsMixedWrite(D3D12_RESOURCE_STATES s) { return IsWrite(s) && (s & (s - 1)) != 0; }

  //! What the batches gave, counted until ResetStats.
  struct SStats
  {
    uint32_t nRequests = 0;    //!< Transition, Require, Uav, UavAll (and each barrier of Barriers)
    uint32_t nCalls = 0;       //!< ResourceBarrier calls
    uint32_t nDrains = 0;      //!< calls with work recorded in the list since the call before (each waits for it)
    uint32_t nTransitions = 0; //!< transition barriers
    uint32_t nUav = 0;         //!< UAV barriers of a resource
    uint32_t nUavAll = 0;      //!< UAV barriers of all resources
    uint32_t nOther = 0;       //!< aliasing barriers (Barriers)
  };

  //! What the check finds (Problems; the renderer logs them with d3d12_BarrierCheck).
  enum EProblem
  {
    PROBLEM_BEFORE,            //!< a transition's before is not the state the resource has (or is queued to have)
    PROBLEM_SAME_STATE,        //!< a transition to the state it names as before (left out)
    PROBLEM_NOT_REGISTERED,    //!< Require of a resource that was not registered (left out)
    PROBLEM_SUBRESOURCE,       //!< a subresource beyond a registered resource's (left out)
    PROBLEM_MIXED_WRITE,       //!< a write state with another state
    PROBLEM_PENDING_AT_EXECUTE,//!< barriers queued when the list was executed (OnListExecuted)
    PROBLEM_ASSUME_PENDING,    //!< Assume of a resource with a queued transition (dropped)
    PROBLEM_TRACKED_BEFORE,    //!< a transition naming its before of a registered resource (SetTrackedBefores: Require it)
  };
  struct SProblem
  {
    EProblem eKind;
    ID3D12Resource *pRes;
    UINT nSub;
    D3D12_RESOURCE_STATES Expected;  //!< the tracked state (PROBLEM_BEFORE)
    D3D12_RESOURCE_STATES Got;       //!< what the request named
    const char *szContext;           //!< SetContext's at the request (the renderer: its GPU section)
  };

  class CBarrierTracker
  {
  public:
    void SetMode(EMode eMode) { m_eMode = eMode; }
    EMode GetMode() const { return m_eMode; }

    // Registered resources ------------------------------------------------------------------------------------------

    //! A resource whose state is kept: nSubresources of them (a buffer one), all in Initial. Registering it again
    //! overwrites it (an address the heap gave again).
    void Register(ID3D12Resource *pRes, D3D12_RESOURCE_STATES Initial, UINT nSubresources = 1, bool bBuffer = false)
    {
      if (!pRes)
        return;
      m_nChanges++;
      SRecord &r = m_Records[pRes];
      r.State.assign(nSubresources ? nSubresources : 1, Initial);
      r.bBuffer = bBuffer;
      r.Decayed = D3D12_RESOURCE_STATE_COMMON;
      r.nSyncEpoch = m_nEpoch;
      for (SEntry &e : m_Entries)
        if (e.pRes == pRes)
          Detach(e);  // queued before as another resource: given as queued, not kept
    }
    //! A released resource forgotten (its queued barriers are still given: it lives until the GPU is done with it).
    void Forget(ID3D12Resource *pRes)
    {
      for (auto jt = m_Census.begin(); jt != m_Census.end();)
        jt = jt->first.first == pRes ? m_Census.erase(jt) : std::next(jt);
      m_CensusIds.erase(pRes);
      auto it = m_Records.find(pRes);
      if (it == m_Records.end())
        return;
      m_nChanges++;
      for (SEntry &e : m_Entries)
        if (e.pRec == &it->second)
          Detach(e);
      m_Records.erase(it);
    }
    bool IsRegistered(ID3D12Resource *pRes) const { return m_Records.count(pRes) != 0; }
    //! A registered resource's state as queued (nSub ALL: the first subresource's), COMMON when not registered.
    D3D12_RESOURCE_STATES GetState(ID3D12Resource *pRes, UINT nSub = ALL) const
    {
      for (const SEntry &e : m_Entries)
        if (e.pRes == pRes && e.pRec)
          return e.Subs[nSub == ALL || nSub >= e.Subs.size() ? 0 : nSub].Want;
      auto it = m_Records.find(pRes);
      if (it == m_Records.end())
        return D3D12_RESOURCE_STATE_COMMON;
      return it->second.State[nSub == ALL || nSub >= it->second.State.size() ? 0 : nSub];
    }

    //! A registered resource wanted in After (all subresources or one): nothing where a read state that holds After it
    //! has; two shader reads combine. bExact: in After alone (a read state narrowed too).
    void Require(ID3D12Resource *pRes, D3D12_RESOURCE_STATES After, UINT nSub = ALL, bool bExact = false)
    {
      m_Stats.nRequests++;
      if (!pRes)
        return;
      SRecord *pRec = Find(pRes);
      if (!pRec)
      {
        Problem(PROBLEM_NOT_REGISTERED, pRes, nSub, D3D12_RESOURCE_STATE_COMMON, After);
        return;
      }
      if (IsMixedWrite(After))
        Problem(PROBLEM_MIXED_WRITE, pRes, nSub, D3D12_RESOURCE_STATE_COMMON, After);
      if (!CheckSub(pRec, pRes, nSub))
        return;
      SEntry *pE = FindEntry(pRes);
      if (pE && pE->pRec != pRec)
        pE = nullptr;  // queued before it was registered (Entry closes that batch)
      const UINT n0 = nSub == ALL ? 0 : nSub, n1 = nSub == ALL ? (UINT)pRec->State.size() : nSub + 1;
      // The state each subresource is to have: a read state that holds After already does, two shader reads combine.
      bool bAny = false, bFits = true;
      for (UINT i = n0; i < n1; i++)
      {
        const D3D12_RESOURCE_STATES Cur = pE ? pE->Subs[i].Want : pRec->State[i], Want = bExact ? After : Combined(Cur, After);
        bAny |= Want != Cur;
        bFits &= !pE || Fits(pE->Subs[i], Want);
      }
      if (!bAny)
        return;
      m_nChanges++;
      if (!bFits)
        Seal();  // a round trip back to a write state: what is queued first, then this
      SEntry &e = Entry(pRes, pRec);
      for (UINT i = n0; i < n1; i++)
        Want(e, i, bExact ? After : Combined(e.Subs[i].Want, After));
      if (m_eMode == MODE_IMMEDIATE)
        Seal();
    }

    //! A third party left a resource in State: no barrier (one not registered: what the census knows of it).
    void Assume(ID3D12Resource *pRes, D3D12_RESOURCE_STATES State)
    {
      SRecord *pRec = Find(pRes);
      if (!pRec)
      {
        if (m_bCensus)
        {
          CensusIdentity(pRes);
          for (auto jt = m_Census.begin(); jt != m_Census.end();)
            jt = jt->first.first == pRes ? m_Census.erase(jt) : std::next(jt);
          m_Census[std::make_pair(pRes, ALL)] = { State, !m_pIsBuffer || m_pIsBuffer(pRes) };
        }
        return;
      }
      if (FindEntry(pRes))
      {
        // Queued and not given before the third party's work: given now, in a call of its own, then the state it left.
        Problem(PROBLEM_ASSUME_PENDING, pRes, ALL, D3D12_RESOURCE_STATE_COMMON, State);
        Seal();
      }
      m_nChanges++;
      pRec->State.assign(pRec->State.size(), State);
      pRec->nSyncEpoch = m_nEpoch;
    }

    // Any resource --------------------------------------------------------------------------------------------------

    //! A transition from the state the caller names (the renderer's barriers): for a registered resource that is
    //! checked against its state (PROBLEM_BEFORE) and the transition made from its state; Before == After is left out.
    void Transition(ID3D12Resource *pRes, D3D12_RESOURCE_STATES Before, D3D12_RESOURCE_STATES After, UINT nSub = ALL)
    {
      if (!m_bAgain)
        m_Stats.nRequests++;
      if (!pRes)
        return;
      if (IsMixedWrite(After) && !m_bAgain)
        Problem(PROBLEM_MIXED_WRITE, pRes, nSub, Before, After);
      SRecord *pRec = Find(pRes);
      if (m_eMode == MODE_IMMEDIATE && !pRec)
      {
        // As it came (Before == After too: the renderer's barriers before the tracker).
        Census(pRes, nSub, Before, After);
        Seal();
        m_Sealed.push_back(TransitionBarrier(pRes, nSub, Before, After));
        m_SealedSizes.push_back(1);
        return;
      }
      if (Before == After)
      {
        Problem(PROBLEM_SAME_STATE, pRes, nSub, Before, After);
        return;
      }
      if (pRec)
      {
        if (m_bTrackedBefores && !m_bAgain)
          Problem(PROBLEM_TRACKED_BEFORE, pRes, nSub, GetState(pRes, nSub), Before);
        if (!CheckSub(pRec, pRes, nSub))
          return;
        m_nChanges++;
        SEntry *pE = &Entry(pRes, pRec);
        const UINT n0 = nSub == ALL ? 0 : nSub, n1 = nSub == ALL ? (UINT)pE->Subs.size() : nSub + 1;
        bool bFits = true;
        for (UINT i = n0; i < n1; i++)
          bFits &= Fits(pE->Subs[i], After);
        for (UINT i = n0; i < n1; i++)
          if (pE->Subs[i].Want != Before)
          {
            Problem(PROBLEM_BEFORE, pRes, nSub, pE->Subs[i].Want, Before);
            break;
          }
        if (!bFits)
        {
          Seal();
          pE = &Entry(pRes, pRec);
        }
        for (UINT i = n0; i < n1; i++)
          Want(*pE, i, After);
        if (m_eMode == MODE_IMMEDIATE)
          Seal();
        return;
      }
      Census(pRes, nSub, Before, After);
      // Not registered: the record of this batch, from the state named.
      SEntry *pE = FindEntry(pRes);
      if (!pE)
      {
        SEntry &e = NewEntry(pRes, nullptr);
        e.bWhole = nSub == ALL;
        e.Subs.push_back({ nSub, Before, Before });
        Want(e, D3D12_RESOURCE_STATE_COMMON, After);
        return;
      }
      SEntry &e = *pE;
      if (e.Subs.empty())
        e.bWhole = nSub == ALL;  // a UAV barrier queued only
      else if (e.bWhole != (nSub == ALL))
      {
        // The whole resource and one of its subresources (how many it has is not known here).
        SealAndTransition(pRes, Before, After, nSub);
        return;
      }
      size_t i = 0;
      while (i < e.Subs.size() && e.Subs[i].nSub != nSub)
        i++;
      if (i == e.Subs.size())
      {
        e.Subs.push_back({ nSub, Before, Before });
        Want(e, i, After);
        return;
      }
      if (e.Subs[i].Want != Before)
      {
        // Not the state queued: given as it came, after what is queued (the renderer's barriers before the tracker).
        Problem(PROBLEM_BEFORE, pRes, nSub, e.Subs[i].Want, Before);
        SealAndTransition(pRes, Before, After, nSub);
        return;
      }
      if (!Fits(e.Subs[i], After))
        SealAndTransition(pRes, Before, After, nSub);
      else
        Want(e, i, After);
    }

    //! The writes to a resource through its UAVs done before what comes after (a null resource: nothing).
    void Uav(ID3D12Resource *pRes)
    {
      m_Stats.nRequests++;
      if (!pRes)
        return;
      if (m_eMode == MODE_IMMEDIATE)
      {
        Seal();
        D3D12_RESOURCE_BARRIER b = {};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        b.UAV.pResource = pRes;
        m_Sealed.push_back(b);
        m_SealedSizes.push_back(1);
        SRecord *pRec = Find(pRes);
        if (pRec)
          pRec->nSyncEpoch = m_nEpoch;
        return;
      }
      if (m_bUavAll)
        return;  // all of them
      SEntry *pE = FindEntry(pRes);
      if (pE)
      {
        if (pE->bUav)
          return;
        // A transition of the whole resource into or out of UNORDERED_ACCESS waits for its writes.
        bool bAll = pE->bWhole || (pE->pRec && pE->Subs.size() > 0), bThrough = bAll;
        for (const SSub &s : pE->Subs)
          if (s.Want == s.Had || !(s.Want == D3D12_RESOURCE_STATE_UNORDERED_ACCESS || s.Had == D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
            bThrough = false;
        if (bThrough)
          return;
      }
      SRecord *pRec = Find(pRes);
      // No work since its last barrier (or since all of them were waited for).
      if (m_nEpoch == std::max(pRec ? pRec->nSyncEpoch : 0, m_nUavAllEpoch) && (pRec || m_bUavAllSeen))
        return;
      SEntry &e = pE ? *pE : NewEntry(pRes, pRec);
      e.bUav = true;
    }

    //! The writes through UAVs of all resources done before what comes after.
    void UavAll()
    {
      m_Stats.nRequests++;
      if (m_eMode == MODE_IMMEDIATE)
      {
        Seal();
        D3D12_RESOURCE_BARRIER b = {};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        m_Sealed.push_back(b);
        m_SealedSizes.push_back(1);
        m_nUavAllEpoch = m_nEpoch;
        m_bUavAllSeen = true;
        return;
      }
      if (m_bUavAll || (m_bUavAllSeen && m_nEpoch == m_nUavAllEpoch))
        return;
      m_bUavAll = true;
    }

    //! Barriers given as they are in a call of their own, after what is queued, in either mode (a library's or a
    //! pass's own array): the registered states follow them, the census checks their befores.
    void Verbatim(const D3D12_RESOURCE_BARRIER *pBarriers, UINT n)
    {
      Seal();
      for (UINT i = 0; i < n; i++)
      {
        m_Stats.nRequests++;
        const D3D12_RESOURCE_BARRIER &b = pBarriers[i];
        if (b.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)
        {
          SRecord *pRec = Find(b.Transition.pResource);
          if (pRec && m_bTrackedBefores)
            Problem(PROBLEM_TRACKED_BEFORE, b.Transition.pResource, b.Transition.Subresource, GetState(b.Transition.pResource, b.Transition.Subresource),
                    b.Transition.StateBefore);
          if (pRec)
            m_nChanges++;
          if (pRec && CheckSub(pRec, b.Transition.pResource, b.Transition.Subresource))
            for (UINT s = 0; s < pRec->State.size(); s++)
              if (b.Transition.Subresource == ALL || b.Transition.Subresource == s)
                pRec->State[s] = b.Transition.StateAfter;
          if (!pRec)
            Census(b.Transition.pResource, b.Transition.Subresource, b.Transition.StateBefore, b.Transition.StateAfter);
        }
        else if (b.Type == D3D12_RESOURCE_BARRIER_TYPE_UAV && !b.UAV.pResource)
        {
          m_nUavAllEpoch = m_nEpoch;
          m_bUavAllSeen = true;
        }
        m_Sealed.push_back(b);
      }
      if (n)
        m_SealedSizes.push_back(n);
    }

    //! Barriers given as an array (the immediate mode: as they are, in one call).
    void Barriers(const D3D12_RESOURCE_BARRIER *pBarriers, UINT n)
    {
      if (m_eMode == MODE_IMMEDIATE)
      {
        Verbatim(pBarriers, n);
        return;
      }
      for (UINT i = 0; i < n; i++)
      {
        const D3D12_RESOURCE_BARRIER &b = pBarriers[i];
        if (b.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)
          Transition(b.Transition.pResource, b.Transition.StateBefore, b.Transition.StateAfter, b.Transition.Subresource);
        else if (b.Type == D3D12_RESOURCE_BARRIER_TYPE_UAV)
        {
          if (b.UAV.pResource)
            Uav(b.UAV.pResource);
          else
            UavAll();
        }
        else
        {
          m_Stats.nRequests++;
          Seal();
          m_Sealed.push_back(b);
          m_SealedSizes.push_back(1);
        }
      }
    }

    // The list --------------------------------------------------------------------------------------------------------

    bool HasPending() const
    {
      if (m_bUavAll || !m_SealedSizes.empty())
        return true;
      for (const SEntry &e : m_Entries)
      {
        if (e.bUav)
          return true;
        for (const SSub &s : e.Subs)
          if (s.Had != s.Want || s.bTrip)
            return true;
      }
      return false;
    }

    //! What is queued, given: Emit(UINT n, const D3D12_RESOURCE_BARRIER *p) once for each call. bWork: work is recorded
    //! after it (a draw, a dispatch, a copy), which the next UAV barriers wait for.
    template <class F> void Flush(F &&Emit, bool bWork)
    {
      if (m_Entries.empty() && !m_bUavAll && m_SealedSizes.empty())
      {
        // Nothing queued (the draws' and dispatches' usual case).
        if (bWork)
          m_nEpoch++;
        return;
      }
      Seal();
      size_t nAt = 0;
      for (size_t nSize : m_SealedSizes)
      {
        m_Stats.nCalls++;
        if (m_nEpoch != m_nCallEpoch)
          m_Stats.nDrains++;
        m_nCallEpoch = m_nEpoch;
        for (size_t i = nAt; i < nAt + nSize; i++)
        {
          const D3D12_RESOURCE_BARRIER &b = m_Sealed[i];
          if (b.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)
            m_Stats.nTransitions++;
          else if (b.Type == D3D12_RESOURCE_BARRIER_TYPE_UAV)
            (b.UAV.pResource ? m_Stats.nUav : m_Stats.nUavAll)++;
          else
            m_Stats.nOther++;
        }
        Emit((UINT)nSize, m_Sealed.data() + nAt);
        nAt += nSize;
      }
      m_Sealed.clear();
      m_SealedSizes.clear();
      if (bWork)
        m_nEpoch++;
    }

    //! The list was executed: the registered buffers' states decayed to COMMON (and the census forgets: a buffer's state
    //! is not what it was set to).
    void OnListExecuted()
    {
      if (HasPending())
        Problem(PROBLEM_PENDING_AT_EXECUTE, nullptr, ALL, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON);
      m_nChanges++;
      m_Entries.clear();
      m_bUavAll = false;
      m_Sealed.clear();
      m_SealedSizes.clear();
      // The next list's first call waits for no work of its own list (the UAV barriers still count the work before).
      m_nCallEpoch = m_nEpoch;
      for (auto &it : m_Records)
        if (it.second.bBuffer && it.second.State[0] != D3D12_RESOURCE_STATE_COMMON)
        {
          it.second.Decayed = it.second.State[0];
          it.second.State[0] = D3D12_RESOURCE_STATE_COMMON;
        }
      // The census keeps the textures' states (they do not decay); without a way to tell, it forgets them all.
      for (auto jt = m_Census.begin(); jt != m_Census.end();)
        jt = jt->second.bBuffer ? m_Census.erase(jt) : std::next(jt);
    }
    //! A list was opened: the registered buffers that decayed given their states back by explicit transitions (queued).
    void OnListOpened()
    {
      for (auto &it : m_Records)
        if (it.second.bBuffer && it.second.Decayed != D3D12_RESOURCE_STATE_COMMON)
        {
          const D3D12_RESOURCE_STATES s = it.second.Decayed;
          it.second.Decayed = D3D12_RESOURCE_STATE_COMMON;
          Want(Entry(it.first, &it.second), 0, s);
          if (m_eMode == MODE_IMMEDIATE)
            Seal();
        }
    }

    // Statistics and the check ------------------------------------------------------------------------------------------

    const SStats &Stats() const { return m_Stats; }
    void ResetStats() { m_Stats = {}; }
    //! Counts the changes of the registered states (each request that queued one, Register, Forget, Assume, the list's
    //! end): the same count, the same states (CBoundTargets asks again only when it moved).
    uint64_t GetChanges() const { return m_nChanges; }
    //! The census of the states the renderer's own transitions name (not registered resources): a before that is not
    //! the state the last transition of the resource gave is PROBLEM_BEFORE (on: d3d12_BarrierCheck).
    //! pIsBuffer tells a buffer (whose state decays when its list has run) from a texture; without it the census forgets
    //! everything at the end of a list.
    //! pIdentity tells one resource from another made at the same address after it was released (one released without
    //! Forget): a resource whose identity is not the one the census saw at its address is new, what was kept of the one
    //! before is dropped.
    using FIsBuffer = bool (*)(ID3D12Resource *);
    using FIdentity = uint64_t (*)(ID3D12Resource *);
    void SetCensus(bool bOn, FIsBuffer pIsBuffer = nullptr, FIdentity pIdentity = nullptr)
    {
      m_bCensus = bOn;
      m_pIsBuffer = pIsBuffer;
      m_pIdentity = pIdentity;
      if (!bOn)
      {
        m_Census.clear();
        m_CensusIds.clear();
      }
    }
    //! On: a transition that names its before of a registered resource (Transition, Barriers, Verbatim) is
    //! PROBLEM_TRACKED_BEFORE, still made from the state kept. The renderer names befores only of the resources it does
    //! not register (d3d12_BarrierCheck finds the others).
    void SetTrackedBefores(bool bOn) { m_bTrackedBefores = bOn; }
    //! The problems found since the last TakeProblems (at most MAX_PROBLEMS of them kept).
    std::vector<SProblem> TakeProblems() { return std::exchange(m_Problems, {}); }
    static constexpr size_t MAX_PROBLEMS = 256;
    //! Where the requests from here on come from (a string with static storage, kept in the problems).
    void SetContext(const char *szContext) { m_szContext = szContext; }

  private:
    struct SRecord
    {
      std::vector<D3D12_RESOURCE_STATES> State;  //!< each subresource's, as given
      bool bBuffer = false;
      D3D12_RESOURCE_STATES Decayed = D3D12_RESOURCE_STATE_COMMON;  //!< its state before it decayed (OnListExecuted)
      uint64_t nSyncEpoch = 0;  //!< the work epoch of its last barrier
    };
    struct SSub
    {
      UINT nSub;
      D3D12_RESOURCE_STATES Had;   //!< at the last call
      D3D12_RESOURCE_STATES Want;  //!< as queued
      bool bTrip = false;          //!< went through another state and back to UNORDERED_ACCESS
    };
    struct SEntry
    {
      ID3D12Resource *pRes = nullptr;
      SRecord *pRec = nullptr;  //!< registered: Subs one each of its subresources
      bool bWhole = true;       //!< not registered: one SSub of all subresources, else SSubs of single ones
      bool bUav = false;
      std::vector<SSub> Subs;
    };

    SRecord *Find(ID3D12Resource *pRes)
    {
      auto it = m_Records.find(pRes);
      return it == m_Records.end() ? nullptr : &it->second;
    }
    SEntry *FindEntry(ID3D12Resource *pRes)
    {
      for (SEntry &e : m_Entries)
        if (e.pRes == pRes)
          return &e;
      return nullptr;
    }
    SEntry &NewEntry(ID3D12Resource *pRes, SRecord *pRec)
    {
      m_Entries.emplace_back();
      SEntry &e = m_Entries.back();
      e.pRes = pRes;
      e.pRec = pRec;
      if (pRec)
      {
        e.bWhole = false;
        for (UINT i = 0; i < (UINT)pRec->State.size(); i++)
          e.Subs.push_back({ i, pRec->State[i], pRec->State[i] });
      }
      return e;
    }
    //! A registered resource's record of the batch kept as a resource not registered (Forget, Register again): one of
    //! the whole where its subresources go from one state to one.
    static void Detach(SEntry &e)
    {
      if (!e.pRec)
        return;
      e.pRec = nullptr;
      bool bUniform = !e.Subs.empty(), bTrip = false;
      for (const SSub &s : e.Subs)
      {
        bUniform = bUniform && s.Had == e.Subs[0].Had && s.Want == e.Subs[0].Want;
        bTrip |= s.bTrip;
      }
      e.bWhole = bUniform;
      if (bUniform)
        e.Subs = { { ALL, e.Subs[0].Had, e.Subs[0].Want, bTrip } };
    }
    //! A registered resource's record of the batch.
    SEntry &Entry(ID3D12Resource *pRes, SRecord *pRec)
    {
      SEntry *pE = FindEntry(pRes);
      if (pE && pE->pRec == pRec)
        return *pE;
      if (pE)
        Seal();  // queued before it was registered: that first
      return NewEntry(pRes, pRec);
    }
    bool CheckSub(SRecord *pRec, ID3D12Resource *pRes, UINT nSub)
    {
      if (nSub == ALL || nSub < pRec->State.size())
        return true;
      Problem(PROBLEM_SUBRESOURCE, pRes, nSub, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON);
      return false;
    }
    static D3D12_RESOURCE_STATES Combined(D3D12_RESOURCE_STATES Cur, D3D12_RESOURCE_STATES After)
    {
      if (Cur != 0 && !IsWrite(Cur) && !IsWrite(After) && (Cur & After) == After)
        return Cur;
      if (IsShaderRead(Cur) && IsShaderRead(After))
        return Cur | After;
      return After;
    }
    //! Whether a subresource queued to be After stays one record: not where that is a round trip back to a write state
    //! other than UNORDERED_ACCESS (two writes in it are ordered only by the transitions between them).
    static bool Fits(const SSub &s, D3D12_RESOURCE_STATES After)
    {
      return !(After == s.Had && s.Want != s.Had && IsWrite(s.Had) && s.Had != D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    //! Subresource i of e queued to be After (Fits).
    static void Want(SEntry &e, size_t i, D3D12_RESOURCE_STATES After)
    {
      SSub &s = e.Subs[i];
      if (After == s.Had && s.Want != s.Had && s.Had == D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
        s.bTrip = true;
      s.Want = After;
    }
    //! The queue sealed and the transition queued again after it: counted, checked and seen by the census once.
    void SealAndTransition(ID3D12Resource *pRes, D3D12_RESOURCE_STATES Before, D3D12_RESOURCE_STATES After, UINT nSub)
    {
      Seal();
      m_bAgain = true;
      Transition(pRes, Before, After, nSub);
      m_bAgain = false;
    }

    //! The batch queued so far closed into a call of its own (sealed): its barriers made, the registered states kept.
    void Seal()
    {
      const size_t nStart = m_Sealed.size();
      if (m_bUavAll)
      {
        D3D12_RESOURCE_BARRIER b = {};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        m_Sealed.push_back(b);
        m_nUavAllEpoch = m_nEpoch;
        m_bUavAllSeen = true;
      }
      for (SEntry &e : m_Entries)
      {
        bool bGiven = false;
        // A registered resource's subresources all from one state to one: one barrier of the whole.
        bool bUniform = e.pRec && !e.Subs.empty();
        for (const SSub &s : e.Subs)
          bUniform = bUniform && s.Had == e.Subs[0].Had && s.Want == e.Subs[0].Want;
        if (bUniform && e.Subs[0].Had != e.Subs[0].Want)
        {
          m_Sealed.push_back(TransitionBarrier(e.pRes, ALL, e.Subs[0].Had, e.Subs[0].Want));
          bGiven = true;
        }
        else
          for (const SSub &s : e.Subs)
            if (s.Had != s.Want)
            {
              m_Sealed.push_back(TransitionBarrier(e.pRes, e.pRec || !e.bWhole ? s.nSub : ALL, s.Had, s.Want));
              bGiven = true;
            }
        // A round trip back to UNORDERED_ACCESS, or a UAV barrier asked for: one, unless a transition of the whole resource
        // or the barrier of all of them waits for its writes.
        bool bTrip = false, bWhole = !e.Subs.empty() && (e.pRec || e.bWhole);
        for (const SSub &s : e.Subs)
        {
          bTrip |= s.bTrip && s.Had == s.Want;
          bWhole &= s.Had != s.Want;
        }
        if (!bWhole && (bTrip || e.bUav) && !m_bUavAll)
        {
          const bool bNeeded = !e.bUav && e.pRec ? m_nEpoch != std::max(e.pRec->nSyncEpoch, m_bUavAllSeen ? m_nUavAllEpoch : 0) : true;
          if (bNeeded || e.bUav)
          {
            D3D12_RESOURCE_BARRIER b = {};
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            b.UAV.pResource = e.pRes;
            m_Sealed.push_back(b);
            bGiven = true;
          }
        }
        if (e.pRec)
        {
          for (const SSub &s : e.Subs)
            e.pRec->State[s.nSub] = s.Want;
          if (bGiven || m_bUavAll)
            e.pRec->nSyncEpoch = m_nEpoch;
        }
      }
      m_Entries.clear();
      m_bUavAll = false;
      if (m_Sealed.size() > nStart)
        m_SealedSizes.push_back(m_Sealed.size() - nStart);
    }

    static D3D12_RESOURCE_BARRIER TransitionBarrier(ID3D12Resource *pRes, UINT nSub, D3D12_RESOURCE_STATES Before, D3D12_RESOURCE_STATES After)
    {
      D3D12_RESOURCE_BARRIER b = {};
      b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      b.Transition.pResource = pRes;
      b.Transition.Subresource = nSub;
      b.Transition.StateBefore = Before;
      b.Transition.StateAfter = After;
      return b;
    }
    void Problem(EProblem eKind, ID3D12Resource *pRes, UINT nSub, D3D12_RESOURCE_STATES Expected, D3D12_RESOURCE_STATES Got)
    {
      if (m_Problems.size() < MAX_PROBLEMS)
        m_Problems.push_back({ eKind, pRes, nSub, Expected, Got, m_szContext });
    }
    //! The census of a transition of a resource not registered.
    void Census(ID3D12Resource *pRes, UINT nSub, D3D12_RESOURCE_STATES Before, D3D12_RESOURCE_STATES After)
    {
      if (!m_bCensus || m_bAgain)
        return;
      CensusIdentity(pRes);
      const auto Key = std::make_pair(pRes, nSub);
      auto it = m_Census.find(Key);
      if (nSub == ALL)
      {
        // The whole resource: what its single subresources were set to is overwritten.
        for (auto jt = m_Census.begin(); jt != m_Census.end();)
          if (jt->first.first == pRes && jt->first.second != ALL)
          {
            if (jt->second.State != Before)
              Problem(PROBLEM_BEFORE, pRes, jt->first.second, jt->second.State, Before);
            jt = m_Census.erase(jt);
          }
          else
            ++jt;
      }
      else if (it == m_Census.end())
        it = m_Census.find(std::make_pair(pRes, ALL));  // a single subresource of a resource set as a whole
      if (it != m_Census.end() && it->second.State != Before)
        Problem(PROBLEM_BEFORE, pRes, nSub, it->second.State, Before);
      // Asked while the resource is alive (a key may outlive it: the list's end does not look at the resource).
      m_Census[Key] = { After, !m_pIsBuffer || m_pIsBuffer(pRes) };
    }

    //! Another resource at pRes' address than the census saw there: what it kept of that one dropped.
    void CensusIdentity(ID3D12Resource *pRes)
    {
      if (!m_pIdentity)
        return;
      const uint64_t nId = m_pIdentity(pRes);
      const auto [it, bNew] = m_CensusIds.try_emplace(pRes, nId);
      if (bNew || it->second == nId)
        return;
      it->second = nId;
      for (auto jt = m_Census.begin(); jt != m_Census.end();)
        jt = jt->first.first == pRes ? m_Census.erase(jt) : std::next(jt);
    }

    struct SKeyHash
    {
      size_t operator()(const std::pair<ID3D12Resource *, UINT> &k) const { return std::hash<const void *>()(k.first) ^ ((size_t)k.second * 0x9e3779b97f4a7c15ull); }
    };

    EMode m_eMode = MODE_IMMEDIATE;
    std::unordered_map<ID3D12Resource *, SRecord> m_Records;
    std::vector<SEntry> m_Entries;                 //!< the batch being queued
    bool m_bUavAll = false;                        //!< a UAV barrier of all resources queued
    std::vector<D3D12_RESOURCE_BARRIER> m_Sealed;  //!< the closed batches, one call each
    std::vector<size_t> m_SealedSizes;
    uint64_t m_nEpoch = 1;                         //!< counts the flushes before work
    uint64_t m_nCallEpoch = 1;                     //!< m_nEpoch at the last call
    uint64_t m_nUavAllEpoch = 0;                   //!< m_nEpoch at the last UAV barrier of all resources
    bool m_bUavAllSeen = false;
    uint64_t m_nChanges = 0;                       //!< GetChanges
    SStats m_Stats;
    std::vector<SProblem> m_Problems;
    const char *m_szContext = nullptr;
    bool m_bCensus = false;
    bool m_bTrackedBefores = false;  //!< SetTrackedBefores
    bool m_bAgain = false;  //!< in SealAndTransition: the transition given again, checked once already
    FIsBuffer m_pIsBuffer = nullptr;
    struct SCensus
    {
      D3D12_RESOURCE_STATES State;
      bool bBuffer;  //!< forgotten when the list has run (without m_pIsBuffer: everything)
    };
    std::unordered_map<std::pair<ID3D12Resource *, UINT>, SCensus, SKeyHash> m_Census;
    FIdentity m_pIdentity = nullptr;
    std::unordered_map<ID3D12Resource *, uint64_t> m_CensusIds;  //!< the identity the census saw at each address
  };

  //! The render targets and the depth target bound to a list (OMSetRenderTargets) whose states a tracker keeps: Require
  //! gives each the state it is drawn in before a draw or a clear, so that a pass that made a bound target readable (a
  //! kernel read the depth buffer) and drew on gets it back as a target; nothing where the tracker's states did not
  //! change since (GetChanges). A depth target bound read only (DEPTH_READ) stays in DEPTH_WRITE or in a read state
  //! that holds DEPTH_READ (a read only view of a writable depth buffer draws as well).
  class CBoundTargets
  {
  public:
    static constexpr int MAX = 10;  //!< 8 colour targets, a second's room, a depth target

    //! Nothing bound (OMSetRenderTargets of other targets follows: Add them).
    void Clear()
    {
      m_n = 0;
      m_bChecked = false;
    }
    //! pRes (one subresource or all) bound, drawn in State: RENDER_TARGET, DEPTH_WRITE or DEPTH_READ (read only).
    void Add(ID3D12Resource *pRes, D3D12_RESOURCE_STATES State, UINT nSub = ALL)
    {
      if (!pRes || m_n >= MAX)
        return;
      m_Bound[m_n++] = { pRes, nSub, State };
      m_bChecked = false;
    }
    //! A released resource no longer looked at (its address may come again).
    void Remove(ID3D12Resource *pRes)
    {
      for (int i = 0; i < m_n;)
        if (m_Bound[i].pRes == pRes)
          m_Bound[i] = m_Bound[--m_n];
        else
          i++;
    }
    int Count() const { return m_n; }
    //! Before a draw or a clear: the bound targets the tracker keeps wanted in their states (queued).
    void Require(CBarrierTracker &Tracker)
    {
      if (m_bChecked && m_nChecked == Tracker.GetChanges())
        return;
      for (int i = 0; i < m_n; i++)
      {
        const SBound &b = m_Bound[i];
        if (!Tracker.IsRegistered(b.pRes))
          continue;
        if (b.State == D3D12_RESOURCE_STATE_DEPTH_READ)
        {
          const D3D12_RESOURCE_STATES s = Tracker.GetState(b.pRes, b.nSub);
          if (s == D3D12_RESOURCE_STATE_DEPTH_WRITE || (s & D3D12_RESOURCE_STATE_DEPTH_READ))
            continue;
        }
        Tracker.Require(b.pRes, b.State, b.nSub);
      }
      m_bChecked = true;
      m_nChecked = Tracker.GetChanges();
    }

  private:
    struct SBound
    {
      ID3D12Resource *pRes;
      UINT nSub;
      D3D12_RESOURCE_STATES State;
    };
    SBound m_Bound[MAX] = {};
    int m_n = 0;
    bool m_bChecked = false;
    uint64_t m_nChecked = 0;
  };
}

#endif
