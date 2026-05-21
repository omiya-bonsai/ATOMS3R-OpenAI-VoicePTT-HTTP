[日本語版 (README.ja.md)](./README.ja.md)

# ATOMS3R OpenAI Voice PTT HTTP

AtomS3R client firmware for voice interaction through a Raspberry Pi 5 HTTP bridge.

## Features

- Push-to-talk recording with button `A` (record while pressed)
- Cancel/interrupt with button `B`
- WAV upload to Pi5 `/voice` endpoint
- Playback of returned WAV response on AtomS3R

## Hardware

- M5Stack AtomS3R
- M5Stack Atomic Echo Base
- M5Stack Dual Button Unit

## Audio Format (current working profile)

- `48000 Hz`
- `32-bit PCM`
- `mono`

## Flow

1. Hold `A` to record.
2. Release `A` to stop recording.
3. Device builds WAV and sends it to Pi5.
4. Pi5 processes OpenAI STT/LLM/TTS.
5. Device receives reply WAV and plays it.

## Related Repository

- Pi5 server: [omiya-bonsai/openai-voice-terminal](https://github.com/omiya-bonsai/openai-voice-terminal)
