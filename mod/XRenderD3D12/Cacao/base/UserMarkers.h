// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Cacao/base/UserMarkers.h
//	Description: What AMD FidelityFX CACAO's Direct3D 12 host
//	(third_party/fidelityfx-cacao, ffx_cacao_impl.cpp) includes of AMD's
//	Cauldron framework for its debug markers: a marker that does nothing.
//	Only that file's include path has this folder.
//
//////////////////////////////////////////////////////////////////////

#pragma once

namespace CAULDRON_DX12
{

class UserMarker
{
public:
  UserMarker(ID3D12GraphicsCommandList *, const char *) {}
};

}
