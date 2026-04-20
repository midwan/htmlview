# HTMLview.mcc Improvement Plan

Multi-session plan. Each phase is independently mergeable. Work top-down
unless a later phase is explicitly marked as dependency-free.

**Status legend:** `[ ]` not started · `[~]` in progress · `[x]` done

---

## Phase 1 — Extract net hook into reusable static library `[x]`

**Goal.** Every consumer currently has to copy the ~600-line hook out of
`mcc/test_image_hook.h`. Move it into a proper library so bug fixes
propagate and later phases (TLS verify, cache, gzip, cookies) land in one
place.

**Touches.**
- New: `mcc/net_hook/htmlview_nethook.h` — public API.
- New: `mcc/net_hook/htmlview_nethook.c` — implementation (code moved
  from `test_image_hook.h`, no behavioural change).
- `mcc/Makefile` — add `libhtmlview_nethook.a` per-OS; link test programs
  against it.
- `mcc/SimpleTest.c`, `mcc/LibLoad_Test.c` — drop `#include
  "test_image_hook.h"`, call the new API.
- Delete `mcc/test_image_hook.h`.

**Public API (initial).**
```c
ULONG HTMLviewNet_HookFunc(struct Hook *hook, APTR obj,
                           struct HTMLview_LoadMsg *msg);
void  HTMLviewNet_InitHook(struct Hook *hook); /* sets h_Entry per-platform */
```

**Acceptance.**
- OS3, OS4, MorphOS build clean via the Docker images in `MEMORY.md`.
- SimpleTest + LibLoad_Test still load local images, plain HTTP, HTTPS on
  OS3/OS4 (MorphOS: HTTP only, unchanged).
- `libhtmlview_nethook.a` present in `bin_<os>/` after a build.

**Notes for the next session.**
- The hook already uses task-local bsdsocket bases; the host task never
  needs to open bsdsocket. Drop those `OpenLibrary("bsdsocket.library")`
  calls from both test programs.
- `HAVE_AMISSL` should be a per-object compile flag on the lib's `.o`, not
  on every caller. Keep `AMISSL_SDK_READY` depending on the lib.

---

## Phase 2 — Certificate verification `[x]`

**Depends on:** Phase 1.

**Goal.** Current TLS uses `SSL_VERIFY_NONE`. Anyone on the network can
MITM. Wire a CA bundle and flip to `VERIFY_PEER`.

**Delivered.**
- `HTMLviewNet_SetCABundle(path)` — NULL restores auto-discovery.
- `HTMLviewNet_SetVerifyMode(mode)` — `AUTO` (default), `NONE`, `PEER`.
- AUTO discovers from: user override → `AmiSSL:Certs/curl-ca-bundle.crt`
  → `ENVARC:AmiSSL/Certs/...` → `ENV:AmiSSL/Certs/...`
  → `SYS:Storage/AmiSSL/Certs/...`.
- When verifying: `SSL_CTX_load_verify_locations` + `SSL_VERIFY_PEER`
  and hostname check via `SSL_set1_host` (RFC 6125).
- Failure diagnostics log `X509_verify_cert_error_string()` so the user
  can tell a cert issue from a handshake issue.
- Setters compile in non-AmiSSL builds too (MorphOS) to keep
  cross-platform callers link-clean.

**Notes for next session.**
- Cert store is re-loaded per-request (each transfer builds its own
  SSL_CTX). Cheap enough for now; revisit if latency matters.
