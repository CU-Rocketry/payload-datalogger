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
#include <SPIMemory.h>
#include <cstdint>
#include <cstring>
#include <array>

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

void flash_init();
void ap_init();
void webserver_init();
void thermocouples_init();
bool writePageToFlash(const Page& page);

UserLED status_led(USER_LED_PIN);
UserButton user_btn(USER_BUTTON_PIN);

SPIFlash flash(FLASH_CS, &SPI);
uint32_t flash_capacity = 0;
uint32_t flash_pointer = 0; // should be aligned with page size

// 32*7 = 224 bits / 8 = 28 bytes per data point
// at a 1Hz sampling rate, this is 28 bytes/s
// at 1 hour, this is 28*60*60 = 100.8kB per hour
// so we have 5 hours of recording time with no optimization
// i.e. we could probably store everything as fixed point int16_t
// but haven't figured that out quite yet

// what about if we pad the data point to 32 bytes?
// then we have 115.2kB per hour or 4 hours of recording time

// with 32 bytes, we store 8 data points per page (256 bytes)

bool ledState = false;
uint32_t last_led_time = 0; // [ms] since boot
#define STATUS_LED_IDLE_DURATION 100 // [ms]
#define STATUS_LED_IDLE_DELAY 900 // [ms]
#define STATUS_LED_RECORDING_DURATION 1000 // [ms]
#define STATUS_LED_RECORDING_DELAY 1000 // [ms]

enum class LoggingState
{
    IDLE,
    RECORDING
};
LoggingState logging_state = LoggingState::IDLE;
uint32_t logging_interval = 1000; // [ms] between logging events, default to 1s
uint32_t last_logging_time = 0;   // [ms] since boot
uint32_t current_time = 0;        // [ms] since boot, fine for ~50 days
uint32_t iteration = 0;           // number of data points logged

#pragma pack(push, 1) // ensure no padding between struct members
struct DataPoint
{
    uint32_t iteration;
    uint32_t timestamp; // [ms] since boot
    float ntc0_temp;    // [C], assuming 32 bit float
    float ntc1_temp;    // [C]
    float tc0_temp;     // [C]
    float tc1_temp;     // [C]
    float tc2_temp;     // [C]
    float tc3_temp;     // [C]
};
#pragma pack(pop) // restore default packing
static_assert(sizeof(DataPoint) == 32, "DataPoint must be exactly 32 bytes");
#define POINTS_PER_PAGE (256 / sizeof(DataPoint)) // 8 data points per page
using Page = std::array<DataPoint, POINTS_PER_PAGE>;
using Buffer = std::array<std::uint8_t, sizeof(DataPoint) * POINTS_PER_PAGE>;
static_assert(Buffer{}.size() == 256, "Serialized page must be 256 bytes");

Page current_page; // Buffer for the current page of data points

void setup()
{
    Serial.begin(115200);
    Serial.println("Serial started");

    thermocouples_init();
    flash_init();
    status_led.init();
    user_btn.init();

    if (!SPIFFS.begin(true)) // for static file serving, true: format if mount fails
    {
        Serial.println("Error mounting SPIFFS");
        return;
    }
    Serial.println("SPIFFS mounted successfully");

    ap_init();
    webserver_init();

    Serial.println("Setup complete");
}

void loop()
{
    current_time = millis();
    switch (logging_state)
    {
        case LoggingState::IDLE:
            // blink the status LED
            if (ledState) {
                if (current_time - last_led_time >= STATUS_LED_IDLE_DURATION) { // if enough time has passed to turn off the LED
                    status_led.set(false);
                    ledState = !ledState; // toggle LED state
                    last_led_time = current_time; // reset timer
                }
            } else {
                if (current_time - last_led_time >= STATUS_LED_IDLE_DELAY) { // if enough time has passed to turn on the LED
                    status_led.set(true);
                    ledState = !ledState; // toggle LED state
                    last_led_time = current_time; // reset timer
                }
            }
            
            break;

        case LoggingState::RECORDING:
            // log data if timer elapsed
            if (current_time - last_logging_time >= logging_interval) { // if enough time has passed to log a new point
                
                // get all temperatures
                float ntc0_temp = ntc0.readCelsius();
                float ntc1_temp = ntc1.readCelsius();
                float tc0_temp = tc0.getHotJunctionTemp();
                float tc1_temp = tc1.getHotJunctionTemp();
                float tc2_temp = tc2.getHotJunctionTemp();
                float tc3_temp = tc3.getHotJunctionTemp();
                
                DataPoint data_point = {iteration, current_time, ntc0_temp, ntc1_temp, tc0_temp, tc1_temp, tc2_temp, tc3_temp};

                current_page[iteration % POINTS_PER_PAGE] = data_point;
                
                last_logging_time = current_time; // reset timer
                iteration++; // increment iteration count

                if (iteration % POINTS_PER_PAGE == 0) { // if we filled the page and are moving onto a new one
                    // write to flash
                    if (writePageToFlash(current_page)) {
                        Serial.println("Successfully wrote page to flash");
                    } else {
                        Serial.println("Failed to write page to flash.");
                    }

                    flash_pointer += sizeof(current_page); // increment flash pointer by page size
                }
            }

            // blink the status LED
            if (ledState) {
                if (current_time - last_led_time >= STATUS_LED_RECORDING_DURATION) { // if enough time has passed to turn off the LED
                    status_led.set(false);
                    ledState = !ledState; // toggle LED state
                    last_led_time = current_time; // reset timer
                }
            } else {
                if (current_time - last_led_time >= STATUS_LED_RECORDING_DELAY) { // if enough time has passed to turn on the LED
                    status_led.set(true);
                    ledState = !ledState; // toggle LED state
                    last_led_time = current_time; // reset timer
                }
            }
            break;
    }
}

