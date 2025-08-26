// ===== Blynk Configuration =====
#define BLYNK_TEMPLATE_ID "TMPL6KogfH2jS"
#define BLYNK_TEMPLATE_NAME "Control Tambak"
#define BLYNK_AUTH_TOKEN "W2GqxXwXgP8NhX2SzWuZhnkGnuqF2Lc7"

#define BLYNK_PRINT Serial
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <ESP32Servo.h>
#include <HTTPClient.h>
#include <WebServer.h>          // Untuk web kontrol lokal
#include <OneWire.h>
#include <DallasTemperature.h>  // Sensor Suhu DS18B20

// ===== WiFi =====
char ssid[] = "Doktor Tije Digital";
char pass[] = "doktortj2025";

// ===== API Server =====
const char* serverName = "http://192.168.18.40:8000/api/sensor-data/insert";

// ===== Web Server Lokal =====
WebServer server(80);

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

// ======= Fungsi Servo (beri pakan) =======
void beriPakan() {
  Serial.println("🔁 Memberi pakan (3x buka‑tutup)");
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

  // === Sensor Suhu DS18B20 ===
  suhuSensor.requestTemperatures();
  float suhu = suhuSensor.getTempCByIndex(0);
  Serial.printf("🌡 Suhu air: %.2f °C\n", suhu);
  Blynk.virtualWrite(V1, suhu);

  // === Kirim ke API Postman ===
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

// ======= Setup Awal =======
void setup() {
  Serial.begin(115200);
  delay(1000);

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

  // Mulai sensor dan servo
  suhuSensor.begin();
  servo.attach(servoPin);
  servo.write(0);

  // Kirim data setiap 5 detik
  timer.setInterval(5000L, sendSensorData);

  // ====== WEB SERVER ROUTES ======
  server.on("/", HTTP_GET, []() {
    String html = "<html><body><h2>🌐 Kontrol Servo Tambak</h2>"
                  "<form action='/beri-pakan'><button>Beri Pakan</button></form>"
                  "</body></html>";
    server.send(200, "text/html", html);
  });

  server.on("/beri-pakan", HTTP_GET, []() {
    beriPakan();
    String json = "{\"status\": \"200\", \"message\": \"Pakan diberikan\"}";
    server.send(200, "application/json", json);
  });


  server.begin();
  Serial.println("🌐 Web server aktif!");
}

// ======= Loop Utama =======
void loop() {
  Blynk.run();
  timer.run();
  server.handleClient();  // ← agar ESP32 bisa menerima request web
}
