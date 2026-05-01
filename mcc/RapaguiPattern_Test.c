/***************************************************************************

 HTMLview.mcc - HTMLview MUI Custom Class
 Test program: reproduce the Hollywood/RapaGUI failure pattern.

 RapaGUI's <htmlview> MOAI class creates the gadget with bare
 MUI_NewObject("HTMLview.mcc", TAG_DONE) -- no LoadHook/ImageLoadHook tags
 and no MUIA_HTMLview_Scrollbars -- and then embeds the object inside its
 own MUI Scrollgroup. Contents arrive later via SetAttrs.

 SimpleTest/LibLoad_Test pass MUIA_HTMLview_LoadHook & MUIA_HTMLview_Image-
 LoadHook at creation time, which masks any failure that depends on the
 default-hook code path. This test deliberately omits those tags.

 Optional: pass "-burn N" on the command line to AllocSignal() N bits
 before creating HTMLview, to simulate Hollywood's task running low on
 free signal bits (see Dispatcher.cpp OM_NEW: AllocSignal(-1) returning
 -1 makes the gadget fail creation).

***************************************************************************/

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/dos.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>
#include <libraries/mui.h>
#include <utility/tagitem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__amigaos4__)
struct Library          *IntuitionBase;
struct Library          *MUIMasterBase;
struct Interface        *IntuitionIFace;
struct MUIMasterIFace   *IMUIMaster;
#else
struct Library          *MUIMasterBase;
struct IntuitionBase    *IntuitionBase;
#endif

#include "HTMLview_mcc.h"

static const char *test_html =
    "<html><body>"
    "<h1>RapaGUI Pattern Test</h1>"
    "<p>This window mimics how Hollywood's RapaGUI plugin creates an "
    "HTMLview gadget: bare <code>MUI_NewObject(\"HTMLview.mcc\", TAG_DONE)</code> "
    "with no LoadHook tags, embedded inside a MUI Scrollgroup, with the "
    "Contents pushed in later via SetAttrs.</p>"
    "<p>If this window opened and you can read this paragraph, OM_NEW "
    "succeeded under the bare-creation path. If creation fails on the "
    "host MUI version, the program prints a diagnostic and exits.</p>"
    "<p>Pass <b>-burn N</b> to AllocSignal() N signal bits before creating "
    "HTMLview, to reproduce the failure mode reported with deeply nested "
    "Hollywood/RapaGUI dialogs (each HTMLview consumes one signal bit "
    "unless MUIA_HTMLview_SigBit is supplied).</p>"
    "</body></html>";

static void usage(const char *prog)
{
    printf("usage: %s [-burn N] [-many N]\n", prog);
    printf("  -burn N   AllocSignal() N signal bits before creating HTMLview,\n");
    printf("            simulating a signal-saturated task. N is clamped to 0..16.\n");
    printf("  -many N   create N HTMLview gadgets in one window (stresses the\n");
    printf("            shared-signal path; without sharing, only the first few\n");
    printf("            succeed before the task runs out of signal bits). N is\n");
    printf("            clamped to 1..16.\n");
}

