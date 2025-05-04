#include <WiFiNINA.h>  // for Nano 33 IoT, MKR1010
#include <ArduinoHttpClient.h>
#include "arduino_secrets.h"

char hueHubIP[] = "172.22.151.183";  // IP address of the HUE bridge

// make a wifi instance and a HttpClient instance:
WiFiClient wifi;
HttpClient httpClient = HttpClient(wifi, hueHubIP);

const int buttonPin2 = 2;  // Button on D2
const int buttonPin3 = 3;  // Button on D3
const int buttonPin4 = 4;  // Button on D4
const int ledPin = 8;      // LED on D8

bool ledFlag = false;
bool autoFlag = false;
bool sceneFlag = false;

// LED on/off
int ledNumber = 16;
bool lastButton2State = LOW;  
bool currentButton2State;

// Auto mode on/off
bool lastButton3State = LOW;
bool currentButton3State;

// Switch Scene mode on/off
bool lastButton4State = LOW;
bool currentButton4State;

void setup() {
  Serial.begin(9600);
  if (!Serial) delay(3000);

  // Initialize
  bool ledFlag = false;
  bool autoFlag = false;
  bool sceneFlag = false;

  // attempt to connect to Wifi network:
  // 4 means WL_CONNECTED
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print("Attempting to connect to WPA SSID: ");
    Serial.println(SECRET_SSID);
    // Connect to WPA/WPA2 network:
    WiFi.begin(SECRET_SSID, SECRET_PASS);
    Serial.println(WiFi.status());
    delay(2000);
  }
  sendRequest(ledNumber, "on", "false");

  // you're connected now, so print out the data:
  Serial.println("Connected to the network: " + String(SECRET_SSID));
  Serial.println("IP:  ");
  Serial.println(WiFi.localIP());


  pinMode(buttonPin2, INPUT_PULLUP);
  pinMode(buttonPin3, INPUT_PULLUP);
  pinMode(buttonPin4, INPUT_PULLUP);
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);
}

void loop() {
  currentButton2State = digitalRead(buttonPin2);
  currentButton3State = digitalRead(buttonPin3);
  currentButton4State = digitalRead(buttonPin4);

  // Detect rising edge: button press (HIGH -> LOW)
  if (lastButton2State == HIGH && currentButton2State == LOW) {
    ledFlag = !ledFlag;
    digitalWrite(ledPin, ledFlag ? HIGH : LOW); 
    Serial.print("Flag toggled. New value: ");
    Serial.println(ledFlag);
    turnOnOffLED(ledFlag);
    delay(200);  // Debounce delay
  }

  // As long as you click, it turns into auto mode.
  if (lastButton3State == HIGH && currentButton3State == LOW) {
    autoFlag = true;
    Serial.println("Auto Mode On");
    setAutoMode();
    delay(200);  // Debounce delay
  }

  // As long as you click, it turns off auto mode and start to loop
  // all colors.
  if (lastButton4State == HIGH && currentButton4State == LOW) {
    if (autoFlag) {
      autoFlag = false;
      Serial.println("Auto Mode Off");
      Serial.println("Scene Mode On");
    }
    changeScene();
    delay(200);  // Debounce delay
  }

  lastButton2State = currentButton2State;
  lastButton3State = currentButton3State;
  lastButton4State = currentButton4State;
  
  delay(100);
}

// ###########################################################
// ################### Helper Functions ######################
// ###########################################################

void turnOnOffLED(bool ledFlag) {
  if (ledFlag) {
    sendRequest(ledNumber, "on", "true");
  } else {
    sendRequest(ledNumber, "on", "false");
  }
}

String REQ_ARRAY[] = {
  "{\"on\":true,\"bri\":130,\"hue\":38595,\"sat\":150}", // 8PM Hello
  "{\"on\":true,\"bri\":126,\"hue\":40504,\"sat\":150}", // 9PM
  "{\"on\":true,\"bri\":122,\"hue\":42413,\"sat\":150}", // 10PM
  "{\"on\":true,\"bri\":120,\"hue\":44321,\"sat\":150}", // 11PM
  "{\"on\":true,\"bri\":100,\"hue\":46230,\"sat\":150}", // 12AM Moonlight
  "{\"on\":true,\"bri\":100,\"hue\":46230,\"sat\":100}", // 1AM
  "{\"on\":true,\"bri\":100,\"hue\":46230,\"sat\":210}", // 2AM
  "{\"on\":true,\"bri\":70,\"hue\":62720,\"sat\":80}", // 4AM
  "{\"on\":true,\"bri\":100,\"hue\":62720,\"sat\":80}", // 5AM
  "{\"on\":true,\"bri\":100,\"hue\":62720,\"sat\":120}", // 6AM Sunrise
};


