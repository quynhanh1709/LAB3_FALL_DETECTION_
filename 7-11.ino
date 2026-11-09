
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <math.h>

// =====================================================
// 1. KHAI BAO CHAN GPIO
// =====================================================

#define SDA_PIN       21
#define SCL_PIN       22

#define LED_PIN       2
#define BUZZER_PIN    25

// =====================================================
// 2. WIFI VA MQTT
// =====================================================

const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";

const char* MQTT_SERVER = "broker.emqx.io";
const int MQTT_PORT = 1883;

// MQTT TOPICS
const char* TOPIC_DATA = "falldetection/data";
const char* TOPIC_ALERT = "falldetection/alert";

const char* TOPIC_LED = "falldetection/led";
const char* TOPIC_BUZZER = "falldetection/buzzer";
const char* TOPIC_MODE = "falldetection/mode";
const char* TOPIC_RESET = "falldetection/reset";

const char* TOPIC_LED_STATUS = "falldetection/led/status";
const char* TOPIC_BUZZER_STATUS = "falldetection/buzzer/status";

// =====================================================
// 3. DOI TUONG CAM BIEN VA MQTT
// =====================================================

Adafruit_MPU6050 mpu;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// =====================================================
// 4. CAC NGUONG PHAT HIEN TE NGA
// =====================================================

#define IMPACT_THRESHOLD     2.34
#define ANGLE_THRESHOLD      50.0

#define FALL_WINDOW_MS       3000
#define POSTURE_HOLD_MS      500
#define FALL_COOLDOWN_MS     5000

// =====================================================
// 5. CAU TRUC DU LIEU
// =====================================================

struct SensorData {
  float ax;
  float ay;
  float az;

  float acc;
  float roll;
  float pitch;

  bool fall;
};

struct AlertEvent {
  bool fall;
};

enum CommandType {
  CMD_LED,
  CMD_BUZZER,
  CMD_MANUAL,
  CMD_AUTO,
  CMD_RESET
};

struct Command {
  CommandType type;
  bool value;
};

// =====================================================
// 6. QUEUE FREERTOS
// =====================================================

QueueHandle_t dataQueue;
QueueHandle_t alertQueue;
QueueHandle_t commandQueue;

// =====================================================
// 7. BIEN TRANG THAI
// =====================================================

volatile bool fallDetected = false;

volatile bool actualLEDState = false;
volatile bool actualBuzzerState = false;

volatile bool resetFallRequested = false;

bool manualMode = false;

bool manualLED = false;
bool manualBuzzer = false;

bool ledOverrideActive = false;
bool buzzerOverrideActive = false;

// =====================================================
// 8. BIEN PHAT HIEN TE NGA
// =====================================================

bool impactDetected = false;
bool postureConfirmed = false;
bool fallLatched = false;

unsigned long impactTime = 0;
unsigned long postureTime = 0;
unsigned long lastFallTime = 0;

unsigned long lastSerialTime = 0;

// =====================================================
// 9. BIEN GUI DU LIEU MQTT
// =====================================================

unsigned long lastPublishTime = 0;

bool lastPublishedFall = false;

bool lastPublishedLEDState = false;
bool lastPublishedBuzzerState = false;

volatile bool statusPublishNeeded = true;

// =====================================================
// 10. KHOI TAO WIFI
// =====================================================

