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

#include <string.h>
#include <ctype.h>
#include <stdio.h>

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/cybergraphics.h>
#include <proto/dos.h>
#include <dos/dostags.h>
#include <proto/utility.h>

#include <clib/macros.h>

#include "mcc_common.h"

#include "IM_Render.h"
#include "IM_Output.h"
#include "ImageDecoder.h"
#include "Animation.h"
#include "SharedData.h"

#include <proto/datatypes.h>
#include <datatypes/datatypes.h>
#include <datatypes/pictureclass.h>
#include <datatypes/datatypesclass.h>
#include <intuition/gadgetclass.h>

#ifndef PMODE_V43
#define PMODE_V43 1
#endif
#ifndef MODE_V43
#define MODE_V43 PMODE_V43
#endif
#ifndef PDTA_MaskPlane
#define PDTA_MaskPlane (DTA_Dummy + 258)
#endif
#ifndef PDTA_Mask
#define PDTA_Mask PDTA_MaskPlane
#endif

#include "classes/HostClass.h"
#include "classes/ImgClass.h"
#include "classes/SuperClass.h"

#include <stdio.h>
#include <new>
#include "private.h"

#if defined(__amigaos4__)
/* On OS4 DeleteFile is deprecated, use Delete instead.
   The SDK will then map Delete to IDOS->Delete. */
#define DeleteFile Delete
#endif

#if defined(__amigaos3__)
#define GetImageDecoderClass(base) (struct IClass *)DoMethod(base, ET_Offset, -30, ET_RegisterA6, base, ET_SaveRegs, TRUE, TAG_DONE)
#elif defined(__MORPHOS__)
#define GetImageDecoderClass(base) (struct IClass *)({ REG_A6 = (ULONG) (base); MyEmulHandle->EmulCallDirectOS(-30); })
#elif defined(__cplusplus)
#define GetImageDecoderClass(base) NULL
#else
#define GetImageDecoderClass(base) ( (struct IClass *(*)(REG(a6, struct Library *))) ((base)->lp_VTable[-5]) ) (base)
#endif

extern struct Library *CyberGfxBase;
extern struct Library *DataTypesBase;

#if defined(__amigaos4__)
extern struct DataTypesIFace *IDataTypes;
extern struct DOSIFace *IDOS;
#endif

struct SignalSemaphore ImageMutex;

CONSTRUCTOR(MutexInit, 5)
{
  memset(&ImageMutex, 0, sizeof(ImageMutex));
  InitSemaphore(&ImageMutex);
}

ImageCacheItem::ImageCacheItem (STRPTR url, struct PictureFrame *pic)
{
  URL = new (std::nothrow) char[strlen(url)+1];
  if (URL)
  {
	  strcpy(URL, url);
	  Picture = pic;
	  pic->LockPicture();
  }
}

ImageCacheItem::~ImageCacheItem ()
{
  Picture->UnLockPicture();
  delete[] URL;
}

ImageCache::ImageCache (ULONG maxsize)
{
  MaxSize = maxsize;
  LastEntry = (struct ImageCacheItem *)&FirstEntry;
}

ImageCache::~ImageCache ()
{
  FlushCache();
}

VOID ImageCache::AddImage (STRPTR url, struct PictureFrame *pic)
{
  ObtainSemaphore(&ImageMutex);
  struct ImageCacheItem *item = new (std::nothrow) struct ImageCacheItem (url, pic);
  if (item)
  {
    LastEntry = (LastEntry->Next = item);
    CurrentSize += pic->Size();

    struct ImageCacheItem *preprev = NULL, *prev, *first = FirstEntry;
    while(CurrentSize > MaxSize && first)
    {
      prev = first;
      first = first->Next;

      if(prev->Picture->LockCnt == 1)
      {
        if(prev == FirstEntry)
          if(!(FirstEntry = first))
            LastEntry = (struct ImageCacheItem *)&FirstEntry;

        CurrentSize -= prev->Picture->Size();
        delete prev;

        if(preprev)
          preprev->Next = first;
      }
      else
      {
        preprev = prev;
      }
    }
  }
  ReleaseSemaphore(&ImageMutex);
}

