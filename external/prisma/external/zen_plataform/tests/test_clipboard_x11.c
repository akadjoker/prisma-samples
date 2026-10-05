/*
 * test_clipboard_x11.c - the real X11 CLIPBOARD selection, against other X clients.
 * The "other applications" are forked children with their own Display connection:
 * one owns the selection and serves it (including INCR for the large PNG), one
 * reads what this process owns. Needs a display; skips (77) without one.
 */
#include "platform.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define BIG_SIZE (2 * 1024 * 1024 + 123) /* not a multiple of any chunk size */
#define TEXT "ol\xC3\xA1 zen \xE2\x80\x94 clipboard"
#define CUSTOM_MIME "application/x-zen-test"
#define CUSTOM_DATA "custom!"

static int g_pass, g_fail;

#define CHECK(cond)                                                \
    do                                                             \
    {                                                              \
        if (cond)                                                  \
            g_pass++;                                              \
        else                                                       \
        {                                                          \
            g_fail++;                                              \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        }                                                          \
    } while (0)

static uint8_t *make_big(void)
{
    uint8_t *p = malloc(BIG_SIZE);
    for (size_t i = 0; i < BIG_SIZE; i++)
        p[i] = (uint8_t)(i * 31 + 7);
    return p;
}

static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

/* ---- child 1: a foreign clipboard owner ---- */

typedef struct
{
    Display *d;
    Window w;
    Atom clipboard, targets, utf8, incr, png, custom, prop_data;
    /* one INCR transfer */
    bool incr_active;
    Window incr_requestor;
    Atom incr_property, incr_target;
    size_t incr_offset;
} Owner;

#define OWNER_CHUNK 65536

static void owner_answer(Owner *o, const uint8_t *big, XSelectionRequestEvent *rq)
{
    XSelectionEvent r = {.type = SelectionNotify, .display = rq->display, .requestor = rq->requestor,
                         .selection = rq->selection, .target = rq->target, .time = rq->time, .property = None};
    Atom prop = rq->property ? rq->property : rq->target;
    if (rq->target == o->targets)
    {
        Atom t[] = {o->targets, o->utf8, o->png, o->custom};
        XChangeProperty(o->d, rq->requestor, prop, XA_ATOM, 32, PropModeReplace, (unsigned char *)t, 4);
        r.property = prop;
    }
    else if (rq->target == o->utf8)
    {
        XChangeProperty(o->d, rq->requestor, prop, o->utf8, 8, PropModeReplace, (unsigned char *)TEXT, (int)strlen(TEXT));
        r.property = prop;
    }
    else if (rq->target == o->custom)
    {
        XChangeProperty(o->d, rq->requestor, prop, o->custom, 8, PropModeReplace, (unsigned char *)CUSTOM_DATA, (int)strlen(CUSTOM_DATA));
        r.property = prop;
    }
    else if (rq->target == o->png && !o->incr_active)
    {
        o->incr_active = true;
        o->incr_requestor = rq->requestor;
        o->incr_property = prop;
        o->incr_target = rq->target;
        o->incr_offset = 0;
        XSelectInput(o->d, rq->requestor, PropertyChangeMask);
        long total = (long)BIG_SIZE;
        XChangeProperty(o->d, rq->requestor, prop, o->incr, 32, PropModeReplace, (unsigned char *)&total, 1);
        r.property = prop;
    }
    (void)big;
    XSendEvent(o->d, rq->requestor, True, NoEventMask, (XEvent *)&r);
    XFlush(o->d);
}

static void owner_incr_step(Owner *o, const uint8_t *big, XPropertyEvent *pe)
{
    if (!o->incr_active || pe->window != o->incr_requestor || pe->atom != o->incr_property || pe->state != PropertyDelete)
        return;
    size_t left = BIG_SIZE - o->incr_offset;
    size_t n = left > OWNER_CHUNK ? OWNER_CHUNK : left;
    XChangeProperty(o->d, o->incr_requestor, o->incr_property, o->incr_target, 8, PropModeReplace,
                    big + o->incr_offset, (int)n);
    o->incr_offset += n;
    if (n == 0)
        o->incr_active = false;
    XFlush(o->d);
}

static void run_owner(int ready_fd)
{
    Owner o = {0};
    o.d = XOpenDisplay(NULL);
    if (!o.d)
        _exit(2);
    o.w = XCreateSimpleWindow(o.d, DefaultRootWindow(o.d), 0, 0, 1, 1, 0, 0, 0);
    o.clipboard = XInternAtom(o.d, "CLIPBOARD", False);
    o.targets = XInternAtom(o.d, "TARGETS", False);
    o.utf8 = XInternAtom(o.d, "UTF8_STRING", False);
    o.incr = XInternAtom(o.d, "INCR", False);
    o.png = XInternAtom(o.d, "image/png", False);
    o.custom = XInternAtom(o.d, CUSTOM_MIME, False);
    uint8_t *big = make_big();
    XSetSelectionOwner(o.d, o.clipboard, o.w, CurrentTime);
    XSync(o.d, False);
    (void)!write(ready_fd, "1", 1);

    double end = now_s() + 20.0;
    while (now_s() < end)
    {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(ConnectionNumber(o.d), &fds);
        struct timeval tv = {0, 20000};
        select(ConnectionNumber(o.d) + 1, &fds, NULL, NULL, &tv);
        while (XPending(o.d))
        {
            XEvent ev;
            XNextEvent(o.d, &ev);
            if (ev.type == SelectionRequest)
                owner_answer(&o, big, &ev.xselectionrequest);
            else if (ev.type == PropertyNotify)
                owner_incr_step(&o, big, &ev.xproperty);
        }
    }
    _exit(0);
}

