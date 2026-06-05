/*
 * ============================================================
 *  DROP TOWER IMU LOGGER — DV SIDE (ESP-NOW)
 *  Adafruit Feather ESP32 V2
 * ============================================================
 *  REQUIRED LIBRARIES:
 *    - Adafruit LSM6DS
 *    - Adafruit LIS3MDL
 *    - Adafruit MCP9808
 *    - Adafruit BusIO
 *    - Adafruit NeoPixel
 *    - LittleFS (built into ESP32 Arduino core)
 *
 *  LED STATUS (onboard NeoPixel, GPIO 0):
 *    Red    — booting / not ready
 *    Green  — ready, waiting for commands
 *    Yellow — logging active
 * ============================================================
 */

// ── Includes ──────────────────────────────────────────────────
#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>
#include <LittleFS.h>
#include <Adafruit_LSM6DSOX.h>
#include <Adafruit_LIS3MDL.h>
#include <Adafruit_MCP9808.h>
#include <Adafruit_NeoPixel.h>

// ── Pin / hardware defines ────────────────────────────────────
#define IMU_SDA_PIN       22
#define IMU_SCL_PIN       20
#define NEOPIXEL_PIN       0
#define NEOPIXEL_COUNT     1
#define NEOPIXEL_BRIGHT   30
#define BATTERY_PIN       35
#define BATTERY_DIVIDER  2.0f
#define ADC_REF_V        3.3f
#define ADC_RESOLUTION   4095.0f
#define CMD_LOWPOWER      5

// ── Sensor addresses ──────────────────────────────────────────
#define LSM6DS3_ADDR  0x6A
#define LIS3MDL_ADDR  0x1C
#define MCP9808_ADDR  0x18

// ── IMU / sampling config ─────────────────────────────────────
#define LSM6DS3_ODR           LSM6DS_RATE_416_HZ
#define SAMPLE_RATE_HZ        416
#define SAMPLE_INTERVAL_US    (1000000 / SAMPLE_RATE_HZ)
#define FREEFALL_THRESHOLD_G  0.15f
#define ESPNOW_SEND_EVERY_N   4

// ── Calibration ───────────────────────────────────────────────
// Hard-coded accel biases (in g) — measured with board flat, facing up.
// Replace these values with your measured offsets.
#define ACCEL_BIAS_X   0.0f   // <-- replace
#define ACCEL_BIAS_Y   0.0f   // <-- replace
#define ACCEL_BIAS_Z   0.058222f   // <-- replace

// Number of samples to average during gyro cal on boot (~3s at 2ms/sample)
#define GYRO_CAL_SAMPLES  500

// ── Storage ───────────────────────────────────────────────────
#define LOG_FILE     "/drop_log.csv"

// ── Commands ──────────────────────────────────────────────────
#define CMD_START      1
#define CMD_STOP       2
#define CMD_CLEAR      4

// ── Ground station MAC — paste yours here ─────────────────────
uint8_t GROUND_STATION_MAC[] = {0xB0, 0xCB, 0xD8, 0xCD, 0xCD, 0x1C};

// ── Structs ───────────────────────────────────────────────────
struct ImuFrame {
  float    ax, ay, az;
  float    gx, gy, gz;
  float    temp;
  bool     freefall;
  uint32_t ts_ms;
};

typedef struct {
  uint32_t ts_ms;
  float    ax, ay, az;
  float    gx, gy, gz;
  float    imu_temp;
  float    ambient_temp;
  uint8_t  freefall;
  uint8_t  logging;
  uint32_t sample_count;
  float    battery_v;
  float    esp32_temp;
} TelemetryPacket;

typedef struct {
  uint8_t cmd;
} CommandPacket;