struct PictureFrame *ImageCache::FindImage (STRPTR url, ULONG width, ULONG height)
{
  ObtainSemaphore(&ImageMutex);
  struct ImageCacheItem *preprev = NULL, *prev, *first = FirstEntry;
  while(first)
  {
    prev = first;
    first = first->Next;

    if(!strcmp(url, prev->URL) && prev->Picture->MatchSize(width, height))
    {
      if(prev != LastEntry)
      {
        if(prev == FirstEntry)
            FirstEntry = first;
        else  preprev->Next = first;

        LastEntry = (LastEntry->Next = prev);
        prev->Next = NULL;
      }
      ReleaseSemaphore(&ImageMutex);
      return(prev->Picture);
    }
    preprev = prev;
  }
  ReleaseSemaphore(&ImageMutex);
  return(NULL);
}

static BOOL Match (STRPTR url, struct ImageCacheItem *item)
{
  BOOL res;
  switch((ULONG)url)
  {
    case (ULONG)MUIV_HTMLview_FlushImage_All:
      res = TRUE;
    break;

    case (ULONG)MUIV_HTMLview_FlushImage_Displayed:
      res = item->Picture->LockCnt == 1;
    break;

    default:
      res = !strcmp(url, item->URL);
    break;
  }
  return(res);
}

VOID ImageCache::FlushCache (STRPTR url)
{
  ObtainSemaphore(&ImageMutex);
  struct ImageCacheItem *prev, *preprev = NULL, *first = FirstEntry;
  while(first)
  {
    prev = first;
    first = first->Next;

    if(Match(url, prev))
    {
      if(prev == FirstEntry)
        if(!(FirstEntry = first))
          LastEntry = (struct ImageCacheItem *)&FirstEntry;

      CurrentSize -= prev->Picture->Size();
      delete prev;

      if(preprev)
        preprev->Next = first;
    }
    else
    {
      preprev = prev;
    }
  }

  if(!(LastEntry = preprev))
    LastEntry = (struct ImageCacheItem *)&FirstEntry;
  ReleaseSemaphore(&ImageMutex);
}

DecodeItem::DecodeItem (Object *obj, struct HTMLviewData *data, struct ImageList *image)
{
  Obj = obj;
  Data = data;
  Images = image;
  PageID = data->PageID;
  Thread = FindTask(NULL);
  InitSemaphore(&Mutex);
}

VOID DecodeItem::Start (struct PictureFrame *pic)
{
  Enter();
  Picture = pic;
  pic->LockPicture();
  Status = StatusDecoding;
  Leave();
}

BOOL DecodeItem::Update ()
{
  Enter();
  BOOL result = Status == StatusPending || Status == StatusDecoding;
  BOOL rem = Status == StatusDone || Status == StatusError;
  LONG y = CurrentY;
  ULONG pass = CurrentPass;
  BOOL done = Picture && Picture->Next;
  Leave();

  if(done)
  {
    done = LastY == (y = Picture->Height), pass = LastPass;

    if(!Anim && !Abort && PageID == Data->PageID)
    {
      Anim = Data->Share->AddAnim(Obj, Data, Picture, Images->Objects);
      Anim->Decode = this;
    }
  }

  if(Picture && !Abort && PageID == Data->PageID && !done)
  {
    if(rem)
    {
      y = Picture->Height;
      Picture->Flags |= PicFLG_Complete;
    }
    if(pass > 0 || y == Picture->Height)
      Picture->Flags |= PicFLG_Full;

    if(!Started)
    {
      Started = TRUE;

      BOOL relayout = FALSE;
      struct ObjectList *first = Images->Objects;
      while(first)
      {
        if(first->Obj->ReceiveImage(Picture))
          relayout = TRUE;
        first = first->Next;
      }

      if(relayout)
      {
        Data->Flags |= FLG_NotResized;
        if(DoMethod(Obj, MUIM_Group_InitChange))
        {
          Data->LayoutMsg.Reset(Data->Width, Data->Height);
          Data->HostObject->Relayout(TRUE);
          DoMethod(Obj, MUIM_Group_ExitChange);
        }
        Data->Flags &= ~FLG_NotResized;
      }
    }

    if(Picture->BMp)
    {
      LONG srcy = LastY;
      LastY = y;

      if(pass != LastPass)
      {
        if(y == Picture->Height)
            srcy = 0;
        else  LastY = 0, y = Picture->Height;
      }

      LONG top = Data->Top, bottom = Data->Top+Data->Height-1;

      struct ObjectList *first = Images->Objects;
      while(first)
      {
        if(y > srcy || first->Obj->id() != tag_IMG)
        {
          if(first->Obj->UpdateImage(srcy, y, top, bottom, rem))
          {
            Data->RedrawObj = first->Obj;
            MUI_Redraw(Obj, MADF_DRAWUPDATE);

            if(first->Obj->id() == tag_IMG)
            {
              class ImgClass *img = (class ImgClass *)first->Obj;
              img->YStart = 0;
              if(Picture->Flags & PicFLG_Full)
                img->YStop = Picture->Height;
            }
          }
        }
        first = first->Next;
      }
      LastPass = pass;
    }
  }

  if(rem)
  {
    if(Picture)
    {
      if(!Abort)
        Data->Share->ImageStorage->AddImage(Images->ImageName, Picture);
      Picture->UnLockPicture();
    }
    DecodeQueue->RemoveElm(this);
  }
  return(result);
}

