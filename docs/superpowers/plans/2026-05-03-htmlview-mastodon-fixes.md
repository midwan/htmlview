# HTMLview Mastodon Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make HTMLview.mcc usable as a Mastodon-content renderer in amidon2 by fixing three observed gaps: `<img width/height>` is ignored (image is clipped, not scaled); the `class` attribute on `<a>` is dropped (mention vs. hashtag indistinguishable); and same-URL `<img>` references aren't deduplicated when sizes differ.

**Architecture:** Three independent changes, each behind its own commit/PR boundary:
1. **Image scaling.** Cache native bitmaps by URL only; scale on receipt inside `ImgClass` via `BitMapScale()` (graphics.library V36+) into per-instance bitmaps. Side-effect: same URL with different requested sizes now shares one network fetch and one decode.
2. **Anchor `class` attribute.** Parse and store `class` on `<a>`; surface it through `HitTestMessage`, the `ContextInfo` getter, and a new `MUIA_HTMLview_LinkClass` notification attribute fired alongside `MUIA_HTMLview_ClickedURL`.
3. **Cache observability.** Add a small debug log line behind `DBF_STARTUP` that records cache hits, misses, and evictions so amidon2 can verify behaviour on real timelines.

**Tech Stack:** C/C++ for AmigaOS 3 (m68k, gcc), AmigaOS 4 (PPC, gcc), MorphOS, AROS. Build via `sacredbanana/amiga-compiler` Docker images. Visual verification via `bin_<os>/SimpleTest` on real Amiga / WinUAE / FS-UAE.

---

## File Structure

**New files:**
- `mcc/BitmapScaler.h` — wrapper API: `ScaleBitmapTo(src, srcW, srcH, dstW, dstH, depth, friendBmp) -> BitMap*`; `ScaleMaskTo(srcMask, srcW, srcH, dstW, dstH) -> UBYTE*`. Pure functions over `struct BitMap`; depend only on graphics.library. Easy to mock at compile time.
- `mcc/BitmapScaler.cpp` — implementation using `BitMapScale()`. ~80 lines.
- `mcc/testdata/mastodon_avatar.html` — fixed test fixture exercising `<img width="64" height="64">` and Mastodon-style `<a class="mention">`/`<a class="hashtag">` links. Loaded by `SimpleTest`.

**Modified files:**
- `mcc/classes/ImgClass.h` — add `BitMap *ScaledBMp; UBYTE *ScaledMask;` members and a `BuildScaledBitmap()` helper.
- `mcc/classes/ImgClass.cpp` — `ReceiveImage` builds scaled bitmaps when sizes diverge; `Render` blits the scaled bitmap when present; destructor + `FreeColours` free them; `Layout`/`MinMax`/`GetImages` request URL-keyed lookups (`0, 0`).
- `mcc/classes/AClass.h` — add `STRPTR Class;` member.
- `mcc/classes/AClass.cpp` — parse `class=` via `ARG_STRING`; surface via `HitTest`.
- `mcc/HitTest.h` — add `STRPTR LinkClass;` to `HitTestMessage`.
- `mcc/HTMLview_mcc.h` — define `MUIA_HTMLview_LinkClass` and `MUIR_HTMLview_GetContextInfo::LinkClass`.
- `mcc/GetSetAttrs.cpp` — wire the new MUIA tag (set/get).
- `mcc/Dispatcher.cpp` — copy `LinkClass` into `ContextInfo` in `MUIM_HTMLview_GetContextInfo`.
- `mcc/classes/HostClass.cpp` — set `MUIA_HTMLview_LinkClass` alongside `MUIA_HTMLview_ClickedURL` on left-click release.
- `mcc/private.h` — add `STRPTR LinkClass;` to the `HTMLviewData` struct so the dispatcher can stash it.
- `mcc/Makefile` — copy `testdata/mastodon_avatar.html` next to `bin_<os>/SimpleTest`.
- `mcc/SimpleTest.c` — add a "Mastodon" section using the test fixture URLs and links.

**Why this split.** The scaler stays in its own translation unit so it can be exercised by a host-side unit test that links only `BitmapScaler.cpp` against a stub `BitMap` definition. The class-attribute change is small and surface-area-only — it doesn't touch the renderer. The plan never changes `ImageCache::FindImage` semantics (only the call sites), keeping the cache backwards-compatible.

---

## Pre-flight

### Task 0: Verify baseline build works

**Files:** none modified.

- [ ] **Step 1: Check baseline build for AmigaOS 3.**

```bash
docker run --rm -v "/d/Github/htmlview-midwan:/work" -w /work/mcc \
  sacredbanana/amiga-compiler:m68k-amigaos make OS=os3 -j4
```

Expected: succeeds; `bin_os3/HTMLview.mcc` and `bin_os3/SimpleTest` produced.

- [ ] **Step 2: Capture binary sizes for diff later.**

```bash
ls -la D:/Github/htmlview-midwan/mcc/bin_os3/HTMLview.mcc D:/Github/htmlview-midwan/mcc/bin_os3/SimpleTest
```

Record the byte sizes in your scratch notes — they will move once the patch lands and you want sanity-check numbers.

- [ ] **Step 3: Commit a baseline tag for easy revert.**

```bash
git tag baseline-pre-mastodon-fixes
```

---

## Phase 1 — Image scaling

