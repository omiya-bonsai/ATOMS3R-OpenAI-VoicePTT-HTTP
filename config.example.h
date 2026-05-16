#pragma once

// ==================================================
// Wi-Fi
// ==================================================
#define WIFI_SSID      "YOUR_WIFI_SSID"
#define WIFI_PASSWORD  "YOUR_WIFI_PASSWORD"

// Pi5 FastAPI server
// 例: "http://server-address:8001/voice"
#define VOICE_SERVER_URL "http://YOUR_PI5_IP:8001/voice"

// ==================================================
// Recording
// ==================================================
#define SAMPLE_RATE_HZ       16000
#define BITS_PER_SAMPLE      16
#define CHANNEL_COUNT        1

// 初期版は短めに固定。成功後に 15 / 30 秒へ伸ばす。
#define RECORD_SECONDS       8

#define PCM_BYTES_PER_SEC    (SAMPLE_RATE_HZ * CHANNEL_COUNT * (BITS_PER_SAMPLE / 8))
#define PCM_RECORD_BYTES     (PCM_BYTES_PER_SEC * RECORD_SECONDS)
#define WAV_HEADER_BYTES     44
#define WAV_TOTAL_BYTES      (WAV_HEADER_BYTES + PCM_RECORD_BYTES)

// ==================================================
// Atomic Voice Base pins for AtomS3R / AtomS3 pin map
// Official example:
// echobase.init(16000, 38, 39, 7, 6, 5, 8, Wire)
// ==================================================
#define ECHO_I2C_SDA  38
#define ECHO_I2C_SCL  39
#define ECHO_I2S_DIN   7
#define ECHO_I2S_WS    6
#define ECHO_I2S_DOUT  5
#define ECHO_I2S_BCK   8

#define SPEAKER_VOLUME_PERCENT 50

// ==================================================
// Dual Button Unit
// ==================================================
// AtomS3R の Grove PORT は多くの例で G1/G2 系として扱われます。
// 実機で A/B が逆なら、この2つを入れ替えてください。
#define BUTTON_A_PIN  1
#define BUTTON_B_PIN  2

// Dual Button Unit は押下時 LOW の構成が多いです。
// 逆に動く場合は false にしてください。
#define BUTTON_ACTIVE_LOW true

// ==================================================
// Reply receive buffer
// ==================================================
#define MAX_REPLY_WAV_BYTES (1024 * 1024)  // 1MB

// 初期版では false 推奨。
// true にすると受信WAVの44バイトヘッダを除いて raw PCM として再生を試みます。
// ただしOpenAI TTSのWAVサンプルレート次第で速度がズレる可能性があります。
#define TRY_PLAY_REPLY_ON_DEVICE false