/* ---- child 2: a foreign reader of what the parent owns ---- */

typedef struct
{
    Display *d;
    Window w;
    Atom clipboard, prop;
} Reader;

static bool reader_wait(Reader *r, int type, XEvent *ev)
{
    double end = now_s() + 5.0;
    while (now_s() < end)
    {
        if (XCheckTypedWindowEvent(r->d, r->w, type, ev))
            return true;
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(ConnectionNumber(r->d), &fds);
        struct timeval tv = {0, 10000};
        select(ConnectionNumber(r->d) + 1, &fds, NULL, NULL, &tv);
        XPending(r->d);
    }
    return false;
}

/* Fetch one target; returns a malloc'd copy. INCR is followed. */
static uint8_t *reader_fetch(Reader *r, Atom target, size_t *out_n, int *out_format)
{
    XEvent ev;
    XDeleteProperty(r->d, r->w, r->prop);
    XConvertSelection(r->d, r->clipboard, target, r->prop, r->w, CurrentTime);
    XFlush(r->d);
    if (!reader_wait(r, SelectionNotify, &ev) || ev.xselection.property == None)
        return NULL;

    Atom type, incr = XInternAtom(r->d, "INCR", False);
    int fmt;
    unsigned long n, after;
    unsigned char *data = NULL;
    XGetWindowProperty(r->d, r->w, r->prop, 0, 1 << 28, True, AnyPropertyType, &type, &fmt, &n, &after, &data);
    if (type != incr)
    {
        size_t unit = fmt == 32 ? sizeof(long) : (size_t)fmt / 8;
        uint8_t *copy = malloc(n * unit + 1);
        memcpy(copy, data, n * unit);
        copy[n * unit] = 0;
        *out_n = n * unit;
        *out_format = fmt;
        XFree(data);
        return copy;
    }
    if (data)
        XFree(data);
    uint8_t *all = NULL;
    size_t have = 0;
    for (;;)
    {
        do
        {
            if (!reader_wait(r, PropertyNotify, &ev))
                return NULL;
        } while (ev.xproperty.atom != r->prop || ev.xproperty.state != PropertyNewValue);
        XGetWindowProperty(r->d, r->w, r->prop, 0, 1 << 28, True, AnyPropertyType, &type, &fmt, &n, &after, &data);
        if (type == None) /* the stale NewValue from the INCR announcement */
            continue;
        if (n == 0)
        {
            if (data)
                XFree(data);
            break;
        }
        all = realloc(all, have + n);
        memcpy(all + have, data, n);
        have += n;
        XFree(data);
    }
    *out_n = have;
    *out_format = 8;
    return all;
}

/* exit code 0 = everything matched */
static void run_reader(void)
{
    Reader r = {0};
    r.d = XOpenDisplay(NULL);
    if (!r.d)
        _exit(2);
    r.w = XCreateSimpleWindow(r.d, DefaultRootWindow(r.d), 0, 0, 1, 1, 0, 0, 0);
    XSelectInput(r.d, r.w, PropertyChangeMask);
    r.clipboard = XInternAtom(r.d, "CLIPBOARD", False);
    r.prop = XInternAtom(r.d, "READER_PROP", False);
    Atom utf8 = XInternAtom(r.d, "UTF8_STRING", False);
    Atom png = XInternAtom(r.d, "image/png", False);
    Atom targets = XInternAtom(r.d, "TARGETS", False);
    Atom custom = XInternAtom(r.d, CUSTOM_MIME, False);

    size_t n;
    int fmt;
    uint8_t *t = reader_fetch(&r, targets, &n, &fmt);
    if (!t || fmt != 32)
        _exit(10);
    bool has_utf8 = false, has_png = false, has_custom = false;
    for (size_t i = 0; i + sizeof(Atom) <= n; i += sizeof(Atom))
    {
        Atom a;
        memcpy(&a, t + i, sizeof a);
        has_utf8 |= a == utf8;
        has_png |= a == png;
        has_custom |= a == custom;
    }
    if (!has_utf8 || !has_png || !has_custom)
        _exit(11);

    uint8_t *text = reader_fetch(&r, utf8, &n, &fmt);
    if (!text || n != strlen(TEXT) || memcmp(text, TEXT, n) != 0)
        _exit(12);

    uint8_t *cu = reader_fetch(&r, custom, &n, &fmt);
    if (!cu || n != strlen(CUSTOM_DATA) || memcmp(cu, CUSTOM_DATA, n) != 0)
        _exit(13);

    uint8_t *img = reader_fetch(&r, png, &n, &fmt);
    uint8_t *big = make_big();
    if (!img || n != BIG_SIZE || memcmp(img, big, BIG_SIZE) != 0)
        _exit(14);

    /* a target we never offered is refused */
    uint8_t *none = reader_fetch(&r, XInternAtom(r.d, "image/x-never", False), &n, &fmt);
    if (none)
        _exit(15);
    _exit(0);
}

