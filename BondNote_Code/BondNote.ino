#include <TFT_eSPI.h>
#include <SPI.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <ArduinoWebsockets.h>
#include <Adafruit_NeoPixel.h>  
#include <HTTPClient.h>             
#include "Free_Fonts.h" 

using namespace websockets;

// **WiFi and WebSocket Configuration**

const char* ssid = "";      // Your WiFi SSID
const char* password = "";  // Your WiFi Password

// WebSocket server
const char* websockets_server_host = "bond-note-c746823b71f7.herokuapp.com";
const uint16_t websockets_server_port = 80;

WebsocketsClient client;
bool wsConnected = false; // WebSocket connection status
unsigned long lastPoll = 0;

// Create TFT_eSPI object
TFT_eSPI tft = TFT_eSPI();

// Two buttons on the home page
TFT_eSPI_Button btnSendMessage;
TFT_eSPI_Button btnReceiveMessage;

// Three buttons on the history messages page
TFT_eSPI_Button btnHistory1;
TFT_eSPI_Button btnHistory2;
TFT_eSPI_Button btnHistory3;

// Fixed known touch calibration data (example values)
uint16_t calData[5] = { 309, 3475, 243, 3519, 1 };

// Screen width and height
#define SCREEN_WIDTH  480
#define SCREEN_HEIGHT 320

// Width and height for each button
#define BTN_W SCREEN_WIDTH
#define BTN_H (SCREEN_HEIGHT / 3)

// Center coordinates for the three history buttons
#define BTN1_CX (SCREEN_WIDTH / 2)
#define BTN1_CY (BTN_H / 2)
#define BTN2_CX (SCREEN_WIDTH / 2)
#define BTN2_CY (BTN_H + BTN_H / 2)
#define BTN3_CX (SCREEN_WIDTH / 2)
#define BTN3_CY (2 * BTN_H + BTN_H / 2)

// Width and height for home page buttons
#define BTN_HOME_W (SCREEN_WIDTH / 4)
#define BTN_HOME_H (SCREEN_HEIGHT / 4)

// Center coordinates for home page buttons
#define BTN_SEND_CX (SCREEN_WIDTH / 4)
#define BTN_SEND_CY (SCREEN_HEIGHT * 3 / 4)
#define BTN_RECEIVE_CX (3 * SCREEN_WIDTH / 4)
#define BTN_RECEIVE_CY (SCREEN_HEIGHT * 3 / 4)

#define MAX_POINTS 100  
#define MAX_STROKES 100 
#define INACTIVITY_TIMEOUT 20000 // 5 seconds of no touch triggers redraw

// LED strip configuration (adjust LED_PIN based on actual wiring)
#define LED_PIN    5   
#define LED_COUNT  45  

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// ============ New: hardware button definitions ============
const int buttonPin12 = 12;  // GPIO12, using internal pull-up
const int buttonPin13 = 13;  // GPIO13, using internal pull-up

// ============ Variables for the writing page ============
uint16_t touchPoints[MAX_POINTS][2]; 
uint8_t pointIndex = 0;              
uint8_t thickness = 2;               
uint8_t strokeIndex = 1;             
unsigned long lastTouchTime = 0;     

struct Stroke {
  uint16_t points[MAX_POINTS][2];
  uint8_t pointCount;            
};
Stroke strokes[MAX_STROKES]; 
uint8_t strokeCount = 0;     

// ============ Page flags: 0-home, 1-history, 2-writing, 3-history strokes display ============
int page = 0;

// ========== Structure to store historical info fetched from the server ========== 
struct DataItem {
  String name;
  String timestamp;
};

#define MAX_DATA_ITEMS 20
DataItem dataItems[MAX_DATA_ITEMS];
int dataCount = 0;

// Refresh data every 5 seconds
unsigned long lastDataUpdate = 0;

// ========== History page pagination ==========
int historyStartIndex = 0;          // Current start index of the history list (0-based)
int buttonIndices[3] = {-1, -1, -1}; // Records the indices in dataItems for the 3 buttons

// ========== New: used to distinguish LED color on the home page ==========
String currentName = "None"; // Used to record the last "name" received from WebSocket

