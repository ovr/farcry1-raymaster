// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: ColorGrading.cpp
//	Description: The colour grading of the HDR pipeline's resolve (Hdr.cpp,
//	r_HDRRendering).
//
//	A 3D table of the display's colours (the tone mapped picture, gamma
//	encoded, 0 .. 1) to the graded ones, HDR_COLOR_LUT_SIZE points along
//	each axis, made on the CPU whenever its settings change. Each colour
//	goes, as light (colour^2.2):
//	  - through the white balance (d3d12_Temperature, d3d12_Tint): the
//	    colours scaled in the LMS cone space (Bradford) by the white of a
//	    black body of another temperature over that of 6504 K, the
//	    temperature shifted by up to 100 mired (warmer for > 0), the tint
//	    moving the white off the black body's line (magenta for > 0, green
//	    for < 0); the brightness of white kept;
//	  - the contrast (d3d12_Contrast): each channel's light around middle
//	    grey (0.18) to that power;
//	  - the saturation (d3d12_Saturation): away from (or towards) the
//	    luminance;
//	then, as a display colour again, through the 3D LUT of the .cube file
//	d3d12_ColorLut (as DaVinci Resolve, Photoshop and others write it, of
//	display colours), mixed in by d3d12_ColorLutAmount. With every setting
//	neutral there is no table, and the resolve is as without it.
//
//	The table is a 2D texture (R16G16B16A16_UNORM) of HDR_COLOR_LUT_SIZE
//	slices of blue side by side, red across each and green down;
//	Hdr.hlsl looks up the two slices around a colour's blue and blends them.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "D3D12Upload.h"

#include <array>
#include <charconv>
#include <string_view>

