/*
 * dialog.c - open_url, message boxes and file dialogs.
 *
 * Windows: the native dialogs (comdlg32 and ole32 are loaded at run time, so the
 * executables import no more than they did).
 * Web: alert() and confirm() for the boxes, window.open for URLs.
 * Other POSIX systems: zenity or kdialog, started without a shell.
 */
#include "platform.h"
#include "error_internal.h"

#include <stdlib.h>
#include <string.h>

static bool bad_url(const char *url)
{
    if (!url || !url[0] || url[0] == '-')
        return true;
    for (const unsigned char *p = (const unsigned char *)url; *p; p++)
        if (*p < 0x20 || *p == 0x7F)
            return true;
    return false;
}

static bool store(const char *text, char *out, size_t cap)
{
    size_t n = strlen(text);
    if (!out || n + 1 > cap)
        return error_set("the path does not fit in the buffer");
    memcpy(out, text, n + 1);
    return true;
}

#if defined(__EMSCRIPTEN__)
/* ========================================================================== */
#include <emscripten.h>

bool message_box(PlatformWindow *parent, MessageKind kind, const char *title, const char *message)
{
    (void)parent, (void)kind;
    platform_clear_error();
    EM_ASM({ alert(UTF8ToString($0) + "\n\n" + UTF8ToString($1)); }, title ? title : "", message ? message : "");
    return true;
}

int confirm_box(PlatformWindow *parent, const char *title, const char *message)
{
    (void)parent;
    platform_clear_error();
    return EM_ASM_INT({ return confirm(UTF8ToString($0) + "\n\n" + UTF8ToString($1)) ? 1 : 0; }, title ? title : "",
                      message ? message : "");
}

static bool unsupported(void)
{
    return error_set("file dialogs are not available on the web");
}
bool dialog_open_file(PlatformWindow *p, const char *t, const char *d, const FileFilter *f, int n, char *o, size_t c)
{
    (void)p, (void)t, (void)d, (void)f, (void)n, (void)o, (void)c;
    return unsupported();
}
bool dialog_save_file(PlatformWindow *p, const char *t, const char *d, const FileFilter *f, int n, char *o, size_t c)
{
    (void)p, (void)t, (void)d, (void)f, (void)n, (void)o, (void)c;
    return unsupported();
}
bool dialog_pick_folder(PlatformWindow *p, const char *t, const char *d, char *o, size_t c)
{
    (void)p, (void)t, (void)d, (void)o, (void)c;
    return unsupported();
}
int dialog_open_files(PlatformWindow *p, const char *t, const char *d, const FileFilter *f, int n, char *o, size_t c)
{
    (void)p, (void)t, (void)d, (void)f, (void)n, (void)o, (void)c;
    unsupported();
    return 0;
}

bool open_url(const char *url)
{
    if (bad_url(url))
        return error_set("invalid URL");
    EM_ASM({ window.open(UTF8ToString($0), "_blank"); }, url);
    return true;
}

#elif defined(_WIN32)
/* ========================================================================== */
#include "win32_util.h"

#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

static HWND parent_hwnd(PlatformWindow *parent)
{
    return parent ? (HWND)window_native_handle(parent, NATIVE_WINDOW) : NULL;
}

static bool widen_or_fail(const char *utf8, wchar_t *out, size_t cap, const char *what)
{
    if (!win32_widen(utf8 ? utf8 : "", out, cap))
        return error_set("%s is too long", what);
    return true;
}

bool message_box(PlatformWindow *parent, MessageKind kind, const char *title, const char *message)
{
    wchar_t wt[256], wm[2048];
    platform_clear_error();
    if (!widen_or_fail(title, wt, 256, "the title") || !widen_or_fail(message, wm, 2048, "the message"))
        return false;
    UINT icon = kind == MESSAGE_ERROR ? MB_ICONERROR : kind == MESSAGE_WARNING ? MB_ICONWARNING : MB_ICONINFORMATION;
    return MessageBoxW(parent_hwnd(parent), wm, wt, MB_OK | icon) != 0 || error_set("cannot show the message box");
}

