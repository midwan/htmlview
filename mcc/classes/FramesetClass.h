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

#ifndef FRAMESETCLASS_H
#define FRAMESETCLASS_H

#include "TreeClass.h"

// forward declarations

class FramesetClass : public TreeClass
{
  public:
    FramesetClass () : TreeClass() { ; }
    ~FramesetClass () { delete ColumnsStr; delete RowsStr; delete Columns; delete Rows; }
    VOID Parse (REG(a2, struct ParseMessage &pmsg));
    BOOL Layout (struct LayoutMessage &lmsg);
    VOID Render (struct RenderMessage &rmsg);
    Object *LookupFrame (STRPTR name, class HostClass *hclass);
    Object *HandleMUIEvent (struct MUIP_HandleEvent *hmsg);
    Object *FindDefaultFrame (ULONG &size);

  protected:
    ULONG *ConvertSizeList(STRPTR value_list, LONG total, ULONG &cnt);

  protected:
    LONG Left, Width;
    ULONG ColumnCnt, RowCnt;
    STRPTR ColumnsStr, RowsStr;
    ULONG *Columns, *Rows;
};

#endif // FRAMESETCLASS_H
