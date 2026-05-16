#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <M5Unified.h>
#include <M5EchoBase.h>
#include "esp_heap_caps.h"

#include "config.h"

// ==================================================
// Globals
// ==================================================

// M5EchoBase echobase(I2S_NUM_0);
M5EchoBase echobase;

static uint8_t* pcmBuffer = nullptr;
static uint8_t* wavBuffer = nullptr;
static uint8_t* replyBuffer = nullptr;

static bool lastButtonA = false;
static bool lastButtonB = false;

// ==================================================
// Utility
// ==================================================

static bool readButtonRaw(int pin) {
  int v = digitalRead(pin);
#if BUTTON_ACTIVE_LOW
  return v == LOW;
#else
  return v == HIGH;
#endif
}

static void drawStatus(const char* line1, const char* line2 = nullptr) {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setCursor(0, 0);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextSize(1);
  M5.Display.println(line1);
  if (line2) {
    M5.Display.println(line2);
  }

  Serial.println(line1);
  if (line2) Serial.println(line2);
}

static void connectWiFi() {
  drawStatus("WiFi connecting");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t startMs = millis();

  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");

    if (millis() - startMs > 30000) {
      drawStatus("WiFi failed", "Restarting...");
      delay(2000);
      ESP.restart();
    }
  }

  Serial.println();
  Serial.print("WiFi connected. IP: ");
  Serial.println(WiFi.localIP());

  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setCursor(0, 0);
  M5.Display.println("WiFi OK");
  M5.Display.println(WiFi.localIP().toString());
}

// ==================================================
// WAV Header
// ==================================================

static void writeLE16(uint8_t* p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xff);
  p[1] = (uint8_t)((v >> 8) & 0xff);
}

static void writeLE32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xff);
  p[1] = (uint8_t)((v >> 8) & 0xff);
  p[2] = (uint8_t)((v >> 16) & 0xff);
  p[3] = (uint8_t)((v >> 24) & 0xff);
}

static void makeWavHeader(uint8_t* header, uint32_t pcmBytes) {
  const uint32_t sampleRate = SAMPLE_RATE_HZ;
  const uint16_t bitsPerSample = BITS_PER_SAMPLE;
  const uint16_t channels = CHANNEL_COUNT;
  const uint32_t byteRate = sampleRate * channels * bitsPerSample / 8;
  const uint16_t blockAlign = channels * bitsPerSample / 8;
  const uint32_t riffSize = 36 + pcmBytes;

  memcpy(header + 0, "RIFF", 4);
  writeLE32(header + 4, riffSize);
  memcpy(header + 8, "WAVE", 4);

  memcpy(header + 12, "fmt ", 4);
  writeLE32(header + 16, 16);
  writeLE16(header + 20, 1);  // PCM
  writeLE16(header + 22, channels);
  writeLE32(header + 24, sampleRate);
  writeLE32(header + 28, byteRate);
  writeLE16(header + 32, blockAlign);
  writeLE16(header + 34, bitsPerSample);

  memcpy(header + 36, "data", 4);
  writeLE32(header + 40, pcmBytes);
}

static void buildWavFromPcm() {
  makeWavHeader(wavBuffer, PCM_RECORD_BYTES);
  memcpy(wavBuffer + WAV_HEADER_BYTES, pcmBuffer, PCM_RECORD_BYTES);
}

// ==================================================
// Multipart POST
// ==================================================

static bool postWavToServer(const uint8_t* wavData, size_t wavSize, size_t* replySizeOut) {
  if (WiFi.status() != WL_CONNECTED) {
    drawStatus("WiFi lost", "Reconnect...");
    connectWiFi();
  }

  const String boundary = "----atoms3r-openai-voice-boundary";

  String head;
  head += "--" + boundary + "\r\n";
  head += "Content-Disposition: form-data; name=\"file\"; filename=\"record.wav\"\r\n";
  head += "Content-Type: audio/wav\r\n\r\n";

  String tail;
  tail += "\r\n--" + boundary + "--\r\n";

  const size_t bodySize = head.length() + wavSize + tail.length();

  uint8_t* body = (uint8_t*)heap_caps_malloc(
    bodySize,
    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
  );

  if (!body) {
    drawStatus("POST body alloc", "FAILED");
    return false;
  }

  size_t offset = 0;
  memcpy(body + offset, head.c_str(), head.length());
  offset += head.length();

  memcpy(body + offset, wavData, wavSize);
  offset += wavSize;

  memcpy(body + offset, tail.c_str(), tail.length());
  offset += tail.length();

  HTTPClient http;
  http.setTimeout(60000);

  drawStatus("POST /voice", "sending...");

  bool ok = false;
  *replySizeOut = 0;

  if (http.begin(VOICE_SERVER_URL)) {
    String contentType = "multipart/form-data; boundary=" + boundary;
    http.addHeader("Content-Type", contentType);

    int code = http.POST(body, bodySize);

    Serial.printf("HTTP status: %d\n", code);

    if (code == 200) {
      WiFiClient* stream = http.getStreamPtr();
      int contentLength = http.getSize();

      Serial.printf("Content-Length: %d\n", contentLength);

      if (contentLength <= 0) {
        drawStatus("No reply length");
      } else if (contentLength > MAX_REPLY_WAV_BYTES) {
        drawStatus("Reply too large");
      } else {
        size_t total = 0;
        uint32_t startMs = millis();

        while (http.connected() && total < (size_t)contentLength) {
          size_t available = stream->available();

          if (available) {
            size_t toRead = available;
            if (total + toRead > (size_t)contentLength) {
              toRead = (size_t)contentLength - total;
            }

            int n = stream->readBytes(replyBuffer + total, toRead);
            if (n > 0) {
              total += n;
            }
          } else {
            delay(5);
          }

          if (millis() - startMs > 60000) {
            drawStatus("Reply timeout");
            break;
          }
        }

        *replySizeOut = total;

        Serial.printf("Reply received: %u bytes\n", (unsigned)total);

        if (total > 128) {
          ok = true;
        }
      }
    } else {
      String err = http.getString();
      Serial.println("Server error body:");
      Serial.println(err);
      drawStatus("HTTP error", String(code).c_str());
    }

    http.end();
  } else {
    drawStatus("HTTP begin failed");
  }

  heap_caps_free(body);
  return ok;
}

