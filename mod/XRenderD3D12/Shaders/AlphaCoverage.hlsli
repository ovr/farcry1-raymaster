// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: AlphaCoverage.hlsli
//	Description: Alpha to coverage (d3d12_AlphaToCoverage): with MSAA the
//	general program's alpha-tested draws (SPipelineKey::bAlphaToCoverage,
//	Draw.cpp's DrawGeometry, the reference negated) cover the samples of
//	their alpha sharpened to the test's edge in place of the test
//	(General.hlsl); the alpha of an object fading in or out by its
//	distance (d3d12_FadeBlend) after the test. Stands alone (the unit
//	tests' TestHeaders.hlsl).
//
//////////////////////////////////////////////////////////////////////

#ifndef XR_ALPHA_COVERAGE_HLSLI
#define XR_ALPHA_COVERAGE_HLSLI

//! The alpha a sharpened to the alpha test's edge at fRef over fWidth, the alpha's change across the pixel (its fwidth):
//! 1 where the test keeps the whole pixel, 0 where it discards it, along the edge the share of the pixel it keeps where
//! the alpha changes evenly across it, the edge a pixel wide (Golus 2017, "Anti-aliased Alpha Test: The Esoteric Alpha To
//! Coverage"). The samples it covers follow the edge, not a step; a magnified texture's edge, its alpha changing little
//! across a pixel, as sharp as the test's; where the alpha does not change the test's own 0 or 1.
float CoverageAlpha(float a, float fRef, float fWidth)
{
  return saturate((a - fRef) / max(fWidth, 1e-4) + 0.5);
}

//! The alpha a of a draw after its alpha test, or the alpha fFade above 0 of an object the engine fades in or out by its
//! distance (SDrawConstants::vEdgeFade.y; d3d12_FadeBlend, Draw3D.cpp's FadeAlpha): the object's alpha alone, as the
//! original's ambient pass gives it (c0.w, the object's alpha squared) whatever the texture's alpha, the gloss of most
//! of the bump templates (the test before it keeps what the texture's alpha cuts out: a grating's holes stay holes).
//! 0: a, the frames before.
float FadeAlpha(float a, float fFade)
{
  return fFade > 0 ? fFade : a;
}

#endif