int confirm_box(PlatformWindow *parent, const char *title, const char *message)
{
    wchar_t wt[256], wm[2048];
    platform_clear_error();
    if (!widen_or_fail(title, wt, 256, "the title") || !widen_or_fail(message, wm, 2048, "the message"))
        return -1;
    int r = MessageBoxW(parent_hwnd(parent), wm, wt, MB_YESNO | MB_ICONQUESTION);
    if (r == IDYES)
        return 1;
    if (r == IDNO)
        return 0;
    error_set("cannot show the message box");
    return -1;
}

/* comdlg32 and ole32 are loaded when first needed. */
typedef BOOL(WINAPI *PfnOpenSave)(LPOPENFILENAMEW);
typedef void(WINAPI *PfnCoTaskMemFree)(LPVOID);

static SharedLibrary *g_comdlg, *g_ole32;

static void *comdlg_fn(const char *name)
{
    if (!g_comdlg)
        g_comdlg = library_open("comdlg32.dll");
    return g_comdlg ? library_symbol(g_comdlg, name) : NULL;
}

/* "Images\0*.png;*.jpg\0All\0*.*\0\0" */
static wchar_t *build_filter(const FileFilter *filters, int count)
{
    size_t cap = 16;
    for (int i = 0; i < count; i++)
        cap += strlen(filters[i].name) * 2 + strlen(filters[i].patterns) * 2 + 4;
    wchar_t *buf = calloc(cap + 8, sizeof(wchar_t));
    if (!buf)
        return NULL;
    size_t pos = 0;
    for (int i = 0; i < count; i++)
    {
        int n = MultiByteToWideChar(CP_UTF8, 0, filters[i].name, -1, buf + pos, (int)(cap - pos));
        pos += n > 0 ? (size_t)n : 1;
        n = MultiByteToWideChar(CP_UTF8, 0, filters[i].patterns, -1, buf + pos, (int)(cap - pos));
        pos += n > 0 ? (size_t)n : 1;
    }
    if (count == 0)
    {
        wcscpy(buf, L"All files");
        wcscpy(buf + 10, L"*.*");
    }
    return buf;
}

/* One implementation behind open, open-many and save. Returns the number of paths
   written to `out` (0 on cancel or failure). */
static int file_dialog(PlatformWindow *parent, bool save, bool many, const char *title, const char *default_path,
                       const FileFilter *filters, int count, char *out, size_t cap)
{
    platform_clear_error();
    PfnOpenSave fn = (PfnOpenSave)comdlg_fn(save ? "GetSaveFileNameW" : "GetOpenFileNameW");
    if (!fn)
        return error_set("the file dialog is not available"), 0;

    const size_t FILE_CAP = 32768;
    wchar_t *file = calloc(FILE_CAP, sizeof(wchar_t));
    wchar_t *filter = build_filter(filters, count);
    wchar_t wtitle[256] = L"", wdir[MAX_PATH * 2] = L"";
    if (!file || !filter)
    {
        free(file);
        free(filter);
        return error_set("out of memory"), 0;
    }
    if (title)
        win32_widen(title, wtitle, 256);

    /* default_path: a folder to start in, or a file whose name is pre-filled */
    if (default_path && default_path[0])
    {
        wchar_t wp[MAX_PATH * 2];
        if (win32_widen(default_path, wp, MAX_PATH * 2))
        {
            for (wchar_t *c = wp; *c; c++)
                if (*c == L'/')
                    *c = L'\\';
            DWORD attr = GetFileAttributesW(wp);
            if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
                wcscpy(wdir, wp);
            else
            {
                wchar_t *slash = wcsrchr(wp, L'\\');
                if (slash)
                {
                    *slash = L'\0';
                    wcscpy(wdir, wp);
                    wcsncpy(file, slash + 1, FILE_CAP - 1);
                }
                else
                    wcsncpy(file, wp, FILE_CAP - 1);
            }
        }
    }

    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = parent_hwnd(parent);
    ofn.lpstrFilter = filter;
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = file;
    ofn.nMaxFile = (DWORD)FILE_CAP;
    ofn.lpstrTitle = wtitle[0] ? wtitle : NULL;
    ofn.lpstrInitialDir = wdir[0] ? wdir : NULL;
    ofn.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_HIDEREADONLY |
                (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST) | (many ? OFN_ALLOWMULTISELECT : 0);

    int paths = 0;
    if (fn(&ofn))
    {
        /* one path, or "dir\0name1\0name2\0\0" for a multiple selection */
        char utf8[MAX_PATH * 4];
        size_t used = 0;
        if (many && file[wcslen(file) + 1] != L'\0')
        {
            const wchar_t *dir = file, *name = file + wcslen(file) + 1;
            for (; *name; name += wcslen(name) + 1)
            {
                wchar_t full[MAX_PATH * 2];
                _snwprintf(full, MAX_PATH * 2, L"%ls\\%ls", dir, name);
                if (!win32_narrow(full, utf8, sizeof utf8))
                    continue;
                size_t n = strlen(utf8);
                if (used + n + 2 > cap)
                {
                    error_set("the paths do not fit in the buffer");
                    paths = 0;
                    used = 0;
                    break;
                }
                memcpy(out + used, utf8, n);
                used += n;
                out[used++] = '\n';
                paths++;
            }
            if (used)
                out[used - 1] = '\0'; /* no newline after the last */
        }
        else if (win32_narrow(file, utf8, sizeof utf8))
        {
            if (store(utf8, out, cap))
                paths = 1;
        }
    }
    else if (CommDlgExtendedError() != 0)
        error_set("the file dialog failed (error %lu)", (unsigned long)CommDlgExtendedError());

    free(file);
    free(filter);
    return paths;
}

