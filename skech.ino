#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <MHZ19.h>
#include <SoftwareSerial.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>

// ----- НАСТРОЙКИ OLED ДИСПЛЕЯ -----
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ----- НАСТРОЙКИ ДАТЧИКА MH-Z19C -----
#define MHZ19_RX_PIN D6
#define MHZ19_TX_PIN D7
SoftwareSerial mySerial(MHZ19_RX_PIN, MHZ19_TX_PIN);
MHZ19 myMHZ19;

// ----- НАСТРОЙКИ RGB СВЕТОДИОДА -----
#define RED_PIN D2      // GPIO4 - красный
#define GREEN_PIN D5    // GPIO14 - зеленый
#define BLUE_PIN D8     // GPIO15 - синий

// ===== НАСТРОЙКИ WI-FI (НЕСКОЛЬКО СЕТЕЙ) =====
struct WiFiNetwork {
  const char* ssid;
  const char* password;
};

// Список доступных WiFi сетей
WiFiNetwork networks[] = {
  {"ssid1", "passwd1"},
  {"ssid2", "passwd2"},
  {"ssid3", "passwd3"},
  {"ssid4", "passwd4"}
};

const int networkCount = sizeof(networks) / sizeof(networks[0]);
int currentNetworkIndex = 0;
String connectedSSID = "";

// ----- НАСТРОЙКИ TELEGRAM -----
const char* BOT_TOKEN = "BOT_TOKEN_FROM_BOT_FATHER"; //Токен вота от имени которого будут отправляться сообщения
const char* CHAT_ID = "TELEGRAM_USER_ID"; //ID пользователя в чат которого будут отправляться сообщения
const unsigned long SEND_INTERVAL = 3000000;

// ----- ПЕРЕМЕННЫЕ ДЛЯ RGB -----
enum LEDMode {
  NORMAL_RAINBOW,
  YELLOW_BLINK,
  RED_BLINK
};

LEDMode currentMode = NORMAL_RAINBOW;
LEDMode lastMode = NORMAL_RAINBOW;

unsigned long lastLEDUpdate = 0;
const int LED_UPDATE_INTERVAL = 30;

bool blinkState = false;
unsigned long lastBlinkTime = 0;
const int YELLOW_BLINK_INTERVAL = 500;
const int RED_BLINK_INTERVAL = 300;

float hue = 0.0;
float pulseBrightness = 0.0;
int pulseDirection = 1;
unsigned long lastPulseUpdate = 0;
const int PULSE_SPEED = 8;

// ----- ОБЩИЕ ПЕРЕМЕННЫЕ -----
unsigned long startTime = 0;
unsigned long lastSendTime = 0;
bool wifiConnected = false;
unsigned long lastReconnectAttempt = 0;
const unsigned long RECONNECT_INTERVAL = 60000; // Попытка переподключения каждую минуту

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("================================");
  Serial.println("CO2 МОНИТОР С OLED ДИСПЛЕЕМ");
  Serial.println("================================");

  // Настройка RGB пинов
  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);

  // Гасим светодиод
  setRGBColor(0, 0, 0, 1, 10, 10);

  // Инициализация OLED дисплея
  Serial.println("Инициализация OLED дисплея...");
  Wire.begin(D14, D15);

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("Ошибка инициализации SSD1306!");
    for(;;);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("CO2 Monitor");
  display.setCursor(0, 16);
  display.println("OLED Ready");
  display.display();

  Serial.println("OLED дисплей OK");
  delay(2000);

  // Инициализация датчика
  Serial.println("Инициализация датчика...");
  mySerial.begin(9600);
  myMHZ19.begin(mySerial);
  myMHZ19.autoCalibration(false);

  Serial.println("Датчик инициализирован");

  // Подключение к WiFi (перебираем все сети)
  connectToWiFi();

  // Если WiFi не подключился, показываем сообщение
  if (!wifiConnected) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("No WiFi!");
    display.setCursor(0, 16);
    display.println("Check networks");
    display.display();
    delay(3000);
  }

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Warming up...");
  display.setCursor(0, 16);
  display.println("3 min");
  display.display();

  startTime = millis();
}

