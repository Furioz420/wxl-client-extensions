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

## Integration and release checks

Build the `wxl-client-extensions` Win32 Release target with the matching WXL core and Runtime 1.1 APIs. Install its DLL and reviewed config under `Extensions/wxl-client-extensions`, after Runtime. This compatibility layer contains no client archives or server implementation. Packet users must provide server handlers with the same numeric assignments; the current catalog includes arena (`0x0538`), epic battleground (`0x0539`), and weekly-reward request/state (`0x053A`/`0x053B`) names.

Check the module load log, call `GetWXLClientExtensionsVersion()` and one script helper, then exercise a packet round trip only with its matching server feature installed. Confirm stock login and Warden behavior remain intact. Preserve the previous DLL/config for rollback. The repository's `main` workflow publishes automatically against moving upstream `v1.1`; keep this draft until that core and package have been validated.

## Credits

The WXL core ABI and original module interfaces come from WarcraftXL contributors. The local v1.1 integration commits in this snapshot are attributed to Furioz in the integration history. Preserve source-file notices and the GPL-3.0-or-later `LICENSE` when redistributing source or binaries.
