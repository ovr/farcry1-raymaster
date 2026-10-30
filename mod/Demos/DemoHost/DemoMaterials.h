// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: DemoMaterials.h
//	Description: Materials of a shader template, for the demos that put
//	one in place of an object's own materials (DemoShaders, DemoSurfaces).
//
//	A material is an IMatInfo of the 3D engine (CreateMatInfo) whose shader
//	item comes from IRenderer::EF_LoadShaderItem with a shader template,
//	the way the engine makes the materials of its objects
//	(MakMatInfoFromMAT_ENTITY.h). SRendParams::pMaterial puts it in place
//	of the object's own materials: chunk id 0 takes the material's shader
//	item, id n its sub material n - 1, so the material has sub materials
//	with the same item. All materials of a demo get the same textures
//	(diffuse, bump map, cube map, gloss map); each template uses the ones
//	it needs.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include "DemoHost.h"

//! The items of a list separated by cDelim, without the spaces around them; empty ones are left out.
std::vector<std::string> SplitList(const std::string &List, char cDelim);
//! Whether szText is part of Name, in any case.
bool HasText(const std::string &Name, const char *szText);
//! The colour "r g b" of szKey, or Default.
CFColor SceneColor(const CSceneFile &Scene, const char *szKey, const CFColor &Default);

//! The textures of a demo's materials.
struct SDemoTextures
{
  std::string Texture, Bump, CubeMap, Gloss;
  //! The texture slots given the cube map: EFTT_CUBEMAP (a plain texture name) and EFTT_REFLECTION
  //! (of type eTT_Cubemap).
  std::vector<float> CubeSlots = { (float)EFTT_CUBEMAP, (float)EFTT_REFLECTION };

  //! From the scene keys texture (default: the first textures/*.dds that is no bump map), bump (the
  //! first *_ddn.dds under textures/), cubemap (the first *_posx.dds under textures/), cubeslots
  //! (the slots, "none" neither) and gloss (none).
  void FromScene(CDemoHost &Host);
};

//! A material of a shader template, released with Release.
struct SDemoMaterial
{
  IMatInfo *pMat = NULL;
  IMatInfo *pSubMat = NULL;

  //! Makes the material: a shader item named szName (the renderer names the shader after it) of
  //! szTemplate with opacity fOpacity, the textures and the lighting Light (which must outlive the
  //! material). Returns the shader item; with no shader the material is not made.
  SShaderItem Make(CDemoHost &Host, const char *szName, const char *szTemplate, float fOpacity,
    const SDemoTextures &Textures, SLightMaterial &Light);
  void Release(CDemoHost &Host);
};
