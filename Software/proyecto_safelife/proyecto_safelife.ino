#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Adafruit_MPU6050.h>
#include "MAX30105.h"
#include "heartRate.h"
#include "spo2_algorithm.h"

const char* WIFI_SSID     = "xxxxxxxxxxxx";
const char* WIFI_PASSWORD = "xxxxxxxxxxx";

const char* MQTT_HOST     = "broker.hivemq.com";
const int   MQTT_PORT     = 1883; 
const char* CLIENT_ID     = "esp32-safelife-completo";

const char* TOPIC_TEMP        = "safe-life/demo/temperatura";
const char* TOPIC_MOVIMIENTO  = "safe-life/demo/movimiento";
const char* TOPIC_ALERTA      = "safe-life/demo/alerta";
const char* TOPIC_INACTIVIDAD = "safe-life/demo/inactividad";
const char* TOPIC_PULSO       = "safe-life/demo/pulso";
const char* TOPIC_SPO2        = "safe-life/demo/oxigeno";
const char* TOPIC_SUB         = "safe-life/cliente/mensaje";

const unsigned long PUBLISH_INTERVAL = 5000; 

const float TEMPERATURA_MAXIMA     = 34.0;  

const float UMBRAL_MOVIMIENTO_REAL = 1.0;    
const unsigned long TIEMPO_INACTIVIDAD_MAX = 30000;

const int BPM_MINIMO = 50;   
const int BPM_MAXIMO = 110;  
const int SPO2_MINIMO = 92;  
const long UMBRAL_DEDO = 50000; 

const int I2C_SDA = 21;
const int I2C_SCL = 22;

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
Adafruit_BME280 bme; 
Adafruit_MPU6050 mpu;
MAX30105 particleSensor;

unsigned long lastPublish = 0;

bool alertaTemperaturaEnviada = false;
bool alertaInactividadEnviada = false;
bool alertaPulsoEnviada = false;
bool alertaSpO2Enviada = false;

float ultimoX = 0;
float ultimoY = 0;
unsigned long ultimaVezEnMovimiento = 0;

const byte RATE_SIZE = 4; 
byte rates[RATE_SIZE];
byte rateSpot = 0;
long lastBeat = 0;
float beatsPerMinute = 0;
int beatAvg = 0;

uint32_t irBuffer[100];  
uint32_t redBuffer[100]; 
int32_t bufferIndex = 0;
int32_t spo2 = 0;
int8_t validSPO2 = 0;     
int32_t heartRate = 0;
int8_t validHeartRate = 0;

void onMessage(char* topic, byte* payload, unsigned int length) {
  Serial.print("[RECIBIDO] Topic: ");
  Serial.print(topic);
  Serial.print(" | Mensaje: ");
  for (unsigned int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

void connectWiFi() {
  Serial.print("Conectando a WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi conectado! IP: " + WiFi.localIP().toString());
}

void connectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("Conectando a Broker MQTT...");
    if (mqttClient.connect(CLIENT_ID)) {
      Serial.println(" conectado!");
      mqttClient.subscribe(TOPIC_SUB);
    } else {
      Serial.print(" Error: rc=");
      Serial.print(mqttClient.state());
      Serial.println(" | Reintentando en 5s...");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  connectWiFi();

  Wire.begin(I2C_SDA, I2C_SCL);

  if (!bme.begin(0x76)) { 
    Serial.println("¡No se pudo encontrar el BME280! Revisa las conexiones.");
    while (1) { delay(10); }
  }
  Serial.println("BME280 encontrado y listo!");

  if (!mpu.begin()) {
    Serial.println("¡No se pudo encontrar el MPU6050! Revisa las conexiones.");
    while (1) { delay(10); }
  }
  Serial.println("MPU6050 encontrado y listo!");
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  ultimaVezEnMovimiento = millis();

  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("¡No se pudo encontrar el MAX30102! Revisa las conexiones.");
    while (1) { delay(10); }
  }
  Serial.println("MAX30102 encontrado y listo!");
  
  byte ledBrightness = 60;  
  byte sampleAverage = 4;   
  byte ledMode = 2;         
  int sampleRate = 100;     
  int pulseWidth = 411;     
  int adcRange = 4096;      
  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);

  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCallback(onMessage);
  mqttClient.setKeepAlive(60);

  connectMQTT();
}

