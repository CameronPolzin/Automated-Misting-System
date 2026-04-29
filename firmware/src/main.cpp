#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RTClib.h>
#include <WiFi.h>
#include <WebServer.h>

// =======================
// Display / RTC Settings
// =======================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C

#define SDA_PIN 4
#define SCL_PIN 5

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
RTC_DS3231 rtc;

// =======================
// Pump / Misting Settings
// =======================
const int pumpPin = 6;                         // MOSFET gate pin
unsigned long mistDurationMs = 5000;           // default 5 seconds

bool misting = false;
unsigned long mistEndTime = 0;

// Simple daily schedule (one time per day)
uint8_t schedHour   = 8;    // default 08:00
uint8_t schedMinute = 0;
bool scheduleEnabled = true;
int lastScheduleDay = -1;      // to avoid multiple triggers in same day/min
int lastScheduleMinute = -1;

// =======================
// Buttons (GPIO)
// =======================
const int BTN_UP_PIN     = 2;
const int BTN_DOWN_PIN   = 3;
const int BTN_SELECT_PIN = 7;
const int BTN_MANUAL_PIN = 10;

struct Button {
  int pin;
  bool lastReading;
  bool stableState;
  unsigned long lastChangeTime;
};

const unsigned long DEBOUNCE_MS = 30;

Button btnUp    = {BTN_UP_PIN, HIGH, HIGH, 0};
Button btnDown  = {BTN_DOWN_PIN, HIGH, HIGH, 0};
Button btnSel   = {BTN_SELECT_PIN, HIGH, HIGH, 0};
Button btnMan   = {BTN_MANUAL_PIN, HIGH, HIGH, 0};

// =======================
// Menu / Screen State
// =======================
enum ScreenState {
  SCREEN_HOME,
  SCREEN_SET_HOUR,
  SCREEN_SET_MINUTE
};

ScreenState screenState = SCREEN_HOME;

// =======================
// Wi-Fi AP / Web Server
// =======================
const char* ap_ssid     = "TerrariumMister";
const char* ap_password = "mist1234";

WebServer server(80);

// Forward declarations
void updateDisplay();
void startMist(unsigned long durationMs);
void stopMist();
void handleConfig();

// -----------------------
// Button helpers
// -----------------------
void initButton(Button &b) {
  pinMode(b.pin, INPUT_PULLUP);
  b.lastReading = HIGH;
  b.stableState = HIGH;
  b.lastChangeTime = 0;
}

// returns true once when a button is pressed (HIGH -> LOW, debounced)
bool buttonPressed(Button &b) {
  bool reading = digitalRead(b.pin);

  if (reading != b.lastReading) {
    b.lastChangeTime = millis();
    b.lastReading = reading;
  }

  if ((millis() - b.lastChangeTime) > DEBOUNCE_MS) {
    if (b.stableState == HIGH && reading == LOW) {
      b.stableState = reading;
      return true;  // new press
    }
    b.stableState = reading;
  }
  return false;
}

