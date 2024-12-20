#include <RTClib.h>
#include <WiFi.h>
#include <FirebaseClient.h>
#include <WiFiClientSecure.h>

#define BUTTON_PIN 15
#define LED_STATE_PIN 5
#define LED_OUTPUT1_PIN 18
#define LED_OUTPUT2_PIN 19 

#define DEBOUNCE_TIME 50          // Thời gian chống dội (rung khi nhấn phím)
#define LONG_TIME 5000            // Thời gian nhấn giữ (ms)
#define PRESS_TIME_THRESHOLD 1000 // Ngưỡng thời gian nhấn nhả và nhấn giữ

#define FIREBASE_AUTH "RUZSEndTdBxeVHA8mm5Xy5Rb1mTQJ1AimWpiQ6t5"
#define FIREBASE_URL "https://codeexam24h-default-rtdb.firebaseio.com/"
#define WIFI_SSID "KuTin"
#define WIFI_PWD "Nhathung0933713845"

RTC_DS3231 rtc;
WiFiClientSecure ssl;
DefaultNetwork network;
AsyncClientClass client(ssl, getNetwork(network));

FirebaseApp app;
RealtimeDatabase Database;
AsyncResult result;
LegacyToken dbSecret(FIREBASE_AUTH);

hw_timer_t* timer = NULL; 
// portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED; 

bool configState = false;     // false: Device Config | true: WiFi Config
bool modeState = false;       // false: OFF Manual Mode (Automations) | true: ON Manual Mode
bool output1State = false;    // false: OFF Output_1 | true: ON Output_1
bool output2State = false;    // false: OFF Output_2 | true: ON Output_2

bool isPressed = false;
bool isHolding = false;
unsigned long currentPressTime = 0;
unsigned long lastPressTime = 0; 

uint8_t lastButtonState = 0;
uint8_t pressCounter = 0;

uint8_t timeON = 0;

void IRAM_ATTR onTimer()
{   
  //...Into mode ISR 
  // portENTER_CRITICAL_ISR(&timerMux);

  static uint8_t sec = 0;

  if (timeON == 1)
  {
    sec++;
    Serial.print("Thời gian ON: "); Serial.println(sec);

    if (sec == 45)
    {
      digitalWrite(LED_OUTPUT1_PIN, LOW);

      // Reset giá trị
      timeON = 0;
      sec = 0;
    }
  }
  
  //...Exit mode ISR 
  // portEXIT_CRITICAL_ISR(&timerMux); 
}

void setup() 
{
  Serial.begin(115200);

  pinMode(BUTTON_PIN, INPUT);
  pinMode(2, OUTPUT);
  pinMode(LED_STATE_PIN, OUTPUT);
  pinMode(LED_OUTPUT1_PIN, OUTPUT);
  pinMode(LED_OUTPUT2_PIN, OUTPUT);

  timer = timerBegin(1000000); // Khởi tạo Timer với tần số 1MHz (1µs mỗi tick)
  timerAttachInterrupt(timer, onTimer); // Gắn hàm xử lý ngắt
  timerAlarm(timer, 1000000, true, 0); // Báo ngắt mỗi 1s (1000000 ticks), không đặt lại bộ đếm
  timerStart(timer); // Bắt đầu Timer

  if (! rtc.begin())
  {
    Serial.println("Couldn't find RTC");
    Serial.flush();
    while (1) delay(10);
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PWD);
  Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print('.');
    delay(1000);
  }
  Serial.println(WiFi.localIP());

  ssl.setInsecure();

  // Initialize the authentication handler.
  initializeApp(client, app, getAuth(dbSecret));

  // Binding the authentication handler with your Database class object.
  app.getApp<RealtimeDatabase>(Database);

  // Set your database URL
  Database.url(FIREBASE_URL);

  // In sync functions, we have to set the operating result for the client that works with the function.
  client.setAsyncResult(result);
}

void loop()
{
  uint8_t buttonState = digitalRead(BUTTON_PIN); // Đọc trạng thái nút nhấn

  // Kiểm tra có nếu nhấn nút
  if (buttonState == HIGH && lastButtonState == LOW)
  {
    currentPressTime = millis();
    if (currentPressTime - lastPressTime > DEBOUNCE_TIME) // Chống rung phím
    {
      isPressed = !isPressed;
      lastPressTime = currentPressTime;
    }
  }

  if (isPressed && buttonState == LOW && millis() - lastPressTime <= PRESS_TIME_THRESHOLD)
  {
    pressCounter++;
    Serial.println("   _"); Serial.println("__| |__");

    // Lưu giá trị
    lastPressTime = millis();
    isPressed = !isPressed;
  }
  else if (isPressed && buttonState == HIGH && millis() - lastPressTime > PRESS_TIME_THRESHOLD)
  {
    currentPressTime = millis();
    Serial.print("Time counter: "); Serial.print(currentPressTime - lastPressTime); Serial.println("ms");

    if (currentPressTime - lastPressTime >= LONG_TIME)
    {
      isHolding = !isHolding;

      // Lưu giá trị
      lastPressTime = currentPressTime;
      isPressed = !isPressed;
    }
  }
  else if (isPressed && buttonState == LOW && millis() - lastPressTime > PRESS_TIME_THRESHOLD)
  {
    // Lưu giá trị
    lastPressTime = currentPressTime;
    isPressed = !isPressed;
  }

  if (isHolding && buttonState == LOW)
  {
    // Reset số lần nhấn
    pressCounter = 0;

    configState =! configState;
    if (configState)
    {
      Serial.println("WiFi Config");
      digitalWrite(LED_STATE_PIN, HIGH);
    }
    else
    {
      Serial.println("Device Config");
      digitalWrite(LED_STATE_PIN, LOW);
    }

    isHolding = !isHolding;
  }

  if (millis() - lastPressTime > 500 && pressCounter > 0)
  {
    switch(pressCounter)
    {
    case 1:
      output1State = !output1State;
      if (output1State)
      {
        Serial.println("ON Output 1");
        digitalWrite(LED_OUTPUT1_PIN, HIGH);
      }
      else
      {
        Serial.println("OFF Output 1");
        digitalWrite(LED_OUTPUT1_PIN, LOW);
      }
      break;

    case 2:
      output2State = !output2State;
      if (output2State)
      {
        Serial.println("ON Output 2");
        digitalWrite(LED_OUTPUT2_PIN, HIGH);
      }
      else
      {
        Serial.println("OFF Output 2");
        digitalWrite(LED_OUTPUT2_PIN, LOW);
      }
      break;

    case 3:
      modeState = !modeState; 
      Database.set<bool>(client, "/App/control/autoMode", modeState);
      break;
    }

    // Reset số lần nhấn
    pressCounter = 0;
  } 

  if (Database.get<bool>(client, "/App/control/autoMode"))
  {
    DateTime now = rtc.now();
    Serial.print(now.hour(), DEC);
    Serial.print(':');
    Serial.print(now.minute(), DEC);
    Serial.print(':');
    Serial.println(now.second(), DEC);

    uint8_t __minute = now.minute() + 2; // Do là giây lệch với đồng hồ thực tế đến 35s
    if (Database.get<int>(client, "/App/setStartTime/setHour") == now.hour() && Database.get<int>(client, "/App/setStartTime/setMinute") == __minute)
    {
      if (timeON < 45)
      {
        timeON = 1;
        digitalWrite(LED_OUTPUT1_PIN, HIGH);
      }
    }
  }
  else
  {
    Serial.println("Manual Mode");
    digitalWrite(2, LOW);
  }

  // Lưu trạng thái nút nhấn
  lastButtonState = buttonState;
}
