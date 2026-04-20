/***************************************************************************

 HTMLview.mcc - HTMLview MUI Custom Class
 Copyright (C) 2026 Dimitris Panokostas <midwan@gmail.com>

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

***************************************************************************/

/*
 * Reference image/content load hook for HTMLview.mcc.
 *
 * All I/O libraries (bsdsocket, amisslmaster, amissl) are opened lazily
 * by the hook itself, from whichever task actually dispatches the load
 * (HTMLview runs hook calls from a decoder thread). The host program
 * does not need to OpenLibrary any of them.
 *
 * This compilation unit is the only place the hook logic lives; consumers
 * link libhtmlview_nethook.a and include htmlview_nethook.h.
 */

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/socket.h>
#include <clib/alib_protos.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <netdb.h>

#include "../HTMLview_mcc.h"
#include "htmlview_nethook.h"

#ifdef HAVE_AMISSL
#include <proto/amisslmaster.h>
#include <proto/amissl.h>
#include <libraries/amisslmaster.h>
#include <libraries/amissl.h>
#include <amissl/amissl.h>
#endif

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/* Per-transfer state stored in lm_Userdata. */
struct HVN_State
{
    BPTR  file;        /* file handle if this is a local file */
    LONG  socket;      /* bsdsocket handle, -1 if none */
    UBYTE *buffer;     /* stashed body bytes from the initial HTTP recv */
    ULONG  bufpos;
    ULONG  buflen;
    int    chunked;    /* non-zero => Transfer-Encoding: chunked */
    ULONG  chunk_left;
    int    use_tls;    /* non-zero => route I/O through SSL_* */
    ULONG  content_length;  /* 0 if not advertised */

    /* Task-local bsdsocket bases. Opened by the decoder thread so it can
       actually call socket/recv/etc.; closed on hook-Close. */
    struct Library *SBase;
#if defined(__amigaos4__)
    struct SocketIFace *SIFace;
#endif

#ifdef HAVE_AMISSL
    struct Library *AMSBase;      /* amisslmaster.library */
    struct Library *ASBase;       /* amissl.library       */
#if defined(__amigaos4__)
    struct AmiSSLMasterIFace *IAMSMaster;
    struct AmiSSLIFace       *IAS;
#endif
    SSL_CTX *ssl_ctx;
    SSL     *ssl;
    int      ssl_initialized;    /* CleanupAmiSSLA required on teardown */
    int      sni_errno;          /* storage for AmiSSL_ErrNoPtr */
#endif
};

/* --- logging ----------------------------------------------------------- */

/* Accumulating-buffer + rewrite pattern (same as ImageManager DTLog) so the
   log file stays coherent across OS3 and OS4 without relying on Seek. */
static char   HVN_LogBuf[16384];
static ULONG  HVN_LogLen = 0;

static void HVN_Log(const char *line)
{
    ULONG need = strlen(line) + 1;
    if (HVN_LogLen + need + 1 >= sizeof(HVN_LogBuf)) return;
    memcpy(HVN_LogBuf + HVN_LogLen, line, need - 1);
    HVN_LogLen += need - 1;
    HVN_LogBuf[HVN_LogLen++] = '\n';
    HVN_LogBuf[HVN_LogLen]   = 0;
    BPTR f = Open((STRPTR)"T:htmlview_hook.log", MODE_NEWFILE);
    if (f) { Write(f, HVN_LogBuf, HVN_LogLen); Close(f); }
}

/* --- low-level I/O (plain TCP or TLS depending on st->use_tls) --------- */

static LONG HVN_Recv(struct HVN_State *st, APTR buf, LONG len)
{
#ifdef HAVE_AMISSL
    if (st->use_tls && st->ssl)
    {
        /* Inline macros in <inline/amissl.h> resolve AMISSL_BASE_NAME
           (default: AmiSSLBase) by normal C name lookup -- shadow the
           file-scope extern with our task-local base. */
        struct Library *AmiSSLBase = st->ASBase;
#if defined(__amigaos4__)
        struct AmiSSLIFace *IAmiSSL = st->IAS;
#endif
        int n = SSL_read(st->ssl, buf, (int)len);
        return n > 0 ? n : 0;
    }
#endif
    {
        struct Library *SocketBase = st->SBase;
#if defined(__amigaos4__)
        struct SocketIFace *ISocket = st->SIFace;
#endif
        LONG got = recv(st->socket, buf, len, 0);
        return got > 0 ? got : 0;
    }
}

static LONG HVN_Send(struct HVN_State *st, const APTR buf, LONG len)
{
#ifdef HAVE_AMISSL
    if (st->use_tls && st->ssl)
    {
        struct Library *AmiSSLBase = st->ASBase;
#if defined(__amigaos4__)
        struct AmiSSLIFace *IAmiSSL = st->IAS;
#endif
        int n = SSL_write(st->ssl, buf, (int)len);
        return n > 0 ? n : -1;
    }
#endif
    {
        struct Library *SocketBase = st->SBase;
#if defined(__amigaos4__)
        struct SocketIFace *ISocket = st->SIFace;
#endif
        return send(st->socket, buf, len, 0);
    }
}

static LONG HVN_RecvLine(struct HVN_State *st, char *buf, LONG maxlen)
{
    LONG i = 0;
    while (i < maxlen - 1)
    {
        UBYTE c;
        LONG got = HVN_Recv(st, (APTR)&c, 1);
        if (got <= 0) return -1;
        if (c == '\n') { buf[i] = 0; return i; }
        if (c != '\r') buf[i++] = c;
    }
    buf[i] = 0;
    return i;
}

