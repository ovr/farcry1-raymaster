//////////////////////////////////////////////////////////////////////
//
//	File: AbiChecks.cpp
//	Description: The SDK classes this renderer shares with the engine have the
//	engine's x64 sizes (StructFields.inl).
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include <CRETriMeshShadow.h>
#include <CREPolyMesh.h>

#define XRC_STRUCT(T, nSize86, nSize64) static_assert(sizeof(T) == (nSize64), #T " does not match StructFields.inl");
#define XRC_STRUCT_END(T)
#define XRC_FIELD(szName, nOffset86, nOffset64)
#include <StructFields.inl>
#undef XRC_STRUCT
#undef XRC_STRUCT_END
#undef XRC_FIELD
