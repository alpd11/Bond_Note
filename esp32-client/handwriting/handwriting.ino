#include <ArduinoWebsockets.h>
#include <WiFi.h>

const char* ssid = "CE-Hub-Student";                                        // WiFi SSID
const char* password = "casa-ce-gagarin-public-service";                    // WiFi Password
const char* websockets_server_host = "hmltry1-1e722cd26d62.herokuapp.com";  // WebSocket Address
const uint16_t websockets_server_port = 80;                                 // WebSocket Port

using namespace websockets;

WebsocketsClient client;
unsigned long lastSendTime = 0;  // Record the time of the last dispatch

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);

  // Waiting for WiFi connection
  for (int i = 0; i < 10 && WiFi.status() != WL_CONNECTED; i++) {
  Serial.print(".");
  delay(1000);
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ No Wifi!");
    return;
  }

  Serial.println("\n✅ Connected to Wifi, Connecting to server...");

  // Connect WebSocket Server
  bool connected = client.connect(websockets_server_host, websockets_server_port, "/");
  if (connected) {
    Serial.println("✅ WebSocket Connected!");
    client.send("Hello Server");
  } else {
    Serial.println("❌ WebSocket Not Connected!");
  }

  // Habdle messages received from WebSocket
  client.onMessage([&](WebsocketsMessage message) {
    Serial.print("📩 Got Message: ");
    Serial.println(message.data());
  });
}

void loop() {
  // Let the WebSocket client check the message
  if (client.available()) {
  client.poll();
  }

  // A message is sent every 1 second
  if (millis() - lastSendTime >= 1000) {
  lastSendTime = millis();
  String message = "ESP32 发送的数据: " + String(millis());
  Serial.println("📤 发送消息: " + message);
  client.send(message);
  }

  delay(10);  // reduce CPU load
}
