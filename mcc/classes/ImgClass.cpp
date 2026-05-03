/***************************************************************************

 HTMLview.mcc - HTMLview MUI Custom Class
 Copyright (C) 1997-2000 Allan Odgaard
 Copyright (C) 2005-2007 by HTMLview.mcc Open Source Team

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.

 HTMLview class Support Site:  http://www.sf.net/projects/htmlview-mcc/

 $Id$

***************************************************************************/

#include "ImgClass.h"
#include "TreeClass.h"
#include "MapClass.h"
#include "BackFillClass.h"

#include "IM_Render.h"
#include "Layout.h"
#include "Map.h"
#include "MinMax.h"
#include "ParseMessage.h"
#include "ScanArgs.h"
#include "SharedData.h"
#include "BitmapScaler.h"

#include <proto/cybergraphics.h>
#include <new>

ImgClass::~ImgClass ()
{
  delete Name;
  delete AltText;
  delete Source;
  delete GivenWidth;
  delete GivenHeight;
  delete Map;
  FreeScaledBitmap();
}

VOID ImgClass::FreeScaledBitmap ()
{
  if(ScaledBMp)
  {
    WaitBlit();
    FreeBitMap(ScaledBMp);
    ScaledBMp = NULL;
  }
  if(ScaledMask)
  {
    FreeRaster(ScaledMask, ScaledW, ScaledH);
    ScaledMask = NULL;
  }
  ScaledW = ScaledH = 0;
}

VOID ImgClass::BuildScaledBitmap (struct PictureFrame *pic)
{
  if(!pic || !pic->BMp || !GivenWidth || !GivenHeight)
    return;

  /* Defer until the picture is fully decoded — scaling an empty or
     half-populated bitmap would just snapshot whatever zeroes/garbage
     the decoder hasn't filled in yet. The cache-hit path always
     enters here with PicFLG_Full set; the streaming-decode path
     reaches it on the final UpdateImage. */
  if(!(pic->Flags & PicFLG_Full))
    return;

  LONG reqW = GivenWidth->Size;
  LONG reqH = GivenHeight->Size;

  /* Skip when nothing to do (no HTML override, or it already matches
     the picture's native size, or the scaled copy is already current).
     The renderer falls through to the cache-shared native bitmap
     for the first two cases. */
  if(reqW <= 0 || reqH <= 0)
    return;
  if(reqW == (LONG)pic->Width && reqH == (LONG)pic->Height)
    return;
  if(ScaledBMp && ScaledW == (UWORD)reqW && ScaledH == (UWORD)reqH)
    return;

  FreeScaledBitmap();

  /* Scale colour and mask atomically: if either fails on a picture
     that has a mask, drop both and leave the renderer to fall back
     to the native bitmap. Mixing a scaled BMp with a native-size
     mask would produce undefined reads in BltMaskRPort. */
  ULONG depth = GetBitMapAttr(pic->BMp, BMA_DEPTH);
  struct BitMap *newBMp = ScaleBitmapTo(pic->BMp,
                                         (UWORD)pic->Width, (UWORD)pic->Height,
                                         (UWORD)reqW, (UWORD)reqH,
                                         depth, pic->BMp);
  if(!newBMp)
    return;

  UBYTE *newMask = NULL;
  if(pic->Mask)
  {
    newMask = ScaleMaskTo(pic->Mask,
                          (UWORD)pic->Width, (UWORD)pic->Height,
                          (UWORD)reqW, (UWORD)reqH);
    if(!newMask)
    {
      WaitBlit();
      FreeBitMap(newBMp);
      return;
    }
  }

  ScaledBMp  = newBMp;
  ScaledMask = newMask;
  ScaledW    = (UWORD)reqW;
  ScaledH    = (UWORD)reqH;
}

VOID ImgClass::FreeColours(struct ColorMap *cmap UNUSED)
{
  if(Picture)
    Picture->UnLockPicture();
  Picture = NULL;

  if(BlendBitMap)
  {
    WaitBlit();
    FreeBitMap(BlendBitMap);
    BlendBitMap = NULL;
  }

  /* Drop any per-instance scaled copy too — it references the same
     screen, and a rebuilt page will rebuild it from the new Picture. */
  FreeScaledBitmap();
}

