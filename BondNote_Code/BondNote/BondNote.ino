#include <TFT_eSPI.h>
#include <SPI.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <ArduinoWebsockets.h>
#include <Adafruit_NeoPixel.h>  
#include <HTTPClient.h>             // Newly added, used for HTTP GET requests
#include "Free_Fonts.h" // Include Free_Fonts.h

using namespace websockets;

// **WiFi and WebSocket settings**
const char* ssid = "";      // Your WiFi SSID
const char* password = "";  // Your WiFi password

// WebSocket server
const char* websockets_server_host = "";
const uint16_t websockets_server_port = 80;

WebsocketsClient client;
bool wsConnected = false;  // WebSocket connection status
unsigned long lastPoll = 0;

// Create TFT_eSPI object
TFT_eSPI tft = TFT_eSPI();

// Two buttons on the home page
TFT_eSPI_Button btnSendMessage;
TFT_eSPI_Button btnReceiveMessage;

// Three buttons on the history page
TFT_eSPI_Button btnHistory1;
TFT_eSPI_Button btnHistory2;
TFT_eSPI_Button btnHistory3;

// Pre-determined touch calibration data (example values)
uint16_t calData[5] = { 303, 3460, 231, 3513, 1 };

// Screen width and height
#define SCREEN_WIDTH  480
#define SCREEN_HEIGHT 320

// Each button's width/height
#define BTN_W SCREEN_WIDTH
#define BTN_H (SCREEN_HEIGHT / 3)

// History message buttons' center coordinates
#define BTN1_CX (SCREEN_WIDTH / 2)
#define BTN1_CY (BTN_H / 2)
#define BTN2_CX (SCREEN_WIDTH / 2)
#define BTN2_CY (BTN_H + BTN_H / 2)
#define BTN3_CX (SCREEN_WIDTH / 2)
#define BTN3_CY (2 * BTN_H + BTN_H / 2)

// Home page button width/height
#define BTN_HOME_W (SCREEN_WIDTH / 4)
#define BTN_HOME_H (SCREEN_HEIGHT / 4)

// Home page buttons' center coordinates
#define BTN_SEND_CX (SCREEN_WIDTH / 4)
#define BTN_SEND_CY (SCREEN_HEIGHT * 3 / 4)
#define BTN_RECEIVE_CX (3 * SCREEN_WIDTH / 4)
#define BTN_RECEIVE_CY (SCREEN_HEIGHT * 3 / 4)

#define MAX_POINTS 100       // Up to 100 points per stroke
#define MAX_STROKES 100      // Up to 100 strokes total
#define INACTIVITY_TIMEOUT 5000 // 5 seconds of no touch triggers redraw

// LED strip configuration (please adjust LED_PIN based on actual wiring)
#define LED_PIN    5    // An available GPIO on ESP32
#define LED_COUNT  40   // Number of LEDs on the strip

// Initialize the LED strip object
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

uint16_t touchPoints[MAX_POINTS][2]; // Points stored for the current stroke
uint8_t pointIndex = 0;              // Index for the current stroke's points
uint8_t thickness = 2;               // Pen thickness
uint8_t strokeIndex = 1;             // Which stroke number we are on
unsigned long lastTouchTime = 0;     // Records the last touch time

struct Stroke {
  uint16_t points[MAX_POINTS][2]; // All points for this stroke
  uint8_t pointCount;             // Number of points in this stroke
};

Stroke strokes[MAX_STROKES]; // Stores all strokes
uint8_t strokeCount = 0;     // Records the total number of strokes

// Page indicator: 0-home, 1-history, 2-drawing
int page = 0;

// ========== Newly added: Store fetched historical data from server ==========
struct DataItem {
  String name;
  String timestamp;
};

// Use a global array or vector to store fetched data
#define MAX_DATA_ITEMS 20
DataItem dataItems[MAX_DATA_ITEMS];
int dataCount = 0;

// Refresh data every 5 seconds, using a global variable as a timer
unsigned long lastDataUpdate = 0;

// ================== Function declarations ==================
void drawHomePage();
void drawHistoryPage();
void handleHomePage();
void handleHistoryPage();
void handleDrawingPage();

void setupWiFiAndWebSocket();
void fetchRemoteData();

// ----- LED strip effect function declarations -----
uint32_t Wheel(byte WheelPos);
void updateLEDHome();
void updateLEDHistory();
void updateLEDDrawing();
void updateLEDs();
// -------------------------------------------------- //

