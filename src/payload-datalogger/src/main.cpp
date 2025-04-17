#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include "peripherals.h"
#include "UserLED.h"
#include "UserButton.h"
#include <Thermistor.h>
#include <NTC_Thermistor.h>
#include "MCP9600.h"

// ESP32 will create an AP with these credentials
const char *ssid = "CUR_Payload_Datalogger";
const char *password = "12345678";

IPAddress local_ip(192, 168, 1, 1); // static IP for ESP32
IPAddress gateway(192, 168, 1, 1); // gateway is ESP32 itself
IPAddress subnet(255, 255, 255, 0); // subnet mask

AsyncWebServer server(80);

String renderLedToggleDiv();
String renderUserButtonDiv();
String renderRecordingToggleDiv();
String renderTimestepInputDiv();
String renderDownloadDiv();
String renderFlashDiv();
String renderTemperatureTable();
String renderIndex();

// Thermistors
#define NTC_REF_R 10000
#define NTC_NOMINAL_R 10000
#define NTC_NOMINAL_T 25
#define NTC_B 3435

NTC_Thermistor_ESP32 ntc0(NTC_0_PIN, NTC_REF_R, NTC_NOMINAL_R, NTC_NOMINAL_T, NTC_B, 3300, 4095);
NTC_Thermistor_ESP32 ntc1(NTC_1_PIN, NTC_REF_R, NTC_NOMINAL_R, NTC_NOMINAL_T, NTC_B, 3300, 4095);

#define TC0_ADDRESS 0x60
#define TC1_ADDRESS 0x61
#define TC2_ADDRESS 0x62
#define TC3_ADDRESS 0x63

MCP9600 tc0(TC0_ADDRESS);
MCP9600 tc1(TC1_ADDRESS);
MCP9600 tc2(TC2_ADDRESS);
MCP9600 tc3(TC3_ADDRESS);

void thermocouples_init();

UserLED status_led(USER_LED_PIN);
UserButton user_btn(USER_BUTTON_PIN);

bool ledState = false;
bool isRecording = false;
int recordingTimestep = 1000;

void setup()
{
  Serial.begin(115200);

  Serial.println("Starting up");

  thermocouples_init();  

  Serial.println("Peripherals initialized");

  // Initialize SPIFFS for static file serving
  if (!SPIFFS.begin(true))
  {
    Serial.println("Error mounting SPIFFS");
    return;
  }
  Serial.println("SPIFFS mounted successfully");

  Serial.println("Starting WiFi AP");

  WiFi.softAP(ssid, password);
  WiFi.softAPConfig(local_ip, gateway, subnet);

  Serial.print("Access Point started. SSID: ");
  Serial.print(ssid);
  Serial.print(" AP IP address: ");
  Serial.println(WiFi.softAPIP());

  Serial.println("Starting web server");

  // Serve static files from SPIFFS
  server.serveStatic("/htmx.min.js", SPIFFS, "/htmx.min.js");
  server.serveStatic("/favicon.ico", SPIFFS, "/favicon.ico");

  // render index in response to root URL request
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", renderIndex());
  });

  // Serve requests from HTMX including toggling the LED, user button, recording, etc.
  server.on("/toggle_status_led", HTTP_POST, [](AsyncWebServerRequest *request) {
    ledState = !ledState; // Toggle the LED state
    status_led.set(ledState); // Set the LED state
    request->send(200, "text/html", renderLedToggleDiv()); // Send updated HTML for the LED toggle div
  });

  server.on("/user_button_state", HTTP_GET, [](AsyncWebServerRequest *request) {
    // Simulate user button state retrieval (replace with actual logic)
    String userButtonState = "Pressed"; // Placeholder for actual button state
    request->send(200, "text/html", renderUserButtonDiv());
  });

  server.on("/toggle_recording", HTTP_POST, [](AsyncWebServerRequest *request) {
    isRecording = !isRecording; // Toggle the recording state
    request->send(200, "text/html", renderRecordingToggleDiv()); // Send updated HTML for the recording toggle div
  });

  server.on("/set_rate", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("rate", true)) {
      recordingTimestep = request->getParam("rate", true)->value().toInt(); // Update the recording timestep
    }
    request->send(200, "text/html", renderTimestepInputDiv()); // Send updated HTML for the timestep input div
  });

  server.on("/download_data", HTTP_GET, [](AsyncWebServerRequest *request) {
    // Simulate data download (replace with actual logic)
    String downloadStatus = "Data downloaded successfully"; // Placeholder for actual download status
    request->send(200, "text/html", renderDownloadDiv());
  });

  server.on("/flash_usage", HTTP_GET, [](AsyncWebServerRequest *request) {
    // Simulate flash usage check (replace with actual logic)
    String flashUsage = "Flash usage: 50%"; // Placeholder for actual flash usage
    request->send(200, "text/html", renderFlashDiv());
  });

  server.on("/refresh_table", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", renderTemperatureTable()); // Send updated temperature table
  });

  server.begin(); // begin web server

  Serial.println("Web server started");
}