int main(int argc, char **argv)
{
    Object *app = NULL, *win = NULL, *root = NULL;
    Object *htmls[16] = { NULL };
    LONG burn_count = 0;
    LONG many_count = 1;
    LONG burned[16];
    int burned_n = 0;
    int rc = 0;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-burn") && i + 1 < argc)
        {
            burn_count = atol(argv[++i]);
            if (burn_count < 0)  burn_count = 0;
            if (burn_count > 16) burn_count = 16;
        }
        else if (!strcmp(argv[i], "-many") && i + 1 < argc)
        {
            many_count = atol(argv[++i]);
            if (many_count < 1)  many_count = 1;
            if (many_count > 16) many_count = 16;
        }
        else
        {
            usage(argv[0]);
            return 5;
        }
    }

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
        printf("RapaguiPattern_Test: failed to open libraries\n");
        rc = 20;
        goto cleanup;
    }

    printf("RapaguiPattern_Test: muimaster.library v%d\n",
           (int)MUIMasterBase->lib_Version);

    if (burn_count > 0)
    {
        printf("RapaguiPattern_Test: burning %ld signal bits before creation...\n",
               (long)burn_count);
        for (int i = 0; i < burn_count; i++)
        {
            LONG s = AllocSignal(-1);
            if (s == -1)
            {
                printf("  AllocSignal #%d returned -1 (no more free bits)\n", i);
                break;
            }
            burned[burned_n++] = s;
            printf("  AllocSignal #%d -> bit %ld\n", i, (long)s);
        }
    }

    /* Step 1: create -many bare HTMLview gadgets the way RapaGUI does --
     * no LoadHook/ImageLoadHook tags, no scrollbars tag. This is the
     * path that historically fails on Amidon/RapaGUI. */
    printf("RapaguiPattern_Test: creating %ld bare HTMLview(s)...\n",
           (long)many_count);
    for (int i = 0; i < many_count; i++)
    {
        htmls[i] = MUI_NewObject("HTMLview.mcc", TAG_DONE);
        if (!htmls[i])
        {
            printf("  *** OM_NEW #%d returned NULL (Amidon's "
                   "\"Error creating MOAI object HTMLview\")\n", i);
            rc = 10;
            goto cleanup;
        }
        printf("  OM_NEW #%d returned %p (ok)\n", i, htmls[i]);
    }

    /* Step 2: wrap each in MUIC_Scrollgroup like RapaGUI does, then stack
     * them in a vgroup (one window can show several). */
    if (many_count == 1)
    {
        root = MUI_NewObject(MUIC_Scrollgroup,
            MUIA_Scrollgroup_FreeVert,  TRUE,
            MUIA_Scrollgroup_FreeHoriz, TRUE,
            MUIA_Scrollgroup_Contents,  htmls[0],
            TAG_DONE);
    }
    else
    {
        Object *vg = MUI_NewObject(MUIC_Group, TAG_DONE);
        if (vg)
        {
            for (int i = 0; i < many_count; i++)
            {
                Object *sg = MUI_NewObject(MUIC_Scrollgroup,
                    MUIA_Scrollgroup_FreeVert,  TRUE,
                    MUIA_Scrollgroup_FreeHoriz, TRUE,
                    MUIA_Scrollgroup_Contents,  htmls[i],
                    TAG_DONE);
                if (!sg) { MUI_DisposeObject(vg); vg = NULL; break; }
                DoMethod(vg, OM_ADDMEMBER, sg);
            }
        }
        root = vg;
    }
    if (!root)
    {
        printf("  *** Scrollgroup wrapper(s) failed to assemble\n");
        rc = 11;
        goto cleanup;
    }

    app = MUI_NewObject(MUIC_Application,
        MUIA_Application_Title,      (ULONG)"RapaGUI Pattern Test",
        MUIA_Application_Version,    (ULONG)"$VER: RapaguiPattern_Test 1.1 (1.5.2026)",
        MUIA_Application_SingleTask, TRUE,
        MUIA_Application_Window, win = MUI_NewObject(MUIC_Window,
            MUIA_Window_Title,      (ULONG)"HTMLview RapaGUI Pattern",
            MUIA_Window_Width,      640,
            MUIA_Window_Height,     400,
            MUIA_Window_RootObject, root,
            TAG_DONE),
        TAG_DONE);

    if (!app)
    {
        printf("  *** Application/Window failed to assemble\n");
        rc = 12;
        goto cleanup;
    }

    DoMethod(win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE,
             app, 2, MUIM_Application_ReturnID, MUIV_Application_ReturnID_Quit);

    SetAttrs(win, MUIA_Window_Open, TRUE, TAG_DONE);

    {
        ULONG open = 0;
        GetAttr(MUIA_Window_Open, win, &open);
        if (!open)
        {
            printf("  *** Window failed to open\n");
            rc = 13;
            goto cleanup;
        }
    }

    /* Step 3: set Contents AFTER the window is open, the way Amidon does
     * (moai.Set("htmlview_content", "Contents", ...)). */
    for (int i = 0; i < many_count; i++)
        SetAttrs(htmls[i], MUIA_HTMLview_Contents, (ULONG)test_html, TAG_DONE);

    {
        ULONG sigs = 0;
        BOOL  running = TRUE;
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

cleanup:
    /* When an MUI parent's NewObject call fails, MUI auto-disposes any
     * children it received via tags (Window/Scrollgroup/HTMLview here).
     * So we only manually dispose if the FULL Application chain went up
     * (app != NULL) -- otherwise we'd double-dispose root and crash. The
     * window-not-open case still owns 'app', so disposing it is safe. */
    if (app)
        MUI_DisposeObject(app);

    while (burned_n > 0)
        FreeSignal(burned[--burned_n]);

#if defined(__amigaos4__)
    if (MUIMasterBase)
    {
        if (IMUIMaster) DropInterface((struct Interface *)IMUIMaster);
        CloseLibrary(MUIMasterBase);
    }
    if (IntuitionBase)
    {
        if (IntuitionIFace) DropInterface(IntuitionIFace);
        CloseLibrary(IntuitionBase);
    }
#else
    if (MUIMasterBase) CloseLibrary(MUIMasterBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
#endif

    return rc;
}