VOID ImgClass::GetImages (struct GetImagesMessage &gmsg)
{
  if(Source && !Picture)
  {
    STRPTR url;
    if((url = (STRPTR)DoMethod(gmsg.HTMLview, MUIM_HTMLview_AddPart, (ULONG)Source)))
    {
      /* Cache and in-flight queue dedupe by URL only — the per-instance
         scaled bitmap (built in ReceiveImage) handles divergent
         width/height. Two <img> tags pointing at the same src thus
         share one network fetch and one decode regardless of size. */
      gmsg.AddImage(url, 0, 0, this);
    }
  }
}

BOOL ImgClass::HitTest (struct HitTestMessage &hmsg)
{
  if(!(Flags & FLG_Layouted))
    return(FALSE);

  BOOL result = FALSE;
  LONG top = Top+VSpace+ImgBorder;
   LONG left = Left+HSpace+ImgBorder;

  if(hmsg.X >= left && hmsg.X < left+Width() && hmsg.Y >= top && hmsg.Y < top+Height())
  {
    hmsg.ImgSrc = Source;
    hmsg.Img = this;
    hmsg.OffsetX = hmsg.X - left;
    hmsg.OffsetY = hmsg.Y - top;

    if(Map)
    {
      if(!MapObj)
        MapObj = hmsg.Host->FindMap(Map);

      if(MapObj)
      {
        struct UseMapMessage umsg(hmsg.X - left, hmsg.Y - top);
        if((result = MapObj->UseMap(umsg)))
        {
          hmsg.Obj = this;
          hmsg.URL = umsg.URL;
          hmsg.Target = umsg.Target;
        }
      }
    }
    else
    {
      if((result = hmsg.Obj ? TRUE : FALSE))
      {
        if(Flags & FLG_Img_IsMap)
        {
          hmsg.ServerMap = TRUE;
        }
        else
        {
          if(Picture && Picture->Mask)
          {
            UBYTE *srcline = Picture->Mask + RASSIZE(Picture->Width, hmsg.Y-top);
            LONG x = hmsg.X-left;
            if(!(result = TestPixel(srcline, x)))
            {
              BOOL left = FALSE, right = FALSE;
              for(LONG lx = x-1; lx > x-5 && lx > 0; lx--)
                left |= TestPixel(srcline, lx);
              for(LONG rx = x+1; rx < x+5 && rx < Picture->Width; rx++)
                right |= TestPixel(srcline, rx);
              result = left && right;
            }
          }
        }
      }
    }
  }
  return(result);
}

VOID ImgClass::Relayout (BOOL all)
{
  if((GivenWidth && GivenWidth->Type == Size_Percent) || (GivenHeight && GivenHeight->Type == Size_Percent))
  {
    /* This will free any scaled bitmaps */
    FreeColours(NULL);
  }
  SuperClass::Relayout(all);
}

LONG ImgClass::Width (LONG def_w, struct LayoutMessage *lmsg)
{
  if(GivenWidth)
  {
    switch(GivenWidth->Type)
    {
      case Size_Percent:
      {
        if(lmsg)
        {
          GivenWidth->Type = Size_Pixels;
          GivenWidth->Size = (lmsg->ScrWidth() * GivenWidth->Size) / 100;
        }
      }
      /* continue... */

      case Size_Pixels:
        def_w = GivenWidth->Size;
      break;
    }
  }
  return(def_w);
}

LONG ImgClass::Height (LONG def_h, struct LayoutMessage *lmsg)
{
  if(GivenHeight)
  {
    switch(GivenHeight->Type)
    {
      case Size_Percent:
      {
        if(lmsg)
        {
          GivenHeight->Type = Size_Pixels;
          GivenHeight->Size = (lmsg->ScrHeight * GivenHeight->Size) / 100;
        }
      }
      /* continue... */

      case Size_Pixels:
        def_h = GivenHeight->Size;
      break;
    }
  }
  return(def_h);
}

