#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <HTTPClient.h>
#include "MAX30105.h"
#include "spo2_algorithm.h"
#include "HardwareSerial.h"
#include "DFRobotDFPlayerMini.h"

// -------- WiFi --------
const char* ssid     = "ESP32_Test";
const char* password = "12345678";

// -------- Telegram --------
const char* BOT_TOKEN = "8691141460:AAEU-y1h6Yq4abhsh9nOiq4AbehYTXQOIXo";
const char* CHAT_ID   = "5703047512";

// -------- Buzzer --------
#define BUZZER_PIN 23

// -------- DFPlayer --------
#define DFPLAYER_RX 16   // ESP32 RX  <- DFPlayer TX
#define DFPLAYER_TX 17   // ESP32 TX  -> DFPlayer RX

HardwareSerial dfSerial(1);
DFRobotDFPlayerMini dfPlayer;
bool dfPlayerReady = false;

WebServer server(80);
MAX30105 particleSensor;

#define BUFFER_SIZE 100

uint32_t irBuffer[BUFFER_SIZE];
uint32_t redBuffer[BUFFER_SIZE];

int32_t spo2;       int8_t validSPO2;
int32_t heartRate;  int8_t validHeartRate;

int  Panic = 0;
int  sbp   = 0;
int  dbp   = 0;
long irValue;

float ppg_amplitude     = 0;
float ppg_avg           = 0;
float pulse_width_ratio = 0;
int   peakCount         = 0;

bool alertSent              = false;
int  panicConsecutiveCount  = 0;
const int PANIC_TRIGGER_THRESHOLD = 2;

// ==========================================
// DFPLAYER
// ==========================================
void initDFPlayer()
{
  dfSerial.begin(9600, SERIAL_8N1, DFPLAYER_RX, DFPLAYER_TX);
  delay(1000);

  if (dfPlayer.begin(dfSerial))
  {
    Serial.println("DFPlayer Mini online.");
    dfPlayer.volume(30);     // 0 to 30
    dfPlayerReady = true;
  }
  else
  {
    Serial.println("DFPlayer init failed!");
    Serial.println("Check wiring and SD card.");
    dfPlayerReady = false;
  }
}

void playDFPlayerTrack(uint8_t trackNumber, uint16_t waitMs = 3000)
{
  if (dfPlayerReady)
  {
    dfPlayer.play(trackNumber);
    delay(waitMs);
  }
}

// ==========================================
// BUZZER / AUDIO
// ==========================================
void playPanicAlarm()
{
  if (dfPlayerReady)
  {
    // Play 001.mp3 from SD card
    playDFPlayerTrack(1, 5000);
  }
  else
  {
    // Fallback buzzer alarm
    for (int i = 0; i < 3; i++)
    {
      tone(BUZZER_PIN, 1000, 300);
      delay(400);
      tone(BUZZER_PIN, 1500, 300);
      delay(400);
    }
    noTone(BUZZER_PIN);
  }
}

void playRecoveryBeep()
{
  if (dfPlayerReady)
  {
    // Play 002.mp3 from SD card
    playDFPlayerTrack(2, 2000);
  }
  else
  {
    tone(BUZZER_PIN, 600, 500);
    delay(600);
    noTone(BUZZER_PIN);
  }
}

// ==========================================
// TELEGRAM ALERT
// ==========================================
void sendTelegramAlert(String message)
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi not connected - skipping alert");
    return;
  }

  HTTPClient http;

  String url = "https://api.telegram.org/bot";
  url += BOT_TOKEN;
  url += "/sendMessage?chat_id=";
  url += CHAT_ID;
  url += "&text=";
  url += message;
  url += "&parse_mode=Markdown";

  http.begin(url);
  int httpCode = http.GET();

  if (httpCode == 200)
    Serial.println("Telegram alert sent!");
  else
    Serial.println("Telegram failed, code: " + String(httpCode));

  http.end();
}