// ==================================================
// Recording
// ==================================================

static bool recordOnce() {
  drawStatus("Recording", "speak now");

  Serial.printf("Recording %d sec, %u bytes\n", RECORD_SECONDS, PCM_RECORD_BYTES);

  memset(pcmBuffer, 0, PCM_RECORD_BYTES);

  // 録音時はスピーカー側をmute
  echobase.setMute(true);
  delay(20);

  // M5Atomic-EchoBase の公式APIは指定バイト数分をブロッキング録音する
  echobase.record(pcmBuffer, PCM_RECORD_BYTES);

  delay(50);

  drawStatus("Recording done");
  return true;
}

static void tryPlayReply(size_t replySize) {
#if TRY_PLAY_REPLY_ON_DEVICE
  if (replySize <= WAV_HEADER_BYTES) {
    drawStatus("Reply too small");
    return;
  }

  drawStatus("Playing reply");

  echobase.setMute(false);
  delay(20);

  // 注意:
  // ここでは WAV 44バイトヘッダを単純に飛ばして raw PCM として再生を試みます。
  // OpenAI TTSのサンプルレートが16kHzでない場合、速度や音程がズレます。
  echobase.play(replyBuffer + WAV_HEADER_BYTES, replySize - WAV_HEADER_BYTES);

  delay(50);
  echobase.setMute(true);
#else
  (void)replySize;
  drawStatus("Reply received", "playback skipped");
#endif
}

// ==================================================
// Setup
// ==================================================

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);

  Serial.begin(115200);
  delay(1000);

  M5.Display.setRotation(0);
  M5.Display.setTextSize(1);
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setCursor(0, 0);

  Serial.println();
  Serial.println("=== ATOMS3R OpenAI Voice PTT HTTP ===");

  if (!psramFound()) {
    drawStatus("PSRAM NOT FOUND");
    while (true) delay(1000);
  }

  Serial.printf("PSRAM size: %u\n", ESP.getPsramSize());
  Serial.printf("Free PSRAM: %u\n", ESP.getFreePsram());
  Serial.printf("Free heap: %u\n", ESP.getFreeHeap());

  pcmBuffer = (uint8_t*)heap_caps_malloc(
    PCM_RECORD_BYTES,
    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
  );

  wavBuffer = (uint8_t*)heap_caps_malloc(
    WAV_TOTAL_BYTES,
    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
  );

  replyBuffer = (uint8_t*)heap_caps_malloc(
    MAX_REPLY_WAV_BYTES,
    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
  );

  if (!pcmBuffer || !wavBuffer || !replyBuffer) {
    drawStatus("Buffer alloc failed");
    Serial.printf("pcmBuffer: %p\n", pcmBuffer);
    Serial.printf("wavBuffer: %p\n", wavBuffer);
    Serial.printf("replyBuffer: %p\n", replyBuffer);
    while (true) delay(1000);
  }

  pinMode(BUTTON_A_PIN, INPUT_PULLUP);
  pinMode(BUTTON_B_PIN, INPUT_PULLUP);

  drawStatus("Init EchoBase");

  Wire.begin(ECHO_I2C_SDA, ECHO_I2C_SCL);

  echobase.init(
    SAMPLE_RATE_HZ,
    ECHO_I2C_SDA,
    ECHO_I2C_SCL,
    ECHO_I2S_DIN,
    ECHO_I2S_WS,
    ECHO_I2S_DOUT,
    ECHO_I2S_BCK,
    Wire
  );

  echobase.setSpeakerVolume(SPEAKER_VOLUME_PERCENT);
  echobase.setMicGain(ES8311_MIC_GAIN_6DB);
  echobase.setMute(true);

  connectWiFi();

  drawStatus("Ready", "A: record/send");
}

// ==================================================
// Loop
// ==================================================

void loop() {
  M5.update();

  bool buttonA = readButtonRaw(BUTTON_A_PIN);
  bool buttonB = readButtonRaw(BUTTON_B_PIN);

  // A: record and send
  if (buttonA && !lastButtonA) {
    Serial.println("Button A pressed");

    if (recordOnce()) {
      drawStatus("Build WAV");
      buildWavFromPcm();

      size_t replySize = 0;
      bool ok = postWavToServer(wavBuffer, WAV_TOTAL_BYTES, &replySize);

      if (ok) {
        Serial.printf("Reply OK: %u bytes\n", (unsigned)replySize);
        tryPlayReply(replySize);
      } else {
        drawStatus("POST failed");
      }

      delay(1000);
      drawStatus("Ready", "A: record/send");
    }
  }

  // B: status / cancel placeholder
  if (buttonB && !lastButtonB) {
    Serial.println("Button B pressed");
    drawStatus("Status",
               WiFi.isConnected() ? "WiFi OK" : "WiFi NG");
    delay(800);
    drawStatus("Ready", "A: record/send");
  }

  lastButtonA = buttonA;
  lastButtonB = buttonB;

  delay(20);
}