namespace
{

using Vec3f = std::array<float, 3>;
using Mat3 = std::array<Vec3f, 3>;    //!< rows

constexpr int N = HDR_COLOR_LUT_SIZE;
constexpr float GAMMA = 2.2f;
constexpr float MIDDLE_GREY = 0.18f;
constexpr float NEUTRAL_TEMPERATURE = 6504.0f;    //!< K, where d3d12_Temperature 0 is
constexpr float MIRED_RANGE = 100.0f;             //!< the shift of d3d12_Temperature +-1
constexpr float TINT_RANGE = 0.03f;               //!< the shift of the white's y of d3d12_Tint +-1
constexpr int CUBE_MAX_SIZE = 128;
const Vec3f LUMINANCE = { 0.2126f, 0.7152f, 0.0722f };

//! Linear sRGB (Rec. 709 primaries, D65) to CIE XYZ, and XYZ to the cone responses (Bradford).
const Mat3 RGB_TO_XYZ = { { { 0.4124f, 0.3576f, 0.1805f }, { 0.2126f, 0.7152f, 0.0722f }, { 0.0193f, 0.1192f, 0.9505f } } };
const Mat3 XYZ_TO_LMS = { { { 0.8951f, 0.2664f, -0.1614f }, { -0.7502f, 1.7135f, 0.0367f }, { 0.0389f, -0.0685f, 1.0296f } } };

Vec3f Mul(const Mat3 &m, const Vec3f &v)
{
  Vec3f r;
  for (int i = 0; i < 3; i++)
    r[i] = m[i][0] * v[0] + m[i][1] * v[1] + m[i][2] * v[2];
  return r;
}

Mat3 Mul(const Mat3 &a, const Mat3 &b)
{
  Mat3 r = {};
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++)
      r[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
  return r;
}

Mat3 Inverse(const Mat3 &m)
{
  const float a = m[0][0], b = m[0][1], c = m[0][2], d = m[1][0], e = m[1][1], f = m[1][2], g = m[2][0], h = m[2][1], k = m[2][2];
  const float A = e * k - f * h, B = f * g - d * k, C = d * h - e * g;
  const float fInvDet = 1.0f / (a * A + b * B + c * C);
  return { { { A * fInvDet, (c * h - b * k) * fInvDet, (b * f - c * e) * fInvDet },
             { B * fInvDet, (a * k - c * g) * fInvDet, (c * d - a * f) * fInvDet },
             { C * fInvDet, (b * g - a * h) * fInvDet, (a * e - b * d) * fInvDet } } };
}

float Dot(const Vec3f &a, const Vec3f &b)
{
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

//! The chromaticity (x, y) of a black body at T kelvin, 1667 to 25000 K (the cubic fits of Kim et
//! al.), its y moved by dy.
std::pair<float, float> BlackBody(float T, float dy)
{
  T = std::clamp(T, 1667.0f, 25000.0f);
  const float t = 1e3f / T, t2 = t * t, t3 = t2 * t;
  const float x = T <= 4000 ? -0.2661239f * t3 - 0.2343589f * t2 + 0.8776956f * t + 0.179910f
                            : -3.0258469f * t3 + 2.1070379f * t2 + 0.2226347f * t + 0.240390f;
  const float x2 = x * x, x3 = x2 * x;
  const float y = T <= 2222 ? -1.1063814f * x3 - 1.34811020f * x2 + 2.18555832f * x - 0.20219683f
                : T <= 4000 ? -0.9549476f * x3 - 1.37418593f * x2 + 2.09137015f * x - 0.16748867f
                            : 3.0817580f * x3 - 5.87338670f * x2 + 3.75112997f * x - 0.37001483f;
  return { x, y + dy };
}

//! The cone responses of the white of chromaticity (x, y), Y = 1.
Vec3f WhiteLms(std::pair<float, float> xy)
{
  const auto [x, y] = xy;
  return Mul(XYZ_TO_LMS, Vec3f{ x / y, 1.0f, (1 - x - y) / y });
}

//! The white balance of linear sRGB colours: temperature -1 (cooler) to 1 (warmer), tint -1
//! (green) to 1 (magenta).
Mat3 WhiteBalance(float fTemperature, float fTint)
{
  const float T = 1e6f / (1e6f / NEUTRAL_TEMPERATURE + MIRED_RANGE * fTemperature);
  const Vec3f Src = WhiteLms(BlackBody(NEUTRAL_TEMPERATURE, 0)), Dst = WhiteLms(BlackBody(T, -TINT_RANGE * fTint));
  Mat3 Gain = {};
  for (int i = 0; i < 3; i++)
    Gain[i][i] = Dst[i] / Src[i];
  const Mat3 RgbToLms = Mul(XYZ_TO_LMS, RGB_TO_XYZ);
  Mat3 m = Mul(Inverse(RgbToLms), Mul(Gain, RgbToLms));
  // White as bright as before.
  const float fWhite = Dot(LUMINANCE, Mul(m, Vec3f{ 1, 1, 1 }));
  for (Vec3f &Row : m)
    for (float &v : Row)
      v /= fWhite;
  return m;
}

//! The words of a line of a .cube file.
std::vector<std::string_view> Words(std::string_view Line)
{
  std::vector<std::string_view> Out;
  size_t i = 0;
  while (i < Line.size())
  {
    while (i < Line.size() && (Line[i] == ' ' || Line[i] == '\t' || Line[i] == '\r'))
      i++;
    const size_t nStart = i;
    while (i < Line.size() && Line[i] != ' ' && Line[i] != '\t' && Line[i] != '\r')
      i++;
    if (i > nStart)
      Out.push_back(Line.substr(nStart, i - nStart));
  }
  return Out;
}

bool ToFloat(std::string_view s, float &f)
{
  const auto Res = std::from_chars(s.data(), s.data() + s.size(), f);
  return Res.ec == std::errc() && Res.ptr == s.data() + s.size();
}

uint16 ToUnorm16(float f)
{
  return (uint16)lroundf(std::clamp(f, 0.0f, 1.0f) * 65535.0f);
}

}

bool CD3D12Renderer::LoadColorCube(const char *szPath, std::string &Error)
{
  SColorGrading &G = m_Grading;
  G.Cube.clear();
  G.nCubeSize = 0;
  ICryPak *pPak = iSystem ? iSystem->GetIPak() : nullptr;
  FILE *f = pPak ? pPak->FOpen(szPath, "rb") : nullptr;
  if (!f)
  {
    Error = "not found";
    return false;
  }
  std::string Text;
  if (pPak->FSeek(f, 0, SEEK_END) == 0)
  {
    const long nSize = pPak->FTell(f);
    if (nSize > 0 && pPak->FSeek(f, 0, SEEK_SET) == 0)
    {
      Text.resize((size_t)nSize);
      Text.resize(pPak->FRead(Text.data(), 1, (size_t)nSize, f));
    }
  }
  pPak->FClose(f);

  // Keywords, then the rows: r g b, red changing fastest, then green, then blue.
  Vec3f Min = { 0, 0, 0 }, Max = { 1, 1, 1 };
  std::vector<float> Rows;
  int nLine = 0;
  for (size_t nPos = 0; nPos < Text.size();)
  {
    size_t nEnd = Text.find('\n', nPos);
    if (nEnd == std::string::npos)
      nEnd = Text.size();
    const std::string_view Line(Text.data() + nPos, nEnd - nPos);
    nPos = nEnd + 1;
    nLine++;
    const std::vector<std::string_view> W = Words(Line);
    if (W.empty() || W[0][0] == '#' || W[0] == "TITLE")
      continue;
    float v[3];
    if (W.size() == 3 && ToFloat(W[0], v[0]) && ToFloat(W[1], v[1]) && ToFloat(W[2], v[2]))
    {
      Rows.insert(Rows.end(), v, v + 3);
      continue;
    }
    if (W[0] == "LUT_3D_SIZE" && W.size() == 2 && ToFloat(W[1], v[0]))
      G.nCubeSize = (int)v[0];
    else if ((W[0] == "DOMAIN_MIN" || W[0] == "DOMAIN_MAX") && W.size() == 4 && ToFloat(W[1], v[0]) && ToFloat(W[2], v[1]) && ToFloat(W[3], v[2]))
      (W[0] == "DOMAIN_MIN" ? Min : Max) = { v[0], v[1], v[2] };
    else if (W[0] == "LUT_3D_INPUT_RANGE" && W.size() == 3 && ToFloat(W[1], v[0]) && ToFloat(W[2], v[1]))
    {
      Min = { v[0], v[0], v[0] };
      Max = { v[1], v[1], v[1] };
    }
    else if (W[0] == "LUT_1D_SIZE")
    {
      Error = "a 1D LUT (only 3D LUTs are taken)";
      return false;
    }
    else
    {
      Error = "line " + std::to_string(nLine) + " is not understood";
      return false;
    }
  }
  const int n = G.nCubeSize;
  if (n < 2 || n > CUBE_MAX_SIZE)
  {
    Error = n ? "LUT_3D_SIZE " + std::to_string(n) + " is not 2 to " + std::to_string(CUBE_MAX_SIZE) : "no LUT_3D_SIZE";
    G.nCubeSize = 0;
    return false;
  }
  if (Rows.size() != (size_t)n * n * n * 3)
  {
    Error = std::to_string(Rows.size() / 3) + " rows, not " + std::to_string(n * n * n);
    G.nCubeSize = 0;
    return false;
  }
  for (int i = 0; i < 3; i++)
  {
    if (!(Max[i] > Min[i]))
    {
      Error = "an empty domain";
      G.nCubeSize = 0;
      return false;
    }
    G.CubeMin[i] = Min[i];
    G.CubeMax[i] = Max[i];
  }
  G.Cube = std::move(Rows);
  return true;
}

int CD3D12Renderer::UpdateColorGrading()
{
  SColorGrading &G = m_Grading;
  if (!m_pColorLutVar && iConsole)
    m_pColorLutVar = iConsole->GetCVar("d3d12_ColorLut");
  const char *szFile = m_pColorLutVar && m_pColorLutVar->GetString() ? m_pColorLutVar->GetString() : "";
  const float fSaturation = std::clamp(CV_d3d12_Saturation, 0.0f, 4.0f), fContrast = std::clamp(CV_d3d12_Contrast, 0.25f, 4.0f);
  const float fTemperature = std::clamp(CV_d3d12_Temperature, -1.0f, 1.0f), fTint = std::clamp(CV_d3d12_Tint, -1.0f, 1.0f);
  const float fAmount = std::clamp(CV_d3d12_ColorLutAmount, 0.0f, 1.0f);
  if (G.bMade && G.fSaturation == fSaturation && G.fContrast == fContrast && G.fTemperature == fTemperature && G.fTint == fTint &&
      G.fLutAmount == fAmount && G.LutFile == szFile)
    return G.pLut ? G.pLut->m_nSRV : -1;

  // The .cube file, read when it is named anew (an error logged once).
  if (G.LoadedFile != szFile)
  {
    G.LoadedFile = szFile;
    G.Cube.clear();
    G.nCubeSize = 0;
    std::string Error;
    if (*szFile && !LoadColorCube(szFile, Error))
      iLog->LogError("XRenderD3D12: d3d12_ColorLut \"%s\": %s", szFile, Error.c_str());
    else if (*szFile)
      iLog->Log("XRenderD3D12: d3d12_ColorLut \"%s\": %d x %d x %d", szFile, G.nCubeSize, G.nCubeSize, G.nCubeSize);
  }
  G.bMade = true;
  G.fSaturation = fSaturation;
  G.fContrast = fContrast;
  G.fTemperature = fTemperature;
  G.fTint = fTint;
  G.fLutAmount = fAmount;
  G.LutFile = szFile;
  if (m_pUpload)
    m_pUpload->ReleaseTexture(G.pLut);
  G.pLut = nullptr;
  const bool bCube = G.nCubeSize > 0 && fAmount > 0;
  if (!m_pUpload || (fSaturation == 1 && fContrast == 1 && fTemperature == 0 && fTint == 0 && !bCube))
    return -1;

  // The cube's colour at a display colour (trilinear).
  const auto SampleCube = [&G](const Vec3f &c)
  {
    const int n = G.nCubeSize;
    int i0[3], i1[3];
    float t[3];
    for (int k = 0; k < 3; k++)
    {
      const float x = std::clamp((c[k] - G.CubeMin[k]) / (G.CubeMax[k] - G.CubeMin[k]), 0.0f, 1.0f) * (n - 1);
      i0[k] = std::min((int)x, n - 2);
      i1[k] = i0[k] + 1;
      t[k] = x - i0[k];
    }
    Vec3f Out = { 0, 0, 0 };
    for (int nCorner = 0; nCorner < 8; nCorner++)
    {
      const int r = nCorner & 1 ? i1[0] : i0[0], g = nCorner & 2 ? i1[1] : i0[1], b = nCorner & 4 ? i1[2] : i0[2];
      const float w = (nCorner & 1 ? t[0] : 1 - t[0]) * (nCorner & 2 ? t[1] : 1 - t[1]) * (nCorner & 4 ? t[2] : 1 - t[2]);
      const float *p = &G.Cube[((size_t)(b * n + g) * n + r) * 3];
      for (int k = 0; k < 3; k++)
        Out[k] += w * p[k];
    }
    return Out;
  };

  const Mat3 Balance = WhiteBalance(fTemperature, fTint);
  std::vector<uint16> Texels((size_t)N * N * N * 4);
  for (int b = 0; b < N; b++)
    for (int g = 0; g < N; g++)
      for (int r = 0; r < N; r++)
      {
        Vec3f c = { r / (N - 1.0f), g / (N - 1.0f), b / (N - 1.0f) };
        Vec3f Light;
        for (int k = 0; k < 3; k++)
          Light[k] = powf(c[k], GAMMA);
        Light = Mul(Balance, Light);
        for (float &v : Light)
          v = MIDDLE_GREY * powf(std::max(v, 0.0f) / MIDDLE_GREY, fContrast);
        const float Y = Dot(LUMINANCE, Light);
        for (int k = 0; k < 3; k++)
          c[k] = std::min(powf(std::max(Y + (Light[k] - Y) * fSaturation, 0.0f), 1 / GAMMA), 1.0f);
        if (bCube)
        {
          const Vec3f Look = SampleCube(c);
          for (int k = 0; k < 3; k++)
            c[k] += (Look[k] - c[k]) * fAmount;
        }
        uint16 *p = &Texels[((size_t)g * N * N + (size_t)b * N + r) * 4];
        p[0] = ToUnorm16(c[0]);
        p[1] = ToUnorm16(c[1]);
        p[2] = ToUnorm16(c[2]);
        p[3] = 65535;
      }
  const SD3D12SubData Sub = { Texels.data(), (uint32)(N * N * 4 * sizeof(uint16)) };
  G.pLut = m_pUpload->CreateTexture(N * N, N, 1, DXGI_FORMAT_R16G16B16A16_UNORM, false, &Sub);
  if (G.pLut && G.pLut->m_nSRV < 0)
  {
    m_pUpload->ReleaseTexture(G.pLut);
    G.pLut = nullptr;
  }
  return G.pLut ? G.pLut->m_nSRV : -1;
}

void CD3D12Renderer::ReleaseColorGrading()
{
  if (m_pUpload)
    m_pUpload->ReleaseTexture(m_Grading.pLut);
  m_Grading.pLut = nullptr;
  m_Grading.bMade = false;
}
