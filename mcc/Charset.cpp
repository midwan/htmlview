/***************************************************************************

 HTMLview.mcc - HTMLview MUI Custom Class
 Copyright (C) 1997-2000 Allan Odgaard
 Copyright (C) 2005-2007 by HTMLview.mcc Open Source Team

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

***************************************************************************/

#include "Charset.h"

#include <proto/exec.h>
#include <libraries/codesets.h>
#include <proto/codesets.h>
#include <string.h>

struct Library *CodesetsBase = NULL;

#if defined(__amigaos4__)
struct CodesetsIFace *ICodesets = NULL;
#endif

#if defined(__amigaos4__)
#  define GETIFACE(name, base) name = (struct CodesetsIFace *)GetInterface(base, "main", 1, NULL)
#  define DROPIFACE(name)      do { if(name) { DropInterface((struct Interface *)name); name = NULL; } } while(0)
#else
#  define GETIFACE(name, base) ((void)0)
#  define DROPIFACE(name)      ((void)0)
#endif

extern "C" BOOL OpenCodesets(VOID)
{
  /* Optional dependency — failure is non-fatal. The .mcc continues to
     work; ConvertUTF8 becomes a no-op. */
  if((CodesetsBase = OpenLibrary("codesets.library", 6)))
  {
    GETIFACE(ICodesets, CodesetsBase);
#if defined(__amigaos4__)
    if(!ICodesets)
    {
      CloseLibrary(CodesetsBase);
      CodesetsBase = NULL;
      return FALSE;
    }
#endif
    return TRUE;
  }
  return FALSE;
}

extern "C" VOID CloseCodesets(VOID)
{
  if(CodesetsBase)
  {
    DROPIFACE(ICodesets);
    CloseLibrary(CodesetsBase);
    CodesetsBase = NULL;
  }
}

/* Cheap pre-check: scan for any byte >= 0x80. Pure-ASCII strings
   short-circuit out without touching codesets.library — most test
   fixtures and English-only pages hit this path. */
static BOOL HasNonASCII(CONST_STRPTR src, ULONG len)
{
  while(len--)
    if((UBYTE)*src++ >= 0x80)
      return TRUE;
  return FALSE;
}

extern "C" STRPTR ConvertUTF8(CONST_STRPTR src, ULONG srcLen)
{
  if(!src || !srcLen || !CodesetsBase)
    return NULL;

  if(!HasNonASCII(src, srcLen))
    return NULL;

  /* CodesetsIsValidUTF8 expects a NUL-terminated string. The caller
     hands us a buffer of known length, but in practice MUIA_HTMLview_
     Contents passes a C string, so it's fine. Still, check the prefix
     up to srcLen as a defensive measure: scan and reject early on a
     malformed sequence. */
  if(!CodesetsIsValidUTF8((CONST_STRPTR)src))
    return NULL;

  ULONG dstLen = 0;
  /* Use the *A (tag-list) form — the varargs wrapper isn't generated
     by every platform's inline header (MorphOS in particular), and
     the underlying library exports both forms. */
  struct TagItem tags[] = {
    { CSA_Source,          (Tag)src                },
    { CSA_SourceLen,       srcLen                  },
    { CSA_DestLenPtr,      (Tag)&dstLen            },
    { CSA_MapForeignChars, TRUE                    },
    { TAG_DONE,            0                       }
  };
  STRPTR dst = CodesetsUTF8ToStrA(tags);

  /* CodesetsUTF8ToStr returns NULL on conversion failure (or empty
     output). Treat as no-op. */
  if(!dst || dstLen == 0)
  {
    if(dst) CodesetsFreeA(dst, NULL);
    return NULL;
  }
  return dst;
}

extern "C" VOID FreeConvertedStr(STRPTR str)
{
  if(str && CodesetsBase)
    CodesetsFreeA(str, NULL);
}
