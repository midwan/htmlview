# HTMLview.mcc

An HTML rendering MUI custom class for AmigaOS, MorphOS, and AROS.

Originally written by Allan Odgaard in the late 1990s, now actively maintained
and modernised. HTMLview renders HTML 4 inside any MUI application — email
clients, documentation viewers, help browsers, and more.

**Current version:** 13.7 · **License:** LGPL 2.1+ · **Repo:**
<https://github.com/midwan/htmlview>


## Runtime requirements

| Dependency | Required | Notes |
|---|---|---|
| MUI 3.8+ (`muimaster.library` ≥ 19) | **Yes** | Core UI framework |
| BetterString.mcc | No | Used by the preferences editor for enhanced text input. Falls back to standard MUI `String` gadgets when absent. |
| HotkeyString.mcc | No | Used by the preferences editor for the page-scroll key binding. Falls back to a standard MUI `String` gadget when absent. |
| TextEditor.mcc | No | Required for HTML `<textarea>` form elements. If absent, textarea fields are silently skipped. |

All three optional MCCs ship with standard MUI distributions. The class will
not crash if any of them are missing — every external custom class is tried at
runtime with a built-in fallback.


## Building

Cross-compile from Linux/macOS/Windows using the Docker toolchain images:

```bash
# AmigaOS 3 (m68k)
docker run --rm -v "$(pwd):/work" -w /work/mcc \
    sacredbanana/amiga-compiler:m68k-amigaos sh -c "make -f makefile OS=os3"

# AmigaOS 4 (ppc)
docker run --rm -v "$(pwd):/work" -w /work/mcc \
    sacredbanana/amiga-compiler:ppc-amigaos sh -c "make -f makefile OS=os4"

# MorphOS (ppc)
docker run --rm -v "$(pwd):/work" -w /work/mcc \
    sacredbanana/amiga-compiler:ppc-morphos sh -c "make -f makefile OS=mos"
```

Build products land under `mcc/bin_<os>/`:

| File | Description |
|---|---|
| `HTMLview.mcc` | The MUI custom class |
| `HTMLview.mcp` | Preferences editor (MUI preferences panel) |
| `libhtmlview_nethook.a` | Reusable image/content load hook library |
| `SimpleTest`, `LibLoad_Test` | Reference programs exercising the class |

CI builds all three platforms on every push and attaches binaries to releases.


## Net hook library

Host applications that want HTTP/HTTPS image loading link
`libhtmlview_nethook.a` and call `HTMLviewNet_InitHook(&hook)` at startup —
see `mcc/net_hook/htmlview_nethook.h` for the full API. The hook opens
bsdsocket/AmiSSL lazily from the decoder task, so the host never needs to
open a networking library itself.

Runtime knobs (all hook-local, no MUI tags required):

```c
HTMLviewNet_SetCABundle(path)      // override AmiSSL CA trust store
HTMLviewNet_SetVerifyMode(mode)    // AUTO / NONE / PEER
HTMLviewNet_SetCacheDir(path)      // enable on-disk response cache
HTMLviewNet_SetCacheTTL(seconds)   // fallback TTL for unhinted responses
```


## What's new since v12

Maintenance picked up at v12.6 (the last SourceForge release). Highlights
through v13.7:

- **Robustness** — runtime fallback for missing external MCCs (BetterString,
  HotkeyString); NULL-safety in HTML form gadgets; no system crashes when
  optional classes are absent.
- **HTTPS** — TLS via AmiSSL with certificate verification and http→https
  redirect following.
- **Net hook library** — the ~600-line image load hook extracted into a
  reusable static library (`libhtmlview_nethook.a`) with TLS verify, disk
  cache, and diagnostics logging.
- **MUI 4 compatibility** — backfill handler guards that were causing
  rendering corruption on MUI 4+ (Zune, MUI 5) fixed.
- **Scrollbars reworked** — `MUIA_HTMLview_Scrollbars` is now a no-op. The
  class always returns the bare HTMLview gadget. Wrap externally with
  `MUIC_Scrollgroup` for scrollbars (`SimpleTest.c` shows the pattern). This
  fixes compatibility with class-introspecting hosts like RapaGUI.
- **Multi-platform CI** — automated builds for AmigaOS 3, AmigaOS 4, and
  MorphOS on every push, with GitHub Release publishing.
- **Crash fixes** — half-constructed OM_DISPOSE path that rebooted MUI 4+
  machines, sprintf→snprintf overflow, and various null-pointer paths.


## Behaviour notes

- **Text outside `<body>` is not shown.** HTML 4 does not require the tag,
  but the current parser gates on it. Tracked in IMPROVEMENTS.md Phase 7.
- **Entities without trailing `;` are ignored.** `&amp` vs `&amp;`. Also
  tracked in Phase 7.
- **No CSS support.** Inline `style=` attributes are silently dropped. CSS
  subset support is planned (Phase 5).
- **Built-in image decoders:** GIF, JPEG, PNG only. Other formats require a
  matching datatype (Phase 6 will add `picture.datatype` fallback).


## Contributing

Issues and pull requests are welcome on GitHub. The roadmap lives in
`IMPROVEMENTS.md`; each phase is scoped to be independently mergeable.


## Credits

Originally by Allan Odgaard. Maintained since 2023 by Dimitris Panokostas
with contributions from Alfonso Ranieri, Ilkka Lehtoranta, Jens Langner,
Thore Boeckelmann, and Dwight Meese. See `AUTHORS` for the full list.
