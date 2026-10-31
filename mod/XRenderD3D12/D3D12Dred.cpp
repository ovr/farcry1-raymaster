// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: D3D12Dred.cpp
//	Description: DRED (Device Removed Extended Data): when the device is
//	removed, what the GPU did, to the engine's log and XRenderD3D12_dred.txt;
//	the markers it records and the test that removes the device.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "D3D12Device.h"

namespace
{

//! D3D12_AUTO_BREADCRUMB_OP's names, by value (d3d12.h).
const char *const s_szOps[] = {
  "SetMarker", "BeginEvent", "EndEvent", "DrawInstanced", "DrawIndexedInstanced", "ExecuteIndirect", "Dispatch",
  "CopyBufferRegion", "CopyTextureRegion", "CopyResource", "CopyTiles", "ResolveSubresource", "ClearRenderTargetView",
  "ClearUnorderedAccessView", "ClearDepthStencilView", "ResourceBarrier", "ExecuteBundle", "Present", "ResolveQueryData",
  "BeginSubmission", "EndSubmission", "DecodeFrame", "ProcessFrames", "AtomicCopyBufferUINT", "AtomicCopyBufferUINT64",
  "ResolveSubresourceRegion", "WriteBufferImmediate", "DecodeFrame1", "SetProtectedResourceSession", "DecodeFrame2",
  "ProcessFrames1", "BuildRaytracingAccelerationStructure", "EmitRaytracingAccelerationStructurePostbuildInfo",
  "CopyRaytracingAccelerationStructure", "DispatchRays", "InitializeMetaCommand", "ExecuteMetaCommand", "EstimateMotion",
  "ResolveMotionVectorHeap", "SetPipelineState1", "InitializeExtensionCommand", "ExecuteExtensionCommand", "DispatchMesh",
  "EncodeFrame", "ResolveEncoderOutputMetadata", "Barrier", "BeginCommandList", "DispatchGraph", "SetProgram" };

std::string OpName(D3D12_AUTO_BREADCRUMB_OP Op)
{
  if ((size_t)Op < std::size(s_szOps))
    return s_szOps[Op];
  if (Op == 52)
    return "ProcessFrames2";
  return "op " + std::to_string((int)Op);
}

std::string AllocationTypeName(D3D12_DRED_ALLOCATION_TYPE Type)
{
  switch (Type)
  {
  case D3D12_DRED_ALLOCATION_TYPE_COMMAND_QUEUE: return "command queue";
  case D3D12_DRED_ALLOCATION_TYPE_COMMAND_ALLOCATOR: return "command allocator";
  case D3D12_DRED_ALLOCATION_TYPE_PIPELINE_STATE: return "pipeline state";
  case D3D12_DRED_ALLOCATION_TYPE_COMMAND_LIST: return "command list";
  case D3D12_DRED_ALLOCATION_TYPE_FENCE: return "fence";
  case D3D12_DRED_ALLOCATION_TYPE_DESCRIPTOR_HEAP: return "descriptor heap";
  case D3D12_DRED_ALLOCATION_TYPE_HEAP: return "heap";
  case D3D12_DRED_ALLOCATION_TYPE_QUERY_HEAP: return "query heap";
  case D3D12_DRED_ALLOCATION_TYPE_COMMAND_SIGNATURE: return "command signature";
  case D3D12_DRED_ALLOCATION_TYPE_PIPELINE_LIBRARY: return "pipeline library";
  case D3D12_DRED_ALLOCATION_TYPE_RESOURCE: return "resource";
  case D3D12_DRED_ALLOCATION_TYPE_PASS: return "pass";
  case D3D12_DRED_ALLOCATION_TYPE_STATE_OBJECT: return "state object";
  case D3D12_DRED_ALLOCATION_TYPE_METACOMMAND: return "meta command";
  default: return "type " + std::to_string((int)Type);
  }
}

const char *DeviceStateName(D3D12_DRED_DEVICE_STATE State)
{
  switch (State)
  {
  case D3D12_DRED_DEVICE_STATE_HUNG: return "hung";
  case D3D12_DRED_DEVICE_STATE_FAULT: return "fault";
  case D3D12_DRED_DEVICE_STATE_PAGEFAULT: return "page fault";
  default: return "unknown";
  }
}

const char *ReasonName(HRESULT hr)
{
  switch (hr)
  {
  case DXGI_ERROR_DEVICE_HUNG: return "DXGI_ERROR_DEVICE_HUNG";
  case DXGI_ERROR_DEVICE_REMOVED: return "DXGI_ERROR_DEVICE_REMOVED";
  case DXGI_ERROR_DEVICE_RESET: return "DXGI_ERROR_DEVICE_RESET";
  case DXGI_ERROR_DRIVER_INTERNAL_ERROR: return "DXGI_ERROR_DRIVER_INTERNAL_ERROR";
  case DXGI_ERROR_INVALID_CALL: return "DXGI_ERROR_INVALID_CALL";
  case E_OUTOFMEMORY: return "E_OUTOFMEMORY";
  case S_OK: return "S_OK";
  default: return "?";
  }
}

std::string Narrow(const wchar_t *sz)
{
  if (!sz || !*sz)
    return {};
  char szOut[512];
  if (WideCharToMultiByte(CP_UTF8, 0, sz, -1, szOut, sizeof(szOut), nullptr, nullptr) <= 0)
    return "?";
  return szOut;
}

std::string ObjectName(const char *szA, const wchar_t *szW)
{
  std::string s = Narrow(szW);
  if (s.empty() && szA)
    s = szA;
  return s.empty() ? "(unnamed)" : s;
}

//! The breadcrumb context (a marker's string) of operation n, or nullptr.
const wchar_t *ContextOf(const D3D12_AUTO_BREADCRUMB_NODE1 &Node, UINT n)
{
  for (UINT i = 0; i < Node.BreadcrumbContextsCount; i++)
    if (Node.pBreadcrumbContexts[i].BreadcrumbIndex == n)
      return Node.pBreadcrumbContexts[i].pContextString;
  return nullptr;
}

//! The last marker string at an operation before n (the section the list was in), or "".
std::string SectionBefore(const D3D12_AUTO_BREADCRUMB_NODE1 &Node, UINT n)
{
  const wchar_t *szBest = nullptr;
  UINT nBest = 0;
  for (UINT i = 0; i < Node.BreadcrumbContextsCount; i++)
  {
    const D3D12_DRED_BREADCRUMB_CONTEXT &C = Node.pBreadcrumbContexts[i];
    if (C.BreadcrumbIndex < n && (!szBest || C.BreadcrumbIndex >= nBest))
    {
      szBest = C.pContextString;
      nBest = C.BreadcrumbIndex;
    }
  }
  return Narrow(szBest);
}

//! Up to nMax names of the allocation nodes from pNode on, "name (type)", and how many there are.
std::string Allocations(const D3D12_DRED_ALLOCATION_NODE1 *pNode, int nMax, int &nCount)
{
  std::string s;
  nCount = 0;
  for (; pNode; pNode = pNode->pNext, nCount++)
  {
    if (nCount >= nMax)
      continue;
    if (!s.empty())
      s += ", ";
    s += "'" + ObjectName(pNode->ObjectNameA, pNode->ObjectNameW) + "' (" + AllocationTypeName(pNode->AllocationType) + ")";
  }
  return s;
}

//! A marker with no arguments as the PIX event runtime (WinPixEventRuntime) encodes it for
//! ID3D12GraphicsCommandList::SetMarker with metadata 2: the event (SetMarker, no arguments, time
//! 0), its colour, the string's info (ANSI, copied in chunks of 8) and the string by 8 bytes,
//! little endian, ending with a 0 byte (a whole 0 when its length is a multiple of 8). DRED
//! takes the string as the breadcrumb's context.
std::vector<uint64> EncodeMarker(const char *sz)
{
  std::vector<uint64> Blob = { (uint64)8 << 10, 0xFF808080ull, ((uint64)8 << 55) | ((uint64)1 << 54) };
  for (size_t i = 0; Blob.size() < 60; i += 8)
  {
    uint64 x = 0;
    bool bEnd = false;
    for (int k = 0; k < 8 && !bEnd; k++)
    {
      const uint64 c = (uint8)sz[i + k];
      bEnd = c == 0;
      x |= c << (8 * k);
    }
    Blob.push_back(x);
    if (bEnd)
      break;
  }
  return Blob;
}

}

