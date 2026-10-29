// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
//////////////////////////////////////////////////////////////////////
//
//	File: DemoTextures.cpp
//	Description: Textures drawn as 2D images.
//
//	Top row: a texture of the game, the test picture (TestTexture.h) with
//	the texture coordinates the engine uses for upright images (0, 1, 1, 0)
//	and with (0, 0, 1, 1). Bottom row: the test picture turned by 90
//	degrees, repeated 2 x 2, tinted, and half transparent over a white
//	quad. Where each quadrant of the test picture lands shows how the
//	renderer maps the coordinates.
//
//	Scene file keys (Demos\DemoTextures.ini):
//	  texture = <file>   the game texture (default: the first textures/*.dds)
//
//////////////////////////////////////////////////////////////////////

#include "DemoHost.h"

class CDemoTextures : public CDemo
{
public:
  virtual const char *GetName() const { return "DemoTextures"; }
  virtual const char *GetDefaultFrames() const { return "10"; }

  virtual bool Init(CDemoHost &Host)
  {
    std::string Name = Host.SceneFileOrFirst("texture", "textures/", "*.dds");
    if (Name.empty())
    {
      Host.Log("DemoTextures: no texture");
      return false;
    }
    m_pTexture = Host.GetRenderer()->EF_LoadTexture(Name.c_str(), FT_NORESIZE, 0, eTT_Base);
    if (!m_pTexture || !m_pTexture->IsTextureLoaded())
    {
      Host.Log("DemoTextures: %s not loaded", Name.c_str());
      return false;
    }
    Host.Log("DemoTextures: %s, %d x %d", Name.c_str(), m_pTexture->GetWidth(), m_pTexture->GetHeight());
    return Host.GetTestTexture() > 0;
  }

  virtual void Draw(CDemoHost &Host, int nFrame)
  {
    IRenderer *pRenderer = Host.GetRenderer();
    const int nTest = Host.GetTestTexture();
    // The 2D calls use a virtual 800 x 600 screen.
    pRenderer->SetState(GS_NODEPTHTEST);
    pRenderer->Draw2dImage(40, 40, 220, 220, m_pTexture->GetTextureID(), 0, 1, 1, 0);
    pRenderer->Draw2dImage(290, 40, 220, 220, nTest, 0, 1, 1, 0);
    pRenderer->Draw2dImage(540, 40, 220, 220, nTest, 0, 0, 1, 1);
    pRenderer->Draw2dImage(40, 320, 220, 220, nTest, 0, 1, 1, 0, 90);
    pRenderer->Draw2dImage(290, 320, 220, 220, nTest, 0, 2, 2, 0);
    // Tinted: the vertex colour modulates the texture.
    pRenderer->Draw2dImage(540, 320, 105, 220, nTest, 0, 1, 1, 0, 0, 1, 0.5f, 0.25f, 1);
    // Half transparent over white.
    pRenderer->Draw2dImage(655, 320, 105, 220, -1, 0, 0, 0, 0, 0, 1, 1, 1, 1);
    pRenderer->SetState(GS_NODEPTHTEST | GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA);
    pRenderer->Draw2dImage(655, 320, 105, 220, nTest, 0, 1, 1, 0, 0, 1, 1, 1, 0.5f);
    pRenderer->SetState(GS_NODEPTHTEST);
  }

  virtual void Shutdown(CDemoHost &Host)
  {
    if (m_pTexture)
      m_pTexture->Release(false);
    m_pTexture = NULL;
  }

private:
  ITexPic *m_pTexture = NULL;
};

DEMO_PLUGIN(CDemoTextures)