// ================ Function Declarations ================
void drawHomePage();
void drawHistoryPage();
void handleHomePage();
void handleHistoryPage();
void handleDrawingPage();
void handleRedrawPage(); // new

void setupWiFiAndWebSocket();
void fetchRemoteData();

uint32_t Wheel(byte WheelPos);
void updateLEDHome();
void updateLEDHistory();
void updateLEDDrawing();
void updateLEDKimi();
void updateLEDJennie();
void updateLEDs();

void sendStrokeData();
void redrawScreen();
void drawBezierCurve(uint16_t p0[], uint16_t p1[], uint16_t p2[], uint16_t p3[], uint16_t color);
void storeStroke();
void fetchAndRedraw(const String& timestamp);
void drawLoadingScreen();

void setup() {
  Serial.begin(115200);

  // Hardware buttons: internal pull-up
  pinMode(buttonPin12, INPUT_PULLUP);
  pinMode(buttonPin13, INPUT_PULLUP);

  // First connect to WiFi & WebSocket
  setupWiFiAndWebSocket();

  // Initialize screen
  tft.init();
  tft.setRotation(3);
  tft.setTouch(calData);
  tft.fillScreen(TFT_BLACK);

  // Initialize LED
  strip.begin();
  strip.show();
  strip.setBrightness(40); // Lower brightness to avoid glare

  // Draw the home page
  drawHomePage();
}

void loop() {
  // 1) Regardless of which page, periodically poll WebSocket
  if (millis() - lastPoll > 100 && wsConnected) {
    client.poll();
    lastPoll = millis();
  }

  // 2) If wsConnected == false, automatically attempt reconnection
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
    handleHomePage();
  } 
  else if (page == 1) {
    handleHistoryPage();
  } 
  else if (page == 2) {
    handleDrawingPage();
  }
  else if (page == 3) {
    handleRedrawPage();
  }

  // 4) Update LEDs based on the current page
  updateLEDs();

  // 5) Periodically perform an HTTP GET to fetch data (only refresh when on home page)
  /*
  if (millis() - lastDataUpdate >= 20000 && page == 0) {
    lastDataUpdate = millis();
    fetchRemoteData();
  }
  */
}

// ================ WebSocket & WiFi Initialization ================
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

  // 3. Set up callbacks
  client.onMessage([&](WebsocketsMessage message) {
      Serial.println("📩 WebSocket Received: " + message.data());

      // Attempt to parse JSON
      StaticJsonDocument<2048> doc;
      DeserializationError err = deserializeJson(doc, message.data());
      if (!err) {
        // If parsing is successful and "name" field exists
        if (doc.containsKey("name")) {
          String incomingName = doc["name"].as<String>();
          // Based on different "name", record to global variable currentName
          if (incomingName == "Jennie") {
            currentName = "Jennie";
            Serial.println("-> Detected name: Jennie");
            fetchRemoteData();
          }
          else if (incomingName == "Kimi") {
            currentName = "Kimi";
            Serial.println("-> Detected name: Kimi");
            fetchRemoteData();
          }
          else {
            // If neither Jennie nor Kimi, you can choose a default
            currentName = "None";
            Serial.println("-> Detected name: " + incomingName + " (ignored)");
          }
        }
      }
      else {
        // JSON parsing failed, do nothing
        Serial.println("❌ JSON parse error in WebSocket data!");
      }
  });

  client.onEvent([](WebsocketsEvent event, String data) {
      if (event == WebsocketsEvent::ConnectionClosed) {
          Serial.println("❌ WebSocket Connection Closed!");
          wsConnected = false; 
      }
  });
}

// ================ HTTP GET to fetch remote data ================
void fetchRemoteData() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin("http://bond-note-c746823b71f7.herokuapp.com/data");
    int httpCode = http.GET(); // Send GET request

    if (httpCode > 0) {
      if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        Serial.println("HTTP GET response:");
        Serial.println(payload);

        StaticJsonDocument<2048> doc;
        DeserializationError error = deserializeJson(doc, payload);
        if (!error) {
          JsonArray arr = doc.as<JsonArray>();
          dataCount = 0;
          for (JsonObject obj : arr) {
            if (dataCount >= MAX_DATA_ITEMS) break; 
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
      Serial.printf("❌ GET failed, error: %s\n", http.errorToString(httpCode).c_str());
    }
    http.end(); 
  } else {
    Serial.println("❌ WiFi not connected!");
  }
}