// Auto mode:
// 1. During 8PM - 6AM, the light will be working.
// 2. between 7AM - 8PM, the light will be turned off.
void setAutoMode() {
  if (!autoFlag) return;

  String response = getTimeResponse();
  int currentHour = getTimeCode(response);
  bool isWorkingTime = (currentHour >= 20 || currentHour < 7);
  if (isWorkingTime) {
    if (ledFlag) {
      turnOnAutoMode(currentHour);
    }
  } else {
    // ledFlag = false;
    // turnOnOffLED(false);
    // digitalWrite(ledPin, ledFlag ? HIGH : LOW);
  }
  return;
}

void turnOnAutoMode(int currentHour) {
  int lightIndex = mapNightHourToIndex(currentHour);
  String lightRequest = REQ_ARRAY[lightIndex];
  sendLightRequest(ledNumber, lightRequest);
}

int sceneIndex = 0; // between 0 and 9
void changeScene() {
  if (sceneIndex < 10) {
    sendLightRequest(ledNumber, REQ_ARRAY[sceneIndex]);
    sceneIndex = (sceneIndex + 1) % 10;
  }  
}

String getTimeResponse() {
  String request = "/api/" + String(SECRET_HUBAPPKEY);
  request += "/config/";
  httpClient.get(request); // Start connection
  String response = httpClient.responseBody();
  return response;
}

int getTimeCode(String response) {
  int hour = getHourFromResponse(response);
  Serial.print("Extracted hour: " + hour);
  return hour;
}

int mapNightHourToIndex(int hour) {
  if (hour >= 20 && hour <= 23) {
    return hour - 20;  // 20 → 0, 21 → 1, ..., 23 → 3
  } else if (hour >= 0 && hour <= 6) {
    return hour + 4;   // 0 → 4, 1 → 5, ..., 6 → 10
  } else {
    return -1; // Not in night range
  }
}


// ###########################################################
// ################ HTTP Request functions ###################
// ###########################################################

int getHourFromResponse(String response) {
  int utcIndex = response.indexOf("\"localtime\":\"");
  if (utcIndex == -1) {
    return -1; // "UTC" field not found
  }

  // Start index of the timestamp (after "UTC":" => 7 characters)
  int timestampStart = utcIndex + 7;

  // Find the 'T' that separates date and time
  int tIndex = response.indexOf("T", timestampStart);
  if (tIndex == -1 || tIndex + 2 >= response.length()) {
    return -1; // invalid format
  }

  // Extract the two digits after 'T' (hour)
  String hourStr = response.substring(tIndex + 1, tIndex + 3);
  return hourStr.toInt(); // Convert to int
}

void sendLightRequest(int light, String requestPayload) {
  // make a String for the HTTP request path:
  String request = "/api/" + String(SECRET_HUBAPPKEY);
  request += "/lights/";
  request += light;
  request += "/state/";

  String contentType = "application/json";

  // see what you assembled to send:
  Serial.print("PUT request to server: ");
  Serial.println(request);
  Serial.print("JSON command to server: ");
  Serial.println(requestPayload);
  // make the PUT request to the hub:
  httpClient.put(request, contentType, requestPayload);

  // read the status code and body of the response
  int statusCode = httpClient.responseStatusCode();
  String response = httpClient.responseBody();

  Serial.println(requestPayload);
  Serial.print("Status code from server: ");
  Serial.println(statusCode);
  Serial.print("Server response: ");
  Serial.println(response);
  Serial.println();
}

void sendRequest(int light, String cmd, String value) {
  // make a String for the HTTP request path:
  String request = "/api/" + String(SECRET_HUBAPPKEY);
  request += "/lights/";
  request += light;
  request += "/state/";

  String contentType = "application/json";

  // make a string for the JSON command:
  String hueCmd = "{\"" + cmd;
  hueCmd += "\":";
  hueCmd += value;
  hueCmd += "}";
  // see what you assembled to send:
  Serial.print("PUT request to server: ");
  Serial.println(request);
  Serial.print("JSON command to server: ");
  Serial.println(hueCmd);
  // make the PUT request to the hub:
  httpClient.put(request, contentType, hueCmd);

  // read the status code and body of the response
  int statusCode = httpClient.responseStatusCode();
  String response = httpClient.responseBody();

  Serial.println(hueCmd);
  Serial.print("Status code from server: ");
  Serial.println(statusCode);
  Serial.print("Server response: ");
  Serial.println(response);
  Serial.println();
}