void flash_init(){
    flash.begin();
    // flash.setClock(50000000); // requires experimentation
    flash_capacity = flash.getCapacity();
    Serial.printf("Flash initialized. Capacity: %lu bytes\n", flash_capacity);
    // Consider erasing the flash or part of it on first boot or via a command
    // For now, it will append or overwrite based on previous state and flash_pointer (which is 0 at boot)
    // flash.eraseChip(); // Uncomment to erase flash on boot DANGEROUS DONT DO IT
}

void ap_init(){
    Serial.println("WiFi AP init");
    WiFi.softAP(ssid, password);
    WiFi.softAPConfig(local_ip, gateway, subnet);
    Serial.println("WiFi AP configured");
    Serial.print("SSID: "); Serial.println(ssid);
    Serial.print("Password: "); Serial.println(password);
    Serial.print("AP IP Address: "); Serial.println(WiFi.softAPIP());
}

void webserver_init(){
    Serial.println("Web server init");

    server.serveStatic("/htmx.min.js", SPIFFS, "/htmx.min.js");
    server.serveStatic("/favicon.ico", SPIFFS, "/favicon.ico");

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/html", renderIndex());
    });

    server.on("/toggle_status_led", HTTP_POST, [](AsyncWebServerRequest *request) {
        ledState = !ledState;
        status_led.set(ledState);
        request->send(200, "text/html", renderLedToggleDiv());
    });

    server.on("/user_button_state", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/html", renderUserButtonDiv());
    });

    server.on("/toggle_recording", HTTP_POST, [](AsyncWebServerRequest *request) {
        is_logging = !is_logging;
        if () {

            Serial.println("Recording started.");
            last_logging_time = millis(); // Reset timer when starting
        } else {
            Serial.println("Recording stopped.");
            // Optionally, write any remaining data in current_page if it's partially filled
            // This requires more complex logic to write a partial page or pad it. This could be a pain.
        }
        request->send(200, "text/html", renderRecordingToggleDiv());
    });

    server.on("/set_rate", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("rate", true)) {
            int rate_val = request->getParam("rate", true)->value().toInt();
            if (rate_val > 0) {
                logging_interval = (uint32_t)rate_val;
                Serial.printf("Logging interval set to: %lu ms\n", logging_interval);
            } else {
                Serial.println("Invalid rate value received.");
            }
        }
        request->send(200, "text/html", renderTimestepInputDiv());
    });

    server.on("/download_data", HTTP_GET, [](AsyncWebServerRequest *request) {
        Serial.println("Data download requested.");
        AsyncResponseStream *response = request->beginResponseStream("text/csv");
        response->addHeader("Content-Disposition", "attachment; filename=\"datalog.csv\"");

        response->print("Iteration,Timestamp,NTC0_Temp,NTC1_Temp,TC0_Temp,TC1_Temp,TC2_Temp,TC3_Temp\n");

        if (iteration == 0 || flash_pointer == 0) {
            response->print("No data logged yet.\n");
            request->send(response);
            Serial.println("No data to send for download.");
            return;
        }

        Page data_page_buffer;
        uint32_t current_read_address = 0;
        uint32_t points_retrieved = 0;
        uint32_t total_points_to_read = iteration; // Total number of data points recorded

        Serial.printf("Preparing to download %lu data points. Flash pointer at %lu.\n", total_points_to_read, flash_pointer);

        while (current_read_address < flash_pointer && points_retrieved < total_points_to_read) {
            // Read one page from flash
            if (!flash.readAnything(current_read_address, data_page_buffer)) {
                Serial.printf("Flash readAnything error at address %lu during download\n", current_read_address);
                response->print("Error reading flash data at address " + String(current_read_address) + "\n");
                break; 
            }

            for (int i = 0; i < POINTS_PER_PAGE; ++i) {
                if (points_retrieved >= total_points_to_read) {
                    break; // All logged data points have been retrieved
                }

                const DataPoint& dp = data_page_buffer[i];
                
                // Basic check: if dp.iteration seems out of sync with points_retrieved for a non-zero iteration
                // This can happen if flash wasn't erased and contains old data.
                // Relying on `points_retrieved < total_points_to_read` is the primary guard.
                if (dp.iteration != points_retrieved && dp.iteration != 0 && points_retrieved != 0) {
                     // This might indicate a data inconsistency or reading uninitialized flash parts of a page
                     // For simplicity, we currently trust the iteration count.
                }

                response->printf("%lu,%lu,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n",
                                 dp.iteration,
                                 dp.timestamp,
                                 dp.ntc0_temp,
                                 dp.ntc1_temp,
                                 dp.tc0_temp,
                                 dp.tc1_temp,
                                 dp.tc2_temp,
                                 dp.tc3_temp);
                points_retrieved++;
            }
            current_read_address += sizeof(Page);
        }
        
        Serial.printf("Sent %lu data points.\n", points_retrieved);
        if (points_retrieved < total_points_to_read) {
            Serial.printf("Warning: Expected to send %lu points, but only sent %lu.\n", total_points_to_read, points_retrieved);
        }
        request->send(response);
    });

    server.on("/flash_usage", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/html", renderFlashDiv());
    });

    server.on("/refresh_table", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/html", renderTemperatureTable());
    });

    server.begin();
    Serial.println("Web server started");
}