/* Reads up to `want` bytes, unwrapping chunked transfer encoding. */
static LONG HVN_ReadChunked(struct HVN_State *st, UBYTE *out, LONG want)
{
    LONG produced = 0;
    while (produced < want)
    {
        if (st->chunk_left == 0)
        {
            char line[32];
            if (HVN_RecvLine(st, line, sizeof(line)) < 0)
                return produced;
            if (line[0] == 0)
            {
                if (HVN_RecvLine(st, line, sizeof(line)) < 0)
                    return produced;
            }
            ULONG size = strtoul(line, NULL, 16);
            if (size == 0) return produced;
            st->chunk_left = size;
        }

        LONG take = want - produced;
        if ((ULONG)take > st->chunk_left) take = (LONG)st->chunk_left;

        LONG got = HVN_Recv(st, (APTR)(out + produced), take);
        if (got <= 0) return produced;
        produced += got;
        st->chunk_left -= got;
    }
    return produced;
}

/* Parses http:// or https:// URLs. Sets *is_https from scheme. */
static int HVN_ParseUrl(CONST_STRPTR url, char *host, ULONG hlen,
                        char *path, ULONG plen, ULONG *portp, int *is_https)
{
    const char *p = url;
    ULONG default_port = 80;
    *is_https = 0;

    if (strncmp(p, "https://", 8) == 0) { p += 8; default_port = 443; *is_https = 1; }
    else if (strncmp(p, "http://", 7) == 0) { p += 7; }
    else return 0;

    const char *hs = p;
    while (*p && *p != '/' && *p != ':') p++;

    ULONG n = (ULONG)(p - hs);
    if (n == 0 || n >= hlen) return 0;
    memcpy(host, hs, n);
    host[n] = 0;

    ULONG port = default_port;
    if (*p == ':')
    {
        p++;
        port = 0;
        while (*p >= '0' && *p <= '9') { port = port * 10 + (*p - '0'); p++; }
        if (port == 0 || port > 65535) return 0;
    }
    *portp = port;

    if (*p == 0) { strncpy(path, "/", plen); path[plen-1] = 0; return 1; }

    strncpy(path, p, plen - 1);
    path[plen - 1] = 0;
    return 1;
}

/* --- connection teardown (shared by error paths and redirect loop) ----- */

static void HVN_Disconnect(struct HVN_State *st)
{
#ifdef HAVE_AMISSL
    if (st->use_tls)
    {
        struct Library *AmiSSLBase       = st->ASBase;
        struct Library *AmiSSLMasterBase = st->AMSBase;
#if defined(__amigaos4__)
        struct AmiSSLIFace       *IAmiSSL       = st->IAS;
        struct AmiSSLMasterIFace *IAmiSSLMaster = st->IAMSMaster;
#endif
        if (st->ssl)     { SSL_shutdown(st->ssl); SSL_free(st->ssl); st->ssl = NULL; }
        if (st->ssl_ctx) { SSL_CTX_free(st->ssl_ctx); st->ssl_ctx = NULL; }
        if (st->ssl_initialized) { CleanupAmiSSLA(NULL); st->ssl_initialized = 0; }
#if defined(__amigaos4__)
        if (st->IAS)        { DropInterface((struct Interface *)st->IAS); st->IAS = NULL; }
#endif
        if (st->ASBase)  { CloseAmiSSL(); st->ASBase = NULL; AmiSSLBase = NULL; }
#if defined(__amigaos4__)
        if (st->IAMSMaster) { DropInterface((struct Interface *)st->IAMSMaster); st->IAMSMaster = NULL; }
#endif
        if (st->AMSBase) { CloseLibrary(st->AMSBase); st->AMSBase = NULL; }
        (void)AmiSSLBase; (void)AmiSSLMasterBase;
    }
#endif

    if (st->socket >= 0 && st->SBase)
    {
        struct Library *SocketBase = st->SBase;
#if defined(__amigaos4__)
        struct SocketIFace *ISocket = st->SIFace;
#endif
        CloseSocket(st->socket);
    }
    st->socket = -1;

#if defined(__amigaos4__)
    if (st->SIFace) { DropInterface((struct Interface *)st->SIFace); st->SIFace = NULL; }
#endif
    if (st->SBase)  { CloseLibrary(st->SBase); st->SBase = NULL; }

    st->use_tls = 0;
    st->chunked = 0;
    st->chunk_left = 0;
    if (st->buffer) { free(st->buffer); st->buffer = NULL; }
    st->buflen = st->bufpos = 0;
}

/* --- TLS verification config ------------------------------------------ */

/* File-scope verification config. Host reads/writes via the public setter
   APIs before triggering any loads; decoder tasks only read. Writes are
   word-sized and in practice never touched concurrently on any target.
   Stored outside the HAVE_AMISSL gate so MorphOS / non-SSL builds link
   cross-platform callers cleanly -- the config is simply unused there. */
static char HVN_CaBundle[256] = { 0 };
static int  HVN_VerifyMode    = HTMLVIEWNET_VERIFY_AUTO;

/* Candidate CA-bundle locations tried when the host hasn't overridden via
   SetCABundle(). Order reflects decreasing user-installability: explicit
   AmiSSL install paths first, then the ENVARC mirror, then a couple of
   common sibling locations other AmiSSL consumers ship to. */
