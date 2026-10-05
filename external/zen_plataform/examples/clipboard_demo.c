/*
 * clipboard_demo.c - copy an image anywhere (a browser, a screenshot tool, Paint,
 * GIMP), then paste it here and see it drawn. Also copies images and text back out.
 *
 *   Ctrl+V   paste the clipboard image          Ctrl+C   copy the pasted image
 *   buttons  paste / copy / test pattern / text / clear / save as pasted.bmp
 *   --paste  paste once on start, before any key press
 *
 * The right panel lists what the clipboard offers: text, an image (PNG), copied
 * files. It refreshes when the window gains focus, so copy in another program,
 * come back, and it is there.
 */
#include "platform.h"
#include "zen_ui.h"

#include <stdio.h>
#include <string.h>

typedef struct
{
    UiContext *ui;
    UiRect panel;

    Framebuffer pasted;
    bool has_pasted;
    Framebuffer pattern;

    char status[200];
    bool paste_on_start; /* --paste: paste once on the first frame */

    /* what the clipboard offers, refreshed on demand */
    bool has_text, has_png, has_uris;
    char text_preview[160];
    char uris[4][140];
    int uri_count, uri_total;
} Demo;

static void set_status(Demo *d, const char *fmt, const char *arg)
{
    snprintf(d->status, sizeof d->status, fmt, arg);
}

/* Keep the first line, cut to fit, one char per column. */
static void preview(char *out, size_t cap, const char *src)
{
    size_t n = 0;
    for (; src[n] && src[n] != '\n' && src[n] != '\r' && n + 4 < cap; n++)
        out[n] = src[n];
    if (src[n] && src[n] != '\n' && src[n] != '\r')
    {
        out[n++] = '.';
        out[n++] = '.';
        out[n++] = '.';
    }
    out[n] = '\0';
}

static void refresh(Demo *d)
{
    d->has_text = clipboard_has_data(CLIPBOARD_TEXT);
    d->has_png = clipboard_has_data(CLIPBOARD_PNG);
    d->has_uris = clipboard_has_data(CLIPBOARD_URIS);
    d->text_preview[0] = '\0';
    d->uri_count = d->uri_total = 0;

    if (d->has_text)
        preview(d->text_preview, sizeof d->text_preview, clipboard_get());
    if (d->has_uris)
    {
        size_t n;
        char *uris = clipboard_get_data(CLIPBOARD_URIS, &n);
        for (char *p = uris; p && *p;)
        {
            char *end = p + strcspn(p, "\r\n");
            if (end > p)
            {
                d->uri_total++;
                if (d->uri_count < 4)
                    snprintf(d->uris[d->uri_count++], sizeof d->uris[0], "%.*s", (int)(end - p), p);
            }
            p = end + strspn(end, "\r\n");
        }
        fs_free(uris);
    }
}

static void paste_image(Demo *d)
{
    Framebuffer img;
    if (!clipboard_get_image(&img))
    {
        set_status(d, "Paste: %s", platform_get_error());
        return;
    }
    if (d->has_pasted)
        framebuffer_free(&d->pasted);
    d->pasted = img;
    d->has_pasted = true;
    snprintf(d->status, sizeof d->status, "Pasted an image, %d x %d", img.width, img.height);
}

static void copy_image(Demo *d, const Framebuffer *fb, const char *what)
{
    if (!fb->pixels)
    {
        set_status(d, "Nothing to copy: %s", "paste an image first");
        return;
    }
    if (clipboard_set_image(fb))
        set_status(d, "Copied %s to the clipboard", what);
    else
        set_status(d, "Copy: %s", platform_get_error());
}

/* A gradient with a see-through ring, so copying shows that alpha survives. */
static void make_pattern(Framebuffer *fb)
{
    const int n = 256;
    framebuffer_alloc(fb, n, n);
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++)
        {
            int dx = x - n / 2, dy = y - n / 2, r2 = dx * dx + dy * dy;
            uint32_t a = r2 <= 100 * 100 ? 255 : (r2 <= 120 * 120 ? 90 : 0);
            fb->pixels[y * n + x] = (a << 24) | ((uint32_t)x << 16) | ((uint32_t)y << 8) | 160u;
        }
}

static void draw_image_area(Demo *d, Framebuffer *fb, UiRect area)
{
    /* checkerboard, so transparency is visible */
    draw_set_clip(area.x, area.y, area.w, area.h);
    for (int y = 0; y < area.h; y += 16)
        for (int x = 0; x < area.w; x += 16)
            draw_fill_rect(fb, area.x + x, area.y + y, 16, 16,
                           ((x / 16 + y / 16) & 1) ? 0xFF3C3C3C : 0xFF2E2E2E, BLEND_NONE);

    if (d->has_pasted)
    {
        const Framebuffer *im = &d->pasted;
        float s = (float)area.w / (float)im->width;
        float sy = (float)area.h / (float)im->height;
        if (sy < s)
            s = sy;
        if (s > 1.0f && im->width < 400) /* small images: show them crisp, not stretched past 2x */
            s = s > 2.0f ? 2.0f : s;
        int dw = (int)((float)im->width * s), dh = (int)((float)im->height * s);
        if (dw < 1) dw = 1;
        if (dh < 1) dh = 1;
        int dx = area.x + (area.w - dw) / 2, dy = area.y + (area.h - dh) / 2;
        draw_blit(fb, im, 0, 0, im->width, im->height, dx, dy, dw, dh, BLEND_ALPHA,
                  s < 1.0f ? SCALE_BILINEAR : SCALE_NEAREST);
        draw_rect(fb, dx - 1, dy - 1, dw + 2, dh + 2, 0xFF909090, BLEND_NONE);
    }
    draw_reset_clip();
    draw_rect(fb, area.x, area.y, area.w, area.h, 0xFF606060, BLEND_NONE);
}

