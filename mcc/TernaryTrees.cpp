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

#include <exec/types.h>

#include "TernaryTrees.h"
#include <new>

#define QToUpper(c) ((c >= 'a' && c <= 'z') ? c-('a'-'A') : c)

TNode::TNode(CONST_STRPTR str, CONST_APTR data)
{
  if((SplitChar = *str))
      Middle = new (std::nothrow) TNode(str+1, data);
  else Data = data;
}

TNode::~TNode ()
{
  delete Left;
  delete Right;
}

struct TNode *TNode::STInsert(struct TNode *root, CONST_STRPTR str, CONST_APTR data)
{
	if (root)
    {
		if (*str < root->SplitChar) root->Left = STInsert(root->Left, str, data);
		else if (*str > root->SplitChar) root->Right = STInsert(root->Right, str, data);
		else if (root->SplitChar) root->Middle = STInsert(root->Middle, str+1, data);

		return root;
	}
	else
    {
		return new (std::nothrow) TNode(str, data);
	}
}

APTR TFind (struct TNode *node, CONST_STRPTR str, UBYTE *table)
{
  UBYTE chr, src = *str++;
  src = QToUpper(src);

  while(node)
  {
    chr = node->SplitChar;
    if(src < chr)
      node = node->Left;
    else if(src > chr)
      node = node->Right;
    else if((node = node->Middle), src)
//      src = (table[src = *str] ? 0 : (str++, QToUpper(src)));
      if(table[src = *str])
          src = 0;
      else  str++, (src = QToUpper(src));
    else break;
  }

  return node;
}