void thermocouples_init()
{
    Wire.begin(I2C_SDA, I2C_SCL, 100000);

    Serial.print("TC0 check: "); Serial.println(tc0.checkDeviceID() ? "OK" : "Failed");
    Serial.print("TC1 check: "); Serial.println(tc1.checkDeviceID() ? "OK" : "Failed");
    Serial.print("TC2 check: "); Serial.println(tc2.checkDeviceID() ? "OK" : "Failed");
    Serial.print("TC3 check: "); Serial.println(tc3.checkDeviceID() ? "OK" : "Failed");
}

inline void serializePage(const Page& page, Buffer& buffer) {
    std::memcpy(buffer.data(), page.data(), buffer.size());
}

inline void deserializePage(const Buffer& buffer, Page& page) {
    std::memcpy(page.data(), buffer.data(), buffer.size());
}

// Returns true on success, false if flash is full and cancels logging
bool writePageToFlash(const Page& page) {
    if (flash_pointer + sizeof(Page) > flash_capacity) {
        Serial.println("Flash full! Write operation aborted.");
        is_logging = false; // Stop logging
        return false;
    }
    Buffer buffer;
    serializePage(page, buffer);
    
    flash.writeAnything(flash_pointer, buffer.data(), buffer.size()); // potentially we could read back to verify?
    flash_pointer += sizeof(Page); 
    Serial.printf("Page written to flash. Flash pointer now: %lu\n", flash_pointer);
    return true;
}

String renderLedToggleDiv() {
    String html = "<div id=\"led-toggle-div\">";
    html += "<label for=\"led-toggle\">Status LED:</label>";
    html += "<input type=\"checkbox\" id=\"led-toggle\" hx-post=\"/toggle_status_led\" hx-trigger=\"change\" hx-target=\"#led-toggle-div\" hx-swap=\"outerHTML\"";
    html += (ledState) ? " checked>" : ">";
    html += "<strong>";
    html += (ledState) ? "ON" : "OFF";
    html += "</strong></div>";
    return html;
}

String renderUserButtonDiv() {
    String html = "<div id=\"user-button-div\">";
    html += "<button hx-get=\"/user_button_state\" hx-target=\"#user-button-div\" hx-swap=\"outerHTML\">Get User Button State</button>";
    bool button_is_pressed = user_btn.read();
    html += "<div>User Button State: <strong>";
    html += button_is_pressed ? "PRESSED" : "RELEASED";
    html += "</strong></div></div>";
    return html;
}

String renderRecordingToggleDiv() {
    String html = "<div id=\"recording-toggle-div\">";
    html += "<button hx-post=\"/toggle_recording\" hx-target=\"#recording-toggle-div\" hx-swap=\"outerHTML\" hx-trigger=\"click\">";
    html += (is_logging) ? "Stop Recording" : "Start Recording";
    html += "</button>";
    html += "<div>Recording is currently <strong>";
    html += (is_logging) ? "ON" : "OFF";
    html += "</strong>.</div></div>";
    return html;
}

