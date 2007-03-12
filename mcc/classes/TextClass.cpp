
#include "TextClass.h"

#include "Entities.h"
#include "General.h"
#include "Layout.h"
#include "MinMax.h"
#include "ParseMessage.h"

#include <graphics/gfxmacros.h>

VOID TextClass::DeleteLineInfo ()
{
	struct TextLineInfo *cur, *first = LineInfo;
	while(first)
	{
		cur = first;
		first = first->Next;
		delete cur;
	}

	LineInfo = NULL;
	LineInfoLast = (struct TextLineInfo *)&LineInfo;
}

class TextClass *TextClass::Find (struct FindMessage &fmsg)
{
	if(fmsg.LastMatch)
	{
		if(this == fmsg.LastMatch)
			fmsg.LastMatch = NULL;
	}
	else
	{
		if(Font && Bottom - Font->tf_YSize > fmsg.TopPos)
		{
			BYTE *map = fmsg.SearchMap;
			LONG skip, len = fmsg.StrLength;
			STRPTR contents = Contents + len-1, upper = Contents + Length;
			while(contents < upper)
			{
				skip = map[*contents];
				contents += skip;
				if(skip <= 0)
				{
					if(!fmsg.StrCmp(contents, fmsg.SearchString, len))
					{
						LONG index = contents - Contents;
						MarkBegin = index;
						MarkEnd = index + len;
						fmsg.MarkMsg->AddBox(this, index, index + len);

						struct TextLineInfo *line = LineInfo;
						while(line)
						{
							if((index -= line->Length) < 0)
							{
								fmsg.TopPos = line->Baseline - Font->tf_Baseline;
								break;
							}
							line = line->Next;

						}
						return(this);
					}
					contents += len;
				}
			}
		}
	}
	return(NULL);
}

BOOL TextClass::HitTest (struct HitTestMessage &hmsg)
{
	if(hmsg.Obj && (Flags & FLG_Layouted))
	{
		LONG x = hmsg.X, y = hmsg.Y;
		STRPTR contents = Contents;
		struct TextLineInfo *line = LineInfo;
		while(line)
		{
			ULONG length = line->Length, width;
			if((Flags & FLG_Text_Pre) && contents[length-1] == '\n')
				length--;

			ULONG top = line->Baseline - Font->tf_Baseline;
			if(y < top)
				break;

			if(y < top+Font->tf_YSize && x >= line->Left)
			{
				width = MyTextLength(Font, contents, length);
				if(x < line->Left + width)
					return(TRUE);
			}
			contents += line->Length;
			line = line->Next;
		}
	}
	return(FALSE);
}

VOID TextClass::Relayout (BOOL all)
{
	DeleteLineInfo();
	Flags &= ~FLG_Newline;
	SuperClass::Relayout(all);
}

BOOL TextClass::Layout (struct LayoutMessage &lmsg)
{
	struct TextFont *font = Font = lmsg.Font;
	Top = lmsg.Y - font->tf_Baseline;
	Bottom = lmsg.Y + (font->tf_YSize-font->tf_Baseline);
	/* This will be added for each loop */
	Bottom -= font->tf_YSize+1;

	STRPTR contents = Contents;
	ULONG length = Length;
	class SuperClass *obj = this;

	BOOL newline;
	do	{

		newline = FALSE;
		ULONG width = 0, skip = 0;
		if(Flags & FLG_Text_Pre)
		{
			while(width < length && contents[width] != '\n')
				width++;

			if(contents[width] == '\n')
			{
				newline = TRUE;
				width++;
			}
		}
		else
		{
			LONG pixels = lmsg.ScrWidth();
			width = (pixels > 0) ? MyTextFit(font, contents, length, pixels, 1) : 1;
			if(length > width)
			{
				newline = TRUE;
				if(contents[width++] != ' ')
				{
					ULONG safewidth = width;
					while(--width && contents[width-1] != ' ' && contents[width-1] != '-')
						;

					if(!width && lmsg.IsAtNewline())
					{
						width = safewidth;
						while(width < length && contents[width-1] != ' ' && contents[width-1] != '-')
							width++;
					}
				}
				else
				{
					skip = 1;
				}
			}
		}
		lmsg.UpdateBaseline(font->tf_YSize+1, font->tf_Baseline);
		Bottom += font->tf_YSize+1;

		if(width)
		{
			struct TextLineInfo *info = new struct TextLineInfo(lmsg.X, width);
			LineInfoLast->Next = info;
			LineInfoLast = info;

			struct ObjectNotify *notify = new struct ObjectNotify(info->Left, info->Baseline, obj);
			lmsg.AddNotify(notify);

			obj = NULL;

			lmsg.X += MyTextLength(font, contents, width-skip);
		}

		if(newline)
		{
			Flags |= FLG_Newline;
			lmsg.Newline();
		}

		contents += width;
		length -= width;

	}	while(length);

	Flags |= FLG_WaitingForSize;
}