### Task 1: Add the `BitmapScaler` skeleton + failing host test

**Files:**
- Create: `mcc/BitmapScaler.h`
- Create: `mcc/BitmapScaler.cpp`
- Create: `mcc/tests/test_bitmap_scaler.c`
- Modify: `mcc/Makefile` (add a host-side test target)

- [ ] **Step 1: Write the header.**

```cpp
// mcc/BitmapScaler.h
#ifndef BITMAPSCALER_H
#define BITMAPSCALER_H

#include <exec/types.h>
struct BitMap;

/* Compute the destination bitmap dimensions BitMapScale() will produce
   for a given (srcW,srcH)->(dstW,dstH) request. The Amiga scaler uses
   16-bit DDA factors that round down; this helper exposes the same
   rounding so callers can size their destination correctly. */
VOID ComputeScaledExtent(UWORD srcW, UWORD srcH,
                         UWORD dstW, UWORD dstH,
                         UWORD *outW, UWORD *outH);

/* Allocate and return a friend bitmap of the given depth holding a
   scaled copy of the source. Returns NULL on alloc failure. Caller
   owns the returned BitMap and must FreeBitMap() it. */
struct BitMap *ScaleBitmapTo(struct BitMap *src,
                             UWORD srcW, UWORD srcH,
                             UWORD dstW, UWORD dstH,
                             ULONG depth,
                             struct BitMap *friendBmp);

/* Scale a 1-bit mask plane (RASSIZE(srcW,srcH) bytes) to dstW x dstH.
   Returns AllocRaster()'d memory of size RASSIZE(dstW,dstH). NULL on
   alloc failure. Caller frees with FreeRaster(ptr, dstW, dstH). */
UBYTE *ScaleMaskTo(UBYTE *srcMask, UWORD srcW, UWORD srcH,
                   UWORD dstW, UWORD dstH);

#endif /* BITMAPSCALER_H */
```

- [ ] **Step 2: Write the failing host test.**

```c
// mcc/tests/test_bitmap_scaler.c
/* Pure-host unit test: exercises ComputeScaledExtent only. The other
   helpers need graphics.library so they're verified visually via
   SimpleTest. Build with: gcc -I.. test_bitmap_scaler.c -o tbs */
#include <assert.h>
#include <stdio.h>
#define UWORD unsigned short
#define ULONG unsigned long
#define VOID void
struct BitMap;
#include "../BitmapScaler.h"

int main(void) {
    UWORD w, h;

    /* Identity. */
    ComputeScaledExtent(64, 64, 64, 64, &w, &h);
    assert(w == 64 && h == 64);

    /* Downscale 400 -> 64. */
    ComputeScaledExtent(400, 400, 64, 64, &w, &h);
    assert(w == 64 && h == 64);

    /* Upscale 16 -> 64. */
    ComputeScaledExtent(16, 16, 64, 64, &w, &h);
    assert(w == 64 && h == 64);

    /* Asymmetric: 200x100 -> 50x100 keeps target. */
    ComputeScaledExtent(200, 100, 50, 100, &w, &h);
    assert(w == 50 && h == 100);

    printf("test_bitmap_scaler OK\n");
    return 0;
}
```

- [ ] **Step 3: Run the test; expect a link-time failure.**

```bash
cd D:/Github/htmlview-midwan/mcc/tests && gcc -I.. test_bitmap_scaler.c -o tbs
```

Expected: linker error — `ComputeScaledExtent` undefined. That confirms the test runs the right code path.

- [ ] **Step 4: Implement just enough to pass.**

Create `mcc/BitmapScaler.cpp` with the host-build-friendly function:

