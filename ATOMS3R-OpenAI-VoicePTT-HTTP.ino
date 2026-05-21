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
static volatile bool cancelRequested = false;

enum class AppState {
  IDLE,
  RECORDING,
  SENDING,
  WAITING_REPLY,
  PLAYING
};

enum class OpResult {
  OK,
  CANCELLED,
  ERROR
};

static AppState appState = AppState::IDLE;
static constexpr size_t RECORD_CHUNK_MS = 200;
static constexpr size_t PLAYBACK_CHUNK_MS = 120;
static constexpr size_t MIN_VALID_RECORD_BYTES = 3200;

// ==================================================
// Utility
// ==================================================

// static void drawSpinnerScreen(const char* label, int frame) {
//   const char spinner[] = "|/-\\";
//   char s[2] = { spinner[frame % 4], '\0' };

//   M5.Display.fillScreen(TFT_BLACK);
//   M5.Display.setTextDatum(MC_DATUM);

//   M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
//   M5.Display.setTextSize(2);
//   M5.Display.drawString(label, M5.Display.width() / 2, M5.Display.height() / 2 - 20);

//   M5.Display.setTextSize(4);
//   M5.Display.drawString(s, M5.Display.width() / 2, M5.Display.height() / 2 + 18);

//   M5.Display.setTextDatum(TL_DATUM);
// }


static void drawMessageScreen(
  const char* label,
  const char* subText,
  uint16_t bgColor = TFT_BLACK,
  uint16_t textColor = TFT_YELLOW) {
  M5.Display.fillScreen(bgColor);
  M5.Display.setTextDatum(MC_DATUM);

  M5.Display.setTextColor(textColor, bgColor);

  M5.Display.setTextSize(2);
  M5.Display.drawString(
    label,
    M5.Display.width() / 2,
    M5.Display.height() / 2 - 12);

  M5.Display.setTextSize(1);
  M5.Display.drawString(
    subText,
    M5.Display.width() / 2,
    M5.Display.height() / 2 + 22);

  M5.Display.setTextDatum(TL_DATUM);
}

static void drawSpinnerScreen(
  const char* label,
  int frame,
  const char* subText = nullptr) {
  const char spinner[] = "|/-\\";
  char s[2] = { spinner[frame % 4], '\0' };

  M5.Display.fillScreen(TFT_BLACK);

  M5.Display.setTextDatum(MC_DATUM);

  // Main label
  M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
  M5.Display.setTextSize(2);

  M5.Display.drawString(
    label,
    M5.Display.width() / 2,
    M5.Display.height() / 2 - 24);

  // Spinner
  M5.Display.setTextSize(4);

  M5.Display.drawString(
    s,
    M5.Display.width() / 2,
    M5.Display.height() / 2 + 4);

  // Optional sub text
  if (subText) {
    M5.Display.setTextSize(1);

    M5.Display.drawString(
      subText,
      M5.Display.width() / 2,
      M5.Display.height() / 2 + 36);
  }

  M5.Display.setTextDatum(TL_DATUM);
}



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



static void drawButtonPrompt() {
  int w = M5.Display.width();
  int h = M5.Display.height();

  M5.Display.fillScreen(TFT_BLACK);

  // Top: A button / Blue
  M5.Display.fillRect(0, 0, w, h / 2, TFT_BLUE);

  // Bottom: B button / Red
  M5.Display.fillRect(0, h / 2, w, h - (h / 2), TFT_RED);

  M5.Display.setTextDatum(MC_DATUM);

  M5.Display.setTextColor(TFT_WHITE, TFT_BLUE);
  M5.Display.setTextSize(4);
  M5.Display.drawString("A", w / 2 - 22, h / 4);

  M5.Display.setTextSize(1);
  M5.Display.drawString("REC", w / 2 + 20, h / 4);

  M5.Display.setTextColor(TFT_WHITE, TFT_RED);
  M5.Display.setTextSize(4);
  M5.Display.drawString("B", w / 2 - 22, h * 3 / 4);

  M5.Display.setTextSize(1);
  M5.Display.drawString("CANSEL", w / 2 + 20, h * 3 / 4);

  M5.Display.setTextDatum(TL_DATUM);

  Serial.println("Ready: A=REC / B=CANSEL");
}

