/*
 * uitest.c - Um teste Immediate Mode GUI (IMGUI) usando RENDER_PIXELS.
 * Valida o rato (movimento, botões, clicks) e desenha primitivas básicas.
 */
#include "platform.h"
#include <stdio.h>

/* ========================================================================== */
/* IMGUI Simples                                                             */
/* ========================================================================== */

typedef struct
{
    Framebuffer fb;
    int hot_item;
    int active_item;
} AppState;

/* Gera IDs únicos baseados na linha de código */
#define UI_ID (__LINE__)

bool ui_button(AppState *app, PlatformWindow *w, int id, int x, int y, int bw, int bh, uint32_t color)
{
    int mx = mouse_x(w);
    int my = mouse_y(w);
    bool hover = (mx >= x && mx < x + bw && my >= y && my < y + bh);

    if (hover)
    {
        app->hot_item = id;
        if (mouse_button_pressed(w, MOUSE_LEFT))
        {
            app->active_item = id;
        }
    }

    bool clicked = false;
    /* Click só é válido se largares o botão do rato em cima do mesmo widget onde clicaste */
    if (mouse_button_released(w, MOUSE_LEFT) && app->hot_item == id && app->active_item == id)
    {
        clicked = true;
    }

    /* Cores de feedback visual */
    uint32_t current_color = color;
    if (app->hot_item == id)
    {
        if (app->active_item == id)
            current_color = 0xFF505050; /* Clicado (escuro) */
        else
            current_color = 0xFFA0A0A0; /* Hover (claro) */
    }

    draw_fill_rect(&app->fb, x, y, bw, bh, current_color, BLEND_NONE);
    draw_rect(&app->fb, x, y, bw, bh, 0xFFFFFFFF, BLEND_NONE); /* Borda branca */

    return clicked;
}

bool ui_checkbox(AppState *app, PlatformWindow *w, int id, int x, int y, int size, bool *value)
{
    int mx = mouse_x(w);
    int my = mouse_y(w);
    bool hover = (mx >= x && mx < x + size && my >= y && my < y + size);

    if (hover)
    {
        app->hot_item = id;
        if (mouse_button_pressed(w, MOUSE_LEFT))
        {
            app->active_item = id;
        }
    }

    if (mouse_button_released(w, MOUSE_LEFT) && app->hot_item == id && app->active_item == id)
    {
        *value = !(*value); /* Alterna o valor */
    }

    uint32_t bg_color = (app->hot_item == id) ? 0xFF606060 : 0xFF303030;

    draw_fill_rect(&app->fb, x, y, size, size, bg_color, BLEND_NONE);
    draw_rect(&app->fb, x, y, size, size, 0xFFFFFFFF, BLEND_NONE);

    if (*value)
    {
        /* Desenha um "X" simples usando rects (como ainda não há linhas perfeitas grossas) */
        draw_fill_rect(&app->fb, x + 4, y + 4, size - 8, size - 8, 0xFF20FF20, BLEND_NONE);
    }

    return *value;
}

void ui_slider(AppState *app, PlatformWindow *w, int id, int x, int y, int bw, int bh, float *value)
{
    int mx = mouse_x(w);
    int my = mouse_y(w);
    bool hover = (mx >= x && mx < x + bw && my >= y && my < y + bh);

    if (hover)
    {
        app->hot_item = id;
        if (mouse_button_pressed(w, MOUSE_LEFT))
        {
            app->active_item = id;
        }
    }

    /* Se estiver ativo, arrastar o rato altera o valor (clamped entre 0.0 e 1.0) */
    if (app->active_item == id)
    {
        float normalized = (float)(mx - x) / (float)bw;
        if (normalized < 0.0f)
            normalized = 0.0f;
        if (normalized > 1.0f)
            normalized = 1.0f;
        *value = normalized;
    }

    /* Fundo da pista do slider */
    draw_fill_rect(&app->fb, x, y, bw, bh, 0xFF202020, BLEND_NONE);
    draw_rect(&app->fb, x, y, bw, bh, 0xFF888888, BLEND_NONE);

    /* Parte preenchida (valor atual) */
    int fill_width = (int)((*value) * bw);
    uint32_t fill_color = (app->active_item == id) ? 0xFF4080FF : 0xFF2060C0;
    draw_fill_rect(&app->fb, x, y, fill_width, bh, fill_color, BLEND_NONE);
}

