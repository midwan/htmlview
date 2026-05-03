/***************************************************************************

 HTMLview.mcc - HTMLview MUI Custom Class
 Copyright (C) 1997-2000 Allan Odgaard
 Copyright (C) 2005-2007 by HTMLview.mcc Open Source Team

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

***************************************************************************/

#ifndef BITMAPSCALER_H
#define BITMAPSCALER_H

#ifndef BMSCALER_HOST_TEST
#  include <exec/types.h>
#else
   /* Host build: supply just the AmigaOS scalar typedefs we need.
      struct BitMap stays forward-declared. */
   typedef unsigned char  UBYTE;
   typedef unsigned short UWORD;
   typedef unsigned long  ULONG;
#  ifndef VOID
#    define VOID void
#  endif
#endif

struct BitMap;

#ifdef __cplusplus
extern "C" {
#endif

/* Compute the destination dimensions BitMapScale() will produce for a
   given (srcW,srcH)->(dstW,dstH) request.

   Exposed for the host-side unit test only — there is no production
   caller. The point is to pin the (dst ? dst : src) rounding rule so
   a future "smarter" implementation can't silently drift. */
VOID ComputeScaledExtent(UWORD srcW, UWORD srcH,
                         UWORD dstW, UWORD dstH,
                         UWORD *outW, UWORD *outH);

/* Allocate a friend-bitmap of `depth` planes holding a scaled copy of
   `src`. Returns NULL on alloc failure or zero dimensions. Caller owns
   the returned BitMap and frees it with FreeBitMap(). */
struct BitMap *ScaleBitmapTo(struct BitMap *src,
                             UWORD srcW, UWORD srcH,
                             UWORD dstW, UWORD dstH,
                             ULONG depth,
                             struct BitMap *friendBmp);

/* Scale a 1-bit mask plane (RASSIZE(srcW,srcH) bytes at srcMask) to
   dstW x dstH. Returns AllocRaster()'d storage of size
   RASSIZE(dstW,dstH); free with FreeRaster(ptr, dstW, dstH). NULL on
   failure. */
UBYTE *ScaleMaskTo(UBYTE *srcMask, UWORD srcW, UWORD srcH,
                   UWORD dstW, UWORD dstH);

#ifdef __cplusplus
}
#endif

#endif /* BITMAPSCALER_H */
