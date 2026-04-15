#include "ui_chat.h"
#include "ui_manager.h"
#include "ui_status.h"
#include "bsp_display.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "ui_chat";

/* Layout constants */
#define CHAT_TOP        UI_STATUS_BAR_H     /* Below status bar */
#define CHAT_BOTTOM     (BSP_LCD_H - BTN_BAR_H)  /* Above bottom button area */
#define CHAT_LEFT       4
#define CHAT_RIGHT      (BSP_LCD_W - 4)
#define CHAT_WIDTH      (CHAT_RIGHT - CHAT_LEFT)
#define CHAT_LINE_H     16   /* Font height */
#define CHAT_MSG_PAD    2    /* Padding between messages */
#define CHAT_VISIBLE_LINES  ((CHAT_BOTTOM - CHAT_TOP) / CHAT_LINE_H)

/* Colors */
#define COLOR_BG         BSP_COLOR_BLACK
#define COLOR_USER_FG    bsp_display_color(0x80, 0xD0, 0xFF)   /* Light blue */
#define COLOR_ASST_FG    bsp_display_color(0xA0, 0xFF, 0xA0)   /* Light green */
#define COLOR_SYS_FG     bsp_display_color(0xFF, 0xFF, 0x80)   /* Light yellow */
#define COLOR_LABEL_FG   bsp_display_color(0x80, 0x80, 0x80)   /* Gray */
#define COLOR_BTN_BG     bsp_display_color(0x30, 0x30, 0x40)   /* Dark blue-gray */
#define COLOR_BTN_FG     BSP_COLOR_WHITE

/* Message storage */
typedef struct {
    ui_chat_role_t role;
    char text[UI_CHAT_MAX_TEXT_LEN];
} chat_message_t;

static chat_message_t s_messages[UI_CHAT_MAX_MESSAGES];
static int s_msg_count = 0;
static int s_msg_head = 0;  /* Ring buffer head */
static int s_scroll_offset = 0;  /* Lines scrolled up from bottom */
static SemaphoreHandle_t s_chat_mutex = NULL;
static ui_chat_send_cb_t s_send_cb = NULL;
static ui_chat_mic_cb_t s_mic_cb = NULL;

/* Quick-reply button labels (UTF-8) */
static const char *s_quick_replies[] = {
    "\xe3\x81\x93\xe3\x82\x93\xe3\x81\xab\xe3\x81\xa1\xe3\x81\xaf",  /* こんにちは */
    "Status?",
};
#define NUM_QUICK_REPLIES  2

/* Bottom bar layout: two rows of 32px each */
#define BTN_BAR_H        32
#define CHAT_BOTTOM_AREA (BSP_LCD_H - BTN_BAR_H)

/* ---- Helpers ---- */

static int msg_index(int i)
{
    /* Get actual index in ring buffer for logical message i (0 = oldest) */
    return (s_msg_head + i) % UI_CHAT_MAX_MESSAGES;
}

static int utf8_char_width(const char *s, int *bytes_out)
{
    uint8_t c = (uint8_t)s[0];
    if (c < 0x80) {
        *bytes_out = 1;
        return 1;  /* ASCII: 1 column (8px) */
    }
    if ((c & 0xE0) == 0xC0) { *bytes_out = 2; }
    else if ((c & 0xF0) == 0xE0) { *bytes_out = 3; }
    else if ((c & 0xF8) == 0xF0) { *bytes_out = 4; }
    else { *bytes_out = 1; return 1; }
    return 2;  /* Non-ASCII: 2 columns (16px) */
}

static int count_lines(const char *text, int max_chars_per_line)
{
    int lines = 1;
    int col = 0;
    while (*text) {
        if (*text == '\n') {
            lines++;
            col = 0;
            text++;
        } else {
            int bytes;
            int w = utf8_char_width(text, &bytes);
            col += w;
            if (col > max_chars_per_line) {
                lines++;
                col = w;
            }
            text += bytes;
        }
    }
    return lines;
}

/* ---- Screen callbacks ---- */

static void chat_on_enter(void)
{
    /* Clear chat area */
    bsp_display_fill(0, CHAT_TOP, BSP_LCD_W, BSP_LCD_H - CHAT_TOP, COLOR_BG);
}