VOID TextClass::AdjustPosition (LONG x, LONG y)
{
	Left += x;
	SuperClass::AdjustPosition(x, y);

	struct TextLineInfo *line = LineInfo;
	while(line)
	{
		line->Left += x;
		line->Baseline += y;
		line = line->Next;
	}
}

BOOL TextClass::Mark (struct MarkMessage &mmsg)
{
	if(SuperClass::Mark(mmsg))
		return(TRUE);

	LONG b_offset = FindChar(mmsg.X1, mmsg.Y1, mmsg.Newline);
	LONG e_offset = FindChar(mmsg.X2, mmsg.Y2, mmsg.Newline);
	mmsg.Newline = FALSE;

	LONG begin = 0, end = Length;
	if(mmsg.Flags & MarkFLG_FirstCoordTaken)
	{
		if(mmsg.Flags & MarkFLG_SecondCoordTaken)
		{
			end = 0;
		}
		else if(e_offset != -1)
		{
			end = e_offset;
			mmsg.Flags |= MarkFLG_SecondCoordTaken;
		}
	}
	else if(mmsg.Flags & MarkFLG_SecondCoordTaken)
	{
		if(b_offset != -1)
		{
			end = b_offset;
			mmsg.Flags |= MarkFLG_FirstCoordTaken;
		}
	}
	else
	{
		if(b_offset == -1 && e_offset == -1)
		{
			end = 0;
		}
		else
		{
			if(b_offset == -1)
					b_offset = Length;
			else	mmsg.Flags |= MarkFLG_FirstCoordTaken;

			if(e_offset == -1)
					e_offset = Length;
			else	mmsg.Flags |= MarkFLG_SecondCoordTaken;

			if(b_offset < e_offset)
			{
				begin = b_offset;
				end = e_offset;
			}
			else
			{
				begin = e_offset;
				end = b_offset;
			}
		}
	}

	if(MarkBegin != MarkEnd || begin != end)
	{
		if(MarkBegin != begin || MarkEnd != end)
		{
			LONG b = min(MarkBegin, begin), e = max(MarkEnd, end);
			if(MarkBegin == begin)
				b = min(MarkEnd, end);
			else if(MarkEnd == end)
				e = max(MarkBegin, begin);
			else if(MarkBegin == MarkEnd)
			{
				b = begin;
				e = end;
			}
			else if(begin == end)
			{
				b = MarkBegin;
				e = MarkEnd;
			}
			mmsg.AddBox(this, b, e);
		}
	}

	if((MarkBegin = begin) != (MarkEnd = end))
		mmsg.WriteText(Contents+begin, end-begin);

	if(mmsg.Flags == (MarkFLG_FirstCoordTaken | MarkFLG_SecondCoordTaken | MarkFLG_LastCoordTaken))
			return(TRUE);
	else	return(FALSE);
}

LONG TextClass::FindChar (LONG x, LONG y, BOOL newline)
{
	STRPTR contents = Contents;
	struct TextLineInfo *line = LineInfo;
	while(line)
	{
		ULONG length = line->Length, width;
		if((Flags & FLG_Text_Pre) && contents[length-1] == '\n')
			length--;

		ULONG top = line->Baseline - Font->tf_Baseline;
		if(y < top)
			break;

		if(y < top+Font->tf_YSize+1)
		{
			LONG result = newline ? 0 : -1;
			if(x >= line->Left)
			{
				width = MyTextLength(Font, contents, length);
				if(x < line->Left + width)
						result = MyTextFit(Font, contents, length, x - line->Left, 1);
				else	result = line->Next ? length : -1;
			}
			return(result != -1 ? result + (contents - Contents) : -1);
		}

		contents += line->Length;
		line = line->Next;
		newline = TRUE;
	}
	return(-1);
}

