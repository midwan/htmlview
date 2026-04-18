/*
 * test_image_hook.h  --  Shared image-load hook used by SimpleTest and
 *                        LibLoad_Test to feed picture data to HTMLview.mcc.
 *
 * The hook resolves three kinds of URLs:
 *   - PROGDIR:, DH0:, etc.  (direct dos Open)
 *   - file://<path>         (stripped and dos Open)
 *   - http://host[:port]/p  (bsdsocket.library GET, chunked aware)
 *
 * https:// URLs return failure (no TLS in the stock Amiga bsdsocket).
 *
 * The header is intentionally single-include, single-translation-unit.
 * Include it from exactly one .c file in a test program; the program owns
 * the SocketBase / ISocket globals (so it can close them on exit).
 *
 * Threading notes: the hook runs in the HTMLview decoder thread, not the
 * main task. On m68k every task that uses bsdsocket must OpenLibrary the
 * library itself -- the library base opened by main() isn't valid for our
 * thread. We therefore open a task-local SocketBase in the hook and shadow
 * the file-scope globals so the proto/socket.h inline macros dispatch
 * through the local base.
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

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/* The test programs carry their own globals for this; only used by the
   main task. The hook uses its own task-local bases instead (see below). */
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

    /* Task-local bsdsocket bases -- opened by the decoder thread so it can
       actually call socket/recv/etc.; closed on hook-Close. */
    struct Library *SBase;
#if defined(__amigaos4__)
    struct SocketIFace *SIFace;
#endif
};

/* --- logging ----------------------------------------------------------- */

/* Accumulating-buffer + rewrite pattern (same as ImageManager DTLog) so the
   log file stays coherent across OS3 and OS4 without relying on Seek. */
static char   THL_LogBuf[8192];
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

/* --- low-level helpers ------------------------------------------------- */

static LONG THL_ReadChunked(struct THL_State *st, UBYTE *out, LONG want);

static LONG THL_RecvLine(struct THL_State *st, char *buf, LONG maxlen)
{
    /* Shadow file-scope bases with our task-local ones for proto/socket.h. */
    struct Library *SocketBase = st->SBase;
#if defined(__amigaos4__)
    struct SocketIFace *ISocket = st->SIFace;
#endif

    LONG i = 0;
    while (i < maxlen - 1)
    {
        UBYTE c;
        LONG got = recv(st->socket, (APTR)&c, 1, 0);
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
    struct Library *SocketBase = st->SBase;
#if defined(__amigaos4__)
    struct SocketIFace *ISocket = st->SIFace;
#endif

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

        LONG got = recv(st->socket, (APTR)(out + produced), take, 0);
        if (got <= 0) return produced;
        produced += got;
        st->chunk_left -= got;
    }
    return produced;
}