// ===== НОВАЯ ФУНКЦИЯ ДЛЯ ПОДКЛЮЧЕНИЯ К WI-FI =====
void connectToWiFi() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Scanning WiFi...");
  display.display();

  Serial.println("\n=== Поиск доступных WiFi сетей ===");

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  int networksFound = WiFi.scanNetworks();
  Serial.print("Найдено сетей: ");
  Serial.println(networksFound);

  if (networksFound == 0) {
    Serial.println("Сети не найдены!");
    wifiConnected = false;
    return;
  }

  // Сначала показываем найденные сети
  for (int i = 0; i < networksFound; i++) {
    Serial.print(i + 1);
    Serial.print(": ");
    Serial.print(WiFi.SSID(i));
    Serial.print(" (");
    Serial.print(WiFi.RSSI(i));
    Serial.println(" dBm)");
  }

  Serial.println("\n=== Пробуем подключиться к известным сетям ===");

  // Перебираем все наши сохраненные сети
  for (int n = 0; n < networkCount; n++) {
    Serial.print("Пробуем: ");
    Serial.println(networks[n].ssid);

    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Connecting to:");
    display.setCursor(0, 16);
    display.println(networks[n].ssid);
    display.display();

    WiFi.begin(networks[n].ssid, networks[n].password);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      wifiConnected = true;
      connectedSSID = networks[n].ssid;
      currentNetworkIndex = n;

      Serial.println("\n✓ Подключено!");
      Serial.print("SSID: ");
      Serial.println(connectedSSID);
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());

      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("WiFi OK!");
      display.setCursor(0, 16);
      display.println(connectedSSID);
      display.setCursor(0, 32);
      display.println(WiFi.localIP());
      display.display();

      // Отправляем сообщение о подключении
      String msg = "✅ Подключено к WiFi:\n";
      msg += "📶 " + connectedSSID + "\n";
      msg += "🌐 IP: " + WiFi.localIP().toString();
      sendToTelegram(msg);

      delay(3000);
      return;
    }

    Serial.println("\n✗ Не удалось подключиться");
  }

  // Если ни одна сеть не подошла
  wifiConnected = false;
  connectedSSID = "";

  Serial.println("\n=== Не удалось подключиться ни к одной сети ===");

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("WiFi ERROR!");
  display.setCursor(0, 16);
  display.println("No connection");
  display.display();
}

// ===== ФУНКЦИЯ ПЕРИОДИЧЕСКОЙ ПРОВЕРКИ WI-FI =====
void checkWiFiConnection() {
  if (wifiConnected) {
    // Если подключены, проверяем статус
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Потеряно соединение с WiFi!");
      wifiConnected = false;

      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("WiFi Lost!");
      display.display();

      lastReconnectAttempt = millis();
    }
  } else {
    // Если не подключены, пробуем переподключиться каждые RECONNECT_INTERVAL
    if (millis() - lastReconnectAttempt > RECONNECT_INTERVAL) {
      Serial.println("Попытка переподключения к WiFi...");
      connectToWiFi();
      lastReconnectAttempt = millis();
    }
  }
}

