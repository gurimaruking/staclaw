#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_sntp.h"
#include "nvs_flash.h"

#include "app_config.h"
#include "bsp_core2.h"
#include "config_manager.h"
#include "net_wifi.h"
#include "llm_claude.h"
#include "llm_openai.h"
#include "llm_provider.h"
#include "memory_store.h"
#include "agent.h"
#include "tool_registry.h"
#include "tool_sysinfo.h"
#include "tool_memory.h"
#include "tool_gpio.h"
#include "channel_router.h"
#include "channel_telegram.h"
#include "channel_touch.h"
#include "channel_voice.h"
#include "cron_engine.h"
#include "tool_cron.h"
#include "ui_manager.h"
#include "ui_chat.h"
#include "ui_status.h"
#include "ui_status_screen.h"
#include "ui_settings.h"

static const char *TAG = "staclaw";

static agent_t s_agent;
static tool_registry_t s_tools;

static void mic_button_cb(void)
{
    channel_voice_trigger();
}

static void cron_trigger_cb(const char *job_name, const char *message, void *user_data)
{
    (void)user_data;
    ESP_LOGI(TAG, "Cron job fired: %s", job_name);

    channel_message_t msg = {
        .channel = CHANNEL_TOUCH,  // Route cron through touch channel for UI display
        .text = (char *)message,
        .sender_id = "cron",
        .channel_data = NULL,
    };

    ui_chat_add_message(UI_CHAT_ROLE_SYSTEM, job_name);
    ui_manager_request_redraw();
    channel_router_handle(&msg);
}

static void init_sntp(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
}

static void init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated, erasing...");
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== staclaw v0.1.0 ===");
    ESP_LOGI(TAG, "M5Stack Core2 AI Agent");
    ESP_LOGI(TAG, "Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());

    // 1. Initialize NVS
    init_nvs();

    // 2. Initialize BSP (power, display, touch)
    esp_err_t err = bsp_core2_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BSP init failed: %s", esp_err_to_name(err));
        // Continue anyway - core functionality doesn't require display
    }

    // 3. Initialize UI (screens + task, needs BSP display/touch)
    ui_status_init();
    ui_chat_init();
    ui_status_screen_init();
    ui_settings_init();
    err = ui_manager_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "UI init failed: %s", esp_err_to_name(err));
    } else {
        ui_chat_add_message(UI_CHAT_ROLE_SYSTEM, "Booting...");
    }

    // 4. Load configuration
    staclaw_config_t config;
    err = config_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Config init failed: %s", esp_err_to_name(err));
        return;
    }

    // 5. Create default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 6. Connect to WiFi
    ESP_LOGI(TAG, "Connecting to WiFi: %s", config.wifi_ssid);
    ui_chat_add_message(UI_CHAT_ROLE_SYSTEM, "Connecting WiFi...");
    err = net_wifi_init(config.wifi_ssid, config.wifi_password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi failed: %s", esp_err_to_name(err));
        ui_chat_add_message(UI_CHAT_ROLE_SYSTEM, "WiFi FAILED!");
        return;
    }
    ESP_LOGI(TAG, "WiFi connected");
    ui_status_set_wifi(true);
    ui_manager_request_redraw();

    // Sync time via SNTP (needed for cron and TLS)
    init_sntp();

    // 7. Initialize SPIFFS for memory storage
    err = memory_store_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS init failed: %s", esp_err_to_name(err));
        return;
    }

    // 6. Register LLM providers
    if (config.claude_api_key[0]) {
        err = llm_claude_register(config.claude_api_key,
                                   config.claude_model[0] ? config.claude_model : "claude-sonnet-4-20250514");
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Claude registration failed: %s", esp_err_to_name(err));
        }
    }

    if (config.openai_api_key[0]) {
        err = llm_openai_register(config.openai_api_key,
                                   config.openai_model[0] ? config.openai_model : "gpt-4o-mini");
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "OpenAI registration failed: %s", esp_err_to_name(err));
        }
    }

    // Set active provider preference
    if (config.active_provider[0]) {
        llm_set_active_provider(config.active_provider);
    }

    // 7. Initialize tools
    tool_registry_init(&s_tools);
    tool_sysinfo_register(&s_tools);
    tool_memory_register(&s_tools);
    tool_gpio_register(&s_tools);
    tool_cron_register(&s_tools);
    ESP_LOGI(TAG, "Tools registered: %d", s_tools.count);

    if (!llm_get_active_provider()) {
        ESP_LOGE(TAG, "No LLM provider available! Set API keys in NVS.");
        ESP_LOGE(TAG, "Use: config_set_string(\"claude_key\", \"sk-ant-...\")");
        ESP_LOGW(TAG, "Entering idle mode (tools ready, no agent)");
        ui_chat_add_message(UI_CHAT_ROLE_SYSTEM, "No LLM key! Set via NVS.");
        ui_status_set_provider("No LLM");
    } else {
        ESP_LOGI(TAG, "Active LLM: %s", llm_get_active_provider()->name);
        ui_status_set_provider(llm_get_active_provider()->name);

        // 8. Initialize agent
        agent_config_t agent_cfg = {
            .max_iterations = AGENT_MAX_ITERATIONS,
            .max_tokens = 1024,
            .tools = &s_tools,
        };

        err = agent_init(&s_agent, &agent_cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Agent init failed: %s", esp_err_to_name(err));
            return;
        }

        // 9. Initialize channel router
        channel_router_init(&s_agent);

        // 9b. Initialize cron engine
        err = cron_engine_init(cron_trigger_cb, NULL);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Cron init failed: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "Cron engine active");
        }

        // 10. Start touch channel (LCD quick-reply -> agent)
        err = channel_touch_init();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Touch channel init failed: %s", esp_err_to_name(err));
        } else {
            /* Wire chat quick-reply buttons to touch channel */
            ui_chat_set_send_callback(channel_touch_send);
            ESP_LOGI(TAG, "Touch channel active");
        }

        // 11. Start Telegram channel (if token configured)
        if (config.telegram_token[0]) {
            err = channel_telegram_init(config.telegram_token);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Telegram init failed: %s", esp_err_to_name(err));
            } else {
                ESP_LOGI(TAG, "Telegram bot active");
            }
        } else {
            ESP_LOGW(TAG, "No Telegram token, bot disabled");
        }

        // 12. Start voice channel (if OpenAI key configured for Whisper/TTS)
        if (config.openai_api_key[0]) {
            err = channel_voice_init(config.openai_api_key,
                                      config.tts_voice[0] ? config.tts_voice : "alloy",
                                      config.vad_threshold);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Voice channel init failed: %s", esp_err_to_name(err));
            } else {
                ui_chat_set_mic_callback(mic_button_cb);
                ESP_LOGI(TAG, "Voice channel active");
            }
        } else {
            ESP_LOGW(TAG, "No OpenAI key, voice disabled");
        }
    }

    // Report status
    ESP_LOGI(TAG, "=== staclaw ready ===");
    ESP_LOGI(TAG, "Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());
    ui_chat_add_message(UI_CHAT_ROLE_SYSTEM, "staclaw ready!");

    // Main task: keep alive and monitor
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000));  // 30 second heartbeat

        ESP_LOGD(TAG, "Heartbeat - heap: %lu, min: %lu",
                 (unsigned long)esp_get_free_heap_size(),
                 (unsigned long)esp_get_minimum_free_heap_size());
    }
}