// ================== Initialization ==================
void setup() {
  Serial.begin(115200);

  // Connect WiFi & WebSocket first
  setupWiFiAndWebSocket();

  // Initialize screen
  tft.init();
  tft.setRotation(3);
  tft.setTouch(calData);
  tft.fillScreen(TFT_BLACK);

  // Initialize LED
  strip.begin();
  strip.show();
  strip.setBrightness(30); // Lower brightness to avoid glare

  // Draw home page
  drawHomePage();
}

// ================== Main loop ==================
void loop() {
  // 1) Poll WebSocket in any page at set intervals
  if (millis() - lastPoll > 100 && wsConnected) {
    client.poll();
    lastPoll = millis();
  }

  // 2) If wsConnected == false, automatically attempt to reconnect (can set retry frequency)
  static unsigned long lastReconnectAttempt = 0;
  if (!wsConnected && millis() - lastReconnectAttempt >= 2000) {
    lastReconnectAttempt = millis();
    Serial.println("Trying to reconnect to WebSocket...");
    wsConnected = client.connect(websockets_server_host, websockets_server_port, "/");
    if (wsConnected) {
      Serial.println("✅ Reconnected!");
    } else {
      Serial.println("❌ Reconnect failed, will retry...");
    }
  }

  // 3) Handle interactions based on the current page
  if (page == 0) {
    // Home page interactions
    handleHomePage();
  } 
  else if (page == 1) {
    // History page interactions
    handleHistoryPage();
  } 
  else if (page == 2) {
    // Drawing page interactions
    handleDrawingPage();
  }

  // 4) Update LED strip effect corresponding to the current page (non-blocking)
  updateLEDs();

  // 5) Make an HTTP GET request to fetch data every 5 seconds if page == 0
  if (millis() - lastDataUpdate >= 5000 && page == 0) {
    lastDataUpdate = millis();
    fetchRemoteData();
  }
}

// ================== WiFi & WebSocket setup ==================
void setupWiFiAndWebSocket() {
  // 1. Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("🔄 Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
      Serial.print(".");
      delay(1000);
  }
  Serial.println("\n✅ WiFi Connected!");

  // 2. Keep reconnecting WebSocket until successful
  Serial.print("🔄 Connecting to WebSocket Server...");
  while (!client.connect(websockets_server_host, websockets_server_port, "/")) {
      Serial.print(".");
      delay(1000);
  }
  wsConnected = true;
  Serial.println("\n✅ WebSocket Connected!");

  // 3. Handle WebSocket messages + handle disconnection callback
  client.onMessage([](WebsocketsMessage message) {
      Serial.println("📩 WebSocket Received: " + message.data());
  });

  client.onEvent([](WebsocketsEvent event, String data) {
      if (event == WebsocketsEvent::ConnectionClosed) {
          Serial.println("❌ WebSocket Connection Closed!");
          wsConnected = false; 
      }
  });
}

// ================== HTTP GET to fetch remote data ==================
void fetchRemoteData() {
  // Ensure WiFi is connected
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    // Target URL
    http.begin("http://bond-note-c746823b71f7.herokuapp.com/data");
    int httpCode = http.GET(); // Send GET request

    if (httpCode > 0) {
      // httpCode is the HTTP response code (200 = OK)
      if (httpCode == HTTP_CODE_OK) {
        // Get response payload
        String payload = http.getString();
        Serial.println("HTTP GET response:");
        Serial.println(payload);

        // Use ArduinoJson to parse
        StaticJsonDocument<2048> doc;  // Adjust size as needed
        DeserializationError error = deserializeJson(doc, payload);
        if (!error) {
          JsonArray arr = doc.as<JsonArray>();

          // Clear existing data
          dataCount = 0;

          // Read each JSON object
          for (JsonObject obj : arr) {
            if (dataCount >= MAX_DATA_ITEMS) break; // Prevent overflow
            dataItems[dataCount].name = obj["name"].as<String>();
            dataItems[dataCount].timestamp = obj["timestamp"].as<String>();
            dataCount++;
          }

          Serial.println("✅ Data fetched and stored!");
        } else {
          Serial.print("❌ JSON parse error: ");
          Serial.println(error.c_str());
        }
      } else {
        Serial.printf("❌ HTTP GET... code: %d\n", httpCode);
      }
    } else {
      // httpCode <= 0 means the request failed
      Serial.printf("❌ GET failed, error: %s\n", http.errorToString(httpCode).c_str());
    }
    http.end(); // Close connection
  } else {
    Serial.println("❌ WiFi not connected!");
  }
}

