/*
 * ============================================================
 *  DROP TOWER — GROUND STATION (ESP-NOW)
 *  Adafruit Feather ESP32 V2
 * ============================================================
 *  Receives telemetry from DV, forwards to laptop as CSV.
 *  Sends commands to DV when triggered from MATLAB.
 *
 *  LAPTOP SERIAL COMMANDS:
 *    's' — Start logging
 *    'x' — Stop logging
 *    'r' — Clear log
 *    'p' — Toggle low power mode
 *
 *  CSV OUTPUT FORMAT:
 *    ts_ms,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps,
 *    imu_temp_c,ambient_temp_c,freefall,logging,samples,battery_v,esp32_temp
 *
 *  SETUP:
 *    1. Flash this sketch, open Serial Monitor, note the MAC
 *    2. Paste that MAC into GROUND_STATION_MAC in DV_ESPNOW.ino
 *    3. Flash DV_ESPNOW.ino, note the DV MAC
 *    4. Paste the DV MAC into DV_MAC below, reflash this sketch
 *
 *  NO EXTRA LIBRARIES NEEDED — ESP32 Arduino core only.
 * ============================================================
 */

#include <WiFi.h>
#include <esp_now.h>

// ── IMPORTANT: paste DV MAC here ─────────────────────────────
uint8_t DV_MAC[] = {0xC0, 0xCD, 0xD6, 0x38, 0xCC, 0x90};

// ── Commands ──────────────────────────────────────────────────
#define CMD_START      1
#define CMD_STOP       2
#define CMD_CLEAR      4
#define CMD_LOWPOWER   5

// ── Packet structs — must match DV_ESPNOW.ino exactly ────────
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

// ── Globals ───────────────────────────────────────────────────
esp_now_peer_info_t peerInfo;
volatile bool dvConnected = false;
uint32_t lastRxMs = 0;

// ── ESP-NOW callbacks ─────────────────────────────────────────
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(TelemetryPacket)) return;

  TelemetryPacket pkt;
  memcpy(&pkt, data, sizeof(pkt));

  dvConnected = true;
  lastRxMs = millis();

  // Forward to laptop as CSV (13 fields)
  Serial.printf("%lu,%.4f,%.4f,%.4f,%.2f,%.2f,%.2f,%.2f,%.4f,%d,%d,%lu,%.2f,%.2f\n",
    pkt.ts_ms,
    pkt.ax, pkt.ay, pkt.az,
    pkt.gx, pkt.gy, pkt.gz,
    pkt.imu_temp, pkt.ambient_temp,
    pkt.freefall, pkt.logging,
    pkt.sample_count, pkt.battery_v,
    pkt.esp32_temp);
}

void onDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  // Uncomment to debug:
  // Serial.println(status == ESP_NOW_SEND_SUCCESS ? "#CMD_OK" : "#CMD_FAIL");
}

// ── Send command to DV ────────────────────────────────────────
void sendCommand(uint8_t cmd) {
  CommandPacket pkt;
  pkt.cmd = cmd;
  if (esp_now_send(DV_MAC, (uint8_t *)&pkt, sizeof(pkt)) != ESP_OK)
    Serial.println("#ERROR: Failed to send command");
}

// ── Setup ─────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("# === Ground Station ESP-NOW — Feather ESP32 V2 ===");

  WiFi.mode(WIFI_STA);
  Serial.print("# GS MAC: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("# ERROR: esp_now_init() failed!");
    while (1) delay(1000);
  }
  esp_now_register_recv_cb(onDataRecv);
  esp_now_register_send_cb(onDataSent);

  memcpy(peerInfo.peer_addr, DV_MAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK)
    Serial.println("# ERROR: Failed to add DV peer");

  Serial.println("# Ready. Commands: s=start  x=stop  r=clear  p=lowpower");
  Serial.println("# ts_ms,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps,imu_temp_c,ambient_temp_c,freefall,logging,samples,battery_v,esp32_temp");
}

// ── Loop ──────────────────────────────────────────────────────
void loop() {
  // Warn if DV goes silent
  if (dvConnected && (millis() - lastRxMs > 2000)) {
    dvConnected = false;
    Serial.println("# WARNING: No packets from DV for >2s");
  }

  // Read single-char commands from MATLAB over serial
  if (Serial.available()) {
    char c = Serial.read();
    switch (c) {
      case 's': Serial.println("# Sending START");     sendCommand(CMD_START);    break;
      case 'x': Serial.println("# Sending STOP");      sendCommand(CMD_STOP);     break;
      case 'r': Serial.println("# Sending CLEAR");     sendCommand(CMD_CLEAR);    break;
      case 'p': Serial.println("# Sending LOWPOWER");  sendCommand(CMD_LOWPOWER); break;
    }
  }
}