// ── Global objects ────────────────────────────────────────────
Adafruit_LSM6DSOX   lsm6ds;
Adafruit_LIS3MDL    lis3mdl;
Adafruit_MCP9808    mcp9808;
Adafruit_NeoPixel   pixel(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// ── Global variables ──────────────────────────────────────────
ImuFrame      latest;
float         gx_bias = 0.0f, gy_bias = 0.0f, gz_bias = 0.0f;
volatile bool logging      = false;
volatile bool lowPowerMode = false;
bool          mcp9808_ok   = false;
float         ambientTempC = 0.0f;
uint32_t      lastMcpReadMs  = 0;
unsigned long lastSampleUs   = 0;
uint32_t      sampleCount    = 0;
uint8_t       espnowCounter  = 0;
File          logFile;
portMUX_TYPE  imuMux = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t  peripheralTaskHandle = NULL;
esp_now_peer_info_t peerInfo;

// ── NeoPixel helpers ──────────────────────────────────────────
void setLED(uint8_t r, uint8_t g, uint8_t b) {
  pixel.setPixelColor(0, pixel.Color(r, g, b));
  pixel.show();
}
void ledRed()    { setLED(NEOPIXEL_BRIGHT, 0, 0); }
void ledYellow() { setLED(NEOPIXEL_BRIGHT, NEOPIXEL_BRIGHT, 0); }
void ledGreen()  { setLED(0, NEOPIXEL_BRIGHT, 0); }

// ── Battery ───────────────────────────────────────────────────
float readBatteryVoltage() {
  uint32_t raw = 0;
  for (int i = 0; i < 8; i++) raw += analogRead(BATTERY_PIN);
  return (raw / 8.0f) * (ADC_REF_V / ADC_RESOLUTION) * BATTERY_DIVIDER;
}

// ── ESP32 internal temperature ────────────────────────────────
void esp32_temp_init() {
  Serial.println("ESP32 temp sensor OK");
}

float readESP32Temp() {
  return temperatureRead();
}

// ── ESP-NOW callbacks ─────────────────────────────────────────
void onDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  // optional TX confirmation
}

void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(CommandPacket)) return;
  CommandPacket cmd;
  memcpy(&cmd, data, sizeof(cmd));

  switch (cmd.cmd) {
    case CMD_START:
      if (!logging) {
        logFile = LittleFS.open(LOG_FILE, FILE_APPEND);
        if (logFile) {
          if (logFile.size() == 0)
            logFile.println("ts_ms,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps,imu_temp_c,ambient_temp_c,freefall");
          logging = true;
          ledYellow();
          Serial.println("CMD: Start logging");
        }
      }
      break;
    case CMD_STOP:
      if (logging) {
        logging = false;
        logFile.close();
        ledGreen();
        Serial.printf("CMD: Stop. %u samples written.\n", sampleCount);
      }
      break;
    case CMD_CLEAR:
      if (logging) { logging = false; logFile.close(); }
      LittleFS.remove(LOG_FILE);
      sampleCount = 0;
      Serial.println("CMD: Log cleared");
      break;
    case CMD_LOWPOWER:
      lowPowerMode = !lowPowerMode;
      if (lowPowerMode) {
        lsm6ds.setAccelDataRate(LSM6DS_RATE_52_HZ);
        lsm6ds.setGyroDataRate(LSM6DS_RATE_52_HZ);
        ledRed();
        Serial.println("CMD: Low power mode ON");
      } else {
        lsm6ds.setAccelDataRate(LSM6DS3_ODR);
        lsm6ds.setGyroDataRate(LSM6DS3_ODR);
        ledGreen();
        Serial.println("CMD: Low power mode OFF");
      }
      break;
  }
}

