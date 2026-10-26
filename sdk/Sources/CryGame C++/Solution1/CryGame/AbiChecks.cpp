//////////////////////////////////////////////////////////////////////
//
//	File: AbiChecks.cpp
//	Description: Compile-time checks that CryGame.dll keeps the binary layout
//	of the types it exchanges with the retail Far Cry 1.4 engine DLLs, which
//	were built with Visual C++ 7.1 and STLport 4.6.
//
//	The expected values follow from the STLport 4.6 sources and the VC7.1 ABI
//	(32-bit pointers, 4-byte alignment, empty allocators folded into the
//	container by the empty base optimization), not from what the current
//	compiler happens to produce.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include <io.h>
#include "IGame.h"
#include "IAgent.h"

#if defined(WIN32) && !defined(WIN64)

// ICryPak::FindFirst/FindNext fill a _finddata_t owned by the game: attrib, three 32-bit
// time_t values, a 32-bit size and a 260 character name. Needs _USE_32BIT_TIME_T.
static_assert(sizeof(time_t) == 4, "time_t must be 32 bits wide (define _USE_32BIT_TIME_T)");
static_assert(sizeof(_finddata_t) == 280, "_finddata_t does not match the VC7.1 layout");

#if defined(_STLPORT_VERSION)

// STLport 4.6: string and vector are three pointers, map is a header node pointer, the node
// count and the (empty, padded) comparator, list is a single pointer to its sentinel node.
static_assert(sizeof(string) == 12, "string does not match the STLport 4.6 layout");
static_assert(sizeof(wstring) == 12, "wstring does not match the STLport 4.6 layout");
static_assert(sizeof(std::vector<int>) == 12, "vector does not match the STLport 4.6 layout");
static_assert(sizeof(std::map<int, int>) == 12, "map does not match the STLport 4.6 layout");
static_assert(sizeof(std::list<int>) == 4, "list does not match the STLport 4.6 layout");

// Structures with STL members that are shared with the engine.
static_assert(sizeof(EntityClass) == 56, "EntityClass does not match the engine layout");
static_assert(sizeof(SGameModDescription) == 92, "SGameModDescription does not match the engine layout");
static_assert(sizeof(GoalParameters) == 44, "GoalParameters does not match the engine layout");

#endif // _STLPORT_VERSION

#endif // WIN32 && !WIN64