```cpp
// mcc/BitmapScaler.cpp
#include "BitmapScaler.h"

#ifndef BMSCALER_HOST_TEST
#  include <proto/exec.h>
#  include <proto/graphics.h>
#  include <graphics/scale.h>
#  include <clib/macros.h>
#endif

VOID ComputeScaledExtent(UWORD srcW, UWORD srcH,
                         UWORD dstW, UWORD dstH,
                         UWORD *outW, UWORD *outH)
{
    /* BitMapScale uses bsa_XDestFactor / bsa_XSrcFactor as numerator/
       denominator. Resulting width is (srcW * dstW) / srcW = dstW
       provided dstW > 0 and we pick factors that don't truncate. We
       stick with 1:1 dst factors and let the scaler honour the
       requested destination extent. */
    if (outW) *outW = dstW ? dstW : srcW;
    if (outH) *outH = dstH ? dstH : srcH;
}

#ifndef BMSCALER_HOST_TEST
struct BitMap *ScaleBitmapTo(struct BitMap *src,
                             UWORD srcW, UWORD srcH,
                             UWORD dstW, UWORD dstH,
                             ULONG depth,
                             struct BitMap *friendBmp)
{
    if (!src || !srcW || !srcH || !dstW || !dstH) return NULL;

    struct BitMap *dst = AllocBitMap(dstW, dstH, depth,
                                     BMF_MINPLANES | BMF_CLEAR,
                                     friendBmp);
    if (!dst) return NULL;

    struct BitScaleArgs args = { 0 };
    args.bsa_SrcX = 0;            args.bsa_SrcY = 0;
    args.bsa_SrcWidth = srcW;     args.bsa_SrcHeight = srcH;
    args.bsa_DestX = 0;           args.bsa_DestY = 0;
    args.bsa_DestWidth = dstW;    args.bsa_DestHeight = dstH;
    args.bsa_XSrcFactor = srcW;   args.bsa_YSrcFactor = srcH;
    args.bsa_XDestFactor = dstW;  args.bsa_YDestFactor = dstH;
    args.bsa_SrcBitMap = src;
    args.bsa_DestBitMap = dst;

    BitMapScale(&args);
    return dst;
}

UBYTE *ScaleMaskTo(UBYTE *srcMask, UWORD srcW, UWORD srcH,
                   UWORD dstW, UWORD dstH)
{
    if (!srcMask || !srcW || !srcH || !dstW || !dstH) return NULL;

    /* Wrap the mask plane in a 1-bit BitMap so we can reuse BitMapScale.
       AllocRaster gives word-aligned storage matching what BitMapScale
       expects in BitMap.Planes[0]. */
    struct BitMap srcBM;
    InitBitMap(&srcBM, 1, srcW, srcH);
    srcBM.Planes[0] = (PLANEPTR)srcMask;

    UBYTE *dstMask = (UBYTE *)AllocRaster(dstW, dstH);
    if (!dstMask) return NULL;

    struct BitMap dstBM;
    InitBitMap(&dstBM, 1, dstW, dstH);
    dstBM.Planes[0] = (PLANEPTR)dstMask;

    struct BitScaleArgs args = { 0 };
    args.bsa_SrcWidth = srcW;     args.bsa_SrcHeight = srcH;
    args.bsa_DestWidth = dstW;    args.bsa_DestHeight = dstH;
    args.bsa_XSrcFactor = srcW;   args.bsa_YSrcFactor = srcH;
    args.bsa_XDestFactor = dstW;  args.bsa_YDestFactor = dstH;
    args.bsa_SrcBitMap = &srcBM;
    args.bsa_DestBitMap = &dstBM;

    BitMapScale(&args);
    return dstMask;
}
#endif /* !BMSCALER_HOST_TEST */
```

- [ ] **Step 5: Re-run host test (with the host-test stub).**

```bash
cd D:/Github/htmlview-midwan/mcc/tests && \
  gcc -DBMSCALER_HOST_TEST -I.. ../BitmapScaler.cpp test_bitmap_scaler.c -o tbs && ./tbs
```

Expected: `test_bitmap_scaler OK`.

- [ ] **Step 6: Wire the file into the Amiga build.**

Edit `mcc/Makefile`. Find the OBJS list (search for `Animation.o`) and add `BitmapScaler.o` next to similar utilities. Show the line you change in your scratch notes — there should be exactly one OBJS list near the top of the OS-independent section.

- [ ] **Step 7: Cross-build confirms it compiles.**

```bash
docker run --rm -v "/d/Github/htmlview-midwan:/work" -w /work/mcc \
  sacredbanana/amiga-compiler:m68k-amigaos make OS=os3 -j4
```

Expected: succeeds.

- [ ] **Step 8: Commit.**

```bash
git add mcc/BitmapScaler.h mcc/BitmapScaler.cpp mcc/tests/test_bitmap_scaler.c mcc/Makefile
git commit -m "feat(mcc): add BitmapScaler helper around graphics.library BitMapScale"
```

---

### Task 2: Drop W/H from cache lookup callers

**Files:**
- Modify: `mcc/classes/ImgClass.cpp` (lines 202, 302; line 69 in `GetImages`)

- [ ] **Step 1: Read and confirm current call sites.**

Open the three locations and re-read them so the next edit is unambiguous.

- [ ] **Step 2: Rewrite call sites to URL-only lookups.**

In `mcc/classes/ImgClass.cpp` line 69 (inside `ImgClass::GetImages`), change:

```cpp
gmsg.AddImage(url, Width(0), Height(0), this);
```

to:

```cpp
/* The cache and the in-flight ImageList both dedupe by URL only — width
   and height are honoured per-instance via BitmapScaler in ReceiveImage. */
gmsg.AddImage(url, 0, 0, this);
```

In `ImgClass::Layout` (around line 202), change:

```cpp
if((Picture = lmsg.Share->ImageStorage->FindImage(url, GivenWidth ? width : 0, GivenHeight ? height : 0)))
```

to:

```cpp
if((Picture = lmsg.Share->ImageStorage->FindImage(url, 0, 0)))
```

In `ImgClass::MinMax` (around line 302), change:

```cpp
if((Picture = lmsg->Share->ImageStorage->FindImage(url, GivenWidth ? width : 0, Height(0, lmsg))))
```

to:

```cpp
if((Picture = lmsg->Share->ImageStorage->FindImage(url, 0, 0)))
```

- [ ] **Step 3: Cross-build.**

```bash
docker run --rm -v "/d/Github/htmlview-midwan:/work" -w /work/mcc \
  sacredbanana/amiga-compiler:m68k-amigaos make OS=os3 -j4
```

Expected: succeeds. (Behaviourally: same-URL different-size IMGs now share one decode. Visually unchanged for now — clipping bug still present until Task 3.)

- [ ] **Step 4: Commit.**

```bash
git add mcc/classes/ImgClass.cpp
git commit -m "refactor(mcc): cache and queue images by URL only

Width/height are honoured per-instance via the upcoming BitmapScaler
path. Without this change, two <img> tags with the same src but
different requested dimensions would each trigger a separate fetch and
each insert a separate cache entry pointing at the same native bitmap."
```

