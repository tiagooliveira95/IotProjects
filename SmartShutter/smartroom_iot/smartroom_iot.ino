#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <IOXhop_FirebaseESP32.h>
#include <HTTPClient.h>
#include "time.h"
#include "credentials.h"

//RTC
//0.pt.pool.ntp.org


const char* ntpServer = "pool.ntp.org";
const char* ntpServer1 = "0.pt.pool.ntp.org";


const long  gmtOffset_sec = 0;
const int   daylightOffset_sec = 3600;

bool daylight = 0;


hw_timer_t * timer = NULL;
volatile SemaphoreHandle_t timerSemaphore;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

//Multicore
TaskHandle_t Task1;


bool lowActivation = false;
bool limitedMode = false;

int lastWeatherQueryTime = 0;

float main_temp;
const char* weather_0_main = ""; // Rain
int clouds_all = 0; // Cloudiness, %
float rain_1h = 0.0;  // Rain volume for the last 1 hour, mm


int relayUP = 27; //laranja
int relayDOWN = 25; //Amarelo
int BUILT_IN_LED = 32;

bool smartOpening = false;

int WINDOW_ACTIVE_FULL = 16 * 1000;
int CYCLE_DURATION = 90;
int CYCLE_COUNT = 9;
int PERIOD = 0;


int WEEK_CLOSE_TIME = -1;
int WEEK_OPEN_TIME = -1;
int WEEKEND_CLOSE_TIME = -1;
int WEEKEND_OPEN_TIME = -1;

bool buzy = false;

bool OTA = false;

float state = 0.0;


int hour;
int minute;
int minuteTime;
int dayOfWeek = 0;

bool wasOpenAlarmTrigered = false;
bool wasCloseAlarmTrigered = false;

//Function f(x) = mx + b
float m1 = 0;
//Function f(g) = mx + b
float m2 = 0;
float b2 = 0;

// Point P, x ~ 32%
float px = 0;
float py = 0;

// Point K
float kx = 0;
float ky = 0;


int errorCounter = 0;


void setPoints() {
  kx = CYCLE_COUNT;
  ky = 1.0;
  px = Firebase.getFloat("data/px") * CYCLE_COUNT;
  py = 0.31;

  fx();
  fg();
}

void fx() {
  m1 = ( ((float) py) / ((float) px) );
  Firebase.set("window/fuc/py", py);
  Firebase.set("window/fuc/px", px);
  String fx = "f(x)=";
  fx += String(m1) + "x";
  Firebase.setString("window/fuc/fx", fx);

}

void fg() {
  m2 = ( (  ky -  py) / ( kx -  px) );
  b2 = ky - (m2 * kx);
  Firebase.set("window/fuc/ky", ky);
  Firebase.set("window/fuc/kx", kx);
  String fg = "f(g)=";
  fg += String(m2) + "x";
  if (b2 > 0) fg += " + "; else fg += " - ";
  fg += fabs(b2);
  Firebase.setString("window/fuc/fg", fg);
}

float fx(float x) {
  return m1 * x;
}

float fg(float x) {
  return m2 * x + b2;
}

void printLocalTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
  minuteTime = timeinfo.tm_hour * 60 + timeinfo.tm_min;
}

// Runs every 10 minutes
void IRAM_ATTR onTimer() {
  Serial.println("onTimer");
}

void setRepeatAlarm() {
  // Create semaphore to inform us when the timer has fired
  timerSemaphore = xSemaphoreCreateBinary();

  // Use 1st timer of 4 (counted from zero).
  // Set 80 divider for prescaler (see ESP32 Technical Reference Manual for more
  // info).
  timer = timerBegin(0, 80, true);

  // Attach onTimer function to our timer.
  timerAttachInterrupt(timer, &onTimer, true);

  // Set alarm to call onTimer function every 10 minutes (value in microseconds).
  // Repeat the alarm (third parameter)
  timerAlarmWrite(timer, 1000000 * 60 * 10, true);

  // Start an alarm
  timerAlarmEnable(timer);
}

void startOTA() {
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();

}

