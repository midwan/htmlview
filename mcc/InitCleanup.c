/***************************************************************************

 HTMLview.mcc - HTMLview MUI Custom Class
 Copyright (C) 1997-2000 Allan Odgaard
 Copyright (C) 2005 by TextEditor.mcc Open Source Team

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

#include <exec/memory.h>
#include <exec/tasks.h>
#include <libraries/mui.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>

#include "General.h"
#include "TagInfo.h"
#include "Entities.h"
#include "Colours.h"
#include "ScrollGroup.h"
#include "private.h"

#include "Memory.c"

/*
struct MUI_CustomClass *HTMLviewClass = NULL;
struct Library *MUIMasterBase;

VOID Cleanup ()
{
	if(MUIMasterBase)
	{
		if(HTMLviewClass)
		{
			MUI_DeleteCustomClass(HTMLviewClass);
			if(ScrollGroupClass)
				MUI_DeleteCustomClass(ScrollGroupClass);
		}
		CloseLibrary(MUIMasterBase);
	}
//	DisposeColourTree();
//	DisposeEntityTree();
//	DisposeTagTree();
}

SAVEDS ASM ULONG _Dispatcher(REG(a0, struct IClass * cl), REG(a2, Object * obj), REG(a1, Msg msg));

BOOL Init (struct Library *base = NULL)
{
//	CreateTagTree();
//	CreateEntityTree();
//	CreateColourTree();
	if(MUIMasterBase = OpenLibrary("muimaster.library", 11))
	{
		if(HTMLviewClass = MUI_CreateCustomClass(base, MUIC_Virtgroup, NULL, sizeof(HTMLviewData), ENTRY(_HTMLviewDispatcher)))
		{
			if(ScrollGroupClass = MUI_CreateCustomClass(NULL, MUIC_Virtgroup, NULL, sizeof(ScrollGroupData), ENTRY(ScrollGroupDispatcher)))
				return(TRUE);
		}
	}
	Cleanup();

	return(FALSE);
}
*/