void loop() {
  // Проверка WiFi соединения
  checkWiFiConnection();

  // Чтение данных с датчика
  int co2ppm = myMHZ19.getCO2();
  int temperature = myMHZ19.getTemperature();

  // Расчет времени работы
  unsigned long elapsedSeconds = (millis() - startTime) / 1000;
  int minutes = elapsedSeconds / 60;
  int seconds = elapsedSeconds % 60;

  // Вывод в монитор порта
  Serial.print("Время: ");
  Serial.print(minutes);
  Serial.print("м ");
  Serial.print(seconds);
  Serial.print("с | CO2: ");

  if (co2ppm > 0 && co2ppm < 5000) {
    Serial.print(co2ppm);
    Serial.print(" ppm");

    if (co2ppm < 800) Serial.print(" | Хорошее");
    else if (co2ppm < 1200) Serial.print(" | Нормальное");
    else if (co2ppm < 2000) Serial.print(" | Плохое");
    else Serial.print(" | Опасно!");

  } else {
    Serial.print("ОЖИДАНИЕ");
  }

  Serial.print(" | Температура: ");

  if (temperature > 0 && temperature < 60) {
    Serial.print(temperature);
    Serial.println(" C");
  } else {
    Serial.println("--- C");
  }

  // Определяем режим RGB
  if (co2ppm > 0 && co2ppm < 5000) {
    if (co2ppm >= 1300) {
      currentMode = RED_BLINK;
    } else if (co2ppm >= 1200) {
      currentMode = YELLOW_BLINK;
    } else {
      currentMode = NORMAL_RAINBOW;
    }
  }

  if (currentMode != lastMode) {
    pulseBrightness = 50;
    pulseDirection = 1;
    lastMode = currentMode;
  }

  updateLED();

  // Вывод на OLED дисплей
  display.clearDisplay();

  if (co2ppm > 0 && co2ppm < 5000) {
    // Первая строка - CO2 крупно
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.print("CO2:");
    display.setCursor(50, 0);
    display.print(co2ppm);

    // Вторая строка - качество и WiFi статус
    display.setTextSize(1);
    display.setCursor(0, 20);
    display.print("Q: ");
    if (co2ppm < 800) display.print("Good");
    else if (co2ppm < 1200) display.print("Normal");
    else if (co2ppm < 2000) display.print("Poor");
    else display.print("BAD!");

    // Индикатор WiFi в верхнем углу
    display.setCursor(100, 0);
    if (wifiConnected) {
      display.print("WiFi");
    } else {
      display.print("NoWF");
    }

    // Третья строка - температура
    display.setCursor(0, 35);
    display.print("Temp: ");
    display.print(temperature);
    display.print(" C");

    // Четвертая строка - режим RGB и SSID
    display.setCursor(0, 50);

    // Режим RGB
    if (currentMode == NORMAL_RAINBOW) display.print("G");
    else if (currentMode == YELLOW_BLINK) display.print("N");
    else display.print("B");

    // SSID сети (если подключены)
    if (wifiConnected && connectedSSID.length() > 0) {
      display.setCursor(20, 50);
      // Обрезаем SSID если слишком длинный
      String shortSSID = connectedSSID.substring(0, 8);
      display.print(shortSSID);
    }

  } else {
    // Режим прогрева
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.print("Warm up");

    display.setTextSize(1);
    display.setCursor(0, 25);
    display.print("Time: ");
    display.print(minutes);
    display.print("m ");
    display.print(seconds);
    display.print("s");

    if (wifiConnected && millis() - lastSendTime >= SEND_INTERVAL) {
      lastSendTime = millis();
    }
  }

  display.display();

  // Отправка в Telegram
  if (wifiConnected && millis() - lastSendTime >= SEND_INTERVAL && co2ppm > 0) {
    sendCO2ToTelegram(co2ppm, temperature);
    lastSendTime = millis();
  }

  Serial.println("----------------------------");
  delay(2000);
}

// ===== ФУНКЦИИ ДЛЯ RGB =====
void setRGBColor(int red, int green, int blue, int stepR, int stepG, int stepB) {
  analogWrite(RED_PIN, 255 - red);
  analogWrite(GREEN_PIN, 255 - green);
  analogWrite(BLUE_PIN, 255 - blue);
  Serial.print("!!! Цвет: ");
  Serial.print(red);
  Serial.print(" ");
  Serial.print(green);
  Serial.print(" ");
  Serial.print(blue);
  Serial.print(" SR:");
  Serial.print(stepR);
  Serial.print(" SG:");
  Serial.print(stepG);
  Serial.print(" SB:");
  Serial.print(stepB);
  Serial.println(" !!!");
}

