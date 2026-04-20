
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/dos.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>
#include <libraries/mui.h>
#include <utility/tagitem.h>
#include <stdio.h>

#if defined(__amigaos4__)
struct Library          *IntuitionBase;
struct Library          *MUIMasterBase;
struct Interface        *IntuitionIFace;
struct MUIMasterIFace   *IMUIMaster;
#else
struct Library          *MUIMasterBase;
struct IntuitionBase    *IntuitionBase;
#endif
struct Library *UtilityBase;

#if defined(DEBUG)
extern void kprintf(const char *fmt, ...);
#else
static void kprintf(const char *fmt, ...) { (void)fmt; }
#endif

#include "HTMLview_mcc.h"
#include "net_hook/htmlview_nethook.h"

static struct Hook ImageLoadHook;

/* Keep this HTML blob self-contained; PROGDIR:test.png is copied into the
   binary directory by the Makefile, and the HTTP entries exercise the
   bsdsocket/chunked path in the shared hook. */
static const char *test_html =
    "<html><body>"
    "<h1>HTMLView MCC Test Suite</h1>"
    "<p>This is a comprehensive test demonstrating HTMLview features.</p>"

    "<h2>Images - Local Files</h2>"
    "<p>Local image (PROGDIR:test.png):</p>"
    "<p><img src=\"PROGDIR:test.png\" alt=\"Local test image\"></p>"
    "<p>Same image sized to 64x64:</p>"
    "<p><img src=\"PROGDIR:test.png\" alt=\"Sized image\" width=\"64\" height=\"64\"></p>"
    "<p>Centered with border:</p>"
    "<p align=\"center\"><img src=\"PROGDIR:test.png\" alt=\"Centered\" width=\"96\" height=\"96\" border=\"2\"></p>"

    "<h2>Images - Network (HTTP)</h2>"
    "<p>These need bsdsocket.library and a working network stack:</p>"
    "<p><img src=\"http://aminet.net/pics/aminet.png\" alt=\"Aminet Logo (HTTP)\"></p>"
    "<p>Exercises http-&gt;https redirect (requires AmiSSL):</p>"
    "<p><img src=\"http://www.amigaworld.net/themes/default/images/logo-top.gif\" alt=\"AmigaWorld (HTTP redirect)\"></p>"

    "<h2>Images - Network (HTTPS)</h2>"
    "<p>Direct TLS fetch. Needs amisslmaster.library + amissl.library installed:</p>"
    "<p><img src=\"https://aminet.net/pics/aminet.png\" alt=\"Aminet Logo (HTTPS)\"></p>"

    "<h2>Text Formatting</h2>"
    "<p><b>Bold text</b>, <i>italic text</i>, "
    "<u>underlined text</u>, <s>strikethrough text</s>, "
    "<tt>monospace text</tt></p>"
    "<p>Combination: <b><i><u>Bold Italic Underlined</u></i></b></p>"

    "<h2>Links</h2>"
    "<p>Visit <a href=\"http://aminet.net\">Aminet</a> or "
    "<a href=\"http://os4depot.net\">OS4Depot</a>.</p>"
    "<p>Links can have <b>bold</b> text inside <a href=\"#\">like this</a>.</p>"

    "<h2>Headings</h2>"
    "<h1>H1</h1><h2>H2</h2><h3>H3</h3>"
    "<h4>H4</h4><h5>H5</h5><h6>H6</h6>"

    "<h2>Lists</h2>"
    "<ul><li>First</li><li>Second</li><li>Third with <b>bold</b></li></ul>"
    "<ol><li>Step one</li><li>Step two</li><li>Step three</li></ol>"
    "<dl>"
    "<dt>HTML</dt><dd>HyperText Markup Language</dd>"
    "<dt>CSS</dt><dd>Cascading Style Sheets</dd>"
    "</dl>"

    "<h2>Tables</h2>"
    "<table border=\"1\" width=\"100%\">"
    "<tr><th>Column 1</th><th>Column 2</th><th>Column 3</th></tr>"
    "<tr><td>Row 1</td><td>Cell 2</td><td>Cell 3</td></tr>"
    "<tr><td>Row 2</td><td><b>bold</b></td><td><a href=\"#\">link</a></td></tr>"
    "</table>"

    "<h2>Forms</h2>"
    "<form action=\"submit\">"
    "<p>Text: <input type=\"text\" name=\"t\" value=\"Sample\"></p>"
    "<p>Pass: <input type=\"password\" name=\"p\"></p>"
    "<p><input type=\"checkbox\" checked> Option A "
    "<input type=\"checkbox\"> Option B</p>"
    "<p><input type=\"radio\" name=\"r\" checked> Yes "
    "<input type=\"radio\" name=\"r\"> No</p>"
    "<select><option>One</option><option selected>Two</option></select>"
    "<textarea rows=\"3\" cols=\"40\">Default text.</textarea>"
    "<p><input type=\"submit\" value=\"Submit\"> "
    "<input type=\"reset\" value=\"Reset\"></p>"
    "</form>"

    "<h2>Preformatted</h2>"
    "<p>Inline: <code>printf(\"hi\");</code></p>"
    "<pre>function example() {\n    return 42;\n}</pre>"
    "<hr><p>Line 1<br>Line 2<br>Line 3</p>"

    "<h2>Font Styles</h2>"
    "<p><big>Big</big>, <small>small</small>, "
    "<strong>strong</strong>, <em>em</em></p>"
    "<p><font color=\"red\">Red</font>, "
    "<font color=\"green\">Green</font>, "
    "<font color=\"blue\">Blue</font></p>"

    "<h2>Entities</h2>"
    "<p>&copy; &reg; &trade; &lt; &gt; &amp; &larr; &rarr;</p>"

    "<p align=\"center\"><b>End of Test Suite</b></p>"
    "</body></html>";

