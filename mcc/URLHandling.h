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

#ifndef URLHANDLING_H
#define URLHANDLING_H

BOOL LocalURL (STRPTR url);
STRPTR HTMLview_AddPart (Object *obj, struct MUIP_HTMLview_AddPart *amsg, struct HTMLviewData *data);
VOID HTMLview_SetPath (Object *obj, STRPTR url, struct HTMLviewData *data);

#endif