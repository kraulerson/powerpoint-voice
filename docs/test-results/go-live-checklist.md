# Go-Live Checklist — powerpoint-voice (desktop)

Rendered at scaffold time from `docs/platform-modules/desktop.md` (its Go-Live
section is MANDATORY). Tick every box as you verify it in production; the
`phase4_release:go_live_verified` gate blocks while any box is unticked,
any module item is missing, or the Date below is a placeholder (BL-106).

| Field | Value |
|---|---|
| **Date** | [YYYY-MM-DD] |
| **Verified by** | [name] |

- [ ] Installer/package installs correctly on each platform
- [ ] Application launches from installed location (not just dev environment)
- [ ] Uninstaller works cleanly (Windows)
- [ ] File associations work (if applicable)
- [ ] Code signing verified — no security warnings on install (Standard+ Track)
- [ ] Auto-update works (if implemented)
- [ ] Checksums published for all download artifacts
