
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/dos.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>
#include <libraries/mui.h>
#include <intuition/intuition.h>
#include <utility/tagitem.h>
#include <stdio.h>

#if defined(__amigaos4__)
struct Library        *IntuitionBase;
struct Library        *MUIMasterBase;
struct IntuitionIFace *IIntuition;
struct MUIMasterIFace *IMUIMaster;
#else
struct IntuitionBase *IntuitionBase;
struct Library       *MUIMasterBase;
#endif

#include "HTMLview_mcc.h"
#include "net_hook/htmlview_nethook.h"

enum {
    MSG_OPEN_HTMLVIEW = 1,
    MSG_QUIT          = 2,
};

/* Same HTML as SimpleTest so the two programs cover identical capability
   surface -- the difference being when the HTMLview window materialises
   (dynamic load vs. up-front creation). */
static const char *test_html =
    "<html><body>"
    "<h1>HTMLView MCC Test Suite</h1>"
    "<p>This is the dynamic-load variant. The MCC is opened only when you "
    "click the button, exercising the MUI class-path lookup.</p>"

    "<h2>Images - Local Files</h2>"
    "<p>Local image (PROGDIR:test.png):</p>"
    "<p><img src=\"PROGDIR:test.png\" alt=\"Local test image\"></p>"
    "<p>Sized: <img src=\"PROGDIR:test.png\" alt=\"Sized\" width=\"64\" height=\"64\"></p>"
    "<p align=\"center\"><img src=\"PROGDIR:test.png\" alt=\"Centered\" width=\"96\" height=\"96\" border=\"2\"></p>"

    "<h2>Images - Network (HTTP)</h2>"
    "<p>Network images require bsdsocket.library:</p>"
    "<p><img src=\"http://aminet.net/pics/aminet.png\" alt=\"Aminet Logo (HTTP)\"></p>"
    "<p>Exercises http-&gt;https redirect (requires AmiSSL):</p>"
    "<p><img src=\"http://www.amigaworld.net/themes/default/images/logo-top.gif\" alt=\"AmigaWorld (HTTP redirect)\"></p>"

    "<h2>Images - Network (HTTPS)</h2>"
    "<p>Direct TLS fetch. Needs amisslmaster.library + amissl.library installed:</p>"
    "<p><img src=\"https://aminet.net/pics/aminet.png\" alt=\"Aminet Logo (HTTPS)\"></p>"

    "<h2>Text</h2>"
    "<p><b>Bold</b>, <i>italic</i>, <u>under</u>, <s>strike</s>, <tt>tt</tt></p>"
    "<p>Visit <a href=\"http://aminet.net\">Aminet</a> or "
    "<a href=\"http://os4depot.net\">OS4Depot</a>.</p>"

    "<h2>Headings</h2>"
    "<h1>H1</h1><h2>H2</h2><h3>H3</h3><h4>H4</h4><h5>H5</h5><h6>H6</h6>"

    "<h2>Lists</h2>"
    "<ul><li>First</li><li>Second with <b>bold</b></li></ul>"
    "<ol><li>Step one</li><li>Step two</li></ol>"
    "<dl><dt>MCC</dt><dd>MUI Custom Class</dd></dl>"

    "<h2>Tables</h2>"
    "<table border=\"1\" width=\"100%\">"
    "<tr><th>A</th><th>B</th><th>C</th></tr>"
    "<tr><td>1</td><td>2</td><td>3</td></tr>"
    "<tr><td>4</td><td><b>bold</b></td><td><a href=\"#\">link</a></td></tr>"
    "</table>"

    "<h2>Forms</h2>"
    "<form>"
    "<p>Text: <input type=\"text\" value=\"hi\"></p>"
    "<p><input type=\"checkbox\" checked> a "
    "<input type=\"checkbox\"> b</p>"
    "<p><input type=\"radio\" name=\"g\" checked> yes "
    "<input type=\"radio\" name=\"g\"> no</p>"
    "<select><option>one</option><option selected>two</option></select>"
    "<textarea rows=\"2\" cols=\"30\">default</textarea>"
    "<p><input type=\"submit\"> <input type=\"reset\"></p>"
    "</form>"

    "<h2>Preformatted</h2>"
    "<pre>int main() {\n    return 0;\n}</pre>"

    "<h2>Colors &amp; Entities</h2>"
    "<p><font color=\"red\">Red</font>, "
    "<font color=\"green\">Green</font>, "
    "<font color=\"blue\">Blue</font></p>"
    "<p>&copy; &reg; &trade; &lt; &gt; &amp;</p>"

    "<p align=\"center\"><b>End of Test Suite</b></p>"
    "</body></html>";

int main(int argc, char **argv)
{
    Object *app = NULL, *mainWin = NULL, *htmlWin = NULL, *htmlObj = NULL, *btnOpen = NULL;
    struct Hook loadHook;
    ULONG signals = 0;
    (void)argc;
    (void)argv;

    /* bsdsocket / AmiSSL are opened on demand by the nethook library from
       the HTMLview decoder task -- nothing to do here at startup. */

#if defined(__amigaos4__)
    IntuitionBase = OpenLibrary("intuition.library", 39);
    if (IntuitionBase)
        IIntuition = (struct IntuitionIFace *)GetInterface(IntuitionBase, "main", 1, NULL);
    MUIMasterBase = OpenLibrary("muimaster.library", 19);
    if (MUIMasterBase)
        IMUIMaster = (struct MUIMasterIFace *)GetInterface(MUIMasterBase, "main", 1, NULL);
    if (!IntuitionBase || !MUIMasterBase || !IIntuition || !IMUIMaster)
#else
    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39);
    MUIMasterBase = OpenLibrary("muimaster.library", 19);
    if (!IntuitionBase || !MUIMasterBase)
