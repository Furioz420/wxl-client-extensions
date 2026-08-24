// Legacy packet writer/reader facade over v1.1's single-owner wxl.network transport.
// Copyright (C) 2026 WarcraftXL. GPLv3.

#include "ExtensionApi.hpp"
#include "PrivateOpcodes.hpp"

#include "engine/events/Event.hpp"
#include "game/Script.hpp"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace wxl_client_extensions
{
    namespace
    {
        namespace script = wxl::game::script;
        namespace opcode = wxl_client_extensions::opcodes;

        constexpr uint32_t kMaxPayload = 10235;
        constexpr size_t kWriterPoolQuota = 8u * 1024u * 1024u;
        constexpr size_t kMaxOutstandingWriters = 128;
        constexpr uint32_t kWriterIdleTimeoutMs = 5u * 60u * 1000u;

        struct OpcodeName { uint16_t opcode; const char* name; };
        constexpr OpcodeName kClientOpcodes[] = {
            {opcode::CmsgQuestMarkerRequest, "CMSG_WXL_QUEST_TRACKER_REQUEST"},
            {opcode::CmsgRadialPing, "CMSG_WXL_RADIAL_PING"},
            {opcode::CmsgRetailItemVariants, "CMSG_WXL_RETAIL_ITEM_VARIANTS"},
            {opcode::CmsgSpellChargesRequest, "CMSG_WXL_SPELL_CHARGES_REQUEST"},
            {opcode::CmsgSkyriding, "CMSG_WXL_SKYRIDING"},
            {opcode::CmsgMoveAddImpulseAck, "CMSG_MOVE_ADD_IMPULSE_ACK"},
            {opcode::CmsgChallengeModeRequestState, "CMSG_WXL_CHALLENGE_MODE_REQUEST_STATE"},
            {opcode::CmsgStartChallengeMode, "CMSG_START_CHALLENGE_MODE"},
            {opcode::CmsgChallengeModeRequestHistory, "CMSG_WXL_CHALLENGE_MODE_REQUEST_HISTORY"},
            {opcode::CmsgSetLootSpecialization, "CMSG_SET_LOOT_SPECIALIZATION"},
        };

        constexpr OpcodeName kServerOpcodes[] = {
            {opcode::SmsgQuestMarkerUpdate, "SMSG_WXL_QUEST_TRACKER_MARKER"},
            {opcode::SmsgRadialPing, "SMSG_WXL_RADIAL_PING"},
            {opcode::SmsgRetailItemVariants, "SMSG_WXL_RETAIL_ITEM_VARIANTS"},
            {opcode::SmsgSpellChargesUpdate, "SMSG_WXL_SPELL_CHARGES_UPDATE"},
            {opcode::SmsgSkyriding, "SMSG_WXL_SKYRIDING"},
            {opcode::SmsgMoveAddImpulse, "SMSG_MOVE_ADD_IMPULSE"},
            {opcode::SmsgChallengeModeState, "SMSG_WXL_CHALLENGE_MODE_STATE"},
            {opcode::SmsgChallengeModeBossInfo, "SMSG_WXL_CHALLENGE_MODE_BOSS_INFO"},
            {opcode::SmsgChallengeModeKeystoneInfo, "SMSG_WXL_CHALLENGE_MODE_KEYSTONE_INFO"},
            {opcode::SmsgChallengeModeComplete, "SMSG_CHALLENGE_MODE_COMPLETE"},
            {opcode::SmsgChallengeModeHistory, "SMSG_WXL_CHALLENGE_MODE_HISTORY"},
            {opcode::SmsgLootSpecialization, "SMSG_LOOT_SPECIALIZATION"},
            {opcode::SmsgDisplayToast, "SMSG_DISPLAY_TOAST"},
            {opcode::SmsgQuestKillEntries, "SMSG_WXL_QUEST_TRACKER_KILL_ENTRIES"},
            {opcode::SmsgQuestCorpsePosition, "SMSG_WXL_QUEST_TRACKER_CORPSE"},
        };

        const char* ClientOpcodeName(uint16_t opcode)
        {
            for (const OpcodeName& entry : kClientOpcodes)
                if (entry.opcode == opcode) return entry.name;
            return nullptr;
        }

        const char* ServerOpcodeName(uint16_t opcode)
        {
            for (const OpcodeName& entry : kServerOpcodes)
                if (entry.opcode == opcode) return entry.name;
            return nullptr;
        }

        struct Writer
        {
            uint16_t opcode = 0;
            std::vector<uint8_t> payload;
            size_t cursor = 0;
            uint32_t lastTouchedMs = 0;

            template <typename T>
            bool Write(T value)
            {
                if (cursor > kMaxPayload || sizeof(T) > kMaxPayload - cursor) return false;
                if (cursor + sizeof(T) > payload.size()) payload.resize(cursor + sizeof(T));
                std::memcpy(payload.data() + cursor, &value, sizeof(T));
                cursor += sizeof(T);
                return true;
            }

            bool WriteBytes(const void* data, size_t size)
            {
                if ((!data && size) || cursor > kMaxPayload || size > kMaxPayload - cursor)
                    return false;
                if (cursor + size > payload.size()) payload.resize(cursor + size);
                if (size) std::memcpy(payload.data() + cursor, data, size);
                cursor += size;
                return true;
            }
        };

        struct Reader
        {
            const uint8_t* payload = nullptr;
            size_t size = 0;
            size_t cursor = 0;

            void Reset() { cursor = 0; }

            template <typename T>
            T Read(T fallback)
            {
                if (!payload || cursor > size || sizeof(T) > size - cursor) return fallback;
                T value{};
                std::memcpy(&value, payload + cursor, sizeof(T));
                cursor += sizeof(T);
                return value;
            }

            std::string ReadString()
            {
                const uint32_t length = Read<uint32_t>((std::numeric_limits<uint32_t>::max)());
                if (length == (std::numeric_limits<uint32_t>::max)() ||
                    cursor > size || length > size - cursor)
                    return {};
                std::string value(reinterpret_cast<const char*>(payload + cursor), length);
                cursor += length;
                return value;
            }
        };

        std::map<uint32_t, Writer> g_writers;
        uint32_t g_nextWriterId = 1;
        size_t g_writerBytes = 0;
        Reader* g_currentReader = nullptr;

        bool CanGrow(const Writer& writer, size_t bytes)
        {
            if (writer.cursor > kMaxPayload || bytes > kMaxPayload - writer.cursor) return false;
            const size_t needed = writer.cursor + bytes;
            const size_t growth = needed > writer.payload.size() ? needed - writer.payload.size() : 0;
            return g_writerBytes <= kWriterPoolQuota && growth <= kWriterPoolQuota - g_writerBytes;
        }

        void AccountGrowth(Writer& writer, size_t previousSize)
        {
            if (writer.payload.size() > previousSize)
                g_writerBytes += writer.payload.size() - previousSize;
            writer.lastTouchedMs = GetTickCount();
        }

        void ReleaseWriter(const Writer& writer)
        {
            const size_t size = writer.payload.size();
            g_writerBytes = size <= g_writerBytes ? g_writerBytes - size : 0;
        }

        void __cdecl OnUpdate(void*, const void*)
        {
            const uint32_t now = GetTickCount();
            for (auto it = g_writers.begin(); it != g_writers.end();)
            {
                if (now - it->second.lastTouchedMs < kWriterIdleTimeoutMs)
                {
                    ++it;
                    continue;
                }
                ReleaseWriter(it->second);
                it = g_writers.erase(it);
            }
        }

        void __cdecl ObservePacket(const uint8_t* payload, uint32_t size, void* user)
        {
            Reader reader{payload, size, 0};
            g_currentReader = &reader;
            char source[96]{};
            const uint16_t opcode = static_cast<uint16_t>(reinterpret_cast<uintptr_t>(user));
            std::snprintf(source, sizeof source, "__FireWXLPacket(%u)", opcode);
            if (!g_frameScript->Execute(source, "wxl-opcode-observer"))
                WLOG_WARN("Lua state unavailable for observed opcode 0x%04X", opcode);
            g_currentReader = nullptr;
        }

        template <typename T>
        int WriteNumber(void* state)
        {
            const uint32_t id = static_cast<uint32_t>(script::ToNumber(state, 2));
            const auto found = g_writers.find(id);
            if (found == g_writers.end()) return 0;
            Writer& writer = found->second;
            if (!CanGrow(writer, sizeof(T))) return 0;
            const size_t previous = writer.payload.size();
            if (writer.Write<T>(static_cast<T>(script::ToNumber(state, 3))))
                AccountGrowth(writer, previous);
            return 0;
        }

        template <typename T>
        int ReadNumber(void* state)
        {
            const T fallback = static_cast<T>(script::ToNumber(state, 2));
            script::PushNumber(state,
                static_cast<double>(g_currentReader ? g_currentReader->Read<T>(fallback) : fallback));
            return 1;
        }

        enum class Operation : uint32_t
        {
            WriteSize, ReadSize, WriteU8, WriteI8, WriteU16, WriteI16, WriteU32, WriteI32,
            WriteU64, WriteI64, WriteFloat, WriteDouble, WriteString, ReadU8, ReadI8, ReadU16,
            ReadI16, ReadU32, ReadI32, ReadU64, ReadI64, ReadFloat, ReadDouble, ReadString,
            Make, Send, Reset, Observe,
        };

        int __cdecl LuaNetwork(void* state)
        {
            const auto operation = static_cast<Operation>(
                static_cast<uint32_t>(script::ToNumber(state, 1)));
            switch (operation)
            {
            case Operation::WriteSize:
            {
                const auto found = g_writers.find(
                    static_cast<uint32_t>(script::ToNumber(state, 2)));
                script::PushNumber(state, found == g_writers.end()
                    ? 0.0 : static_cast<double>(found->second.payload.size()));
                return 1;
            }
            case Operation::ReadSize:
                script::PushNumber(state, g_currentReader
                    ? static_cast<double>(g_currentReader->size) : 0.0);
                return 1;
            case Operation::WriteU8: return WriteNumber<uint8_t>(state);
            case Operation::WriteI8: return WriteNumber<int8_t>(state);
            case Operation::WriteU16: return WriteNumber<uint16_t>(state);
            case Operation::WriteI16: return WriteNumber<int16_t>(state);
            case Operation::WriteU32: return WriteNumber<uint32_t>(state);
            case Operation::WriteI32: return WriteNumber<int32_t>(state);
            case Operation::WriteU64: return WriteNumber<uint64_t>(state);
            case Operation::WriteI64: return WriteNumber<int64_t>(state);
            case Operation::WriteFloat: return WriteNumber<float>(state);
            case Operation::WriteDouble: return WriteNumber<double>(state);
            case Operation::WriteString:
            {
                const auto found = g_writers.find(
                    static_cast<uint32_t>(script::ToNumber(state, 2)));
                if (found == g_writers.end()) return 0;
                size_t length = 0;
                const char* value = script::ToString(state, 3, &length);
                if (length > kMaxPayload - sizeof(uint32_t)) return 0;
                Writer& writer = found->second;
                if (!CanGrow(writer, sizeof(uint32_t) + length)) return 0;
                const size_t previous = writer.payload.size();
                if (writer.Write<uint32_t>(static_cast<uint32_t>(length)) &&
                    writer.WriteBytes(value ? value : "", length))
                    AccountGrowth(writer, previous);
                return 0;
            }
            case Operation::ReadU8: return ReadNumber<uint8_t>(state);
            case Operation::ReadI8: return ReadNumber<int8_t>(state);
            case Operation::ReadU16: return ReadNumber<uint16_t>(state);
            case Operation::ReadI16: return ReadNumber<int16_t>(state);
            case Operation::ReadU32: return ReadNumber<uint32_t>(state);
            case Operation::ReadI32: return ReadNumber<int32_t>(state);
            case Operation::ReadU64: return ReadNumber<uint64_t>(state);
            case Operation::ReadI64: return ReadNumber<int64_t>(state);
            case Operation::ReadFloat: return ReadNumber<float>(state);
            case Operation::ReadDouble: return ReadNumber<double>(state);
            case Operation::ReadString:
            {
                const std::string value = g_currentReader
                    ? g_currentReader->ReadString() : std::string{};
                script::PushString(state, value.c_str());
                return 1;
            }
            case Operation::Make:
            {
                const double opcodeValue = script::ToNumber(state, 2);
                const double sizeValue = script::ToNumber(state, 3);
                if (!std::isfinite(opcodeValue) || !std::isfinite(sizeValue) ||
                    opcodeValue < 0.0 || opcodeValue > 65535.0 ||
                    sizeValue < 0.0 || sizeValue > kMaxPayload)
                {
                    script::PushNil(state);
                    return 1;
                }
                const uint16_t opcode = static_cast<uint16_t>(opcodeValue);
                const char* name = ClientOpcodeName(opcode);
                if (!name)
                {
                    WLOG_WARN("Lua refused unknown outgoing opcode 0x%04X", opcode);
                    script::PushNil(state);
                    return 1;
                }
                // Idempotent with each feature's later/earlier registration because the names are
                // the same authoritative catalog strings.
                g_network->RegisterClientOpcode(opcode, name);

                const size_t declared = static_cast<size_t>(sizeValue);
                if (g_writers.size() >= kMaxOutstandingWriters ||
                    declared > kWriterPoolQuota - g_writerBytes)
                {
                    script::PushNil(state);
                    return 1;
                }
                while (g_writers.contains(g_nextWriterId)) ++g_nextWriterId;
                Writer writer;
                writer.opcode = opcode;
                writer.payload.resize(declared);
                writer.lastTouchedMs = GetTickCount();
                g_writerBytes += declared;
                const uint32_t id = g_nextWriterId++;
                g_writers.emplace(id, std::move(writer));
                script::PushNumber(state, id);
                return 1;
            }
            case Operation::Send:
            {
                const uint32_t id = static_cast<uint32_t>(script::ToNumber(state, 2));
                const auto found = g_writers.find(id);
                if (found == g_writers.end()) return 0;
                g_network->Send(found->second.opcode, found->second.payload.data(),
                                static_cast<uint32_t>(found->second.payload.size()));
                ReleaseWriter(found->second);
                g_writers.erase(found);
                return 0;
            }
            case Operation::Reset:
                if (g_currentReader) g_currentReader->Reset();
                return 0;
            case Operation::Observe:
            {
                const double opcodeValue = script::ToNumber(state, 2);
                if (!std::isfinite(opcodeValue) || opcodeValue < 0.0 || opcodeValue > 65535.0)
                {
                    script::PushBoolean(state, false);
                    return 1;
                }
                const uint16_t opcode = static_cast<uint16_t>(opcodeValue);
                const char* name = ServerOpcodeName(opcode);
                if (!name)
                {
                    WLOG_WARN("Lua refused unknown incoming opcode 0x%04X", opcode);
                    script::PushBoolean(state, false);
                    return 1;
                }
                char observerName[96]{};
                std::snprintf(observerName, sizeof observerName, "lua-observer:%s", name);
                const bool registered = g_network->RegisterServerObserver(
                    opcode, observerName, &ObservePacket,
                    reinterpret_cast<void*>(static_cast<uintptr_t>(opcode))) != 0;
                script::PushBoolean(state, registered);
                return 1;
            }
            default:
                return 0;
            }
        }

        constexpr char kLuaApi[] = R"lua(
WXL_OPCODES={
 CMSG_QUEST_MARKER_REQUEST=0x051F,SMSG_QUEST_MARKER_UPDATE=0x0520,
 CMSG_RADIAL_PING=0x0521,SMSG_RADIAL_PING=0x0522,
 CMSG_RETAIL_ITEM_VARIANTS=0x0523,SMSG_RETAIL_ITEM_VARIANTS=0x0524,
 CMSG_SPELL_CHARGES_REQUEST=0x0525,SMSG_SPELL_CHARGES_UPDATE=0x0526,
 CMSG_SKYRIDING=0x0527,SMSG_SKYRIDING=0x0528,
 CMSG_MOVE_ADD_IMPULSE_ACK=0x0529,SMSG_MOVE_ADD_IMPULSE=0x052A,
 CMSG_CHALLENGE_MODE_REQUEST_STATE=0x052B,SMSG_CHALLENGE_MODE_STATE=0x052C,
 CMSG_START_CHALLENGE_MODE=0x052D,SMSG_CHALLENGE_MODE_BOSS_INFO=0x052E,
 SMSG_CHALLENGE_MODE_KEYSTONE_INFO=0x052F,SMSG_CHALLENGE_MODE_COMPLETE=0x0530,
 CMSG_CHALLENGE_MODE_REQUEST_HISTORY=0x0531,SMSG_CHALLENGE_MODE_HISTORY=0x0532,
 CMSG_SET_LOOT_SPECIALIZATION=0x0533,SMSG_LOOT_SPECIALIZATION=0x0534,
 SMSG_DISPLAY_TOAST=0x0535,SMSG_QUEST_KILL_ENTRIES=0x0536,
 SMSG_QUEST_CORPSE_POSITION=0x0537
}
-- Preserve the compact legacy keys above while also accepting the exact v1.1 transport names.
-- This lets older addons and newer native-module documentation refer to the same assignments.
WXL_OPCODES.CMSG_WXL_QUEST_TRACKER_REQUEST=WXL_OPCODES.CMSG_QUEST_MARKER_REQUEST
WXL_OPCODES.SMSG_WXL_QUEST_TRACKER_MARKER=WXL_OPCODES.SMSG_QUEST_MARKER_UPDATE
WXL_OPCODES.CMSG_WXL_RADIAL_PING=WXL_OPCODES.CMSG_RADIAL_PING
WXL_OPCODES.SMSG_WXL_RADIAL_PING=WXL_OPCODES.SMSG_RADIAL_PING
WXL_OPCODES.CMSG_WXL_RETAIL_ITEM_VARIANTS=WXL_OPCODES.CMSG_RETAIL_ITEM_VARIANTS
WXL_OPCODES.SMSG_WXL_RETAIL_ITEM_VARIANTS=WXL_OPCODES.SMSG_RETAIL_ITEM_VARIANTS
WXL_OPCODES.CMSG_WXL_SPELL_CHARGES_REQUEST=WXL_OPCODES.CMSG_SPELL_CHARGES_REQUEST
WXL_OPCODES.SMSG_WXL_SPELL_CHARGES_UPDATE=WXL_OPCODES.SMSG_SPELL_CHARGES_UPDATE
WXL_OPCODES.CMSG_WXL_SKYRIDING=WXL_OPCODES.CMSG_SKYRIDING
WXL_OPCODES.SMSG_WXL_SKYRIDING=WXL_OPCODES.SMSG_SKYRIDING
WXL_OPCODES.CMSG_WXL_CHALLENGE_MODE_REQUEST_STATE=WXL_OPCODES.CMSG_CHALLENGE_MODE_REQUEST_STATE
WXL_OPCODES.SMSG_WXL_CHALLENGE_MODE_STATE=WXL_OPCODES.SMSG_CHALLENGE_MODE_STATE
WXL_OPCODES.CMSG_WXL_CHALLENGE_MODE_REQUEST_HISTORY=WXL_OPCODES.CMSG_CHALLENGE_MODE_REQUEST_HISTORY
WXL_OPCODES.SMSG_WXL_CHALLENGE_MODE_HISTORY=WXL_OPCODES.SMSG_CHALLENGE_MODE_HISTORY
WXL_OPCODES.SMSG_WXL_CHALLENGE_MODE_BOSS_INFO=WXL_OPCODES.SMSG_CHALLENGE_MODE_BOSS_INFO
WXL_OPCODES.SMSG_WXL_CHALLENGE_MODE_KEYSTONE_INFO=WXL_OPCODES.SMSG_CHALLENGE_MODE_KEYSTONE_INFO
WXL_OPCODES.SMSG_WXL_QUEST_TRACKER_KILL_ENTRIES=WXL_OPCODES.SMSG_QUEST_KILL_ENTRIES
WXL_OPCODES.SMSG_WXL_QUEST_TRACKER_CORPSE=WXL_OPCODES.SMSG_QUEST_CORPSE_POSITION
local O={WS=0,RS=1,WU8=2,WI8=3,WU16=4,WI16=5,WU32=6,WI32=7,WU64=8,WI64=9,WF=10,WD=11,WSTR=12,RU8=13,RI8=14,RU16=15,RI16=16,RU32=17,RI32=18,RU64=19,RI64=20,RF=21,RD=22,RSTR=23,MAKE=24,SEND=25,RESET=26,OBSERVE=27}
function CreateWXLPacket(opcode,size)
 local w={id=_WXL_NETWORK(O.MAKE,opcode,size or 0)}
 if not w.id then return nil end
 function w:WriteUInt8(v)_WXL_NETWORK(O.WU8,self.id,v)return self end
 function w:WriteInt8(v)_WXL_NETWORK(O.WI8,self.id,v)return self end
 function w:WriteUInt16(v)_WXL_NETWORK(O.WU16,self.id,v)return self end
 function w:WriteInt16(v)_WXL_NETWORK(O.WI16,self.id,v)return self end
 function w:WriteUInt32(v)_WXL_NETWORK(O.WU32,self.id,v)return self end
 function w:WriteInt32(v)_WXL_NETWORK(O.WI32,self.id,v)return self end
 function w:WriteUInt64(v)_WXL_NETWORK(O.WU64,self.id,v)return self end
 function w:WriteInt64(v)_WXL_NETWORK(O.WI64,self.id,v)return self end
 function w:WriteFloat(v)_WXL_NETWORK(O.WF,self.id,v)return self end
 function w:WriteDouble(v)_WXL_NETWORK(O.WD,self.id,v)return self end
 function w:WriteString(v)_WXL_NETWORK(O.WSTR,self.id,v)return self end
 function w:Size()return _WXL_NETWORK(O.WS,self.id)end
 function w:Send()_WXL_NETWORK(O.SEND,self.id)return self end
 return w
end
local function WXLReader()
 local r={}
 function r:ReadUInt8()return _WXL_NETWORK(O.RU8)end
 function r:ReadInt8()return _WXL_NETWORK(O.RI8)end
 function r:ReadUInt16()return _WXL_NETWORK(O.RU16)end
 function r:ReadInt16()return _WXL_NETWORK(O.RI16)end
 function r:ReadUInt32()return _WXL_NETWORK(O.RU32)end
 function r:ReadInt32()return _WXL_NETWORK(O.RI32)end
 function r:ReadUInt64()return _WXL_NETWORK(O.RU64)end
 function r:ReadInt64()return _WXL_NETWORK(O.RI64)end
 function r:ReadFloat()return _WXL_NETWORK(O.RF)end
 function r:ReadDouble()return _WXL_NETWORK(O.RD)end
 function r:ReadString()return _WXL_NETWORK(O.RSTR)end
 function r:Size()return _WXL_NETWORK(O.RS)end
 return r
end
WXL_PACKET_CALLBACKS=WXL_PACKET_CALLBACKS or {}
function OnWXLPacket(opcode,callback)
 if not _WXL_NETWORK(O.OBSERVE,opcode) then return false end
 local list=WXL_PACKET_CALLBACKS[opcode] or {}
 WXL_PACKET_CALLBACKS[opcode]=list
 table.insert(list,callback)
 return true
end
function __FireWXLPacket(opcode)
 local list=WXL_PACKET_CALLBACKS[opcode]
 if not list then return end
 for _,callback in ipairs(list) do callback(WXLReader()) _WXL_NETWORK(O.RESET) end
end
)lua";
    }

    bool InstallNetworkCompatibility()
    {
        bool ok = true;
        ok &= g_frameScript->RegisterFunction("_WXL_NETWORK", &LuaNetwork) != 0;
        ok &= g_frameScript->RegisterScript("wxl-opcode-compatibility", kLuaApi) != 0;
        if (CanRegisterCVar())
            ok &= g_frameScript->RegisterCVar("wxlClientExtensionsNetwork", "1") != 0;
        g_api->Subscribe(static_cast<uint32_t>(wxl::events::Event::OnUpdate), &OnUpdate, nullptr);
        return ok;
    }
}
