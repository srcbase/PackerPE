//
#include "import_packer.h"

template <int bits>
PeLib::ImportDirectory<bits> GenerateDefaultImports(PeLib::dword stubDataRVA, stub::STUB_DATA& stubDataToUpdate)
{
  PeLib::ImportDirectory<bits> newImp;

  newImp.addFunction("kernel32.dll", "LoadLibraryA");
  newImp.addFunction("kernel32.dll", "GetProcAddress");
  //newImp.addFunction("ntdll.dll", "RtlDecompressBuffer");
  //newImp.addFunction("user32.dll", "CharNextA");

  newImp.setFirstThunk(0, PeLib::NEWDIR, stubDataRVA); // first 2 items are kernel32 libs
  //newImp.setFirstThunk(1, PeLib::NEWDIR, stubDataRVA + sizeof(PeLib::dword) * 2); // 3rd item is one from ntdll

  // NOW WE SHOULD UPDATE IAT WITH POINTERS TO IMAGE_THUNK_DATA

  return newImp;
}

template<int bits>
void dumpImportDirectory(
  PeLib::PeFile& peFile
  , ImportsArr& importOut
  , PeLib::dword newImportTableRVA
  , PeLib::dword stubDataRVA
  , stub::STUB_DATA& stubDataToUpdate)
{
  if (peFile.readImportDirectory() != NO_ERROR)
  {
    return;
  }

  auto& imp = static_cast<PeLib::PeFileT<bits>&>(peFile).impDir();
  imp.rebuild(importOut.old_imports, 0);

  // filling new import table
  auto newImp = GenerateDefaultImports<bits>(stubDataRVA, stubDataToUpdate);
  newImp.rebuild(importOut.new_imports, newImportTableRVA, false);
}

class DumpImportsVisitor : public PeLib::PeFileVisitor
{
public:
  DumpImportsVisitor(ImportsArr& importOut, PeLib::dword newImportTableRVA, PeLib::dword stubRVA, stub::STUB_DATA& stubDataToUpdate):
    importOut_(importOut)
    , newImportTableRVA_(newImportTableRVA)
    , stubRVA_(stubRVA)
    , stubDataToUpdate_(stubDataToUpdate)
  {}
  virtual void callback(PeLib::PeFile32& file)
  {
    dumpImportDirectory<32>(file, importOut_, newImportTableRVA_, stubRVA_, stubDataToUpdate_);
  }
  virtual void callback(PeLib::PeFile64& file)
  {
    dumpImportDirectory<64>(file, importOut_, newImportTableRVA_, stubRVA_, stubDataToUpdate_);
  }
private:
  ImportsArr& importOut_;
  PeLib::dword newImportTableRVA_;
  PeLib::dword stubRVA_;
  stub::STUB_DATA& stubDataToUpdate_;
};

ImportsArr ImportPacker::ProcessExecutable(const AdditionalDataBlocksType& additionalDataBlocks, stub::STUB_DATA& stubDataToUpdate)
{
  // pick last section VA to use it as new imports VA
  auto additionalImportsBlock = std::find_if(
    additionalDataBlocks.begin()
    , additionalDataBlocks.end()
    , [&](auto& valIt)
  {
    return valIt.ownerType == PackerType::kImportPacker &&
      valIt.packerParam == ( int32_t )ImportBlockTypes::kNewImportData;
  });

  auto additionalStubDataBlock = std::find_if(
    additionalDataBlocks.begin()
    , additionalDataBlocks.end()
    , [&](auto& valIt)
  {
    return valIt.ownerType == PackerType::kStubPacker;
  });

  auto newImportTableRVA = 0;
  if (additionalImportsBlock != additionalDataBlocks.end() && additionalStubDataBlock != additionalDataBlocks.end())
  {
    newImportTableRVA = additionalImportsBlock->virtualOffset;
  }
  else
  {
    throw std::runtime_error("unexpected error - missing new import table VA.");
  }

  ImportsArr importsData;
  DumpImportsVisitor importsVisitor(importsData, newImportTableRVA, additionalStubDataBlock->virtualOffset, stubDataToUpdate);
  srcPEFile_->visit(importsVisitor);

  return importsData;
}

std::vector<RequiredDataBlock> ImportPacker::GetRequiredDataBlocks() const
{
  ImportsArr importsData;
  stub::STUB_DATA fake_stub_data;

  DumpImportsVisitor importsVisitor(importsData, 0, 0, fake_stub_data);
  srcPEFile_->visit(importsVisitor);

  std::vector<RequiredDataBlock> result;
  result.push_back({importsData.new_imports.size(), packerType_, (int32_t)ImportBlockTypes::kNewImportData});
  result.push_back({importsData.old_imports.size(), packerType_, (int32_t)ImportBlockTypes::kOldImportData});
  return result;
}