bool dialog_open_file(PlatformWindow *parent, const char *title, const char *default_path, const FileFilter *filters,
                      int count, char *out, size_t cap)
{
    return file_dialog(parent, false, false, title, default_path, filters, count, out, cap) == 1;
}
bool dialog_save_file(PlatformWindow *parent, const char *title, const char *default_path, const FileFilter *filters,
                      int count, char *out, size_t cap)
{
    return file_dialog(parent, true, false, title, default_path, filters, count, out, cap) == 1;
}
int dialog_open_files(PlatformWindow *parent, const char *title, const char *default_path, const FileFilter *filters,
                      int count, char *out, size_t cap)
{
    return file_dialog(parent, false, true, title, default_path, filters, count, out, cap);
}

bool dialog_pick_folder(PlatformWindow *parent, const char *title, const char *default_path, char *out, size_t cap)
{
    (void)default_path; /* the old folder browser has no start folder without a callback */
    platform_clear_error();
    wchar_t wtitle[256] = L"", display[MAX_PATH];
    if (title)
        win32_widen(title, wtitle, 256);
    BROWSEINFOW bi;
    memset(&bi, 0, sizeof bi);
    bi.hwndOwner = parent_hwnd(parent);
    bi.pszDisplayName = display;
    bi.lpszTitle = wtitle[0] ? wtitle : NULL;
    bi.ulFlags = BIF_RETURNONLYFSDIRS;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl)
        return false; /* cancelled */
    wchar_t path[MAX_PATH];
    bool ok = SHGetPathFromIDListW(pidl, path) != 0;
    if (!g_ole32)
        g_ole32 = library_open("ole32.dll");
    PfnCoTaskMemFree free_fn = g_ole32 ? (PfnCoTaskMemFree)library_symbol(g_ole32, "CoTaskMemFree") : NULL;
    if (free_fn)
        free_fn(pidl);
    char utf8[MAX_PATH * 4];
    if (!ok || !win32_narrow(path, utf8, sizeof utf8))
        return error_set("that folder has no file system path");
    return store(utf8, out, cap);
}

bool open_url(const char *url)
{
    if (bad_url(url))
        return error_set("invalid URL");
    wchar_t w[2048];
    if (!win32_widen(url, w, 2048))
        return error_set("the URL is too long");
    return (INT_PTR)ShellExecuteW(NULL, L"open", w, NULL, NULL, SW_SHOWNORMAL) > 32 || error_set("cannot open the URL");
}

#elif defined(__ANDROID__)
/* ========================================================================== */