void loop()
{
  
}

void thermocouples_init()
{
  Wire.begin(I2C_SDA, I2C_SCL, 100000);

  if (!tc0.checkDeviceID()) Serial.println("TC0 Device ID check failed");
  if (!tc1.checkDeviceID()) Serial.println("TC1 Device ID check failed");
  if (!tc2.checkDeviceID()) Serial.println("TC2 Device ID check failed");
  if (!tc3.checkDeviceID()) Serial.println("TC3 Device ID check failed");
}

String renderLedToggleDiv()
{
  /*
    <div id="led-toggle-div">
        <label for="led-toggle">Status LED:</label>
        <input type="checkbox" id="led-toggle" hx-post="/toggle_status_led" hx-trigger="change" hx-target="#led-toggle-div"
            hx-swap="outerHTML">
        <strong>OFF</strong>
    </div>
  */
  String html = "<div id=\"led-toggle-div\">";
  html += "<label for=\"led-toggle\">Status LED:</label>";
  html += "<input type=\"checkbox\" id=\"led-toggle\" hx-post=\"/toggle_status_led\" hx-trigger=\"change\" hx-target=\"#led-toggle-div\" hx-swap=\"outerHTML\"";
  html += (ledState) ? " checked>" : ">";
  html += "<strong>";
  html += (ledState) ? "ON" : "OFF"; // Display the current state of the LED
  html += "</strong></div>";
  return html;
}

String renderUserButtonDiv()
{
  /*
   <div id="user-button-div">
        <button hx-get="/user_button_state" hx-target="#user-button-div" hx-swap="outerHTML">
            Get User Button State
        </button>
        <div>Unknown</div>
    </div>
  */
  String html = "<div id=\"user-button-div\">";
  html += "<button hx-get=\"/user_button_state\" hx-target=\"#user-button-div\" hx-swap=\"outerHTML\">Get User Button State</button>";
  html += "<div>Placeholder</div></div>"; // Placeholder for user button state
  return html;
}

String renderRecordingToggleDiv()
{
  /*
  <div id="recording-toggle-div">
        <button hx-post="/toggle_recording" hx-target="#recording-toggle-div" hx-swap="outerHTML"
            hx-trigger="click">
            Toggle Recording
        </button>
        <div>
            Recording is currently <strong>OFF</strong>.
        </div>
    </div>
  */

  String html = "<div id=\"recording-toggle-div\">";
  html += "<button hx-post=\"/toggle_recording\" hx-target=\"#recording-toggle-div\" hx-swap=\"outerHTML\" hx-trigger=\"click\">";
  html += (isRecording) ? "Stop Recording" : "Start Recording";
  html += "</button>";
  html += "<div>Recording is currently <strong>";
  html += (isRecording) ? "ON" : "OFF"; // Display the current recording state
  html += "</strong>.</div></div>";
  return html;
}