VOID TextClass::RenderMarked (struct RastPort *rp, LONG markbegin, LONG markend, LONG xoffset, LONG yoffset)
{
	STRPTR contents = Contents;
	struct TextLineInfo *line = LineInfo;
	while(line)
	{
		LONG length = line->Length;
		if((Flags & FLG_Text_Pre) && contents[length-1] == '\n')
			length--;

		if(line->Next && contents[length-1] == ' ')
			length--;

		if(length > 0)
		{
			LONG offset = contents - Contents;
			LONG begin = markbegin - offset;
			LONG end = markend - offset;
			if(begin < length && end > 0)
			{
				if(begin < 0)
					begin = 0;
				if(end > length)
					end = length;

				LONG p_begin, p_length;
				p_begin = line->Left + MyTextLength(Font, contents, begin);
				p_length = MyTextLength(Font, contents+begin, end - begin);

				LONG x = p_begin-xoffset;
				LONG y = line->Baseline-Font->tf_Baseline-yoffset;
				if(y > 0 && y < 5000)
					RectFill(rp, x, y, x + p_length - 1, y + Font->tf_YSize - 1);
			}
		}
		contents += line->Length;
		line = line->Next;
	}
}

VOID TextClass::MinMax (struct MinMaxMessage &mmsg)
{
	if(Flags & FLG_Text_Pre)
	{
		ULONG l = 0, i;
		while(Contents[l])
		{
			i = 0;
			while(Contents[l+i] && Contents[l+i] != '\n')
				i++;

			mmsg.X += MyTextLength(mmsg.Font, Contents+l, i);

			if(Contents[l+i] == '\n')
			{
				mmsg.Min = max(mmsg.X, mmsg.Min);
				mmsg.Newline();
				l++;
			}
			l += i;
		}
		mmsg.Min = max(mmsg.X, mmsg.Min);
	}
	else
	{
		mmsg.X += MyTextLength(mmsg.Font, Contents, Length);

		ULONG i = 0;
		while(Contents[i])
		{
			ULONG length = 0;
			while(Contents[i+length] && Contents[i+length] != ' ')
				length++;

			mmsg.Min = max(mmsg.Indent + MyTextLength(mmsg.Font, Contents+i, length), mmsg.Min);

			i += length;
			if(Contents[i] == ' ')
				i++;
		}
	}
	Flags |= FLG_KnowsMinMax;
}

VOID TextClass::Parse(REG(a2, struct ParseMessage &pmsg))
{
	ULONG length = pmsg.Current-pmsg.Locked;
	Contents = new char[length+2];
	BOOL pre;
	if(pre = pmsg.OpenCounts[tag_PRE])
		Flags |= FLG_Text_Pre;

	BOOL space = pmsg.PendingSpace;
	STRPTR src = pmsg.Locked, dst = Contents;

	if(space)
	{
		*dst++ = ' ';
		pmsg.PendingSpace = FALSE;
	}

	while(length--)
	{
		UBYTE character = *src++;

		switch(character)
		{
			case 132:
			{
				*dst++ = '"';
				space = FALSE;
			}
			break;

			case 146:
			{
				*dst++ = '\'';
				space = FALSE;
			}
			break;

			case 149:
				*dst++ = '�';
				space = FALSE;
			break;

			case 150:
			case 151:
			{
				*dst++ = '-';
				space = FALSE;
			}
			break;

/*			case 153:
			{
				*dst++ = '(';
				*dst++ = 'T';
				*dst++ = 'M';
				*dst++ = ')';
				space = FALSE;
			}
			break;
*/
			case '&':
			{
				if(*src == '#')
				{
					WORD t_char = 0;
					BOOL scanning = TRUE;
					UWORD i = 1;
					do {

						t_char *= 10;
						t_char += src[i]-'0';
						i++;

					}	while(src[i] != ';' && i < 4);

					if(src[i++] == ';' && t_char < 256)
					{
						character = t_char;
						src += i;
						length -= i;

						switch(character)
						{
							case 132:
								character = '"';
							break;

							case 146:
								character = '\'';
							break;

							case 149:
								character = '�';
							break;

							case 150:
							case 151:
								character = '-';
							break;

							case 153:
							{
								*dst++ = '(';
								*dst++ = 'T';
								*dst++ = 'M';
								character = ')';
							}
							break;
						}
					}
				}
				else
				{
					struct EntityInfo *entity;
					if(entity = GetEntityInfo(src))
					{
						character = entity->ByteCode;
						src += strlen(entity->Name);
						length -= strlen(entity->Name);
						if(*src == ';')
						{
							src++;
							length--;
						}
					}
				}
			}
			/* `break' mangler, og det er med vijle ;-) */

			default:
			{
				if(IsWhitespace(character))
				{
					if(pre)
					{
						if(length && ((character == 10 && *src == 13) || (character == 13 && *src == 10)))
						{
							character = 10;
							length--;
							src++;
						}
						else if(character == 13)
							character = 10;

						*dst++ = character;
					}
					else
					{
						if(!space)
						{
							*dst++ = ' ';
							space = TRUE;
						}
					}
				}
				else
				{
					*dst++ = character;
					space = FALSE;
				}
			}
			break;
		}
	}

	Length = dst-Contents;
	Contents[Length] = '\0';

#ifdef OUTPUT
	printf("...text...\n");
#endif
}

