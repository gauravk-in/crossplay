// FORK CHANGE: see m3_fork.h.

#include "m3_fork.h"

#include "m3_env.h"

uint32_t m3fork_UnlinkedImports(IM3Module i_module, const char** o_module, const char** o_field)
{
    uint32_t missing = 0;
    for (uint32_t i = 0; i < i_module->numFuncImports; ++i)
    {
        IM3Function function = &i_module->functions[i];
        if (function->import.moduleUtf8 == NULL || function->compiled != NULL) continue;
        if (missing == 0)
        {
            if (o_module) *o_module = function->import.moduleUtf8;
            if (o_field) *o_field = function->import.fieldUtf8;
        }
        ++missing;
    }
    return missing;
}

bool m3fork_LimitMemory(IM3Runtime i_runtime, uint32_t i_maxBytes)
{
    M3Memory* memory = &i_runtime->memory;
    const uint32_t pageSize = memory->pageSize ? memory->pageSize : d_m3DefaultMemPageSize;
    const uint32_t maxPages = i_maxBytes / pageSize;
    if (memory->maxPages > maxPages) memory->maxPages = maxPages;
    return memory->numPages <= maxPages;
}