// ==========================================
// PPG FEATURE EXTRACTION
// ==========================================
void extractPPGFeatures()
{
  long sum    = 0;
  long maxVal = 0;
  long minVal = 999999;

  for (int i = 0; i < BUFFER_SIZE; i++)
  {
    sum += irBuffer[i];
    if (irBuffer[i] > maxVal) maxVal = irBuffer[i];
    if (irBuffer[i] < minVal) minVal = irBuffer[i];
  }

  ppg_avg       = sum / BUFFER_SIZE;
  ppg_amplitude = maxVal - minVal;

  peakCount = 0;
  for (int i = 1; i < BUFFER_SIZE - 1; i++)
  {
    if (irBuffer[i] > irBuffer[i - 1] &&
        irBuffer[i] > irBuffer[i + 1] &&
        irBuffer[i] > ppg_avg)
      peakCount++;
  }

  int widthCount = 0;
  for (int i = 0; i < BUFFER_SIZE; i++)
    if (irBuffer[i] > ppg_avg) widthCount++;

  pulse_width_ratio = (float)widthCount / BUFFER_SIZE;
}

// ==========================================
// PANIC DETECTION + ALARM + TELEGRAM
// ==========================================
void detectPanic()
{
  if (!validHeartRate || heartRate <= 0 || heartRate > 250)
  {
    Serial.println("Skipping - invalid HR: " + String(heartRate));
    panicConsecutiveCount = 0;
    return;
  }

  if (!validSPO2 || spo2 <= 0 || spo2 > 100)
  {
    Serial.println("Skipping - invalid SpO2: " + String(spo2));
    panicConsecutiveCount = 0;
    return;
  }

  int score = 0;

  if (heartRate > 110)
    score++;

  if (sbp > 140 || dbp > 90)
    score++;

  if (pulse_width_ratio < 0.35 || pulse_width_ratio > 0.7)
    score++;

  if (ppg_amplitude < 3000 || ppg_amplitude > 20000)
    score++;

  if (score >= 2)
  {
    panicConsecutiveCount++;
    Panic = 1;

    Serial.print("Panic count: ");
    Serial.print(panicConsecutiveCount);
    Serial.print("/");
    Serial.println(PANIC_TRIGGER_THRESHOLD);

    if (panicConsecutiveCount >= PANIC_TRIGGER_THRESHOLD && !alertSent)
    {
      // 1. Play audio through DFPlayer / buzzer fallback
      playPanicAlarm();

      // 2. Send Telegram alert
      String msg = "*PANIC ALERT DETECTED*%0A";
      msg += "------------------------------%0A";
      msg += "Heart Rate: *" + String(heartRate) + " BPM*%0A";
      msg += "SpO2: *"       + String(spo2) + " %%*%0A";
      msg += "Amplitude: "   + String(ppg_amplitude) + "%0A";
      msg += "Pulse Width: " + String(pulse_width_ratio) + "%0A";
      msg += "Score: "       + String(score) + "/4%0A";
      msg += "Confirmed: "   + String(panicConsecutiveCount) + " consecutive readings%0A";
      msg += "------------------------------%0A";
      msg += "Please check on the patient immediately!";

      sendTelegramAlert(msg);
      alertSent = true;
    }
  }
  else
  {
    if (alertSent)
    {
      playRecoveryBeep();

      String recoveryMsg = "*Vitals Back to Normal*%0A";
      recoveryMsg += "HR: "   + String(heartRate) + " BPM%0A";
      recoveryMsg += "SpO2: " + String(spo2) + " %%";
      sendTelegramAlert(recoveryMsg);
    }

    panicConsecutiveCount = 0;
    Panic                 = 0;
    alertSent             = false;
  }
}

