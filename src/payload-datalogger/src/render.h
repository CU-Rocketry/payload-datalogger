#ifndef RENDER_H
#define RENDER_H

#include <Arduino.h>

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



#endif