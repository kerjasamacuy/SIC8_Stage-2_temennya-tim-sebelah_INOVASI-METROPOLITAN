#include <Arduino.h>
#include <PubSubClient.h>
#include <DHTesp.h>
#include <WiFi.h>

constexpr uint8_t PIN_MQ2 = 35;
constexpr uint8_t PIN_DHT22 = 15;
constexpr uint8_t PIN_RELAY = 32;
constexpr uint8_t PIN_SAKELAR = 26;
DHTesp dhtSensor;

constexpr int GAS_THRESHOLD = 1000;
constexpr float TEMP_THRESHOLD = 35.0;
constexpr float HUM_THRESHOLD = 60.0;

//------------------- MQTT ----------------------------
const char* SSID = "Wokwi-GUEST"; //nama wifi
const char* PASSWORD = ""; //password wifi
const char* MQTT_SERVER = "broker.hivemq.com";
const int MQTT_PORT = 1883;

#define ID_PERANGKAT "ESP32Client-Perangkat1"
const char* clientId = ID_PERANGKAT;

const char* topic_gas = "uy3^*y1y3bhgasGSjas/2026/okt/sic2/temennyatimsebelah/esp32/" ID_PERANGKAT "data/sensor/gas";
const char* topic_temp = "uy3^*y1y3bhgasGSjas/2026/okt/sic2/temennyatimsebelah/esp32/" ID_PERANGKAT "data/sensor/temperature";
const char* topic_hum = "uy3^*y1y3bhgasGSjas/2026/okt/sic2/temennyatimsebelah/esp32/" ID_PERANGKAT "data/sensor/humidity";

WiFiClient espClient;
PubSubClient client(espClient);

unsigned long lastMsg = 0;
const long interval = 2000;
unsigned long lastReconnectAttempt = 0;
//------------------- MQTT ----------------------------

//---------- konversi MQ2 analog ke PPM ---------------
constexpr float MQ2_VCC = 3.3;
constexpr float MQ2_RL = 5.0;
constexpr float MQ2_CLEAN_AIR = 9.83;

//dari grafik datasheet untuk CO2
constexpr float MQ2_A = 36974.0;
constexpr float MQ2_B = -3.109;

float mq2R0 = 10.0;

//ubah ke true kalau pake wokwi, ubah ke false kalau pake hardware beneran
constexpr bool USE_WOKWI_TABLE = true;

//khusus untuk wokwi
constexpr int WOKWI_POINTS = 8;
constexpr float WOKWI_MV[WOKWI_POINTS] = {1552.0, 2302.0, 2833.0, 2972.0, 3032.0, 3065.0, 3098.0, 3115.0};
constexpr float WOKWI_PPM[WOKWI_POINTS] = {1.0, 10.0, 100.0, 400.0, 1000.0, 2000.0, 5000.0, 10000.0};
//---------- konversi MQ2 analog ke PPM ---------------

//----------------- inisialisasi function -----------------
float mq2ReadMv(); //baca milivolt
float mq2ReadRs(); //konvert ADC (analog digital current) ke voltase
void calibrateMq2(); //kalibrasi resistansi MQ2 terhadap udara bersih
float mq2ReadPpm(); //dapat nilai PPM buat real life
float mq2ReadPpmWokwi(); //dapat nilai PPM buat simulasi wokwi

void setupWifi();
bool reconnect();

void relayOn();
void relayOff();
//----------------- inisialisasi function -----------------

