// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: CascadeGroups.h
//	Description: The casters of the sun's cascades that draw alike
//	(d3d12_CascadeInstancing 3, Cascades.cpp): the parts of the same
//	mesh, chunk, shader, resources and class, which differ only in their
//	objects' matrices, grouped so that each group is drawn once, an
//	instance per object and slice. The groups are numbered in the order
//	of their first parts, the members of each kept in the parts' order;
//	the parts that have to be drawn on their own (bAlone) a group each.
//	The unit tests (CascadeGroupsTests.cpp) compare it with a map.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_CASCADEGROUPS_H
#define XRENDERD3D12_CASCADEGROUPS_H

#include "WordHash.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace CascadeGroups
{

//! What a caster's draw into the cascades takes from its part, but for its object (whole words: no padding).
struct SKey
{
  const void *pBuffer;  //!< the leaf buffer
  const void *pChunk;   //!< its chunk
  const void *pShader;
  const void *pRes;     //!< the shader's resources
  uint64_t nClass;      //!< the draw class
};
static_assert(sizeof(SKey) == 40, "SKey is hashed as whole words");

class CGrouper
{
public:
  //! Groups[i] the group of part i of n (keys pKeys, pAlone[i] != 0: a group of its own); the number of groups.
  uint32_t Group(const SKey *pKeys, const uint8_t *pAlone, size_t n, std::vector<uint32_t> &Groups)
  {
    Groups.resize(n);
    size_t nSize = 16;
    while (nSize < 2 * n)
      nSize *= 2;
    m_Slots.assign(nSize, 0);
    uint32_t nGroups = 0;
    for (size_t i = 0; i < n; i++)
    {
      if (pAlone && pAlone[i])
      {
        Groups[i] = nGroups++;
        continue;
      }
      CWordHash Hash;
      Hash.Add(pKeys[i]);
      for (size_t s = (size_t)Hash.Get() & (nSize - 1);; s = (s + 1) & (nSize - 1))
      {
        const uint32_t nFirst = m_Slots[s];
        if (!nFirst)
        {
          m_Slots[s] = (uint32_t)i + 1;
          Groups[i] = nGroups++;
          break;
        }
        if (!memcmp(&pKeys[nFirst - 1], &pKeys[i], sizeof(SKey)))
        {
          Groups[i] = Groups[nFirst - 1];
          break;
        }
      }
    }
    return nGroups;
  }

  //! The parts of group g: Members[First[g]] to Members[First[g + 1] - 1], in the parts' order.
  void Members(const std::vector<uint32_t> &Groups, uint32_t nGroups, std::vector<uint32_t> &First, std::vector<uint32_t> &Members)
  {
    First.assign((size_t)nGroups + 1, 0);
    for (uint32_t g : Groups)
      First[g + 1]++;
    for (uint32_t g = 0; g < nGroups; g++)
      First[g + 1] += First[g];
    Members.resize(Groups.size());
    m_Fill.assign(First.begin(), First.end() - 1);
    for (uint32_t i = 0; i < (uint32_t)Groups.size(); i++)
      Members[m_Fill[Groups[i]]++] = i;
  }

private:
  std::vector<uint32_t> m_Slots;  //!< open addressing: 1 + the first part of a key, 0 empty
  std::vector<uint32_t> m_Fill;   //!< Members: where each group's next part goes
};

}  // namespace CascadeGroups

#endif