void CD3D12Device::DredMarker(ID3D12GraphicsCommandList *pList, const char *szSection)
{
  if (!m_nDred || !pList || !szSection)
    return;
  auto It = std::find_if(m_DredMarkers.begin(), m_DredMarkers.end(), [szSection](const auto &e) { return e.first == szSection; });
  if (It == m_DredMarkers.end())
  {
    m_DredMarkers.emplace_back(szSection, EncodeMarker(szSection));
    It = m_DredMarkers.end() - 1;
  }
  // Metadata 2: the PIX event runtime's blob; DRED ignores a plain string (0, 1) as a context.
  pList->SetMarker(2, It->second.data(), (UINT)(It->second.size() * sizeof(uint64)));
}

void CD3D12Device::DredTest(int nMode)
{
  if (!m_bInFrame || !m_pDevice)
    return;
  if (nMode == 1)
  {
    // The report's path without a GPU fault or a reset; DRED has no command lists for a removal
    // the GPU did not cause (none either when they wait in the queue).
    ComPtr<ID3D12Device5> pDevice5;
    if (FAILED(m_pDevice.As(&pDevice5)))
      return;
    iLog->LogWarning("XRenderD3D12: d3d12_DredTest 1: the device is removed (ID3D12Device5::RemoveDevice)");
    pDevice5->RemoveDevice();
    return;
  }
  if (nMode != 2)
    return;
  ComPtr<ID3D12GraphicsCommandList2> pList2;
  if (FAILED(m_pCmdList.As(&pList2)))
    return;
  // A buffer made only to give its address back: once it is freed the address leads nowhere.
  D3D12_HEAP_PROPERTIES Heap = {};
  Heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC Desc = {};
  Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  Desc.Width = 1 << 20;
  Desc.Height = 1;
  Desc.DepthOrArraySize = 1;
  Desc.MipLevels = 1;
  Desc.SampleDesc.Count = 1;
  Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  ComPtr<ID3D12Resource> pBuffer;
  if (FAILED(m_pDevice->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&pBuffer))))
    return;
  pBuffer->SetName(L"XRenderD3D12 DRED test");
  const D3D12_GPU_VIRTUAL_ADDRESS VA = pBuffer->GetGPUVirtualAddress() + 4096;
  pBuffer.Reset();
  // Should the freed range still be mapped, an address far from everything (2^46 away) after it.
  const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER Writes[2] = { { VA, 0xDEADu }, { VA ^ (1ull << 46), 0xDEADu } };
  GpuSection("dred test");
  pList2->WriteBufferImmediate(2, Writes, nullptr);
  iLog->LogWarning("XRenderD3D12: d3d12_DredTest 2: writes to 0x%llX (a freed buffer's) and 0x%llX", (unsigned long long)Writes[0].Dest,
                   (unsigned long long)Writes[1].Dest);
}