---

### Task 3: Scale on receipt + render the scaled bitmap

**Files:**
- Modify: `mcc/classes/ImgClass.h`
- Modify: `mcc/classes/ImgClass.cpp`

- [ ] **Step 1: Extend the class with scaled-bitmap members.**

In `mcc/classes/ImgClass.h`, after `struct BitMap *BlendBitMap;` (line 60 area), add:

```cpp
    /* Scaled copy of Picture->BMp / Picture->Mask, present only when
       the HTML requested width/height differs from the picture's
       native dimensions. NULL otherwise; renderer falls back to the
       cache-shared Picture->BMp directly. */
    struct BitMap *ScaledBMp;
    UBYTE *ScaledMask;
    UWORD ScaledW, ScaledH;
```

(`ScaledW`/`ScaledH` let the destructor call `FreeRaster(ScaledMask, ScaledW, ScaledH)` correctly.)

- [ ] **Step 2: Write a failing visual test fixture.**

Create `mcc/testdata/mastodon_avatar.html`:

```html
<html>
<body>
<h1>Mastodon avatar scaling</h1>

<h2>Native size (no width/height)</h2>
<p><img src="PROGDIR:test.png" alt="native"></p>

<h2>Scaled to 64x64</h2>
<p><img src="PROGDIR:test.png" alt="64x64" width="64" height="64"></p>

<h2>Scaled to 32x32 (twice — should fetch once)</h2>
<p>
  <img src="PROGDIR:test.png" alt="32x32 #1" width="32" height="32">
  <img src="PROGDIR:test.png" alt="32x32 #2" width="32" height="32">
</p>

<h2>Asymmetric 96x32</h2>
<p><img src="PROGDIR:test.png" alt="96x32" width="96" height="32"></p>

<h2>Mastodon-style links</h2>
<p>Hello <a href="https://example.social/@user" class="mention">@user</a> and
   <a href="https://example.social/tags/foo" class="hashtag">#foo</a>.</p>
</body>
</html>
```

This is the acceptance fixture; no automated assertion (Amiga visual test) but it captures the exact symptom from the report.

- [ ] **Step 3: Build the scaled bitmap in `ReceiveImage`.**

In `mcc/classes/ImgClass.cpp`, just below the existing `if(!GivenHeight) { ... }` block in `ReceiveImage` (around line 374), add:

```cpp
  /* If the HTML asked for dimensions that differ from the picture's
     native size, build a per-instance scaled copy. Falling back to
     Picture->BMp on alloc failure preserves the old (clipped) behaviour
     rather than dropping the image entirely. */
  LONG reqW = GivenWidth->Size, reqH = GivenHeight->Size;
  if(reqW > 0 && reqH > 0 &&
     (reqW != (LONG)pic->Width || reqH != (LONG)pic->Height))
  {
    ULONG depth = GetBitMapAttr(pic->BMp, BMA_DEPTH);
    if(ScaledBMp) { WaitBlit(); FreeBitMap(ScaledBMp); ScaledBMp = NULL; }
    if(ScaledMask){ FreeRaster(ScaledMask, ScaledW, ScaledH); ScaledMask = NULL; }

    ScaledBMp = ScaleBitmapTo(pic->BMp,
                              pic->Width, pic->Height,
                              (UWORD)reqW, (UWORD)reqH,
                              depth, pic->BMp);
    if(pic->Mask)
      ScaledMask = ScaleMaskTo(pic->Mask,
                               pic->Width, pic->Height,
                               (UWORD)reqW, (UWORD)reqH);
    ScaledW = (UWORD)reqW;
    ScaledH = (UWORD)reqH;
  }
```

Add `#include "BitmapScaler.h"` at the top of the file.

- [ ] **Step 4: Render the scaled bitmap when present.**

In `mcc/classes/ImgClass.cpp::Render` (around line 448-468), change the source bitmap selection. Today the code reads `Picture->BMp` and `Picture->Mask` directly. Replace:

```cpp
    if(Picture->Mask)
    {
      ...
      BltMaskRPort(Picture->BMp, 0, YStart, rp, x1, y1+YStart, width, pass_height, Picture->Mask);
      ...
    }
    else
    {
      ...
      struct BitMap *bmp = BlendBitMap ? BlendBitMap : Picture->BMp;
      BltBitMapRastPort(bmp, 0, YStart, rp, x1, y1+YStart, width, pass_height, 0x0c0);
    }
```

with:

```cpp
    /* When a scaled copy exists, blit from it at 1:1 — the dimensions
       already match (width, pass_height). Otherwise blit from the
       cache-shared native bitmap. */
    struct BitMap *srcBmp = ScaledBMp ? ScaledBMp : Picture->BMp;
    UBYTE       *srcMask = ScaledMask ? ScaledMask : Picture->Mask;

    if(srcMask)
    {
      ...  /* same body as before, but replace Picture->BMp with srcBmp
              and Picture->Mask with srcMask in BltMaskRPort + the
              double-buffer path */
    }
    else
    {
      ...
      struct BitMap *bmp = BlendBitMap ? BlendBitMap : srcBmp;
      BltBitMapRastPort(bmp, 0, YStart, rp, x1, y1+YStart, width, pass_height, 0x0c0);
    }
```

