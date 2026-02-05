#define BLYNK_TEMPLATE_ID "TMPL6N_NhaIdJ"
#define BLYNK_TEMPLATE_NAME "watering"
#define BLYNK_AUTH_TOKEN "r3XFsboZCozoKIUgsb1IjejmsEtC6_Z_"

#define BLYNK_PRINT Serial

// --- ไลบรารีที่ต้องใช้ทั้งหมด ---
#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include "time.h"

// --- ข้อมูล Wi-Fi ---
char ssid[] = "mark_2.4G";
char pass[] = "phuwadon01";

// --- ตั้งค่าเซ็นเซอร์ DHT ---
#define DHTPIN 25
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// --- ตั้งค่าจอ LCD ---
LiquidCrystal_I2C lcd(0x27, 16, 2);

// --- กำหนดขาที่ต่ออุปกรณ์อื่นๆ ---
const int soilSensorPin = 34;
const int pumpPin = 26;

BlynkTimer timer;

// <<< NEW: Global Sensor Variables ---
// ตัวแปรส่วนกลางสำหรับเก็บค่าเซ็นเซอร์ เพื่อให้ฟังก์ชันอื่นดึงไปใช้ได้
int   globalMoisturePercent = 0;
float globalTemperature     = 0;
float globalHumidity        = 0;
// <<< END NEW ---

// =================== ตัวแปรสำหรับโหมด AUTO ===================
bool isAutoMode = false;
int moistureLowThreshold = 70;
int moistureHighThreshold = 60;
// ===============================================================
bool isWateringNotified = false;
bool isLowMoistureNotified = false;

// --- ตัวแปรสำหรับตั้งเวลา NTP ---
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 7 * 3600; // GMT+7 (ประเทศไทย)
const int   daylightOffset_sec = 0;   // ไม่ใช้ Daylight Saving

bool dailySummarySent = false; // Flag ป้องกันการส่งซ้ำ
int targetHour = 16;  // ชั่วโมง (ตั้งค่าเป็น 16)
int targetMin = 47;   // นาที (ตั้งค่าเป็น 20)  --> เวลารวมคือ 16:20
// --- END ---


BLYNK_WRITE(V5) {
  moistureLowThreshold = param.asInt();
  Serial.printf("New LOW threshold set to: %d\n", moistureLowThreshold);
}

BLYNK_WRITE(V6) {
  moistureHighThreshold = param.asInt();
  Serial.printf("New HIGH threshold set to: %d\n", moistureHighThreshold);
}

BLYNK_CONNECTED() {
  Blynk.syncVirtual(V5, V6);
}

// <<< MODIFIED: แก้ไขฟังก์ชัน sendSensorData ทั้งหมด ---
void sendSensorData() {
  // อ่านค่าจากเซ็นเซอร์
  int moistureValue = analogRead(soilSensorPin);
  int moisturePercent = map(moistureValue, 4095, 0, 0, 100);
  
  // 1. อัปเดตค่าความชื้นลงตัวแปร global ทันที
  globalMoisturePercent = moisturePercent;

  // =================== แจ้งเตือนความชื้นต่ำ < 65% ===================
  if (moisturePercent < 65) {
    if (!isLowMoistureNotified) { 
      Blynk.logEvent("low_moisture_warning"); 
      isLowMoistureNotified = true; 
    }
  } else if (moisturePercent > 68) { 
    isLowMoistureNotified = false;
  }
  // =====================================================================

  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();

  // 2. ตรวจสอบ DHT
  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("Failed to read from DHT sensor!");
    Blynk.logEvent("sensor_error"); 
    // ถ้า DHT พัง, จะไม่ return แต่จะใช้ค่า globalTemperature/Humidity เดิม (หรือ 0)
  } else {
    // ถ้า DHT อ่านได้, ให้อัปเดตค่า global
    globalTemperature = temperature;
    globalHumidity = humidity;
  }

  // 3. ส่งข้อมูลไปที่ Blynk (ใช้ตัวแปร global)
  // แก้ปัญหา: ถ้า DHT พัง V0 ก็ยังถูกส่งไปที่เซิร์ฟเวอร์
  Blynk.virtualWrite(V0, globalMoisturePercent);
  Blynk.virtualWrite(V2, globalTemperature);
  Blynk.virtualWrite(V3, globalHumidity);

  // --- แสดงผลบน Serial Monitor (ใช้ตัวแปร global) ---
  Serial.printf("Mode: %s | Moisture: %d%%, Temp: %.1f°C, Humidity: %.1f%%\n",
                (isAutoMode ? "AUTO" : "MANUAL"), globalMoisturePercent, globalTemperature, globalHumidity);

  // --- แสดงผลบนจอ LCD (ใช้ตัวแปร global) ---
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.printf("M:%d%% T:%.1fC", globalMoisturePercent, globalTemperature);
  lcd.setCursor(0, 1);
  lcd.printf("H:%.1f%% %s", globalHumidity, (isAutoMode ? "AUTO" : "MAN")); 

  // =================== ตรรกะการทำงานของโหมด AUTO ===================
  if (isAutoMode) {
    // (ส่วนนี้ใช้ค่า local moisturePercent ซึ่งถูกต้องแล้ว)
    if (moisturePercent < moistureLowThreshold) {
      digitalWrite(pumpPin, HIGH); 
      Blynk.virtualWrite(V1, 1);
      if (!isWateringNotified) {
        Blynk.logEvent("watering_started"); 
        isWateringNotified = true;
      }   
    } else if (moisturePercent > moistureHighThreshold) {
      digitalWrite(pumpPin, LOW);  
      Blynk.virtualWrite(V1, 0);   
      
      if (isWateringNotified) { 
        Blynk.logEvent("watering_stopped"); 
        isWateringNotified = false; 
      }
    }
  } else {
    isWateringNotified = false; 
  }
}
// <<< END MODIFIED FUNCTION ---