void CD3D12Device::ReportDeviceRemoved(HRESULT hr, const char *szWhere)
{
  if (m_bRemovedReported || !m_pDevice)
    return;
  const HRESULT hrReason = m_pDevice->GetDeviceRemovedReason();
  if (hr != DXGI_ERROR_DEVICE_REMOVED && hr != DXGI_ERROR_DEVICE_RESET && SUCCEEDED(hrReason))
    return;
  m_bRemovedReported = true;
  iLog->LogError("XRenderD3D12: device removed (%s: 0x%08X, reason 0x%08X %s)", szWhere, (unsigned)hr, (unsigned)hrReason, ReasonName(hrReason));
  if (!m_nDred)
  {
    iLog->LogError("XRenderD3D12: no DRED report (d3d12_DRED 0)");
    return;
  }
  ComPtr<ID3D12DeviceRemovedExtendedData1> pDred;
  if (FAILED(m_pDevice.As(&pDred)))
  {
    iLog->LogError("XRenderD3D12: DRED: no data (ID3D12DeviceRemovedExtendedData1)");
    return;
  }
  ComPtr<ID3D12DeviceRemovedExtendedData2> pDred2;
  pDred.As(&pDred2);

  std::unique_ptr<FILE, int (*)(FILE *)> pFile(fopen("XRenderD3D12_dred.txt", "w"), &fclose);
  FILE *f = pFile.get();
  const auto Out = [f](const char *szFormat, auto... Args)
  {
    if (f)
    {
      fprintf(f, szFormat, Args...);
      fputc('\n', f);
    }
  };
  Out("# DRED: what the GPU did when the device was removed");
  Out("removed: %s, 0x%08X; reason 0x%08X %s", szWhere, (unsigned)hr, (unsigned)hrReason, ReasonName(hrReason));
  Out("adapter: %s", m_szAdapter);
  Out("breadcrumbs: %s", m_nDred == 1 ? "the markers only (d3d12_DRED 1)" : "every command (d3d12_DRED 2)");
  if (pDred2)
  {
    const D3D12_DRED_DEVICE_STATE State = pDred2->GetDeviceState();
    Out("device state: %s", DeviceStateName(State));
    iLog->LogError("XRenderD3D12: DRED: device state %s", DeviceStateName(State));
  }

  // The command lists in flight: done, not begun, or stopped part way (the GPU's place).
  D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 Crumbs = {};
  if (SUCCEEDED(pDred->GetAutoBreadcrumbsOutput1(&Crumbs)))
  {
    int nLists = 0, nDone = 0, nNotBegun = 0, nStopped = 0;
    std::string LastFrameSection;     // of the frame's last list done (the nodes come oldest first)
    Out("");
    Out("command lists:");
    for (const D3D12_AUTO_BREADCRUMB_NODE1 *pNode = Crumbs.pHeadAutoBreadcrumbNode; pNode; pNode = pNode->pNext)
    {
      nLists++;
      const UINT nCount = pNode->BreadcrumbCount;
      const UINT nLast = pNode->pLastBreadcrumbValue ? *pNode->pLastBreadcrumbValue : 0;
      const std::string List = ObjectName(pNode->pCommandListDebugNameA, pNode->pCommandListDebugNameW);
      const std::string Queue = ObjectName(pNode->pCommandQueueDebugNameA, pNode->pCommandQueueDebugNameW);
      if (nLast >= nCount)
      {
        // A page fault does not stop the list that made it: its last section tells where it ran.
        nDone++;
        const std::string Section = SectionBefore(*pNode, nCount);
        if (List == "XRenderD3D12 frame" && !Section.empty())
          LastFrameSection = Section;
        Out("  '%s' on '%s': all %u done%s%s%s", List.c_str(), Queue.c_str(), nCount, Section.empty() ? "" : ", the last section \"",
            Section.c_str(), Section.empty() ? "" : "\"");
        continue;
      }
      const bool bBegun = nLast > 0;
      (bBegun ? nStopped : nNotBegun)++;
      Out("  '%s' on '%s': %u of %u done%s", List.c_str(), Queue.c_str(), nLast, nCount, bBegun ? " - STOPPED HERE" : " (not begun)");
      // The commands about the place: 30 before it, all after it (at most 200).
      const UINT nFrom = nLast > 30 ? nLast - 30 : 0;
      const UINT nTo = std::min(nCount, nLast + 200);
      for (UINT i = nFrom; i < nTo; i++)
      {
        const std::string Context = Narrow(ContextOf(*pNode, i));
        Out("    %c %5u %s%s%s%s", i == nLast ? '>' : ' ', i, OpName(pNode->pCommandHistory[i]).c_str(), Context.empty() ? "" : " \"",
            Context.c_str(), Context.empty() ? "" : "\"");
      }
      if (nTo < nCount)
        Out("      ... %u more", nCount - nTo);
      if (bBegun && nStopped <= 4)
      {
        const std::string Section = SectionBefore(*pNode, nLast);
        iLog->LogError("XRenderD3D12: DRED: '%s' stopped after %u of %u commands, at %s%s%s", List.c_str(), nLast, nCount,
                       OpName(pNode->pCommandHistory[nLast]).c_str(), Section.empty() ? "" : ", in section ", Section.c_str());
      }
    }
    Out("%d lists: %d done, %d stopped part way, %d not begun", nLists, nDone, nStopped, nNotBegun);
    if (!nStopped)
      iLog->LogError("XRenderD3D12: DRED: no command list stopped part way (%d lists, %d not begun); the frame's last list done ended in section %s",
                     nLists, nNotBegun, LastFrameSection.empty() ? "?" : LastFrameSection.c_str());
  }
  else
    Out("no breadcrumbs");

  // The page fault: its address, the objects whose memory it is in, and those freed lately.
  D3D12_GPU_VIRTUAL_ADDRESS FaultVA = 0;
  const D3D12_DRED_ALLOCATION_NODE1 *pExisting = nullptr, *pFreed = nullptr;
  bool bFault = false;
  if (pDred2)
  {
    D3D12_DRED_PAGE_FAULT_OUTPUT2 Fault = {};
    if (SUCCEEDED(pDred2->GetPageFaultAllocationOutput2(&Fault)))
    {
      bFault = true;
      FaultVA = Fault.PageFaultVA;
      pExisting = Fault.pHeadExistingAllocationNode;
      pFreed = Fault.pHeadRecentFreedAllocationNode;
    }
  }
  else
  {
    D3D12_DRED_PAGE_FAULT_OUTPUT1 Fault = {};
    if (SUCCEEDED(pDred->GetPageFaultAllocationOutput1(&Fault)))
    {
      bFault = true;
      FaultVA = Fault.PageFaultVA;
      pExisting = Fault.pHeadExistingAllocationNode;
      pFreed = Fault.pHeadRecentFreedAllocationNode;
    }
  }
  Out("");
  if (bFault && (FaultVA || pExisting || pFreed))
  {
    int nExisting = 0, nFreed = 0;
    const std::string Existing = Allocations(pExisting, 1000, nExisting), Freed = Allocations(pFreed, 1000, nFreed);
    Out("page fault at 0x%016llX", (unsigned long long)FaultVA);
    Out("  objects there (%d): %s", nExisting, Existing.empty() ? "none" : Existing.c_str());
    Out("  freed lately (%d): %s", nFreed, Freed.empty() ? "none" : Freed.c_str());
    int n;
    iLog->LogError("XRenderD3D12: DRED: page fault at 0x%llX; there: %s; freed lately: %s", (unsigned long long)FaultVA,
                   nExisting ? Allocations(pExisting, 4, n).c_str() : "nothing", nFreed ? Allocations(pFreed, 4, n).c_str() : "nothing");
  }
  else
    Out("no page fault");
  if (f)
    iLog->LogError("XRenderD3D12: DRED: the report in XRenderD3D12_dred.txt");
}
