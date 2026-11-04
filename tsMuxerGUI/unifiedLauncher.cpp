#include "unifiedLauncher.h"

#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

int tsmuxerEngineMain(int argc, char** argv);

namespace
{
#ifdef _WIN32
bool isValidStdHandle(DWORD which)
{
    HANDLE h = GetStdHandle(which);
    return h != nullptr && h != INVALID_HANDLE_VALUE && GetFileType(h) != FILE_TYPE_UNKNOWN;
}

bool g_attachedToParentConsole = false;
#endif
}  // namespace

bool unifiedShouldRunEngine(int argc, char** argv)
{
    if (argc < 2)
        return false;
    if (std::strcmp(argv[1], "--engine") == 0)
        return true;
    if (std::strcmp(argv[1], "--gui") == 0)
        return false;
#ifdef _WIN32
    // Started from a terminal or a script: behave like the classic tsMuxeR CLI.
    // Started from Explorer (files dropped on the exe): open them in the GUI.
    if (isValidStdHandle(STD_OUTPUT_HANDLE))
        return true;
    g_attachedToParentConsole = AttachConsole(ATTACH_PARENT_PROCESS) != 0;
    return g_attachedToParentConsole;
#else
    return true;
#endif
}

int unifiedRunEngine(int argc, char** argv)
{
#ifdef _WIN32
    // A GUI-subsystem process has no stdio when run from a console unless the
    // output was redirected, so route it to the parent console ourselves.
    if (g_attachedToParentConsole)
    {
        if (!isValidStdHandle(STD_OUTPUT_HANDLE))
            freopen("CONOUT$", "w", stdout);
        if (!isValidStdHandle(STD_ERROR_HANDLE))
            freopen("CONOUT$", "w", stderr);
        std::fputs("\n", stdout);
    }
#endif
    const int rc = tsmuxerEngineMain(argc, argv);
    std::fflush(stdout);
    std::fflush(stderr);
    return rc;
}