static void drawRecordingScreen() {
  M5.Display.fillScreen(TFT_BLUE);

  M5.Display.setTextDatum(MC_DATUM);

  M5.Display.setTextColor(TFT_WHITE, TFT_BLUE);

  M5.Display.setTextSize(5);
  M5.Display.drawString("REC", M5.Display.width() / 2,
                        M5.Display.height() / 2 - 10);

  M5.Display.setTextSize(1);
  M5.Display.drawString("Hold A to record",
                        M5.Display.width() / 2,
                        M5.Display.height() / 2 + 30);

  M5.Display.setTextDatum(TL_DATUM);

  Serial.println("Recording...");
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

static void pollCancelButton() {
  if (readButtonRaw(BUTTON_B_PIN)) {
    cancelRequested = true;
  }
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

static void buildWavFromPcm(size_t pcmBytes) {
  makeWavHeader(wavBuffer, pcmBytes);
  memcpy(wavBuffer + WAV_HEADER_BYTES, pcmBuffer, pcmBytes);
}

// ==================================================
// Multipart POST
// ==================================================

static OpResult postWavToServer(const uint8_t* wavData, size_t wavSize, size_t* replySizeOut) {
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
    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

  if (!body) {
    drawStatus("POST body alloc", "FAILED");
    return OpResult::ERROR;
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

  // drawStatus("POST /voice", "sending...");
  // drawSpinnerScreen("SEND", 0);
  // drawSpinnerScreen(
  //   "SENDING",
  //   0,
  //   "Please wait...");


  drawMessageScreen(
    "SENDING",
    "Please wait...",
    TFT_BLACK,
    TFT_YELLOW);
  appState = AppState::SENDING;

  OpResult result = OpResult::ERROR;
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


        int spinnerFrame = 0;
        uint32_t lastSpinnerMs = 0;
        drawSpinnerScreen("WAIT", spinnerFrame);
        appState = AppState::WAITING_REPLY;

        while (http.connected() && total < (size_t)contentLength) {
          pollCancelButton();
          if (cancelRequested) {
            drawStatus("Cancelled");
            result = OpResult::CANCELLED;
            break;
          }

          size_t available = stream->available();


          if (millis() - lastSpinnerMs > 200) {
            lastSpinnerMs = millis();
            spinnerFrame++;
            drawSpinnerScreen("WAIT", spinnerFrame);
          }

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
            result = OpResult::ERROR;
            break;
          }
        }

        *replySizeOut = total;

        Serial.printf("Reply received: %u bytes\n", (unsigned)total);

        if (result != OpResult::CANCELLED) {
          if (total > 128) {
            result = OpResult::OK;
          } else {
            result = OpResult::ERROR;
          }
        }
      }
    } else {
      String err = http.getString();
      Serial.println("Server error body:");
      Serial.println(err);
      drawStatus("HTTP error", String(code).c_str());
      result = OpResult::ERROR;
    }

    http.end();
  } else {
    drawStatus("HTTP begin failed");
    result = OpResult::ERROR;
  }

  heap_caps_free(body);
  return result;
}

// ==================================================
// Recording
// ==================================================

static OpResult recordWithPtt(size_t* recordedBytesOut) {
  // drawStatus("Recording", "speak now");

  *recordedBytesOut = 0;
  if (cancelRequested) {
    return OpResult::CANCELLED;
  }

  appState = AppState::RECORDING;
  drawRecordingScreen();

  Serial.printf("Recording %d sec, %u bytes\n", RECORD_SECONDS, PCM_RECORD_BYTES);

  memset(pcmBuffer, 0, PCM_RECORD_BYTES);

  // 録音時はスピーカー側をmute
  echobase.setMute(true);
  delay(20);

  // 録音を短チャンクに分け、各チャンク間でBキャンセルを検知する。
  const size_t sampleBytes = CHANNEL_COUNT * (BITS_PER_SAMPLE / 8);
  size_t chunkBytes = (PCM_BYTES_PER_SEC * RECORD_CHUNK_MS) / 1000;
  if (chunkBytes < sampleBytes) {
    chunkBytes = sampleBytes;
  }
  chunkBytes = (chunkBytes / sampleBytes) * sampleBytes;

  const uint32_t maxRecordMs = (uint32_t)MAX_RECORD_SECONDS * 1000U;
  uint32_t recordStartMs = millis();
  size_t recorded = 0;
  while (recorded < PCM_RECORD_BYTES) {
    pollCancelButton();
    if (cancelRequested) {
      drawStatus("Recording cancelled");
      memset(pcmBuffer, 0, PCM_RECORD_BYTES);
      return OpResult::CANCELLED;
    }
    if (!readButtonRaw(BUTTON_A_PIN)) {
      break;
    }
    if (millis() - recordStartMs >= maxRecordMs) {
      drawStatus("Max record reached");
      break;
    }

    size_t toRecord = chunkBytes;
    if (toRecord > (PCM_RECORD_BYTES - recorded)) {
      toRecord = PCM_RECORD_BYTES - recorded;
    }
    echobase.record(pcmBuffer + recorded, toRecord);
    recorded += toRecord;
  }

  delay(50);
  *recordedBytesOut = recorded;

  drawStatus("Recording done", String((unsigned)recorded).c_str());
  return OpResult::OK;
}