void loop() {
  if (!mqttClient.connected()) {
    connectMQTT();
  }
  mqttClient.loop();

  unsigned long now = millis();

  float temperatura = bme.readTemperature();

  if (temperatura > TEMPERATURA_MAXIMA && !alertaTemperaturaEnviada) {
    alertaTemperaturaEnviada = true;
    String payloadAlerta = "{\"dispositivo\":\"esp32\",\"alerta\":\"TEMPERATURA_ALTA\",\"temperatura_actual\":" + String(temperatura, 2) + "}";
    mqttClient.publish(TOPIC_ALERTA, payloadAlerta.c_str());
    Serial.println("[ALERTA] Temperatura alta: " + payloadAlerta);
  } else if (temperatura <= TEMPERATURA_MAXIMA && alertaTemperaturaEnviada) {
    alertaTemperaturaEnviada = false;
    mqttClient.publish(TOPIC_ALERTA, "{\"estado\":\"normal\",\"mensaje\":\"Temperatura dentro del rango normal\"}");
  }

  sensors_event_t a, g, temp_mpu;
  mpu.getEvent(&a, &g, &temp_mpu);
  float accelX = a.acceleration.x;
  float accelY = a.acceleration.y;

  float deltaX = abs(accelX - ultimoX);
  float deltaY = abs(accelY - ultimoY);

  ultimoX = accelX;
  ultimoY = accelY;

  if (deltaX > UMBRAL_MOVIMIENTO_REAL || deltaY > UMBRAL_MOVIMIENTO_REAL) {
    ultimaVezEnMovimiento = now;

    if (alertaInactividadEnviada) {
      alertaInactividadEnviada = false;
      mqttClient.publish(TOPIC_INACTIVIDAD, "{\"estado\":\"activo\",\"mensaje\":\"Persona en movimiento nuevamente\"}");
      Serial.println("[INFO] Movimiento detectado. Alerta de inactividad resuelta.");
    }
  } else {
    if (!alertaInactividadEnviada && (now - ultimaVezEnMovimiento >= TIEMPO_INACTIVIDAD_MAX)) {
      alertaInactividadEnviada = true;
      String payloadInactividad = "{\"dispositivo\":\"esp32\",\"estado\":\"INACTIVIDAD_PROLONGADA\",\"tiempo_quieto_ms\":" + String(now - ultimaVezEnMovimiento) + "}";
      mqttClient.publish(TOPIC_INACTIVIDAD, payloadInactividad.c_str());
      Serial.println("[ALERTA] Inactividad prolongada: " + payloadInactividad);
    }
  }

  long irValue = particleSensor.getIR();

  if (irValue > UMBRAL_DEDO) {
    bufferIndex = 0;
    while (bufferIndex < 100) {
      while (particleSensor.available() == false) {
        particleSensor.check(); 
      }
      redBuffer[bufferIndex] = particleSensor.getRed();
      irBuffer[bufferIndex] = particleSensor.getIR();
      particleSensor.nextSample(); 
      bufferIndex++;
    }

    maxim_heart_rate_and_oxygen_saturation(irBuffer, 100, redBuffer, &spo2, &validSPO2, &heartRate, &validHeartRate);

    if (checkForBeat(irValue) == true) {
      long delta = millis() - lastBeat;
      lastBeat = millis();
      beatsPerMinute = 60 / (delta / 1000.0);

      if (beatsPerMinute < 255 && beatsPerMinute > 20) {
        rates[rateSpot++] = (byte)beatsPerMinute;
        rateSpot %= RATE_SIZE;

        beatAvg = 0;
        for (byte x = 0; x < RATE_SIZE; x++) {
          beatAvg += rates[x];
        }
        beatAvg /= RATE_SIZE;
      }
    }

    int bpmFinal = (validHeartRate == 1 && heartRate > 20 && heartRate < 220) ? heartRate : beatAvg;

    if (bpmFinal > 0) {
      if ((bpmFinal < BPM_MINIMO || bpmFinal > BPM_MAXIMO) && !alertaPulsoEnviada) {
        alertaPulsoEnviada = true;
        String tipoAlerta = (bpmFinal > BPM_MAXIMO) ? "PULSO_ALTO" : "PULSO_BAJO";
        String payloadAlerta = "{\"dispositivo\":\"esp32\",\"alerta\":\"" + tipoAlerta + "\",\"bpm\":" + String(bpmFinal) + "}";
        mqttClient.publish(TOPIC_ALERTA, payloadAlerta.c_str());
      } else if (bpmFinal >= BPM_MINIMO && bpmFinal <= BPM_MAXIMO && alertaPulsoEnviada) {
        alertaPulsoEnviada = false;
      }
    }

    if (validSPO2 == 1 && spo2 > 50 && spo2 <= 100) {
      if (spo2 < SPO2_MINIMO && !alertaSpO2Enviada) {
        alertaSpO2Enviada = true;
        String payloadAlertaSpO2 = "{\"dispositivo\":\"esp32\",\"alerta\":\"OXIGENO_BAJO\",\"spo2\":" + String(spo2) + "}";
        mqttClient.publish(TOPIC_ALERTA, payloadAlertaSpO2.c_str());
      } else if (spo2 >= SPO2_MINIMO && alertaSpO2Enviada) {
        alertaSpO2Enviada = false;
      }
    }

  } else {
    beatsPerMinute = 0;
    beatAvg = 0;
    spo2 = 0;
    heartRate = 0;
    validHeartRate = 0;
    validSPO2 = 0;
  }
  
    if (now - lastPublish >= PUBLISH_INTERVAL) {
    lastPublish = now;
 
    String payloadTemp = "{\"dispositivo\":\"esp32\",\"temperatura\":" + String(temperatura, 2) + ",\"uptime\":" + String(now / 1000) + "}";
    mqttClient.publish(TOPIC_TEMP, payloadTemp.c_str());
    Serial.println("[PUBLICADO TEMPERATURA] " + payloadTemp);

    String payloadMovimiento = "{\"dispositivo\":\"esp32\",\"accel_x\":" + String(accelX, 2) + ",\"accel_y\":" + String(accelY, 2) + "}";
    mqttClient.publish(TOPIC_MOVIMIENTO, payloadMovimiento.c_str());
    Serial.println("[PUBLICADO MOVIMIENTO] " + payloadMovimiento);

    String estadoDedo = (irValue > UMBRAL_DEDO) ? "detectado" : "sin_dedo";
    int bpmFinalMostrar = (validHeartRate == 1 && heartRate > 20 && heartRate < 220) ? heartRate : beatAvg;
    String payloadPulso = "{\"dispositivo\":\"esp32\",\"dedo\":\"" + estadoDedo + "\",\"bpm\":" + String(bpmFinalMostrar) + "}";
    mqttClient.publish(TOPIC_PULSO, payloadPulso.c_str());
    Serial.println("[PUBLICADO PULSO] " + payloadPulso);

    int spo2Mostrar = (validSPO2 == 1) ? spo2 : 0;
    String payloadSpO2 = "{\"dispositivo\":\"esp32\",\"dedo\":\"" + estadoDedo + "\",\"spo2_porcentaje\":" + String(spo2Mostrar) + "}";
    mqttClient.publish(TOPIC_SPO2, payloadSpO2.c_str());
    Serial.println("[PUBLICADO SPO2] " + payloadSpO2);
  }
}