// ================ Draw the home page ================
void drawHomePage() {
  fetchRemoteData();

  tft.setTextSize(1);
  tft.fillScreen(TFT_BLACK);

  tft.setFreeFont(FSBI24);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  int16_t stringWidth = tft.textWidth("BondNote", GFXFF);
  int16_t xpos = (SCREEN_WIDTH - stringWidth) / 2;
  int16_t ypos = 40;  
  tft.drawString("BondNote", xpos, ypos, GFXFF);

  tft.setFreeFont(FSBI9);
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

  tft.setFreeFont(NULL);
  tft.setTextFont(2);

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

// ================ Draw the history page ================
void drawHistoryPage() {
  tft.fillScreen(TFT_BLACK);

  // A small function to format time
  auto formatTimestamp = [](const String &ts) -> String {
    // For example "2025-03-14T01:10:12.551Z" -> "2025-03-14, 01:10"
    if (ts.length() < 16) return ts; 
    String datePart = ts.substring(0, 10);
    String timePart = ts.substring(11, 16);
    return datePart + String(", ") + timePart;
  };

  // Before each redraw, clear buttonIndices
  for (int i = 0; i < 3; i++) {
    buttonIndices[i] = -1;
  }

  // Generate three rows of buttons corresponding to historyStartIndex through historyStartIndex+2
  for (int i = 0; i < 3; i++) {
    int itemIndex = historyStartIndex + i;

    // If out of range, show “-- No Data --”
    if (itemIndex >= dataCount) {
      switch (i) {
        case 0:
          btnHistory1.initButton(
            &tft, BTN1_CX, BTN1_CY, BTN_W, BTN_H,
            TFT_WHITE, TFT_DARKGREY, TFT_WHITE,
            (char*)"-- No Data --", 2
          );
          btnHistory1.drawButton(false);
          break;
        case 1:
          btnHistory2.initButton(
            &tft, BTN2_CX, BTN2_CY, BTN_W, BTN_H,
            TFT_WHITE, TFT_DARKGREY, TFT_WHITE,
            (char*)"-- No Data --", 2
          );
          btnHistory2.drawButton(false);
          break;
        case 2:
          btnHistory3.initButton(
            &tft, BTN3_CX, BTN3_CY, BTN_W, BTN_H,
            TFT_WHITE, TFT_DARKGREY, TFT_WHITE,
            (char*)"-- No Data --", 2
          );
          btnHistory3.drawButton(false);
          break;
      }
    } 
    else {
      // Based on itemIndex, get the corresponding name/time info
      String userName = dataItems[itemIndex].name;
      String timeStr  = formatTimestamp(dataItems[itemIndex].timestamp);
      String label    = timeStr + " by " + userName;

      // ====== Button color based on username ======
      uint16_t fillColor;
      if (userName == "Jennie") {
        // Bright yellow (can be adjusted)
        fillColor = tft.color565(255, 255, 0);
      }
      else if (userName == "Kimi") {
        // Bright green (can be adjusted)
        fillColor = tft.color565(0, 255, 0);
      }
      else {
        // If neither Jennie nor Kimi, use a default color. 
        // You could also assign based on i, for example.
        switch(i) {
          case 0: fillColor = TFT_BLUE;  break;
          case 1: fillColor = TFT_GREEN; break;
          case 2: fillColor = TFT_RED;   break;
        }
      }

      // ====== Generate buttons ======
      switch (i) {
        case 0:
          btnHistory1.initButton(
            &tft, BTN1_CX, BTN1_CY, BTN_W, BTN_H,
            TFT_WHITE, fillColor, TFT_WHITE,
            (char *)label.c_str(), 2
          );
          btnHistory1.drawButton(false);
          buttonIndices[0] = itemIndex;
          break;

        case 1:
          btnHistory2.initButton(
            &tft, BTN2_CX, BTN2_CY, BTN_W, BTN_H,
            TFT_WHITE, fillColor, TFT_WHITE,
            (char *)label.c_str(), 2
          );
          btnHistory2.drawButton(false);
          buttonIndices[1] = itemIndex;
          break;

        case 2:
          btnHistory3.initButton(
            &tft, BTN3_CX, BTN3_CY, BTN_W, BTN_H,
            TFT_WHITE, fillColor, TFT_WHITE,
            (char *)label.c_str(), 2
          );
          btnHistory3.drawButton(false);
          buttonIndices[2] = itemIndex;
          break;
      }
    }
  }
}


// ================ Handle interactions on the home page ================
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

    // Reset page index to 0 before going to history page
    currentName = "Sirui";
    historyStartIndex = 0;
    page = 1;
    drawHistoryPage();
  }
  if (btnReceiveMessage.justReleased()) {
    btnReceiveMessage.drawButton(false);
  }
}