DecodeQueueManager::~DecodeQueueManager ()
{
}
VOID DecodeQueueManager::InsertElm (struct DecodeItem *item)
{
  ObtainSemaphore(&Mutex);
  if(Queue)
  {
    item->Next = Queue;
    Queue->Prev = item;
  }
  Queue = item;
  item->DecodeQueue = this;
  ReleaseSemaphore(&Mutex);
}

VOID DecodeQueueManager::RemoveElm (struct DecodeItem *item)
{
  ObtainSemaphore(&Mutex);
  if(item->Prev)
      item->Prev->Next = item->Next;
  else  Queue = item->Next;

  if(item->Next)
    item->Next->Prev = item->Prev;
  ReleaseSemaphore(&Mutex);

  if(item->Anim)
    item->Anim->Decode = NULL;

  delete item;
}

ULONG DecodeQueueManager::DumpQueue ()
{
  ULONG total = 0;
  ObtainSemaphore(&Mutex);
  struct DecodeItem *prev, *first = Queue;
  while(first)
  {
    prev = first;
    first = first->Next;
    if(prev->Update())
    {
      total++;
    }
  }
  ReleaseSemaphore(&Mutex);
  return(total);
}

VOID DecodeQueueManager::InvalidateQueue (Object *obj)
{
  ObtainSemaphore(&Mutex);
  struct DecodeItem *first = Queue;
  while(first)
  {
    if(first->Obj == obj || !obj)
    {
      first->Enter();
      first->Abort = TRUE;
      if(first->Thread)
        Signal(first->Thread, SIGBREAKF_CTRL_C);
      first->Leave();
    }
    first = first->Next;
  }
  ReleaseSemaphore(&Mutex);
}

struct Args
{
  Args (Object *obj, struct HTMLviewData *data, struct ImageList *image, struct Screen *scr, LONG sigbit, struct Task *parenttask)
  {
    TaskName = new (std::nothrow) char[strlen(image->ImageName)+12];
    if (TaskName)
    {
    	sprintf(TaskName, "HTMLview - %s", image->ImageName);
	    Name = TaskName + 11;
    }

    Obj = obj;
    App = (Object *)DoMethod(obj, MUIM_GetConfigItem, MUIC_Application, 0);
    Data = data;
    Img = image;
    Scr = scr;
    SigBit = sigbit;
    ParentTask = parenttask;
    MainSigBit = data->SigBit;
  }

  ~Args ()
  {
    UnlockPubScreen(NULL, Scr);
    delete TaskName;
  }

  Object *Obj, *App;
  STRPTR TaskName, Name;
  struct ImageList *Img;
  struct Screen *Scr;
  struct HTMLviewData *Data;
  LONG SigBit, MainSigBit;
  struct Task *ParentTask;
};

struct DecoderThreadStartupMessage
{
  struct Message message;
  struct Args *args;
};

/* Accumulate diagnostic output in a ring-style buffer and rewrite
   T:htmlview_dt.log on each call. Avoids seek/append portability issues
   across OS3/OS4/MorphOS. */
