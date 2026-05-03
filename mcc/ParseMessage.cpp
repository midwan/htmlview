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
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/utility.h>
#include <libraries/mui.h>

#include "General.h"
#include "ParseMessage.h"
#include "Charset.h"
#include <new>

BOOL ParseMessage::OpenURL (STRPTR url, Object *htmlview, ULONG flags)
{
  HTMLview = htmlview;
  LoadMsg.lm_Type = HTMLview_Open;
  LoadMsg.lm_PageID = PageID;
  LoadMsg.lm_Params.lm_Open.URL = url;
  LoadMsg.lm_Params.lm_Open.Flags = flags;
  LoadMsg.lm_App = _app(htmlview);
  BOOL result = CallHookPkt(LoadHook, htmlview, &LoadMsg) ? TRUE : FALSE;

  return(result);
}

LONG ParseMessage::ReadURL (STRPTR buffer, ULONG size)
{
  LoadMsg.lm_Params.lm_Read.Buffer = buffer;
  LoadMsg.lm_Params.lm_Read.Size = size;
  return(CallHookPkt(LoadHook, HTMLview, &LoadMsg));
}

VOID ParseMessage::WriteURL (STRPTR buffer)
{
  if(buffer)
  {
    LoadMsg.lm_Type = HTMLview_Write;
    LoadMsg.lm_Params.lm_Write.Buffer = buffer;
    LoadMsg.lm_Params.lm_Write.Size = strlen(buffer);
    CallHookPkt(LoadHook, HTMLview, &LoadMsg);
  }
  LoadMsg.lm_Type = HTMLview_Read;
}

VOID ParseMessage::CloseURL ()
{
  LoadMsg.lm_Type = HTMLview_Close;
  CallHookPkt(LoadHook, HTMLview, &LoadMsg);
}

ParseMessage::ParseMessage (STRPTR buf, ULONG size, struct Hook *loadhook, ULONG pageid)
{
  Buffer = Current = Locked = Upper = buf;
  Size = size;
  LoadHook = loadhook;
  PageID = pageid;

  OL_Level = -1;
  FrameBorder = 1;
  PendingNewline = TRUE;

  /* The message is stack-allocated, so better clear the fields! */
  Linkable = FALSE;
  Title = NULL;
  Total = 0;
  Parsed = 0;
  Status = 0;
  PendingSpace = Gadgets = FALSE;
  NoFrames = FALSE;
  memset(OpenCounts, 0, tag_NumberOf);
  memset(OpenGroups, 0, group_NumberOf);
}

ParseMessage::~ParseMessage ()
{
  delete Buffer;
}

VOID ParseMessage::NextStartBracket ()
{
  while(Fetch() && *++Current != '<')
    ;
}

VOID ParseMessage::AdvancedNextEndBracket ()
{
  BOOL tag = FALSE, running = (*Current == '>') ? FALSE : TRUE;
  UBYTE quote = 0;
  while(running && Fetch())
  {
    switch(*++Current)
    {
      case '>':
        if(tag)
            tag = FALSE;
        else  running = FALSE;
      break;

      case '<':
        if(quote)
            tag = TRUE;
        else  running = FALSE;
      break;

      case '"':
      case '\'':
        if(!quote)
          quote = *Current;
        else if(quote == *Current)
          quote = 0;
      break;
    }
  }

  if(Fetch())
  {
    if(*Current == '>')
        *Current++ = '\0';
    else  *Current = '\0';
  }
}

VOID ParseMessage::NextEndBracket ()
{
  BOOL running = (*Current == '>') ? FALSE : TRUE;
  while(running && Fetch())
  {
    switch(*++Current)
    {
      case '<':
      case '>':
        running = FALSE;
      break;
    }
  }

  if(Fetch())
  {
    if(*Current == '>')
        *Current++ = '\0';
    else  *Current = '\0';
  }
}

VOID ParseMessage::SkipSpaces ()
{
  while(Fetch())
  {
    if(IsWhitespace(*Current))
        Current++;
    else  break;
  }
}

VOID ParseMessage::SetError (ULONG error)
{
  Status = error;
  Current = Upper;
}