void beginFirebaseStream() {
  Firebase.stream("/", [](FirebaseStream stream) {
    handleFirebaseStream(stream);
  });
}

void setup() {
  Serial.begin(115200);

  pinMode(relayUP, OUTPUT);
  pinMode(relayDOWN, OUTPUT);
  pinMode(BUILT_IN_LED, OUTPUT);

  setRepeatAlarm();

  resetRelays();

  // connect to wifi.
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("connecting");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    digitalWrite(BUILT_IN_LED, HIGH);
    delay(500);
  }

  Serial.println();
  Serial.print("connected: ");
  Serial.println(WiFi.localIP());

  digitalWrite(BUILT_IN_LED, LOW);

  xTaskCreatePinnedToCore(
    Core0, /* Function to implement the task */
    "Core0", /* Name of the task */
    10000,  /* Stack size in words */
    NULL,  /* Task input parameter */
    0,  /* Priority of the task */
    &Task1,  /* Task handle. */
    0); /* Core where the task should run */

  //Setting ESP32 RTC time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer,ntpServer1);
  printLocalTime();

  fetchWeather();

  //ArduinoOTA for over the air flashing
  startOTA();

  //Firebase initialization
  Firebase.begin(FIREBASE_HOST, FIREBASE_AUTH);

  //Resets "device buzy" to false
  Firebase.set("window/buzy", false);
  //Gets current state of shutter
  state = Firebase.getFloat("window/state");
  //Reset OTA flag
  Firebase.setBool("data/ota", false);

  updateData();

  beginFirebaseStream();
}

void loop() {}

int time_elapsed = 0;
void Core0( void * parameter) {

  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      digitalWrite(BUILT_IN_LED, HIGH);
    } else {
      digitalWrite(BUILT_IN_LED, LOW);
    }

    if (OTA) {
      while (time_elapsed < 15000) {
        ArduinoOTA.handle();
        time_elapsed += 50;
        delay(50);
      }
      Firebase.setBool("data/ota", false);
      OTA = false;
      return;
    }

    printLocalTime();

    triggerAlarmClock();

    printStatus();

    delay(1000);
  }
}

void printStatus() {
  Serial.print(" Temp ");
  Serial.print(main_temp);
  Serial.print(" , Clouds ");
  Serial.print(clouds_all);
  Serial.print(" , Rain/mm ");
  Serial.println(rain_1h);

  /*Serial.print(" HEAP: ");
     Serial.println(ESP.getFreeHeap());

     Serial.print(" weekCloseTime: ");
    Serial.print(WEEK_CLOSE_TIME);
    Serial.print(" weekOpenTime: ");
    Serial.print(WEEK_OPEN_TIME);

    Serial.print(" weekendCloseTime: ");
    Serial.print(WEEKEND_CLOSE_TIME);
    Serial.print(" weekendOpenTime: ");
    Serial.println(WEEKEND_OPEN_TIME);

      Serial.print(" f(x) = ");
      Serial.print(m1);
      Serial.print("x");

      Serial.print(" f(g) = ");
      Serial.print(m2);
      Serial.print("x");
      if (b2 > 0) Serial.print(" + "); else Serial.print(" - ");
      Serial.print(fabs(b2));

      Serial.print(" px ");
      Serial.print(px);

      Serial.print(" py ");
      Serial.print(py);

      Serial.print(" kx ");
      Serial.print(kx);

      Serial.print(" ky ");
      Serial.println(ky);
  */
}

void handleFirebaseStream(FirebaseStream event) {
  String eventType = event.getEvent();
  eventType.toLowerCase();

  Serial.print("event: ");
  Serial.print(eventType);
  Serial.print(" path ");
  Serial.println(event.getPath());

  //event.getJsonVariant().printTo(Serial);

  if (eventType == "put") {
    updateData(event);
  }

  if (eventType == "patch" && event.getPath() == "/data") {
    event.getData().printTo(Serial);

    float val = event.getData()["state"].as<float>();
    Serial.print("state: ");
    Serial.println(val);
    setWindowState(val);
  }
}