#endif
    {
        goto cleanup;
    }

    HTMLviewNet_InitHook(&loadHook);

    app = MUI_NewObject(MUIC_Application,
        MUIA_Application_Title,      (ULONG)"LibLoad Test",
        MUIA_Application_Version,    (ULONG)"$VER: LibLoad_Test 1.1 (18.4.2026)",
        MUIA_Application_SingleTask, TRUE,
        MUIA_Application_Window, mainWin = MUI_NewObject(MUIC_Window,
            MUIA_Window_Title,  (ULONG)"HTMLView Library Load Test",
            MUIA_Window_Width,  400,
            MUIA_Window_Height, 200,
            MUIA_Window_RootObject, MUI_NewObject(MUIC_Group,
                MUIA_Group_Child, MUI_NewObject(MUIC_Text,
                    MUIA_Text_Contents, (ULONG)
                        "\nLibLoad Test Program\n\n"
                        "Demonstrates loading HTMLview.mcc dynamically.\n"
                        "Click the button to open the test document.\n",
                    MUIA_Text_PreParse, (ULONG)"\33c",
                    TAG_DONE),
                MUIA_Group_Child, btnOpen = MUI_NewObject(MUIC_Text,
                    MUIA_Frame,         MUIV_Frame_Button,
                    MUIA_Text_Contents, (ULONG)"[ Open HTMLView Test ]",
                    MUIA_Text_PreParse, (ULONG)"\33c",
                    MUIA_InputMode,     MUIV_InputMode_RelVerify,
                    TAG_DONE),
                TAG_DONE),
            TAG_DONE),
        TAG_DONE);

    if (!app) goto cleanup;

    DoMethod(btnOpen, MUIM_Notify, MUIA_Selected, MUIV_EveryTime,
        app, 2, MUIM_Application_ReturnID, MSG_OPEN_HTMLVIEW);

    DoMethod(mainWin, MUIM_Notify, MUIA_Window_CloseRequest, TRUE,
        app, 2, MUIM_Application_ReturnID, MSG_QUIT);

    SetAttrs(mainWin, MUIA_Window_Open, TRUE, TAG_DONE);

    BOOL running = TRUE;
    while (running)
    {
        ULONG id = DoMethod(app, MUIM_Application_NewInput, &signals);

        if (id == MSG_OPEN_HTMLVIEW && !htmlWin)
        {
            /* Create the MCC with only the hooks -- the image decoder needs
               a screen, which is only known once the window is open. Setting
               Contents at creation time queues image decode with no screen
               and the decoded bitmaps end up unused. We mirror SimpleTest:
               open the window first, THEN push Contents via SetAttrs. */
            htmlObj = MUI_NewObject("HTMLview.mcc",
                MUIA_HTMLview_ImageLoadHook, (ULONG)&loadHook,
                MUIA_HTMLview_LoadHook,      (ULONG)&loadHook,
                TAG_DONE);

            if (htmlObj)
            {
                htmlWin = MUI_NewObject(MUIC_Window,
                    MUIA_Window_Title,  (ULONG)"HTMLView Test Content",
                    MUIA_Window_Width,  800,
                    MUIA_Window_Height, 600,
                    MUIA_Window_RootObject, MUI_NewObject(MUIC_Scrollgroup,
                        MUIA_Scrollgroup_FreeVert,  TRUE,
                        MUIA_Scrollgroup_FreeHoriz, TRUE,
                        MUIA_Scrollgroup_Contents,  htmlObj,
                        TAG_DONE),
                    TAG_DONE);

                if (htmlWin)
                {
                    DoMethod(htmlWin, MUIM_Notify, MUIA_Window_CloseRequest, TRUE,
                        app, 2, MUIM_Application_ReturnID, MSG_QUIT);
                    DoMethod(app, OM_ADDMEMBER, htmlWin);
                    SetAttrs(htmlWin, MUIA_Window_Open, TRUE, TAG_DONE);

                    ULONG open = 0;
                    GetAttr(MUIA_Window_Open, htmlWin, &open);
                    if (open)
                        SetAttrs(htmlObj, MUIA_HTMLview_Contents, (ULONG)test_html, TAG_DONE);
                }
            }
        }
        else if (id == MSG_QUIT)
        {
            running = FALSE;
        }

        if (running && signals)
        {
            ULONG got = Wait(signals | SIGBREAKF_CTRL_C);
            if (got & SIGBREAKF_CTRL_C) running = FALSE;
        }
    }

    if (htmlWin)  SetAttrs(htmlWin, MUIA_Window_Open, FALSE, TAG_DONE);
    if (mainWin)  SetAttrs(mainWin, MUIA_Window_Open, FALSE, TAG_DONE);

cleanup:
    if (app) MUI_DisposeObject(app);

#if defined(__amigaos4__)
    if (MUIMasterBase)
    {
        if (IMUIMaster) DropInterface((struct Interface *)IMUIMaster);
        CloseLibrary(MUIMasterBase);
    }
    if (IntuitionBase)
    {
        if (IIntuition) DropInterface((struct Interface *)IIntuition);
        CloseLibrary(IntuitionBase);
    }
#else
    if (MUIMasterBase) CloseLibrary(MUIMasterBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
#endif

    return 0;
}