/* ========================================================================== */
/* O Loop Principal                                                          */
/* ========================================================================== */

static bool g_checkbox_state = false;
static float g_slider_value = 0.5f;

static void frame(PlatformWindow *w, void *user)
{
    AppState *app = user;
    Event e;

    /* Esvaziar a fila de eventos (necessário mesmo que usemos só o state) */
    while (poll_event(w, &e))
    {
    }

    /* Começar frame IMGUI: reset ao hot_item; active_item só limpa DEPOIS
       dos widgets para que o click detection ainda o encontre ativo. */
    app->hot_item = 0;

    /* Limpar o Framebuffer */
    if (window_lock_pixels(w, &app->fb))
    {
        draw_clear(&app->fb, 0xFF181818); /* Fundo da app */

        /* 1. Desenhar a janela/painel de fundo */
        int panel_x = 50, panel_y = 50;
        draw_fill_rect(&app->fb, panel_x, panel_y, 300, 250, 0xFF2A2A2A, BLEND_NONE);
        draw_rect(&app->fb, panel_x, panel_y, 300, 250, 0xFF555555, BLEND_NONE);

        /* Simular uma barra de título */
        draw_fill_rect(&app->fb, panel_x, panel_y, 300, 24, 0xFF151515, BLEND_NONE);

        /* 2. O nosso Botão */
        if (ui_button(app, w, UI_ID, panel_x + 20, panel_y + 40, 120, 30, 0xFF707070))
        {
            printf("Botao clicado!\n");
        }

        /* 3. A Checkbox */
        if (ui_checkbox(app, w, UI_ID, panel_x + 20, panel_y + 90, 20, &g_checkbox_state))
        {
            /* Desenha um círculo decorativo se a checkbox estiver ligada */
            draw_fill_circle(&app->fb, panel_x + 200, panel_y + 100, 20, 0xFF20FF20, BLEND_NONE);
        }
        else
        {
            draw_circle(&app->fb, panel_x + 200, panel_y + 100, 20, 0xFF888888, BLEND_NONE);
        }

        /* 4. O Slider */
        ui_slider(app, w, UI_ID, panel_x + 20, panel_y + 140, 200, 20, &g_slider_value);

        /* Feedback visual do valor do slider manipulando uma cor */
        uint8_t c_val = (uint8_t)(g_slider_value * 255.0f);
        uint32_t dynamic_color = 0xFF000000 | (c_val << 16) | (c_val << 8) | c_val;
        draw_fill_rect(&app->fb, panel_x + 240, panel_y + 140, 20, 20, dynamic_color, BLEND_NONE);

        /* Limpa active_item no frame do release (só depois dos widgets
           terem tido oportunidade de detetar o click). */
        if (mouse_button_released(w, MOUSE_LEFT))
            app->active_item = 0;

        /* Rato customizado puramente em software rendering! */
        int mx = mouse_x(w), my = mouse_y(w);
        draw_fill_rect(&app->fb, mx, my, 4, 4, 0xFFFF0000, BLEND_NONE);

        window_present_pixels(w);
    }
}

int main(void)
{
    if (!platform_init())
        return 1;

    WindowConfig cfg = {
        .title = "UI Input Test",
        .width = 800,
        .height = 600,
        .x = WINDOW_POS_CENTERED,
        .y = WINDOW_POS_CENTERED,
        .render = RENDER_PIXELS};

    PlatformWindow *w = window_create(&cfg);
    if (!w)
        return 1;

    /* Esconder o cursor nativo porque vamos desenhar o nosso cursor vermelho a software */
    mouse_set_mode(w, MOUSE_MODE_HIDDEN);
    key_set_exit(w, KEY_ESCAPE);

    AppState app = {0};

    app_run(w, frame, &app);

    window_destroy(w);
    platform_shutdown();

    return 0;
}