//setup & loop
void setup() {
  Serial.begin(115200);
  delay(2000);

  pinMode(PIN_MQ2, INPUT);
  pinMode(PIN_SAKELAR, INPUT_PULLDOWN);
  pinMode(PIN_RELAY, OUTPUT);
  relayOff();

  dhtSensor.setup(PIN_DHT22, DHTesp::DHT22);
  if (!USE_WOKWI_TABLE) calibrateMq2(); //simpan MQ2 di udara bersih dulu selama ~1s (slider wokwi di kirikan)

  setupWifi();

  client.setServer(MQTT_SERVER, MQTT_PORT);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("");
    WiFi.reconnect();
    delay(500);
    return;
  }

  if (!client.connected()) {
    unsigned long now = millis();
    if (now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;
      reconnect();
    }
  }
  else {
    client.loop();
  }

  unsigned long now = millis();
  if (now - lastMsg < interval) return;
  lastMsg = now;

  bool manual = digitalRead(PIN_SAKELAR) == HIGH;

  float gas = lroundf(mq2ReadPpm());
  Serial.printf("raw mV: %d\n", int(analogReadMilliVolts(PIN_MQ2)));

  TempAndHumidity dataDHT = dhtSensor.getTempAndHumidity();
  float suhu = dataDHT.temperature;
  float kelembapan = dataDHT.humidity;

  if (isnan(suhu) || isnan(kelembapan)) {
    Serial.println("Data DHT22 tidak kebaca");
  }
  else {
    // debugging
    Serial.printf("Temp: %.1f C  Hum: %.1f %%  Gas: %.2f", suhu, kelembapan, gas);
    Serial.print("\n");

    if (client.connected()) {
      char buf[16];

      snprintf(buf, sizeof(buf), "%.2f", gas);
      client.publish(topic_gas, buf);

      snprintf(buf, sizeof(buf), "%.1f", suhu);
      client.publish(topic_temp, buf);

      snprintf(buf, sizeof(buf), "%.1f", kelembapan);
      client.publish(topic_hum, buf);
    }
  }

  bool dhtValid = !isnan(suhu) && !isnan(kelembapan);
  bool gasHigh = gas >= GAS_THRESHOLD;
  bool tempHigh = dhtValid && suhu >= TEMP_THRESHOLD;
  bool humLow = dhtValid && kelembapan < HUM_THRESHOLD;

  bool relayTriggered = (gasHigh || tempHigh) && humLow;

  if (relayTriggered || manual) {
    relayOn();
  }
  else {
    relayOff();
  }
}

//definisi function
void relayOn() { digitalWrite(PIN_RELAY, HIGH); }
void relayOff() { digitalWrite(PIN_RELAY, LOW); }

void setupWifi() {
  delay(10);

  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

bool reconnect() {
  if (client.connect(clientId)) {
    Serial.println("connected");
    return true;
  }

  Serial.print("MQTT failed, rc=");
  Serial.print(client.state());
  return false;
}

float mq2ReadMv() {
  float mV = 0.0;
  //for loop buat kurangi noise
  for (int i = 0; i < 10; i++) {
    mV += analogReadMilliVolts(PIN_MQ2);
    delay(5);
  }
  return mV / 10.0;
}

float mq2ReadRs() {
  float vOut = mq2ReadMv() / 1000.0;
  if (vOut < 0.01) vOut = 0.01;
  return MQ2_RL * (MQ2_VCC - vOut) / vOut;
}

void calibrateMq2() {
  float rs = 0.0;
  //for loop buat dapat baseline udara bersih
  for (int i = 0; i < 20; i++) {
    rs += mq2ReadRs();
    delay(50);
  }
  rs /= 20.0;
  mq2R0 = rs / MQ2_CLEAN_AIR;

  Serial.printf("MQ-2 berhasil dikalibrasi, R0 = %.2f kOhm\n", mq2R0);
}

float mq2ReadPpmWokwi() {
  float mV = mq2ReadMv();

  int i = 0;
  while (i < WOKWI_POINTS - 2 && mV > WOKWI_MV[i+1]) i++;

  float l0 = log10f(WOKWI_PPM[i]);
  float l1 = log10f(WOKWI_PPM[i+1]);
  float t = (mV - WOKWI_MV[i]) / (WOKWI_MV[i+1] - WOKWI_MV[i]);

  return powf(10.0f, l0 + t * (l1 - l0));
}

float mq2ReadPpm() {
  if (USE_WOKWI_TABLE) {
    return mq2ReadPpmWokwi();
  }

  float ratio = mq2ReadRs() / mq2R0;
  return MQ2_A * pow(ratio, MQ2_B);
}