// ================ Handle interactions on the history page ================
void handleHistoryPage() {
  uint16_t tx, ty;
  bool touched = tft.getTouch(&tx, &ty);

  btnHistory1.press(touched && btnHistory1.contains(tx, ty));
  btnHistory2.press(touched && btnHistory2.contains(tx, ty));
  btnHistory3.press(touched && btnHistory3.contains(tx, ty));

  if (btnHistory1.justPressed()) {
    btnHistory1.drawButton(true);
    if (buttonIndices[0] >= 0 && buttonIndices[0] < dataCount) {
      Serial.println("History1 Pressed! -> index " + String(buttonIndices[0]));
      page = 3;
      fetchAndRedraw(dataItems[buttonIndices[0]].timestamp);
    }
  }
  if (btnHistory1.justReleased()) {
    btnHistory1.drawButton(false);
  }

  if (btnHistory2.justPressed()) {
    btnHistory2.drawButton(true);
    if (buttonIndices[1] >= 0 && buttonIndices[1] < dataCount) {
      Serial.println("History2 Pressed! -> index " + String(buttonIndices[1]));
      page = 3;
      fetchAndRedraw(dataItems[buttonIndices[1]].timestamp);
    }
  }
  if (btnHistory2.justReleased()) {
    btnHistory2.drawButton(false);
  }

  if (btnHistory3.justPressed()) {
    btnHistory3.drawButton(true);
    if (buttonIndices[2] >= 0 && buttonIndices[2] < dataCount) {
      Serial.println("History3 Pressed! -> index " + String(buttonIndices[2]));
      page = 3;
      fetchAndRedraw(dataItems[buttonIndices[2]].timestamp);
    }
  }
  if (btnHistory3.justReleased()) {
    btnHistory3.drawButton(false);
  }

  // ======== New: detect D12 and D13 on the history page to control pagination ========
  if (digitalRead(buttonPin12) == LOW) {
    // D12: next page
    Serial.println("D12 Pressed -> Next Page of History");
    if (historyStartIndex + 3 < dataCount) {
      historyStartIndex += 3;
      drawHistoryPage();
    } else {
      Serial.println("Already at last page, no further data.");
    }
    delay(300); // Debounce
  }

  if (digitalRead(buttonPin13) == LOW) {
    // D13: previous page / or go back to home
    Serial.println("D13 Pressed -> Prev Page or Home");
    if (historyStartIndex == 0) {
      page = 0;
      drawLoadingScreen();
      drawHomePage();
    } else {
      historyStartIndex -= 3;
      if (historyStartIndex < 0) {
        historyStartIndex = 0; 
      }
      drawHistoryPage();
    }
    delay(300); // Debounce
  }
}

