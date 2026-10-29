// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: DemoCompare.cpp
//	Description: Compares the saved frames of two demo runs.
//
//	DemoCompare <folder A> <folder B> [--tolerance <n>] [--diff <folder>]
//
//	Every TGA file of folder A is compared with the file of the same name
//	in folder B, e.g. DemoShots\old and DemoShots\<our renderer>, or two
//	runs of the same renderer to see that a demo repeats itself. Per file
//	it prints the number of pixels whose blue, green or red value differs
//	by more than the tolerance (default 0) and the largest difference;
//	alpha is left out, the back buffer's alpha is not part of the picture.
//	With --diff, a TGA file per differing frame shows where: the frame of
//	A dimmed to grey, the differing pixels in red, brighter for larger
//	differences.
//
//	The exit code is 0 when every file has its twin and they are equal
//	(within the tolerance), 1 when some differ or have no twin, 2 for bad
//	arguments or files that can't be read.
//
//////////////////////////////////////////////////////////////////////

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

#include "Tga.h"

//! Names of the TGA files in szDir, sorted.
static std::vector<std::string> FindFrames(const std::string &Dir)
{
  std::vector<std::string> Names;
  WIN32_FIND_DATAA Data;
  HANDLE hFind = FindFirstFileA((Dir + "\\*.tga").c_str(), &Data);
  if (hFind == INVALID_HANDLE_VALUE)
    return Names;
  do
  {
    if (!(Data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
      Names.push_back(Data.cFileName);
  } while (FindNextFileA(hFind, &Data));
  FindClose(hFind);
  std::sort(Names.begin(), Names.end(), [](const std::string &A, const std::string &B) { return _stricmp(A.c_str(), B.c_str()) < 0; });
  return Names;
}

static bool Contains(const std::vector<std::string> &Names, const std::string &Name)
{
  for (const std::string &Other : Names)
    if (!_stricmp(Other.c_str(), Name.c_str()))
      return true;
  return false;
}

enum EResult
{
  eEqual,
  eDiffer,
  eError,
};

static EResult CompareFrame(const std::string &FileA, const std::string &FileB, const char *szName, int nTolerance,
  const std::string &DiffDir)
{
  int nWidthA = 0, nHeightA = 0, nWidthB = 0, nHeightB = 0;
  std::vector<unsigned char> A, B;
  if (!Tga_Read(FileA.c_str(), nWidthA, nHeightA, A) || !Tga_Read(FileB.c_str(), nWidthB, nHeightB, B))
  {
    printf("%s: can't be read (an uncompressed 24- or 32-bit TGA file is needed)\n", szName);
    return eError;
  }
  if (nWidthA != nWidthB || nHeightA != nHeightB)
  {
    printf("%s: %dx%d against %dx%d\n", szName, nWidthA, nHeightA, nWidthB, nHeightB);
    return eDiffer;
  }

  const int nPixels = nWidthA * nHeightA;
  int nDiffer = 0, nLargest = 0;
  std::vector<unsigned char> Diff(DiffDir.empty() ? 0 : nPixels * 4);
  for (int i = 0; i < nPixels; i++)
  {
    const unsigned char *pA = &A[i * 4], *pB = &B[i * 4];
    int nDelta = 0;
    for (int c = 0; c < 3; c++)
      nDelta = std::max(nDelta, abs(pA[c] - pB[c]));
    nLargest = std::max(nLargest, nDelta);
    const bool bDiffers = nDelta > nTolerance;
    nDiffer += bDiffers;
    if (!Diff.empty())
    {
      unsigned char *pDiff = &Diff[i * 4];
      if (bDiffers)
      {
        pDiff[0] = pDiff[1] = 0;
        pDiff[2] = (unsigned char)std::min(255, 128 + nDelta * 4);
      }
      else
        pDiff[0] = pDiff[1] = pDiff[2] = (unsigned char)((pA[0] + pA[1] * 2 + pA[2]) / 12);
      pDiff[3] = 255;
    }
  }

  if (!nDiffer)
  {
    printf("%s: equal (largest difference %d)\n", szName, nLargest);
    return eEqual;
  }
  printf("%s: %d pixels differ (%.3f%%), largest difference %d\n", szName, nDiffer, 100.0 * nDiffer / nPixels, nLargest);
  if (!Diff.empty())
  {
    std::string DiffFile = DiffDir + "\\" + szName;
    if (!Tga_Write(DiffFile.c_str(), nWidthA, nHeightA, Diff.data(), nWidthA * 4))
      printf("%s: can't write %s\n", szName, DiffFile.c_str());
  }
  return eDiffer;
}

int main(int argc, char **argv)
{
  std::vector<std::string> Dirs;
  std::string DiffDir;
  int nTolerance = 0;
  for (int i = 1; i < argc; i++)
  {
    if (!strcmp(argv[i], "--tolerance") && i + 1 < argc)
      nTolerance = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--diff") && i + 1 < argc)
      DiffDir = argv[++i];
    else if (argv[i][0] != '-')
      Dirs.push_back(argv[i]);
    else
      Dirs.clear(), i = argc;
  }
  if (Dirs.size() != 2)
  {
    printf("Usage: DemoCompare <folder A> <folder B> [--tolerance <n>] [--diff <folder>]\n");
    return 2;
  }
  if (!DiffDir.empty())
    CreateDirectoryA(DiffDir.c_str(), NULL);

  const std::vector<std::string> NamesA = FindFrames(Dirs[0]), NamesB = FindFrames(Dirs[1]);
  if (NamesA.empty())
  {
    printf("%s: no TGA files\n", Dirs[0].c_str());
    return 2;
  }
  int nEqual = 0, nDiffer = 0, nMissing = 0, nErrors = 0;
  for (const std::string &Name : NamesA)
  {
    if (!Contains(NamesB, Name))
    {
      printf("%s: not in %s\n", Name.c_str(), Dirs[1].c_str());
      nMissing++;
      continue;
    }
    switch (CompareFrame(Dirs[0] + "\\" + Name, Dirs[1] + "\\" + Name, Name.c_str(), nTolerance, DiffDir))
    {
    case eEqual: nEqual++; break;
    case eDiffer: nDiffer++; break;
    default: nErrors++; break;
    }
  }
  for (const std::string &Name : NamesB)
    if (!Contains(NamesA, Name))
    {
      printf("%s: not in %s\n", Name.c_str(), Dirs[0].c_str());
      nMissing++;
    }

  printf("%d equal, %d differ, %d without a twin, %d unreadable (tolerance %d)\n", nEqual, nDiffer, nMissing, nErrors, nTolerance);
  return nErrors ? 2 : nDiffer || nMissing ? 1 : 0;
}