static void chat_on_draw(void)
{
    xSemaphoreTake(s_chat_mutex, portMAX_DELAY);

    /* Clear chat area */
    bsp_display_fill(0, CHAT_TOP, BSP_LCD_W, CHAT_BOTTOM - CHAT_TOP, COLOR_BG);

    int chars_per_line = CHAT_WIDTH / 8;  /* 8px per char */
    if (chars_per_line < 1) chars_per_line = 1;

    /* Calculate total lines needed for all messages */
    int total_lines = 0;
    for (int i = 0; i < s_msg_count; i++) {
        int mi = msg_index(i);
        total_lines += 1;  /* Role label line */
        total_lines += count_lines(s_messages[mi].text, chars_per_line);
        total_lines += 1;  /* Spacing after message */
    }

    ESP_LOGI(TAG, "draw: msgs=%d lines=%d vis=%d start_y=%d",
             s_msg_count, total_lines, CHAT_VISIBLE_LINES, CHAT_TOP);

    /* Start drawing from bottom, respecting scroll offset */
    int visible_lines = CHAT_VISIBLE_LINES;
    int start_line = total_lines - visible_lines - s_scroll_offset;
    if (start_line < 0) start_line = 0;

    /* Walk through messages and draw visible portion */
    int current_line = 0;
    uint16_t draw_y = CHAT_TOP;

    for (int i = 0; i < s_msg_count && draw_y < CHAT_BOTTOM; i++) {
        int mi = msg_index(i);
        chat_message_t *msg = &s_messages[mi];

        /* Role label */
        const char *label;
        uint16_t fg_color;
        switch (msg->role) {
        case UI_CHAT_ROLE_USER:
            label = "You:";
            fg_color = COLOR_USER_FG;
            break;
        case UI_CHAT_ROLE_ASSISTANT:
            label = "AI:";
            fg_color = COLOR_ASST_FG;
            break;
        default:
            label = "Sys:";
            fg_color = COLOR_SYS_FG;
            break;
        }

        int msg_lines = 1 + count_lines(msg->text, chars_per_line) + 1;

        if (current_line + msg_lines <= start_line) {
            /* Entire message is above visible area */
            current_line += msg_lines;
            continue;
        }

        int line_in_msg = 0;

        /* Draw label if visible */
        if (current_line >= start_line) {
            ui_draw_text(CHAT_LEFT, draw_y, CHAT_WIDTH, label, COLOR_LABEL_FG, COLOR_BG);
            draw_y += CHAT_LINE_H;
        }
        current_line++;
        line_in_msg++;

        /* Draw message text */
        int text_lines = count_lines(msg->text, chars_per_line);
        if (draw_y < CHAT_BOTTOM && (current_line + text_lines) > start_line) {
            /* Text overlaps visible area — draw from current draw_y.
             * If the text starts above the visible window, we still render
             * it from draw_y (which is at CHAT_TOP), showing the tail end. */
            draw_y = ui_draw_text(CHAT_LEFT, draw_y, CHAT_WIDTH, msg->text, fg_color, COLOR_BG);
        }
        current_line += text_lines;

        /* Spacing */
        if (current_line >= start_line && draw_y < CHAT_BOTTOM) {
            draw_y += CHAT_MSG_PAD;
        }
        current_line++;
    }

    /* Draw bottom button bar */
    bsp_display_fill(0, CHAT_BOTTOM, BSP_LCD_W, BTN_BAR_H, COLOR_BTN_BG);

    /* Buttons: [Hello!] [Status?] [Mic] [Sys] */
    int num_btns = NUM_QUICK_REPLIES + 2;  /* +1 mic, +1 nav */
    int btn_w = BSP_LCD_W / num_btns;
    int btn_y = CHAT_BOTTOM + 8;

    for (int i = 0; i < NUM_QUICK_REPLIES; i++) {
        int tx = btn_w * i + 4;
        ui_draw_text(tx, btn_y, btn_w - 8, s_quick_replies[i], COLOR_BTN_FG, COLOR_BTN_BG);
    }
    /* Mic button */
    int mic_x = btn_w * NUM_QUICK_REPLIES + 4;
    uint16_t mic_color = s_mic_cb ? bsp_display_color(0xFF, 0x60, 0x60) : COLOR_LABEL_FG;
    ui_draw_text(mic_x, btn_y, btn_w - 8, "Mic", mic_color, COLOR_BTN_BG);
    /* Nav button */
    int nav_x = btn_w * (NUM_QUICK_REPLIES + 1) + 4;
    ui_draw_text(nav_x, btn_y, btn_w - 8, "Sys", COLOR_LABEL_FG, COLOR_BTN_BG);

    xSemaphoreGive(s_chat_mutex);
}

static void chat_on_touch(uint16_t x, uint16_t y)
{
    /* Bottom button bar touch handling */
    if (y >= CHAT_BOTTOM) {
        int num_btns = NUM_QUICK_REPLIES + 2;
        int btn_w = BSP_LCD_W / num_btns;
        int btn_idx = x / btn_w;

        if (btn_idx < NUM_QUICK_REPLIES) {
            /* Quick-reply button pressed */
            if (s_send_cb) {
                s_send_cb(s_quick_replies[btn_idx]);
            }
        } else if (btn_idx == NUM_QUICK_REPLIES) {
            /* Mic button pressed */
            if (s_mic_cb) {
                s_mic_cb();
            }
        } else {
            /* Nav button - switch to status screen */
            ui_manager_switch_screen(UI_SCREEN_STATUS);
        }
        return;
    }

    /* Tap in chat area: scroll to bottom (reset scroll) */
    xSemaphoreTake(s_chat_mutex, portMAX_DELAY);
    if (s_scroll_offset > 0) {
        s_scroll_offset = 0;
        xSemaphoreGive(s_chat_mutex);
        ui_manager_request_redraw();
    } else {
        xSemaphoreGive(s_chat_mutex);
    }
}