static const char *const HVN_DefaultBundles[] = {
    "AmiSSL:Certs/curl-ca-bundle.crt",
    "ENVARC:AmiSSL/Certs/curl-ca-bundle.crt",
    "ENV:AmiSSL/Certs/curl-ca-bundle.crt",
    "SYS:Storage/AmiSSL/Certs/curl-ca-bundle.crt",
    NULL
};

/* Fast "does this file exist" check via AmigaDOS. SSL_CTX_load_verify_
   locations() itself happily succeeds on a zero-byte/missing file on
   some OpenSSL builds, so we gate on Open() before handing the path
   over. */
static int HVN_FileExists(const char *path)
{
    BPTR f = Open((STRPTR)path, MODE_OLDFILE);
    if (!f) return 0;
    Close(f);
    return 1;
}

/* Resolve the path to hand to SSL_CTX_load_verify_locations. User override
   wins; otherwise walk the default list. Returns NULL if nothing found. */
static const char *HVN_ResolveCaBundle(void)
{
    if (HVN_CaBundle[0])
        return HVN_FileExists(HVN_CaBundle) ? HVN_CaBundle : NULL;

    for (int i = 0; HVN_DefaultBundles[i]; i++)
        if (HVN_FileExists(HVN_DefaultBundles[i]))
            return HVN_DefaultBundles[i];

    return NULL;
}

void HTMLviewNet_SetCABundle(const char *path)
{
    if (!path || !*path) { HVN_CaBundle[0] = 0; return; }
    ULONG n = strlen(path);
    if (n >= sizeof(HVN_CaBundle)) n = sizeof(HVN_CaBundle) - 1;
    memcpy(HVN_CaBundle, path, n);
    HVN_CaBundle[n] = 0;
}

void HTMLviewNet_SetVerifyMode(int mode)
{
    if (mode == HTMLVIEWNET_VERIFY_AUTO ||
        mode == HTMLVIEWNET_VERIFY_NONE ||
        mode == HTMLVIEWNET_VERIFY_PEER)
    {
        HVN_VerifyMode = mode;
    }
}

/* --- response cache --------------------------------------------------- */

/* Maximum cacheable body size. Larger responses stream straight to the
   consumer without being mirrored to disk, both to bound memory use and
   to keep cold-path latency acceptable. */
#define HVN_CACHE_MAX_BYTES  (8UL * 1024UL * 1024UL)

static char  HVN_CacheDir[256] = { 0 };
static ULONG HVN_CacheTTL      = 3600;

void HTMLviewNet_SetCacheDir(const char *path)
{
    if (!path || !*path) { HVN_CacheDir[0] = 0; return; }
    ULONG n = strlen(path);
    if (n >= sizeof(HVN_CacheDir)) n = sizeof(HVN_CacheDir) - 1;
    memcpy(HVN_CacheDir, path, n);
    HVN_CacheDir[n] = 0;
}

void HTMLviewNet_SetCacheTTL(ULONG seconds)
{
    HVN_CacheTTL = seconds;
}

/* FNV-1a 64-bit over the URL string. Collision-resistant enough for a
   user-local cache and cheap to compute without dragging in an SSL hash. */
static void HVN_CacheKey(const char *url, char out[17])
{
    static const char hex[] = "0123456789abcdef";
    ULONG h1 = 0x811c9dc5UL;
    ULONG h2 = 0xcbf29ce4UL;
    for (const unsigned char *p = (const unsigned char *)url; *p; p++)
    {
        h1 ^= *p; h1 *= 0x01000193UL;
        h2 ^= *p; h2 *= 0x100000001UL & 0xffffffffUL;
        h2 += (h1 << 1);
    }
    for (int i = 0; i < 8; i++) out[i]     = hex[(h1 >> ((7 - i) * 4)) & 0xf];
    for (int i = 0; i < 8; i++) out[8 + i] = hex[(h2 >> ((7 - i) * 4)) & 0xf];
    out[16] = 0;
}

/* Build "<HVN_CacheDir>/<key><suffix>" in buf. Returns 1 on success. */
static int HVN_CachePath(char *buf, ULONG bufsz, const char *key, const char *suffix)
{
    if (!HVN_CacheDir[0]) return 0;
    int n = snprintf(buf, bufsz, "%s/%s%s", HVN_CacheDir, key, suffix);
    return (n > 0 && (ULONG)n < bufsz);
}

/* Minimal portable time source. clock() is good enough for relative
   freshness decisions inside a session; for absolute dates we'd need
   DateStamp(), but the Expires field we write is just
   now() + max-age so everything is on the same monotonic scale. */
static ULONG HVN_Now(void)
{
    /* AmigaDOS DateStamp: days since 1978, mins, ticks. Convert to a
       single ULONG "seconds since 1978" -- room for ~136 years. */
    struct DateStamp ds;
    DateStamp(&ds);
    return (ULONG)ds.ds_Days * 86400UL
         + (ULONG)ds.ds_Minute * 60UL
         + (ULONG)(ds.ds_Tick / 50);
}

/* Parse the .meta file at `path`. Populates *expires_out. Returns 1 on
   success. Meta format is one `key=value` line per field; unknown keys
   are ignored, missing Expires defaults to 0 (treat as stale). */
