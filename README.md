[日本語版 (README.ja.md)](./README.ja.md)

# ATOMS3R OpenAI Voice PTT HTTP

This project uses AtomS3R + Atomic Voice Base + Dual Button Unit
to perform voice interaction with OpenAI API via Raspberry Pi 5.

Current verified flow:

- Record on AtomS3R
- HTTP POST to Raspberry Pi 5
- OpenAI speech recognition
- OpenAI response generation
- OpenAI TTS generation
- Playback on AtomS3R

![Photo 1](./images/img_8898.jpg)

---

# Current Setup

## Hardware

- M5Stack AtomS3R
- M5Stack Atomic Echo Base
- M5Stack Dual Button Unit
- Raspberry Pi 5
- Mobile battery

---

# System Architecture

```text
AtomS3R
  -> Wi-Fi / HTTP POST
Raspberry Pi 5 (FastAPI)
  ->
OpenAI API
  |- Speech-to-Text
  |- LLM
  \- Text-to-Speech
  ->
Raspberry Pi 5
  -> WAV response
AtomS3R
```

---

# Current Behavior

## AtomS3R Side

### Button Actions

| Action | Description |
|---|---|
| Button A | Record -> Send -> Play response |
| Button B | Basic status check |

---

## Voice Flow

```text
Record
-> Build WAV
-> Send to Pi5
-> OpenAI Speech-to-Text
-> OpenAI LLM
-> OpenAI TTS
-> Normalize WAV on Pi5
-> Play on AtomS3R
```

![Photos 2-5 Collage](./images/img_8899_8902_collage.jpg)

---

# Recording Format

Current stable settings:

```cpp
#define SAMPLE_RATE_HZ       48000
#define BITS_PER_SAMPLE      32
#define CHANNEL_COUNT        1
```

With other settings, we observed:

- Wrong recording speed
- Wrong pitch
- Silent output

So the format must match Atomic Echo Base behavior.

---

# Pi5-side TTS Normalization

Pi5 uses `ffmpeg` to convert OpenAI TTS WAV for AtomS3R playback.

Example:

```python
cmd = [
    "ffmpeg",
    "-y",
    "-i", str(raw_tts_path),
    "-ar", "48000",
    "-ac", "1",
    "-c:a", "pcm_s32le",
    "-filter:a", "volume=4.0",
    str(normalized_path),
]
```

---

# Current Volume Settings

## Pi5 side

```python
"-filter:a", "volume=4.0"
```

## AtomS3R side

```cpp
#define SPEAKER_VOLUME_PERCENT 65
```

---

# Required Libraries

Arduino IDE:

- M5Unified
- M5GFX
- M5Atomic-EchoBase

Python:

```text
fastapi
uvicorn[standard]
python-dotenv
openai
python-multipart
```

---

# Git Operation

Current stable point:

```text
v0.1.0
```

Working branch:

```text
work-from-v0.1.0
```

---

# Verified Items

- PSRAM 8MB available
- HTTP multipart upload success
- OpenAI Speech-to-Text success
- OpenAI TTS success
- WAV return success
- Playback success on AtomS3R

---

# Next Steps

- Better status display
- Press-and-hold recording
- Volume tuning
- Long-press operations
- Conversation continuation
- Streaming
- Conversation history
- Move to OpenAI Realtime API

---

# Notes

ESP32 audio behavior depends on many coupled factors:

- I2S
- WAV
- Sample rate
- Bit depth
- PSRAM
- HTTP
- OpenAI API

Use small, tagged increments to avoid breaking the system.

---

# Related Repositories

- Pi5 server: [omiya-bonsai/openai-voice-terminal](https://github.com/omiya-bonsai/openai-voice-terminal)
- Integration docs: [omiya-bonsai/openai-voice-terminal-system](https://github.com/omiya-bonsai/openai-voice-terminal-system)

---

# License

This project is licensed under the MIT License. See [LICENSE](./LICENSE).

---

# AI Assistance

Parts of this repository, its documentation, and related assets were developed with assistance from AI tools including Codex app and GitHub Copilot.
