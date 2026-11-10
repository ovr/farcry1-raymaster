// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: CascadeGroupsTests.cpp
//	Description: Unit tests of the casters' groups of the sun's cascades
//	(CascadeGroups.h, d3d12_CascadeInstancing 3): random keys with many
//	repeats and parts drawn on their own give the groups a std::map
//	gives, numbered in the order of their first parts, every part in
//	one group, the members in the parts' order.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "CascadeGroups.h"

#include <map>
#include <tuple>
#include <vector>

namespace
{
  uint64_t Next(uint64_t &nState)
  {
    nState = nState * 6364136223846793005ull + 1442695040888963407ull;
    return nState ^ (nState >> 29);
  }

  const void *Ptr(uint64_t v) { return (const void *)(uintptr_t)(0x10000 + v * 16); }
}

XR_TEST(CascadeGroups_AgreeWithMap)
{
  uint64_t nState = 379;
  CascadeGroups::CGrouper Grouper;
  for (size_t n : { 0, 1, 2, 7, 100, 1000, 6070 })
  {
    std::vector<CascadeGroups::SKey> Keys(n);
    std::vector<uint8_t> Alone(n);
    for (size_t i = 0; i < n; i++)
    {
      // Few meshes, a shader or two each: most parts repeat a key; some differ in one field only.
      const uint64_t m = Next(nState) % 40;
      Keys[i] = { Ptr(m), Ptr(m * 3 + Next(nState) % 2), Ptr(m % 5), Ptr(m % 7 + (Next(nState) % 9 == 0)), Next(nState) % 3 };
      Alone[i] = Next(nState) % 11 == 0;
    }
    std::vector<uint32_t> Groups, First, Members;
    const uint32_t nGroups = Grouper.Group(Keys.data(), Alone.data(), n, Groups);
    Grouper.Members(Groups, nGroups, First, Members);

    // The reference: a map from the key to its group, groups numbered as they first come.
    std::map<std::tuple<const void *, const void *, const void *, const void *, uint64_t>, uint32_t> Ref;
    uint32_t nRef = 0;
    bool bSame = Groups.size() == n;
    for (size_t i = 0; i < n && bSame; i++)
    {
      uint32_t g;
      if (Alone[i])
        g = nRef++;
      else
      {
        const auto k = std::make_tuple(Keys[i].pBuffer, Keys[i].pChunk, Keys[i].pShader, Keys[i].pRes, Keys[i].nClass);
        const auto Found = Ref.find(k);
        g = Found != Ref.end() ? Found->second : (Ref[k] = nRef++);
      }
      bSame = Groups[i] == g;
    }
    CHECK(bSame);
    CHECK(nGroups == nRef);
    // Every part once, in its group, in order.
    bool bMembers = First.size() == (size_t)nGroups + 1 && First[0] == 0 && First[nGroups] == n && Members.size() == n;
    for (uint32_t g = 0; g < nGroups && bMembers; g++)
    {
      bMembers = First[g + 1] > First[g];
      for (uint32_t j = First[g]; j < First[g + 1] && bMembers; j++)
        bMembers = Groups[Members[j]] == g && (j == First[g] || Members[j] > Members[j - 1]);
    }
    CHECK(bMembers);
  }
}

XR_TEST(CascadeGroups_Repeats)
{
  // 6 parts of 2 keys, one of them alone: groups { 0, 2, 5 }, { 1, 4 }, { 3 }.
  const CascadeGroups::SKey a = { Ptr(1), Ptr(2), Ptr(3), Ptr(4), 0 }, b = { Ptr(1), Ptr(2), Ptr(3), Ptr(4), 1 };
  const CascadeGroups::SKey Keys[6] = { a, b, a, a, b, a };
  const uint8_t Alone[6] = { 0, 0, 0, 1, 0, 0 };
  CascadeGroups::CGrouper Grouper;
  std::vector<uint32_t> Groups, First, Members;
  const uint32_t nGroups = Grouper.Group(Keys, Alone, 6, Groups);
  Grouper.Members(Groups, nGroups, First, Members);
  CHECK(nGroups == 3);
  CHECK((Groups == std::vector<uint32_t>{ 0, 1, 0, 2, 1, 0 }));
  CHECK((First == std::vector<uint32_t>{ 0, 3, 5, 6 }));
  CHECK((Members == std::vector<uint32_t>{ 0, 2, 5, 1, 4, 3 }));
}
