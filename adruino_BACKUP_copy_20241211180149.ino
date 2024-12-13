#include <EEPROM.h>
#include <Firebase_ESP_Client.h>
#include <LiquidCrystal_I2C.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <ESP8266WiFi.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include <SoftwareSerial.h>
#include <PZEM004Tv30.h>

// PZEM
SoftwareSerial mySerial(14, 12); // Rx, Tx
PZEM004Tv30 pzem1(mySerial);

// Relay
#define RELAY_PIN D4
#define RELAY_PIN1 D7
#define RELAY_PIN2 D8
#define EEPROM_SIZE 1 // Use 1 byte to store the relay state

// Define pins and objects
#define ONE_WIRE_BUS D3
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
LiquidCrystal_I2C lcd(0x27, 16, 2);

// WiFi credentials
#define WIFI_SSID "Dwensland 2.4Ghz"
#define WIFI_PASSWORD "globe1234"

// Firebase credentials
#define DATABASE_URL "https://smart-circuit-breaker-c0350-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define API_KEY "AIzaSyDOGdMZyslKqvjX5qmXRjlaJAzlJ2licsE"

// WiFi client
WiFiClient client;

// Firebase objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

unsigned long sendDataPrevMillis = 0;
bool isSignedUp = false;
unsigned long relayOnTime = 0;
bool relayState = true;
bool isRelayTurningOn = true;

unsigned long lcdUpdateInterval = 2000; // Interval for LCD updates
unsigned long lastLCDUpdateTime = 0;    // Tracks when the last LCD update occurred

void setup() {
  // Initialize LCD and sensors
  sensors.begin();
  lcd.init();
  lcd.backlight();

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(RELAY_PIN1, OUTPUT);
  pinMode(RELAY_PIN2, OUTPUT);
  EEPROM.begin(EEPROM_SIZE);

  // Initialize Serial
  Serial.begin(115200);
  mySerial.begin(9600);

  // Connect to WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" Connected!");
  Serial.println("IP Address: ");
  Serial.println(WiFi.localIP());

  // Configure Firebase
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  config.token_status_callback = tokenStatusCallback;  // Monitor token status

  // Attempt to sign up for Firebase
  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("Firebase SignUp Successful");
    isSignedUp = true;
  } else {
    Serial.printf("Firebase SignUp Failed: %s\n", config.signer.signupError.message.c_str());
    isSignedUp = false;
  }

  // Initialize Firebase
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  Serial.println("PZEM004T Testing Started");

  digitalWrite(RELAY_PIN, 1); // Turn the relay OFF
  digitalWrite(RELAY_PIN1, 1);
  digitalWrite(RELAY_PIN2, 1);
  EEPROM.write(0, 1);         // Save state as 1 in EEPROM
  EEPROM.commit();
  Serial.println("Relay initialized to OFF (1) and saved to EEPROM.");
}