static int HVN_MetaRead(const char *path, ULONG *expires_out)
{
    *expires_out = 0;
    BPTR f = Open((STRPTR)path, MODE_OLDFILE);
    if (!f) return 0;
    char buf[512];
    LONG n = Read(f, buf, sizeof(buf) - 1);
    Close(f);
    if (n <= 0) return 0;
    buf[n] = 0;
    const char *p = strstr(buf, "Expires=");
    if (p)
    {
        p += 8;
        ULONG v = 0;
        while (*p >= '0' && *p <= '9') { v = v*10 + (*p - '0'); p++; }
        *expires_out = v;
    }
    return 1;
}

/* Write a .meta file. Intentionally tiny -- URL for humans to grep, the
   numeric Expires for the freshness check. ETag/Last-Modified are future
   work (they'd drive 304 revalidation). */
static int HVN_MetaWrite(const char *path, const char *url, ULONG expires)
{
    BPTR f = Open((STRPTR)path, MODE_NEWFILE);
    if (!f) return 0;
    char buf[512];
    int n = snprintf(buf, sizeof(buf), "URL=%s\nExpires=%lu\n", url, expires);
    if (n > 0) Write(f, buf, n);
    Close(f);
    return 1;
}

/* Is a cache entry present and fresh? If yes, returns a BPTR to the open
   body file (caller owns the handle); else ZERO. */
static BPTR HVN_CacheTryOpen(const char *url)
{
    if (!HVN_CacheDir[0]) return (BPTR)0;

    char key[17];
    char metapath[320], bodypath[320];
    HVN_CacheKey(url, key);
    if (!HVN_CachePath(metapath, sizeof(metapath), key, ".meta")) return (BPTR)0;
    if (!HVN_CachePath(bodypath, sizeof(bodypath), key, ".body")) return (BPTR)0;

    ULONG expires = 0;
    if (!HVN_MetaRead(metapath, &expires)) return (BPTR)0;
    if (HVN_Now() >= expires) return (BPTR)0;

    BPTR bf = Open((STRPTR)bodypath, MODE_OLDFILE);
    if (bf)
    {
        char logbuf[320];
        snprintf(logbuf, sizeof(logbuf), "cache: HIT %s -> %s", url, bodypath);
        HVN_Log(logbuf);
    }
    return bf;
}

/* Persist a body+meta pair. Errors are logged but non-fatal (a cache
   miss on the next fetch is the worst outcome). */
static void HVN_CacheStore(const char *url, const UBYTE *body, ULONG len, ULONG ttl)
{
    if (!HVN_CacheDir[0] || !body || !len || !ttl) return;
    if (len > HVN_CACHE_MAX_BYTES) return;

    char key[17];
    char metapath[320], bodypath[320];
    HVN_CacheKey(url, key);
    if (!HVN_CachePath(metapath, sizeof(metapath), key, ".meta")) return;
    if (!HVN_CachePath(bodypath, sizeof(bodypath), key, ".body")) return;

    BPTR f = Open((STRPTR)bodypath, MODE_NEWFILE);
    if (!f) { HVN_Log("cache: Open(body, NEWFILE) failed"); return; }
    LONG w = Write(f, (APTR)body, (LONG)len);
    Close(f);
    if (w != (LONG)len) { HVN_Log("cache: short Write on body"); return; }

    HVN_MetaWrite(metapath, url, HVN_Now() + ttl);

    char logbuf[320];
    snprintf(logbuf, sizeof(logbuf), "cache: STORE %s (%lu bytes, ttl=%lu)",
             url, len, ttl);
    HVN_Log(logbuf);
}

/* --- AmiSSL-wrapped connect (only linked when HAVE_AMISSL) ------------- */

