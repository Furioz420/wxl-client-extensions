// wxl-client-extensions access to Hub services.
// Copyright (C) 2026 WarcraftXL. GPLv3.

#pragma once

#include "common/ExtensionConfig.hpp"
#include "wxl/FrameScriptApi.h"
#include "wxl/NetworkApi.h"
#include "wxl/PluginApi.h"

#include <cstddef>

namespace wxl_client_extensions
{
    extern const WXL_Api* g_api;
    extern const WXL_FrameScriptApi* g_frameScript;
    extern const WXL_NetworkApi* g_network;

    inline bool ConfigBool(const char* name, bool fallback)
    {
        char value[16]{};
        return wxl::ext::config::Raw(
            name, value, sizeof value,
            "Extensions\\wxl-client-extensions\\wxl-client-extensions.cfg")
            ? wxl::ext::config::Truthy(value, fallback)
            : fallback;
    }

    inline bool CanRegisterCVar()
    {
        return g_frameScript &&
               g_frameScript->structSize >=
                   offsetof(WXL_FrameScriptApi, RegisterCVar) +
                       sizeof(g_frameScript->RegisterCVar) &&
               g_frameScript->RegisterCVar;
    }

    inline bool CanObserveNetwork()
    {
        return g_network &&
               g_network->structSize >=
                   offsetof(WXL_NetworkApi, RegisterServerObserver) +
                       sizeof(g_network->RegisterServerObserver) &&
               g_network->RegisterServerObserver;
    }

    bool InstallClientHelpers();
    bool InstallNetworkCompatibility();
    bool InstallCompanionGuard();
}

#define WLOG_TRACE(...) ::wxl_client_extensions::g_api->Log(WXL_LOG_TRACE, "wxl-client-extensions", __VA_ARGS__)
#define WLOG_DEBUG(...) ::wxl_client_extensions::g_api->Log(WXL_LOG_DEBUG, "wxl-client-extensions", __VA_ARGS__)
#define WLOG_INFO(...)  ::wxl_client_extensions::g_api->Log(WXL_LOG_INFO,  "wxl-client-extensions", __VA_ARGS__)
#define WLOG_WARN(...)  ::wxl_client_extensions::g_api->Log(WXL_LOG_WARN,  "wxl-client-extensions", __VA_ARGS__)
#define WLOG_ERROR(...) ::wxl_client_extensions::g_api->Log(WXL_LOG_ERROR, "wxl-client-extensions", __VA_ARGS__)