void loop() {
  static unsigned long lastUpdateTime1 = 0;
  static unsigned long lastUpdateTime2 = 0;
  static unsigned long lastUpdateTime = 0;
  static unsigned long lastFirebaseCheckTime = 0;
  const unsigned long updateInterval = 2000; // Interval for checking Firebase and sending data
  unsigned long currentTime = millis();
  const unsigned long firebaseFetchInterval = 10000; // Fetch every 10 seconds
  static unsigned long lastFetchTime = 0;

  // Read temperature and power data from sensors
  sensors.requestTemperatures();
  float tempC = sensors.getTempCByIndex(0);
  float voltage = pzem1.voltage();
  float current = pzem1.current();
  float power = pzem1.power();
  float energy = pzem1.energy(); // chose no to put pzem1.energy/1000 for demonstration purposes :kWh
  float frequency = pzem1.frequency();

 if (tempC > 38.0) {
    digitalWrite(RELAY_PIN, 1); // Turn relay OFF
    digitalWrite(RELAY_PIN1, 1);
    digitalWrite(RELAY_PIN2, 1);
    Serial.println("Temperature exceeded 38°C. Relay turned OFF.");

    // Set /mainSwitch to 1 in Firebase
    if (Firebase.RTDB.setString(&fbdo, "/mainSwitch", "1")) {
      Serial.println("/mainSwitch set to 1 in Firebase.");
    } else {
      Serial.println("Failed to set /mainSwitch to 1 in Firebase: " + fbdo.errorReason());
    }

    if (Firebase.RTDB.setString(&fbdo, "/subSwitch1", "1")) {
      Serial.println("/subSwitch1 set to 1 in Firebase.");
    } else {
      Serial.println("Failed to set /subSwitch1 to 1 in Firebase: " + fbdo.errorReason());
    }

    if (Firebase.RTDB.setString(&fbdo, "/subSwitch2", "1")) {
      Serial.println("/subSwitch2 set to 1 in Firebase.");
    } else {
      Serial.println("Failed to set /subSwitch2 to 1 in Firebase: " + fbdo.errorReason());
    }

    // Display "Temp too high" on the LCD
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("TEMP TOO HIGH");
  }

  // Fetch currency value from Firebase periodically
  static float currencyValue = 0.0;
  if (millis() - lastFetchTime > firebaseFetchInterval) {
    lastFetchTime = millis();
    if (Firebase.ready()) {
      Serial.println("Attempting to fetch currency value from Firebase...");
      if (Firebase.RTDB.getFloat(&fbdo, "currency/Value")) {
        if (fbdo.dataType() == "float") {
          currencyValue = fbdo.floatData();
          Serial.println("Currency Value: " + String(currencyValue));
        } else {
          Serial.println("Data is not of type float.");
        }
      } else {
        Serial.println("Failed to fetch currency value. Error: " + fbdo.errorReason());
      }
    } else {
      Serial.println("Firebase not ready. Attempting to reconnect...");
      Firebase.reconnectWiFi(true);
    }
  }

  // Update Firebase data every update interval
  if (currentTime - lastFirebaseCheckTime >= updateInterval) {
    lastFirebaseCheckTime = currentTime;

    // Update temperature, voltage, current, power, energy, and frequency to Firebase
    if (tempC == DEVICE_DISCONNECTED_C) tempC = 0.00;
    if (Firebase.RTDB.setFloat(&fbdo, "/temperature/Celsius", tempC)) {
      Serial.println("Celsius data sent to Firebase.");
    } else {
      Serial.printf("Failed to send Celsius data: %s\n", fbdo.errorReason().c_str());
    }

    if (isnan(voltage)) voltage = 0.00;
    if (Firebase.RTDB.setFloat(&fbdo, "/voltage/Value", voltage)) {
      Serial.println("Voltage data sent to Firebase.");
    } else {
      Serial.printf("Failed to send voltage data: %s\n", fbdo.errorReason().c_str());
    }

    if (isnan(current)) current = 0.00;
    if (Firebase.RTDB.setFloat(&fbdo, "/current/Value", current)) {
      Serial.println("Current data sent to Firebase.");
    } else {
      Serial.printf("Failed to send current data: %s\n", fbdo.errorReason().c_str());
    }

    if (isnan(power)) power = 0.00;
    if (Firebase.RTDB.setFloat(&fbdo, "/power/Value", power)) {
      Serial.println("Power data sent to Firebase.");
    } else {
      Serial.printf("Failed to send power data: %s\n", fbdo.errorReason().c_str());
    }

    if (isnan(energy)) energy = 0.00;
    if (Firebase.RTDB.setFloat(&fbdo, "/energy/Value", energy)) {
      Serial.println("Energy data sent to Firebase.");
    } else {
      Serial.printf("Failed to send energy data: %s\n", fbdo.errorReason().c_str());
    }

    if (isnan(frequency)) frequency = 0.00;
    if (Firebase.RTDB.setFloat(&fbdo, "/frequency/Value", frequency)) {
      Serial.println("Frequency data sent to Firebase.");
    } else {
      Serial.printf("Failed to send frequency data: %s\n", fbdo.errorReason().c_str());
    }
  }

// Check relay status from Firebase only when needed
if (currentTime - lastUpdateTime >= updateInterval) {
    lastUpdateTime = currentTime;

    if (Firebase.RTDB.getString(&fbdo, "/mainSwitch")) {
        String mainSwitchState = fbdo.stringData();
        Serial.println("Main Switch State: " + mainSwitchState);

        if (mainSwitchState == "0") {
            digitalWrite(RELAY_PIN, 0); // Turn relay ON
            Serial.println("Relay turned ON");
        } else if (mainSwitchState == "1") {
            digitalWrite(RELAY_PIN, 1); // Turn relay OFF
            Serial.println("Relay turned OFF");
        }
    } else {
        Serial.println("Failed to read from Firebase: " + fbdo.errorReason());
    }
}


if (currentTime - lastUpdateTime1 >= updateInterval) {
    lastUpdateTime1 = currentTime;

    if (Firebase.RTDB.getString(&fbdo, "/subSwitch1")) {
        String subSwitch1State = fbdo.stringData();
        Serial.println("SubSwitch1 State: " + subSwitch1State);

        if (subSwitch1State == "0") {
            digitalWrite(RELAY_PIN1, 0); // Turn relay ON
            Serial.println("Relay 1 turned ON");
        } else if (subSwitch1State == "1") {
            digitalWrite(RELAY_PIN1, 1); // Turn relay OFF
            Serial.println("Relay 1 turned OFF");
        }
    } else {
        Serial.println("Failed to read subSwitch1 from Firebase: " + fbdo.errorReason());
    }
}

if (currentTime - lastUpdateTime2 >= updateInterval) {
    lastUpdateTime2 = currentTime;

    if (Firebase.RTDB.getString(&fbdo, "/subSwitch2")) {
        String subSwitch2State = fbdo.stringData();
        Serial.println("SubSwitch2 State: " + subSwitch2State);

        if (subSwitch2State == "0") {
            digitalWrite(RELAY_PIN2, 0); // Turn relay ON
            Serial.println("Relay 2 turned ON");
        } else if (subSwitch2State == "1") {
            digitalWrite(RELAY_PIN2, 1); // Turn relay OFF
            Serial.println("Relay 2 turned OFF");
        }
    } else {
        Serial.println("Failed to read subSwitch2 from Firebase: " + fbdo.errorReason());
    }
}


  // Non-blocking LCD update logic
  if (currentTime - lastLCDUpdateTime >= lcdUpdateInterval) {
    lastLCDUpdateTime = currentTime;

    // Determine which set of data to show
    static int displayCycle = 0; // Keeps track of which display to show

    lcd.clear();
    lcd.setCursor(0, 0);
    switch (displayCycle) {
      case 0: // Current and Power
        lcd.print("Current: ");
        lcd.print(current, 1);
        lcd.print("A");
        lcd.setCursor(0, 1);
        lcd.print("Power: ");
        lcd.print(power, 1);
        lcd.print("W");
        break;
      case 1: // Energy and Frequency
        lcd.print("Energy: ");
        lcd.print(energy, 2);
        lcd.print("kWh");
        lcd.setCursor(0, 1);
        lcd.print("Frq: ");
        lcd.print(frequency, 1);
        lcd.print("Hz");
        break;
      case 2: // Temperature and Voltage
        lcd.print("Temp: ");
        lcd.print(tempC, 1);
        lcd.print("C");
        lcd.setCursor(0, 1);
        lcd.print("Volt: ");
        lcd.print(voltage, 1);
        lcd.print("V");
        break;
      case 3: // Total Bill
        lcd.print("Total Bill:");
        lcd.setCursor(0, 1);
        lcd.print(currencyValue, 2);
        lcd.print(" PHP");
        break;
    }

    displayCycle = (displayCycle + 1) % 4; // Cycle through the display cases
  }
}
