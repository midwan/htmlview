/***************************************************************************

 HTMLview.mcc - HTMLview MUI Custom Class
 Copyright (C) 1997-2000 Allan Odgaard
 Copyright (C) 2005-2026 by HTMLview.mcc Open Source Team

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

***************************************************************************/

/*
 * libhtmlview_nethook -- reference HTMLview.mcc image/content load hook.
 *
 * Resolves these URL schemes:
 *   - PROGDIR:, DH0:, etc.   -- direct AmigaDOS Open()
 *   - file://<path>          -- stripped then Open()
 *   - http://host[:port]/p   -- bsdsocket.library, chunked-aware, follows 3xx
 *   - https://host[:port]/p  -- AmiSSL-wrapped (when compiled with -DHAVE_AMISSL)
 *
 * Usage (consumer side):
 *
 *   struct Hook net_hook;
 *   HTMLviewNet_InitHook(&net_hook);
 *   ...
 *   HTMLviewObject,
 *     MUIA_HTMLview_LoadHook,      &net_hook,
 *     MUIA_HTMLview_ImageLoadHook, &net_hook,
 *     ...
 *
 * The hook opens its own task-local bsdsocket / amissl bases; the host
 * program does NOT need to OpenLibrary("bsdsocket.library") itself.
 *
 * Up to 5 consecutive 3xx redirects are followed (absolute URLs only).
 */

#ifndef HTMLVIEW_NETHOOK_H
#define HTMLVIEW_NETHOOK_H

#include <exec/types.h>
#include <utility/hooks.h>

struct HTMLview_LoadMsg;

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Hook entry point. Normally consumers invoke this indirectly by passing
 * an initialised struct Hook to HTMLview.mcc; this prototype is exported
 * so HTMLviewNet_InitHook() can wire it into h_Entry/h_SubEntry.
 */
ULONG HTMLviewNet_HookFunc(struct Hook *hook, APTR obj,
                           struct HTMLview_LoadMsg *msg);

/*
 * Populate `hook` with the right h_Entry / h_SubEntry for the current
 * platform. Safe to call multiple times; idempotent. The hook keeps no
 * per-instance state of its own, so one Hook can serve many HTMLview
 * objects concurrently.
 */
void HTMLviewNet_InitHook(struct Hook *hook);

/* --- TLS verification (HAVE_AMISSL builds) ---------------------------- */

/*
 * Verification modes for HTTPS fetches. Default is AUTO: verification is
 * enabled if a CA bundle is discovered, otherwise disabled with a single
 * warning line in T:htmlview_hook.log. The verify decision is taken per
 * request, so installing the bundle later "just works".
 */
#define HTMLVIEWNET_VERIFY_AUTO 0  /* peer-verify if bundle present */
#define HTMLVIEWNET_VERIFY_NONE 1  /* never verify (pre-Phase-2 behaviour) */
#define HTMLVIEWNET_VERIFY_PEER 2  /* always verify; fail if no bundle */

/*
 * Override the CA-bundle path. Pass NULL to clear and restore auto-
 * discovery. Path is copied into an internal buffer; the caller owns its
 * storage. Typical use: host app knows where it ships certificates and
 * wants to point the hook at that location directly.
 */
void HTMLviewNet_SetCABundle(const char *path);

/*
 * Override verify mode. Default HTMLVIEWNET_VERIFY_AUTO.
 */
void HTMLviewNet_SetVerifyMode(int mode);

/* --- On-disk response cache ------------------------------------------ */

/*
 * Enable / disable a simple on-disk response cache. Pass a directory path
 * to enable (the directory must exist; the hook will not create it); pass
 * NULL or an empty string to disable. Disabled by default so the hook's
 * behaviour without opt-in matches pre-cache versions.
 *
 * Cache keys are derived from the URL; entries live as `<key>.body` and
 * `<key>.meta` file pairs. There is no eviction policy in this version --
 * stale entries accumulate until the user cleans the directory.
 */
void HTMLviewNet_SetCacheDir(const char *path);

/*
 * Set the fallback TTL in seconds used when a response carries no
 * Cache-Control: max-age / Expires hint. Default 3600 (1 hour). Pass 0 to
 * make unhinted responses non-cacheable.
 */
void HTMLviewNet_SetCacheTTL(ULONG seconds);

#ifdef __cplusplus
}
#endif

#endif /* HTMLVIEW_NETHOOK_H */
