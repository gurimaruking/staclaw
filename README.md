# staclaw

AI agent running on M5Stack Core2 (ESP32), built with ESP-IDF in C.

Inspired by open-source AI agent projects like OpenClaw, PicoClaw, and zclaw.

## Features

- **Multi-LLM support** - Claude (Anthropic) and OpenAI with runtime switching
- **ReAct agent loop** - Think → Act → Observe with tool calling
- **Touch screen UI** - Chat, system status, and settings screens on ILI9342C LCD
- **Telegram bot** - Remote chat interface via long-polling
- **Voice pipeline** - Push-to-talk with Whisper STT and OpenAI TTS
- **Tool system** - GPIO control, system info, persistent memory, cron scheduling
- **Cron engine** - Schedule recurring agent tasks with cron expressions
- **Persistent memory** - SOUL.md personality, USER.md profile, MEMORY.md long-term storage on SPIFFS

## Architecture

```
[Touch UI] [Telegram] [Voice]    ← Channels
         \     |     /
      [Channel Router]           ← Message routing
              |
         [Agent Loop]            ← ReAct (Think → Act → Observe)
        /     |      \
   [LLM]  [Tools]  [Memory]     ← Core
   /   \
[Claude] [OpenAI]               ← LLM providers
```

## Hardware

- M5Stack Core2 (ESP32-D0WDQ6, 8MB PSRAM, 16MB Flash)
- ILI9342C 320x240 LCD (SPI)
- FT6336U capacitive touch
- AXP192 power management
- NS4168 speaker + SPM1423 PDM microphone (I2S)

## Project Structure

```
staclaw/
├── main/              # app_main, config
├── components/
│   ├── agent/         # ReAct agent loop
│   ├── bsp_core2/     # Hardware drivers (display, touch, power, audio)
│   ├── channel/       # Input/output channels (touch, telegram, voice)
│   ├── config/        # NVS configuration manager
│   ├── cron/          # Cron scheduler engine
│   ├── llm/           # LLM provider abstraction (Claude, OpenAI)
│   ├── memory/        # SPIFFS persistent memory + conversation history
│   ├── net/           # WiFi, HTTPS client, SSE streaming
│   ├── tools/         # Tool registry + implementations
│   ├── ui/            # Screen manager, chat, status, settings
│   └── voice/         # Voice pipeline (capture, VAD, STT, TTS, playback)
├── data/              # SPIFFS data (SOUL.md, USER.md, MEMORY.md)
└── partitions.csv     # 3MB app + 256KB SPIFFS + NVS + coredump
```

## Tools

| Tool | Description |
|------|-------------|
| `get_system_info` | Battery, heap, PSRAM, uptime |
| `read_memory` / `write_memory` | Read/write SPIFFS markdown files |
| `gpio_read` / `gpio_write` | GPIO pin control (whitelisted pins) |
| `cron_add` / `cron_remove` / `cron_list` | Schedule recurring tasks |

## Building

### Prerequisites

- [ESP-IDF v5.5.x](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/)
- M5Stack Core2 connected via USB-C

### Configure

Set API keys and WiFi credentials in NVS using the provisioning script:

```bash
python provision.py --port COM4 \
  --wifi-ssid "YourSSID" --wifi-pass "YourPassword" \
  --claude-key "sk-ant-..." \
  --telegram-token "123456:ABC..."
```

Or set individual values:
```bash
python provision.py --port COM4 --set claude_key "sk-ant-..."
python provision.py --port COM4 --set openai_key "sk-..."
```

### Build and Flash

```bash
idf.py build
idf.py -p COM4 flash monitor
```

Or using the included batch script (Windows):
```bash
build.bat build
build.bat flash
```

## Configuration (NVS Keys)

| Key | Required | Description |
|-----|----------|-------------|
| `wifi_ssid` | Yes | WiFi network name |
| `wifi_pass` | Yes | WiFi password |
| `claude_key` | * | Anthropic API key |
| `openai_key` | * | OpenAI API key (also enables voice) |
| `tg_token` | No | Telegram bot token |
| `claude_model` | No | Claude model (default: claude-sonnet-4-20250514) |
| `openai_model` | No | OpenAI model (default: gpt-4o-mini) |
| `active_llm` | No | Active provider: "claude" or "openai" |
| `tts_voice` | No | TTS voice (default: alloy) |
| `vad_threshold` | No | VAD energy threshold (default: 500.0) |

\* At least one LLM API key is required.

## FreeRTOS Tasks

| Task | Core | Priority | Stack | Role |
|------|------|----------|-------|------|
| ui_task | 1 | 5 | 4KB | UI rendering and touch |
| agent_task | 1 | 4 | 16KB | ReAct agent loop |
| telegram_task | 0 | 3 | 8KB | Telegram long-polling |
| voice_capture | 0 | 6 | 4KB | Microphone sampling |
| voice_playback | 0 | 6 | 4KB | Speaker output |
| cron_task | 0 | 2 | 4KB | Cron minute-check |

## License

MIT