// ── IMU init ──────────────────────────────────────────────────
void imu_init() {
  Wire.beginTransmission(LSM6DS3_ADDR);
  Wire.write(0x0F);
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)LSM6DS3_ADDR, (uint8_t)1);
  uint8_t whoami = Wire.available() ? Wire.read() : 0xFF;
  Serial.printf("WHO_AM_I at 0x6A = 0x%02X\n", whoami);

  uint8_t addr = 0;
  if      (lsm6ds.begin_I2C(0x6A, &Wire)) { addr = 0x6A; }
  else if (lsm6ds.begin_I2C(0x6B, &Wire)) { addr = 0x6B; }
  else { Serial.println("LSM6DS NOT FOUND — halting."); while (1) delay(1000); }

  lsm6ds.setAccelRange(LSM6DS_ACCEL_RANGE_16_G);
  lsm6ds.setAccelDataRate(LSM6DS3_ODR);
  lsm6ds.setGyroRange(LSM6DS_GYRO_RANGE_2000_DPS);
  lsm6ds.setGyroDataRate(LSM6DS3_ODR);
  Serial.printf("IMU at 0x%02X: ±16g, ±2000dps, 416Hz\n", addr);

  if (!lis3mdl.begin_I2C(LIS3MDL_ADDR, &Wire)) {
    Serial.println("LIS3MDL not found (non-fatal)");
  } else {
    lis3mdl.setDataRate(LIS3MDL_DATARATE_155_HZ);
    lis3mdl.setRange(LIS3MDL_RANGE_4_GAUSS);
    lis3mdl.setPerformanceMode(LIS3MDL_MEDIUMMODE);
    lis3mdl.setOperationMode(LIS3MDL_CONTINUOUSMODE);
    Serial.println("LIS3MDL OK");
  }
}

// ── IMU read ──────────────────────────────────────────────────
void read_raw(float &ax, float &ay, float &az,
              float &gx, float &gy, float &gz, float &temp) {
  sensors_event_t accel_evt, gyro_evt, temp_evt;
  lsm6ds.getEvent(&accel_evt, &gyro_evt, &temp_evt);
  ax   = accel_evt.acceleration.x / 9.80665f;
  ay   = accel_evt.acceleration.y / 9.80665f;
  az   = accel_evt.acceleration.z / 9.80665f;
  gx   = gyro_evt.gyro.x * RAD_TO_DEG;
  gy   = gyro_evt.gyro.y * RAD_TO_DEG;
  gz   = gyro_evt.gyro.z * RAD_TO_DEG;
  temp = temp_evt.temperature;
}

ImuFrame read_imu() {
  float ax, ay, az, gx, gy, gz, temp;
  read_raw(ax, ay, az, gx, gy, gz, temp);
  ImuFrame f;
  f.ax    = ax - ACCEL_BIAS_X;
  f.ay    = ay - ACCEL_BIAS_Y;
  f.az    = az - ACCEL_BIAS_Z;
  f.gx    = gx - gx_bias;
  f.gy    = gy - gy_bias;
  f.gz    = gz - gz_bias;
  f.temp  = temp;
  f.ts_ms = millis();
  float mag  = sqrt(f.ax*f.ax + f.ay*f.ay + f.az*f.az);
  f.freefall = (mag < FREEFALL_THRESHOLD_G);
  return f;
}

// ── Gyro calibration ──────────────────────────────────────────
void calibrate_gyro() {
  Serial.println("Gyro cal: keep still for ~3s...");
  double sx = 0, sy = 0, sz = 0;
  float ax, ay, az, gx, gy, gz, temp;
  // Discard first 50 samples to let sensor settle
  for (int i = 0; i < 50; i++) { read_raw(ax, ay, az, gx, gy, gz, temp); delay(2); }
  for (int i = 0; i < GYRO_CAL_SAMPLES; i++) {
    read_raw(ax, ay, az, gx, gy, gz, temp);
    sx += gx; sy += gy; sz += gz;
    delay(2);
  }
  gx_bias = sx / GYRO_CAL_SAMPLES;
  gy_bias = sy / GYRO_CAL_SAMPLES;
  gz_bias = sz / GYRO_CAL_SAMPLES;
  Serial.printf("Gyro bias — X:%.4f Y:%.4f Z:%.4f dps\n", gx_bias, gy_bias, gz_bias);
}

// ── MCP9808 ───────────────────────────────────────────────────
void mcp9808_init() {
  if (!mcp9808.begin(MCP9808_ADDR, &Wire)) {
    Serial.println("MCP9808 NOT FOUND — ambient temp will read 0.00");
    mcp9808_ok = false;
    return;
  }
  mcp9808.setResolution(3);
  mcp9808_ok = true;
  Serial.println("MCP9808 OK");
}