BOOL ParseMessage::PreloadAndConvert ()
{
  /* Drain the response body into a growing local buffer. We can't
     reuse Buffer/Upper directly because the Fetch() machinery
     interleaves them with progressive read state — easier to read
     into a fresh buffer and swap once we know the full size. */

  /* Hard cap for the URL-load body. A misbehaving (or malicious)
     server could otherwise stream gigabytes; on a small Amiga that
     would thrash the memory pool before allocation finally fails.
     8 MiB is comfortably above any realistic HTML page and small
     enough to bail predictably on systems with modest free RAM. */
  const ULONG MAX_BODY = 8u * 1024u * 1024u;

  ULONG total    = 0;
  ULONG capacity = 4096;
  STRPTR buf = new (std::nothrow) char[capacity];
  if(!buf) return FALSE;

  for(;;)
  {
    if(total + 1024 > capacity)
    {
      ULONG newcap = capacity * 2;
      if(newcap > MAX_BODY) newcap = MAX_BODY;
      if(newcap <= capacity) break;     /* Cap reached. */
      STRPTR nb = new (std::nothrow) char[newcap];
      if(!nb)
      {
        delete[] buf;
        return FALSE;
      }
      memcpy(nb, buf, total);
      delete[] buf;
      buf      = nb;
      capacity = newcap;
    }
    if(total + 1 >= capacity) break;    /* No more room to grow. */
    LONG n = ReadURL(buf + total, capacity - total - 1);
    if(n <= 0) break;
    total += n;
  }
  buf[total] = '\0';
  CloseURL();

  /* Run the drained body through codesets.library when applicable.
     ConvertUTF8 returns NULL on ASCII-only input or when the library
     isn't installed — in either case we keep `buf` as the parse
     buffer. */
  STRPTR converted = ConvertUTF8(buf, total);
  if(converted)
  {
    ULONG cLen = strlen(converted);
    STRPTR cb = new (std::nothrow) char[cLen + 1];
    if(cb)
    {
      memcpy(cb, converted, cLen + 1);
      delete[] buf;
      buf   = cb;
      total = cLen;
    }
    /* If allocation for cb failed, fall through with the original
       UTF-8 bytes — degraded but safe. */
    FreeConvertedStr(converted);
  }

  /* Swap into ParseMessage's owned buffer slot. */
  delete[] Buffer;
  Buffer   = buf;
  Locked   = buf;
  Current  = buf;
  Upper    = buf + total;
  Size     = total + 1;
  Total    = total;
  Parsed   = 0;
  Status   = ParseMsg_Succes;     /* No more network reads. */

  return TRUE;
}

BOOL ParseMessage::Fetch (UWORD len)
{
  if(Current+len >= Upper && !Status)
  {
    Parsed += Locked-Buffer;

    LONG left = Upper-Locked;
    if(Size-left < 1024)
    {
      Size *= 2;
      STRPTR buf = new (std::nothrow) char[Size];
      if (buf)
      {
//      if(left > 0)
        memcpy(buf, Locked, left);
        delete Buffer;
        Buffer = buf;
  	  }
      else return FALSE;
    }
    else
    {
//      if(left >= 0)
          memcpy(Buffer, Locked, left);
//      else  kprintf("*** memcpy(0x%lx, 0x%lx, %ld)\n", Buffer, Locked, left);
    }
    LONG offset = Current-Locked;
    Locked = Buffer;
    Current = Buffer+offset;

    ULONG size = Size-left;
    if(!(size = ReadURL(Buffer+left, size)))
      Status = ParseMsg_Succes;
//    Buffer[left + size] = '\0';

    Upper = Buffer+left+size;
    Total += size;

    if(SetSignal(0L, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
      SetError(ParseMsg_Aborted);
  }
  return(Current < Upper); // && !Status);
}

BufferParseMessage::BufferParseMessage (STRPTR buf, ULONG size) : ParseMessage(NULL, 0, NULL, 0)
{
  ULONG len = strlen(buf) + 1;
  STRPTR buffer = new (std::nothrow) char[len];
  if (buffer)
  {
	memcpy(buffer, buf, len);

    Buffer = Current = Locked = buffer;
    Upper = buffer + len - 1;
    Size = len;
  }

  if(*Current != '<')
    NextStartBracket();
}

BOOL BufferParseMessage::Fetch (UWORD len)
{
  return(Current < Upper);
}