#ifdef HAVE_AMISSL
static int HVN_TlsWrap(struct HVN_State *st, const char *host)
{
    char logbuf[256];

    /* All inline SSL calls resolve their library base via normal C name
       lookup. We declare these early and reassign as bases come up, so
       every call below sees the right local. */
    struct Library *AmiSSLMasterBase = NULL;
    struct Library *AmiSSLBase       = NULL;
#if defined(__amigaos4__)
    struct AmiSSLMasterIFace *IAmiSSLMaster = NULL;
    struct AmiSSLIFace       *IAmiSSL       = NULL;
    struct SocketIFace       *ISocket       = st->SIFace;
#endif

    st->AMSBase = OpenLibrary((STRPTR)"amisslmaster.library", AMISSLMASTER_MIN_VERSION);
    if (!st->AMSBase)
    {
        HVN_Log("https: OpenLibrary amisslmaster.library failed");
        return 0;
    }
    AmiSSLMasterBase = st->AMSBase;
#if defined(__amigaos4__)
    st->IAMSMaster = (struct AmiSSLMasterIFace *)
        GetInterface(st->AMSBase, "main", 1, NULL);
    if (!st->IAMSMaster) { HVN_Log("https: GetInterface IAmiSSLMaster failed"); return 0; }
    IAmiSSLMaster = st->IAMSMaster;
#endif

    if (!InitAmiSSLMaster(AMISSL_CURRENT_VERSION, TRUE))
    {
        HVN_Log("https: InitAmiSSLMaster failed (library too old)");
        return 0;
    }

    st->ASBase = OpenAmiSSL();
    if (!st->ASBase)
    {
        HVN_Log("https: OpenAmiSSL failed");
        return 0;
    }
    AmiSSLBase = st->ASBase;
#if defined(__amigaos4__)
    st->IAS = (struct AmiSSLIFace *)GetInterface(st->ASBase, "main", 1, NULL);
    if (!st->IAS) { HVN_Log("https: GetInterface IAmiSSL failed"); return 0; }
    IAmiSSL = st->IAS;
#endif

    /* Use the A-suffixed variant: OS3 builds pass -DNO_INLINE_STDARG,
       which disables the varargs InitAmiSSL() macro. */
    {
        struct TagItem init_tags[] = {
            { AmiSSL_ErrNoPtr,     (ULONG)&st->sni_errno },
#if defined(__amigaos4__)
            { AmiSSL_ISocket,      (ULONG)st->SIFace },
#else
            { AmiSSL_SocketBase,   (ULONG)st->SBase },
#endif
            { TAG_DONE, 0 }
        };
        if (InitAmiSSLA(init_tags) != 0)
        {
            HVN_Log("https: InitAmiSSL failed");
            return 0;
        }
    }
    st->ssl_initialized = 1;

    OPENSSL_init_ssl(OPENSSL_INIT_SSL_DEFAULT
                     | OPENSSL_INIT_ADD_ALL_CIPHERS
                     | OPENSSL_INIT_ADD_ALL_DIGESTS, NULL);

    /* Minimal entropy seeding -- enough for client handshake; see the
       AmiSSL sample for a proper implementation if needed. */
    {
        unsigned char seed[32];
        ULONG t = (ULONG)FindTask(NULL);
        for (ULONG i = 0; i < sizeof(seed); i++)
            seed[i] = (unsigned char)(t >> ((i & 3) * 8)) ^ (unsigned char)i;
        RAND_seed(seed, sizeof(seed));
    }

    st->ssl_ctx = SSL_CTX_new(TLS_client_method());
    if (!st->ssl_ctx) { HVN_Log("https: SSL_CTX_new failed"); return 0; }

    /* Resolve verification policy for this request. AUTO is best-effort
       (peer-verify if a bundle is installed, else warn and continue);
       PEER is fail-closed; NONE is the pre-Phase-2 behaviour. */
    int want_verify;
    switch (HVN_VerifyMode)
    {
        case HTMLVIEWNET_VERIFY_NONE: want_verify = 0; break;
        case HTMLVIEWNET_VERIFY_PEER: want_verify = 1; break;
        default:                      want_verify = 1; break; /* AUTO */
    }

    const char *bundle = want_verify ? HVN_ResolveCaBundle() : NULL;
    if (want_verify && bundle)
    {
        if (SSL_CTX_load_verify_locations(st->ssl_ctx, bundle, NULL) != 1)
        {
            snprintf(logbuf, sizeof(logbuf),
                     "https: load_verify_locations(%s) failed", bundle);
            HVN_Log(logbuf);
            if (HVN_VerifyMode == HTMLVIEWNET_VERIFY_PEER) return 0;
            want_verify = 0;   /* AUTO: fall back to no verify */
        }
        else
        {
            snprintf(logbuf, sizeof(logbuf),
                     "https: CA bundle loaded from %s", bundle);
            HVN_Log(logbuf);
        }
    }
    else if (want_verify)
    {
        if (HVN_VerifyMode == HTMLVIEWNET_VERIFY_PEER)
        {
            HVN_Log("https: VERIFY_PEER requested but no CA bundle found");
            return 0;
        }
        HVN_Log("https: no CA bundle found, continuing without verification");
        want_verify = 0;       /* AUTO fallback */
    }

    SSL_CTX_set_verify(st->ssl_ctx,
                       want_verify ? SSL_VERIFY_PEER : SSL_VERIFY_NONE,
                       NULL);

    st->ssl = SSL_new(st->ssl_ctx);
    if (!st->ssl) { HVN_Log("https: SSL_new failed"); return 0; }

    SSL_set_fd(st->ssl, (int)st->socket);
    SSL_set_tlsext_host_name(st->ssl, host);

    /* Hostname check: the chain-level SSL_VERIFY_PEER only validates the
       certificate chain, not that the cert matches the host we asked for.
       Enable RFC 6125 hostname matching so a valid cert for example.com
       can't be reused against example.org. */
    if (want_verify)
    {
        SSL_set_hostflags(st->ssl, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
        if (SSL_set1_host(st->ssl, host) != 1)
        {
            HVN_Log("https: SSL_set1_host failed");
            return 0;
        }
    }

    int h = SSL_connect(st->ssl);
    if (h <= 0)
    {
        int  err = SSL_get_error(st->ssl, h);
        long verr = SSL_get_verify_result(st->ssl);
        if (verr != X509_V_OK)
            snprintf(logbuf, sizeof(logbuf),
                     "https: SSL_connect rc=%d err=%d verify=%ld (%s)",
                     h, err, verr, X509_verify_cert_error_string(verr));
        else
            snprintf(logbuf, sizeof(logbuf),
                     "https: SSL_connect failed rc=%d err=%d", h, err);
        HVN_Log(logbuf);
        return 0;
    }

    snprintf(logbuf, sizeof(logbuf), "https: handshake OK cipher=%s", SSL_get_cipher(st->ssl));
    HVN_Log(logbuf);
    return 1;
}
#endif /* HAVE_AMISSL */

/* --- per-hop connect (TCP + optional TLS). Does NOT send the request. - */

static int HVN_Connect(struct HVN_State *st, const char *host, ULONG port, int use_tls)
{
    char logbuf[256];
    st->use_tls = use_tls;

#ifndef HAVE_AMISSL
    if (use_tls)
    {
        HVN_Log("https: no AmiSSL support compiled in");
        return 0;
    }
#endif

    struct Library *SocketBase = OpenLibrary((STRPTR)"bsdsocket.library", 4);
#if defined(__amigaos4__)
    struct SocketIFace *ISocket = SocketBase
        ? (struct SocketIFace *)GetInterface(SocketBase, "main", 1, NULL)
        : NULL;
    if (!SocketBase || !ISocket)
    {
        if (SocketBase && !ISocket) CloseLibrary(SocketBase);
        HVN_Log("http: OpenLibrary bsdsocket.library failed");
        return 0;
    }
    st->SBase = SocketBase; st->SIFace = ISocket;
#else
    if (!SocketBase) { HVN_Log("http: OpenLibrary bsdsocket.library failed"); return 0; }
    st->SBase = SocketBase;
#endif

    struct hostent *he;
#if defined(__amigaos4__)
    he = (struct hostent *)gethostbyname((STRPTR)host);
#else
    he = gethostbyname((STRPTR)host);
#endif
    if (!he)
    {
        snprintf(logbuf, sizeof(logbuf), "http: gethostbyname(%s) failed", host);
        HVN_Log(logbuf);
        return 0;
    }

    st->socket = socket(AF_INET, SOCK_STREAM, 0);
    if (st->socket < 0) { HVN_Log("http: socket() failed"); return 0; }

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port   = htons((UWORD)port);
    sin.sin_addr.s_addr = *((ULONG *)he->h_addr_list[0]);

    if (connect(st->socket, (struct sockaddr *)&sin, sizeof(sin)) < 0)
    {
        HVN_Log("http: connect() failed");
        return 0;
    }

#ifdef HAVE_AMISSL
    if (use_tls)
    {
        if (!HVN_TlsWrap(st, host)) return 0;
    }
#endif
    return 1;
}

/* --- build + send GET; read headers; stash any body bytes in st->buffer --
   Returns:  1 = headers OK, body follows
             0 = error / non-200
             -1 = redirect; next URL copied into redirect_out (size plen). */
static int HVN_DoRequest(struct HVN_State *st, const char *host,
                         const char *path, char *redirect_out, ULONG rlen)
{
    char logbuf[320];

    char request[2048];
    int rlen_s = snprintf(request, sizeof(request),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: HTMLview-Net/1.0\r\n"
        "Accept: */*\r\n"
        "Connection: close\r\n"
        "\r\n",
        path, host);

    if (HVN_Send(st, request, rlen_s) < 0)
    {
        HVN_Log("http: send() failed");
        return 0;
    }

    UBYTE *hdr = (UBYTE *)malloc(16384);
    if (!hdr) return 0;

    LONG have = 0;
    char *hdrend = NULL;
    while (have < 16384 - 1)
    {
        LONG got = HVN_Recv(st, (APTR)(hdr + have), 16384 - 1 - have);
        if (got <= 0) break;
        have += got;
        hdr[have] = 0;
        if ((hdrend = strstr((char *)hdr, "\r\n\r\n")) != NULL) break;
        if ((hdrend = strstr((char *)hdr, "\n\n")) != NULL) break;
    }

    if (!hdrend)
    {
        HVN_Log("http: no header terminator in reply");
        free(hdr);
        return 0;
    }

    ULONG header_len = (*hdrend == '\r')
        ? (ULONG)(hdrend - (char *)hdr) + 4
        : (ULONG)(hdrend - (char *)hdr) + 2;
    *hdrend = 0;

    /* Parse status line. */
    char status[160];
    ULONG n = 0;
    while (n < sizeof(status) - 1 && n < header_len &&
           hdr[n] != '\r' && hdr[n] != '\n')
    {
        status[n] = (char)hdr[n];
        n++;
    }
    status[n] = 0;
    snprintf(logbuf, sizeof(logbuf), "http: status=%s", status);
    HVN_Log(logbuf);

    int code = 0;
    const char *sp = strchr(status, ' ');
    if (sp)
    {
        while (*sp == ' ') sp++;
        while (*sp >= '0' && *sp <= '9') { code = code*10 + (*sp - '0'); sp++; }
    }

    /* Helper: extract a single named header into `out`. Case-aware: accepts
       either capitalised or lowercase names (lazy match of the common
       Amiga-side server responses). */
    #define GRAB_HDR(upper, lower, out, outsz) do {                         \
        const char *v = strstr((char *)hdr, upper);                         \
        if (!v) v = strstr((char *)hdr, lower);                             \
        if (v) {                                                            \
            v += strlen(upper);                                             \
            while (*v == ' ' || *v == '\t') v++;                            \
            const char *e = v;                                              \
            while (*e && *e != '\r' && *e != '\n') e++;                     \
            ULONG L = (ULONG)(e - v);                                       \
            if (L > (ULONG)(outsz) - 1) L = (ULONG)(outsz) - 1;             \
            memcpy(out, v, L); out[L] = 0;                                  \
        } else out[0] = 0;                                                  \
    } while (0)

    char v_loc[512], v_type[128], v_len[32], v_enc[32];
    GRAB_HDR("Location:",       "location:",       v_loc,  sizeof(v_loc));
    GRAB_HDR("Content-Type:",   "content-type:",   v_type, sizeof(v_type));
    GRAB_HDR("Content-Length:", "content-length:", v_len,  sizeof(v_len));
    GRAB_HDR("Content-Encoding:","content-encoding:", v_enc, sizeof(v_enc));
    if (v_loc[0])  { snprintf(logbuf, sizeof(logbuf), "http: location=%s", v_loc);  HVN_Log(logbuf); }
    if (v_type[0]) { snprintf(logbuf, sizeof(logbuf), "http: content-type=%s", v_type); HVN_Log(logbuf); }
    if (v_len[0])  { snprintf(logbuf, sizeof(logbuf), "http: content-length=%s", v_len);  HVN_Log(logbuf); }
    if (v_enc[0])  { snprintf(logbuf, sizeof(logbuf), "http: content-encoding=%s", v_enc); HVN_Log(logbuf); }
    #undef GRAB_HDR

    /* Stash Content-Length for the cache write path; 0 means "unknown"
       and makes the response non-cacheable. */
    {
        ULONG v = 0;
        for (const char *p = v_len; *p >= '0' && *p <= '9'; p++) v = v*10 + (*p - '0');
        st->content_length = v;
    }

    /* 3xx + Location => signal caller to reconnect. We don't handle relative
       redirects yet; the request will fail on next parse if Location isn't
       a full URL. */
    if ((code == 301 || code == 302 || code == 303 ||
         code == 307 || code == 308) && v_loc[0])
    {
        strncpy(redirect_out, v_loc, rlen - 1);
        redirect_out[rlen - 1] = 0;
        free(hdr);
        return -1;
    }

    if (code != 200)
    {
        snprintf(logbuf, sizeof(logbuf), "http: aborting (status=%d)", code);
        HVN_Log(logbuf);
        free(hdr);
        return 0;
    }

    if (strstr((char *)hdr, "Transfer-Encoding: chunked") ||
        strstr((char *)hdr, "transfer-encoding: chunked"))
        st->chunked = 1;

    ULONG body_have = (ULONG)have - header_len;
    if (body_have > 0)
    {
        st->buffer = (UBYTE *)malloc(body_have);
        if (st->buffer)
        {
            memcpy(st->buffer, hdr + header_len, body_have);
            st->buflen = body_have;
            st->bufpos = 0;
        }
    }

    snprintf(logbuf, sizeof(logbuf), "http: OK header_len=%lu body_have=%lu chunked=%d tls=%d",
            header_len, body_have, st->chunked, st->use_tls);
    HVN_Log(logbuf);

    if (body_have > 0)
    {
        UBYTE *bp = hdr + header_len;
        ULONG show = body_have < 16 ? body_have : 16;
        char hex[96];
        int off = snprintf(hex, sizeof(hex), "http: body[0..%lu]=", show);
        for (ULONG i = 0; i < show && off < (int)sizeof(hex) - 4; i++)
            off += snprintf(hex + off, sizeof(hex) - off, "%02x ", bp[i]);
        HVN_Log(hex);
    }

    free(hdr);
    return 1;
}

