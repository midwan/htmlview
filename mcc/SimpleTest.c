
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>
#include <libraries/mui.h>
#include <stdio.h>

#if defined(__amigaos4__)
#define kprintf(...) ((void)0)
#else
extern void kprintf(const char *fmt, ...);
#endif

struct Library *MUIMasterBase;
#if defined(__amigaos4__)
struct Library *IntuitionBase;
#else
struct IntuitionBase *IntuitionBase;
#endif
struct Library *UtilityBase;

// Simple MUI macros if missing
#ifndef MUI_Set
#define MUI_Set(o,a,v) SetAttrs(o,a,v,TAG_DONE)
#endif
#ifndef MUI_GetVal
#define MUI_GetVal(o,a) ({ ULONG v; GetAttr(a,o,&v); v; })
#endif

int main(void)
{
    Object *app, *win, *html;

    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39);
    MUIMasterBase = OpenLibrary("muimaster.library", 19);

    if (!IntuitionBase || !MUIMasterBase)
    {
        kprintf("SimpleTest: Failed to open libraries\n");
        return 20;
    }

    app = MUI_NewObject(MUIC_Application,
        MUIA_Application_Title      , "HTMLView Simple Test",
        MUIA_Application_Version    , "$VER: SimpleTest 1.0 (15.12.2025)",
        MUIA_Application_SingleTask , TRUE,
        MUIA_Application_Window     , win = MUI_NewObject(MUIC_Window,
            MUIA_Window_Title, "HTMLView Test Window",
            MUIA_Window_RootObject, html = MUI_NewObject("HTMLview.mcc",
                MUIA_Background, MUII_TextBack,
                TAG_DONE),
            TAG_DONE),
        TAG_DONE);

    if (!app)
    {
        kprintf("SimpleTest: Failed to create Application object\n");
    }
    else
    {
        SetAttrs(win, MUIA_Window_Open, TRUE, TAG_DONE);
        
        static const char *test_html = 
            "<html><body>"
            "<h1>HTMLView Test</h1>"
            "<p>This is a <b>bold</b> paragraph.</p>"
            "<p>This is an <i>italic</i> paragraph.</p>"
            "<p>This is a <a href=\"http://google.com\">link</a>.</p>"
            "</body></html>";

        // Correct ID for HTMLview contents
        #define MUIA_HTMLview_Contents 0xad003005
        SetAttrs(html, MUIA_HTMLview_Contents, test_html, TAG_DONE);

        ULONG val = 0;
        GetAttr(MUIA_Window_Open, win, &val);
        if (val)
        {
             ULONG sigs = 0;
             BOOL running = TRUE;
             
             DoMethod(win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, 
                      app, 2, MUIM_Application_ReturnID, MUIV_Application_ReturnID_Quit);
             
             while(running)
             {
                 ULONG id = DoMethod(app, MUIM_Application_NewInput, &sigs);
                 
                 if (id == MUIV_Application_ReturnID_Quit) {
                     running = FALSE;
                 }
                 
                 if (running && sigs) {
                    ULONG got = Wait(sigs | SIGBREAKF_CTRL_C);
                    if (got & SIGBREAKF_CTRL_C) {
                        running = FALSE;
                    }
                 }
             }
        }
        else
        {
             kprintf("SimpleTest: Window failed to open.\n");
        }
        
        MUI_DisposeObject(app);
    }
    
    if (MUIMasterBase) CloseLibrary(MUIMasterBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    
    return 0;
}