BOOL ImgClass::Layout (struct LayoutMessage &lmsg)
{
  LONG width = Width(80, &lmsg);
  LONG height = Height(20, &lmsg);

  STRPTR url;
  if(!Picture && Source && (url = (STRPTR)DoMethod(lmsg.HTMLview, MUIM_HTMLview_AddPart, (ULONG)Source)))
  {
    /* URL-only lookup: ImgClass owns scaling, so the cache entry is
       always the native-sized picture shared across instances. */
    if((Picture = lmsg.Share->ImageStorage->FindImage(url, 0, 0)))
    {
      ReceiveImage(Picture);
      /* Width()/Height() return the HTML-requested dims if set, else
         the native size that ReceiveImage just stamped in. */
      width  = Width(80, &lmsg);
      height = Height(20, &lmsg);
      UpdateImage(0, height, 0, 0, TRUE);

      if(Picture->Next)
        DoMethod(lmsg.HTMLview, MUIM_HTMLview_AddSingleAnim, (ULONG)Picture, (ULONG)this);
    }
    delete url;
  }
  else
  {
    if(Picture && (Picture->Flags & PicFLG_Complete) && Picture->AlphaMask)
      Flags |= FLG_Img_CreateAlpha;
  }

  width += 2*(ImgBorder+HSpace);
  height += 2*(ImgBorder+VSpace);


  if(lmsg.X + width-1 > lmsg.MaxX)
    lmsg.Newline();
  else if(width > lmsg.ScrWidth())
    lmsg.EnsureNewline();

  if(Alignment == Align_Left || Alignment == Align_Right)
  {
    lmsg.EnsureNewline();

    Top = lmsg.Y;
    Bottom = lmsg.Y + height - 1;

    struct FloadingImage *img = new (std::nothrow) struct FloadingImage(Top, Left, width, height, this, lmsg.Parent);
    if (!img) return FALSE;
    Left = lmsg.AddImage(img, Alignment == Align_Right);

    lmsg.TopChange = MIN(lmsg.TopChange, Top);
  }
  else
  {
    Left = lmsg.X;
    lmsg.X += width;

    Top = lmsg.Y - (height-1);
    Bottom = lmsg.Y;

    LONG baseline;
    switch(Alignment)
    {
      case Align_Top:
      {
        Top = lmsg.Y;
        Bottom = lmsg.Y + (height-1);
        lmsg.SetLineHeight(height);
      }
      break;

      case Align_Middle:
      {
        baseline = height/2;
        LONG offset = (height - baseline)-1;
        Top += offset;
        Bottom += offset;
        lmsg.UpdateBaseline(height, baseline);
      }
      break;

      default:
      {
        baseline = height-1;
        lmsg.UpdateBaseline(height, baseline);
      }
      break;
    }

    struct ObjectNotify *notify = new (std::nothrow) struct ObjectNotify(Left, Baseline, this);
    if (!notify) return FALSE;
    lmsg.AddNotify(notify);
  }
  Flags |= FLG_WaitingForSize;

   return TRUE;
}

VOID ImgClass::AdjustPosition (LONG x, LONG y)
{
  Left += x;
  SuperClass::AdjustPosition(x, y);
}

VOID ImgClass::MinMax (struct MinMaxMessage &mmsg)
{
  struct LayoutMessage *lmsg = mmsg.LMsg;
  LONG width = Width(80, lmsg);

  STRPTR url;
  if(!Picture && Source && (url = (STRPTR)DoMethod(lmsg->HTMLview, MUIM_HTMLview_AddPart, (ULONG)Source)))
  {
    /* URL-only lookup; see Layout() above for rationale. */
    if((Picture = lmsg->Share->ImageStorage->FindImage(url, 0, 0)))
    {
      ReceiveImage(Picture);
      width = Width(80, lmsg);
      UpdateImage(0, Picture->Height, 0, 0, TRUE);

      if(Picture->Next)
        DoMethod(lmsg->HTMLview, MUIM_HTMLview_AddSingleAnim, (ULONG)Picture, (ULONG)this);
    }
    delete url;
  }

  width += 2*(HSpace+ImgBorder);

  if(Alignment == Align_Left)
    width += 5;

  mmsg.Min = MAX(width, mmsg.Min);
  mmsg.X += width;

  Flags |= FLG_KnowsMinMax;
}