// ================== Draw home page ==================
void drawHomePage() {
  tft.fillScreen(TFT_BLACK);

  // Use a FreeFont for the system title
  tft.setFreeFont(FSBI24);  // Using Serif Bold Italic 24pt
  tft.setTextColor(TFT_WHITE, TFT_BLACK); // White text on black background

  int16_t stringWidth = tft.textWidth("BondNote", GFXFF);
  int16_t xpos = (SCREEN_WIDTH - stringWidth) / 2;
  int16_t ypos = 40;  
  tft.drawString("BondNote", xpos, ypos, GFXFF);

  tft.setFreeFont(FSBI9);  // Use FSB9
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  String line1 = "Every word you write brings your face so near,";
  int16_t line1Width = tft.textWidth(line1, GFXFF);
  int16_t line1_x = (SCREEN_WIDTH - line1Width) / 2;
  int16_t line1_y = ypos + tft.fontHeight(GFXFF) + 40;
  tft.drawString(line1, line1_x, line1_y, GFXFF);

  String line2 = "as if your presence were truly here.";
  int16_t line2Width = tft.textWidth(line2, GFXFF);
  int16_t line2_x = (SCREEN_WIDTH - line2Width) / 2;
  int16_t line2_y = line1_y + tft.fontHeight(GFXFF) + 8;
  tft.drawString(line2, line2_x, line2_y, GFXFF);

  // Revert to default font, prepare to draw buttons
  tft.setFreeFont(NULL);
  tft.setTextFont(2);

  // Initialize and draw buttons
  btnSendMessage.initButton(
    &tft, BTN_SEND_CX, BTN_SEND_CY, BTN_HOME_W, BTN_HOME_H,
    TFT_WHITE, TFT_BLUE, TFT_WHITE, (char*)"Send", 2
  );
  btnSendMessage.drawButton(false);

  btnReceiveMessage.initButton(
    &tft, BTN_RECEIVE_CX, BTN_RECEIVE_CY, BTN_HOME_W, BTN_HOME_H,
    TFT_WHITE, TFT_GREEN, TFT_WHITE, (char*)"Receive", 2
  );
  btnReceiveMessage.drawButton(false);
}

// ================== Draw history page ==================
void drawHistoryPage() {
  tft.fillScreen(TFT_BLACK);
  delay(500);

  // For safety, check dataCount (must be > 3 to safely access dataItems[3])
  if (dataCount < 4) {
    Serial.println("Data count is less than 4, skipping label drawing...");
    return;
  }

  // ========== Timestamp formatting lambda ==========
  auto formatTimestamp = [](const String &ts) {
      // Example: original "2025-03-14T01:10:12.551Z"
      // 1) Extract date part "2025-03-14" (substring [0..10))
      String datePart = ts.substring(0, 10);
      // 2) Extract the first 5 chars of time "01:10" (substring [11..16))
      String timePart = ts.substring(11, 16);
      // 3) Combine into "2025-03-14, 01:10"
      String formatted = datePart + ", " + timePart;
      return formatted;
  };

  // ========== Process label1 ==========
  // e.g. "2025-03-14T01:10:12.551Z" -> "2025-03-14, 01:10"
  String newTime1 = formatTimestamp(dataItems[1].timestamp);
  String label1 = newTime1 + " by " + dataItems[1].name;
  btnHistory1.initButton(
    &tft, BTN1_CX, BTN1_CY, BTN_W, BTN_H,
    TFT_WHITE, TFT_BLUE, TFT_WHITE,
    (char *)label1.c_str(),
    2
  );
  btnHistory1.drawButton(false);

  // ========== label2 ==========
  String newTime2 = formatTimestamp(dataItems[2].timestamp);
  String label2 = newTime2 + " by " + dataItems[2].name;
  btnHistory2.initButton(
    &tft, BTN2_CX, BTN2_CY, BTN_W, BTN_H,
    TFT_WHITE, TFT_GREEN, TFT_WHITE,
    (char *)label2.c_str(),
    2
  );
  btnHistory2.drawButton(false);

  // ========== label3 ==========
  String newTime3 = formatTimestamp(dataItems[3].timestamp);
  String label3 = newTime3 + " by " + dataItems[3].name;
  btnHistory3.initButton(
    &tft, BTN3_CX, BTN3_CY, BTN_W, BTN_H,
    TFT_WHITE, TFT_RED, TFT_WHITE,
    (char *)label3.c_str(),
    2
  );
  btnHistory3.drawButton(false);
}