// =================== ฟังก์ชันสำหรับสลับโหมด ===================
BLYNK_WRITE(V4) {
  int state = param.asInt();
  isAutoMode = (state == 1); 
}
// ===============================================================

// ฟังก์ชันสำหรับรับคำสั่งเปิด/ปิดปั๊มจากแอป
BLYNK_WRITE(V1) {
  if (!isAutoMode) {
    int pumpState = param.asInt();
    digitalWrite(pumpPin, pumpState);
  }
}

// <<< MODIFIED: ฟังก์ชันเช็คเวลาและส่งสรุปรายวัน ---
void checkDailySummary() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to get time for summary check");
    return;
  }

  // 1. เช็คว่าถึงเวลาที่กำหนดหรือไม่ (ตามตัวแปร targetHour, targetMin)
  if (timeinfo.tm_hour == targetHour && timeinfo.tm_min == targetMin) {
    
    // 2. เช็คว่า "ยังไม่ได้ส่ง" ของวันนี้
    if (!dailySummarySent) {
      Serial.printf("+++ Sending Daily Summary (Time: %02d:%02d) +++\n", timeinfo.tm_hour, timeinfo.tm_min);
      
      // --- 3. สร้างข้อความบน ESP32 ---
      char msg[128]; // สร้างที่เก็บข้อความ
      
      // ประกอบร่างข้อความโดยใช้ตัวแปร global ที่อัปเดตล่าสุด
      snprintf(msg, 128, "ความชื้นดิน %d%%, อุณหภูมิ %.1f°C, ความชื้นอากาศ %.1f%%",
               globalMoisturePercent, globalTemperature, globalHumidity);

      // 4. สั่งยิง Event "daily_summary" พร้อม "ข้อความ" ที่เราสร้าง
      // Blynk จะใช้ข้อความนี้แทนสิ่งที่อยู่ในช่อง DESCRIPTION
      Blynk.logEvent("daily_summary", msg); 
      
      // 5. ตั้งธงว่า "ส่งแล้ว"
      dailySummarySent = true; 
    }
  } 
  // 6. รีเซ็ตธง เมื่อพ้นเวลานั้นไปแล้ว
  else {
    if (dailySummarySent) {
      dailySummarySent = false;
    }
  }
}
// <<< END MODIFIED FUNCTION ---

void setup() {
  Serial.begin(115200);
  pinMode(pumpPin, OUTPUT);
  digitalWrite(pumpPin, LOW);

  dht.begin();
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("System Start!");

  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

  // --- ตั้งค่าและซิงค์เวลา NTP ---
  Serial.println("Connecting to NTP server...");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
  } else {
    Serial.println("Time synchronized");
    Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S"); // พิมพ์เวลาปัจจุบัน
  }
  // --- END ---

  timer.setInterval(2000L, sendSensorData);
}

void loop() {
  Blynk.run();
  timer.run();

  // --- เรียกใช้ฟังก์ชันเช็คเวลาทุกรอบ ---
  checkDailySummary();
  // --- END ---
}