/*
 * test_image_hook.h  --  Shared image-load hook used by SimpleTest and
 *                        LibLoad_Test to feed picture data to HTMLview.mcc.
 *
 * URL schemes resolved by the hook:
 *   - PROGDIR:, DH0:, etc.  -- direct dos Open
 *   - file://<path>         -- stripped and dos Open
 *   - http://host[:port]/p  -- bsdsocket GET, chunked-aware, redirect-aware
 *   - https://host[:port]/p -- AmiSSL-wrapped GET (only if HAVE_AMISSL)
 *
 * The hook follows up to 5 consecutive 3xx redirects (absolute URLs only;
 * relative Location: headers are NOT supported yet) and upgrades http->https
 * transparently when a server redirects to TLS.
 *
 * HAVE_AMISSL is defined by the Makefile when the AmiSSL SDK is present.
 * Without it, https:// URLs and http->https redirects fail cleanly.
 *
 * Threading: runs in the HTMLview decoder task. On m68k every task that uses
 * bsdsocket must OpenLibrary the library itself, so this file opens a task-
 * local SocketBase (and AmiSSLMasterBase / AmiSSLBase) in the hook and
 * shadows the file-scope globals so the proto/socket.h inline macros
 * dispatch through the local base.
 *
 * Include this header from exactly one translation unit per test program.
 */

#ifndef HTMLVIEW_TEST_IMAGE_HOOK_H
#define HTMLVIEW_TEST_IMAGE_HOOK_H

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/socket.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <netdb.h>
#include "HTMLview_mcc.h"

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


/* Owned by the test program's main task. The hook uses its own task-local
   library bases and never touches these from the decoder thread. */
#if defined(__amigaos4__)
extern struct Library     *SocketBase;
extern struct SocketIFace *ISocket;
#else
extern struct Library *SocketBase;
#endif

/* Per-transfer state stored in lm_Userdata. */
struct THL_State
{
    BPTR  file;        /* file handle if this is a local file */
    LONG  socket;      /* bsdsocket handle, -1 if none */
    UBYTE *buffer;     /* stashed body bytes from the initial HTTP recv */
    ULONG  bufpos;
    ULONG  buflen;
    int    chunked;    /* non-zero => Transfer-Encoding: chunked */
    ULONG  chunk_left;
    int    use_tls;    /* non-zero => route I/O through SSL_* */

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
static char   THL_LogBuf[16384];
static ULONG  THL_LogLen = 0;

static void THL_Log(const char *line)
{
    ULONG need = strlen(line) + 1;
    if (THL_LogLen + need + 1 >= sizeof(THL_LogBuf)) return;
    memcpy(THL_LogBuf + THL_LogLen, line, need - 1);
    THL_LogLen += need - 1;
    THL_LogBuf[THL_LogLen++] = '\n';
    THL_LogBuf[THL_LogLen]   = 0;
    BPTR f = Open((STRPTR)"T:htmlview_hook.log", MODE_NEWFILE);
    if (f) { Write(f, THL_LogBuf, THL_LogLen); Close(f); }
}

/* --- low-level I/O (plain TCP or TLS depending on st->use_tls) --------- */

static LONG THL_Recv(struct THL_State *st, APTR buf, LONG len)
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

static LONG THL_Send(struct THL_State *st, const APTR buf, LONG len)
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

static LONG THL_RecvLine(struct THL_State *st, char *buf, LONG maxlen)
{
    LONG i = 0;
    while (i < maxlen - 1)
    {
        UBYTE c;
        LONG got = THL_Recv(st, (APTR)&c, 1);
        if (got <= 0) return -1;
        if (c == '\n') { buf[i] = 0; return i; }
        if (c != '\r') buf[i++] = c;
    }
    buf[i] = 0;
    return i;
}

/* Reads up to `want` bytes, unwrapping chunked transfer encoding. */
static LONG THL_ReadChunked(struct THL_State *st, UBYTE *out, LONG want)
{
    LONG produced = 0;
    while (produced < want)
    {
        if (st->chunk_left == 0)
        {
            char line[32];
            if (THL_RecvLine(st, line, sizeof(line)) < 0)
                return produced;
            if (line[0] == 0)
            {
                if (THL_RecvLine(st, line, sizeof(line)) < 0)
                    return produced;
            }
            ULONG size = strtoul(line, NULL, 16);
            if (size == 0) return produced;
            st->chunk_left = size;
        }

        LONG take = want - produced;
        if ((ULONG)take > st->chunk_left) take = (LONG)st->chunk_left;

        LONG got = THL_Recv(st, (APTR)(out + produced), take);
        if (got <= 0) return produced;
        produced += got;
        st->chunk_left -= got;
    }
    return produced;
}

/* Parses http:// or https:// URLs. Sets *is_https from scheme. */
static int THL_ParseUrl(CONST_STRPTR url, char *host, ULONG hlen,
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

static void THL_Disconnect(struct THL_State *st)
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

/* --- AmiSSL-wrapped connect (only linked when HAVE_AMISSL) ------------- */

#ifdef HAVE_AMISSL
static int THL_TlsWrap(struct THL_State *st, const char *host)
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
        THL_Log("https: OpenLibrary amisslmaster.library failed");
        return 0;
    }
    AmiSSLMasterBase = st->AMSBase;
#if defined(__amigaos4__)
    st->IAMSMaster = (struct AmiSSLMasterIFace *)
        GetInterface(st->AMSBase, "main", 1, NULL);
    if (!st->IAMSMaster) { THL_Log("https: GetInterface IAmiSSLMaster failed"); return 0; }
    IAmiSSLMaster = st->IAMSMaster;
#endif