// ================== Home page interaction ==================
void handleHomePage() {
  uint16_t tx, ty;
  bool touched = tft.getTouch(&tx, &ty);

  btnSendMessage.press(touched && btnSendMessage.contains(tx, ty));
  btnReceiveMessage.press(touched && btnReceiveMessage.contains(tx, ty));

  if (btnSendMessage.justPressed()) {
    btnSendMessage.drawButton(true);
    Serial.println("Send Message Pressed!");
    page = 2;
    tft.fillScreen(TFT_WHITE);
  }
  if (btnSendMessage.justReleased()) {
    btnSendMessage.drawButton(false);
  }

  if (btnReceiveMessage.justPressed()) {
    btnReceiveMessage.drawButton(true);
    Serial.println("Receive Message Pressed! Going to History Page...");
    page = 1;
    drawHistoryPage();
  }
  if (btnReceiveMessage.justReleased()) {
    btnReceiveMessage.drawButton(false);
  }
}

// ================== History page interaction ==================
void handleHistoryPage() {
  uint16_t tx, ty;
  bool touched = tft.getTouch(&tx, &ty);

  btnHistory1.press(touched && btnHistory1.contains(tx, ty));
  btnHistory2.press(touched && btnHistory2.contains(tx, ty));
  btnHistory3.press(touched && btnHistory3.contains(tx, ty));

  if (btnHistory1.justPressed()) {
    btnHistory1.drawButton(true);
    Serial.println("History1 Pressed!");
  }
  if (btnHistory1.justReleased()) {
    btnHistory1.drawButton(false);
  }
  if (btnHistory2.justPressed()) {
    btnHistory2.drawButton(true);
    Serial.println("History2 Pressed!");
  }
  if (btnHistory2.justReleased()) {
    btnHistory2.drawButton(false);
  }
  if (btnHistory3.justPressed()) {
    btnHistory3.drawButton(true);
    Serial.println("History3 Pressed!");
  }
  if (btnHistory3.justReleased()) {
    btnHistory3.drawButton(false);
  }
}

// ================== Drawing page interaction ==================
void handleDrawingPage() {
  uint16_t x, y;
  if (tft.getTouch(&x, &y)) {
    lastTouchTime = millis();

    if (pointIndex < MAX_POINTS) {
      touchPoints[pointIndex][0] = x;
      touchPoints[pointIndex][1] = y;
      pointIndex++;
    }
    if (pointIndex >= 4) {
      uint16_t *p0 = touchPoints[pointIndex - 4];
      uint16_t *p1 = touchPoints[pointIndex - 3];
      uint16_t *p2 = touchPoints[pointIndex - 2];
      uint16_t *p3 = touchPoints[pointIndex - 1];
      drawBezierCurve(p0, p1, p2, p3, TFT_BLACK);
    }
  } 
  else {
    // If finger is lifted off, store this stroke
    if (pointIndex > 0) {
      storeStroke();
      strokeIndex++;
    }
    // If more than INACTIVITY_TIMEOUT has passed, redraw and send data
    if (millis() - lastTouchTime > INACTIVITY_TIMEOUT && strokeCount > 0) {
      redrawScreen();
      lastTouchTime = millis();
      sendStrokeData(); // Send JSON data after 5s inactivity
    }
  }
}

// ================== Bezier curve interpolation ==================
void drawBezierCurve(uint16_t p0[], uint16_t p1[], uint16_t p2[], uint16_t p3[], uint16_t color) {
  for (float t = 0; t <= 1; t += 0.1) {
      float x = (1 - t) * (1 - t) * (1 - t) * p0[0] +
                3 * (1 - t) * (1 - t) * t * p1[0] +
                3 * (1 - t) * t * t * p2[0] +
                t * t * t * p3[0];
      float y = (1 - t) * (1 - t) * (1 - t) * p0[1] +
                3 * (1 - t) * (1 - t) * t * p1[1] +
                3 * (1 - t) * t * t * p2[1] +
                t * t * t * p3[1];
      tft.fillCircle(x, y, thickness, color);
  }
}

// ================== Store a stroke ==================
void storeStroke() {
  if (pointIndex == 0) return;
  if (strokeCount < MAX_STROKES) {
    memcpy(strokes[strokeCount].points, touchPoints, pointIndex * sizeof(touchPoints[0]));
    strokes[strokeCount].pointCount = pointIndex;
    strokeCount++;
  }
  pointIndex = 0; 
}