String renderTimestepInputDiv() {
    String html = "<div id=\"record-rate-div\">";
    html += "<label for=\"record-rate\">Recording timestep [ms]:</label>";
    html += "<input type=\"number\" id=\"record-rate\" name=\"rate\" value=\"" + String(logging_interval) + "\" min=\"1\" hx-post=\"/set_rate\" hx-trigger=\"change\" hx-target=\"#record-rate-div\" hx-swap=\"outerHTML\">";
    html += "<div>Current timestep: <strong>" + String(logging_interval) + "</strong> ms</div></div>";
    return html;
}

String renderDownloadDiv(){
    String html = "<div id=\"download-data-div\">";
    // Using the <a> tag for downloads is better in my experience. The download attr gives a default filename.
    html += "<a href=\"/download_data\" download=\"datalog.csv\"><button type=\"button\">Download Logged Data (CSV)</button></a>";
    html += "<div>Click the button to download all logged temperature data.</div></div>";
    return html;
}

String renderFlashDiv(){
    String html = "<div id=\"flash-usage-div\">";
    html += "<button hx-get=\"/flash_usage\" hx-target=\"#flash-usage-div\" hx-swap=\"outerHTML\" hx-trigger=\"click,load\">Refresh Flash Usage</button>";
    float usage_percent = 0.0;
    if (flash_capacity > 0) {
        usage_percent = ((float)flash_pointer / flash_capacity) * 100.0;
    }
    html += "<div>Flash Usage: " + String(flash_pointer) + " / " + String(flash_capacity) + " bytes (<strong>" + String(usage_percent, 1) + "%</strong>)</div>";
    html += "<div>Total data points logged: <strong>" + String(iteration) + "</strong></div></div>";
    return html;
}

String renderTemperatureTable() {
    String html = "<table id=\"temperature-table\">";
    html += "<tr><th>Sensor</th><th>Temperature (°C)</th></tr>";

    // Get actual current sensor values for display
    float ntc0_val = ntc0.readCelsius();
    float ntc1_val = ntc1.readCelsius();
    float tc0_val = tc0.getHotJunctionTemp();
    float tc1_val = tc1.getHotJunctionTemp();
    float tc2_val = tc2.getHotJunctionTemp();
    float tc3_val = tc3.getHotJunctionTemp();

    html += "<tr><td>NTC 0</td><td>" + String(ntc0_val, 2) + "</td></tr>";
    html += "<tr><td>NTC 1</td><td>" + String(ntc1_val, 2) + "</td></tr>";
    html += "<tr><td>Thermocouple 0</td><td>" + String(tc0_val, 2) + "</td></tr>";
    html += "<tr><td>Thermocouple 1</td><td>" + String(tc1_val, 2) + "</td></tr>";
    html += "<tr><td>Thermocouple 2</td><td>" + String(tc2_val, 2) + "</td></tr>";
    html += "<tr><td>Thermocouple 3</td><td>" + String(tc3_val, 2) + "</td></tr>";
    html += "</table>";
    return html;
}

String renderIndex() {
    String html = "<!DOCTYPE html><html lang=\"en\"><head>";
    html += "<meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">";
    html += "<title>CUR Payload Datalogger</title>";
    html += "<link rel=\"icon\" href=\"/favicon.ico\" type=\"image/x-icon\">";
    html += "<style>body{font-family:sans-serif;margin:20px}div{margin-bottom:15px}table{border-collapse:collapse;margin-top:10px}td,th{border:1px solid #ccc;padding:8px;text-align:left}th{background-color:#f2f2f2}input[type='number']{width:80px;padding:4px}button{padding:8px 12px;background-color:#007bff;color:white;border:none;border-radius:4px;cursor:pointer}button:hover{background-color:#0056b3}a button{text-decoration:none}</style>";
    html += "<script src=\"/htmx.min.js\"></script></head>";
    html += "<body><h1>CUR Payload Datalogger</h1>";
    html += "<h2>Device Control & Status</h2>";
    html += renderLedToggleDiv();
    html += renderUserButtonDiv();
    html += "<h2>Data Logging</h2>";
    html += renderRecordingToggleDiv();
    html += renderTimestepInputDiv();
    html += renderDownloadDiv();
    html += renderFlashDiv();
    html += "<h2>Live Temperature Sensors</h2>";
    html += "<div><button hx-get=\"/refresh_table\" hx-target=\"#temperature-table\" hx-swap=\"outerHTML\" hx-trigger=\"click, every 5s\">Refresh Temperature Data</button></div>"; // outerHTML to replace table
    html += "<div id=\"temperature-table-container\">" + renderTemperatureTable() + "</div>"; // Initial table load
    html += "<script>document.addEventListener('DOMContentLoaded', (event) => {htmx.process(document.body);});</script>"; // Ensure HTMX processes dynamically added content if needed, or ensure refresh button targets correctly.
    html += "</body></html>";

    return html;
}