// ================ Handle interactions on the writing page ================
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
    // Finger lifted, store stroke
    if (pointIndex > 0) {
      storeStroke();
      strokeIndex++;
    }
  }

  // Detect button presses
  if (digitalRead(buttonPin12) == LOW) {
    Serial.println("D12 Pressed -> Send Data & Go Home");
    // 1) Send stroke data
    sendStrokeData();

    // 2) Reset arrays
    memset(touchPoints, 0, sizeof(touchPoints));
    memset(strokes, 0, sizeof(strokes));

    // 3) Other reset operations
    page = 0;          
    strokeCount = 0;  
    pointIndex = 0;   
    strokeIndex = 1;   
    
    // 4) Return to home page and debounce
    drawLoadingScreen();
    drawHomePage();   
  }

  if (digitalRead(buttonPin13) == LOW) {
    Serial.println("D13 Pressed -> Clear Strokes");
    memset(touchPoints, 0, sizeof(touchPoints));
    memset(strokes, 0, sizeof(strokes));
    strokeCount = 0;  
    pointIndex = 0; 
    strokeIndex = 1;   
    tft.fillScreen(TFT_WHITE);
    delay(300);       
  }
}

// ================ Handle interactions on the history stroke redraw page ================
void handleRedrawPage(){
  if (digitalRead(buttonPin12) == LOW) {
    Serial.println("D12 Pressed -> Go Home");
    memset(touchPoints, 0, sizeof(touchPoints));
    memset(strokes, 0, sizeof(strokes));

    page = 0;          
    strokeCount = 0;  
    pointIndex = 0;   
    strokeIndex = 1;   
    
    drawLoadingScreen();  
    drawHomePage();        
  }

  if (digitalRead(buttonPin13) == LOW) {
    Serial.println("D13 Pressed -> Clear & Go Home");
    memset(touchPoints, 0, sizeof(touchPoints));
    memset(strokes, 0, sizeof(strokes));

    page = 0;          
    strokeCount = 0;  
    pointIndex = 0;   
    strokeIndex = 1;   
    drawLoadingScreen();
    drawHomePage();      
  }
}

// ================ Bezier curve interpolation drawing ================
void drawBezierCurve(uint16_t p0[], uint16_t p1[], uint16_t p2[], uint16_t p3[], uint16_t color) {
  for (float t = 0; t <= 1; t += 0.1) {
    float x = (1 - t)*(1 - t)*(1 - t)*p0[0] +
              3*(1 - t)*(1 - t)*t*p1[0] +
              3*(1 - t)*t*t*p2[0] +
              t*t*t*p3[0];
    float y = (1 - t)*(1 - t)*(1 - t)*p0[1] +
              3*(1 - t)*(1 - t)*t*p1[1] +
              3*(1 - t)*t*t*p2[1] +
              t*t*t*p3[1];
    tft.fillCircle(x, y, thickness, color);
  }
}

// ================ Store a stroke ================
void storeStroke() {
  if (pointIndex == 0) return;
  if (strokeCount < MAX_STROKES) {
    memcpy(strokes[strokeCount].points, touchPoints, pointIndex * sizeof(touchPoints[0]));
    strokes[strokeCount].pointCount = pointIndex;
    strokeCount++;
  }
  pointIndex = 0; 
}