String renderTimestepInputDiv()
{
  /*
<div id="record-rate-div">
        <label for="record-rate">Recording timestep [ms]:</label>
        <input type="number" id="record-rate" name="rate" value="1000" min="1" hx-post="/set_rate" hx-trigger="change"
            hx-target="#record-rate-div" hx-swap="outerHTML">
        <div>Current timestep: <strong>1000</strong> ms</div>
    </div>
  */
  String html = "<div id=\"record-rate-div\">";
  html += "<label for=\"record-rate\">Recording timestep [ms]:</label>";
  html += "<input type=\"number\" id=\"record-rate\" name=\"rate\" value=\"" + String(recordingTimestep) + "\" min=\"1\" hx-post=\"/set_rate\" hx-trigger=\"change\" hx-target=\"#record-rate-div\" hx-swap=\"outerHTML\">";
  html += "<div>Current timestep: <strong>" + String(recordingTimestep) + "</strong> ms</div></div>";
  return html;
}

String renderDownloadDiv(){
  /*
    <div id="download-data-div">
        <button hx-get="/download_data" hx-target="#download-data-div" hx-swap="outerHTML" hx-trigger="click">
            Download Data
        </button>
        <div>Download status: Unknown</div>
    </div>
  */
  String html = "<div id=\"download-data-div\">";
  html += "<button hx-get=\"/download_data\" hx-target=\"#download-data-div\" hx-swap=\"outerHTML\" hx-trigger=\"click\">Download Data</button>";
  html += "<div>Download status: Placeholder</div></div>";
  return html;
}

String renderFlashDiv(){
  /*
  <div id="flash-usage-div">
        <button hx-get="/flash_usage" hx-target="#flash-usage-div" hx-swap="outerHTML" hx-trigger="click,load">
            Check Flash Usage
        </button>
        <div>Flash usage: Unknown</div>
    </div>
  */
  String html = "<div id=\"flash-usage-div\">";
  html += "<button hx-get=\"/flash_usage\" hx-target=\"#flash-usage-div\" hx-swap=\"outerHTML\" hx-trigger=\"click,load\">Check Flash Usage</button>";
  html += "<div>Flash usage: Placeholder</div></div>";
  return html;
}

// Helper function to simulate sensor readings and generate the temperature table HTML.
// This function constructs an HTML snippet that includes a header row and dynamically updated sensor readings.
String renderTemperatureTable()
{
  String html = "<table id=\"temperature-table\">";
  html += "<tr><th>Sensor</th><th>Temperature (°C)</th></tr>";

  // Array of sensor identifiers for demonstration purposes.
  const char *sensorNames[] = {"NTC 0", "NTC 1", "Thermocouple 0", "Thermocouple 1", "Thermocouple 2", "Thermocouple 3"};
  const int numSensors = sizeof(sensorNames) / sizeof(sensorNames[0]);

  // Generate each sensor's temperature row using a randomized temperature value between 20.0 and 30.0 °C.
  for (int i = 0; i < numSensors; i++)
  {
    float temperature = 20.0 + (random(0, 1000) / 1000.0) * 10.0;
    html += "<tr><td>";
    html += sensorNames[i];
    html += "</td><td>";
    html += String(temperature, 1);
    html += "</td></tr>";
  }
  html += "</table>";
  return html;
}

String renderIndex()
{
  String html = "<!DOCTYPE html><html lang=\"en\"><head>";
  html += "<meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">";
  html += "<title>CUR Payload Datalogger</title>";
  html += "<link rel=\"icon\" href=\"/favicon.ico\" type=\"image/x-icon\">";
  html += "<style>table{border-collapse:collapse}td,th{border:1px solid #444;padding:8px;text-align:left}th{background-color:#f2f2f2}input{padding:4px}button{padding:4px 8px}</style>";
  html += "<script src=\"/htmx.min.js\"></script></head>";
  html += "<body><h1>CUR Payload Datalogger</h1>";
  html += "<h2>Debug</h2>";
  html += renderLedToggleDiv();
  html += renderUserButtonDiv();
  html += "<h2>Recording</h2>";
  html += renderRecordingToggleDiv();
  html += renderTimestepInputDiv();
  html += renderDownloadDiv();
  html += renderFlashDiv();
  html += "<h2>Temperature Sensors</h2>";
  html += "<div><button hx-get=\"/refresh_table\" hx-target=\"#temperature-table\" hx-swap=\"innerHTML\" hx-trigger=\"click, every 5s\">Refresh Temperature Data</button></div>";
  html += renderTemperatureTable();
  html += "</body></html>";
  return html;
}