// Глобальные переменные для RGB
int currentR = 0, currentG = 0, currentB = 0;
int targetMinR, targetMinG, targetMinB;
int targetMax, targetMaxR, targetMaxG, targetMaxB;
int stepR, stepG, stepB;
int phase = 0;
unsigned long nextColorTime = 0;

void updateLED() {
  unsigned long currentMillis = millis();

  switch (currentMode) {
    case NORMAL_RAINBOW:
      targetMax = 150;
      stepR = random(0, 150);
      stepG = random(0, 150);
      stepB = random(0, 150);
      setRGBColor(stepR, stepG, stepB, stepR, stepG, stepB);

      break;

    case YELLOW_BLINK:
      if (currentMillis - lastBlinkTime >= YELLOW_BLINK_INTERVAL) {
        blinkState = !blinkState;
        if (blinkState) {
          setRGBColor(255, 60, 0, stepR, stepG, stepB);
          Serial.println("Желтый ВКЛ");
        } else {
          setRGBColor(0, 0, 0, 1, 10, 10);
          Serial.println("Желтый ВЫКЛ");
        }
        lastBlinkTime = currentMillis;
      }
      break;

    case RED_BLINK:
      if (currentMillis - lastBlinkTime >= RED_BLINK_INTERVAL) {
        blinkState = !blinkState;
        if (blinkState) {
          setRGBColor(255, 0, 0, stepR, stepG, stepB);
          Serial.println("Красный ВКЛ");
        } else {
          setRGBColor(0, 0, 0, 1, 10, 10);
          Serial.println("Красный ВЫКЛ");
        }
        lastBlinkTime = currentMillis;
      }
      break;
  }
}

// ===== ФУНКЦИИ ДЛЯ TELEGRAM =====
void sendToTelegram(String message) {
  if (!wifiConnected || WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi не подключен");
    return;
  }

  std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
  client->setInsecure();
  HTTPClient https;

  String url = "https://api.telegram.org/bot" + String(BOT_TOKEN) + "/sendMessage";

  if (https.begin(*client, url)) {
    https.addHeader("Content-Type", "application/json");

    String payload = "{\"chat_id\":\"" + String(CHAT_ID) + "\", \"text\":\"" + message + "\", \"parse_mode\":\"HTML\"}";
    int httpCode = https.POST(payload.c_str());

    if (httpCode > 0) {
      Serial.println("Сообщение отправлено в Telegram");
    } else {
      Serial.printf("Ошибка отправки: %s\n", https.errorToString(httpCode).c_str());
    }
    https.end();
  }
}

void sendCO2ToTelegram(int co2, int temp) {
  String quality;
  String emoji;

  if (co2 < 800) {
    quality = "Хорошее";
    emoji = "✅";
  } else if (co2 < 1200) {
    quality = "Нормальное";
    emoji = "⚠️";
  } else if (co2 < 2000) {
    quality = "Плохое";
    emoji = "⚠️⚠️";
  } else {
    quality = "ОПАСНОЕ!";
    emoji = "❌❌❌";
  }

  String message = "📊 <b>Показания CO2</b>\n\n";
  message += "• CO₂: <b>" + String(co2) + " ppm</b>\n";
  message += "• Температура: <b>" + String(temp) + "°C</b>\n";
  message += "• Качество: " + emoji + " " + quality + "\n";

  message += "• Индикация: ";
  if (co2 < 1000) message += "🟢 Радуга";
  else if (co2 < 1300) message += "🟡 Желтая пульсация";
  else message += "🔴 Красная пульсация";

  // Добавляем информацию о WiFi сети
  if (wifiConnected) {
    message += "\n• 📶 " + connectedSSID;
  }

  sendToTelegram(message);
}