// ================== Generate JSON and send via WebSocket ==================
void sendStrokeData() {
  if (strokeCount == 0) return; // Do nothing if no stroke data

  StaticJsonDocument<4096> jsonDoc;
  jsonDoc["name"] = "Sirui";          // Customize the sender
  jsonDoc["personal_color"] = "Red";  // Customize the color

  JsonArray strokesArray = jsonDoc.createNestedArray("strokes");
  for (uint8_t i = 0; i < strokeCount; i++) {
    JsonObject strokeObj = strokesArray.createNestedObject();
    strokeObj["stroke"] = i + 1;
    JsonArray pointsArray = strokeObj.createNestedArray("points");
    for (uint8_t j = 0; j < strokes[i].pointCount; j++) {
      JsonObject pointObj = pointsArray.createNestedObject();
      pointObj["x"] = strokes[i].points[j][0];
      pointObj["y"] = strokes[i].points[j][1];
    }
  }

  String jsonString;
  serializeJson(jsonDoc, jsonString);
  if (wsConnected) {
    client.send(jsonString);
    Serial.println("📤 JSON Sent: " + jsonString);
  } else {
    Serial.println("❌ WebSocket Not Connected, Cannot Send Data");
  }
}

// ================== Redraw all strokes ==================
void redrawScreen() {
  tft.fillScreen(TFT_WHITE);
  delay(50);
  for (uint8_t i = 0; i < strokeCount; i++) {
    if (strokes[i].pointCount >= 4) {
      for (uint8_t j = 0; j <= strokes[i].pointCount - 4; j++) {
        drawBezierCurve(
          strokes[i].points[j],
          strokes[i].points[j + 1],
          strokes[i].points[j + 2],
          strokes[i].points[j + 3],
          TFT_BLACK
        );
        delay(25);
      }
    } else {
      // If fewer than 4 points, use simple lines
      for (uint8_t j = 0; j < strokes[i].pointCount - 1; j++) {
        tft.drawLine(
          strokes[i].points[j][0],   strokes[i].points[j][1],
          strokes[i].points[j+1][0], strokes[i].points[j+1][1],
          TFT_BLACK
        );
      }
    }
  }
}

// ================== LED strip effects ==================
uint32_t Wheel(byte WheelPos) {
  // Given a 0-255 position, return a rainbow color
  if(WheelPos < 85) {
    return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
  } else if(WheelPos < 170) {
    WheelPos -= 85;
    return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  } else {
    WheelPos -= 170;
    return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
}

// Home page LED rainbow flow (non-blocking update)
void updateLEDHome() {
  static unsigned long lastLEDUpdate = 0;
  static uint16_t j = 0;
  unsigned long currentMillis = millis();
  if (currentMillis - lastLEDUpdate >= 50) {
    lastLEDUpdate = currentMillis;
    for (uint16_t i = 0; i < LED_COUNT; i++) {
      uint32_t color = Wheel((i * 256 / LED_COUNT + j) & 255);
      strip.setPixelColor(i, color);
    }
    strip.show();
    j++;
  }
}

// History page LED "theater chase" effect (blue brightness fade)
void updateLEDHistory() {
  static unsigned long lastUpdate = 0;
  static int brightness = 0;
  static int direction = 1;
  unsigned long currentMillis = millis();
  if (currentMillis - lastUpdate >= 20) {
    lastUpdate = currentMillis;
    brightness += direction;
    if (brightness <= 0 || brightness >= 50) {
      direction = -direction;
    }
    for (uint16_t i = 0; i < LED_COUNT; i++){
      uint8_t r = 0;
      uint8_t g = 0;
      uint8_t b = 255;
      strip.setPixelColor(i, strip.Color(r, g, b));
    }
    strip.setBrightness(brightness);
    strip.show();
  }
}

// Drawing page LED breathing effect (smooth purple brightness changes)
void updateLEDDrawing() {
  static unsigned long lastUpdate = 0;
  static int brightness = 0;
  static int direction = 1;
  unsigned long currentMillis = millis();
  if (currentMillis - lastUpdate >= 20) {
    lastUpdate = currentMillis;
    brightness += direction;
    if (brightness <= 0 || brightness >= 50) {
      direction = -direction;
    }
    // Use purple (r,g,b depending on needs)
    for (uint16_t i = 0; i < LED_COUNT; i++){
      uint8_t r = 255;
      uint8_t g = 255;
      uint8_t b = 255;
      strip.setPixelColor(i, strip.Color(r, g, b));
    }
    strip.setBrightness(brightness);
    strip.show();
  }
}

// Choose the LED update function based on the current page
void updateLEDs() {
  if (page == 0) {
    updateLEDHome();
  } else if (page == 1) {
    updateLEDHistory();
  } else if (page == 2) {
    updateLEDDrawing();
  }
}
