#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>

#include "gamepad_xinput_map.h"

#define XINPUT_USERS 4
#define RESCAN_NANOS 1000000000ull

typedef struct
{
    DWORD packet;
    XInputPadState pad;
} XInputRawState;

typedef DWORD(WINAPI *PFN_XInputGetState)(DWORD, void *);

typedef struct
{
    WORD left_motor, right_motor; /* XINPUT_VIBRATION */
} XInputVibration;
typedef DWORD(WINAPI *PFN_XInputSetState)(DWORD, XInputVibration *);

static HMODULE g_dll;
static PFN_XInputGetState g_get_state;
static PFN_XInputSetState g_set_state;
static uint64_t g_rumble_stop[XINPUT_USERS]; /* when to switch the motors off, 0 = not running */
static int g_slot[XINPUT_USERS];
static uint64_t g_next_scan;

static bool read_user(int user, XInputPadState *out)
{
    XInputRawState raw;
    if (g_get_state(user, &raw) != ERROR_SUCCESS)
        return false;
    *out = raw.pad;
    return true;
}

static void scan_disconnected(void)
{
    for (int i = 0; i < XINPUT_USERS; i++)
    {
        XInputPadState state;
        if (g_slot[i] >= 0 || !read_user(i, &state))
            continue;
        g_slot[i] = gamepad_internal_connect("Xbox Controller");
        if (g_slot[i] >= 0)
            xinput_apply(g_slot[i], &state);
    }
}

/* ---- generic joysticks through winmm (loaded at run time) ---- */

#define WINMM_IDS 16

typedef UINT(WINAPI *PFN_joyGetNumDevs)(void);
typedef MMRESULT(WINAPI *PFN_joyGetPosEx)(UINT, LPJOYINFOEX);
typedef MMRESULT(WINAPI *PFN_joyGetDevCapsW)(UINT_PTR, LPJOYCAPSW, UINT);

static HMODULE g_winmm;
static PFN_joyGetNumDevs g_joy_num;
static PFN_joyGetPosEx g_joy_pos;
static PFN_joyGetDevCapsW g_joy_caps;
static uint64_t g_next_joy_scan;

static struct
{
    int slot; /* -1 when the id is free */
    JOYCAPSW caps;
} g_wj[WINMM_IDS];

static float joy_norm(DWORD v, UINT lo, UINT hi)
{
    if (hi <= lo)
        return 0.0f;
    double half = ((double)hi - (double)lo) / 2.0;
    double r = ((double)v - ((double)lo + (double)hi) / 2.0) / half;
    return (float)(r < -1.0 ? -1.0 : r > 1.0 ? 1.0 : r);
}

static int pov_mask(DWORD pov)
{
    if (LOWORD(pov) == 0xFFFF)
        return 0;
    int deg = (int)(pov / 100); /* hundredths of a degree, clockwise from up */
    int mask = 0;
    if (deg > 292 || deg < 68)
        mask |= JOYHAT_UP;
    if (deg > 22 && deg < 158)
        mask |= JOYHAT_RIGHT;
    if (deg > 112 && deg < 248)
        mask |= JOYHAT_DOWN;
    if (deg > 202 && deg < 338)
        mask |= JOYHAT_LEFT;
    return mask;
}

static void joy_apply(int id, const JOYINFOEX *ji)
{
    const JOYCAPSW *c = &g_wj[id].caps;
    int slot = g_wj[id].slot;
    int axes = (int)c->wNumAxes;
    const struct
    {
        DWORD v;
        UINT lo, hi;
    } a[] = {{ji->dwXpos, c->wXmin, c->wXmax}, {ji->dwYpos, c->wYmin, c->wYmax}, {ji->dwZpos, c->wZmin, c->wZmax},
             {ji->dwRpos, c->wRmin, c->wRmax}, {ji->dwUpos, c->wUmin, c->wUmax}, {ji->dwVpos, c->wVmin, c->wVmax}};
    for (int i = 0; i < axes && i < 6; i++)
        joystick_internal_set_axis(slot, i, joy_norm(a[i].v, a[i].lo, a[i].hi));
    for (int b = 0; b < (int)c->wNumButtons && b < 32; b++)
        joystick_internal_set_button(slot, b, (ji->dwButtons >> b) & 1);
    if (c->wCaps & JOYCAPS_HASPOV)
        joystick_internal_set_hat(slot, 0, pov_mask(ji->dwPOV));
}

static void joystick_scan(void)
{
    if (!g_joy_pos)
        return;
    UINT n = g_joy_num ? g_joy_num() : 0;
    for (UINT id = 0; id < n && id < WINMM_IDS; id++)
    {
        JOYINFOEX ji;
        memset(&ji, 0, sizeof ji);
        ji.dwSize = sizeof ji;
        ji.dwFlags = JOY_RETURNALL;
        if (g_wj[id].slot >= 0 || g_joy_pos(id, &ji) != JOYERR_NOERROR)
            continue;
        JOYCAPSW caps;
        memset(&caps, 0, sizeof caps);
        if (g_joy_caps(id, &caps, sizeof caps) != JOYERR_NOERROR)
            continue;
        char name[128] = "";
        WideCharToMultiByte(CP_UTF8, 0, caps.szPname, -1, name, sizeof name, NULL, NULL);
        int axes = (int)caps.wNumAxes > 6 ? 6 : (int)caps.wNumAxes;
        int buttons = (int)caps.wNumButtons > 32 ? 32 : (int)caps.wNumButtons;
        int slot = joystick_internal_connect(name, axes, buttons, (caps.wCaps & JOYCAPS_HASPOV) ? 1 : 0);
        if (slot < 0)
            return;
        g_wj[id].slot = slot;
        g_wj[id].caps = caps;
        joy_apply((int)id, &ji);
    }
}