// ==========================================
// SEND DATA TO DASHBOARD
// ==========================================
void sendData()
{
  if (irValue < 10000 || spo2 <= 0 || spo2 > 100 || heartRate <= 0)
  {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", "{\"status\":\"invalid\"}");
    return;
  }

  String json = "{";
  json += "\"ir\":"          + String(irValue) + ",";
  json += "\"bpm\":"         + String(heartRate) + ",";
  json += "\"spo2\":"        + String(spo2) + ",";
  json += "\"amplitude\":"   + String(ppg_amplitude) + ",";
  json += "\"avg_ir\":"      + String(ppg_avg) + ",";
  json += "\"pulse_width\":" + String(pulse_width_ratio) + ",";
  json += "\"panic\":"       + String(Panic);
  json += "}";

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

// ==========================================
// SETUP
// ==========================================
void setup()
{
  Serial.begin(115200);
  Wire.begin(21, 22);

  // Buzzer init
  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN);

  // WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(1000);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected! IP: ");
  Serial.println(WiFi.localIP());

  // Web server
  server.on("/data", sendData);
  server.begin();

  // DFPlayer init
  initDFPlayer();

  // Sensor
  if (!particleSensor.begin(Wire))
  {
    Serial.println("MAX30102 not found - check wiring!");
    while (1);
  }

  particleSensor.setup();
  particleSensor.setPulseAmplitudeRed(0x1F);
  particleSensor.setPulseAmplitudeIR(0x1F);

  Serial.println("Sensor initialized.");

  // Boot sound
  if (dfPlayerReady)
  {
    playDFPlayerTrack(3, 2000);   // optional: 003.mp3 as startup sound
  }
  else
  {
    tone(BUZZER_PIN, 800, 200);
    delay(300);
    noTone(BUZZER_PIN);
  }

  // Boot Telegram
  String bootMsg = "*CaringHealth Monitor Online*%0A";
  bootMsg += "IP: `" + WiFi.localIP().toString() + "`%0A";
  bootMsg += "Sensor: MAX30102%0A";
  bootMsg += "Threshold: " + String(PANIC_TRIGGER_THRESHOLD) + " consecutive valid readings";
  sendTelegramAlert(bootMsg);
}

// ==========================================
// LOOP
// ==========================================
void loop()
{
  server.handleClient();

  irValue = particleSensor.getIR();

  if (irValue < 10000)
  {
    Serial.println("Finger not detected");
    panicConsecutiveCount = 0;
    Panic                 = 0;
    alertSent             = false;
    delay(500);
    return;
  }

  for (byte i = 0; i < BUFFER_SIZE; i++)
  {
    while (!particleSensor.available())
      particleSensor.check();

    redBuffer[i] = particleSensor.getRed();
    irBuffer[i]  = particleSensor.getIR();
    particleSensor.nextSample();
  }

  maxim_heart_rate_and_oxygen_saturation(
    irBuffer, BUFFER_SIZE, redBuffer,
    &spo2, &validSPO2, &heartRate, &validHeartRate
  );

  extractPPGFeatures();
  detectPanic();

  Serial.println("------ DATA ------");
  Serial.print("HR: ");
  Serial.println(validHeartRate && heartRate > 0 && heartRate <= 250
                 ? String(heartRate) + " BPM" : "Invalid");
  Serial.print("SpO2: ");
  Serial.println(validSPO2 && spo2 > 0 && spo2 <= 100
                 ? String(spo2) + " %" : "Invalid");
  Serial.print("Amplitude: ");   Serial.println(ppg_amplitude);
  Serial.print("Avg IR: ");      Serial.println(ppg_avg);
  Serial.print("Pulse Width: "); Serial.println(pulse_width_ratio);
  Serial.print("Panic: ");       Serial.println(Panic);
  Serial.print("Count: ");       Serial.print(panicConsecutiveCount);
  Serial.print("/");             Serial.println(PANIC_TRIGGER_THRESHOLD);
  Serial.print("Alert Sent: ");  Serial.println(alertSent ? "YES" : "NO");
  Serial.print("DFPlayer: ");    Serial.println(dfPlayerReady ? "READY" : "NOT READY");
  Serial.println("------------------");

  delay(1000);
}