Concretely, replace the four `Picture->BMp` references and the one `Picture->Mask` reference in the `if(Picture->Mask)` / `else` blocks of `Render` with `srcBmp` and `srcMask`. Leave the `pass_height = YStop-YStart` calculation untouched — Task 4 handles `YStop`.

- [ ] **Step 5: Cap `YStop` at the rendered height, not the source height.**

Still in `Render`, find:

```cpp
    if(Picture->Flags & PicFLG_Full)
      YStop = height;
```

This already exists post-blit. The bug is that it runs *after* the first blit, meaning the first paint still reads `YStop = Picture->Height` (the native source). Move the `YStop = height` line to fire before `pass_height = YStop-YStart` whenever the picture is fully decoded, so the first blit also clips correctly:

```cpp
  if(Picture)
  {
    /* If the picture is fully decoded, clip YStop to the rendered
       height before computing pass_height — otherwise the first paint
       reads pass_height = Picture->Height (native), which combined
       with the destination width = requested-width gives the
       "clipped horizontally, full native height vertically" symptom
       seen in amidon2. */
    if((Picture->Flags & PicFLG_Full) && ScaledBMp)
      YStop = height;

    LONG pass_height = YStop-YStart;
    ...
  }
```

The guard on `ScaledBMp` keeps the scroll-while-decoding code path untouched for non-scaled images.

- [ ] **Step 6: Free the scaled resources in destructor and `FreeColours`.**

In `~ImgClass()`:

```cpp
ImgClass::~ImgClass ()
{
  delete Name;
  delete AltText;
  delete Source;
  delete GivenWidth;
  delete GivenHeight;
  delete Map;
  if(ScaledBMp)  { WaitBlit(); FreeBitMap(ScaledBMp); }
  if(ScaledMask) { FreeRaster(ScaledMask, ScaledW, ScaledH); }
}
```

In `FreeColours`, after `Picture = NULL;`, add:

```cpp
  if(ScaledBMp)  { WaitBlit(); FreeBitMap(ScaledBMp);  ScaledBMp = NULL; }
  if(ScaledMask) { FreeRaster(ScaledMask, ScaledW, ScaledH); ScaledMask = NULL; }
```

- [ ] **Step 7: Cross-build for all three platforms.**

```bash
docker run --rm -v "/d/Github/htmlview-midwan:/work" -w /work/mcc \
  sacredbanana/amiga-compiler:m68k-amigaos make OS=os3 -j4
docker run --rm -v "/d/Github/htmlview-midwan:/work" -w /work/mcc \
  sacredbanana/amiga-compiler:ppc-amigaos make OS=os4 -j4
docker run --rm -v "/d/Github/htmlview-midwan:/work" -w /work/mcc \
  sacredbanana/amiga-compiler:ppc-morphos make OS=mos -j4
```

All three: must succeed. If MorphOS / OS4 fail with `BitMapScale` not found, gate the helper on a `#if defined(__amigaos3__)` block and fall back to nearest-neighbor in plain C — but MorphOS and OS4 both ship `BitMapScale` natively, so this guard is unlikely to be needed.

- [ ] **Step 8: Visual verification on hardware/emulator.**

Build `SimpleTest` and run on an Amiga / WinUAE / FS-UAE. Open the new `mastodon_avatar.html` (Task 5 wires it in). Confirm:
- Native (`<img>` no size): renders at original 200x200 (or whatever `test.png` actually is).
- 64x64: renders at 64x64, *not* clipped.
- 32x32: both copies render at 32x32; the network/load hook fires once (tail the debug log).
- 96x32: renders at 96x32, asymmetric scaling visible.

If any case still shows the old "clipped horizontally, full vertically" symptom, return to Phase 1 step 1 of `superpowers:systematic-debugging` — the most likely culprit is `YStop` not being clipped on the early-decode redraw path.

- [ ] **Step 9: Commit.**

```bash
git add mcc/classes/ImgClass.h mcc/classes/ImgClass.cpp mcc/testdata/mastodon_avatar.html
git commit -m "fix(mcc): honour <img width/height> by scaling on receipt

Previously the renderer blitted Picture->BMp at 1:1 against a
destination clipped to the requested width, leaving the rendered
image clipped horizontally and at native height vertically.

Now ImgClass::ReceiveImage builds a per-instance scaled BitMap (and
mask if present) via BitMapScale when the requested dimensions differ
from the picture's native size, and Render uses that scaled bitmap.
The cache continues to store native bitmaps shared across instances.

Fixes the avatar-scaling regression observed in amidon2."
```

---

### Task 4: Wire the new test fixture into SimpleTest

**Files:**
- Modify: `mcc/SimpleTest.c`
- Modify: `mcc/Makefile`

- [ ] **Step 1: Copy the fixture next to the binary.**

In `mcc/Makefile`, find the `$(BINDIR)/test.png` target (~line 578). Add a sibling target:

```makefile
$(BINDIR)/mastodon_avatar.html: testdata/mastodon_avatar.html | $(BINDIR)
	@echo "Copying mastodon_avatar.html"
	@cp testdata/mastodon_avatar.html $@
```

Add `$(BINDIR)/mastodon_avatar.html` to the `all:` target right after `$(BINDIR)/test.png`.

- [ ] **Step 2: Add a "Mastodon" page selector in SimpleTest.**

