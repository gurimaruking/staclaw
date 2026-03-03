#include "bsp_audio.h"
#include "bsp_power.h"
#include "bsp_pins.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "driver/i2s_pdm.h"
#include <string.h>

static const char *TAG = "bsp_audio";

static i2s_chan_handle_t s_tx_handle = NULL;
static i2s_chan_handle_t s_rx_handle = NULL;
static bool s_initialized = false;

esp_err_t bsp_audio_init(void)
{
    if (s_initialized) return ESP_OK;
    s_initialized = true;
    ESP_LOGI(TAG, "Audio subsystem initialized");
    return ESP_OK;
}

/* ---- Speaker ---- */

esp_err_t bsp_audio_speaker_start(int sample_rate)
{
    if (s_tx_handle) {
        ESP_LOGW(TAG, "Speaker already started");
        return ESP_OK;
    }
    if (s_rx_handle) {
        ESP_LOGE(TAG, "Mic active, stop it before starting speaker");
        return ESP_ERR_INVALID_STATE;
    }

    /* Create I2S TX channel */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 6;
    chan_cfg.dma_frame_num = 240;
    esp_err_t err = i2s_new_channel(&chan_cfg, &s_tx_handle, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create TX channel: %s", esp_err_to_name(err));
        return err;
    }

    /* Configure standard I2S for NS4168 */
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)BSP_I2S_BCLK,
            .ws   = (gpio_num_t)BSP_I2S_LRCK,
            .dout = (gpio_num_t)BSP_I2S_DATA_OUT,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    err = i2s_channel_init_std_mode(s_tx_handle, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init std TX: %s", esp_err_to_name(err));
        i2s_del_channel(s_tx_handle);
        s_tx_handle = NULL;
        return err;
    }

    err = i2s_channel_enable(s_tx_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable TX: %s", esp_err_to_name(err));
        i2s_del_channel(s_tx_handle);
        s_tx_handle = NULL;
        return err;
    }

    /* Enable speaker amplifier */
    bsp_power_set_speaker(true);

    ESP_LOGI(TAG, "Speaker started (%d Hz)", sample_rate);
    return ESP_OK;
}

esp_err_t bsp_audio_speaker_write(const int16_t *data, size_t samples, int timeout_ms)
{
    if (!s_tx_handle) return ESP_ERR_INVALID_STATE;

    size_t bytes_written = 0;
    esp_err_t err = i2s_channel_write(s_tx_handle, data, samples * sizeof(int16_t),
                                       &bytes_written, timeout_ms);
    return err;
}

esp_err_t bsp_audio_speaker_stop(void)
{
    if (!s_tx_handle) return ESP_OK;

    /* Disable amplifier first to avoid pop */
    bsp_power_set_speaker(false);

    i2s_channel_disable(s_tx_handle);
    i2s_del_channel(s_tx_handle);
    s_tx_handle = NULL;

    ESP_LOGI(TAG, "Speaker stopped");
    return ESP_OK;
}

/* ---- Microphone ---- */

esp_err_t bsp_audio_mic_start(int sample_rate)
{
    if (s_rx_handle) {
        ESP_LOGW(TAG, "Mic already started");
        return ESP_OK;
    }
    if (s_tx_handle) {
        ESP_LOGE(TAG, "Speaker active, stop it before starting mic");
        return ESP_ERR_INVALID_STATE;
    }

    /* Create I2S RX channel */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 6;
    chan_cfg.dma_frame_num = 240;
    esp_err_t err = i2s_new_channel(&chan_cfg, NULL, &s_rx_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create RX channel: %s", esp_err_to_name(err));
        return err;
    }

    /* Configure PDM for SPM1423 mic */
    i2s_pdm_rx_config_t pdm_cfg = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(sample_rate),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk = (gpio_num_t)BSP_I2S_LRCK,
            .din = (gpio_num_t)BSP_I2S_DATA_IN,
            .invert_flags = {
                .clk_inv = false,
            },
        },
    };

    err = i2s_channel_init_pdm_rx_mode(s_rx_handle, &pdm_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init PDM RX: %s", esp_err_to_name(err));
        i2s_del_channel(s_rx_handle);
        s_rx_handle = NULL;
        return err;
    }

    err = i2s_channel_enable(s_rx_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable RX: %s", esp_err_to_name(err));
        i2s_del_channel(s_rx_handle);
        s_rx_handle = NULL;
        return err;
    }

    ESP_LOGI(TAG, "Microphone started (%d Hz PDM)", sample_rate);
    return ESP_OK;
}

esp_err_t bsp_audio_mic_read(int16_t *data, size_t samples, size_t *read_count, int timeout_ms)
{
    if (!s_rx_handle) return ESP_ERR_INVALID_STATE;

    size_t bytes_read = 0;
    esp_err_t err = i2s_channel_read(s_rx_handle, data, samples * sizeof(int16_t),
                                      &bytes_read, timeout_ms);
    if (read_count) {
        *read_count = bytes_read / sizeof(int16_t);
    }
    return err;
}

esp_err_t bsp_audio_mic_stop(void)
{
    if (!s_rx_handle) return ESP_OK;

    i2s_channel_disable(s_rx_handle);
    i2s_del_channel(s_rx_handle);
    s_rx_handle = NULL;

    ESP_LOGI(TAG, "Microphone stopped");
    return ESP_OK;
}