/* Parses "http://host[:port]/path" into components (buffers owned by caller). */
static int THL_ParseHttp(CONST_STRPTR url, char *host, ULONG hlen,
                         char *path, ULONG plen, ULONG *portp)
{
    const char *p = url;
    if (strncmp(p, "http://", 7) != 0) return 0;
    p += 7;

    const char *hs = p;
    while (*p && *p != '/' && *p != ':') p++;

    ULONG n = (ULONG)(p - hs);
    if (n == 0 || n >= hlen) return 0;
    memcpy(host, hs, n);
    host[n] = 0;

    ULONG port = 80;
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

/* Opens an HTTP connection, reads and parses response headers, stashes any
   body bytes that came with the header recv. Returns 1 on success. */
static LONG THL_HttpOpen(CONST_STRPTR url, struct THL_State *st)
{
    char logbuf[320];

    char host[256], path[1024];
    ULONG port = 80;
    if (!THL_ParseHttp(url, host, sizeof(host), path, sizeof(path), &port))
    {
        THL_Log("http: parse URL failed");
        return 0;
    }

    sprintf(logbuf, "http: host=%s port=%lu path=%s", host, port, path);
    THL_Log(logbuf);

    /* Open bsdsocket fresh for THIS task -- the decoder thread. On m68k the
       library is opened per-task; the main task's SocketBase is not usable
       from us. */
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
#else
    if (!SocketBase)
    {
        THL_Log("http: OpenLibrary bsdsocket.library failed");
        return 0;
    }
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
#if defined(__amigaos4__)
        if (ISocket) DropInterface((struct Interface *)ISocket);
#endif
        CloseLibrary(SocketBase);
        return 0;
    }

    LONG s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
    {
        THL_Log("http: socket() failed");
#if defined(__amigaos4__)
        if (ISocket) DropInterface((struct Interface *)ISocket);
#endif
        CloseLibrary(SocketBase);
        return 0;
    }

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port   = htons((UWORD)port);
    sin.sin_addr.s_addr = *((ULONG *)he->h_addr_list[0]);

    if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) < 0)
    {
        THL_Log("http: connect() failed");
        CloseSocket(s);
#if defined(__amigaos4__)
        if (ISocket) DropInterface((struct Interface *)ISocket);
#endif
        CloseLibrary(SocketBase);
        return 0;
    }

    char request[2048];
    int rlen = sprintf(request,
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: HTMLview-TestHook/1.1\r\n"
        "Accept: */*\r\n"
        "Connection: close\r\n"
        "\r\n",
        path, host);

    if (send(s, request, rlen, 0) < 0)
    {
        THL_Log("http: send() failed");
        CloseSocket(s);
#if defined(__amigaos4__)
        if (ISocket) DropInterface((struct Interface *)ISocket);
#endif
        CloseLibrary(SocketBase);
        return 0;
    }

    /* Read a moderate chunk so we're very likely to have all headers plus
       some body in a single recv. */
    UBYTE *hdr = (UBYTE *)malloc(16384);
    if (!hdr)
    {
        CloseSocket(s);
#if defined(__amigaos4__)
        if (ISocket) DropInterface((struct Interface *)ISocket);
#endif
        CloseLibrary(SocketBase);
        return 0;
    }

    LONG have = 0;
    char *hdrend = NULL;
    while (have < 16384 - 1)
    {
        LONG got = recv(s, (APTR)(hdr + have), 16384 - 1 - have, 0);
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
        CloseSocket(s);
#if defined(__amigaos4__)
        if (ISocket) DropInterface((struct Interface *)ISocket);
#endif
        CloseLibrary(SocketBase);
        return 0;
    }

    /* hdrend points at the first byte of the header/body separator.
       Separator is either "\r\n\r\n" (4 bytes) or "\n\n" (2 bytes).
       Compute body offset BEFORE null-terminating. */
    ULONG header_len = (*hdrend == '\r')
        ? (ULONG)(hdrend - (char *)hdr) + 4
        : (ULONG)(hdrend - (char *)hdr) + 2;

    /* Null-terminate the header block so strstr on header values is safe.
       Writing at hdrend clobbers only the first byte of the separator,
       which we no longer need -- NOT the first body byte. */
    *hdrend = 0;

    {
        /* Log the HTTP status line so we can spot 301/302 redirects, 404s,
           etc. The header block also tells us whether the server returned
           HTML instead of an image. */
        char status[120];
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

        /* Bail on non-200 responses -- redirects, 404s and 5xx must not
           reach the decoder as "image body". Parse the numeric code. */
        int code = 0;
        const char *sp = strchr(status, ' ');
        if (sp)
        {
            while (*sp == ' ') sp++;
            while (*sp >= '0' && *sp <= '9') { code = code*10 + (*sp - '0'); sp++; }
        }
        if (code != 200)
        {
            sprintf(logbuf, "http: aborting (status=%d)", code);
            THL_Log(logbuf);
            free(hdr);
            CloseSocket(s);
#if defined(__amigaos4__)
            if (ISocket) DropInterface((struct Interface *)ISocket);
#endif
            CloseLibrary(SocketBase);
            return 0;
        }

        /* Helper: dig out a header value by name, log as "http: <tag>=<val>". */
        const char *hdrs[] = { "Location:", "location:",
                               "Content-Type:", "content-type:",
                               "Content-Length:", "content-length:",
                               "Content-Encoding:", "content-encoding:" };
        const char *tags[] = { "location", "location",
                               "content-type", "content-type",
                               "content-length", "content-length",
                               "content-encoding", "content-encoding" };
        for (ULONG i = 0; i < sizeof(hdrs)/sizeof(hdrs[0]); i++)
        {
            const char *v = strstr((char *)hdr, hdrs[i]);
            if (!v) continue;
            v += strlen(hdrs[i]);
            while (*v == ' ' || *v == '\t') v++;
            const char *e = v;
            while (*e && *e != '\r' && *e != '\n') e++;
            ULONG L = (ULONG)(e - v);
            if (L > sizeof(status) - 1) L = sizeof(status) - 1;
            memcpy(status, v, L);
            status[L] = 0;
            sprintf(logbuf, "http: %s=%s", tags[i], status);
            THL_Log(logbuf);
        }
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

    sprintf(logbuf, "http: OK header_len=%lu body_have=%lu chunked=%d",
            header_len, body_have, st->chunked);
    THL_Log(logbuf);

    /* Log the first 16 body bytes in hex -- lets us see the magic number
       of the actual payload so we can tell PNG / GIF / HTML / gzip apart
       at a glance. */
    if (body_have > 0)
    {
        UBYTE *bp = hdr + header_len;
        ULONG show = body_have < 16 ? body_have : 16;
        char hex[80];
        int off = sprintf(hex, "http: body[0..%lu]=", show);
        for (ULONG i = 0; i < show; i++)
            off += sprintf(hex + off, "%02x ", bp[i]);
        THL_Log(hex);
    }

    st->socket = s;
    st->SBase  = SocketBase;
#if defined(__amigaos4__)
    st->SIFace = ISocket;
#endif
    free(hdr);
    return 1;
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

            if (strncmp(url, "https://", 8) == 0)
            {
                THL_Log("hook: https:// unsupported");
                free(st); msg->lm_Userdata = NULL; return 0;
            }

            if (strncmp(url, "http://", 7) == 0)
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
                struct Library *SocketBase = st->SBase;
#if defined(__amigaos4__)
                struct SocketIFace *ISocket = st->SIFace;
#endif
                LONG rd = st->chunked
                    ? THL_ReadChunked(st, (UBYTE *)out, len)
                    : recv(st->socket, out, len, 0);
                {
                    char logbuf[96];
                    sprintf(logbuf, "read: recv(sock=%ld want=%ld) => %ld%s",
                            st->socket, len, rd, st->chunked ? " (chunked)" : "");
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

                if (st->socket >= 0 && st->SBase)
                {
                    struct Library *SocketBase = st->SBase;
#if defined(__amigaos4__)
                    struct SocketIFace *ISocket = st->SIFace;
#endif
                    CloseSocket(st->socket);
                }

#if defined(__amigaos4__)
                if (st->SIFace)
                    DropInterface((struct Interface *)st->SIFace);
#endif
                if (st->SBase) CloseLibrary(st->SBase);

                if (st->buffer) free(st->buffer);
                free(st);
                msg->lm_Userdata = NULL;
            }
            return 1;
        }
    }
    return 0;
}

#endif /* HTMLVIEW_TEST_IMAGE_HOOK_H */
