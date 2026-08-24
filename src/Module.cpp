// v1.1 entry point for legacy addon-facing client helpers.
// Copyright (C) 2026 WarcraftXL. GPLv3.

#include "ExtensionApi.hpp"

namespace wxl_client_extensions
{
    const WXL_Api* g_api = nullptr;
    const WXL_FrameScriptApi* g_frameScript = nullptr;
    const WXL_NetworkApi* g_network = nullptr;
}

const WXL_PluginInfo* __cdecl WXL_Query(void)
{
    static const WXL_PluginInfo info{
        sizeof(WXL_PluginInfo), WXL_API_VERSION, "wxl-client-extensions", 1, WXL_CLIENT_BUILD,
    };
    return &info;
}

int __cdecl WXL_Load(const WXL_Api* api)
{
    using namespace wxl_client_extensions;
    if (!api || api->apiVersion != WXL_API_VERSION) return 0;
    g_api = api;

    if (!ConfigBool("WXL_CLIENT_EXTENSIONS", true))
    {
        WLOG_INFO("extension disabled by configuration");
        return 1;
    }

    g_frameScript = static_cast<const WXL_FrameScriptApi*>(
        api->GetInterface("wxl.framescript", WXL_FRAME_SCRIPT_API_VERSION));
    if (!g_frameScript)
    {
        WLOG_ERROR("required wxl.framescript v1 is unavailable");
        return 0;
    }

    g_network = static_cast<const WXL_NetworkApi*>(
        api->GetInterface("wxl.network", WXL_NETWORK_API_VERSION));
    bool ok = InstallClientHelpers();
    ok &= InstallCompanionGuard();
    if (CanObserveNetwork())
        ok &= InstallNetworkCompatibility();
    else
        WLOG_WARN("network compatibility unavailable; shared runtime transport is disabled or outdated");

    if (ok) WLOG_INFO("legacy helper contracts installed through v1.1 services");
    return ok ? 1 : 0;
}
