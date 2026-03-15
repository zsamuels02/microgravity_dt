#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <WiFi.h>
#include <WebServer.h>

// ── WiFi AP credentials ──────────────────────────────────────────────────────
const char* AP_SSID = "IMU-ESP32";
const char* AP_PASS = "imudata123";

// ── Web server on port 80 ────────────────────────────────────────────────────
WebServer server(80);

Adafruit_MPU6050 mpu;

struct ImuData {
  float ax, ay, az;   // m/s²
  float gx, gy, gz;   // rps
} imuData;

// ── HTML dashboard ───────────────────────────────────────────────────────────
void handleRoot() {
  const char* html = R"rawhtml(
<!DOCTYPE html><html><head>
<meta charset="utf-8">
<title>IMU Live Data</title>
<style>
  body { font-family: Helvetica; background: #111; color: #0f0; padding: 2rem; }
  h1   { color: #0ff; }
  table{ border-collapse: collapse; width: 400px; }
  td   { padding: 6px 12px; border: 1px solid #333; }
  td:first-child { color: #aaa; }
  .val { color: #0f0; }
</style>
</head><body>
<h1>Drop Tower Live IMU Data</h1>
<table>
  <tr><td>Accel X</td><td class="val" id="ax">--</td></tr>
  <tr><td>Accel Y</td><td class="val" id="ay">--</td></tr>
  <tr><td>Accel Z</td><td class="val" id="az">--</td></tr>
  <tr><td>Gyro X</td> <td class="val" id="gx">--</td></tr>
  <tr><td>Gyro Y</td> <td class="val" id="gy">--</td></tr>
  <tr><td>Gyro Z</td> <td class="val" id="gz">--</td></tr>
</table>
<script>
  const src = new EventSource('/stream');
  src.onmessage = e => {
    const d = JSON.parse(e.data);
    document.getElementById('ax').textContent = d.ax.toFixed(2) + ' m/s²';
    document.getElementById('ay').textContent = d.ay.toFixed(2) + ' m/s²';
    document.getElementById('az').textContent = d.az.toFixed(2) + ' m/s²';
    document.getElementById('gx').textContent = d.gx.toFixed(3) + ' rps';
    document.getElementById('gy').textContent = d.gy.toFixed(3) + ' rps';
    document.getElementById('gz').textContent = d.gz.toFixed(3) + ' rps';
  };
</script>
</body></html>
)rawhtml";
  server.send(200, "text/html", html);
}

// ── JSON snapshot (GET /data) ────────────────────────────────────────────────
void handleData() {
  char buf[128];
  snprintf(buf, sizeof(buf),
    "{\"ax\":%.3f,\"ay\":%.3f,\"az\":%.3f,"
    "\"gx\":%.4f,\"gy\":%.4f,\"gz\":%.4f}",
    imuData.ax, imuData.ay, imuData.az,
    imuData.gx, imuData.gy, imuData.gz);
  server.send(200, "application/json", buf);
}

// ── SSE stream (GET /stream) ─────────────────────────────────────────────────
WiFiClient sseClient;
bool sseConnected = false;

void handleStream() {
  sseClient = server.client();
  sseClient.println("HTTP/1.1 200 OK");
  sseClient.println("Content-Type: text/event-stream");
  sseClient.println("Cache-Control: no-cache");
  sseClient.println("Connection: keep-alive");
  sseClient.println();
  sseConnected = true;
}

void pushSSE() {
  if (!sseConnected || !sseClient.connected()) {
    sseConnected = false;
    return;
  }
  char buf[160];
  snprintf(buf, sizeof(buf),
    "data:{\"ax\":%.3f,\"ay\":%.3f,\"az\":%.3f,"
    "\"gx\":%.4f,\"gy\":%.4f,\"gz\":%.4f}\n\n",
    imuData.ax, imuData.ay, imuData.az,
    imuData.gx, imuData.gy, imuData.gz);
  sseClient.print(buf);
}

// ────────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  Wire.begin(8, 9);  // SDA=IO8, SCL=IO9 — adjust if needed
  Wire.setClock(400000); // reading at 400kHz

  if (!mpu.begin()) {
    Serial.println("MPU6050 init failed — check wiring!");
    while (1) yield();
  }
  Serial.println("MPU6050 found");
  mpu.setFilterBandwidth(MPU6050_BAND_260_HZ);

  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  server.on("/",       handleRoot);
  server.on("/data",   handleData);
  server.on("/stream", handleStream);
  server.begin();
  Serial.println("HTTP server started — connect to IMU-ESP32 and open 192.168.4.1");
}

void loop() {
  server.handleClient();

  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  imuData = { a.acceleration.x, a.acceleration.y, a.acceleration.z,
              g.gyro.x, g.gyro.y, g.gyro.z };

  pushSSE();

  Serial.printf("Accel  X:%.1f  Y:%.1f  Z:%.1f m/s²\n", imuData.ax, imuData.ay, imuData.az);
  Serial.printf("Gyro   X:%.1f  Y:%.1f  Z:%.1f rps\n",  imuData.gx, imuData.gy, imuData.gz);
}