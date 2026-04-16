#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/dos.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>
#include <libraries/mui.h>
#include <intuition/intuition.h>

#ifndef MUIA_HTMLview_Contents
#define MUIA_HTMLview_Contents 0xad003005
#endif

#if defined(__amigaos4__)
struct Library *IntuitionBase;
struct Library *MUIMasterBase;
struct IntuitionIFace *IIntuition;
struct MUIMasterIFace *IMUIMaster;
#else
struct IntuitionBase *IntuitionBase;
struct DosLibrary *DOSBase;
struct Library *MUIMasterBase;
#endif

static void ShowError(char *msg)
{
    struct EasyStruct es = {
        sizeof(struct EasyStruct),
        0,
        (UBYTE *)"LibLoad_Test Error",
        (UBYTE *)msg,
        (UBYTE *)"OK"
    };
    EasyRequestArgs(NULL, &es, 0, NULL);
}

static void LogMsg(BPTR fh, char *msg)
{
    if (fh) VFPrintf(fh, "%s\n", (APTR)msg);
}

int main(int argc, char **argv)
{
    Object *app = NULL;
    Object *win = NULL;
    Object *html = NULL;
    int result = 0;
    BPTR fh = NULL;

    DOSBase = (struct DosLibrary *)OpenLibrary("dos.library", 0);
    if (!DOSBase) return 20;

    fh = Open("t:libload_test.txt", MODE_NEWFILE);

    LogMsg(fh, "LibLoad_Test: Starting...");

    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39);
    if (!IntuitionBase)
    {
        LogMsg(fh, "Failed intuition.library");
        if (fh) Close(fh);
        ShowError("Failed to open intuition.library");
        return 20;
    }
    LogMsg(fh, "Opened intuition.library OK");

    MUIMasterBase = OpenLibrary("muimaster.library", 19);
    if (!MUIMasterBase)
    {
        LogMsg(fh, "Failed muimaster.library");
        if (fh) Close(fh);
        ShowError("Failed to open muimaster.library");
        goto cleanup_intuition;
    }
    LogMsg(fh, "Opened muimaster.library OK");
    LogMsg(fh, "MUI will find HTMLview.mcc via its class path (MUI:Libs/mui)");

    LogMsg(fh, "Creating MUI application...");
    app = MUI_NewObject(MUIC_Application,
        MUIA_Application_Title, (ULONG)"LibLoad Test",
        MUIA_Application_Version, (ULONG)"$VER: LibLoad_Test 1.0",
        MUIA_Application_SingleTask, TRUE,
        MUIA_Application_Window, win = MUI_NewObject(MUIC_Window,
            MUIA_Window_Title, (ULONG)"HTMLView Library Load Test",
            MUIA_Window_RootObject, html = MUI_NewObject("HTMLview.mcc",
                MUIA_Background, MUII_TextBack,
                TAG_DONE),
            TAG_DONE),
        TAG_DONE);

    if (!app)
    {
        LogMsg(fh, "FAILED to create Application!");
        LogMsg(fh, "MUI could not find HTMLview.mcc in its class path.");
        LogMsg(fh, "Make sure HTMLview.mcc is in MUI:Libs/mui/");
        if (fh) Close(fh);
        ShowError("LibLoad_Test: Failed!\nMUI cannot find HTMLview.mcc.\nMake sure it is in MUI:Libs/mui/");
        result = 20;
        goto cleanup_muimaster;
    }

    LogMsg(fh, "Application created OK - MUI found HTMLview.mcc!");

    SetAttrs(html, MUIA_HTMLview_Contents,
        (ULONG)"<html><body><h1>LibLoad Test</h1><p>If you see this, HTMLview.mcc loads correctly!</p></body></html>",
        TAG_DONE);

    LogMsg(fh, "Opening window...");
    SetAttrs(win, MUIA_Window_Open, TRUE, TAG_DONE);

    ULONG open = 0;
    GetAttr(MUIA_Window_Open, win, &open);
    if (open)
    {
        LogMsg(fh, "TEST PASSED!");
        if (fh) Close(fh);
        ShowError("LibLoad_Test: TEST PASSED!\nHTMLview.mcc loaded successfully!");
    }
    else
    {
        LogMsg(fh, "Window failed to open");
        if (fh) Close(fh);
        ShowError("Window failed to open.");
    }

    Delay(50);
    SetAttrs(win, MUIA_Window_Open, FALSE, TAG_DONE);

cleanup_htmlview:
    if (app) MUI_DisposeObject(app);

cleanup_muimaster:
    if (MUIMasterBase) CloseLibrary(MUIMasterBase);

cleanup_intuition:
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    if (DOSBase) CloseLibrary((struct Library *)DOSBase);

    return result;
}