// ================ Generate JSON and send via WebSocket ================
void sendStrokeData() {
  if (strokeCount == 0) {
    Serial.println("No strokes to send, skipping...");
    return;
  }

  StaticJsonDocument<4096> jsonDoc;
  jsonDoc["name"] = "Jennie";         // Change to your name if desired
  jsonDoc["personal_color"] = "Yellow";

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

// ================ Redraw all strokes ================
void redrawScreen() {
  tft.fillScreen(TFT_WHITE);
  delay(50);
  for (uint8_t i = 0; i < strokeCount; i++) {
    if (strokes[i].pointCount >= 4) {
      for (uint8_t j = 0; j <= strokes[i].pointCount - 4; j++) {
        drawBezierCurve(
          strokes[i].points[j],
          strokes[i].points[j+1],
          strokes[i].points[j+2],
          strokes[i].points[j+3],
          TFT_BLACK
        );
        delay(25);
      }
    } else {
      // If points are fewer than 4, use simple line
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

// ================ Fetch and redraw strokes based on a timestamp ================
void fetchAndRedraw(const String& timestamp) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ WiFi not connected!");
    return;
  }
  String url = "http://bond-note-c746823b71f7.herokuapp.com/data/" + timestamp;
  HTTPClient http;
  http.begin(url);
  Serial.println("🔗 Requesting strokes from: " + url);

  int httpCode = http.GET();
  if (httpCode > 0) {
    if (httpCode == HTTP_CODE_OK) {
      tft.fillScreen(TFT_WHITE);
      delay(50);
      String payload = http.getString();
      Serial.println("HTTP GET response:");
      Serial.println(payload);

      StaticJsonDocument<2048> doc;  
      DeserializationError error = deserializeJson(doc, payload);
      if (!error) {
        JsonArray strokesArray = doc["strokes"];
        if (!strokesArray.isNull()) {
          strokeCount = 0;
          for (JsonObject strokeObj : strokesArray) {
            if (strokeCount >= MAX_STROKES) break;
            uint8_t sIndex = strokeCount++;
            JsonArray pointsArray = strokeObj["points"];
            uint8_t pIndex = 0;
            for (JsonObject pointObj : pointsArray) {
              if (pIndex >= MAX_POINTS) break;
              strokes[sIndex].points[pIndex][0] = pointObj["x"] | 0;
              strokes[sIndex].points[pIndex][1] = pointObj["y"] | 0;
              pIndex++;
            }
            strokes[sIndex].pointCount = pIndex;
          }
          Serial.println("✅ Stroke data updated, now redrawing...");
          redrawScreen();
        }
        else {
          Serial.println("❌ 'strokes' array not found in JSON!");
        }
      } else {
        Serial.println("❌ JSON parse error: " + String(error.c_str()));
      }
    } else {
      Serial.printf("❌ HTTP GET... code: %d\n", httpCode);
    }
  } else {
    Serial.printf("❌ GET failed, error: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();
}

// ================ LED strip effects ================
uint32_t Wheel(byte WheelPos) {
  if(WheelPos < 85) {
    return strip.Color(WheelPos*3, 255 - WheelPos*3, 0);
  } else if(WheelPos < 170) {
    WheelPos -= 85;
    return strip.Color(255 - WheelPos*3, 0, WheelPos*3);
  } else {
    WheelPos -= 170;
    return strip.Color(0, WheelPos*3, 255 - WheelPos*3);
  }
}

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
    strip.setBrightness(40);
    strip.show();
    j++;
  }
}

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
      strip.setPixelColor(i, strip.Color(0, 0, 255));
    }
    strip.setBrightness(brightness);
    strip.show();
  }
}

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
    for (uint16_t i = 0; i < LED_COUNT; i++){
      strip.setPixelColor(i, strip.Color(255, 255, 255));
    }
    strip.setBrightness(brightness);
    strip.show();
  }
}

void updateLEDKimi() {
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
      strip.setPixelColor(i, strip.Color(255, 255, 255));
    }
    strip.setBrightness(brightness);
    strip.show();
  }
}

void updateLEDJennie() {
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
      // Setting color to yellow (255, 255, 0). You can adjust it if needed.
      strip.setPixelColor(i, strip.Color(255, 255, 0));
    }
    strip.setBrightness(brightness);
    strip.show();
  }
}

// ***** NEW or MODIFIED *****
// Choose corresponding LED update function based on current page and currentName
void updateLEDs() {
  if (page == 0) {
    // On home page, decide if it is Jennie or Kimi
    if (currentName == "Jennie") {
      updateLEDJennie();
    } 
    else if (currentName == "Kimi") {
      updateLEDKimi();
    } 
    else {
      // If neither Jennie nor Kimi, use the default rainbow for home page
      updateLEDHome();
    }
  } else if (page == 1) {
    updateLEDHistory();
  } else if (page == 2) {
    updateLEDDrawing();
  }
}

// Show "Loading..." in the center of the screen
void drawLoadingScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setFreeFont(FSB9);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  String loadingText = "Loading...";
  int16_t textWidth = tft.textWidth(loadingText, GFXFF);
  int16_t textHeight = tft.fontHeight(GFXFF);
  int16_t x = (SCREEN_WIDTH - textWidth) / 2;
  int16_t y = (SCREEN_HEIGHT - textHeight) / 2;
  tft.drawString(loadingText, x, y, GFXFF);
}