/* ---- the tests ---- */

static pid_t spawn_owner(void)
{
    int fds[2];
    if (pipe(fds) != 0)
        return -1;
    pid_t pid = fork();
    if (pid == 0)
    {
        close(fds[0]);
        run_owner(fds[1]);
    }
    close(fds[1]);
    char c;
    ssize_t got = read(fds[0], &c, 1);
    close(fds[0]);
    return got == 1 ? pid : -1;
}

static void test_foreign_owner(void)
{
    pid_t pid = spawn_owner();
    CHECK(pid > 0);
    if (pid <= 0)
        return;

    CHECK(clipboard_has_data(CLIPBOARD_TEXT));
    CHECK(clipboard_has_data(CLIPBOARD_PNG));
    CHECK(clipboard_has_data(CUSTOM_MIME));
    CHECK(!clipboard_has_data("image/x-never"));
    CHECK(!clipboard_has_data("text/uri-list"));

    CHECK(strcmp(clipboard_get(), TEXT) == 0);

    size_t n = 0;
    char *c = clipboard_get_data(CUSTOM_MIME, &n);
    CHECK(c && n == strlen(CUSTOM_DATA) && memcmp(c, CUSTOM_DATA, n) == 0);
    fs_free(c);

    /* 2 MB: arrives by INCR, in 64 KB chunks */
    double t0 = now_s();
    uint8_t *img = clipboard_get_data(CLIPBOARD_PNG, &n);
    uint8_t *big = make_big();
    CHECK(img != NULL && n == BIG_SIZE);
    CHECK(img && memcmp(img, big, BIG_SIZE) == 0);
    CHECK(now_s() - t0 < 5.0);
    free(big);
    fs_free(img);

    CHECK(clipboard_get_data("image/x-never", &n) == NULL);

    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
}

/* A desktop clipboard manager may take the selection from us as soon as we set it
   (so the content outlives the app). We then free our data, as we should, and the
   paste is served by the manager, which keeps only some types. That is not a
   failure of ours: when the reader fails and we find we no longer own the content,
   set it again and retry. A bare X server, as in CI, passes on the first attempt. */
static bool read_back_with_reader(PlatformWindow *w, const ClipboardItem *items, int count, int *attempts)
{
    for (*attempts = 1; *attempts <= 5; (*attempts)++)
    {
        if (!clipboard_set_items(items, count))
            return false;
        for (int i = 0; i < 10 && w; i++) /* let the selection settle */
        {
            window_begin_frame(w);
            struct timespec ts = {0, 5000000};
            nanosleep(&ts, NULL);
        }
        pid_t pid = fork();
        if (pid == 0)
            run_reader();
        int status = -1;
        double end = now_s() + 20.0;
        while (now_s() < end)
        {
            if (w)
                window_begin_frame(w); /* answers the selection requests */
            if (waitpid(pid, &status, WNOHANG) == pid)
                break;
            struct timespec ts = {0, 2000000};
            nanosleep(&ts, NULL);
        }
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
            return true;
        printf("reader child exited with %d (attempt %d)\n", WIFEXITED(status) ? WEXITSTATUS(status) : -1, *attempts);
        if (clipboard_has_data(CUSTOM_MIME))
            return false; /* still ours, so the failure is real */
    }
    return false;
}

static void test_we_own(void)
{
    uint8_t *big = make_big();
    ClipboardItem items[] = {
        {CLIPBOARD_TEXT, TEXT, strlen(TEXT)},
        {CLIPBOARD_PNG, big, BIG_SIZE},
        {CUSTOM_MIME, CUSTOM_DATA, strlen(CUSTOM_DATA)},
    };
    CHECK(clipboard_set_items(items, 3));

    PlatformWindow *w = window_create(&(WindowConfig){.title = "owner", .width = 64, .height = 64});
    CHECK(w != NULL);

    /* a different client pastes it, while this one keeps its event loop going */
    int attempts;
    CHECK(read_back_with_reader(w, items, 3, &attempts));

    /* clearing releases our claim: what we offered is gone (CUSTOM_MIME is a type no
       clipboard manager would have kept) */
    CHECK(clipboard_set_items(NULL, 0));
    CHECK(!clipboard_has_data(CUSTOM_MIME));
    if (w)
        window_destroy(w);
    free(big);
}

int main(void)
{
    if (!platform_init())
    {
        printf("skip: no display\n");
        return 77;
    }
    test_foreign_owner();
    test_we_own();
    platform_shutdown();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