VOID ImgClass::Parse(struct ParseMessage &pmsg)
{
  AttrClass::Parse(pmsg);

  BOOL ismap = FALSE;
  struct ArgList args[] =
  {
    { "ALT",    &AltText,     ARG_STRING,   NULL },
    { "SRC",    &Source,      ARG_URL,      NULL },
    { "NAME",   &Name,        ARG_URL,      NULL },
    { "WIDTH",  &GivenWidth,  ARG_VALUE,    NULL },
    { "HEIGHT", &GivenHeight, ARG_VALUE,    NULL },
    { "BORDER", &ImgBorder,   ARG_NUMBER,   NULL },
    { "ALIGN",  &Alignment,   ARG_KEYWORD,  ImgAlignKeywords },
    { "HSPACE", &HSpace,      ARG_NUMBER,   NULL },
    { "VSPACE", &VSpace,      ARG_NUMBER,   NULL },
    { "USEMAP", &Map,         ARG_URL,      NULL },
    { "ISMAP",  &ismap,       ARG_SWITCH,   NULL },
    { NULL,     NULL,         0,            NULL }
  };
  ImgBorder = pmsg.Linkable;
  Alignment = (ULONG)-1;
  ScanArgs(pmsg.Locked, args);
  Alignment++;
  if(Alignment == Align_Center)
    Alignment = Align_None;
  if(ismap)
    Flags |= FLG_Img_IsMap;
}

BOOL ImgClass::ReceiveImage (struct PictureFrame *pic)
{
  BOOL relayout = FALSE;
  Picture = pic;
  pic->LockPicture();
  YStart = YStop = 0;

  if(!GivenWidth)
  {
    GivenWidth = new (std::nothrow) struct ArgSize(pic->Width, Size_Pixels);
    if (!GivenWidth) return FALSE;
    relayout = TRUE;
  }

  if(!GivenHeight)
  {
    GivenHeight = new (std::nothrow) struct ArgSize(pic->Height, Size_Pixels);
    if (!GivenHeight) return FALSE;
    relayout = TRUE;
  }

  /* Scaling is built lazily on first Render — see BuildScaledBitmap.
     Doing it here would snapshot the bitmap before the decoder has
     finished populating it for the streaming path. */

  return(relayout);
}

BOOL ImgClass::UpdateImage (LONG ystart, LONG ystop, LONG top, LONG bottom, BOOL last)
{
  if(!Picture)
    return(FALSE);

  if(last && Picture->AlphaMask && CyberGfxBase)
  {
    ystart = 0;
    Flags |= FLG_Img_CreateAlpha;
  }

  BOOL redraw;
  LONG pic_top = Top+ystart;
  if((redraw = (pic_top <= bottom && pic_top+(ystop-ystart) >= top)))
  {
    YStart = ystart;
    YStop = ystop;
  }
  else
  {
    YStart = 0;
    YStop = (Picture->Flags & PicFLG_Full) ? Picture->Height : ystop;
  }

  return(redraw);
}

BOOL ImgClass::FlushImage (LONG top, LONG bottom)
{
  if(Picture)
  {
    YStop = 0;
    Picture->UnLockPicture();
    Picture = NULL;
    Flags |= FLG_Img_DrawBackground;
    return(Top <= bottom && Bottom >= top);
  }
  return(FALSE);
}

