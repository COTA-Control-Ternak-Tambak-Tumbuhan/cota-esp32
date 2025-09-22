#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ArduinoJson.h>

// ===== WiFi =====
char ssid[] = "Doktor Tije Digital";
char pass[] = "doktortj2025";

// ===== API Server =====
const char* serverApiSensor = "https://cota-demo.agrobizportal.com/api/sensor-data/insert";
const char* apiTokenUrl     = "https://cota-demo.agrobizportal.com/api/pond/connect-from-iot";

// ===== MQTT (Broker TLS) =====
const char* mqtt_server = "chameleon.lmq.cloudamqp.com";
const int   mqtt_port   = 8883;
const char* mqtt_user   = "anfvrqjy:anfvrqjy";
const char* mqtt_pass   = "V4OJdwnNv8d8nN2OmCbLrdBqDF5-WS5G";
const char* mqtt_topic_perintah = "cota/command/feed_all";

// ===== Web Server Lokal =====
WebServer server(80);

// ===== Preferences (Flash Storage) =====
Preferences preferences;
String savedToken = "";

// ===== Sensor & Servo =====
#define ONE_WIRE_BUS 35
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature suhuSensor(&oneWire);

Servo servo;
const int servoPin = 13;
const int pHPin = 34;
const int turbidityPin = 33;
int pH_buf[10];

// ===== MQTT Client =====
WiFiClientSecure espClient;
PubSubClient client(espClient);

// ===== Fungsi Servo =====
void beriPakan() {
  Serial.println("🔁 Memberi pakan (3x buka-tutup)");
  for (int i = 0; i < 3; i++) {
    servo.write(80); delay(2000);
    servo.write(0);  delay(2000);
  }
  Serial.println("✅ Pemberian pakan selesai");
}

// ===== Kirim Token ke API =====
void kirimTokenKeAPI(String token) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(apiTokenUrl);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");

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
  }
}

// ===== Kirim Data Sensor =====
void sendSensorData() {
  // === Sensor pH ===
  for (int i = 0; i < 10; i++) { 
    pH_buf[i] = analogRead(pHPin); 
    delay(10); 
  }

  // Urutkan untuk cari median
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
  float pH_value;

  if (median < 50) {  
    pH_value = -1;  
    Serial.println("⚠ Sensor pH tidak terbaca!");
  } else {
    float voltage = median * (3.3 / 4095.0);
    pH_value = voltage * 2.55;
    Serial.printf("🧪 pH Air: %.2f\n", pH_value);
  }

  // === Sensor Turbidity ===
  int rawTurb = analogRead(turbidityPin);
  float turbidityNTU;
  if (rawTurb < 50) {
    turbidityNTU = -1;
    Serial.println("⚠ Sensor turbidity tidak terbaca!");
  } else {
    turbidityNTU = 100.0 - (rawTurb / 3700.0 * 100.0);
    Serial.printf("🌫 Turbidity: %.1f NTU\n", turbidityNTU);
  }

  // === Sensor Suhu (DS18B20) ===
  suhuSensor.requestTemperatures();
  delay(500);
  float suhu = suhuSensor.getTempCByIndex(0);

  if (suhu == -127.00) {
    Serial.println("⚠ Sensor DS18B20 tidak terdeteksi!");
  } else {
    Serial.printf("🌡 Suhu air: %.2f °C\n", suhu);
  }

  // === Kirim ke API (kalau token ada) ===
  if (WiFi.status() == WL_CONNECTED && savedToken != "") {
    HTTPClient http;
    http.begin(serverApiSensor);
    http.addHeader("Content-Type", "application/json");

    String jsonData = "{\"token\": \"" + savedToken + "\", "
                      "\"keasaman\": " + String(pH_value, 2) +
                      ", \"kekeruhan\": " + String(turbidityNTU, 1) +
                      ", \"suhu\": " + String(suhu, 2) + "}";

    int httpResponseCode = http.POST(jsonData);
    Serial.print("📡 Kirim data sensor ke API: ");
    Serial.println(httpResponseCode);
    http.end();
  } else {
    Serial.println("⚠ Gagal kirim ke API (token kosong atau WiFi putus)");
  }
}

// ===== MQTT Callback =====
void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) { message += (char)payload[i]; }
  Serial.printf("📩 MQTT: %s\n", message.c_str());
  if (message == "FEED") {
    beriPakan();
  }
}

// ===== MQTT Reconnect =====
void reconnect() {
  while (!client.connected()) {
    Serial.print("⏳ MQTT connect...");
    String clientId = "esp32-cota-" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      Serial.println("✅ MQTT Connected!");
      client.subscribe(mqtt_topic_perintah);
    } else {
      Serial.printf("❌ Gagal (rc=%d). Retry 5s\n", client.state());
      delay(5000);
    }
  }
}

// ===== Setup =====
void setup() {
  Serial.begin(115200);
  delay(1000);

  // Preferences
  preferences.begin("config", false);
  savedToken = preferences.getString("token", "");
  if (savedToken != "") Serial.println("✅ Token ditemukan: " + savedToken);

  // WiFi
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\n✅ WiFi Terhubung! IP: " + WiFi.localIP().toString());

  // MQTT
  espClient.setInsecure();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  // Sensor & Servo
  suhuSensor.begin();
  servo.attach(servoPin);
  servo.write(0);

  // WebServer routes
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", "<h2>🌐 Kontrol Tambak</h2><a href='/beri-pakan'>Beri Pakan</a>");
  });

  server.on("/beri-pakan", HTTP_GET, []() {
    server.send(200, "application/json", "{\"status\":200, \"message\":\"Pakan diberikan\"}");
    beriPakan();
  });

  server.on("/pond/token", HTTP_GET, []() {
    if (server.hasArg("token")) {
      String token = server.arg("token");
      preferences.putString("token", token);
      savedToken = token;
      Serial.println("🔑 Token diterima: " + token);
      kirimTokenKeAPI(token);
      server.send(200, "application/json", "{\"status\":200, \"message\":\"Token disimpan\"}");
    } else {
      server.send(400, "application/json", "{\"status\":400, \"message\":\"Token tidak ada\"}");
    }
  });

  server.begin();
  Serial.println("🌐 Web server aktif!");

  // Jalankan pertama kali
  sendSensorData();
}

// ===== Loop =====
void loop() {
  server.handleClient();

  if (WiFi.status() == WL_CONNECTED) {
    if (!client.connected()) reconnect();
    client.loop();
  }

  // Sampling sensor tiap 5 detik
  static unsigned long lastMillis = 0;
  if (millis() - lastMillis > 5000) {
    lastMillis = millis();
    sendSensorData();
  }
}