- `T:htmlview_hook.log` carries the verify decision ("CA bundle loaded
  from …" or "no CA bundle found, continuing without verification").

---

## Phase 3 — On-disk image/page cache `[x]` (v1)

**Depends on:** Phase 1.

**Goal.** The hook refetches every image on every reload. On Amiga
networking, this is painful; a tiny keyed cache gives huge wins.

**Delivered (v1).**
- `HTMLviewNet_SetCacheDir(path)` / `HTMLviewNet_SetCacheTTL(secs)`.
- Disabled by default; `SetCacheDir(NULL)` clears.
- Key = 16-char hex FNV-1a-64 of URL; entries stored as `<key>.body` +
  `<key>.meta` file pairs in the configured directory.
- Meta format: `URL=<url>\nExpires=<seconds since 1978>\n`.
- Read path: if body+meta exist and `now < Expires`, serve the body file
  directly (opened as `st->file`) with no network I/O.
- Write path: on 200 with Content-Length <= 8 MB and not chunked, drain
  the whole body, persist body+meta with TTL = `HVN_CacheTTL`.
- Log entries: `cache: HIT`, `cache: STORE`, size-skip reasons.

**Deferred to v2.**
- `ETag` / `Last-Modified` capture + `If-None-Match`/`If-Modified-Since`
  revalidation (serve 304 cheaply).
- `Cache-Control: max-age=` + `Expires` server hints.
- Chunked-transfer caching.
- Eviction policy (cache dir currently grows unbounded).

**Acceptance.** `SetCacheDir("T:htmlcache/")`, reload a page, look for
`cache: HIT` in `T:htmlview_hook.log` on the second fetch.

---

## Phase 4 — UTF-8 / charset handling `[ ]`

**Goal.** Pages with `<meta charset="utf-8">` or HTTP `Content-Type:
text/html; charset=utf-8` currently render mojibake. Minimum viable:
detect charset, transliterate UTF-8 → Latin-1 (replacing unrepresentable
codepoints with `?` or the nearest entity).

**Touches.**
- `mcc/Entities.cpp`, `mcc/ParseMessage.cpp` most likely.
- Possibly new `mcc/Charset.cpp` for the transliteration table.
- Hook contract gains: net hook passes the `Content-Type` charset hint
  into the parser (either via `HTMLview_LoadMsg` extension or side-channel
  tag — needs a small design call).

**Acceptance.** A UTF-8 page with `© é ñ` renders them as the Latin-1
byte equivalents, not as two-byte sequences.

---

## Phase 5 — CSS subset `[ ]`

**Goal.** Stop dropping `style=` on the floor. Scope: inline + `<style>`
block.

**Supported properties.** `color`, `background-color`, `font-family`,
`font-size`, `font-weight`, `font-style`, `text-align`, `margin`,
`padding`, `border` on block elements.

**Supported selectors.** type, `.class`, `#id`, descendant. No pseudo-
classes, no cascade beyond "most specific wins by source order".

**Not supporting (document clearly).** Flexbox/grid, media queries, the
full cascade, pseudo-elements, `!important`.

**Touches.** Core parser + layout. New `mcc/CSS.cpp`, `mcc/CSS.h`.

**Acceptance.** A curated test page exercising the subset renders with
the expected colours, margins, alignment.

---

## Phase 6 — `picture.datatype` fallback for unknown formats `[ ]`

**Goal.** Built-in decoders cover GIF/JPEG/PNG. Everything else (WebP,
SVG, AVIF) currently fails. Fall through to `picture.datatype` when no
internal decoder matches; fix the "only works with P96" wrinkle noted in
the README.

**Touches.** `mcc/ImageManager.cpp`, `mcc/_ImageDecoder.c`,
`mcc/IM_Output.cpp`.

**Acceptance.** A page referencing a `.webp` image renders correctly on a
system with `webp.datatype` installed.

---

## Phase 7 — Three long-standing known bugs `[ ]`

From `doc/MCC_HTMLview.readme`:

1. **Text outside `<body>` is not shown.** HTML 4 does not require the
   tag. Remove the gate in the parser. Touches `ParseMessage.cpp` /
   `classes/BodyClass.cpp`.
2. **Entities without trailing `;` are ignored.** Implement HTML5's
   named-entity matching for the common set (`&amp`, `&nbsp`, `&copy`,
   `&lt`, `&gt`). Touches `Entities.cpp`.
3. **Floyd–Steinberg tile seams.** The dither currently leaks error
   across tile edges making backgrounds look striped. Clamp error
   distribution at tile boundaries. Touches `IM_Dither.cpp`.

Each sub-task is independently small; can be three commits.

---

## Phase 8 — Headless rendering regression tests `[ ]`

**Goal.** CI currently only proves the thing *builds*. Add golden-file
tests that diff the parse tree / layout output for known HTML inputs.

**Mechanism.** Build a small Linux host harness (not Amiga) that compiles
the parser + layout and dumps an intermediate representation. Snapshot
against `testdata/golden/*.txt`.

**Touches.** New `tests/` directory, new CI job.

**Acceptance.** `make test` (on Linux host) exits 0 for unchanged HTML
and non-zero when any golden file no longer matches.

---

## Phase 9 — Memory-safety pass `[ ]`

**Goal.** 1990s C++ with manual buffer management. Get a Linux host build
running under AddressSanitizer; feed it normal and malformed HTML. Fix
whatever falls out.

**Scope.** `ParseMessage.cpp`, `ScanArgs.cpp`, `Entities.cpp`, and
anything else the fuzzer reaches. The recent `snprintf` fix in the image
hook suggests siblings exist.

**Acceptance.** ASan-clean for the test corpus; afl fuzzer runs for 1h
with no new crashes.

---

## Phase 10 — gzip `Content-Encoding` in the net hook `[ ]`

**Depends on:** Phase 1.

**Goal.** Modern web servers default to gzip. Without it, we get garbled
bodies or have to request `identity` (which some CDNs ignore). Inflate
via xadmaster.library if present, else bundled miniz (~40 KB).

**Touches.** Net hook only.

**Acceptance.** A gzip-encoded response decodes transparently; no
regression on identity responses.

---

## Phase 11 — Cookie jar hook (opt-in) `[ ]`

**Goal.** Enable logged-in content without the class owning storage. New
`MUIA_HTMLview_CookieHook`: called with the URL + `Set-Cookie` lines on
response, called to fetch applicable cookies before request.

**Touches.** `mcc/HTMLview_mcc.h` (new attr), net hook (Set-Cookie /
Cookie wiring), classes that trigger requests.

**Acceptance.** A curated test server that requires a cookie loads
successfully when the app persists it; not loading when the app doesn't.

---

## Phase 12 — `target="_blank"` notification `[ ]`

**Goal.** Currently `MUIA_HTMLview_Target` is passive — hosts must parse
target strings. Add a dedicated notify carrying URL + target so hosts can
trivially open a new tab/window.

**Touches.** `mcc/HTMLview_mcc.h` (new attr or method), `Dispatcher.cpp`,
anchor handling.

---

## Phase 13 — Modernize docs `[x]`

**Delivered.**
- `README` rewritten: GitHub URL, Docker build commands for all three
  platforms, description of the net-hook static library + its optional
  runtime knobs.
- `TODO` replaced with a short bucket list that points at this file.
- `doc/MCC_HTMLview.readme` updated: v13.6, GitHub URL, "prototype"
  language removed, each long-standing limitation is now cross-
  referenced to its IMPROVEMENTS phase.

**Deferred.** Adding a `mcc/examples/README.md` for SimpleTest +
LibLoad_Test -- out of scope for this pass; the test programs are
self-documenting via their HTML payloads.

---

## Build / test cheat sheet

```bash
# OS3
docker run --rm -v "$(pwd):/work" -w /work/mcc \
  sacredbanana/amiga-compiler:m68k-amigaos sh -c "make -f makefile OS=os3"

# OS4
docker run --rm -v "$(pwd):/work" -w /work/mcc \
  sacredbanana/amiga-compiler:ppc-amigaos sh -c "make -f makefile OS=os4"

# MorphOS
docker run --rm -v "$(pwd):/work" -w /work/mcc \
  sacredbanana/amiga-compiler:ppc-morphos sh -c "make -f makefile OS=mos"
```
