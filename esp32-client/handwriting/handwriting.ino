#include <WiFi.h>
#include <WebSocketsClient.h>
#include <secret.h>

WebSocketsClient webSocket;

void webSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.println("Disconnected!");
      break;
    case WStype_CONNECTED:
      Serial.println("Connected to server");
      break;
    case WStype_TEXT:
      Serial.printf("Received: %s\n", payload);
      break;
  }
}

void setup() {
  Serial.begin(115200);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while(WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected");

  webSocket.beginSSL(WS_SERVER, WS_PORT, "/");
  webSocket.onEvent(webSocketEvent);
}

void loop() {
  webSocket.loop();
  
  if(Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    webSocket.sendTXT(input);
  }
}