static void joystick_init(void)
{
    for (int i = 0; i < WINMM_IDS; i++)
        g_wj[i].slot = -1;
    g_winmm = LoadLibraryW(L"winmm.dll");
    if (!g_winmm)
        return;
    g_joy_num = (PFN_joyGetNumDevs)(void *)GetProcAddress(g_winmm, "joyGetNumDevs");
    g_joy_pos = (PFN_joyGetPosEx)(void *)GetProcAddress(g_winmm, "joyGetPosEx");
    g_joy_caps = (PFN_joyGetDevCapsW)(void *)GetProcAddress(g_winmm, "joyGetDevCapsW");
    if (!g_joy_num || !g_joy_pos || !g_joy_caps)
    {
        FreeLibrary(g_winmm);
        g_winmm = NULL;
        g_joy_pos = NULL;
        return;
    }
    joystick_scan();
    g_next_joy_scan = time_nanos() + RESCAN_NANOS;
}

static void joystick_poll(void)
{
    if (!g_joy_pos)
        return;
    for (int id = 0; id < WINMM_IDS; id++)
    {
        if (g_wj[id].slot < 0)
            continue;
        JOYINFOEX ji;
        memset(&ji, 0, sizeof ji);
        ji.dwSize = sizeof ji;
        ji.dwFlags = JOY_RETURNALL;
        if (g_joy_pos((UINT)id, &ji) == JOYERR_NOERROR)
            joy_apply(id, &ji);
        else
        {
            joystick_internal_disconnect(g_wj[id].slot);
            g_wj[id].slot = -1;
        }
    }
    uint64_t now = time_nanos();
    if (now >= g_next_joy_scan)
    {
        joystick_scan();
        g_next_joy_scan = now + RESCAN_NANOS;
    }
}

static void joystick_shutdown(void)
{
    for (int i = 0; i < WINMM_IDS; i++)
    {
        if (g_wj[i].slot >= 0)
            joystick_internal_disconnect(g_wj[i].slot);
        g_wj[i].slot = -1;
    }
    if (g_winmm)
        FreeLibrary(g_winmm);
    g_winmm = NULL;
    g_joy_pos = NULL;
}

void gamepad_backend_init(void)
{
    joystick_init();
    static const wchar_t *k_names[] = {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"};
    for (int i = 0; i < XINPUT_USERS; i++)
        g_slot[i] = -1;
    for (unsigned i = 0; i < sizeof k_names / sizeof k_names[0] && !g_dll; i++)
        g_dll = LoadLibraryW(k_names[i]);
    if (!g_dll)
        return;
    g_get_state = (PFN_XInputGetState)(void *)GetProcAddress(g_dll, "XInputGetState");
    g_set_state = (PFN_XInputSetState)(void *)GetProcAddress(g_dll, "XInputSetState");
    if (!g_get_state)
    {
        FreeLibrary(g_dll);
        g_dll = NULL;
        return;
    }
    scan_disconnected();
    g_next_scan = time_nanos() + RESCAN_NANOS;
}

void gamepad_backend_poll(void)
{
    joystick_poll();
    if (!g_get_state)
        return;
    for (int i = 0; i < XINPUT_USERS; i++)
    {
        XInputPadState state;
        if (g_slot[i] < 0)
            continue;
        if (read_user(i, &state))
        {
            xinput_apply(g_slot[i], &state);
        }
        else
        {
            gamepad_internal_disconnect(g_slot[i]);
            g_slot[i] = -1;
            g_rumble_stop[i] = 0;
        }
    }
    uint64_t now = time_nanos();
    for (int i = 0; i < XINPUT_USERS; i++)
    {
        if (g_rumble_stop[i] && now >= g_rumble_stop[i])
        {
            XInputVibration off = {0, 0};
            if (g_set_state)
                g_set_state(i, &off);
            g_rumble_stop[i] = 0;
        }
    }
    if (now >= g_next_scan)
    {
        scan_disconnected();
        g_next_scan = now + RESCAN_NANOS;
    }
}

void gamepad_backend_shutdown(void)
{
    for (int i = 0; i < XINPUT_USERS; i++)
    {
        if (g_slot[i] >= 0)
        {
            XInputVibration off = {0, 0};
            if (g_set_state && g_rumble_stop[i])
                g_set_state(i, &off);
            gamepad_internal_disconnect(g_slot[i]);
        }
        g_slot[i] = -1;
        g_rumble_stop[i] = 0;
    }
    joystick_shutdown();
    if (g_dll)
        FreeLibrary(g_dll);
    g_dll = NULL;
    g_get_state = NULL;
    g_set_state = NULL;
}

bool gamepad_backend_rumble(int slot, float strong, float weak, uint32_t ms)
{
    if (!g_set_state)
        return false;
    for (int i = 0; i < XINPUT_USERS; i++)
    {
        if (g_slot[i] != slot)
            continue;
        bool off = ms == 0 || (strong <= 0.0f && weak <= 0.0f);
        XInputVibration v = {off ? 0 : (WORD)(strong * 65535.0f), off ? 0 : (WORD)(weak * 65535.0f)};
        if (g_set_state(i, &v) != ERROR_SUCCESS)
            return false;
        g_rumble_stop[i] = off ? 0 : time_nanos() + (uint64_t)ms * 1000000ull;
        return true;
    }
    return false;
}
