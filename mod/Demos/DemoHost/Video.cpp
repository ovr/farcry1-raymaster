// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Video.cpp
//	Description: Writes the frames of a demo into a video file.
//
//////////////////////////////////////////////////////////////////////

#include "DemoHost.h"
#include "Video.h"

bool CVideo::Open(const char *szFile, int nWidth, int nHeight, int nFps)
{
  Close();
  SECURITY_ATTRIBUTES Inherit = { sizeof(Inherit), NULL, TRUE };
  HANDLE hRead = NULL, hWrite = NULL;
  if (!CreatePipe(&hRead, &hWrite, &Inherit, 1 << 20))
    return false;
  SetHandleInformation(hWrite, HANDLE_FLAG_INHERIT, 0);
  m_LogFile = std::string(szFile) + ".log";
  HANDLE hLog = CreateFileA(m_LogFile.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &Inherit, CREATE_ALWAYS, 0, NULL);

  // The frames are full range RGB; yuv420p is the format every player takes. Converted with the
  // BT.709 matrix and tagged so, as players take untagged HD video for BT.709.
  char szCmd[2048];
  _snprintf(szCmd, sizeof(szCmd) - 1,
    "ffmpeg -hide_banner -loglevel warning -y -f rawvideo -pix_fmt bgra -video_size %dx%d -framerate %d -i - "
    "-vf scale=out_color_matrix=bt709:out_range=tv -c:v libx264 -preset medium -crf 16 -pix_fmt yuv420p "
    "-colorspace bt709 -color_primaries bt709 -color_trc bt709 -color_range tv -movflags +faststart \"%s\"",
    nWidth, nHeight, nFps, szFile);
  szCmd[sizeof(szCmd) - 1] = 0;
  STARTUPINFOA Startup = { sizeof(Startup) };
  Startup.dwFlags = STARTF_USESTDHANDLES;
  Startup.hStdInput = hRead;
  Startup.hStdOutput = hLog != INVALID_HANDLE_VALUE ? hLog : NULL;
  Startup.hStdError = Startup.hStdOutput;
  PROCESS_INFORMATION Process = {};
  BOOL bStarted = CreateProcessA(NULL, szCmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &Startup, &Process);
  CloseHandle(hRead);
  if (hLog != INVALID_HANDLE_VALUE)
    CloseHandle(hLog);
  if (!bStarted)
  {
    CloseHandle(hWrite);
    return false;
  }
  CloseHandle(Process.hThread);
  m_hProcess = Process.hProcess;
  m_hPipe = hWrite;
  m_nWidth = nWidth;
  m_nHeight = nHeight;
  m_nFrames = 0;
  return true;
}

bool CVideo::Write(const unsigned char *pPixels)
{
  if (!m_hPipe)
    return false;
  const size_t nSize = (size_t)m_nWidth * m_nHeight * 4;
  for (size_t nDone = 0; nDone < nSize;)
  {
    DWORD nWritten = 0;
    DWORD nPart = (DWORD)min(nSize - nDone, (size_t)1 << 24);
    if (!WriteFile(m_hPipe, pPixels + nDone, nPart, &nWritten, NULL) || !nWritten)
      return false;
    nDone += nWritten;
  }
  m_nFrames++;
  return true;
}

int CVideo::Close()
{
  if (!m_hProcess)
    return 0;
  // The end of the input makes ffmpeg finish the file.
  CloseHandle(m_hPipe);
  m_hPipe = NULL;
  DWORD nExitCode = (DWORD)-1;
  if (WaitForSingleObject(m_hProcess, 120000) == WAIT_OBJECT_0)
    GetExitCodeProcess(m_hProcess, &nExitCode);
  CloseHandle(m_hProcess);
  m_hProcess = NULL;
  // ffmpeg had nothing to say: no log.
  WIN32_FILE_ATTRIBUTE_DATA Log;
  if (GetFileAttributesExA(m_LogFile.c_str(), GetFileExInfoStandard, &Log) && !Log.nFileSizeLow && !Log.nFileSizeHigh)
    DeleteFileA(m_LogFile.c_str());
  return (int)nExitCode;
}
