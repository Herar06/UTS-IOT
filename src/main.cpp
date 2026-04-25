#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <BH1750.h>
#include <DHT.h>

// ================= WIFI =================
#define WIFI_SSID "CEIOT"
#define WIFI_PASS "CE-1OT@!"

// ================= MQTT =================
#define MQTT_HOST "broker.emqx.io"
#define MQTT_PORT 1883
#define MQTT_CLIENT_ID "ESP32_FINAL_FIX"

#define TOPIC_LIGHT "LightE"
#define TOPIC_TEMP  "TemperaturenG"
#define TOPIC_HUM   "HumidityF"
#define TOPIC_LED   "LED/control"

WiFiClient espClient;
PubSubClient mqtt(espClient);

// ================= SENSOR =================
#define SDA_PIN 21
#define SCL_PIN 22
BH1750 lightMeter;

#define DHTPIN 5
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// ================= LED =================
#define LED_PIN 4

// ================= RTOS =================
QueueHandle_t sensorQueue;
SemaphoreHandle_t mqttMutex;
SemaphoreHandle_t ledSem;   

// ================= STRUCT =================
struct SensorData {
  float lux;
  float temp;
  float hum;
};

// ================= DELAY =================
int X = 1;
int Y = 0;

// ================= WIFI =================
void connectWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("WiFi connecting");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" CONNECTED");
}

// ================= MQTT CALLBACK =================
void callback(char* topic, byte* payload, unsigned int length) {
  String msg = "";

  for (int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }

  msg.trim();
  msg.toUpperCase();

  Serial.print("MQTT MSG: ");
  Serial.println(msg);

  if (msg == "ON") {
    xSemaphoreGive(ledSem);   
  }
}

// ================= MQTT CONNECT =================
void connectMQTT() {
  while (!mqtt.connected()) {
    Serial.print("MQTT connecting...");

    if (mqtt.connect(MQTT_CLIENT_ID)) {
      Serial.println(" CONNECTED");
      mqtt.subscribe(TOPIC_LED);
    } else {
      Serial.print(" FAILED rc=");
      Serial.println(mqtt.state());
      delay(2000);
    }
  }
}

// ================= TASK MQTT =================
void taskMQTT(void *pv) {
  connectMQTT();

  while (1) {
    if (!mqtt.connected()) connectMQTT();

    if (xSemaphoreTake(mqttMutex, portMAX_DELAY)) {
      mqtt.loop();
      xSemaphoreGive(mqttMutex);
    }

    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

// ================= TASK SENSOR =================
void taskSensor(void *pv) {
  SensorData data;

  while (1) {

    float total = 0;
    for (int i = 0; i < 5; i++) {
      total += lightMeter.readLightLevel();
      vTaskDelay(50 / portTICK_PERIOD_MS);
    }

    float lux = total / 5.0;

    if (lux < 0 || lux > 10000) {
      Serial.println("BH1750 ERROR");
      vTaskDelay(2000 / portTICK_PERIOD_MS);
      continue;
    }

    float t = dht.readTemperature();
    float h = dht.readHumidity();

    if (isnan(t) || isnan(h)) {
      Serial.println("DHT ERROR");
      vTaskDelay(2000 / portTICK_PERIOD_MS);
      continue;
    }

    data.lux = lux;
    data.temp = t;
    data.hum = h;

    Serial.printf("SENSOR OK -> Lux=%.1f T=%.1f H=%.1f\n", lux, t, h);

    xQueueOverwrite(sensorQueue, &data);

    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }
}

// ================= TASK PUBLISH =================
void taskPublish(void *pv) {
  SensorData data;

  while (1) {

  
    xQueuePeek(sensorQueue, &data, portMAX_DELAY);

    // ===== LIGHT =====
    if (xSemaphoreTake(mqttMutex, portMAX_DELAY)) {
      mqtt.publish(TOPIC_LIGHT, String(data.lux, 2).c_str());
      xSemaphoreGive(mqttMutex);
    }

    vTaskDelay(pdMS_TO_TICKS(1000)); // X = 1 detik

    // ===== TEMP =====
    if (xSemaphoreTake(mqttMutex, portMAX_DELAY)) {
      mqtt.publish(TOPIC_TEMP, String(data.temp, 2).c_str());
      xSemaphoreGive(mqttMutex);
    }


    // ===== HUM =====
    if (xSemaphoreTake(mqttMutex, portMAX_DELAY)) {
      mqtt.publish(TOPIC_HUM, String(data.hum, 2).c_str());
      xSemaphoreGive(mqttMutex);
    }

    Serial.println("MQTT SENT");

    vTaskDelay(pdMS_TO_TICKS(9000)); 
  }
}

// ================= TASK LED =================
void taskLED(void *pv) {
  while (1) {

    // ===== MODE MQTT (OVERRIDE) =====
    if (xSemaphoreTake(ledSem, 0) == pdTRUE) {
      Serial.println("OVERRIDE: LED NYALA 10 DETIK");

      digitalWrite(LED_PIN, HIGH);
      vTaskDelay(pdMS_TO_TICKS(10000));

      digitalWrite(LED_PIN, LOW);
      continue;
    }

    // ===== MODE NORMAL =====
    digitalWrite(LED_PIN, HIGH);
    vTaskDelay(pdMS_TO_TICKS(1000));

    digitalWrite(LED_PIN, LOW);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);

  Wire.begin(SDA_PIN, SCL_PIN);
  delay(500);

  lightMeter.begin();
  dht.begin();

  connectWiFi();

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(callback);

  sensorQueue = xQueueCreate(1, sizeof(SensorData));
  mqttMutex = xSemaphoreCreateMutex();
  ledSem = xSemaphoreCreateBinary();   

  xTaskCreate(taskSensor, "sensor", 4096, NULL, 1, NULL);
  xTaskCreate(taskPublish, "publish", 4096, NULL, 1, NULL);
  xTaskCreate(taskMQTT, "mqtt", 4096, NULL, 2, NULL);
  xTaskCreate(taskLED, "led", 2048, NULL, 1, NULL);
}

void loop() {}