void triggerAlarmClock() {
  if (minuteTime == 0) {
    resetAlarmClockFlags();
  }

  if (dayOfWeek == 0 || dayOfWeek == 1) {
    if (!wasCloseAlarmTrigered && WEEKEND_CLOSE_TIME == minuteTime) {
      Serial.println("WEEKEND CLOSE");
      wasCloseAlarmTrigered = true;
      setWindowState(0);
    } else if (!wasOpenAlarmTrigered && WEEKEND_OPEN_TIME == minuteTime || smartOpening) {
      Serial.println("WEEKEND OPEN");
      smartOpening = true;
      //wasOpenAlarmTrigered = true;

      float x = (minuteTime - WEEKEND_OPEN_TIME) / PERIOD;
      Serial.print("x: ");
      Serial.println(x);

      if (WEEKEND_OPEN_TIME + CYCLE_DURATION + 1 > minuteTime &&  WEEKEND_OPEN_TIME + PERIOD * x == minuteTime) {

        if (x < px) {
          setWindowState(fx(x));
        } else {
          setWindowState(fg(x));
        }

      } else if (WEEKEND_OPEN_TIME + CYCLE_DURATION < minuteTime) {
        smartOpening = false;
        wasOpenAlarmTrigered = true;
      }
    }

  } else {
    if (!wasCloseAlarmTrigered && WEEK_CLOSE_TIME == minuteTime) {
      wasCloseAlarmTrigered = true;
      setWindowState(0);
    } else if (!wasOpenAlarmTrigered && WEEK_OPEN_TIME == minuteTime || smartOpening) {
      Serial.println("WEEK OPEN");
      smartOpening = true;
      //wasOpenAlarmTrigered = true;

      float x = ( minuteTime - (float)  WEEK_OPEN_TIME) / (float) PERIOD;
      Serial.print("x: ");
      Serial.println(x);


      if (WEEK_OPEN_TIME + CYCLE_DURATION + 1 > minuteTime &&  WEEK_OPEN_TIME + PERIOD * x == minuteTime && x != 0 && x <= 1) {

        if (x < px) {
          setWindowState(fx(x));

        } else {
          setWindowState(fg(x));
        }

        if (x == 1) {
          smartOpening = false;
          wasOpenAlarmTrigered = true;
        }
      }
    }
  }

}


void resetAlarmClockFlags() {
  wasCloseAlarmTrigered = false;
  wasOpenAlarmTrigered = false;

}


void updateData() {
  py = Firebase.getFloat("data/py");
  CYCLE_DURATION = Firebase.getInt("data/cycleDuration");
  CYCLE_COUNT = Firebase.getInt("data/cycleCount");

  WEEK_CLOSE_TIME = Firebase.getInt("data/weekCloseTime");
  WEEK_OPEN_TIME = Firebase.getInt("data/weekOpenTime");
  WEEKEND_CLOSE_TIME = Firebase.getInt("data/weekendCloseTime");
  WEEKEND_OPEN_TIME = Firebase.getInt("data/weekendOpenTime");
  PERIOD = CYCLE_DURATION /  CYCLE_COUNT;

  setPoints();
  fx();
  fg();
}


void updateData(FirebaseStream event) {
  if (event.getPath().indexOf("window") != -1) return;

  if (event.getPath() == "/data/px")
    py = event.getDataFloat();

  if (event.getPath() == "/data/cycleDuration")
    CYCLE_DURATION = event.getDataInt();

  if (event.getPath() == "/data/cycleCount")
    CYCLE_COUNT = event.getDataInt();

  if (event.getPath() == "/data/weekCloseTime")
    WEEK_CLOSE_TIME = event.getDataInt();

  if (event.getPath() == "/data/weekOpenTime") {
    WEEK_OPEN_TIME = event.getDataInt();
  }

  if (event.getPath() == "/data/weekendCloseTime") {
    WEEKEND_CLOSE_TIME = event.getDataInt();
  }

  if (event.getPath() == "/data/weekendOpenTime") {
    WEEKEND_OPEN_TIME = event.getDataInt();
    Serial.print("Firebase event: weekendOpenTime: ");
    Serial.println(WEEKEND_OPEN_TIME);
  }


  if (event.getPath() == "/data/cycleCount" || event.getPath() == "/data/cycleDuration" || event.getPath() == "/data/px" ) {
    setPoints();
    if (CYCLE_COUNT > 0)
      PERIOD = CYCLE_DURATION /  CYCLE_COUNT;

  }

  if (event.getPath() == "/data/ota") {
    OTA = event.getDataBool();
  }

}