static char   DTLogBuf[8192];
static ULONG  DTLogLen = 0;

static void DTLog(const char *line)
{
    ULONG need = strlen(line) + 1;
    if (DTLogLen + need + 1 >= sizeof(DTLogBuf))
        return; /* silently drop if overflow */
    memcpy(DTLogBuf + DTLogLen, line, need - 1);
    DTLogLen += need - 1;
    DTLogBuf[DTLogLen++] = '\n';
    DTLogBuf[DTLogLen]   = 0;

    BPTR f = Open((STRPTR)"T:htmlview_dt.log", MODE_NEWFILE);
    if (f)
    {
        Write(f, DTLogBuf, DTLogLen);
        Close(f);
    }
}

extern "C" void DecoderThread(void)
{
  struct Process *me = (struct Process *)FindTask(NULL);
  struct DecoderThreadStartupMessage *startup;

  WaitPort(&me->pr_MsgPort);
  if((startup = (struct DecoderThreadStartupMessage *)GetMsg(&me->pr_MsgPort)) != NULL)
  {
    struct Args *args = startup->args;
    struct Task *parent = args->ParentTask;
    ULONG sigbit = args->SigBit;

    ReplyMsg((struct Message *)startup);

    BOOL result = FALSE;
    struct ImageList *image = args->Img;
    struct Hook *loadhook = args->Data->ImageLoadHook;
    struct HTMLview_LoadMsg loadmsg;
    loadmsg.lm_App = args->App;

    {
        char logbuf[256];
        sprintf(logbuf, "thread start url=%s", args->Name);
        DTLog(logbuf);
    }
    D(DBF_STARTUP, "DecoderThread: started for %s", args->Name);

    struct DecodeItem *item = new (std::nothrow) struct DecodeItem(args->Obj, args->Data, image);
    if (item)
    {
      args->Data->Share->DecodeQueue.InsertElm(item);
      Signal(parent, 1 << sigbit);

      loadmsg.lm_Type = HTMLview_Open;
      loadmsg.lm_PageID = item->PageID;
      loadmsg.lm_Params.lm_Open.URL = args->Name;
      loadmsg.lm_Params.lm_Open.Flags = MUIF_HTMLview_LoadMsg_Image;
      
      ULONG openres = CallHookPkt(loadhook, args->Obj, &loadmsg);
      {
          char logbuf[64];
          sprintf(logbuf, "  hook-open => %lu", openres);
          DTLog(logbuf);
      }
      if(openres)
      {
        D(DBF_STARTUP, "DecoderThread: hook open success");
        char tmpname[64];
        STRPTR ext = (STRPTR)"";
        STRPTR url = args->Name;
        if (strstr(url, ".png") || strstr(url, ".PNG")) ext = (STRPTR)".png";
        else if (strstr(url, ".gif") || strstr(url, ".GIF")) ext = (STRPTR)".gif";
        else if (strstr(url, ".jpg") || strstr(url, ".JPG") || strstr(url, ".jpeg")) ext = (STRPTR)".jpg";
        
        sprintf(tmpname, "T:hv_%lx_%lx%s", (ULONG)item->PageID, (ULONG)FindTask(NULL), ext);
        BPTR tmpf = Open(tmpname, MODE_NEWFILE);
        {
            char logbuf[128];
            sprintf(logbuf, "  open(%s, NEWFILE) => %lx", tmpname, (ULONG)tmpf);
            DTLog(logbuf);
        }
        if (tmpf)
        {
            char *buf = new (std::nothrow) char[8192];
            if (buf)
            {
                LONG rd;
                ULONG total = 0;
                loadmsg.lm_Type = HTMLview_Read;
                loadmsg.lm_Params.lm_Read.Buffer = buf;
                loadmsg.lm_Params.lm_Read.Size = 8192;
                
                while ((rd = CallHookPkt(loadhook, args->Obj, &loadmsg)) > 0)
                {
                    Write(tmpf, buf, rd);
                    total += rd;
                    if (item->Abort) break;
                }
                delete[] buf;
                {
                    char logbuf[64];
                    sprintf(logbuf, "  recv %lu bytes", total);
                    DTLog(logbuf);
                }
                D(DBF_STARTUP, "DecoderThread: wrote %lu bytes to %s", total, tmpname);
            }
            Close(tmpf);
            
            if (!item->Abort)
            {
                Object *dt = NewDTObject(tmpname,
                    DTA_GroupID,       GID_PICTURE,
                    DTA_SourceType,    DTST_FILE,
                    PDTA_DestMode,     PMODE_V43,
                    PDTA_Remap,        TRUE,
                    PDTA_Screen,       args->Scr,
                    PDTA_UseFriendBitMap, TRUE,
                    TAG_DONE);

                {
                    char logbuf[128];
                    sprintf(logbuf, "  NewDTObject(%s) => %lx ioerr=%ld",
                            tmpname, (ULONG)dt, IoErr());
                    DTLog(logbuf);
                }
                if (dt)
                {
                    D(DBF_STARTUP, "DecoderThread: DT created");

                    /* Trigger layout / colour remap for the target screen. */
                    DoMethod(dt, DTM_PROCLAYOUT, NULL, 1L);

                    struct BitMapHeader *bmhd = NULL;
                    struct BitMap *srcbmp = NULL;
                    UBYTE *mask = NULL;

                    GetDTAttrs(dt,
                        PDTA_BitMapHeader, (ULONG)&bmhd,
                        PDTA_DestBitMap,   (ULONG)&srcbmp,
                        PDTA_MaskPlane,    (ULONG)&mask,
                        TAG_DONE);

                    /* PDTA_DestBitMap is only populated after PROCLAYOUT;
                       fall back to the raw source bitmap otherwise. */
                    if (!srcbmp)
                        GetDTAttrs(dt, PDTA_BitMap, (ULONG)&srcbmp, TAG_DONE);

                    ULONG width = 0, height = 0;
                    if (bmhd)
                    {
                        width  = bmhd->bmh_Width;
                        height = bmhd->bmh_Height;
                    }
                    else
                    {
                        GetDTAttrs(dt,
                            DTA_NominalHoriz, (ULONG)&width,
                            DTA_NominalVert,  (ULONG)&height,
                            TAG_DONE);
                    }

                    {
                        char logbuf[160];
                        sprintf(logbuf,
                                "  DT %lux%lu srcbmp=%lx mask=%lx bmhd=%lx",
                                width, height, (ULONG)srcbmp, (ULONG)mask, (ULONG)bmhd);
                        DTLog(logbuf);
                    }
                    D(DBF_STARTUP, "DecoderThread: DT %lux%lu src=%lx mask=%lx",
                        width, height, (ULONG)srcbmp, (ULONG)mask);

                    struct DecoderData decData;
                    memset(&decData, 0, sizeof(decData));
                    decData.Scr = args->Scr;
                    decData.StatusItem = item;
                    decData.HTMLview = args->Obj;
                    decData.LoadHook = loadhook;
                    decData.LoadMsg = &loadmsg;

                    RenderEngine render(args->Scr, &decData);
                    BOOL alloc_ok = FALSE;
                    if (srcbmp && width > 0 && height > 0)
                    {
                        alloc_ok = render.AllocateFrame(width, height, 0, DisposeNOP, 0, 0, NULL,
                                       mask ? TransparencySINGLE : TransparencyNONE, PicFLG_Full);
                    }
                    {
                        char logbuf[128];
                        sprintf(logbuf, "  AllocateFrame => %s item->Picture=%lx",
                                alloc_ok ? "OK" : "FAIL", (ULONG)item->Picture);
                        DTLog(logbuf);
                    }
                    if (alloc_ok && item->Picture && item->Picture->BMp)
                    {
                        /* BltBitMap both src and dest are friend bitmaps
                           of args->Scr, so this is a straight copy with
                           any necessary on-the-fly format conversion. */
                        BltBitMap(srcbmp, 0, 0,
                                  item->Picture->BMp, 0, 0,
                                  width, height,
                                  0xC0, 0xFF, NULL);
                        WaitBlit();

                        if (mask && item->Picture->Mask)
                            CopyMem(mask, item->Picture->Mask, RASSIZE(width, height));

                        item->Enter();
                        item->CurrentY = height;
                        item->Started = FALSE;
                        item->Leave();
                        result = TRUE;
                        DTLog("  blit OK");
                        D(DBF_STARTUP, "DecoderThread: blit OK");
                    }
                    else
                    {
                        char logbuf[160];
                        sprintf(logbuf,
                                "  decode failed: srcbmp=%lx w=%lu h=%lu alloc=%s",
                                (ULONG)srcbmp, width, height, alloc_ok ? "OK" : "FAIL");
                        DTLog(logbuf);
                        D(DBF_STARTUP, "DecoderThread: no source bitmap / AllocateFrame failed");
                    }
                    DisposeDTObject(dt);
                } else {
                    D(DBF_STARTUP, "DecoderThread: NewDTObject failed for %s", tmpname);
                }
            }
            DeleteFile(tmpname);
        }

        loadmsg.lm_Type = HTMLview_Close;
        CallHookPkt(loadhook, args->Obj, &loadmsg);
      } else {
          D(DBF_STARTUP, "DecoderThread: hook open failed for %s", args->Name);
      }

      item->Enter();
      item->Status = result ? StatusDone : StatusError;
      item->Thread = NULL;
      item->Leave();
      
      Signal(parent, 1 << args->MainSigBit);
    }
    delete args;
  }
}

