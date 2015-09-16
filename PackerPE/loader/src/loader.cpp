// SimplePackPE.cpp : Defines the entry point for the console application.
//

#include "includes.h"
#include "loader.h"

int StubEP()
{
  stub::PSTUB_DATA pStubData;
  // get address of STUB_DATA
  __asm{
    jmp __data_raw_end
  __data_raw:
    nop                 // <-- packer will write RVA of stub data here
    nop
    nop
    nop
  __data_raw_end:
    call __getmyaddr
  __getmyaddr:
    pop eax             // <-- current eip in edx
    sub eax, 4 + 5      // <-- get VA of __data_raw:  [4 - count of nops(sizeof stubDataOffset)] + [5-sizeof call __getmyaddr]
    mov ecx, eax        // save stubDataOffset
    add eax, [ecx]
    mov pStubData, eax
  }

  PPEB Peb;
  __asm {
    mov eax, FS:[0x30];
    mov Peb, eax
    nop
  }

  pStubData->dwOriginalEP += Peb->ImageBaseAddress;
  pStubData->dwImageBase = Peb->ImageBaseAddress;

  UnpackSections(pStubData);
  StartOriginalPE(pStubData);

  return 0;
}

void StartOriginalPE(stub::PSTUB_DATA pStubData)
{
  auto pOriginalEP = reinterpret_cast<LPVOID>(pStubData->dwOriginalEP);
  __asm
  {
    jmp pOriginalEP
  }
}

void UnpackSections(stub::PSTUB_DATA pStubData)
{
  PIMAGE_DOS_HEADER pDOSHead = reinterpret_cast<PIMAGE_DOS_HEADER>(pStubData->dwImageBase);
  PIMAGE_NT_HEADERS32 pNtHead = reinterpret_cast<PIMAGE_NT_HEADERS32>(reinterpret_cast<char*>(pDOSHead) + pDOSHead->e_lfanew);
  DWORD dwSizeOfOptHead = pNtHead->FileHeader.SizeOfOptionalHeader;

  PIMAGE_SECTION_HEADER pSectionHead = reinterpret_cast<PIMAGE_SECTION_HEADER>(reinterpret_cast<char*>(&pNtHead->OptionalHeader) + dwSizeOfOptHead);
  DWORD dwFileAlignment = pNtHead->OptionalHeader.FileAlignment;
  DWORD dwNumberOfSections = pNtHead->FileHeader.NumberOfSections;

  DWORD dwUnpackedSize = 0;
  for (DWORD i = 0; i < dwNumberOfSections; ++i, ++pSectionHead)
  {
    if (pSectionHead->SizeOfRawData == 0)
    {
      continue;
    }

    DWORD dwRsrc = 0x72737263; // "rsrc"
    if (memcmp(reinterpret_cast<char*>(&pSectionHead->Name[1]), reinterpret_cast<char*>(&dwRsrc), 4) == 0)
    {
      continue;
    }

    ((fpRtlDecompressBuffer)pStubData->pRtlDecompressBuffer)(
      COMPRESSION_FORMAT_LZNT1,
      reinterpret_cast<PUCHAR>(pStubData->dwImageBase + pSectionHead->VirtualAddress),
      pSectionHead->SizeOfRawData,
      reinterpret_cast<PUCHAR>(pStubData->dwImageBase + pSectionHead->VirtualAddress),
      pSectionHead->Misc.VirtualSize,
      reinterpret_cast<PULONG>(dwUnpackedSize));
  }
}

