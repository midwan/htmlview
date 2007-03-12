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

#include <stdio.h>
#include <clib/alib_protos.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <libraries/mui.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <mui/BetterString_mcc.h>
#include <mui/InfoText_mcc.h>
#include <mui/TextEditor_mcc.h>
//#include <mui/ipc_mcc.h>

#include "HTMLview_mcc.h"
#include "private.h"
#include "ScrollGroup.h"

struct MUI_CustomClass* mcc = NULL;

struct Library* LayersBase = NULL;
struct Library* KeymapBase = NULL;
struct Library* CxBase = NULL;
struct Library* CyberGfxBase = NULL;
struct Library* DiskfontBase = NULL;
struct Library* DataTypesBase = NULL;
struct Library* MUIMasterBase = NULL;
struct Library* IntuitionBase = NULL;
struct Library* GfxBase = NULL;
struct Library* UtilityBase = NULL;

#if defined(__amigaos4__)
struct LayersIFace*       ILayers = NULL;
struct KeymapIFace*       IKeymap = NULL;
struct CommoditiesIFace*  ICommodities = NULL;
struct CyberGfxIFace*     ICyberGfx = NULL;
struct DiskfontIFace*     IDiskfont = NULL;
struct DataTypesIFace*    IDataTypes = NULL;
struct MUIMasterIFace*    IMUIMaster = NULL;
struct IntuitionIFace*    IIntuition = NULL;
struct GraphicsIFace*     IGraphics = NULL;
struct UtilityIFace*      IUtility = NULL;
#endif

SAVEDS ASM ULONG _Dispatcher(REG(a0, struct IClass * cl), REG(a2, Object * obj), REG(a1, Msg msg));

HOOKPROTONH(GotoURLCode, ULONG, Object* htmlview, STRPTR *url)
{
	STRPTR target;
	GetAttrs(htmlview, MUIA_HTMLview_Target, &target, TAG_DONE);
	DoMethod(htmlview, MUIM_HTMLview_GotoURL, *url, target);
}
MakeStaticHook(GotoURLHook, GotoURLCode);