    if (!InitAmiSSLMaster(AMISSL_CURRENT_VERSION, TRUE))
    {
        THL_Log("https: InitAmiSSLMaster failed (library too old)");
        return 0;
    }

    st->ASBase = OpenAmiSSL();
    if (!st->ASBase)
    {
        THL_Log("https: OpenAmiSSL failed");
        return 0;
    }
    AmiSSLBase = st->ASBase;
#if defined(__amigaos4__)
    st->IAS = (struct AmiSSLIFace *)GetInterface(st->ASBase, "main", 1, NULL);
    if (!st->IAS) { THL_Log("https: GetInterface IAmiSSL failed"); return 0; }
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
            THL_Log("https: InitAmiSSL failed");
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
    if (!st->ssl_ctx) { THL_Log("https: SSL_CTX_new failed"); return 0; }

    /* Disable peer verification for the test programs. Strict verification
       needs ENVARC:AmiSSL/cacert.pem installed; relaxing lets the tests run
       on a barebones setup without silently masking real TLS errors -- any
       real protocol failure still shows up in SSL_connect below. */
    SSL_CTX_set_verify(st->ssl_ctx, SSL_VERIFY_NONE, NULL);

    st->ssl = SSL_new(st->ssl_ctx);
    if (!st->ssl) { THL_Log("https: SSL_new failed"); return 0; }

    SSL_set_fd(st->ssl, (int)st->socket);
    SSL_set_tlsext_host_name(st->ssl, host);

    int h = SSL_connect(st->ssl);
    if (h <= 0)
    {
        int err = SSL_get_error(st->ssl, h);
        sprintf(logbuf, "https: SSL_connect failed rc=%d err=%d", h, err);
        THL_Log(logbuf);
        return 0;
    }

    sprintf(logbuf, "https: handshake OK cipher=%s", SSL_get_cipher(st->ssl));
    THL_Log(logbuf);
    return 1;
}
#endif /* HAVE_AMISSL */

/* --- per-hop connect (TCP + optional TLS). Does NOT send the request. - */

static int THL_Connect(struct THL_State *st, const char *host, ULONG port, int use_tls)
{
    char logbuf[256];
    st->use_tls = use_tls;

#ifndef HAVE_AMISSL
    if (use_tls)
    {
        THL_Log("https: no AmiSSL support compiled in");
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
        THL_Log("http: OpenLibrary bsdsocket.library failed");
        return 0;
    }
    st->SBase = SocketBase; st->SIFace = ISocket;
#else
    if (!SocketBase) { THL_Log("http: OpenLibrary bsdsocket.library failed"); return 0; }
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
        sprintf(logbuf, "http: gethostbyname(%s) failed", host);
        THL_Log(logbuf);
        return 0;
    }

    st->socket = socket(AF_INET, SOCK_STREAM, 0);
    if (st->socket < 0) { THL_Log("http: socket() failed"); return 0; }

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port   = htons((UWORD)port);
    sin.sin_addr.s_addr = *((ULONG *)he->h_addr_list[0]);

    if (connect(st->socket, (struct sockaddr *)&sin, sizeof(sin)) < 0)
    {
        THL_Log("http: connect() failed");
        return 0;
    }

#ifdef HAVE_AMISSL
    if (use_tls)
    {
        if (!THL_TlsWrap(st, host)) return 0;
    }
#endif
    return 1;
}

/* --- build + send GET; read headers; stash any body bytes in st->buffer --
   Returns:  1 = headers OK, body follows
             0 = error / non-200
             -1 = redirect; next URL copied into redirect_out (size plen). */
