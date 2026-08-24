// Legacy addon packet names mirrored by wxl-client-extensions.
// Numeric ownership remains with each feature and its server counterpart; this compatibility
// catalog exists only so the legacy Lua facade can expose those established packet names.
// Copyright (C) 2026 WarcraftXL. GPLv3.

#pragma once

#include <cstdint>

namespace wxl_client_extensions::opcodes
{
    constexpr uint16_t CmsgQuestMarkerRequest           = 0x051F;
    constexpr uint16_t SmsgQuestMarkerUpdate            = 0x0520;
    constexpr uint16_t CmsgRadialPing                    = 0x0521;
    constexpr uint16_t SmsgRadialPing                    = 0x0522;
    constexpr uint16_t CmsgRetailItemVariants            = 0x0523;
    constexpr uint16_t SmsgRetailItemVariants            = 0x0524;
    constexpr uint16_t CmsgSpellChargesRequest           = 0x0525;
    constexpr uint16_t SmsgSpellChargesUpdate            = 0x0526;
    constexpr uint16_t CmsgSkyriding                     = 0x0527;
    constexpr uint16_t SmsgSkyriding                     = 0x0528;
    constexpr uint16_t CmsgMoveAddImpulseAck             = 0x0529;
    constexpr uint16_t SmsgMoveAddImpulse                = 0x052A;
    constexpr uint16_t CmsgChallengeModeRequestState     = 0x052B;
    constexpr uint16_t SmsgChallengeModeState            = 0x052C;
    constexpr uint16_t CmsgStartChallengeMode            = 0x052D;
    constexpr uint16_t SmsgChallengeModeBossInfo         = 0x052E;
    constexpr uint16_t SmsgChallengeModeKeystoneInfo     = 0x052F;
    constexpr uint16_t SmsgChallengeModeComplete         = 0x0530;
    constexpr uint16_t CmsgChallengeModeRequestHistory   = 0x0531;
    constexpr uint16_t SmsgChallengeModeHistory          = 0x0532;
    constexpr uint16_t CmsgSetLootSpecialization         = 0x0533;
    constexpr uint16_t SmsgLootSpecialization            = 0x0534;
    constexpr uint16_t SmsgDisplayToast                  = 0x0535;
    constexpr uint16_t SmsgQuestKillEntries              = 0x0536;
    constexpr uint16_t SmsgQuestCorpsePosition           = 0x0537;
}