static bool none(void)
{
    return error_set("not supported on Android yet");
}
bool message_box(PlatformWindow *p, MessageKind k, const char *t, const char *m)
{
    (void)p, (void)k, (void)t, (void)m;
    return none();
}
int confirm_box(PlatformWindow *p, const char *t, const char *m)
{
    (void)p, (void)t, (void)m;
    none();
    return -1;
}
bool dialog_open_file(PlatformWindow *p, const char *t, const char *d, const FileFilter *f, int n, char *o, size_t c)
{
    (void)p, (void)t, (void)d, (void)f, (void)n, (void)o, (void)c;
    return none();
}
bool dialog_save_file(PlatformWindow *p, const char *t, const char *d, const FileFilter *f, int n, char *o, size_t c)
{
    (void)p, (void)t, (void)d, (void)f, (void)n, (void)o, (void)c;
    return none();
}
bool dialog_pick_folder(PlatformWindow *p, const char *t, const char *d, char *o, size_t c)
{
    (void)p, (void)t, (void)d, (void)o, (void)c;
    return none();
}
int dialog_open_files(PlatformWindow *p, const char *t, const char *d, const FileFilter *f, int n, char *o, size_t c)
{
    (void)p, (void)t, (void)d, (void)f, (void)n, (void)o, (void)c;
    none();
    return 0;
}
bool open_url(const char *url)
{
    if (bad_url(url))
        return error_set("invalid URL");
    return none();
}

#else
/* ========================================================================== */
/* Linux and other POSIX: zenity or kdialog, started directly (no shell). */

#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

#define MAX_ARGS 64
#define MAX_OUTPUT 65536

typedef struct
{
    char *v[MAX_ARGS];
    int n;
} Args;

static void args_add(Args *a, const char *s)
{
    if (a->n < MAX_ARGS - 1)
        a->v[a->n++] = strdup(s);
}
static void args_addf(Args *a, const char *fmt, const char *x)
{
    char buf[1024];
    snprintf(buf, sizeof buf, fmt, x);
    args_add(a, buf);
}
static void args_free(Args *a)
{
    for (int i = 0; i < a->n; i++)
        free(a->v[i]);
    a->n = 0;
}

/* The first executable called `name` on PATH. */
static bool find_tool(const char *name, char *out, size_t cap)
{
    const char *path = getenv("PATH");
    if (!path)
        return false;
    while (*path)
    {
        size_t len = strcspn(path, ":");
        if (len > 0 && len + strlen(name) + 2 <= cap)
        {
            snprintf(out, cap, "%.*s/%s", (int)len, path, name);
            if (access(out, X_OK) == 0)
                return true;
        }
        path += len;
        if (*path == ':')
            path++;
    }
    return false;
}

/* Run argv[0] with the arguments, collect its standard output (malloc'd, NUL
   ended) and return the exit status, or -1 if it could not be started. */