int main(void)
{
    Object *app, *win, *html;

#if defined(__amigaos4__)
    IntuitionBase = OpenLibrary("intuition.library", 39);
    if (IntuitionBase)
        IntuitionIFace = GetInterface(IntuitionBase, "main", 1, NULL);
    MUIMasterBase = OpenLibrary("muimaster.library", 19);
    if (MUIMasterBase)
        IMUIMaster = (struct MUIMasterIFace *)GetInterface(MUIMasterBase, "main", 1, NULL);
    if (!IntuitionBase || !MUIMasterBase || !IntuitionIFace || !IMUIMaster)
#else
    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39);
    MUIMasterBase = OpenLibrary("muimaster.library", 19);
    if (!IntuitionBase || !MUIMasterBase)
#endif
    {
        kprintf("SimpleTest: Failed to open libraries\n");
        return 20;
    }

    /* The nethook library opens bsdsocket/amisslmaster/amissl on demand
       from the HTMLview decoder task; nothing to do here beyond wiring
       the hook pointer into MUI. */
    HTMLviewNet_InitHook(&ImageLoadHook);

    app = MUI_NewObject(MUIC_Application,
        MUIA_Application_Title      , "HTMLView Simple Test",
        MUIA_Application_Version    , (ULONG)"$VER: SimpleTest 1.2 (18.4.2026)",
        MUIA_Application_SingleTask , TRUE,
        MUIA_Application_Window     , win = MUI_NewObject(MUIC_Window,
            MUIA_Window_Title,  (ULONG)"HTMLView Test Window",
            MUIA_Window_Width,  640,
            MUIA_Window_Height, 480,
            MUIA_Window_RootObject, MUI_NewObject(MUIC_Scrollgroup,
                MUIA_Scrollgroup_FreeVert,  TRUE,
                MUIA_Scrollgroup_FreeHoriz, TRUE,
                MUIA_Scrollgroup_Contents,  html = MUI_NewObject("HTMLview.mcc",
                    MUIA_Background,               MUII_TextBack,
                    MUIA_HTMLview_ImageLoadHook,   (ULONG)&ImageLoadHook,
                    MUIA_HTMLview_LoadHook,        (ULONG)&ImageLoadHook,
                    TAG_DONE),
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

        SetAttrs(html, MUIA_HTMLview_Contents, (ULONG)test_html, TAG_DONE);

        ULONG open = 0;
        GetAttr(MUIA_Window_Open, win, &open);
        if (open)
        {
             DoMethod(win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE,
                      app, 2, MUIM_Application_ReturnID, MUIV_Application_ReturnID_Quit);

             ULONG sigs = 0;
             BOOL running = TRUE;
             while (running)
             {
                 ULONG id = DoMethod(app, MUIM_Application_NewInput, &sigs);
                 if (id == MUIV_Application_ReturnID_Quit) running = FALSE;

                 if (running && sigs)
                 {
                    ULONG got = Wait(sigs | SIGBREAKF_CTRL_C);
                    if (got & SIGBREAKF_CTRL_C) running = FALSE;
                 }
             }
        }
        else
        {
             kprintf("SimpleTest: Window failed to open.\n");
        }

        MUI_DisposeObject(app);
    }

    if (MUIMasterBase)
    {
#if defined(__amigaos4__)
        if (IMUIMaster) DropInterface((struct Interface *)IMUIMaster);
#endif
        CloseLibrary(MUIMasterBase);
    }
    if (IntuitionBase)
    {
#if defined(__amigaos4__)
        if (IntuitionIFace) DropInterface(IntuitionIFace);
        CloseLibrary(IntuitionBase);
#else
        CloseLibrary((struct Library *)IntuitionBase);
#endif
    }

    return 0;
}