/* Entry point: opens a connection and reads headers, following up to 5
   redirects across http / https. Returns 1 on success, 0 on any error. */
static LONG HVN_HttpOpen(CONST_STRPTR url, struct HVN_State *st)
{
    char logbuf[384];

    /* Cache read path: if a fresh entry is on disk, serve it without
       touching the network. We store the BPTR in st->file; the existing
       Read handler picks it up transparently. */
    {
        BPTR cached = HVN_CacheTryOpen((const char *)url);
        if (cached)
        {
            st->file = cached;
            return 1;
        }
    }

    char current[1024];
    strncpy(current, url, sizeof(current) - 1);
    current[sizeof(current) - 1] = 0;

    for (int hop = 0; hop < 5; hop++)
    {
        char host[256], path[1024];
        ULONG port;
        int is_https;
        if (!HVN_ParseUrl(current, host, sizeof(host),
                          path, sizeof(path), &port, &is_https))
        {
            snprintf(logbuf, sizeof(logbuf), "http: unparseable URL '%s'", current);
            HVN_Log(logbuf);
            return 0;
        }

        snprintf(logbuf, sizeof(logbuf), "http: [hop %d] %s://%s:%lu%s",
                hop, is_https ? "https" : "http", host, port, path);
        HVN_Log(logbuf);

        if (!HVN_Connect(st, host, port, is_https))
        {
            HVN_Disconnect(st);
            return 0;
        }

        char next[1024];
        int r = HVN_DoRequest(st, host, path, next, sizeof(next));
        if (r == 1)
        {
            /* Cache write path: drain the whole response into st->buffer
               if it's within budget, mirror to disk, then close the
               socket so Read() replays from memory. Skipped when the
               response is chunked or has no Content-Length (we can't
               bound memory without Content-Length, and extending this to
               chunked streams is a follow-up). */
            if (HVN_CacheDir[0] && HVN_CacheTTL > 0 && !st->chunked &&
                st->content_length > 0 && st->content_length <= HVN_CACHE_MAX_BYTES)
            {
                ULONG total = st->content_length;
                UBYTE *full = (UBYTE *)malloc(total);
                if (full)
                {
                    ULONG have = 0;
                    if (st->buffer && st->buflen)
                    {
                        ULONG copy = st->buflen - st->bufpos;
                        if (copy > total) copy = total;
                        memcpy(full, st->buffer + st->bufpos, copy);
                        have = copy;
                        free(st->buffer);
                        st->buffer = NULL; st->buflen = st->bufpos = 0;
                    }
                    while (have < total)
                    {
                        LONG got = HVN_Recv(st, full + have, (LONG)(total - have));
                        if (got <= 0) break;
                        have += (ULONG)got;
                    }
                    if (have == total)
                    {
                        HVN_CacheStore(current, full, total, HVN_CacheTTL);
                        /* Disconnect first -- it frees st->buffer (which
                           we already freed and NULLed above) and clears
                           the socket. Then install `full` as the replay
                           buffer so Read() serves from memory. */
                        HVN_Disconnect(st);
                        st->buffer = full;
                        st->buflen = total;
                        st->bufpos = 0;
                    }
                    else
                    {
                        HVN_Log("cache: incomplete drain, skipping store");
                        st->buffer = full;
                        st->buflen = have;
                        st->bufpos = 0;
                    }
                }
            }
            return 1;
        }
        if (r == 0) { HVN_Disconnect(st); return 0; }

        /* 3xx -- tear down and try the new URL. */
        HVN_Disconnect(st);
        strncpy(current, next, sizeof(current) - 1);
        current[sizeof(current) - 1] = 0;
    }

    HVN_Log("http: too many redirects");
    HVN_Disconnect(st);
    return 0;
}