// =======================
// Web Handlers
// =======================
void handleRoot() {
  // Simple HTML page with buttons and config
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Terrarium Mister</title>";
  html += "<style>";
  html += "body{font-family:Arial;text-align:center;margin-top:20px;}";
  html += "button{font-size:18px;padding:8px 18px;margin:8px;}";
  html += "input{font-size:16px;padding:4px;margin:4px;}";
  html += "label{display:block;margin:6px;}";
  html += "fieldset{margin:10px;border-radius:8px;}";
  html += "</style></head><body>";

  html += "<h1>Terrarium Mister</h1>";

  html += "<p>Status: <b>";
  html += (misting ? "Misting" : "Idle");
  html += "</b></p>";

  // Show current schedule and duration
  char buf[16];
  sprintf(buf, "%02d:%02d", schedHour, schedMinute);
  html += "<p>Schedule: <b>";
  html += buf;
  html += scheduleEnabled ? " (ENABLED)" : " (DISABLED)";
  html += "</b></p>";

  html += "<p>Mist duration: <b>";
  html += String(mistDurationMs / 1000);
  html += " s</b></p>";

  // Manual controls
  html += "<fieldset><legend>Manual Control</legend>";
  html += "<form action='/mist' method='GET'>";
  html += "<button type='submit'>Mist Now</button>";
  html += "</form>";

  html += "<form action='/stop' method='GET'>";
  html += "<button type='submit'>Stop</button>";
  html += "</form>";
  html += "</fieldset>";

  // Configuration form
  html += "<fieldset><legend>Schedule Configuration</legend>";
  html += "<form action='/config' method='GET'>";
  html += "<label>Hour (0-23): ";
  html += "<input type='number' name='hour' min='0' max='23' value='";
  html += String(schedHour);
  html += "'></label>";

  html += "<label>Minute (0-59): ";
  html += "<input type='number' name='minute' min='0' max='59' value='";
  html += String(schedMinute);
  html += "'></label>";

  html += "<label>Mist duration (seconds): ";
  html += "<input type='number' name='duration' min='1' max='600' value='";
  html += String(mistDurationMs / 1000);
  html += "'></label>";

  html += "<label>";
  html += "<input type='checkbox' name='enable' value='1' ";
  if (scheduleEnabled) html += "checked";
  html += "> Enable daily schedule";
  html += "</label>";

  html += "<button type='submit'>Save Settings</button>";
  html += "</form>";
  html += "</fieldset>";

  html += "<p><a href='/status'>Raw status endpoint</a></p>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

void handleMist() {
  startMist(mistDurationMs);
  // Redirect back to root page
  server.sendHeader("Location", "/", true);
  server.send(303);
}

void handleStop() {
  stopMist();
  server.sendHeader("Location", "/", true);
  server.send(303);
}

void handleStatus() {
  String s = misting ? "Misting" : "Idle";
  s += " | Schedule: ";
  s += (scheduleEnabled ? "ON" : "OFF");
  s += " @ ";
  char buf[16];
  sprintf(buf, "%02d:%02d", schedHour, schedMinute);
  s += buf;
  s += " | Duration: ";
  s += String(mistDurationMs / 1000);
  s += "s";
  server.send(200, "text/plain", s);
}

// Handle configuration from web form
void handleConfig() {
  // hour
  if (server.hasArg("hour")) {
    int h = server.arg("hour").toInt();
    if (h < 0) h = 0;
    if (h > 23) h = 23;
    schedHour = (uint8_t)h;
  }

  // minute
  if (server.hasArg("minute")) {
    int m = server.arg("minute").toInt();
    if (m < 0) m = 0;
    if (m > 59) m = 59;
    schedMinute = (uint8_t)m;
  }

  // duration (seconds)
  if (server.hasArg("duration")) {
    long d = server.arg("duration").toInt(); // seconds
    if (d < 1) d = 1;
    if (d > 600) d = 600; // cap at 10 minutes
    mistDurationMs = (unsigned long)d * 1000UL;
  }

  // enable checkbox
  // If checkbox is present AND '1', enable schedule
  // If checkbox is missing (unchecked), disable
  if (server.hasArg("enable") && server.arg("enable") == "1") {
    scheduleEnabled = true;
  } else {
    scheduleEnabled = false;
  }

  // After updating values, redirect back to main page
  server.sendHeader("Location", "/", true);
  server.send(303);
}

// =======================
// Pump Control Functions
// =======================
void startMist(unsigned long durationMs) {
  misting = true;
  mistEndTime = millis() + durationMs;
  digitalWrite(pumpPin, HIGH);  // turn pump ON
}

void stopMist() {
  misting = false;
  digitalWrite(pumpPin, LOW);   // turn pump OFF
}

// =======================
// Setup
// =======================
void setup() {
  Wire.begin(SDA_PIN, SCL_PIN);
  Serial.begin(115200);

  // Pump pin
  pinMode(pumpPin, OUTPUT);
  digitalWrite(pumpPin, LOW); // ensure off

  // Buttons
  initButton(btnUp);
  initButton(btnDown);
  initButton(btnSel);
  initButton(btnMan);

  // OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED init failed");
    while (true);
  }

  // RTC
  if (!rtc.begin()) {
    Serial.println("RTC init failed");
    while (true);
  }

  // If you ever need to set the time again, uncomment this line,
  // upload once, then comment it out and upload again:
  //rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

  // ---- Wi-Fi AP Mode ----
  WiFi.mode(WIFI_AP);
  bool apResult = WiFi.softAP(ap_ssid, ap_password);
  if (apResult) {
    Serial.println("AP started successfully.");
  } else {
    Serial.println("AP start failed.");
  }

  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);  // usually 192.168.4.1

  // Web routes
  server.on("/", handleRoot);
  server.on("/mist", handleMist);
  server.on("/stop", handleStop);
  server.on("/status", handleStatus);
  server.on("/config", handleConfig);
  server.begin();
  Serial.println("HTTP server started.");

  display.clearDisplay();
  updateDisplay();
}

