// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: DrawExtTests.cpp
//	Description: Unit tests of when a draw uploads the rest of its
//	constants (DrawExt.h, on the CPU): the default block in place of a rest
//	that is the default, and of the rest a depth-only draw never reads;
//	the draws that read it whatever it holds upload it.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "TestTypes.h"

#include "DrawExt.h"

#include <string>

using namespace XrTest;
using namespace DrawExt;

namespace
{
  SDrawExt Default()
  {
    SDrawExt e;
    SetDefault(e);
    return e;
  }

  //! The general program's draw with a pixel shader.
  SDraw General()
  {
    SDraw d;
    d.bGeneral = true;
    return d;
  }
}

// Why: the default block's bytes are those every draw started from (DefaultDrawConstants: zeros, the texture matrix the
// identity); a draw that binds it in place of its own rest reads what it would have uploaded only if they are the same.
XR_TEST(DrawExt_Default)
{
  const SDrawExt e = Default();
  const uint32 *pWords = (const uint32 *)&e;
  const size_t nMat0 = offsetof(SDrawExt, vTexMat0) / 4, nMat1 = offsetof(SDrawExt, vTexMat1) / 4 + 1;
  for (size_t w = 0; w < sizeof(SDrawExt) / 4; w++)
    CHECK_MSG(pWords[w] == (w == nMat0 || w == nMat1 ? 0x3f800000u : 0u), "word " + std::to_string(w));
  CHECK(Needs(General(), e, e) == REASON_DEFAULT);
}

// Why: the decision is by the bytes, so no member is left out: a draw whose rest differs from the default in any word
// uploads it; -0 and +0 are other bytes (the shaders may tell them apart), so the draw uploads too.
XR_TEST(DrawExt_AnyWordDiffers)
{
  const SDrawExt Def = Default();
  for (size_t w = 0; w < sizeof(SDrawExt) / 4; w++)
  {
    SDrawExt e = Def;
    ((uint32 *)&e)[w] ^= 0x80000000u;
    CHECK_MSG(Needs(General(), e, Def) == REASON_DIFFERS, "word " + std::to_string(w));
  }
  SDrawExt e = Def;
  e.vAmbient[0] = -0.0f;
  CHECK(Needs(General(), e, Def) == REASON_DIFFERS);
}

// Why: a depth-only draw of the general program has no pixel shader: only the clip position and the slice matter, and
// the vertex shader moves those by the rest only for DF2_SHADER_FX (fur, bubble) and the cascades' instances.
XR_TEST(DrawExt_DepthOnly)
{
  const SDrawExt Def = Default();
  SDrawExt Lit = Def;
  Lit.vAmbient[0] = 0.5f;
  Lit.vLightPos[0][3] = 1;
  SDraw d = General();
  d.bDepthOnly = true;
  CHECK(Needs(d, Lit, Def) == REASON_DEFAULT);
  CHECK(Needs(d, Def, Def) == REASON_DEFAULT);
  d.nFlags2 = DF2_SHADER_FX;
  CHECK(Needs(d, Lit, Def) == REASON_VERTEX);
  CHECK(Needs(d, Def, Def) == REASON_VERTEX);
  d.nFlags2 = DF2_HEAT;
  CHECK(Needs(d, Lit, Def) == REASON_DEFAULT);
  d.nFlags2 = 0;
  d.bCascadeInstances = true;
  CHECK(Needs(d, Def, Def) == REASON_VERTEX);
}

// Why: the other programs (the water, the underwater light, the HDR and post passes, the anti-aliasing, the path tracer's
// composite, the rays' debug views) read the rest whatever it holds, and their depth-only draws (the ocean's grid's depth
// pass) move their vertices by it.
XR_TEST(DrawExt_OtherPrograms)
{
  const SDrawExt Def = Default();
  SDraw d;
  CHECK(Needs(d, Def, Def) == REASON_PROGRAM);
  d.bDepthOnly = true;
  CHECK(Needs(d, Def, Def) == REASON_PROGRAM);
}

// Why: heat vision writes vHeat into the draw's copy after the decision, and d3d12_DrawExt 0 uploads every draw's rest
// (the same bytes as before the default block, for comparisons).
XR_TEST(DrawExt_ChangedAndAll)
{
  const SDrawExt Def = Default();
  SDraw d = General();
  d.bChanged = true;
  CHECK(Needs(d, Def, Def) == REASON_DIFFERS);
  SDraw All;
  All.bAll = true;
  All.bDepthOnly = true;
  CHECK(Needs(All, Def, Def) == REASON_ALL);
  All.bGeneral = true;
  CHECK(Needs(All, Def, Def) == REASON_ALL);
}
