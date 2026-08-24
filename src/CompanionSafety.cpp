// Guard the stock companion sorter when an asynchronously queried creature name is still null.
// Copyright (C) 2026 WarcraftXL. GPLv3.

#include "ExtensionApi.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdint>

namespace wxl_client_extensions
{
    namespace
    {
        using SortFn = int(__cdecl*)(const uint32_t* left, const uint32_t* right);
        SortFn g_originalSort = nullptr;

        int ExceptionFilter(unsigned int code)
        {
            return code == EXCEPTION_ACCESS_VIOLATION
                ? EXCEPTION_EXECUTE_HANDLER
                : EXCEPTION_CONTINUE_SEARCH;
        }

        int __cdecl SortCompanions(const uint32_t* left, const uint32_t* right)
        {
            uint32_t leftSpell = 0;
            uint32_t rightSpell = 0;
            __try
            {
                if (left) leftSpell = *left;
                if (right) rightSpell = *right;
                if (g_originalSort) return g_originalSort(left, right);
            }
            __except (ExceptionFilter(GetExceptionCode())) {}

            const uint32_t low = (std::min)(leftSpell, rightSpell);
            const uint32_t high = (std::max)(leftSpell, rightSpell);
            static uint32_t reports = 0;
            if (reports < 8)
            {
                ++reports;
                WLOG_WARN("companion-sort: missing name for spell pair %u/%u; using numeric order",
                          leftSpell, rightSpell);
                if (reports == 8)
                    WLOG_WARN("companion-sort: further fallback diagnostics suppressed");
            }
            return leftSpell < rightSpell ? -1 : leftSpell > rightSpell ? 1 : 0;
        }
    }

    bool InstallCompanionGuard()
    {
        if (!ConfigBool("WXL_CLIENT_EXTENSIONS_COMPANION_GUARD", true))
        {
            WLOG_INFO("companion-name sort guard disabled by configuration");
            return true;
        }
        if (!g_api->HookAttachByName(
                "Client.CompanionSort", reinterpret_cast<void*>(&SortCompanions),
                reinterpret_cast<void**>(&g_originalSort), WXL_HOOK_DEFAULT_PRIORITY))
        {
            WLOG_ERROR("Client.CompanionSort named hook point unavailable");
            return false;
        }
        WLOG_INFO("companion-name sort guard installed");
        return true;
    }
}
