/***************************************************************************

 HTMLview.mcc - HTMLview MUI Custom Class
 Copyright (C) 1997-2000 Allan Odgaard
 Copyright (C) 2005-2007 by HTMLview.mcc Open Source Team

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

***************************************************************************/

#ifndef CHARSET_H
#define CHARSET_H

#include <exec/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Open codesets.library v6+ for the lifetime of the .mcc. Failure is
   non-fatal: ConvertUTF8() then becomes a no-op (returns NULL). Called
   from ClassInit; do NOT call from class code. */
BOOL OpenCodesets(VOID);
VOID CloseCodesets(VOID);

/* If `src` is valid UTF-8 with at least one non-ASCII byte and
   codesets.library is available, return a freshly-allocated, NUL-
   terminated copy converted to the system codeset. Otherwise return
   NULL — the caller should fall back to the original buffer.

   srcLen is the byte length of src (excluding any NUL).

   The returned pointer must be released with FreeConvertedStr(). */
STRPTR ConvertUTF8(CONST_STRPTR src, ULONG srcLen);

VOID FreeConvertedStr(STRPTR str);

#ifdef __cplusplus
}
#endif

#endif /* CHARSET_H */
