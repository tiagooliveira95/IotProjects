#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <FirebaseArduino.h>
#include <NTPtimeESP.h>
#include <ESP8266HTTPClient.h>

NTPtime NTPch("0.pt.pool.ntp.org");
#define FIREBASE_HOST "XXXX"                         // the project name address from firebase id
#define FIREBASE_AUTH "XXXX"                    // the secret key generated from firebase
#define WIFI_SSID "XXXX"                                          // input your home or public wifi name 
#define WIFI_PASSWORD "XXXX"      //password of wifi ssid

bool lowActivation = false;

bool limitedMode = false;

int lastWeatherQueryTime = 0;

float main_temp;
const char* weather_0_main; // Rain
int clouds_all; // Cloudiness, %
float rain_1h;  // Rain volume for the last 1 hour, mm


int relayUP = D1; 
int relayDOWN = D2;
int ledPin = D4;

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

strDateTime dateTime;

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
  Firebase.set("janela/fuc/py", py);
  Firebase.set("janela/fuc/px", px);
  String fx = "f(x)=";
  fx += String(m1) + "x";
  Firebase.setString("janela/fuc/fx", fx);

}

void fg() {
  m2 = ( (  ky -  py) / ( kx -  px) );
  b2 = ky - (m2 * kx);
  Firebase.set("janela/fuc/ky", ky);
  Firebase.set("janela/fuc/kx", kx);
  String fg = "f(g)=";
  fg += String(m2) + "x";
  if (b2 > 0) fg += " + "; else fg += " - ";
  fg += fabs(b2);
  Firebase.setString("janela/fuc/fg", fg);
}

float fx(float x) {
  return m1 * x;
}

float fg(float x) {
  return m2 * x + b2;
}


void setup() {
  Serial.begin(9600);

  pinMode(relayUP, OUTPUT);
  pinMode(relayDOWN, OUTPUT);
  pinMode(ledPin, OUTPUT);
  pinMode(16, OUTPUT);
  resetRelays();

  // connect to wifi.
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("connecting");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    digitalWrite(ledPin, LOW);  
    delay(500);
    
  }
  


  Serial.println();
  Serial.print("connected: ");
  Serial.println(WiFi.localIP());

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

  Firebase.begin(FIREBASE_HOST, FIREBASE_AUTH);
  Firebase.stream("/");
  Firebase.set("janela/buzy", buzy);
  state = Firebase.getFloat("janela/state");
  Firebase.setBool("data/ota",false);
  updateData();

}

void printStatus() {
  Serial.print(hour);
  Serial.print(":");
  Serial.print(minute);
  Serial.print(" , week ");
  Serial.print(dayOfWeek);
  Serial.print(" , minTime: ");
  Serial.print(minuteTime);

  Serial.print(" , Temp ");
  Serial.print(main_temp);
  Serial.print(" , Weather ");
  Serial.print(weather_0_main);
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

int time_elapsed = 0;
void loop() {  

   if (WiFi.status() != WL_CONNECTED) {
       digitalWrite(ledPin, LOW);
   }else{
           digitalWrite(ledPin, HIGH);

    }
   
  if(OTA){
    while(time_elapsed < 15000){
      ArduinoOTA.handle();
      time_elapsed += 50;
      delay(50);
    }
    Firebase.setBool("data/ota",false);
    OTA = false;
    return;
  }
  
  digitalWrite(16, LOW);   


  dateTime = NTPch.getNTPtime(1, 0);


  hour = dateTime.hour;
  minute = dateTime.minute;
  dayOfWeek = dateTime.dayofWeek;

  minuteTime = hour * 60 + minute;

  fetchWeather();

  triggerAlarmClock();


  printStatus();

  if (Firebase.failed()) {
    Serial.println("streaming error");
    Serial.println(Firebase.error());
    Firebase.stream("/");
    digitalWrite(ledPin, LOW);
    delay(50);
    digitalWrite(ledPin, HIGH);  
    delay(50);
    digitalWrite(ledPin, LOW);
    delay(50);
    digitalWrite(ledPin, HIGH);  
    delay(50);
    digitalWrite(ledPin, LOW);
    delay(50);
    digitalWrite(ledPin, HIGH);  
    delay(50);
    digitalWrite(ledPin, LOW);
    delay(50);
    digitalWrite(ledPin, HIGH);  
    delay(50);
    digitalWrite(ledPin, LOW);
    delay(50);
    digitalWrite(ledPin, HIGH);  
    delay(50);
    digitalWrite(ledPin, LOW);
    delay(50);
    digitalWrite(ledPin, HIGH);  
  
    errorCounter++;
    if (errorCounter > 10) ESP.reset();
  }

  if (Firebase.available()) {
    digitalWrite(ledPin, HIGH);

    FirebaseObject event = Firebase.readEvent();

    String eventType = event.getString("type");
    eventType.toLowerCase();

    Serial.print("event: ");
    Serial.println(eventType);

    event.getJsonVariant().printTo(Serial);

    if (eventType == "put") {
      updateData(event);
    }

    if (eventType == "patch") {

      float val = event.getFloat("data/state");
      Serial.print("state: ");
      Serial.println(val);
      setWindowState(val);

    }
  }
  digitalWrite(16, HIGH);   
  delay(1000);
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



void updateData(FirebaseObject event) {
  if (event.getString("path").indexOf("janela") != -1) return;

  if (event.getString("path") == "/data/py")
    py = event.getFloat("data");

  if (event.getString("path") == "/data/cycleDuration")
    CYCLE_DURATION = event.getInt("data");

  if (event.getString("path") == "/data/cycleCount")
    CYCLE_COUNT = event.getInt("data");

  if (event.getString("path") == "/data/weekCloseTime")
    WEEK_CLOSE_TIME = event.getInt("data");

  if (event.getString("path") == "/data/weekOpenTime") {
    WEEK_OPEN_TIME = event.getInt("data");
  }

  if (event.getString("path") == "/data/weekendCloseTime") {
    WEEKEND_CLOSE_TIME = event.getInt("data");
  }

  if (event.getString("path") == "/data/weekendOpenTime") {
    WEEKEND_OPEN_TIME = event.getInt("data");
    Serial.print("Firebase event: weekendOpenTime: ");
    Serial.println(WEEKEND_OPEN_TIME);
  }

  if (event.getString("path") == "/data/cycleCount" || event.getString("path") == "/data/cycleDuration" || event.getString("path") == "/data/py" ) {
    setPoints();
    PERIOD = CYCLE_DURATION /  CYCLE_COUNT;
  }

  if(event.getString("path") == "/data/ota"){
    OTA = event.getBool("data");
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
  if(t>1000)
  Firebase.set("janela/buzy", buzy);
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
  if(t>1000)
  Firebase.set("janela/buzy", buzy);
  if(t>1000)
  Firebase.set("janela/state", state);

}

void fetchWeather() {
  if (lastWeatherQueryTime == minuteTime && (minuteTime % 10) != 0) {
    return;
    Serial.println("RETURNED");
  }

  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;  //Object of class HTTPClient
    http.begin("http://api.openweathermap.org/data/2.5/weather?q=Cacia,PT&appid=eeee5d398a4f7e3ed74bf9c3ec65f341&units=metric");
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

  if(weather_0_main == "Rain"){
    Serial.print("Limited Mode Enabled");
    limitedMode = true;
  }

}


void resetRelays() {
  digitalWrite(relayUP, lowActivation);
  digitalWrite(relayDOWN, lowActivation);
}