void connectWiFi() {

  Serial.println();
  Serial.println("Dang ket noi WiFi...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {

    Serial.print(".");
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  Serial.println();
  Serial.println("WiFi da ket noi!");

  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

// =====================================================
// 11. MQTT CALLBACK
// =====================================================

void mqttCallback(char* topic, byte* payload, unsigned int length) {

  char message[32];

  unsigned int copyLength = length;

  if (copyLength >= sizeof(message)) {
    copyLength = sizeof(message) - 1;
  }

  for (unsigned int i = 0; i < copyLength; i++) {
    message[i] = (char)payload[i];
  }

  message[copyLength] = '\0';

  String receivedMessage = String(message);
  receivedMessage.trim();
  receivedMessage.toUpperCase();

  Command cmd;

  if (strcmp(topic, TOPIC_LED) == 0) {

    if (receivedMessage == "ON") {
      cmd.type = CMD_LED;
      cmd.value = true;
    }
    else if (receivedMessage == "OFF") {
      cmd.type = CMD_LED;
      cmd.value = false;
    }
    else {
      return;
    }
  }

  else if (strcmp(topic, TOPIC_BUZZER) == 0) {

    if (receivedMessage == "ON") {
      cmd.type = CMD_BUZZER;
      cmd.value = true;
    }
    else if (receivedMessage == "OFF") {
      cmd.type = CMD_BUZZER;
      cmd.value = false;
    }
    else {
      return;
    }
  }

  else if (strcmp(topic, TOPIC_MODE) == 0) {

    if (receivedMessage == "MANUAL") {
      cmd.type = CMD_MANUAL;
      cmd.value = true;
    }
    else if (receivedMessage == "AUTO") {
      cmd.type = CMD_AUTO;
      cmd.value = false;
    }
    else {
      return;
    }
  }

  else if (strcmp(topic, TOPIC_RESET) == 0) {

    if (receivedMessage == "RESET") {
      cmd.type = CMD_RESET;
      cmd.value = false;
    }
    else {
      return;
    }
  }

  else {
    return;
  }

  // Gui lenh vao Queue
  if (commandQueue != NULL) {
    xQueueSend(commandQueue, &cmd, 0);
  }
}

// =====================================================
// 12. KET NOI LAI MQTT
// =====================================================

void reconnectMQTT() {

  while (!mqttClient.connected()) {

    Serial.print("Dang ket noi MQTT...");

    String clientId = "ESP32_FallDetection_";
    clientId += String((uint32_t)ESP.getEfuseMac(), HEX);

    if (mqttClient.connect(clientId.c_str())) {

      Serial.println("Thanh cong!");

      mqttClient.subscribe(TOPIC_LED);
      mqttClient.subscribe(TOPIC_BUZZER);
      mqttClient.subscribe(TOPIC_MODE);
      mqttClient.subscribe(TOPIC_RESET);

      statusPublishNeeded = true;

      Serial.println("Da Subscribe cac topic dieu khien");
    }

    else {

      Serial.print("That bai, ma loi: ");
      Serial.println(mqttClient.state());

      vTaskDelay(pdMS_TO_TICKS(2000));
    }
  }
}

// =====================================================
// 13. TASK 1 - DOC DU LIEU MPU6050
// =====================================================

void TaskReadData(void *pvParameters) {

  SensorData data = {};

  sensors_event_t a, g, temp;

  Serial.println("TaskReadData da khoi dong");

  for (;;) {

    // Doc du lieu MPU6050
    mpu.getEvent(&a, &g, &temp);

    // Chuyen gia toc sang don vi g
    data.ax = a.acceleration.x / 9.80665;
    data.ay = a.acceleration.y / 9.80665;
    data.az = a.acceleration.z / 9.80665;

    // Tinh do lon gia toc
    data.acc = sqrt(
      data.ax * data.ax +
      data.ay * data.ay +
      data.az * data.az
    );

    // Tinh Roll va Pitch
    data.roll = atan2(data.ay, data.az) * 180.0 / PI;

    data.pitch = atan2(
      -data.ax,
      sqrt(data.ay * data.ay + data.az * data.az)
    ) * 180.0 / PI;

    unsigned long now = millis();

    bool abnormalAngle =
      (fabs(data.roll) > ANGLE_THRESHOLD) ||
      (fabs(data.pitch) > ANGLE_THRESHOLD);

    // -----------------------------------------------
    // XU LY YEU CAU RESET
    // -----------------------------------------------

    if (resetFallRequested) {

      fallLatched = false;
      impactDetected = false;
      postureConfirmed = false;

      impactTime = 0;
      postureTime = 0;

      resetFallRequested = false;

      data.fall = false;
    }

    // -----------------------------------------------
    // PHAT HIEN TE NGA
    // -----------------------------------------------

    if (!fallLatched) {

      // Phat hien va cham
      if (data.acc >= IMPACT_THRESHOLD) {

        impactDetected = true;
        impactTime = now;

        postureConfirmed = false;
        postureTime = 0;
      }

      // Sau va cham, kiem tra goc nghieng
      if (impactDetected) {

        if (now - impactTime <= FALL_WINDOW_MS) {

          if (abnormalAngle) {

            if (!postureConfirmed) {

              postureConfirmed = true;
              postureTime = now;
            }

            // Goc nghieng duoc duy tri lien tuc
            if (now - postureTime >= POSTURE_HOLD_MS) {

              if (
                lastFallTime == 0 ||
                now - lastFallTime >= FALL_COOLDOWN_MS
              ) {

                fallLatched = true;
                fallDetected = true;

                lastFallTime = now;

                AlertEvent event;
                event.fall = true;

                xQueueSend(alertQueue, &event, 0);

                Serial.println();
                Serial.println("!!! PHAT HIEN TE NGA !!!");
              }
            }
          }

          else {

            postureConfirmed = false;
            postureTime = 0;
          }
        }

        else {

          // Het thoi gian cho xac nhan
          impactDetected = false;
          postureConfirmed = false;

          impactTime = 0;
          postureTime = 0;
        }
      }
    }

    data.fall = fallDetected;

    // Gui du lieu vao Queue
    xQueueOverwrite(dataQueue, &data);

    // In Serial moi 1 giay
    if (now - lastSerialTime >= 1000) {

      lastSerialTime = now;

      Serial.println("----------------------------");

      Serial.print("Acc: ");
      Serial.print(data.acc, 2);
      Serial.println(" g");

      Serial.print("Roll: ");
      Serial.println(data.roll, 2);

      Serial.print("Pitch: ");
      Serial.println(data.pitch, 2);

      Serial.print("Fall: ");
      Serial.println(data.fall ? "YES" : "NO");
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// =====================================================
// 14. TASK 2 - XU LY CANH BAO LED VA BUZZER
// =====================================================

void TaskAlert(void *pvParameters) {

  Serial.println("TaskAlert da khoi dong");

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  digitalWrite(LED_PIN, LOW);
  noTone(BUZZER_PIN);

  AlertEvent event;
  Command cmd;

  bool autoAlarm = false;

  for (;;) {

    // -----------------------------------------------
    // NHAN SU KIEN TE NGA
    // -----------------------------------------------

    if (xQueueReceive(alertQueue, &event, 0) == pdTRUE) {

      if (event.fall) {

        autoAlarm = true;

        // Mac dinh bat LED va buzzer
        ledOverrideActive = false;
        buzzerOverrideActive = false;

        Serial.println("CANH BAO: BAT LED VA BUZZER");
      }
    }

    // -----------------------------------------------
    // NHAN LENH DIEU KHIEN TU MQTT
    // -----------------------------------------------

    while (xQueueReceive(commandQueue, &cmd, 0) == pdTRUE) {

      switch (cmd.type) {

        case CMD_LED:

          manualLED = cmd.value;
          ledOverrideActive = true;

          Serial.print("Lenh LED: ");
          Serial.println(manualLED ? "ON" : "OFF");

          break;

        case CMD_BUZZER:

          manualBuzzer = cmd.value;
          buzzerOverrideActive = true;

          Serial.print("Lenh BUZZER: ");
          Serial.println(manualBuzzer ? "ON" : "OFF");

          break;

        case CMD_MANUAL:

          manualMode = true;

          Serial.println("CHE DO MANUAL");

          break;

        case CMD_AUTO:

          manualMode = false;

          Serial.println("CHE DO AUTO");

          break;

        case CMD_RESET:

          autoAlarm = false;

          fallDetected = false;
          resetFallRequested = true;

          ledOverrideActive = false;
          buzzerOverrideActive = false;

          manualLED = false;
          manualBuzzer = false;

          Serial.println("DA RESET CANH BAO");

          break;
      }
    }

    // -----------------------------------------------
    // XAC DINH TRANG THAI LED VA BUZZER
    // -----------------------------------------------

    bool ledOutput = false;
    bool buzzerOutput = false;

    if (autoAlarm) {

      // Khi te nga: bat mac dinh ca hai
      // Cho phep tat rieng tung thiet bi

      ledOutput = ledOverrideActive ? manualLED : true;

      buzzerOutput =
        buzzerOverrideActive ? manualBuzzer : true;
    }

    else if (manualMode) {

      ledOutput = manualLED;
      buzzerOutput = manualBuzzer;
    }

    else {

      ledOutput = false;
      buzzerOutput = false;
    }

    // -----------------------------------------------
    // DIEU KHIEN LED GPIO2
    // -----------------------------------------------

    digitalWrite(
      LED_PIN,
      ledOutput ? HIGH : LOW
    );

    // -----------------------------------------------
    // DIEU KHIEN BUZZER GPIO25
    // -----------------------------------------------

    if (buzzerOutput) {

      tone(BUZZER_PIN, 2000);
    }

    else {

      noTone(BUZZER_PIN);
    }

    // Cap nhat trang thai thuc te
    actualLEDState = ledOutput;
    actualBuzzerState = buzzerOutput;

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// =====================================================
// 15. TASK 3 - GUI DU LIEU LEN MQTT
// =====================================================

void TaskTransmitData(void *pvParameters) {

  Serial.println("TaskTransmitData da khoi dong");

  SensorData data;

  for (;;) {

    // Ket noi WiFi neu mat ket noi
    if (WiFi.status() != WL_CONNECTED) {

      connectWiFi();
    }

    // Ket noi MQTT
    if (!mqttClient.connected()) {

      reconnectMQTT();
    }

    // Xu ly MQTT callback
    mqttClient.loop();

    unsigned long now = millis();

    // -----------------------------------------------
    // GUI TRANG THAI LED VA BUZZER
    // -----------------------------------------------

    bool currentLED = actualLEDState;
    bool currentBuzzer = actualBuzzerState;

    if (mqttClient.connected()) {

      if (statusPublishNeeded) {

        mqttClient.publish(
          TOPIC_LED_STATUS,
          currentLED ? "ON" : "OFF",
          true
        );

        mqttClient.publish(
          TOPIC_BUZZER_STATUS,
          currentBuzzer ? "ON" : "OFF",
          true
        );

        lastPublishedLEDState = currentLED;
        lastPublishedBuzzerState = currentBuzzer;

        statusPublishNeeded = false;
      }

      else {

        if (currentLED != lastPublishedLEDState) {

          mqttClient.publish(
            TOPIC_LED_STATUS,
            currentLED ? "ON" : "OFF",
            true
          );

          lastPublishedLEDState = currentLED;
        }

        if (currentBuzzer != lastPublishedBuzzerState) {

          mqttClient.publish(
            TOPIC_BUZZER_STATUS,
            currentBuzzer ? "ON" : "OFF",
            true
          );

          lastPublishedBuzzerState = currentBuzzer;
        }
      }
    }

    // -----------------------------------------------
    // GUI TELEMETRY MOI 500ms
    // -----------------------------------------------

    if (now - lastPublishTime >= 500) {

      lastPublishTime = now;

      if (xQueuePeek(dataQueue, &data, 0) == pdTRUE) {

        char jsonData[256];

        snprintf(
          jsonData,
          sizeof(jsonData),
          "{\"ax\":%.2f,\"ay\":%.2f,\"az\":%.2f,"
          "\"acc\":%.2f,\"roll\":%.2f,\"pitch\":%.2f,"
          "\"fall\":%s,\"led\":%s,\"buzzer\":%s}",
          data.ax,
          data.ay,
          data.az,
          data.acc,
          data.roll,
          data.pitch,
          data.fall ? "true" : "false",
          currentLED ? "true" : "false",
          currentBuzzer ? "true" : "false"
        );

        if (mqttClient.connected()) {

          mqttClient.publish(TOPIC_DATA, jsonData);

          Serial.print("MQTT DATA: ");
          Serial.println(jsonData);

          // Gui trang thai canh bao khi thay doi
          if (data.fall != lastPublishedFall) {

            mqttClient.publish(
              TOPIC_ALERT,
              data.fall ? "FALL_DETECTED" : "NORMAL",
              true
            );

            lastPublishedFall = data.fall;
          }
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// =====================================================
// 16. SETUP
// =====================================================

void setup() {

  Serial.begin(115200);

  Serial.println();
  Serial.println("================================");
  Serial.println("   ESP32 FALL DETECTION SYSTEM");
  Serial.println("================================");

  // Khoi tao GPIO
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  digitalWrite(LED_PIN, LOW);
  noTone(BUZZER_PIN);

  // Khoi tao I2C
  Wire.begin(SDA_PIN, SCL_PIN);

  // Khoi tao MPU6050
  if (!mpu.begin()) {

    Serial.println("LOI: KHONG TIM THAY MPU6050!");

    while (1) {
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }

  Serial.println("MPU6050 da san sang!");

  // Cau hinh MPU6050
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  // Cau hinh MQTT
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(512);
  mqttClient.setKeepAlive(30);

  // Khoi tao Queue
  dataQueue = xQueueCreate(1, sizeof(SensorData));

  alertQueue = xQueueCreate(5, sizeof(AlertEvent));

  commandQueue = xQueueCreate(10, sizeof(Command));

  if (
    dataQueue == NULL ||
    alertQueue == NULL ||
    commandQueue == NULL
  ) {

    Serial.println("LOI KHOI TAO QUEUE!");

    while (1) {
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }

  // Khoi tao WiFi
  connectWiFi();

  // Tao du lieu ban dau
  SensorData initialData = {};
  xQueueOverwrite(dataQueue, &initialData);

  // -----------------------------------------------
  // TAO 3 TASK FREERTOS
  // -----------------------------------------------

  xTaskCreatePinnedToCore(
    TaskReadData,
    "TaskReadData",
    4096,
    NULL,
    2,
    NULL,
    1
  );

  xTaskCreatePinnedToCore(
    TaskAlert,
    "TaskAlert",
    4096,
    NULL,
    2,
    NULL,
    1
  );

  xTaskCreatePinnedToCore(
    TaskTransmitData,
    "TaskTransmitData",
    8192,
    NULL,
    1,
    NULL,
    0
  );

  Serial.println("DA KHOI DONG 3 TASK FREERTOS");
}

// =====================================================
// 17. LOOP
// =====================================================

void loop() {

  // Cac cong viec duoc xu ly boi 3 FreeRTOS Task
  vTaskDelay(pdMS_TO_TICKS(1000));
}