void mcp9808_update() {
  if (!mcp9808_ok) return;
  if (millis() - lastMcpReadMs < 1000) return;
  lastMcpReadMs = millis();
  mcp9808.wake();
  ambientTempC = mcp9808.readTempC();
}

// ── Core 0 peripheral task ────────────────────────────────────
void peripheralTask(void* pvParameters) {
  vTaskDelay(pdMS_TO_TICKS(100));
  for (;;) {
    mcp9808_update();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ── Setup ─────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n=== DV ESP-NOW — Feather ESP32 V2 ===");

  pixel.begin();
  pixel.setBrightness(NEOPIXEL_BRIGHT);
  ledRed();

  Wire.begin(IMU_SDA_PIN, IMU_SCL_PIN);
  Wire.setClock(400000);

  Serial.println("Scanning I2C...");
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  0x%02X\n", addr);
      found++;
    }
  }
  Serial.printf("  %d device(s) found\n", found);

  imu_init();
  calibrate_gyro();
  mcp9808_init();
  esp32_temp_init();

  if (!LittleFS.begin(true)) {
    Serial.println("ERROR: LittleFS mount failed!");
    while (1) delay(1000);
  }

  WiFi.mode(WIFI_STA);
  Serial.print("DV MAC: "); Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: esp_now_init() failed!");
    while (1) delay(1000);
  }
  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataRecv);

  memcpy(peerInfo.peer_addr, GROUND_STATION_MAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("ERROR: Failed to add GS peer");
  } else {
    Serial.println("ESP-NOW peer registered");
  }

  xTaskCreatePinnedToCore(
    peripheralTask, "PeripheralTask", 8192, NULL, 1,
    &peripheralTaskHandle, 0
  );

  ledGreen();
  Serial.println("Ready. Waiting for START command from ground station.");
}

// ── Loop (Core 1) ─────────────────────────────────────────────
void loop() {
  unsigned long now = micros();
  if (now - lastSampleUs < SAMPLE_INTERVAL_US) return;
  lastSampleUs = now;

  ImuFrame f = read_imu();

  portENTER_CRITICAL(&imuMux);
  latest = f;
  float ambientSnap = ambientTempC;
  portEXIT_CRITICAL(&imuMux);

  if (logging && logFile) {
    logFile.printf("%lu,%.4f,%.4f,%.4f,%.2f,%.2f,%.2f,%.2f,%.4f,%d\n",
      f.ts_ms, f.ax, f.ay, f.az,
      f.gx, f.gy, f.gz,
      f.temp, ambientSnap,
      f.freefall ? 1 : 0);

    portENTER_CRITICAL(&imuMux);
    sampleCount++;
    uint32_t sc = sampleCount;
    portEXIT_CRITICAL(&imuMux);

    if (sc % 100 == 0) logFile.flush();
  }

  espnowCounter++;
  if (espnowCounter >= (lowPowerMode ? ESPNOW_SEND_EVERY_N * 4 : ESPNOW_SEND_EVERY_N)) {
    espnowCounter = 0;

    TelemetryPacket pkt;
    pkt.ts_ms        = f.ts_ms;
    pkt.ax           = f.ax;
    pkt.ay           = f.ay;
    pkt.az           = f.az;
    pkt.gx           = f.gx;
    pkt.gy           = f.gy;
    pkt.gz           = f.gz;
    pkt.imu_temp     = f.temp;
    pkt.ambient_temp = ambientSnap;
    pkt.freefall     = f.freefall ? 1 : 0;
    pkt.logging      = logging    ? 1 : 0;
    pkt.sample_count = sampleCount;
    pkt.battery_v    = readBatteryVoltage();
    pkt.esp32_temp   = readESP32Temp();

    esp_now_send(GROUND_STATION_MAC, (uint8_t *)&pkt, sizeof(pkt));
  }
}