// =======================
// Loop
// =======================
void loop() {
  server.handleClient();  // handle web requests

  unsigned long nowMs = millis();

  // Update mist state based on timer
  if (misting && nowMs > mistEndTime) {
    stopMist();
  }

  // --- Button handling ---
  bool upPressed     = buttonPressed(btnUp);
  bool downPressed   = buttonPressed(btnDown);
  bool selectPressed = buttonPressed(btnSel);
  bool manualPressed = buttonPressed(btnMan);

  // Manual mist button always works (unless already misting)
  if (manualPressed && !misting) {
    startMist(mistDurationMs);
  }

  // Menu state machine
  if (screenState == SCREEN_HOME) {
    if (selectPressed) {
      screenState = SCREEN_SET_HOUR;
    }
  } else if (screenState == SCREEN_SET_HOUR) {
    if (upPressed) {
      schedHour = (schedHour + 1) % 24;
    }
    if (downPressed) {
      schedHour = (schedHour + 23) % 24; // wrap backwards
    }
    if (selectPressed) {
      screenState = SCREEN_SET_MINUTE;
    }
  } else if (screenState == SCREEN_SET_MINUTE) {
    if (upPressed) {
      schedMinute = (schedMinute + 1) % 60;
    }
    if (downPressed) {
      schedMinute = (schedMinute + 59) % 60;
    }
    if (selectPressed) {
      screenState = SCREEN_HOME; // exit menu
    }
  }

  // --- Auto schedule: fire once per day at schedHour:schedMinute ---
  if (scheduleEnabled && !misting) {
    DateTime now = rtc.now();
    if (now.hour() == schedHour && now.minute() == schedMinute) {
      if (now.day() != lastScheduleDay || now.minute() != lastScheduleMinute) {
        startMist(mistDurationMs);
        lastScheduleDay = now.day();
        lastScheduleMinute = now.minute();
      }
    }
  }

  // Update display periodically (~4 times per second)
  static unsigned long lastDisplayUpdate = 0;
  if (nowMs - lastDisplayUpdate >= 250) {
    lastDisplayUpdate = nowMs;
    updateDisplay();
  }
}

// =======================
// Display Function
// =======================
void updateDisplay() {
  DateTime now = rtc.now();

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  // ===== Title =====
  display.setCursor(0, 0);
  display.println("Terrarium Controller");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  if (screenState == SCREEN_HOME) {
    // ===== Date =====
    display.setCursor(0, 14);
    display.printf("Date: %04d-%02d-%02d",
                   now.year(),
                   now.month(),
                   now.day());

    // ===== Time =====
    display.setCursor(0, 26);
    display.printf("Time: %02d:%02d:%02d",
                   now.hour(),
                   now.minute(),
                   now.second());

    // ===== Status =====
    display.setCursor(0, 40);
    if (misting) {
      display.println("Status: Misting");
    } else {
      display.println("Status: Idle");
    }

    // ===== Next Mist =====
    display.setCursor(0, 50);
    if (scheduleEnabled) {
      display.printf("Next mist: %02d:%02d", schedHour, schedMinute);
    } else {
      display.println("Next mist: OFF");
    }
  }
  else if (screenState == SCREEN_SET_HOUR) {
    display.setCursor(0, 20);
    display.println("Set Hour");
    display.setTextSize(2);
    display.setCursor(40, 36);
    display.printf("%02d", schedHour);
    display.setTextSize(1);
  }
  else if (screenState == SCREEN_SET_MINUTE) {
    display.setCursor(0, 20);
    display.println("Set Minute");
    display.setTextSize(2);
    display.setCursor(32, 36);
    display.printf("%02d", schedMinute);
    display.setTextSize(1);
  }

  display.display();
}
