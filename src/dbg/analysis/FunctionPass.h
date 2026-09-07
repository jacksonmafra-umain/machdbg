#pragma once

#include <functional>
#include "AnalysisPass.h"
#include "BasicBlock.h"

class FunctionPass : public AnalysisPass
{
public:
    FunctionPass(duint VirtualStart, duint VirtualEnd, BBlockArray & MainBlocks);

    const char* GetName() override;
    bool Analyse() override;

private:
    duint mModuleStart = 0;

    void AnalysisWorker(duint Start, duint End, std::vector<FunctionDef>* Blocks);
    void FindFunctionWorkerPrepass(duint Start, duint End, std::vector<FunctionDef>* Blocks);
    void FindFunctionWorker(std::vector<FunctionDef>* Blocks);

    bool ResolveKnownFunctionEnd(FunctionDef* Function);
    bool ResolveFunctionEnd(FunctionDef* Function, BasicBlock* LastBlock);

#ifdef _WIN64
    std::vector<RUNTIME_FUNCTION> mRuntimeFunctions;
#endif // _WIN64
};