/*
   @nwState: range -> [0.0 , 1.0]
*/
void setWindowState(float nwState) {
  Serial.print("new state: ");
  Serial.println(nwState);
  //|| (limitedMode && state > 0.36)
  if (buzy || state == nwState ) return;

  int t = fabs(nwState - state) * WINDOW_ACTIVE_FULL;
  Serial.print("time:");
  Serial.println(t);
  if (t < 500) return;

  buzy = true;
  if (t > 1000)
    Firebase.set("window/buzy", buzy);
  resetRelays();



  // lim x -> 1
  if (nwState > state) {
    digitalWrite(relayUP, !lowActivation);
    delay(t);
    digitalWrite(relayUP, lowActivation);
  } else {
    digitalWrite(relayDOWN, !lowActivation);
    delay(t);
    digitalWrite(relayDOWN, lowActivation);

  }

  state = nwState;

  buzy = false;
  if (t > 1000)
    Firebase.set("window/buzy", buzy);
  if (t > 1000)
    Firebase.set("window/state", state);

}

void fetchWeather() {
  if (lastWeatherQueryTime == minuteTime && (minuteTime % 10) != 0) {
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;  //Object of class HTTPClient
    http.begin("http://api.openweathermap.org/data/2.5/weather?q=" + WEATHER_LOCAL +  "&appid=" + WEATHER_API_KEY + "&units=" + WEATHER_UNITS);
    int httpCode = http.GET();
    //Check the returning code
    if (httpCode > 0) {
      // Get the request response payload
      String payload = http.getString();
      parseWeather(payload);
    }

    http.end();   //Close connection
    lastWeatherQueryTime = minuteTime;

  } else {
    Serial.println("NOT CONNECTED");
  }

}

void parseWeather(String jsonString) {
  const size_t capacity = JSON_ARRAY_SIZE(1) + 2 * JSON_OBJECT_SIZE(1) + JSON_OBJECT_SIZE(2) + JSON_OBJECT_SIZE(3) + JSON_OBJECT_SIZE(4) + JSON_OBJECT_SIZE(5) + JSON_OBJECT_SIZE(6) + JSON_OBJECT_SIZE(13) + 420;
  DynamicJsonBuffer jsonBuffer(capacity);

  const char* json = jsonString.c_str();

  JsonObject& root = jsonBuffer.parseObject(json);

  JsonObject& weather_0 = root["weather"][0];
  int weather_0_id = weather_0["id"]; // 501
  weather_0_main = weather_0["main"]; // "Rain"
  const char* weather_0_description = weather_0["description"]; // "moderate rain"
  //const char* weather_0_icon = weather_0["icon"]; // "10d"

  JsonObject& main = root["main"];

  main_temp = main["temp"]; // 284.88
  //int main_humidity = main["humidity"]; // 66


  JsonObject& wind = root["wind"];
  //float wind_speed = wind["speed"]; // 6.2
  //int wind_deg = wind["deg"]; // 320
  // float wind_gust = wind["gust"]; // 11.8
  rain_1h = root["rain"]["1h"]; // 2.79
  clouds_all = root["clouds"]["all"]; // 40


  JsonObject& sys = root["sys"];
  // long sys_sunrise = sys["sunrise"]; // 1554531019
  //long sys_sunset = sys["sunset"]; // 1554577387
  int cod = root["cod"]; // 200

  if (weather_0_main == "Rain") {
    Serial.print("Limited Mode Enabled");
    limitedMode = true;
  }
}

void resetRelays() {
  digitalWrite(relayUP, lowActivation);
  digitalWrite(relayDOWN, lowActivation);
}
