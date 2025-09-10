#include <Preferences.h>   // Tambahan untuk simpan token

// ===== Blynk Configuration =====
#define BLYNK_TEMPLATE_ID "TMPL6KogfH2jS"
#define BLYNK_TEMPLATE_NAME "Control Tambak"
#define BLYNK_AUTH_TOKEN "W2GqxXwXgP8NhX2SzWuZhnkGnuqF2Lc7"

#define BLYNK_PRINT Serial
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <ESP32Servo.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ===== WiFi =====
char ssid[] = "Doktor Tije Digital";
char pass[] = "doktortj2025";

// ===== API Server =====
const char* serverName = "https://cota-demo.agrobizportal.com/api/sensor-data/insert";
const char* tokenApiUrl = "https://cota-demo.agrobizportal.com/api/token/verify";

// ===== Web Server Lokal =====
WebServer server(80);

// ===== Preferences (Flash Storage) =====
Preferences preferences;
String savedToken = "";   // Token global

// ===== Sensor dan Servo =====
#define ONE_WIRE_BUS 32
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature suhuSensor(&oneWire);

Servo servo;
const int servoPin = 13;
const int pHPin = 34;
const int turbidityPin = 33;

int pH_buf[10];
BlynkTimer timer;

// ======= Fungsi Servo =======
void beriPakan() {
  Serial.println("🔁 Memberi pakan (3x buka-tutup)");
  for (int i = 0; i < 3; i++) {
    servo.write(80); delay(2000);
    servo.write(0);  delay(2000);
  }
  Serial.println("✅ Pemberian pakan selesai");
}

// ======= Handler Blynk =======
BLYNK_WRITE(V0) {
  int tombol = param.asInt();
  if (tombol == 1) {
    beriPakan();
    Blynk.virtualWrite(V0, 0);
  }
}

// ======= Fungsi Kirim Token ke API =======
void kirimTokenKeAPI(String token) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String apiUrl = "https://cota-demo.agrobizportal.com/api/pond/connect-from-iot";  
    http.begin(apiUrl);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");   // ✅ Tambahan header

    // Token dikirim dalam JSON
    String jsonData = "{\"token\": \"" + token + "\"}";
    int httpResponseCode = http.POST(jsonData);

    Serial.print("📡 Kirim Token ke API: ");
    Serial.println(httpResponseCode);

    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.println("🔎 Respon API: " + response);
    } else {
      Serial.print("⚠ Error: ");
      Serial.println(httpResponseCode);
    }

    http.end();
  } else {
    Serial.println("⚠ Tidak bisa kirim token: WiFi tidak terhubung");
  }
}

// ======= Kirim Data Sensor =======
void sendSensorData() {
  // === Sensor pH ===
  for (int i = 0; i < 10; i++) {
    pH_buf[i] = analogRead(pHPin);
    delay(10);
  }
  for (int i = 0; i < 9; i++) {
    for (int j = i + 1; j < 10; j++) {
      if (pH_buf[j] < pH_buf[i]) {
        int temp = pH_buf[i];
        pH_buf[i] = pH_buf[j];
        pH_buf[j] = temp;
      }
    }
  }
  int median = pH_buf[5];
  float voltage = median * (3.3 / 4095.0);
  float pH_value = voltage * 2.55;
  Serial.printf("🧪 pH Air: %.2f\n", pH_value);
  Blynk.virtualWrite(V3, pH_value);

  // === Sensor Turbidity ===
  int rawTurb = analogRead(turbidityPin);
  float voltageT = rawTurb * (3.3 / 4095.0);
  float turbidityNTU = 100.0 - (rawTurb / 3700.0 * 100.0);
  Serial.printf("🌫 Turbidity: %.1f NTU (%.2f V)\n", turbidityNTU, voltageT);
  Blynk.virtualWrite(V2, turbidityNTU);

  // === Sensor Suhu ===
  suhuSensor.requestTemperatures();
  float suhu = suhuSensor.getTempCByIndex(0);
  Serial.printf("🌡 Suhu air: %.2f °C\n", suhu);
  Blynk.virtualWrite(V1, suhu);

  // === Kirim ke API ===
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(serverName);
    http.addHeader("Content-Type", "application/json");

    String jsonData = "{\"keasaman\": " + String(pH_value, 2) +
                      ", \"kekeruhan\": " + String(turbidityNTU, 1) +
                      ", \"suhu\": " + String(suhu, 2) + "}";

    delay(100);
    int httpResponseCode = http.POST(jsonData);
    Serial.print("📡 Kirim ke API: ");
    Serial.println(httpResponseCode);
    http.end();
  } else {
    Serial.println("⚠ Gagal kirim ke API: Tidak ada koneksi WiFi");
  }
}

// ======= Setup =======
void setup() {
  Serial.begin(115200);
  delay(1000);

  // Buka preferences untuk baca token
  preferences.begin("config", false);
  savedToken = preferences.getString("token", "");
  if (savedToken != "") {
    Serial.println("✅ Token ditemukan di flash: " + savedToken);
  } else {
    Serial.println("⚠ Belum ada token tersimpan");
  }

  Serial.println("⏳ Menghubungkan ke WiFi...");
  WiFi.begin(ssid, pass);
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 20) {
    delay(500); Serial.print("."); retry++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi Terhubung!");
    Serial.println("IP: " + WiFi.localIP().toString());

    Blynk.config(BLYNK_AUTH_TOKEN);
    if (Blynk.connect(5000)) {
      Serial.println("✅ Terhubung ke Blynk!");
    } else {
      Serial.println("❌ Gagal konek ke Blynk!");
    }
  } else {
    Serial.println("\n❌ Gagal konek ke WiFi!");
  }

  // Mulai sensor & servo
  suhuSensor.begin();
  servo.attach(servoPin);
  servo.write(0);

  // Kirim data tiap 5 detik
  timer.setInterval(5000L, sendSensorData);

  // ====== Routes ======
  server.on("/", HTTP_GET, []() {
    String html = "<html><body><h2>🌐 Kontrol Servo Tambak</h2>"
                  "<form action='/beri-pakan'><button>Beri Pakan</button></form>"
                  "</body></html>";
    server.send(200, "text/html", html);
  });

  server.on("/beri-pakan", HTTP_GET, []() {
    // ✅ Kirim respon dulu supaya client tidak timeout
    String json = "{\"status\": \"200\", \"message\": \"Pakan sedang diberikan\"}";
    server.send(200, "application/json", json);

    // Jalankan servo setelah respon terkirim
    beriPakan();
  });

  // ====== Endpoint Terima Token ======
  server.on("/pond/token", HTTP_GET, []() {
    if (server.hasArg("token")) {
      String token = server.arg("token");
      Serial.println("🔑 Token diterima: " + token);

      // Simpan ke flash
      preferences.putString("token", token);
      savedToken = token;

      // Kirim ke API tujuan
      kirimTokenKeAPI(token);

      String json = "{\"status\": \"200\", \"message\": \"Token diterima & disimpan\"}";
      server.send(200, "application/json", json);
    } else {
      server.send(400, "application/json", "{\"status\": \"400\", \"message\": \"Token tidak ada\"}");
    }
  });

  server.begin();
  Serial.println("🌐 Web server aktif!");
}

// ======= Loop =======
void loop() {
  Blynk.run();
  timer.run();
  server.handleClient();
}

