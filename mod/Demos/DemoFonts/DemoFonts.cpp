//////////////////////////////////////////////////////////////////////
//
//	File: DemoFonts.cpp
//	Description: Text drawn with the engine's fonts (ICryFont).
//
//	For each font: a sentence at four sizes, the same line in four colours
//	and the printable ASCII characters, drawn with IFFont::DrawString on
//	the virtual 800 x 600 screen. The fonts build their glyphs into
//	textures (FontCreateTexture, FontUpdateTexture) and draw them with the
//	renderer's font calls, so the frame shows that whole path.
//
//	Scene file keys (Demos\DemoFonts.ini):
//	  fonts = <name>, <name>   the fonts (default: Default)
//	  effect = <name>          the effect of the fonts (default: default)
//
//////////////////////////////////////////////////////////////////////

#include "DemoHost.h"
#include <IFont.h>

class CDemoFonts : public CDemo
{
public:
  virtual const char *GetName() const { return "DemoFonts"; }
  virtual const char *GetDefaultFrames() const { return "10"; }

  virtual bool Init(CDemoHost &Host)
  {
    ICryFont *pCryFont = Host.GetSystem()->GetICryFont();
    std::string Fonts = Host.GetScene().Get("fonts", "Default");
    m_Effect = Host.GetScene().Get("effect", "default");
    for (size_t nBegin = 0; nBegin < Fonts.size();)
    {
      size_t nEnd = Fonts.find(',', nBegin);
      if (nEnd == std::string::npos)
        nEnd = Fonts.size();
      std::string Name = Fonts.substr(nBegin, nEnd - nBegin);
      Name.erase(0, Name.find_first_not_of(' '));
      Name.erase(Name.find_last_not_of(' ') + 1);
      nBegin = nEnd + 1;
      IFFont *pFont = pCryFont && !Name.empty() ? pCryFont->GetFont(Name.c_str()) : NULL;
      Host.Log("DemoFonts: font '%s' %s", Name.c_str(), pFont ? "found" : "not found");
      if (pFont)
        m_Fonts.push_back(pFont);
    }
    return !m_Fonts.empty();
  }

  virtual void Draw(CDemoHost &Host, int nFrame)
  {
    static const char szSentence[] = "The quick brown fox jumps over the lazy dog 0123456789";
    static const color4f Colors[4] = { color4f(1, 0, 0, 1), color4f(0, 1, 0, 1), color4f(0, 0, 1, 1), color4f(1, 1, 0, 1) };
    char szAscii[2][48];
    for (int i = 0; i < 2; i++)
    {
      for (int c = 0; c < 47; c++)
        szAscii[i][c] = (char)(33 + i * 47 + c);
      szAscii[i][47] = 0;
    }

    float y = 10;
    for (size_t i = 0; i < m_Fonts.size(); i++)
    {
      IFFont *pFont = m_Fonts[i];
      pFont->Reset();
      pFont->SetEffect(m_Effect.c_str());
      pFont->SetSameSize(false);
      pFont->SetCharWidthScale(1.0f);
      pFont->SetColor(color4f(1, 1, 1, 1));
      static const float Sizes[4] = { 12, 16, 24, 32 };
      for (int s = 0; s < 4; s++)
      {
        pFont->SetSize(vector2f(Sizes[s], Sizes[s]));
        pFont->DrawString(10, y, szSentence);
        y += Sizes[s] + 4;
      }
      pFont->SetSize(vector2f(20, 20));
      for (int c = 0; c < 4; c++)
      {
        pFont->SetColor(Colors[c]);
        pFont->DrawString(10 + c * 195.0f, y, "Colour");
      }
      y += 24;
      pFont->SetColor(color4f(1, 1, 1, 1));
      pFont->SetSize(vector2f(16, 16));
      pFont->DrawString(10, y, szAscii[0]);
      pFont->DrawString(10, y + 20, szAscii[1]);
      y += 50;
    }
  }

private:
  std::vector<IFFont *> m_Fonts;
  std::string m_Effect;
};

DEMO_PLUGIN(CDemoFonts)
