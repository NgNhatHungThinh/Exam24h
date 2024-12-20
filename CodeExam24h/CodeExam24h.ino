#include <WiFi.h>
#include <FirebaseClient.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h> // Thư viện Mutex

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

WiFiClientSecure ssl;
DefaultNetwork network;
AsyncClientClass client(ssl, getNetwork(network));

FirebaseApp app;
RealtimeDatabase Database;
AsyncResult result;
LegacyToken dbSecret(FIREBASE_AUTH);

TaskHandle_t FirebaseTaskHandle = NULL;
SemaphoreHandle_t stateMutex; // Mutex bảo vệ truy cập biến

hw_timer_t* timer = NULL; 
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED; 

volatile int scheduledStart = 0;
volatile int scheduledEnd = 0;

volatile bool manualModeState = true;  // false: OFF Manual Mode) | true: ON Manual Mode
volatile bool autoModeState = false;   // false: OFF Automation Mode) | true: ON Automation Mode
bool configState = false;     // false: Device Config | true: WiFi Config
bool output1State = false;    // false: OFF Output_1 | true: ON Output_1
bool output2State = false;    // false: OFF Output_2 | true: ON Output_2

bool isPressed = false;
bool isHolding = false;
unsigned long currentPressTime = 0;
unsigned long lastPressTime = 0; 

uint8_t lastButtonState = 0;
uint8_t pressCounter = 0;

uint8_t timeONFlag = 0;
uint8_t timeONSecond = 0;
uint8_t intervalFlag = 0;
uint8_t intervalMinute = 0; 
uint8_t intervalSecond = 0;

void IRAM_ATTR onTimer()
{   
  //...Into mode ISR 
  portENTER_CRITICAL_ISR(&timerMux);

  if (timeONFlag == 1)
  {
    timeONSecond++;
    Serial.print("Thời gian ON: "); Serial.print(timeONSecond); Serial.println(" giây");

    if (timeONSecond == 45)
    {
      // Hẹn sau 5 phút 15 giây sẽ lặp lại
      intervalFlag = 1;

      // Reset giá trị
      timeONFlag = 0;
      timeONSecond = 0;
    }
  }

  if (intervalFlag == 1)
  {
    digitalWrite(LED_OUTPUT1_PIN, LOW);

    Serial.print("Thời gian lặp lại: "); Serial.print(intervalMinute); Serial.print(" phút "); Serial.print(intervalSecond); Serial.println(" giây");
    intervalSecond++; 

    if (intervalSecond > 59)
    {
      intervalMinute++;

      // Reset giá trị
      intervalSecond = 0;
    }

    if (intervalMinute == 5 && intervalSecond == 15)
    {
      timeONFlag = 1;
      digitalWrite(LED_OUTPUT1_PIN, HIGH);

      // Reset giá trị
      intervalMinute = 0;
      intervalSecond = 0;
      intervalFlag = 0;
    }
  }
  
  //...Exit mode ISR 
  portEXIT_CRITICAL_ISR(&timerMux); 
}

void FirebaseTask(void *pvParameters)
{
  unsigned long lastTime = 0; 

  while (true)
  {
    unsigned long currentTime = millis();
    if (currentTime - lastTime > 500)
    {
      scheduledStart = Database.get<int>(client, "/App/06Hour00Minute/start");
      scheduledEnd = Database.get<int>(client, "/App/00Hour59Minute/end");

      // Serial.print("Giờ bắt đầu: "); Serial.println(getHourStart);
      // Serial.print("Phút bắt đầu: "); Serial.println(getMinuteStart);
      // Serial.print("Giờ kết thúc: "); Serial.println(getHourEnd);
      // Serial.print("Phút kết thúc: "); Serial.println(getMinuteEnd);

      lastTime = currentTime;
    }

    // Delay before next update
    vTaskDelay(pdMS_TO_TICKS(500)); // 500 millisecond
  }
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

  manualModeState = Database.get<bool>(client, "/App/control/manualMode");
  autoModeState = Database.get<bool>(client, "/App/control/autoMode");

  xTaskCreatePinnedToCore(FirebaseTask, "FirebaseTask", 8192, NULL, 1, &FirebaseTaskHandle, 0);
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
    isPressed = false;
  }
  else if (isPressed && buttonState == HIGH && millis() - lastPressTime > PRESS_TIME_THRESHOLD)
  {
    currentPressTime = millis();
    Serial.print("Time counter: "); Serial.print(currentPressTime - lastPressTime); Serial.println("ms");

    if (currentPressTime - lastPressTime >= LONG_TIME)
    {
      isHolding = !isHolding;
      pressCounter = 0;

      // Lưu giá trị
      lastPressTime = currentPressTime;
      isPressed = false;
    }
  }
  else if (isPressed && buttonState == LOW && millis() - lastPressTime > PRESS_TIME_THRESHOLD)
  {
    // Lưu giá trị
    lastPressTime = currentPressTime;
    isPressed = false;
  }

  if (autoModeState)
  {
    if (scheduledStart)
    {
      if (timeONSecond < 45 && intervalFlag == 0)
      {
        timeONFlag = 1;
        digitalWrite(LED_OUTPUT1_PIN, HIGH);
      }
    }

    if (scheduledEnd)
    {
      timeONFlag = 0;
      timeONSecond = 0;
      intervalFlag = 0;
      intervalMinute = 0; 
      intervalSecond = 0;
      
      digitalWrite(LED_OUTPUT1_PIN, LOW);
    }

    if (millis() - lastPressTime > 500 && pressCounter > 0)
    {
      switch(pressCounter)
      {
      case 3:
        Serial.println("Manual Mode");
        
        digitalWrite(LED_OUTPUT1_PIN, LOW);

        autoModeState = false;
        manualModeState = true;
        Database.set<bool>(client, "/App/control/autoMode", autoModeState);
        Database.set<bool>(client, "/App/control/manualMode", manualModeState);

        timeONFlag = 0;
        timeONSecond = 0;
        intervalFlag = 0;
        intervalMinute = 0; 
        intervalSecond = 0;
        break;
      }

      // Reset số lần nhấn
      pressCounter = 0;
    }
  }
  
  if (manualModeState)
  {
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
        Serial.println("Automation Mode");

        autoModeState = true; 
        manualModeState = false;
        Database.set<bool>(client, "/App/control/autoMode", autoModeState);
        Database.set<bool>(client, "/App/control/manualMode", manualModeState);

        timeONFlag = 0;
        timeONSecond = 0;
        intervalFlag = 0;
        intervalMinute = 0; 
        intervalSecond = 0;
        break;
      }

      // Reset số lần nhấn
      pressCounter = 0;
    }
  }

  // Lưu trạng thái nút nhấn
  lastButtonState = buttonState;
}