static int THL_DoRequest(struct THL_State *st, const char *host,
                         const char *path, char *redirect_out, ULONG rlen)
{
    char logbuf[320];

    char request[2048];
    int rlen_s = sprintf(request,
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: HTMLview-TestHook/1.2\r\n"
        "Accept: */*\r\n"
        "Connection: close\r\n"
        "\r\n",
        path, host);

    if (THL_Send(st, request, rlen_s) < 0)
    {
        THL_Log("http: send() failed");
        return 0;
    }

    UBYTE *hdr = (UBYTE *)malloc(16384);
    if (!hdr) return 0;

    LONG have = 0;
    char *hdrend = NULL;
    while (have < 16384 - 1)
    {
        LONG got = THL_Recv(st, (APTR)(hdr + have), 16384 - 1 - have);
        if (got <= 0) break;
        have += got;
        hdr[have] = 0;
        if ((hdrend = strstr((char *)hdr, "\r\n\r\n")) != NULL) break;
        if ((hdrend = strstr((char *)hdr, "\n\n")) != NULL) break;
    }

    if (!hdrend)
    {
        THL_Log("http: no header terminator in reply");
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
    sprintf(logbuf, "http: status=%s", status);
    THL_Log(logbuf);

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
    if (v_loc[0])  { sprintf(logbuf, "http: location=%s", v_loc);  THL_Log(logbuf); }
    if (v_type[0]) { sprintf(logbuf, "http: content-type=%s", v_type); THL_Log(logbuf); }
    if (v_len[0])  { sprintf(logbuf, "http: content-length=%s", v_len);  THL_Log(logbuf); }
    if (v_enc[0])  { sprintf(logbuf, "http: content-encoding=%s", v_enc); THL_Log(logbuf); }
    #undef GRAB_HDR

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
        sprintf(logbuf, "http: aborting (status=%d)", code);
        THL_Log(logbuf);
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

    sprintf(logbuf, "http: OK header_len=%lu body_have=%lu chunked=%d tls=%d",
            header_len, body_have, st->chunked, st->use_tls);
    THL_Log(logbuf);

    if (body_have > 0)
    {
        UBYTE *bp = hdr + header_len;
        ULONG show = body_have < 16 ? body_have : 16;
        char hex[96];
        int off = sprintf(hex, "http: body[0..%lu]=", show);
        for (ULONG i = 0; i < show; i++)
            off += sprintf(hex + off, "%02x ", bp[i]);
        THL_Log(hex);
    }

    free(hdr);
    return 1;
}

/* Entry point: opens a connection and reads headers, following up to 5
   redirects across http / https. Returns 1 on success, 0 on any error. */
static LONG THL_HttpOpen(CONST_STRPTR url, struct THL_State *st)
{
    char logbuf[384];
    char current[1024];
    strncpy(current, url, sizeof(current) - 1);
    current[sizeof(current) - 1] = 0;

    for (int hop = 0; hop < 5; hop++)
    {
        char host[256], path[1024];
        ULONG port;
        int is_https;
        if (!THL_ParseUrl(current, host, sizeof(host),
                          path, sizeof(path), &port, &is_https))
        {
            sprintf(logbuf, "http: unparseable URL '%s'", current);
            THL_Log(logbuf);
            return 0;
        }

        sprintf(logbuf, "http: [hop %d] %s://%s:%lu%s",
                hop, is_https ? "https" : "http", host, port, path);
        THL_Log(logbuf);

        if (!THL_Connect(st, host, port, is_https))
        {
            THL_Disconnect(st);
            return 0;
        }

        char next[1024];
        int r = THL_DoRequest(st, host, path, next, sizeof(next));
        if (r == 1) return 1;
        if (r == 0) { THL_Disconnect(st); return 0; }

        /* 3xx -- tear down and try the new URL. */
        THL_Disconnect(st);
        strncpy(current, next, sizeof(current) - 1);
        current[sizeof(current) - 1] = 0;
    }

    THL_Log("http: too many redirects");
    THL_Disconnect(st);
    return 0;
}

/* --- the hook function ------------------------------------------------- */

static ULONG TestImageHookFunc(struct Hook *hook, APTR obj,
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

            struct THL_State *st = (struct THL_State *)calloc(1, sizeof(*st));
            if (!st) return 0;
            st->socket = -1;
            msg->lm_Userdata = st;

            if (strncmp(url, "http://", 7) == 0 ||
                strncmp(url, "https://", 8) == 0)
            {
                if (!THL_HttpOpen(url, st))
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
            struct THL_State *st = (struct THL_State *)msg->lm_Userdata;
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
                    ? THL_ReadChunked(st, (UBYTE *)out, len)
                    : THL_Recv(st, out, len);
                {
                    char logbuf[96];
                    sprintf(logbuf, "read: recv(sock=%ld want=%ld) => %ld%s%s",
                            st->socket, len, rd,
                            st->chunked ? " (chunked)" : "",
                            st->use_tls ? " (tls)" : "");
                    THL_Log(logbuf);
                }
                return rd > 0 ? rd : 0;
            }

            return 0;
        }

        case HTMLview_Write:
            return 0;

        case HTMLview_Close:
        {
            struct THL_State *st = (struct THL_State *)msg->lm_Userdata;
            if (st)
            {
                if (st->file) Close(st->file);
                THL_Disconnect(st);
                free(st);
                msg->lm_Userdata = NULL;
            }
            return 1;
        }
    }
    return 0;
}

#endif /* HTMLVIEW_TEST_IMAGE_HOOK_H */