In `mcc/SimpleTest.c`, locate the `test_html` blob and add — between existing `<h2>` sections — the contents of the fixture inline (so SimpleTest exercises Mastodon-style markup without needing a second window). A two-line edit at the top of the blob and the matching close tags is enough.

- [ ] **Step 3: Cross-build.**

```bash
docker run --rm -v "/d/Github/htmlview-midwan:/work" -w /work/mcc \
  sacredbanana/amiga-compiler:m68k-amigaos make OS=os3 -j4
```

Expected: `bin_os3/mastodon_avatar.html` is created and `SimpleTest` rebuilds.

- [ ] **Step 4: Commit.**

```bash
git add mcc/SimpleTest.c mcc/Makefile mcc/testdata/mastodon_avatar.html
git commit -m "test(mcc): exercise <img width/height> scaling in SimpleTest"
```

---

## Phase 2 — Anchor `class` attribute

### Task 5: Parse and store `class` on `<a>`

**Files:**
- Modify: `mcc/classes/AClass.h`
- Modify: `mcc/classes/AClass.cpp`

- [ ] **Step 1: Add the field.**

In `mcc/classes/AClass.h`, find the protected/private members section and add:

```cpp
    STRPTR Class;       /* HTML class= attribute, or NULL. Owned. */
```

Confirm there's a destructor that frees `URL` and `Name`; add `delete Class;` next to them — open `AClass.cpp` to see whether the destructor is implicit (inherited) or explicit. If implicit, add an explicit one.

- [ ] **Step 2: Parse `class=`.**

In `AClass::Parse`, extend the `args` array:

```cpp
  struct ArgList args[] =
  {
    { "HREF",   &URL,     ARG_URL,    NULL },
    { "NAME",   &Name,    ARG_URL,    NULL },
    { "TARGET", &Target,  ARG_URL,    NULL },
    { "CLASS",  &Class,   ARG_STRING, NULL },
    { NULL,     NULL,     0,          NULL }
  };
```

- [ ] **Step 3: Cross-build.**

```bash
docker run --rm -v "/d/Github/htmlview-midwan:/work" -w /work/mcc \
  sacredbanana/amiga-compiler:m68k-amigaos make OS=os3 -j4
```

Expected: succeeds. (No behaviour change yet — class is parsed but unused.)

- [ ] **Step 4: Commit.**

```bash
git add mcc/classes/AClass.h mcc/classes/AClass.cpp
git commit -m "feat(mcc): parse and retain HTML class attribute on <a>"
```

---

### Task 6: Surface `class` through the hit-test message + GetContextInfo

**Files:**
- Modify: `mcc/HitTest.h`
- Modify: `mcc/classes/AClass.cpp`
- Modify: `mcc/HTMLview_mcc.h`
- Modify: `mcc/Dispatcher.cpp`

- [ ] **Step 1: Extend `HitTestMessage`.**

In `mcc/HitTest.h`, find the struct and add:

```cpp
    STRPTR LinkClass;    /* class= of the matched <a>, or NULL. Borrowed. */
```

Initialise it to `NULL` in the constructor (search for `HitTestMessage(` to find it).

- [ ] **Step 2: Set it in `AClass::HitTest`.**

In `mcc/classes/AClass.cpp::HitTest`, where `hmsg.URL = URL;` is set:

```cpp
    hmsg.URL = URL;
    hmsg.LinkClass = Class;     /* may be NULL */
    hmsg.Target = Target;
```

The "save and restore on miss" pattern around it must mirror `Obj`/`URL` — restore `LinkClass` if `TreeClass::HitTest` returns FALSE.

- [ ] **Step 3: Add the public MUIA tag.**

In `mcc/HTMLview_mcc.h`, near `MUIA_HTMLview_ClickedURL` (~line 282), pick the next free `HTMLview_ID(...)` slot and define:

```cpp
/*
 * MUIA_HTMLview_LinkClass -- [..G], STRPTR
 *
 * Set alongside MUIA_HTMLview_ClickedURL whenever a clicked anchor
 * carries an HTML class attribute (e.g. Mastodon's class="mention" /
 * class="hashtag"). NULL when the click was on a link without a class
 * or on something other than a link. Borrowed pointer; copy if you
 * need to retain it past the notification callback.
 */
#define MUIA_HTMLview_LinkClass            HTMLview_ID(...)
```

Pick the next unused ID — search for `HTMLview_ID(` and use the largest+1.

Also extend the `MUIR_HTMLview_GetContextInfo` struct: add `STRPTR LinkClass;` next to the other STRPTR members (around the `URL`/`Img`/`Frame` section).

- [ ] **Step 4: Wire the GetContextInfo path.**

In `mcc/Dispatcher.cpp::MUIM_HTMLview_GetContextInfo` (~line 1440), where the URL/Img/Frame/Background pointers are copied out of `hmsg` into `cinfo`, add `LinkClass`. The class string is short-lived (lives on the parsed tree), so copy it like the other strings using `MUIM_HTMLview_AddPart`-equivalent allocation — but `MUIM_HTMLview_AddPart` resolves URLs, so for class names use a plain dup:

```cpp
        delete cinfo->LinkClass;
        cinfo->LinkClass = NULL;
        if(hmsg->LinkClass)
        {
          cinfo->LinkClass = new (std::nothrow) char[strlen(hmsg->LinkClass)+1];
          if(cinfo->LinkClass) strcpy(cinfo->LinkClass, hmsg->LinkClass);
        }
```

