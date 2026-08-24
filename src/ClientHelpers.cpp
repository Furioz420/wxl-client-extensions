// Addon-facing diagnostics, arguments, mounted text reads, and spell descriptions.
// Copyright (C) 2026 WarcraftXL. GPLv3.

#include "ExtensionApi.hpp"

#include "game/Io.hpp"
#include "game/Script.hpp"
#include "game/Spell.hpp"

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace wxl_client_extensions
{
    namespace
    {
        namespace io = wxl::game::io;
        namespace script = wxl::game::script;

        constexpr uint32_t kMaxLuaAssetBytes = 8u * 1024u * 1024u;
        std::vector<std::string> g_arguments;

        void ParseArguments()
        {
            const char* commandLine = GetCommandLineA();
            if (!commandLine) return;

            std::string current;
            bool quoted = false;
            for (const char* cursor = commandLine;; ++cursor)
            {
                const char ch = *cursor;
                if (ch == '"')
                {
                    quoted = !quoted;
                    continue;
                }
                if (ch == '\0' || (!quoted && (ch == ' ' || ch == '\t')))
                {
                    if (!current.empty())
                    {
                        g_arguments.push_back(current);
                        current.clear();
                    }
                    if (ch == '\0') break;
                    continue;
                }
                current.push_back(ch);
            }
        }

        const std::string* FindArgument(const std::string& name)
        {
            for (const std::string& argument : g_arguments)
            {
                if (argument == name) return &argument;
                if (argument.size() > name.size() &&
                    argument.compare(0, name.size(), name) == 0 &&
                    argument[name.size()] == '=')
                    return &argument;
            }
            return nullptr;
        }

        int __cdecl GetSpellDescription(void* state)
        {
            if (!state || !script::IsNumber(state, 1))
            {
                script::PushNil(state);
                return 1;
            }

            char description[1024]{};
            const uint32_t spellId = static_cast<uint32_t>(script::ToNumber(state, 1));
            if (wxl::game::spell::Description(spellId, description, sizeof description))
                script::PushString(state, description);
            else
                script::PushNil(state);
            return 1;
        }

        int __cdecl GetVersion(void* state)
        {
            // Preserve the legacy helper contract: this is a v1.1 implementation change, not an
            // addon-facing API revision.
            script::PushNumber(state, 3.0);
            return 1;
        }

        int __cdecl LuaLog(void* state)
        {
            const int requested = static_cast<int>(script::ToNumber(state, 1));
            size_t length = 0;
            const char* message = script::ToString(state, 2, &length);
            const std::string owned = message ? std::string(message, length) : std::string{};
            const int level = requested >= 3 ? WXL_LOG_ERROR
                            : requested == 2 ? WXL_LOG_WARN
                            : requested == 0 ? WXL_LOG_DEBUG
                                             : WXL_LOG_INFO;
            g_api->Log(level, "wxl-client-extensions", "%s", owned.c_str());
            return 0;
        }

        int __cdecl HasArgument(void* state)
        {
            size_t length = 0;
            const char* value = script::ToString(state, 1, &length);
            script::PushBoolean(state,
                value && FindArgument(std::string(value, length)) != nullptr);
            return 1;
        }

        int __cdecl GetArgument(void* state)
        {
            size_t length = 0;
            const char* value = script::ToString(state, 1, &length);
            const std::string name = value ? std::string(value, length) : std::string{};
            const std::string* found = name.empty() ? nullptr : FindArgument(name);
            if (found && found->size() > name.size())
            {
                script::PushString(state, found->c_str() + name.size() + 1);
                return 1;
            }

            const char* fallback = script::ToString(state, 2);
            if (fallback) script::PushString(state, fallback);
            else script::PushNil(state);
            return 1;
        }

        int __cdecl ReadClientFile(void* state)
        {
            size_t pathLength = 0;
            const char* rawPath = script::ToString(state, 1, &pathLength);
            if (!rawPath || !pathLength)
            {
                script::PushNil(state);
                return 1;
            }

            const std::string path(rawPath, pathLength);
            void* handle = nullptr;
            if (!io::FileOpen(path.c_str(), io::kOpenWholeFile, &handle) || !handle)
            {
                script::PushNil(state);
                return 1;
            }

            uint32_t high = 0;
            const uint32_t size = io::FileSize(handle, &high);
            if (high || !size || size > kMaxLuaAssetBytes)
            {
                io::FileClose(handle);
                script::PushNil(state);
                return 1;
            }

            std::vector<char> data(static_cast<size_t>(size) + 1, '\0');
            uint32_t read = 0;
            const bool ok = io::FileRead(handle, data.data(), size, &read) != 0 && read == size;
            io::FileClose(handle);
            if (ok) script::PushString(state, data.data());
            else script::PushNil(state);
            return 1;
        }

        constexpr char kBootstrap[] = R"lua(
WXLClientExtensions = WXLClientExtensions or {}
WXLClientExtensions.version = GetWXLClientExtensionsVersion()

local function _wxlStringify(...)
 local values={...} local output=""
 for _,value in ipairs(values) do
  if type(value)=="string" then output=output..value
  elseif type(value)=="number" or type(value)=="boolean" then output=output..tostring(value)
  else output=output.."["..type(value).."]" end
 end
 return output
end
function LOG_DEBUG(...) _WXL_CLIENT_LOG(0,_wxlStringify(...)) end
function LOG_INFO(...)  _WXL_CLIENT_LOG(1,_wxlStringify(...)) end
function LOG_WARN(...)  _WXL_CLIENT_LOG(2,_wxlStringify(...)) end
function LOG_ERROR(...) _WXL_CLIENT_LOG(3,_wxlStringify(...)) end
)lua";
    }

    bool InstallClientHelpers()
    {
        ParseArguments();
        bool ok = true;
        ok &= g_frameScript->RegisterFunction("GetSpellDescription", &GetSpellDescription) != 0;
        ok &= g_frameScript->RegisterFunction(
            "GetWXLClientExtensionsVersion", &GetVersion) != 0;
        ok &= g_frameScript->RegisterFunction("_WXL_CLIENT_LOG", &LuaLog) != 0;
        ok &= g_frameScript->RegisterFunction("WXL_HasArgument", &HasArgument) != 0;
        ok &= g_frameScript->RegisterFunction("WXL_GetArgument", &GetArgument) != 0;
        ok &= g_frameScript->RegisterFunction("WXL_ReadClientFileText", &ReadClientFile) != 0;
        ok &= g_frameScript->RegisterScript("wxl-client-extensions", kBootstrap) != 0;

        if (CanRegisterCVar())
            ok &= g_frameScript->RegisterCVar("wxlClientExtensions", "1") != 0;
        else
            WLOG_WARN("runtime FrameScript provider lacks appended RegisterCVar v1 capability");

        if (ConfigBool("WXL_CLIENT_EXTENSIONS_RUNE_TOOLTIP", false))
            WLOG_WARN("rune-tooltip requested but deferred: v1.1 has no signature-checked patch service");
        WLOG_INFO("Warden retains stock client behavior");
        return ok;
    }
}
