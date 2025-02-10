/*
  Sketch to generate the setup() calibration values, these are reported
  to the Serial Monitor.

  The sketch has been tested on the ESP8266 and screen with XPT2046 driver.
*/

#include <SPI.h>
#include <TFT_eSPI.h>      // Hardware-specific library

TFT_eSPI tft = TFT_eSPI(); // Invoke custom library

//------------------------------------------------------------------------------------------

void setup() {
  // Use serial port
  Serial.begin(115200);

  // Initialise the TFT screen
  tft.init();

  // Set the rotation to the orientation you wish to use in your project before calibration
  // (the touch coordinates returned then correspond to that rotation only)
  tft.setRotation(1);

  // Calibrate the touch screen and retrieve the scaling factors
  touch_calibrate();

/*
  // Replace above line with the code sent to Serial Monitor
  // once calibration is complete, e.g.:
  uint16_t calData[5] = { 286, 3534, 283, 3600, 6 };
  tft.setTouch(calData);
*/

  // Clear the screen
  tft.fillScreen(TFT_BLACK);
  tft.drawCentreString("Touch screen to test!",tft.width()/2, tft.height()/2, 2);
}

//------------------------------------------------------------------------------------------
void loop() {
  static uint16_t last_x = 0, last_y = 0;
  static bool touchedPreviously = false;  // Track if touch was continuous
  uint16_t x = 0, y = 0;
  uint8_t thickness = 3; // Adjust thickness (higher value = thicker lines)

  if (tft.getTouch(&x, &y)) {  
    // Print coordinates continuously to Serial Monitor
    Serial.print("Touch detected at: X = ");
    Serial.print(x);
    Serial.print(", Y = ");
    Serial.println(y);
    
    if (touchedPreviously && (abs(x - last_x) > 2 || abs(y - last_y) > 2)) {  
      for (int i = -thickness; i <= thickness; i++) {
        tft.drawLine(last_x + i, last_y, x + i, y, TFT_WHITE); // Horizontal shift
        tft.drawLine(last_x, last_y + i, x, y + i, TFT_WHITE); // Vertical shift
      }
    }
    
    last_x = x;
    last_y = y;
    touchedPreviously = true; // Mark as continuous touch
  } else {
    touchedPreviously = false; // Reset when the touch is lifted
  }
}

//------------------------------------------------------------------------------------------

// Code to run a screen calibration, not needed when calibration values set in setup()
void touch_calibrate()
{
  uint16_t calData[5];
  uint8_t calDataOK = 0;

  // Calibrate
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(20, 0);
  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  tft.println("Touch corners as indicated");

  tft.setTextFont(1);
  tft.println();

  tft.calibrateTouch(calData, TFT_MAGENTA, TFT_BLACK, 15);

  Serial.println(); Serial.println();
  Serial.println("// Use this calibration code in setup():");
  Serial.print("  uint16_t calData[5] = ");
  Serial.print("{ ");

  for (uint8_t i = 0; i < 5; i++)
  {
    Serial.print(calData[i]);
    if (i < 4) Serial.print(", ");
  }

  Serial.println(" };");
  Serial.print("  tft.setTouch(calData);");
  Serial.println(); Serial.println();

  tft.fillScreen(TFT_BLACK);
  
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.println("Calibration complete!");
  tft.println("Calibration code sent to Serial port.");

  delay(4000);
}