VOID DecodeImage (Object *obj, UNUSED struct IClass *cl, struct ImageList *image, struct HTMLviewData *data)
{
  LONG sigbit = 0;
  if(image && (sigbit = AllocSignal(-1)) > 0)
  {
    STRPTR name = NULL;
    struct List *pscrs = LockPubScreenList();
    for(struct Node *node = pscrs->lh_Head; node->ln_Succ; node = node->ln_Succ)
    {
      struct PubScreenNode *pnode = (struct PubScreenNode *)node;
      if(pnode->psn_Screen == _screen(obj))
      {
        name = pnode->psn_Node.ln_Name;
        break;
      }
    }
    UnlockPubScreenList();
    struct Screen *lock = LockPubScreen(name);

    struct Args *args = new (std::nothrow) struct Args(obj, data, image, lock, sigbit, FindTask(NULL));
    if (!args)
    {
        UnlockPubScreen(NULL,lock);
        FreeSignal(sigbit);
        return;
    }

    #if defined(__PPC__)
    static const BOOL FBlit = FALSE;
    #else
    BOOL FBlit = FindPort("FBlit") ? TRUE : FALSE;
    #endif

    STRPTR taskname = FBlit ? (char *)"HTMLview ImageDecoder" : args->TaskName;
    struct Process *thread;
    if((thread = CreateNewProcTags(
      NP_Entry,        (ULONG)DecoderThread,
      NP_Priority,     (ULONG)-1,
      NP_Name,         (ULONG)taskname,
      #if defined(__MORPHOS__)
      NP_CodeType, 	   CODETYPE_PPC,
	  NP_PPCStackSize, STACKSIZEPPC,
      NP_StackSize,    STACKSIZE68K,
	  NP_CopyVars,     FALSE,
      NP_Input,        NULL,
      NP_CloseInput,   FALSE,
      NP_Output,       NULL,
	  NP_CloseOutput,  FALSE,
      NP_Error,        NULL,
      NP_CloseError,   FALSE,
      #endif
      TAG_DONE)) != NULL)
    {
      struct MsgPort replyPort;
      struct DecoderThreadStartupMessage startup;

      memset(&replyPort, 0, sizeof(replyPort));
      replyPort.mp_Node.ln_Type = NT_MSGPORT;
      NewList(&replyPort.mp_MsgList);
      replyPort.mp_SigBit = SIGB_SINGLE;
      replyPort.mp_SigTask = FindTask(NULL);

      memset(&startup, 0, sizeof(startup));
      startup.message.mn_ReplyPort = &replyPort;
      startup.args = args;

      PutMsg(&thread->pr_MsgPort, (struct Message *)&startup);
      
      WaitPort(&replyPort);
      while (GetMsg(&replyPort));

      Wait(1 << sigbit);
    } else {
        delete args;
    }

    FreeSignal(sigbit);
  }
}