VOID TextClass::Render (struct RenderMessage &rmsg)
{
	struct RastPort *rp = rmsg.RPort;
	if(rp->Font != Font)
		SetFont(rp, Font);
//	SetAPen(rp, rmsg.Colours[(rmsg.Textstyles & TSF_ALink) ? Col_ALink : ((rmsg.Textstyles & TSF_Link) ? ((rmsg.Textstyles & TSF_VLink) ? Col_VLink : Col_Link) : Col_Text)]);
	SetAPen(rp, rmsg.Colours[rmsg.Textstyles & TSF_ALink ? Col_ALink : Col_Text]);
	SetSoftStyle(rp, rmsg.Textstyles & TSF_StyleMask, TSF_StyleMask);

	LONG baseline = Font->tf_Baseline, extra = Font->tf_YSize - Font->tf_Baseline;
	LONG miny = rmsg.MinY+rmsg.OffsetY, maxy = rmsg.MaxY+rmsg.OffsetY;
	BOOL mark = MarkBegin != MarkEnd;

	STRPTR contents = Contents;
	struct TextLineInfo *line = LineInfo;
	while(line && line->Baseline - baseline <= maxy)
	{
		if(line->Baseline + extra > miny)
		{
			LONG length = line->Length;
			if((Flags & FLG_Text_Pre) && contents[length-1] == '\n')
				length--;

			if(line->Next && contents[length-1] == ' ')
				length--;

			if(length > 0)
			{
				Move(rp, line->Left-rmsg.OffsetX, line->Baseline-rmsg.OffsetY);
				Text(rp, contents, length);
				if(rmsg.Textstyles & TSF_Link)
				{
					UWORD pattern[] = { 0xcccc, 0xcccc };
					if(rmsg.Textstyles & TSF_VLink)
						SetAfPt(rp, pattern, 1);

					LONG underscore = (line->Baseline+2-rmsg.OffsetY);
					RectFill(rp, line->Left-rmsg.OffsetX, underscore, rp->cp_x-1, underscore);
					SetAfPt(rp, NULL, 0);
				}

				if(mark)
				{
					LONG offset = contents - Contents;
					LONG begin = MarkBegin - offset;
					LONG end = MarkEnd - offset;
					if(begin < length && end > 0)
					{
						if(begin < 0)
							begin = 0;
						if(end > length)
							end = length;

						LONG p_begin, p_length;
						p_begin = line->Left + MyTextLength(Font, contents, begin);
						p_length = MyTextLength(Font, contents+begin, end - begin);

						LONG x = p_begin-rmsg.OffsetX;
						LONG y = line->Baseline-Font->tf_Baseline-rmsg.OffsetY;
						SetDrMd(rp, COMPLEMENT);
						RectFill(rp, x, y, x + p_length - 1, y + Font->tf_YSize - 1);
						SetDrMd(rp, JAM1);
					}
				}
			}
		}
		contents += line->Length;
		line = line->Next;
	}

/*	struct RastPort *rp = rmsg.RPort;

	ULONG skip = 0, left = Left, length = Length;
	STRPTR contents = Contents;
	if(Left < rmsg.MinX+rmsg.OffsetX)
	{
		skip = MyTextFit(rmsg.Font, Contents, Length, (rmsg.MinX+rmsg.OffsetX-Left), 1);
		left += MyTextLength(rmsg.Font, Contents, skip);
		length -= skip;
		contents += skip;
	}

	if(length)
	{
		length = MyTextFit(rmsg.Font, contents, length, (rmsg.MaxX-rmsg.MinX), 1);
		if(length < Length-(skip+1))
			length += 2;
		else if(length < Length-skip)
			length++;

		SetSoftStyle(rp, rmsg.Textstyles & TSF_StyleMask, TSF_StyleMask);
		SetAPen(rp, (rmsg.Textstyles & TSF_Link) ? 3 : 1);
		Move(rp, left-rmsg.OffsetX, Baseline-rmsg.OffsetY);
		Text(rp, contents, length);
		if(rmsg.Textstyles & TSF_Link)
			RectFill(rp, left-rmsg.OffsetX, Top-rmsg.OffsetY+rmsg.Font->tf_YSize-1, rp->cp_x-1, Top-rmsg.OffsetY+rmsg.Font->tf_YSize-1)
	}
*/
}

