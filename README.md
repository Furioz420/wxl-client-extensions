# wxl-client-extensions

Compatibility helpers for addons and private-client scripts written for the earlier Eunoia
client-extension layer.

This ABI 1.1 extension adapts the legacy Lua-facing surface to the shared `wxl.framescript` and
`wxl.network` services. It does not own a packet hook, archive mount, Lua callback validator, or
replacement security path.

Exposed compatibility contracts include:

- `GetSpellDescription(spellId)` and `GetWXLClientExtensionsVersion()`;
- `WXL_HasArgument(name)` and `WXL_GetArgument(name, fallback)`;
- `WXL_ReadClientFileText(path)` for bounded reads from mounted client data;
- the legacy logging helpers;
- `CreateWXLPacket` and `OnWXLPacket` over the shared Runtime transport.

The companion-name sort guard is enabled by default. Stock Warden behavior is retained. The old
rune-tooltip byte rewrite remains disabled until a narrow signature-checked patch contract exists.

## Requirements

- WarcraftXL Core ABI 1.1 with the required script and event bindings.
- `wxl-runtime` 1.1.0 or newer.

## Status

The v1.1 source is published for review. Release publication remains gated on the matching Core
contracts and a clean standalone build.

## License

GPL-3.0-or-later. See `LICENSE`.
