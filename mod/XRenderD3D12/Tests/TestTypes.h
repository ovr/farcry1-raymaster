// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: TestTypes.h
//	Description: The structures C++ shares with the shaders
//	(Shaders/Constants.h, Shaders/RayConstants.h) for the unit tests,
//	without the SDK: those headers take uint32 from the engine's platform
//	header in the renderer; here it is the standard one of the same size.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_TESTS_TESTTYPES_H
#define XRENDERD3D12_TESTS_TESTTYPES_H

#include <cstdint>

typedef std::uint32_t uint32;

#include "Shaders/RayConstants.h"

#endif