/* --- the hook function ------------------------------------------------- */

ULONG HTMLviewNet_HookFunc(struct Hook *hook, APTR obj,
                           struct HTMLview_LoadMsg *msg)
{
    (void)hook;
    (void)obj;
    if (!msg) return 0;

    switch (msg->lm_Type)
    {
        case HTMLview_Open:
        {
            CONST_STRPTR url = msg->lm_Params.lm_Open.URL;
            if (!url) return 0;

            struct HVN_State *st = (struct HVN_State *)calloc(1, sizeof(*st));
            if (!st) return 0;
            st->socket = -1;
            msg->lm_Userdata = st;

            if (strncmp(url, "http://", 7) == 0 ||
                strncmp(url, "https://", 8) == 0)
            {
                if (!HVN_HttpOpen(url, st))
                {
                    free(st); msg->lm_Userdata = NULL; return 0;
                }
                return 1;
            }

            if (strncmp(url, "file://", 7) == 0) url += 7;

            st->file = Open((STRPTR)url, MODE_OLDFILE);
            if (st->file) return 1;

            free(st); msg->lm_Userdata = NULL;
            return 0;
        }

        case HTMLview_Read:
        {
            struct HVN_State *st = (struct HVN_State *)msg->lm_Userdata;
            if (!st) return 0;

            STRPTR out = msg->lm_Params.lm_Read.Buffer;
            LONG   len = msg->lm_Params.lm_Read.Size;
            if (!out || len <= 0) return 0;

            /* Replay stashed body bytes first. */
            if (st->buffer && st->bufpos < st->buflen)
            {
                ULONG remain = st->buflen - st->bufpos;
                ULONG n = (remain < (ULONG)len) ? remain : (ULONG)len;
                memcpy(out, st->buffer + st->bufpos, n);
                st->bufpos += n;
                if (st->bufpos >= st->buflen)
                {
                    free(st->buffer);
                    st->buffer = NULL;
                }
                return n;
            }

            if (st->file)
            {
                LONG rd = Read(st->file, out, len);
                return rd > 0 ? rd : 0;
            }

            if (st->socket >= 0)
            {
                LONG rd = st->chunked
                    ? HVN_ReadChunked(st, (UBYTE *)out, len)
                    : HVN_Recv(st, out, len);
                {
                    char logbuf[96];
                    snprintf(logbuf, sizeof(logbuf), "read: recv(sock=%ld want=%ld) => %ld%s%s",
                            st->socket, len, rd,
                            st->chunked ? " (chunked)" : "",
                            st->use_tls ? " (tls)" : "");
                    HVN_Log(logbuf);
                }
                return rd > 0 ? rd : 0;
            }

            return 0;
        }

        case HTMLview_Write:
            return 0;

        case HTMLview_Close:
        {
            struct HVN_State *st = (struct HVN_State *)msg->lm_Userdata;
            if (st)
            {
                if (st->file) Close(st->file);
                HVN_Disconnect(st);
                free(st);
                msg->lm_Userdata = NULL;
            }
            return 1;
        }
    }
    return 0;
}

/* --- public wiring helper --------------------------------------------- */

void HTMLviewNet_InitHook(struct Hook *hook)
{
    if (!hook) return;

    /* CallHookPkt on m68k and MorphOS passes args in registers (a0=hook,
       a2=obj, a1=msg); our plain-C hook function expects stack args.
       HookEntry is the amiga.lib trampoline that does the register->stack
       conversion. On OS4 hooks are already called with stack args, and
       HookEntry is not declared, so we assign directly. */
#if defined(__amigaos4__)
    hook->h_Entry    = (HOOKFUNC)HTMLviewNet_HookFunc;
    hook->h_SubEntry = NULL;
#else
    hook->h_Entry    = (HOOKFUNC)HookEntry;
    hook->h_SubEntry = (HOOKFUNC)HTMLviewNet_HookFunc;
#endif
    hook->h_Data     = NULL;
}