Object *BuildApp(void)
{
	static STRPTR classes[] = { "HTMLview.mcc", "TextEditor.mcc", "BetterString.mcc", NULL };
	Object *app, *win, *urlstring, *gauge, *htmlview, *vscroll, *hscroll, *infotext;
	Object *alien, *mcp, *scalos, *sysspeed, *konsollen, *p96;
	Object *searchstr;
//	Object *htmlview2, *vscroll2, *hscroll2;

  ENTER();

	if(app = ApplicationObject,
		MUIA_Application_Author,		"Allan Odgaard",
		MUIA_Application_Base,			"HTMLview-Demo",
		MUIA_Application_Copyright,	"�1998 Allan Odgaard",
		MUIA_Application_Description,	"HTML-display custom class",
		MUIA_Application_Title,			"HTMLview",
		MUIA_Application_Version,		"$VER: HTMLview V0.1� (" __DATE__ ")",
		//MUIA_Application_UsedClasses, classes,

		SubWindow, win = WindowObject,
			MUIA_Window_ID, 'MAIN',
			MUIA_Window_Title, "HTMLview-Demo",
			MUIA_Window_DefaultObject, htmlview,
			MUIA_Window_UseBottomBorderScroller, TRUE,
			MUIA_Window_UseRightBorderScroller, TRUE,

			WindowContents, VGroup,
				Child, HGroup,
					Child, alien = SimpleButton("Alien Design"),
					Child, mcp = SimpleButton("MCP"),
					Child, scalos = SimpleButton("Scalos"),
					Child, sysspeed = SimpleButton("SysSpeed"),
					Child, konsollen = SimpleButton("Konsollen"),
					Child, p96 = SimpleButton("Picasso 96"),
					End,
				Child, urlstring = BetterStringObject,
					StringFrame,
					MUIA_CycleChain, TRUE,
					MUIA_ControlChar, '\r',
//					MUIA_String_Contents, "file://DH0:T/�98.07.13�/parcon/index.htm",
//					MUIA_String_Contents, "file://Duff's:T/�98.07.05�/TestV.html",
//					MUIA_String_Contents, "file://DH0:T/�98.05.29�/frames.html",
//					MUIA_String_Contents, "file://Duff's:T/�98.06.27�/page.html",
//					MUIA_String_Contents, "file://Duff's:T/�98.05.31�/aminew.html",
//					MUIA_String_Contents, "file://Duff's:T/�98.05.19�/jubii.html",
//					MUIA_String_Contents, "file://Duff's:T/�98.05.23�/download.html",
//					MUIA_String_Contents, "file://Data:Homepage/daywatch/welcome.html",
//					MUIA_String_Contents, "file://Duff's:T/�97.12.31�/applist.html",
//					MUIA_String_Contents, "file://Duff's:WorkBench/Locale/Help/English/Developer/HTML4.0/cover.html#toc",
//					MUIA_String_Contents, "file://progdir:testpages/main.html",
//					MUIA_String_Contents, "file://Duff's:I'mOnline/AWeb3/Docs/html.html",
//					MUIA_String_Contents, "file://Data:Homepage/htmlview/index.html",
//					MUIA_String_Contents, "file://Data:Homepage/texteditor/index.html#Features",
//					MUIA_String_Contents, "file://Duff's:Data/C-Sources/HTMLParser/Testpage.html",
//					MUIA_String_Contents, "file://Duff's:T/�98.08.28�/alt_os.html",
//					MUIA_String_Contents, "file://Data:Homepage/index.html",
//					MUIA_String_Contents, "file://Data:Products/IProbe/IProbe.ReadMe",
					MUIA_String_Contents, "file://Download:index.html",
					End,
				Child, gauge = GaugeObject,
					GaugeFrame,
					MUIA_Gauge_Current, 0,
					MUIA_Gauge_Max, 100,
					MUIA_Gauge_Horiz, TRUE,
					MUIA_Gauge_InfoText, "(no document loaded)",
					End,

/*				Child, HGroup,

				Child, ColGroup(2),
					MUIA_Group_Spacing, 0,
					Child, htmlview2 = (Object *)NewObject(HTMLviewClass->mcc_Class, NULL,
						VirtualFrame,
						End,
					Child, vscroll2 = ScrollbarObject,
						End,
					Child, hscroll2 = ScrollbarObject,
						MUIA_Group_Horiz, TRUE,
						End,
					Child, RectangleObject,
						End,
					End,

				Child, BalanceObject, End,
*/
				Child, ScrollgroupObject,
					MUIA_Scrollgroup_Contents, htmlview = (Object *)NewObject(mcc->mcc_Class, NULL,
						End,
					End,

				Child, ColGroup(2),
					MUIA_Group_Spacing, 0,
					Child, /*htmlview = (Object *)*/NewObject(mcc->mcc_Class, NULL,
						VirtualFrame,
						MUIA_HTMLview_DiscreteInput, FALSE,
						MUIA_HTMLview_Contents, "<Body><Center><H1>Hej med dig",
//						MUIA_ContextMenu, TRUE,
						End,
					Child, vscroll = ScrollbarObject,
						MUIA_Prop_UseWinBorder, MUIV_Prop_UseWinBorder_Right,
						End,
					Child, hscroll = ScrollbarObject,
						MUIA_Prop_UseWinBorder, MUIV_Prop_UseWinBorder_Bottom,
						MUIA_Group_Horiz, TRUE,
						End,
					Child, RectangleObject,
						End,
					End,

//				End,

				Child, infotext = InfoTextObject,
					End,

				Child, searchstr = BetterStringObject,
					StringFrame,
					MUIA_CycleChain, TRUE,
					MUIA_ControlChar, 's',
					End,

				End,
			End,
		End)
	{
		DoMethod(searchstr,	MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, htmlview, 3, MUIM_HTMLview_Search, MUIV_TriggerValue, MUIF_HTMLview_Search_Next);

		DoMethod(alien,		MUIM_Notify, MUIA_Pressed, FALSE, htmlview, 3, MUIM_HTMLview_GotoURL, "file://Duff's:T/�98.07.02�/Alien/index.html", NULL);
		DoMethod(mcp,			MUIM_Notify, MUIA_Pressed, FALSE, htmlview, 3, MUIM_HTMLview_GotoURL, "file://Duff's:T/�98.07.02�/Alien/programs/mcp/index.html", NULL);
		DoMethod(scalos,		MUIM_Notify, MUIA_Pressed, FALSE, htmlview, 3, MUIM_HTMLview_GotoURL, "file://Duff's:T/�98.07.02�/Alien/programs/scalos/index.html", NULL);
		DoMethod(sysspeed,	MUIM_Notify, MUIA_Pressed, FALSE, htmlview, 3, MUIM_HTMLview_GotoURL, "file://Duff's:T/�98.07.02�/Alien/programs/sysspeed/index.html", NULL);
		DoMethod(konsollen,	MUIM_Notify, MUIA_Pressed, FALSE, htmlview, 3, MUIM_HTMLview_GotoURL, "file://Duff's:T/�98.07.02�/Konsollen/index1.html", NULL);
		DoMethod(p96,			MUIM_Notify, MUIA_Pressed, FALSE, htmlview, 3, MUIM_HTMLview_GotoURL, "file://Duff's:T/�98.07.02�/~etk10317/Picasso96/Picasso96.html", NULL);

		DoMethod(urlstring, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, htmlview, 2, MUIM_HTMLview_GotoURL, MUIV_TriggerValue);

/*		DoMethod(htmlview, MUIM_Notify, MUIA_Virtgroup_Top,		MUIV_EveryTime, vscroll, 3, MUIM_Set, MUIA_Prop_First,	MUIV_TriggerValue);
		DoMethod(htmlview, MUIM_Notify, MUIA_Height,					MUIV_EveryTime, vscroll, 3, MUIM_Set, MUIA_Prop_Visible,	MUIV_TriggerValue);
		DoMethod(htmlview, MUIM_Notify, MUIA_Virtgroup_Height,	MUIV_EveryTime, vscroll, 3, MUIM_Set, MUIA_Prop_Entries,	MUIV_TriggerValue);
		DoMethod(htmlview, MUIM_Notify, MUIA_Virtgroup_Left,		MUIV_EveryTime, hscroll, 3, MUIM_Set, MUIA_Prop_First,	MUIV_TriggerValue);
		DoMethod(htmlview, MUIM_Notify, MUIA_Width,					MUIV_EveryTime, hscroll, 3, MUIM_Set, MUIA_Prop_Visible,	MUIV_TriggerValue);
		DoMethod(htmlview, MUIM_Notify, MUIA_Virtgroup_Width,		MUIV_EveryTime, hscroll, 3, MUIM_Set, MUIA_Prop_Entries,	MUIV_TriggerValue);
*/
		DoMethod(htmlview, MUIM_Notify, MUIA_HTMLview_CurrentURL, MUIV_EveryTime, infotext, 3, MUIM_Set, MUIA_Text_Contents, MUIV_TriggerValue);
//		DoMethod(htmlview, MUIM_Notify, MUIA_HTMLview_ClickedURL, MUIV_EveryTime, MUIV_Notify_Self, 2, MUIM_HTMLview_GotoURL, MUIV_TriggerValue);
		DoMethod(htmlview, MUIM_Notify, MUIA_HTMLview_ClickedURL, MUIV_EveryTime, MUIV_Notify_Self, 3, MUIM_CallHook, &GotoURLHook, MUIV_TriggerValue);

		DoMethod(vscroll,	MUIM_Notify, MUIA_Prop_First,		MUIV_EveryTime, htmlview, 3, MUIM_Set, MUIA_Virtgroup_Top,		MUIV_TriggerValue);
		DoMethod(hscroll,	MUIM_Notify, MUIA_Prop_First,		MUIV_EveryTime, htmlview, 3, MUIM_Set, MUIA_Virtgroup_Left,		MUIV_TriggerValue);

/*		DoMethod(htmlview2, MUIM_Notify, MUIA_Virtgroup_Top,		MUIV_EveryTime, vscroll2, 3, MUIM_Set, MUIA_Prop_First,	MUIV_TriggerValue);
		DoMethod(htmlview2, MUIM_Notify, MUIA_Height,					MUIV_EveryTime, vscroll2, 3, MUIM_Set, MUIA_Prop_Visible,	MUIV_TriggerValue);
		DoMethod(htmlview2, MUIM_Notify, MUIA_Virtgroup_Height,	MUIV_EveryTime, vscroll2, 3, MUIM_Set, MUIA_Prop_Entries,	MUIV_TriggerValue);
		DoMethod(htmlview2, MUIM_Notify, MUIA_Virtgroup_Left,		MUIV_EveryTime, hscroll2, 3, MUIM_Set, MUIA_Prop_First,	MUIV_TriggerValue);
		DoMethod(htmlview2, MUIM_Notify, MUIA_Width,					MUIV_EveryTime, hscroll2, 3, MUIM_Set, MUIA_Prop_Visible,	MUIV_TriggerValue);
		DoMethod(htmlview2, MUIM_Notify, MUIA_Virtgroup_Width,		MUIV_EveryTime, hscroll2, 3, MUIM_Set, MUIA_Prop_Entries,	MUIV_TriggerValue);
		DoMethod(vscroll2,	MUIM_Notify, MUIA_Prop_First,		MUIV_EveryTime, htmlview2, 3, MUIM_Set, MUIA_Virtgroup_Top,		MUIV_TriggerValue);
		DoMethod(hscroll2,	MUIM_Notify, MUIA_Prop_First,		MUIV_EveryTime, htmlview2, 3, MUIM_Set, MUIA_Virtgroup_Left,		MUIV_TriggerValue);
		DoMethod(htmlview2, MUIM_HTMLview_GotoURL, "Data:Homepage/testpage.html");
*/
//		set(htmlview, MUIA_HTMLview_Gauge, gauge);

		SetAttrs(vscroll, 0x804236ce, TRUE, TAG_DONE);
		SetAttrs(hscroll, 0x804236ce, TRUE, TAG_DONE);

		DoMethod(win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, MUIV_Notify_Application, 2, MUIM_Application_ReturnID, MUIV_Application_ReturnID_Quit);
//		set(app, MUIA_Application_Iconified, TRUE);
		SetAttrs(win, MUIA_Window_Open, TRUE, TAG_DONE);
//		DoMethod(htmlview, MUIM_HTMLview_GotoURL, "file://Duff's:T/�98.09.08�/Log.html", NULL); //Duff's:T/�98.05.31�/aminew.html", NULL);
//		DoMethod(htmlview, MUIM_HTMLview_GotoURL, "file://Duff's:T/�98.11.13�/index.html");
//		DoMethod(htmlview, MUIM_HTMLview_GotoURL, "file://Data:Homepage - old/testpage.html");
//		DoMethod(htmlview, MUIM_HTMLview_GotoURL, "file://Duff's:Data/C-Sources/ImageRender/GIFAnims/AllAnims.HTML");
		DoMethod(htmlview, MUIM_HTMLview_GotoURL, "file://Silvia:Homepage_Real/index.html", NULL);
	}

  RETURN(app);
	return(app);
}

VOID MainLoop (Object *app)
{
	ULONG sigs;

  ENTER();

	while(DoMethod(app, MUIM_Application_NewInput, &sigs) != MUIV_Application_ReturnID_Quit)
	{
		if(sigs)
		{
			sigs = Wait(sigs | SIGBREAKF_CTRL_C);
			if(sigs & SIGBREAKF_CTRL_C)
				break;
		}
	}

  LEAVE();
}

int main(void)
{
  if((IntuitionBase = OpenLibrary("intuition.library", 38)) &&
     GETINTERFACE(IIntuition, struct IntuitionIFace*, IntuitionBase))
  if((GfxBase = OpenLibrary("graphics.library", 38)) &&
     GETINTERFACE(IGraphics, struct GraphicsIFace*, GfxBase))
  if((UtilityBase = OpenLibrary("utility.library", 38)) &&
     GETINTERFACE(IUtility, struct UtilityIFace*, UtilityBase))
  if((LayersBase = OpenLibrary("layers.library", 36)) &&
     GETINTERFACE(ILayers, struct LayersIFace*, LayersBase))
  if((KeymapBase = OpenLibrary("keymap.library", 36)) &&
     GETINTERFACE(IKeymap, struct KeymapIFace*, KeymapBase))
  if((CxBase = OpenLibrary("commodities.library", 36)) &&
     GETINTERFACE(ICommodities, struct CommoditiesIFace*, CxBase))
  if((DiskfontBase = OpenLibrary("diskfont.library", 36)) &&
     GETINTERFACE(IDiskfont, struct DiskfontIFace*, DiskfontBase))
  if((DataTypesBase = OpenLibrary("datatypes.library", 36)) &&
     GETINTERFACE(IDataTypes, struct DataTypesIFace*, DataTypesBase))
  {
    // open cybergraphics.library optional!
    if((CyberGfxBase = OpenLibrary("cybergraphics.library", 40)) &&
      GETINTERFACE(ICyberGfx, struct CyberGfxIFace*, CyberGfxBase))
    { }

    #if defined(DEBUG)
    SetupDebug();
    #endif

    ENTER();

    if((MUIMasterBase = OpenLibrary("muimaster.library", MUIMASTER_VMIN)) &&
      GETINTERFACE(IMUIMaster, struct MUIMasterIFace*, MUIMasterBase))
    {
		  mcc = MUI_CreateCustomClass(NULL, MUIC_Virtgroup, NULL, sizeof(HTMLviewData), ENTRY(_Dispatcher));
			ScrollGroupClass = MUI_CreateCustomClass(NULL, MUIC_Virtgroup, NULL, sizeof(ScrollGroupData), ENTRY(ScrollGroupDispatcher));

  		Object *app;
	  	if(app = BuildApp())
		  {
			  MainLoop(app);
  			MUI_DisposeObject(app);
	  	}

  		MUI_DeleteCustomClass(ScrollGroupClass);
			MUI_DeleteCustomClass(mcc);
    }

    if(MUIMasterBase)
    {
      DROPINTERFACE(IMUIMaster);
      CloseLibrary(MUIMasterBase);
      MUIMasterBase = NULL;
    }

    if(CyberGfxBase)
    {
      DROPINTERFACE(ICyberGfx);
      CloseLibrary(CyberGfxBase);
      CyberGfxBase = NULL;
    }

    if(DataTypesBase)
    {
      DROPINTERFACE(IDataTypes);
      CloseLibrary(DataTypesBase);
      DataTypesBase = NULL;
    }

    if(DiskfontBase)
    {
      DROPINTERFACE(IDiskfont);
      CloseLibrary(DiskfontBase);
      DiskfontBase = NULL;
    }

    if(CxBase)
    {
      DROPINTERFACE(ICommodities);
      CloseLibrary(CxBase);
      CxBase = NULL;
    }

    if(KeymapBase)
    {
      DROPINTERFACE(IKeymap);
      CloseLibrary(KeymapBase);
      KeymapBase = NULL;
    }

    if(LayersBase)
    {
      DROPINTERFACE(ILayers);
      CloseLibrary(LayersBase);
      LayersBase = NULL;
    }

    if(UtilityBase)
    {
      DROPINTERFACE(IUtility);
      CloseLibrary(UtilityBase);
      UtilityBase = NULL;
    }

    if(GfxBase)
    {
      DROPINTERFACE(IGraphics);
      CloseLibrary(GfxBase);
      GfxBase = NULL;
    }

    if(IntuitionBase)
    {
      DROPINTERFACE(IIntuition);
      CloseLibrary((struct Library *)IntuitionBase);
      IntuitionBase = NULL;
    }
	}

  RETURN(0);
  return 0;
}