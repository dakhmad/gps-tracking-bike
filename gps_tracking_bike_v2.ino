/*************************************************
 * ESP32 GPS Tracking Bike
 * Firebase + GPS + Buzzer (TIMER SAFE MODE)
 *************************************************/

#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <TinyGPS++.h>

/* WIFI */
#define WIFI_SSID "Dani_Hotspot"
#define WIFI_PASSWORD ""

/* FIREBASE */
#define API_KEY "AIzaSyADeh3RG2LxSg3pBZsAl8ZE9KdP087jrlM"
#define DATABASE_URL "https://iot-bike-tracking-gps-default-rtdb.firebaseio.com"
#define USER_EMAIL "ownerbike@gmail.com"
#define USER_PASSWORD "bike123"
#define BIKE_ID "bike_001"

/* GPS */
#define GPS_RX 16
#define GPS_TX 17

/* BUZZER */
#define BUZZER_PIN 25

HardwareSerial gpsSerial(2);
TinyGPSPlus gps;

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
FirebaseJson json;

/* TIMING */
unsigned long lastSend = 0;
const unsigned long SEND_INTERVAL = 5000;

/* BUZZER STATE */
unsigned long buzzerMillis = 0;
bool buzzerState = false;
bool timerExecuted = false;

/* LAST STATE */
String lastMode = "";
bool lastActive = false;

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  Serial.println("\n=== ESP32 GPS BIKE START ===");

  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
  Serial.println("[OK] GPS initialized");

  Serial.print("[INFO] Connecting WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n[OK] WiFi connected");

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  Serial.println("[OK] Firebase ready");
  Serial.println("================================");
}

void loop() {

  /* ================= GPS ================= */
  while (gpsSerial.available()) gps.encode(gpsSerial.read());

  if (gps.location.isValid() && millis() - lastSend > SEND_INTERVAL) {
    lastSend = millis();

    Serial.println("\n[GPS] Sending location");

    Firebase.RTDB.setDouble(&fbdo,
      "/bikes/" BIKE_ID "/location/lat",
      gps.location.lat());

    Firebase.RTDB.setDouble(&fbdo,
      "/bikes/" BIKE_ID "/location/lng",
      gps.location.lng());

    Serial.println("[OK] GPS sent");
  }

  /* ================= READ BUZZER ================= */
  if (!Firebase.RTDB.getJSON(&fbdo, "/bikes/" BIKE_ID "/buzzer")) {
    Serial.print("[ERR] Read buzzer: ");
    Serial.println(fbdo.errorReason());
    return;
  }

  FirebaseJsonData d;
  String mode = "off";
  bool active = false;

  fbdo.jsonObject().get(d, "mode");
  if (d.success) mode = d.to<String>();

  fbdo.jsonObject().get(d, "active");
  if (d.success) active = d.to<bool>();

  if (mode != lastMode || active != lastActive) {
    Serial.println("\n[BUZZER] State changed");
    Serial.print("Mode   : "); Serial.println(mode);
    Serial.print("Active : "); Serial.println(active);
    lastMode = mode;
    lastActive = active;
  }

  /* ================= MODE OFF ================= */
  if (!active || mode == "off") {
    digitalWrite(BUZZER_PIN, LOW);
    buzzerState = false;
    timerExecuted = false;
    return;
  }

  /* ================= MODE WARNING ================= */
  if (mode == "warning") {
    if (millis() - buzzerMillis > 500) {
      buzzerMillis = millis();
      buzzerState = !buzzerState;
      digitalWrite(BUZZER_PIN, buzzerState);
      Serial.println(buzzerState ? "[WARNING] ON" : "[WARNING] OFF");
    }
    return;
  }

  /* ================= MODE TIMER ================= */
  if (mode == "timer" && !timerExecuted) {

    if (!Firebase.RTDB.getJSON(&fbdo, "/bikes/" BIKE_ID "/timer")) {
      Serial.print("[ERR] Read timer: ");
      Serial.println(fbdo.errorReason());
      return;
    }

    bool timerActive = true;
    long finishedAt = 0;

    fbdo.jsonObject().get(d, "active");
    if (d.success) timerActive = d.to<bool>();

    fbdo.jsonObject().get(d, "finishedAt");
    if (d.success) finishedAt = d.to<long>();

    /* SYARAT VALID */
    if (!timerActive && finishedAt > 0) {
      Serial.println("\n[TIMER] Timer VALID & finished");
      Serial.println("[TIMER] Buzzing for 3 seconds");

      unsigned long start = millis();
      while (millis() - start < 3000) {
        digitalWrite(BUZZER_PIN, HIGH);
        delay(300);
        digitalWrite(BUZZER_PIN, LOW);
        delay(300);
      }

      timerExecuted = true;
      Serial.println("[TIMER] Buzzer done");

      json.clear();
      json.set("mode", "off");
      json.set("active", false);

      Firebase.RTDB.updateNode(
        &fbdo,
        "/bikes/" BIKE_ID "/buzzer",
        &json
      );

      Serial.println("[OK] Buzzer reset");
    }
  }
}