static OpResult tryPlayReply(size_t replySize) {
#if TRY_PLAY_REPLY_ON_DEVICE
  if (cancelRequested) {
    return OpResult::CANCELLED;
  }

  if (replySize <= WAV_HEADER_BYTES) {
    drawStatus("Reply too small");
    return OpResult::ERROR;
  }

  appState = AppState::PLAYING;
  drawStatus("Playing reply");

  echobase.setMute(false);
  delay(20);

  // 注意:
  // ここでは WAV 44バイトヘッダを単純に飛ばして raw PCM として再生を試みます。
  // OpenAI TTSのサンプルレートが16kHzでない場合、速度や音程がズレます。
  const size_t sampleBytes = CHANNEL_COUNT * (BITS_PER_SAMPLE / 8);
  size_t chunkBytes = (PCM_BYTES_PER_SEC * PLAYBACK_CHUNK_MS) / 1000;
  if (chunkBytes < sampleBytes) {
    chunkBytes = sampleBytes;
  }
  chunkBytes = (chunkBytes / sampleBytes) * sampleBytes;

  size_t played = 0;
  const size_t payloadBytes = replySize - WAV_HEADER_BYTES;
  while (played < payloadBytes) {
    pollCancelButton();
    if (cancelRequested) {
      echobase.setMute(true);
      drawStatus("Playback cancelled");
      return OpResult::CANCELLED;
    }

    size_t toPlay = chunkBytes;
    if (toPlay > (payloadBytes - played)) {
      toPlay = payloadBytes - played;
    }
    echobase.play(replyBuffer + WAV_HEADER_BYTES + played, toPlay);
    played += toPlay;
  }

  delay(50);
  echobase.setMute(true);
#else
  (void)replySize;
  drawStatus("Reply received", "playback skipped");
#endif
  return OpResult::OK;
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
    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

  wavBuffer = (uint8_t*)heap_caps_malloc(
    WAV_TOTAL_BYTES,
    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

  replyBuffer = (uint8_t*)heap_caps_malloc(
    MAX_REPLY_WAV_BYTES,
    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

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
    Wire);

  echobase.setSpeakerVolume(SPEAKER_VOLUME_PERCENT);
  echobase.setMicGain(ES8311_MIC_GAIN_6DB);
  echobase.setMute(true);

  connectWiFi();

  drawButtonPrompt();
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
    cancelRequested = false;
    appState = AppState::IDLE;

    size_t recordedBytes = 0;
    if (recordWithPtt(&recordedBytes) == OpResult::OK) {
      if (recordedBytes < MIN_VALID_RECORD_BYTES) {
        drawStatus("Too short");
        delay(500);
        drawButtonPrompt();
        cancelRequested = false;
        appState = AppState::IDLE;
        lastButtonA = buttonA;
        lastButtonB = buttonB;
        delay(20);
        return;
      }

      drawStatus("Build WAV");
      buildWavFromPcm(recordedBytes);

      size_t replySize = 0;
      OpResult postResult = postWavToServer(
        wavBuffer,
        WAV_HEADER_BYTES + recordedBytes,
        &replySize);

      if (postResult == OpResult::OK) {
        Serial.printf("Reply OK: %u bytes\n", (unsigned)replySize);
        (void)tryPlayReply(replySize);
      } else if (postResult == OpResult::CANCELLED) {
        drawStatus("Cancelled");
      } else {
        drawStatus("POST failed");
      }

      delay(1000);
      drawButtonPrompt();
      cancelRequested = false;
      appState = AppState::IDLE;
    } else {
      delay(500);
      drawButtonPrompt();
      cancelRequested = false;
      appState = AppState::IDLE;
    }
  }

  // B: cancel current operation
  if (buttonB && !lastButtonB) {
    Serial.println("Button B pressed");
    cancelRequested = true;
    if (appState == AppState::IDLE) {
      drawStatus("No active op");
      delay(600);
      drawButtonPrompt();
    } else {
      drawStatus("Cancel requested");
    }
  }

  lastButtonA = buttonA;
  lastButtonB = buttonB;

  delay(20);
}
