// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Video.h
//	Description: Writes the frames of a demo into a video file.
//
//	The frames go as raw B, G, R, A pixels through a pipe into ffmpeg
//	(which has to be on PATH), and it encodes them as H.264 into an MP4
//	file; its messages go to <file>.log (removed when empty). HDR10 frames
//	(R10G10B10A2: PQ, Rec. 2020) become HEVC Main 10 with the HDR10
//	metadata.
//
//////////////////////////////////////////////////////////////////////

#pragma once

class CVideo
{
public:
  ~CVideo() { Close(); }

  //! Starts ffmpeg for frames of nWidth x nHeight pixels, nFps frames per second. fHdrPeak > 0: the
  //! frames are HDR10 (R10G10B10A2 in 32 bits, red in the low bits), the content's peak fHdrPeak
  //! and its frame average fHdrFrameAverage in nits (the metadata).
  bool Open(const char *szFile, int nWidth, int nHeight, int nFps, float fHdrPeak = 0, float fHdrFrameAverage = 0);
  //! Writes one frame of the size given to Open: B, G, R, A pixels (or HDR10's), rows from the top.
  bool Write(const unsigned char *pPixels);
  //! Ends the file: waits until ffmpeg has written it (two minutes at most). Its exit code, -1 if it
  //! did not end; 0 when nothing was open.
  int Close();

  bool IsOpen() const { return m_hProcess != NULL; }
  int GetWidth() const { return m_nWidth; }
  int GetHeight() const { return m_nHeight; }
  int GetFrames() const { return m_nFrames; }

private:
  void *m_hProcess = NULL;
  void *m_hPipe = NULL;
  std::string m_LogFile;
  int m_nWidth = 0;
  int m_nHeight = 0;
  int m_nFrames = 0;
};