static void frame(PlatformWindow *w, void *user)
{
    Demo *d = (Demo *)user;

    if (d->paste_on_start)
    {
        d->paste_on_start = false;
        refresh(d);
        paste_image(d);
    }

    Event ev;
    while (poll_event(w, &ev))
    {
        if (ev.type == EVENT_WINDOW_FOCUS && ev.data.focus.gained)
            refresh(d); /* the user may have copied something elsewhere */
        else if (ev.type == EVENT_WINDOW_DROP && ev.data.drop.count > 0)
            set_status(d, "Dropped a file: %s (drop is shown, not loaded)", ev.data.drop.paths[0]);
    }

    int mods = key_mods(w);
    if ((mods & KEYMOD_CTRL) && key_pressed(w, KEY_V))
    {
        refresh(d);
        paste_image(d);
    }
    if ((mods & KEYMOD_CTRL) && key_pressed(w, KEY_C))
        copy_image(d, &d->pasted, "the pasted image");

    Framebuffer fb;
    if (!window_lock_pixels(w, &fb))
        return;
    draw_clear(&fb, 0xFF1E1E1E);

    int ww, wh;
    window_get_size(w, &ww, &wh);
    UiRect area = zui_rect(16, 100, ww - 16 - 340, wh - 100 - 16);
    if (area.w > 8 && area.h > 8)
        draw_image_area(d, &fb, area);

    zui_begin(d->ui, w);

    zui_row(d->ui, 30, 5);
    if (zui_button(d->ui, "Paste (Ctrl+V)"))
    {
        refresh(d);
        paste_image(d);
    }
    if (zui_button(d->ui, "Copy (Ctrl+C)"))
        copy_image(d, &d->pasted, "the pasted image");
    if (zui_button(d->ui, "Copy test pattern"))
        copy_image(d, &d->pattern, "the test pattern");
    if (zui_button(d->ui, "Copy text"))
    {
        clipboard_set("text from the zen_platform clipboard demo");
        set_status(d, "Copied %s", "text");
    }
    if (zui_button(d->ui, "Clear"))
    {
        clipboard_set_items(NULL, 0);
        set_status(d, "Cleared %s", "the clipboard");
    }

    zui_row(d->ui, 24, 1);
    zui_label(d->ui, d->status[0] ? d->status : "Copy an image in any program, then press Ctrl+V here.");

    if (zui_window_begin(d->ui, "Clipboard", &d->panel))
    {
        char line[200];
        zui_row(d->ui, 24, 1);
        snprintf(line, sizeof line, "Text:   %s", d->has_text ? "yes" : "no");
        zui_label(d->ui, line);
        if (d->has_text)
        {
            snprintf(line, sizeof line, "  \"%s\"", d->text_preview);
            zui_label(d->ui, line);
        }
        snprintf(line, sizeof line, "Image (PNG):   %s", d->has_png ? "yes" : "no");
        zui_label(d->ui, line);
        snprintf(line, sizeof line, "Files:   %s", d->has_uris ? "yes" : "no");
        zui_label(d->ui, line);
        for (int i = 0; i < d->uri_count; i++)
        {
            snprintf(line, sizeof line, "  %s", d->uris[i]);
            zui_label(d->ui, line);
        }
        if (d->uri_total > d->uri_count)
        {
            snprintf(line, sizeof line, "  ... and %d more", d->uri_total - d->uri_count);
            zui_label(d->ui, line);
        }
        if (d->has_pasted)
        {
            snprintf(line, sizeof line, "Pasted:   %d x %d", d->pasted.width, d->pasted.height);
            zui_label(d->ui, line);
        }

        zui_row(d->ui, 28, 2);
        if (zui_button(d->ui, "Refresh"))
            refresh(d);
        if (zui_button(d->ui, "Save pasted.bmp") && d->has_pasted)
            set_status(d, framebuffer_save_bmp(&d->pasted, "pasted.bmp") ? "Saved %s" : "Could not save %s", "pasted.bmp");
        zui_window_end(d->ui);
    }

    zui_end(d->ui);
    zui_render_software(&fb, zui_draw_list(d->ui));
    window_present_pixels(w);
}

int main(int argc, char **argv)
{
    if (!platform_init())
        return 1;

    WindowConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.title = "zen_platform clipboard";
    cfg.width = 1100;
    cfg.height = 700;
    cfg.x = WINDOW_POS_CENTERED;
    cfg.y = WINDOW_POS_CENTERED;
    cfg.render = RENDER_PIXELS;
    cfg.resizable = true;

    PlatformWindow *w = window_create(&cfg);
    if (!w)
    {
        fprintf(stderr, "window_create: %s\n", platform_get_error());
        platform_shutdown();
        return 1;
    }

    Demo d;
    memset(&d, 0, sizeof d);
    d.ui = zui_create();
    UiStyle s;
    zui_style_dark(&s);
    zui_set_style(d.ui, &s);
    d.panel = zui_rect(cfg.width - 330, 100, 320, 360);
    make_pattern(&d.pattern);
    d.paste_on_start = argc > 1 && strcmp(argv[1], "--paste") == 0;
    refresh(&d);

    app_run(w, frame, &d);

    if (d.has_pasted)
        framebuffer_free(&d.pasted);
    framebuffer_free(&d.pattern);
    zui_destroy(d.ui);
    window_destroy(w);
    platform_shutdown();
    return 0;
}