static void chat_on_gesture(ui_gesture_t gesture, int16_t delta)
{
    /* Swipe to scroll chat history */
    int scroll_lines = delta / 16;  /* Convert pixel delta to line count */
    if (scroll_lines < 1) scroll_lines = 1;

    xSemaphoreTake(s_chat_mutex, portMAX_DELAY);
    if (gesture == UI_GESTURE_SWIPE_UP) {
        /* Swipe up: scroll to see older messages */
        s_scroll_offset += scroll_lines;
    } else {
        /* Swipe down: scroll to see newer messages */
        s_scroll_offset -= scroll_lines;
        if (s_scroll_offset < 0) s_scroll_offset = 0;
    }
    xSemaphoreGive(s_chat_mutex);
    ui_manager_request_redraw();
}

/* ---- Public API ---- */

esp_err_t ui_chat_init(void)
{
    s_chat_mutex = xSemaphoreCreateMutex();
    if (!s_chat_mutex) return ESP_ERR_NO_MEM;

    s_msg_count = 0;
    s_msg_head = 0;
    s_scroll_offset = 0;

    static const ui_screen_ops_t chat_ops = {
        .on_enter   = chat_on_enter,
        .on_draw    = chat_on_draw,
        .on_touch   = chat_on_touch,
        .on_gesture = chat_on_gesture,
    };
    ui_manager_register_screen(UI_SCREEN_CHAT, &chat_ops);

    ESP_LOGI(TAG, "Chat screen initialized");
    return ESP_OK;
}

void ui_chat_add_message(ui_chat_role_t role, const char *text)
{
    if (!text) return;
    ESP_LOGI(TAG, "add_msg role=%d len=%d: %.40s", role, (int)strlen(text), text);

    xSemaphoreTake(s_chat_mutex, portMAX_DELAY);

    int slot;
    if (s_msg_count < UI_CHAT_MAX_MESSAGES) {
        /* Buffer not full yet - append at end */
        slot = (s_msg_head + s_msg_count) % UI_CHAT_MAX_MESSAGES;
        s_msg_count++;
    } else {
        /* Buffer full - overwrite oldest (at head), advance head */
        slot = s_msg_head;
        s_msg_head = (s_msg_head + 1) % UI_CHAT_MAX_MESSAGES;
    }

    s_messages[slot].role = role;
    strncpy(s_messages[slot].text, text, UI_CHAT_MAX_TEXT_LEN - 1);
    s_messages[slot].text[UI_CHAT_MAX_TEXT_LEN - 1] = '\0';

    /* Auto-scroll to bottom on new message */
    s_scroll_offset = 0;

    xSemaphoreGive(s_chat_mutex);

    ui_manager_request_redraw();
}

void ui_chat_stream_update(const char *text)
{
    if (!text) return;

    xSemaphoreTake(s_chat_mutex, portMAX_DELAY);

    if (s_msg_count == 0) {
        /* No messages yet, create one */
        xSemaphoreGive(s_chat_mutex);
        ui_chat_add_message(UI_CHAT_ROLE_ASSISTANT, text);
        return;
    }

    /* Find last message */
    int last = (s_msg_head + s_msg_count - 1) % UI_CHAT_MAX_MESSAGES;

    if (s_messages[last].role != UI_CHAT_ROLE_ASSISTANT) {
        /* Last message isn't assistant, create new one */
        xSemaphoreGive(s_chat_mutex);
        ui_chat_add_message(UI_CHAT_ROLE_ASSISTANT, text);
        return;
    }

    /* Update last assistant message */
    strncpy(s_messages[last].text, text, UI_CHAT_MAX_TEXT_LEN - 1);
    s_messages[last].text[UI_CHAT_MAX_TEXT_LEN - 1] = '\0';

    xSemaphoreGive(s_chat_mutex);

    ui_manager_request_redraw();
}

void ui_chat_clear(void)
{
    xSemaphoreTake(s_chat_mutex, portMAX_DELAY);
    s_msg_count = 0;
    s_msg_head = 0;
    s_scroll_offset = 0;
    xSemaphoreGive(s_chat_mutex);
}

void ui_chat_set_send_callback(ui_chat_send_cb_t cb)
{
    s_send_cb = cb;
}

void ui_chat_set_mic_callback(ui_chat_mic_cb_t cb)
{
    s_mic_cb = cb;
}