VOID ImgClass::Render (struct RenderMessage &rmsg)
{
  struct RastPort *rp = rmsg.RPort;
  LONG x1 = Left+HSpace-rmsg.OffsetX, y1 = Top+VSpace-rmsg.OffsetY;
  LONG width = Width(), height = Height();

  if(!width || !height)
    return;

  if(ImgBorder)
  {
    LONG x2 = x1+width+(2*ImgBorder)-1, y2 = y1+height+(2*ImgBorder)-1;

    ULONG border = ImgBorder-1;
    SetAPen(rp, rmsg.Colours[(rmsg.Textstyles & TSF_ALink) ? Col_ALink : ((rmsg.Textstyles & TSF_Link) ? ((rmsg.Textstyles & TSF_VLink) ? Col_VLink : Col_Link) : Col_Text)]);

    RectFill(rp, x1+ImgBorder, y1, x2, y1+border);
    RectFill(rp, x2-border, y1+ImgBorder, x2, y2);
    RectFill(rp, x1, y2-border, x2-ImgBorder, y2);
    RectFill(rp, x1, y1, x1+border, y2-ImgBorder);

    x1 += ImgBorder;
    y1 += ImgBorder;
  }

  if(rmsg.RedrawLink)
    return;

  LONG x2 = x1+width-1, y2 = y1+height-1;
  if(Picture)
  {
    /* Lazy-build the per-instance scaled copy if needed. Idempotent:
       early-returns when the picture is incomplete, when no scaling
       is asked for, or when the existing scaled copy already matches
       the requested dimensions. */
    BuildScaledBitmap(Picture);

    /* Default source = the cache-shared native bitmap with the
       progressive-decode YStart/YStop window. When a per-instance
       scaled copy exists, override both: the scaled bitmap is always
       complete and its extent matches the rendered rectangle. The
       atomic-allocation path in BuildScaledBitmap guarantees that
       ScaledBMp and ScaledMask are either both set (with matching
       dimensions) or both NULL — never mixed with the native pair. */
    struct BitMap *srcBmp  = Picture->BMp;
    UBYTE         *srcMask = Picture->Mask;
    LONG src_y       = YStart;
    LONG pass_height = YStop-YStart;
    if(ScaledBMp)
    {
      srcBmp      = ScaledBMp;
      srcMask     = ScaledMask;
      src_y       = 0;
      pass_height = height;
    }

    if(srcMask)
    {
      struct RastPort *tmprp;
      if(rmsg.TargetObj == (class SuperClass *)this && (tmprp = rmsg.ObtainDoubleBuffer(width, pass_height)))
      {
        LONG  xoffset = rmsg.Left + x1 - rmsg.MinX,
            yoffset = rmsg.Top  + y1 - rmsg.MinY;

        rmsg.RPort = tmprp;
        rmsg.BackgroundObj->DrawBackground(rmsg, 0, 0, width-1, pass_height-1, xoffset, yoffset+src_y);
        BltMaskRPort(srcBmp, 0, src_y, tmprp, 0, 0, width, pass_height, srcMask);
        BltBitMapRastPort(tmprp->BitMap, 0, 0, rp, x1, y1+src_y, width, pass_height, 0x0c0);
        rmsg.RPort = rp;
      }
      else
      {
        BltMaskRPort(srcBmp, 0, src_y, rp, x1, y1+src_y, width, pass_height, srcMask);
      }
    }
    else
    {
      if(Flags & FLG_Img_CreateAlpha && rmsg.BackgroundObj->ReadyForAlpha())
      {
        struct RastPort *tmprp, srcrport, dstrport;

        if(BlendBitMap)
        {
          WaitBlit();
          FreeBitMap(BlendBitMap);
        }
        BlendBitMap = AllocBitMap(width, height, GetBitMapAttr(Picture->BMp, BMA_DEPTH), BMF_MINPLANES, Picture->BMp);

        if(BlendBitMap && (tmprp = rmsg.ObtainDoubleBuffer(width, pass_height)))
        {
          LONG  xoffset = rmsg.Left + x1 - rmsg.MinX,
              yoffset = rmsg.Top  + y1 - rmsg.MinY;

          rmsg.RPort = tmprp;

          struct RGBPixel *mixline;
          mixline = new (std::nothrow) struct RGBPixel [2*width];
          if (mixline)
		  {
            InitRastPort(&srcrport);
            srcrport.BitMap = Picture->BMp;
            InitRastPort(&dstrport);
            dstrport.BitMap = BlendBitMap;

            UBYTE *alpha = Picture->AlphaMask;
            for(UWORD y = 0; y < height; y++)
            {
              rmsg.BackgroundObj->DrawBackground(rmsg, 0, 0, width-1, 0, xoffset, yoffset+y);
              ReadPixelArray(mixline, 0, 0, width*sizeof(RGBPixel), &srcrport, 0, y, width, 1, PIXEL_FORMAT);
              ReadPixelArray(mixline, 0, 1, width*sizeof(RGBPixel), tmprp, 0, 0, width, 1, PIXEL_FORMAT);

              for(UWORD x = 0; x < width; x++)
              {
                UBYTE factor = *alpha++;
                mixline[x].SetRGB(mixline[x].Scale(factor) + mixline[x+width].Scale(255-factor));
              }

              WritePixelArray(mixline, 0, 0, width*sizeof(RGBPixel), &dstrport, 0, y, width, 1, PIXEL_FORMAT);
            }

            delete mixline;
            rmsg.RPort = rp;
          }
        }
        Flags &= ~FLG_Img_CreateAlpha;
      }

      struct BitMap *bmp = BlendBitMap ? BlendBitMap : srcBmp;
      BltBitMapRastPort(bmp, 0, src_y, rp, x1, y1+src_y, width, pass_height, 0x0c0);
    }

    YStart = 0;
    if(Picture->Flags & PicFLG_Full)
      YStop = height;
    y1 += YStop;
  }
  else if(Flags & FLG_Img_DrawBackground)
  {
    LONG  xoffset = rmsg.Left + x1 - rmsg.MinX,
        yoffset = rmsg.Top  + y1 - rmsg.MinY;

    rmsg.BackgroundObj->DrawBackground(rmsg, x1, y1, x1+width-1, y1+height-1, xoffset, yoffset);
    Flags &= ~FLG_Img_DrawBackground;
  }

  LONG col1, col2;
  if(rmsg.Textstyles & TSF_Link)
  {
    col1 = rmsg.Colours[Col_Halfshadow];
    col2 = rmsg.Colours[Col_Halfshine];
  }
  else
  {
    col1 = rmsg.Colours[Col_Halfshine];
    col2 = rmsg.Colours[Col_Halfshadow];
  }

  if(width > 1 && y2 >= y1)
  {
    SetAPen(rp, col1);
    RectFill(rp, x1, y2, x2, y2);
    RectFill(rp, x2, y1, x2, y2);
    SetAPen(rp, col2);
    if(YStop == 0)
      RectFill(rp, x1, y1, x2, y1);
    RectFill(rp, x1, y1, x1, y2);
  }

  if(!Picture)
  {
    STRPTR alt = AltText ? AltText : (STRPTR)"[Image]";
    ULONG length = strlen(alt);
//    for(UWORD i = AltText ? Font_H1 : Font_H6; i <= Font_H6; i++)
    for(UWORD i = Font_H5; i <= Font_H6; i++)
    {
      UWORD pixels = MyTextLength(rmsg.Share->Fonts[i], alt, length);

      if((pixels+2) <= width-4 && (rmsg.Share->Fonts[i]->tf_YSize+2) <= height-4)
      {
        LONG startx = x1 + (width-(pixels+2))/2;
        LONG starty = y1 + ((height-(rmsg.Share->Fonts[i]->tf_YSize+2))/2);
        SetAPen(rp, rmsg.Colours[Col_Text]);
        SetFont(rp, rmsg.Share->Fonts[i]);
        SetSoftStyle(rp, 0L, TSF_StyleMask);
        Move(rp, startx, starty+rp->TxBaseline);
        Text(rp, alt, length);

        break;
      }
    }
  }
}

BOOL ImgClass::TestPixel(UBYTE *line, LONG x)
{
  return(line[x >> 3] & (1 << (x%7)));
}