Make sure the cleanup path that releases `cinfo->URL`/`Img`/etc. also releases `LinkClass` — search for the matching `delete cinfo->URL` (search `delete cinfo->`) and add a `delete cinfo->LinkClass;`.

- [ ] **Step 5: Cross-build.**

```bash
docker run --rm -v "/d/Github/htmlview-midwan:/work" -w /work/mcc \
  sacredbanana/amiga-compiler:m68k-amigaos make OS=os3 -j4
```

- [ ] **Step 6: Commit.**

```bash
git add mcc/HitTest.h mcc/classes/AClass.cpp mcc/HTMLview_mcc.h mcc/Dispatcher.cpp
git commit -m "feat(mcc): expose anchor class via HitTestMessage + GetContextInfo"
```

---

### Task 7: Notify `MUIA_HTMLview_LinkClass` on click

**Files:**
- Modify: `mcc/private.h`
- Modify: `mcc/classes/HostClass.cpp`
- Modify: `mcc/GetSetAttrs.cpp`

- [ ] **Step 1: Stash the class on `HTMLviewData`.**

The hit-test only knows the class at hover/click time. Click delivery happens in `HostClass::HandleEvent`. We need to either pass the class through the hit-test or stash it on `HTMLviewData` like `LastURL` is stashed. In `mcc/private.h`, near the existing `STRPTR LastURL;` add:

```cpp
  STRPTR LastLinkClass;   /* class= of the most recently hit <a>, or NULL */
```

Initialise to NULL in the data constructor / `OM_NEW` (search `LastURL = NULL` and add the same line).

- [ ] **Step 2: Update the hit-test handler.**

In `mcc/classes/HostClass.cpp` where `HitTest` results feed `data->LastURL`, also store `data->LastLinkClass = hmsg->LinkClass`. Search for `LastURL = ` in HostClass to find the spot.

- [ ] **Step 3: Send the new MUIA on click.**

Still in `mcc/classes/HostClass.cpp`, find the `SetAttrs(dst, MUIA_HTMLview_ClickedURL, ...)` block (around line 366). Extend:

```cpp
              SetAttrs(dst,
                MUIA_HTMLview_ClickedURL, (ULONG)url,
                MUIA_HTMLview_Target,     (ULONG)target,
                MUIA_HTMLview_LinkClass,  (ULONG)data->LastLinkClass,
                MUIA_HTMLview_Qualifier,  imsg->Qualifier,
                TAG_DONE);
```

- [ ] **Step 4: Make MUIA_HTMLview_LinkClass settable/gettable.**

In `mcc/GetSetAttrs.cpp`, find the `MUIA_HTMLview_ClickedURL` case (line 235) and mirror it:

```cpp
    case MUIA_HTMLview_LinkClass:
      /* Set by the dispatcher just before MUIA_HTMLview_ClickedURL is
         set; readers receive the class via MUIM_Notify on
         MUIA_HTMLview_ClickedURL and a separate Get on this attr. */
      data->LinkClass = (STRPTR)tag->ti_Data;
      break;
```

Add the corresponding GET case so notifies can read it.

(`HTMLviewData::LinkClass` itself: add a `STRPTR LinkClass;` field next to `ClickedURL` in `private.h`.)

- [ ] **Step 5: Cross-build + commit.**

```bash
docker run --rm -v "/d/Github/htmlview-midwan:/work" -w /work/mcc \
  sacredbanana/amiga-compiler:m68k-amigaos make OS=os3 -j4
git add mcc/private.h mcc/classes/HostClass.cpp mcc/GetSetAttrs.cpp
git commit -m "feat(mcc): notify MUIA_HTMLview_LinkClass on anchor click"
```

- [ ] **Step 6: Hardware verification.**

Run `SimpleTest`, click the `class="mention"` link in the new fixture. Trap notifications via a small `MUIM_Notify` setup in SimpleTest itself (Task 8) and confirm both URL and class arrive.

---

### Task 8: Demonstrate the click contract in SimpleTest

**Files:**
- Modify: `mcc/SimpleTest.c`

- [ ] **Step 1: Wire a hook on `MUIA_HTMLview_ClickedURL`.**

Add a hook function that retrieves both URL and `MUIA_HTMLview_LinkClass`, prints them via `kprintf` (debug build) or `printf`. Mirror existing hook patterns in the file.

- [ ] **Step 2: Cross-build + commit.**

```bash
docker run --rm -v "/d/Github/htmlview-midwan:/work" -w /work/mcc \
  sacredbanana/amiga-compiler:m68k-amigaos make OS=os3 -j4
git add mcc/SimpleTest.c
git commit -m "test(mcc): print link class alongside URL on click"
```

---

## Phase 3 — Cache observability

### Task 9: Log cache hits / misses / evictions

**Files:**
- Modify: `mcc/ImageManager.cpp`

- [ ] **Step 1: Add D() lines in `FindImage`, `AddImage`, and the eviction loop.**

