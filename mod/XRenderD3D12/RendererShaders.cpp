//////////////////////////////////////////////////////////////////////
//
//	File: RendererShaders.cpp
//	Description: The IRenderer shader methods (shaders, shader items,
//	templates, light materials); the state is in CShaderManager (Shader.h).
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "Shader.h"

bool CD3D12Renderer::EF_PrecacheResource(IShader *pSH, float fDist, float fTimeToReady, int Flags)
{
  // A material's textures are loaded with its shader item.
  return true;
}

IShader *CD3D12Renderer::EF_LoadShader(const char *name, EShClass Class, int flags, uint64 nMaskGen)
{
  if (!m_pShaders)
    return nullptr;
  return m_pShaders->LoadShader(name, Class, flags, nMaskGen);
}

SShaderItem CD3D12Renderer::EF_LoadShaderItem(const char *name, EShClass Class, bool bShare, const char *templName, int flags, SInputShaderResources *Res, uint64 nMaskGen)
{
  if (!m_pShaders)
    return SShaderItem();
  return m_pShaders->LoadShaderItem(name, Class, bShare, templName, flags, Res, nMaskGen);
}

bool CD3D12Renderer::EF_ReloadFile(const char *szFileName)
{
  // No shader scripts are read, so there is nothing to reload.
  return false;
}

void CD3D12Renderer::EF_ReloadShaderFiles(int nCategory)
{
}

IShader *CD3D12Renderer::EF_CopyShader(IShader *ef)
{
  if (!m_pShaders)
    return nullptr;
  return m_pShaders->CopyShader(ef);
}

int CD3D12Renderer::EF_ReadAllImgFiles(IShader *ef, SShaderTexUnit *tl, STexAnim *ta, char *name)
{
  return 0;
}

char **CD3D12Renderer::EF_GetShadersForFile(const char *File, int num)
{
  return nullptr;
}

SLightMaterial *CD3D12Renderer::EF_GetLightMaterial(char *Str)
{
  if (!m_pShaders)
    return nullptr;
  return m_pShaders->GetLightMaterial(Str);
}

bool CD3D12Renderer::EF_RegisterTemplate(int nTemplId, char *Name, bool bReplace)
{
  if (!m_pShaders)
    return false;
  return m_pShaders->RegisterTemplate(nTemplId, Name, bReplace);
}

bool CD3D12Renderer::EF_HideTemplate(const char *name)
{
  // Hiding only concerns the lists of editors.
  return true;
}

bool CD3D12Renderer::EF_UnhideTemplate(const char *name)
{
  return true;
}

bool CD3D12Renderer::EF_UnhideAllTemplates()
{
  return true;
}

void CD3D12Renderer::EF_ConstructEf(IShader *Ef)
{
}

void CD3D12Renderer::SetGlobalShaderTemplateId(int nTemplateId)
{
  if (m_pShaders)
    m_pShaders->SetGlobalTemplateId(nTemplateId);
}

int CD3D12Renderer::GetGlobalShaderTemplateId()
{
  return m_pShaders ? m_pShaders->GetGlobalTemplateId() : -2;
}
