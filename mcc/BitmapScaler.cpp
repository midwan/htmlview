/***************************************************************************

 HTMLview.mcc - HTMLview MUI Custom Class
 Copyright (C) 1997-2000 Allan Odgaard
 Copyright (C) 2005-2007 by HTMLview.mcc Open Source Team

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

***************************************************************************/

#include "BitmapScaler.h"

#ifndef BMSCALER_HOST_TEST
#  include <proto/exec.h>
#  include <proto/graphics.h>
#  include <graphics/scale.h>
#  include <graphics/gfx.h>
#  include <clib/macros.h>
#endif

extern "C" VOID ComputeScaledExtent(UWORD srcW, UWORD srcH,
                                     UWORD dstW, UWORD dstH,
                                     UWORD *outW, UWORD *outH)
{
  if(outW) *outW = dstW ? dstW : srcW;
  if(outH) *outH = dstH ? dstH : srcH;
}

#ifndef BMSCALER_HOST_TEST

extern "C" struct BitMap *ScaleBitmapTo(struct BitMap *src,
                                         UWORD srcW, UWORD srcH,
                                         UWORD dstW, UWORD dstH,
                                         ULONG depth,
                                         struct BitMap *friendBmp)
{
  if(!src || !srcW || !srcH || !dstW || !dstH)
    return NULL;

  struct BitMap *dst = AllocBitMap(dstW, dstH, depth,
                                    BMF_MINPLANES | BMF_CLEAR,
                                    friendBmp);
  if(!dst)
    return NULL;

  /* BitMapScale is documented as synchronous, but BitScaleArgs has
     reserved fields that must be zero — value-initialise the whole
     struct before filling the inputs. */
  struct BitScaleArgs args = {};
  args.bsa_SrcWidth    = srcW;
  args.bsa_SrcHeight   = srcH;
  args.bsa_DestWidth   = dstW;
  args.bsa_DestHeight  = dstH;
  args.bsa_XSrcFactor  = srcW;
  args.bsa_YSrcFactor  = srcH;
  args.bsa_XDestFactor = dstW;
  args.bsa_YDestFactor = dstH;
  args.bsa_SrcBitMap   = src;
  args.bsa_DestBitMap  = dst;

  BitMapScale(&args);
  return dst;
}

extern "C" UBYTE *ScaleMaskTo(UBYTE *srcMask, UWORD srcW, UWORD srcH,
                               UWORD dstW, UWORD dstH)
{
  if(!srcMask || !srcW || !srcH || !dstW || !dstH)
    return NULL;

  /* BitMapScale on a 1-plane bitmap performs nearest-neighbour bit
     replication. Edges of the resulting mask will not be a clean
     resampling — for non-integer scale ratios the silhouette gets
     mild jitter — but for HTML <img> usage (Mastodon avatars,
     thumbnails) the result is acceptable and far better than the
     "no scaling at all" baseline. */
  struct BitMap srcBM;
  InitBitMap(&srcBM, 1, srcW, srcH);
  srcBM.Planes[0] = (PLANEPTR)srcMask;

  UBYTE *dstMask = (UBYTE *)AllocRaster(dstW, dstH);
  if(!dstMask)
    return NULL;

  struct BitMap dstBM;
  InitBitMap(&dstBM, 1, dstW, dstH);
  dstBM.Planes[0] = (PLANEPTR)dstMask;

  struct BitScaleArgs args = {};
  args.bsa_SrcWidth    = srcW;
  args.bsa_SrcHeight   = srcH;
  args.bsa_DestWidth   = dstW;
  args.bsa_DestHeight  = dstH;
  args.bsa_XSrcFactor  = srcW;
  args.bsa_YSrcFactor  = srcH;
  args.bsa_XDestFactor = dstW;
  args.bsa_YDestFactor = dstH;
  args.bsa_SrcBitMap   = &srcBM;
  args.bsa_DestBitMap  = &dstBM;

  BitMapScale(&args);
  return dstMask;
}

#endif /* !BMSCALER_HOST_TEST */