static int run_tool(const char *exe, Args *a, char **output)
{
    *output = NULL;
    a->v[a->n] = NULL;

    int fd[2];
    if (pipe(fd) != 0)
        return -1;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, fd[1], 1);
    posix_spawn_file_actions_addclose(&fa, fd[0]);
    posix_spawn_file_actions_addclose(&fa, fd[1]);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);

    pid_t pid;
    int rc = posix_spawn(&pid, exe, &fa, NULL, a->v, environ);
    posix_spawn_file_actions_destroy(&fa);
    close(fd[1]);
    if (rc != 0)
    {
        close(fd[0]);
        return -1;
    }

    char *buf = malloc(MAX_OUTPUT + 1);
    size_t used = 0;
    for (;;)
    {
        ssize_t n = buf ? read(fd[0], buf + used, MAX_OUTPUT - used) : 0;
        if (n > 0)
            used += (size_t)n;
        else if (n < 0 && errno == EINTR)
            continue;
        else
            break;
        if (used == MAX_OUTPUT)
            break;
    }
    close(fd[0]);
    if (buf)
        buf[used] = '\0';
    *output = buf;

    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
    {
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

typedef enum { TOOL_NONE, TOOL_ZENITY, TOOL_KDIALOG } Tool;

static Tool pick_tool(char *path, size_t cap)
{
    if (find_tool("zenity", path, cap))
        return TOOL_ZENITY;
    if (find_tool("kdialog", path, cap))
        return TOOL_KDIALOG;
    error_set("no dialog program found: install zenity or kdialog");
    return TOOL_NONE;
}

/* The window id a dialog should attach to: X11 only (elsewhere the handle is not an XID). */
static unsigned long parent_xid(PlatformWindow *parent)
{
    return parent ? (unsigned long)(uintptr_t)window_native_handle(parent, NATIVE_WINDOW) : 0;
}

static void add_attach(Args *a, Tool t, PlatformWindow *parent)
{
    unsigned long id = parent_xid(parent);
    if (!id)
        return;
    char buf[64];
    if (t == TOOL_ZENITY)
    {
        args_add(a, "--modal");
        snprintf(buf, sizeof buf, "--attach=%lu", id);
        args_add(a, buf);
    }
    else
    {
        args_add(a, "--attach");
        snprintf(buf, sizeof buf, "%lu", id);
        args_add(a, buf);
    }
}

bool message_box(PlatformWindow *parent, MessageKind kind, const char *title, const char *message)
{
    platform_clear_error();
    char exe[512];
    Tool t = pick_tool(exe, sizeof exe);
    if (t == TOOL_NONE)
        return false;

    Args a = {0};
    args_add(&a, t == TOOL_ZENITY ? "zenity" : "kdialog");
    if (t == TOOL_ZENITY)
    {
        args_add(&a, kind == MESSAGE_ERROR ? "--error" : kind == MESSAGE_WARNING ? "--warning" : "--info");
        args_addf(&a, "--title=%s", title ? title : "");
        args_addf(&a, "--text=%s", message ? message : "");
        args_add(&a, "--no-markup");
    }
    else
    {
        args_add(&a, kind == MESSAGE_ERROR ? "--error" : kind == MESSAGE_WARNING ? "--sorry" : "--msgbox");
        args_add(&a, message ? message : "");
        args_add(&a, "--title");
        args_add(&a, title ? title : "");
    }
    add_attach(&a, t, parent);
    char *out;
    int rc = run_tool(exe, &a, &out);
    free(out);
    args_free(&a);
    if (rc < 0)
        return error_set("cannot run the dialog program");
    return true;
}

int confirm_box(PlatformWindow *parent, const char *title, const char *message)
{
    platform_clear_error();
    char exe[512];
    Tool t = pick_tool(exe, sizeof exe);
    if (t == TOOL_NONE)
        return -1;

    Args a = {0};
    args_add(&a, t == TOOL_ZENITY ? "zenity" : "kdialog");
    if (t == TOOL_ZENITY)
    {
        args_add(&a, "--question");
        args_addf(&a, "--title=%s", title ? title : "");
        args_addf(&a, "--text=%s", message ? message : "");
        args_add(&a, "--no-markup");
    }
    else
    {
        args_add(&a, "--yesno");
        args_add(&a, message ? message : "");
        args_add(&a, "--title");
        args_add(&a, title ? title : "");
    }
    add_attach(&a, t, parent);
    char *out;
    int rc = run_tool(exe, &a, &out);
    free(out);
    args_free(&a);
    if (rc == 0)
        return 1;
    if (rc == 1)
        return 0;
    error_set("cannot run the dialog program");
    return -1;
}

typedef enum { DLG_OPEN, DLG_OPEN_MANY, DLG_SAVE, DLG_FOLDER } DialogKind;

/* "*.png;*.jpg" -> "*.png *.jpg" (what both programs take) */
static void spaced_patterns(const char *patterns, char *out, size_t cap)
{
    size_t n = 0;
    for (; patterns && *patterns && n + 1 < cap; patterns++)
        out[n++] = *patterns == ';' ? ' ' : *patterns;
    out[n] = '\0';
}

static int file_dialog(PlatformWindow *parent, DialogKind kind, const char *title, const char *default_path,
                       const FileFilter *filters, int count, char *out, size_t cap)
{
    platform_clear_error();
    char exe[512];
    Tool t = pick_tool(exe, sizeof exe);
    if (t == TOOL_NONE)
        return 0;

    Args a = {0};
    char pat[512], line[1200];
    if (t == TOOL_ZENITY)
    {
        args_add(&a, "zenity");
        args_add(&a, "--file-selection");
        args_addf(&a, "--title=%s", title ? title : "");
        if (kind == DLG_SAVE)
        {
            args_add(&a, "--save");
            args_add(&a, "--confirm-overwrite");
        }
        if (kind == DLG_FOLDER)
            args_add(&a, "--directory");
        if (kind == DLG_OPEN_MANY)
        {
            args_add(&a, "--multiple");
            args_add(&a, "--separator=\n");
        }
        if (default_path && default_path[0])
            args_addf(&a, "--filename=%s", default_path);
        if (kind != DLG_FOLDER)
            for (int i = 0; i < count; i++)
            {
                spaced_patterns(filters[i].patterns, pat, sizeof pat);
                snprintf(line, sizeof line, "--file-filter=%s | %s", filters[i].name, pat);
                args_add(&a, line);
            }
    }
    else
    {
        args_add(&a, "kdialog");
        const char *start = default_path && default_path[0] ? default_path : ".";
        args_add(&a, kind == DLG_SAVE ? "--getsavefilename" : kind == DLG_FOLDER ? "--getexistingdirectory" : "--getopenfilename");
        args_add(&a, start);
        if (kind != DLG_FOLDER && count > 0)
        {
            /* kdialog takes one filter string: "*.png *.jpg|Images\n*.txt|Text" */
            char all[1500] = "";
            for (int i = 0; i < count; i++)
            {
                spaced_patterns(filters[i].patterns, pat, sizeof pat);
                snprintf(line, sizeof line, "%s%s|%s", i ? "\n" : "", pat, filters[i].name);
                strncat(all, line, sizeof all - strlen(all) - 1);
            }
            args_add(&a, all);
        }
        if (kind == DLG_OPEN_MANY)
        {
            args_add(&a, "--multiple");
            args_add(&a, "--separate-output");
        }
        args_add(&a, "--title");
        args_add(&a, title ? title : "");
    }
    add_attach(&a, t, parent);

    char *text;
    int rc = run_tool(exe, &a, &text);
    args_free(&a);
    if (rc < 0)
    {
        free(text);
        error_set("cannot run the dialog program");
        return 0;
    }
    if (rc != 0 || !text || !text[0])
    {
        free(text); /* cancelled: no error */
        return 0;
    }

    /* one path per line; trim the trailing newline(s) */
    size_t len = strlen(text);
    while (len && (text[len - 1] == '\n' || text[len - 1] == '\r'))
        text[--len] = '\0';
    int paths = 0;
    if (len)
    {
        paths = 1;
        for (size_t i = 0; i < len; i++)
            paths += text[i] == '\n';
    }
    int result = 0;
    if (kind == DLG_OPEN_MANY)
        result = store(text, out, cap) ? paths : 0;
    else
    {
        char *nl = strchr(text, '\n');
        if (nl)
            *nl = '\0';
        result = store(text, out, cap) ? 1 : 0;
    }
    free(text);
    return result;
}

bool dialog_open_file(PlatformWindow *parent, const char *title, const char *default_path, const FileFilter *filters,
                      int count, char *out, size_t cap)
{
    return file_dialog(parent, DLG_OPEN, title, default_path, filters, count, out, cap) == 1;
}
bool dialog_save_file(PlatformWindow *parent, const char *title, const char *default_path, const FileFilter *filters,
                      int count, char *out, size_t cap)
{
    return file_dialog(parent, DLG_SAVE, title, default_path, filters, count, out, cap) == 1;
}
bool dialog_pick_folder(PlatformWindow *parent, const char *title, const char *default_path, char *out, size_t cap)
{
    return file_dialog(parent, DLG_FOLDER, title, default_path, NULL, 0, out, cap) == 1;
}
int dialog_open_files(PlatformWindow *parent, const char *title, const char *default_path, const FileFilter *filters,
                      int count, char *out, size_t cap)
{
    return file_dialog(parent, DLG_OPEN_MANY, title, default_path, filters, count, out, cap);
}

bool open_url(const char *url)
{
    platform_clear_error();
    if (bad_url(url))
        return error_set("invalid URL");
    char exe[512];
    if (!find_tool("xdg-open", exe, sizeof exe))
        return error_set("xdg-open was not found");

    /* Start it detached: some browsers keep xdg-open alive until they exit. A double
       fork leaves nothing to reap and nothing to wait for. */
    pid_t pid = fork();
    if (pid < 0)
        return error_set("cannot start xdg-open");
    if (pid == 0)
    {
        pid_t grand = fork();
        if (grand == 0)
        {
            int null = open("/dev/null", O_RDWR);
            if (null >= 0)
            {
                dup2(null, 0);
                dup2(null, 1);
                dup2(null, 2);
            }
            char *argv[] = {"xdg-open", (char *)url, NULL};
            execv(exe, argv);
            _exit(127);
        }
        _exit(grand < 0 ? 1 : 0);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
    {
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? true : error_set("cannot start xdg-open");
}

#endif