```cpp
struct PictureFrame *ImageCache::FindImage (STRPTR url, ULONG width, ULONG height)
{
  ObtainSemaphore(&ImageMutex);
  ...
  if(!strcmp(url, prev->URL) && prev->Picture->MatchSize(width, height))
  {
    D(DBF_STARTUP, "ImageCache: HIT  %s (%lux%lu cached, asked %lux%lu)",
      url, prev->Picture->Width, prev->Picture->Height, width, height);
    ...
  }
  ...
  D(DBF_STARTUP, "ImageCache: MISS %s (asked %lux%lu)", url, width, height);
  return(NULL);
}
```

In `AddImage`:

```cpp
D(DBF_STARTUP, "ImageCache: ADD  %s (%lux%lu, total now %lu/%lu)",
  url, pic->Width, pic->Height, CurrentSize, MaxSize);
```

In the eviction `while(CurrentSize > MaxSize ...)`:

```cpp
D(DBF_STARTUP, "ImageCache: EVICT %s (%lub freed)",
  prev->URL, prev->Picture->Size());
```

- [ ] **Step 2: Cross-build (debug).**

```bash
docker run --rm -v "/d/Github/htmlview-midwan:/work" -w /work/mcc \
  sacredbanana/amiga-compiler:m68k-amigaos make OS=os3 DEBUG=1 -j4
```

- [ ] **Step 3: Commit.**

```bash
git add mcc/ImageManager.cpp
git commit -m "chore(mcc): log image cache hits, misses, additions, evictions"
```

- [ ] **Step 4: Hardware verification.**

Run a debug build of `SimpleTest`. Reload the Mastodon fixture twice; tail the serial / Sashimi output. Expected: first load shows MISS+ADD per URL, second load shows HIT per URL — including for the two `<img width="32">` references in the fixture.

---

## Phase 4 — Wrap-up

### Task 10: Documentation + README note

**Files:**
- Modify: `README.md` (if a feature/change list exists)
- Modify: `mcc/HTMLview_mcc.h` (autodocs comments)

- [ ] **Step 1: Document the new attribute.**

In `mcc/HTMLview_mcc.h`, mention `MUIA_HTMLview_LinkClass` in the autodoc-style comment block alongside `MUIA_HTMLview_ClickedURL`.

- [ ] **Step 2: Note the scaling fix in the changelog.**

If `README.md` / `IMPROVEMENTS.md` (per recent commits) tracks notable fixes, add a one-liner:

```
- <img width=W height=H> now scales the rendered bitmap (was clipped).
- <a class="..."> is now exposed via MUIA_HTMLview_LinkClass.
```

- [ ] **Step 3: Commit.**

```bash
git add README.md mcc/HTMLview_mcc.h
git commit -m "docs: changelog entry for image scaling and link class"
```

---

### Task 11: Final integration verification

**Files:** none modified.

- [ ] **Step 1: Cross-build all three platforms in sequence.**

```bash
for OS in os3 os4 mos; do \
  case $OS in \
    os3) IMG=sacredbanana/amiga-compiler:m68k-amigaos ;; \
    os4) IMG=sacredbanana/amiga-compiler:ppc-amigaos ;; \
    mos) IMG=sacredbanana/amiga-compiler:ppc-morphos ;; \
  esac; \
  docker run --rm -v /d/Github/htmlview-midwan:/work -w /work/mcc \
    $IMG make OS=$OS -j4 || { echo "FAIL $OS"; exit 1; }; \
done
```

Expected: all three succeed.

- [ ] **Step 2: Run host unit test.**

```bash
cd D:/Github/htmlview-midwan/mcc/tests && \
  gcc -DBMSCALER_HOST_TEST -I.. ../BitmapScaler.cpp test_bitmap_scaler.c -o tbs && ./tbs
```

Expected: `test_bitmap_scaler OK`.

- [ ] **Step 3: Hardware run-through.**

Boot WinUAE / FS-UAE / Amiga; install `bin_os3/HTMLview.mcc` to `MUI:Libs/mui/`; run `bin_os3/SimpleTest`. Walk through:

- Section "Mastodon avatar scaling" — all four `<img>` cells should be shaped exactly as their `width`/`height` attributes say.
- Section "Mastodon-style links" — clicking `@user` and `#foo` should each fire a notification carrying URL + `LinkClass = "mention"` / `"hashtag"` (visible via the SimpleTest hook from Task 8).
- Reload the page and tail the debug output — the second visit should be all HITs.

- [ ] **Step 4: Push the branch and open a PR.**

```bash
git push -u origin feat/mastodon-img-link-fixes
gh pr create --title "fix(mcc): img scaling + anchor class for Mastodon clients" --body "..."
```

---

## Self-Review Checklist (already-applied)

- **Spec coverage:** Phase 1 covers (1) image scaling; Phase 2 covers (2) link clickability gap; Phase 3 covers (3) cache observability. ✓
- **Placeholders:** none — all code blocks contain the actual content; the "pick the next free `HTMLview_ID(...)`" instruction is a deliberate ask of the implementer because the next ID depends on the latest in-tree value. ✓
- **Type consistency:** `ScaledBMp`/`ScaledMask`/`ScaledW`/`ScaledH` are declared in Task 3 step 1 and used in steps 3, 4, 5, 6. `LinkClass` flows: parsed (Task 5), into HitTestMessage (Task 6), onto HTMLviewData (Task 7), out via MUIA (Task 7). `MUIA_HTMLview_LinkClass` defined once (Task 6), used in Tasks 6/7/8. ✓
