// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: DrawExt.h
//	Description: Whether a draw uploads the rest of its constants
//	(SDrawExt, b3; Shaders/Constants.h) or binds the default block in
//	their place (d3d12_DrawExt; CD3D12Pipeline::GetDefaultDrawExt). The
//	block holds the rest of DefaultDrawConstants (SetDefault), so a draw
//	whose rest is that reads the same bytes either way; a depth-only draw
//	of the general program reads none of it but where its vertex shader
//	moves the vertices by it (General.hlsl's VSMain). Header only (the
//	unit tests, Tests/DrawExtTests.cpp).
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_DRAWEXT_H
#define XRENDERD3D12_DRAWEXT_H

#include "BytesEqual.h"
#include "Shaders/Constants.h"

#include <cstdint>
#include <cstring>

namespace DrawExt
{
  //! Why a draw uploads its rest (the counters of d3d12_FrameLog and r_Stats).
  enum EReason
  {
    REASON_DEFAULT,     //!< none: the default block
    REASON_ALL,         //!< d3d12_DrawExt 0: every draw uploads it
    REASON_PROGRAM,     //!< a program other than the general one (the water, the HDR and post passes...): they read it whatever it holds
    REASON_VERTEX,      //!< a depth-only draw whose vertex shader moves its vertices by it: DF2_SHADER_FX (vParams), the
                        //!< cascades' instances (mShadow, nShadowTex)
    REASON_DIFFERS,     //!< a draw with a pixel shader whose rest is not the default, or that the renderer changes (heat vision)
    REASON_NUM
  };

  //! What the decision looks at besides the rest itself.
  struct SDraw
  {
    bool bAll = false;              //!< d3d12_DrawExt 0
    bool bGeneral = false;          //!< the general program (eD3D12P_General)
    bool bDepthOnly = false;        //!< SPipelineKey::bDepthOnly: no pixel shader
    bool bCascadeInstances = false; //!< SPipelineKey::bCascadeInstances
    bool bChanged = false;          //!< the renderer writes into the rest (heat vision's vHeat)
    uint32 nFlags2 = 0;             //!< SDrawConstants::nFlags2
  };

  //! The rest of DefaultDrawConstants: zeros, the texture matrix the identity.
  inline void SetDefault(SDrawExt &Ext)
  {
    memset(&Ext, 0, sizeof(Ext));
    Ext.vTexMat0[0] = 1;
    Ext.vTexMat1[1] = 1;
  }

  //! REASON_DEFAULT: the draw binds the default block (Default, SetDefault's); else why it uploads its own.
  inline EReason Needs(const SDraw &d, const SDrawExt &Ext, const SDrawExt &Default)
  {
    if (d.bAll)
      return REASON_ALL;
    if (!d.bGeneral)
      return REASON_PROGRAM;
    if (d.bDepthOnly)
      return (d.nFlags2 & DF2_SHADER_FX) || d.bCascadeInstances ? REASON_VERTEX : REASON_DEFAULT;
    return d.bChanged || !BytesEqual::Equal(&Ext, &Default, sizeof(SDrawExt)) ? REASON_DIFFERS : REASON_DEFAULT;
  }

  //! The rest the last draw that uploaded one wrote (without the renderer's changes) and where, in the frame memory of
  //! nUploadFrame (CD3D12Upload::GetFrameGeneration): a draw with the same bytes binds that copy (d3d12_DrawExtReuse).
  struct SLast
  {
    SDrawExt Data;
    uint64_t nGPU = 0;
    uint64_t nUploadFrame = 0;

    //! The copy's address for a rest of these bytes in that frame's memory, 0 none.
    uint64_t Find(const SDrawExt &Ext, uint64_t nFrame) const
    {
      return nGPU && nUploadFrame == nFrame && BytesEqual::Equal(&Ext, &Data, sizeof(SDrawExt)) ? nGPU : 0;
    }
    void Keep(const SDrawExt &Ext, uint64_t nAddress, uint64_t nFrame)
    {
      memcpy(&Data, &Ext, sizeof(SDrawExt));
      nGPU = nAddress;
      nUploadFrame = nFrame;
    }
  };
}

#endif
