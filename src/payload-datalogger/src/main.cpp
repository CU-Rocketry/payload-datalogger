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
#include <Preferences.h> // to store settings
#include <nvs_flash.h>

// ESP32 will create an AP with these credentials
const char *ssid = "CUR_Payload_Datalogger";
const char *password = "12345678";

IPAddress local_ip(192, 168, 1, 1); // static IP for ESP32
IPAddress gateway(192, 168, 1, 1);  // gateway is ESP32 itself
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

TwoWire wire = TwoWire(0);

MCP9600 tc0(TC0_ADDRESS, &wire);
MCP9600 tc1(TC1_ADDRESS, &wire);
MCP9600 tc2(TC2_ADDRESS, &wire);
MCP9600 tc3(TC3_ADDRESS, &wire);

UserLED status_led(USER_LED_PIN);
UserButton user_btn(USER_BUTTON_PIN);

SPIFlash flash(FLASH_CS, &SPI);
uint32_t flash_capacity = 0;
uint32_t flash_pointer = 0; // should be aligned with page size
#define SECTOR_SIZE 4096 // 4kB sector size

bool led_state = false;
uint32_t last_led_time = 0;                // [ms] since boot
#define STATUS_LED_IDLE_DURATION 100       // [ms]
#define STATUS_LED_IDLE_DELAY 500          // [ms]
#define STATUS_LED_RECORDING_DURATION 1000 // [ms]
#define STATUS_LED_RECORDING_DELAY 1000    // [ms]

enum class LoggingState
{
    IDLE,
    RECORDING
};
LoggingState logging_state = LoggingState::IDLE;
uint32_t logging_interval = 1000; // [ms] between logging events, default to 1s
uint32_t last_logging_time = 0;   // [ms] since boot
uint32_t current_time = 0;        // [ms] since boot, fine for ~50 days
uint32_t start_time = 0;         // [ms] since boot, used to calculate timestamps
uint32_t timestamp = 0;         // [ms] since recording began
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

/*
We have 524,288 bytes of flash available (512kB).
This means that with each data point being 32 bytes, we may store 16,384 data points in total.
At a rate of 1 point per second, this gives us a total logging time of 4.5 hours.
If we log every 10 seconds, we can log for 45 hours.
*/

Page current_page; // Buffer for the current page of data points

Preferences preferences;

void flash_init();
void ap_init();
void webserver_init();
void thermocouples_init();
bool flushPageToFlash(const Page &page);
void flushPartialPage();
void loadMetadata();
void saveMetadata();
void dumpFlash();

void setup()
{
    Serial.begin(115200);
    // Serial.begin(921600);
    Serial.println("Serial started");


    // ONCE ONLY
    // Serial.println("Initializing NVS");
    // nvs_flash_erase();
    // nvs_flash_init();
    // preferences.begin("datalogger", false);
    // while(1);

    // thermocouples_init();
    flash_init();

    loadMetadata();

    dumpFlash(); // for debugging

    Serial.println("Flash dump complete");

    while (1){}

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
    timestamp = current_time - start_time;

    switch (logging_state)
    {
    case LoggingState::IDLE:
        // nothing needed here yet
        break;

    case LoggingState::RECORDING:
        // log data if timer elapsed
        if (current_time - last_logging_time >= logging_interval)
        { // if enough time has passed to log a new point

            // get all temperatures
            float ntc0_temp = ntc0.readCelsius();
            float ntc1_temp = ntc1.readCelsius();
            float tc0_temp = tc0.getHotJunctionTemp();
            float tc1_temp = tc1.getHotJunctionTemp();
            float tc2_temp = tc2.getHotJunctionTemp();
            float tc3_temp = tc3.getHotJunctionTemp();

            DataPoint data_point = {iteration, timestamp, ntc0_temp, ntc1_temp, tc0_temp, tc1_temp, tc2_temp, tc3_temp};

            current_page[iteration % POINTS_PER_PAGE] = data_point;

            last_logging_time = current_time; // reset timer
            iteration++;                      // increment iteration count

            if (iteration > 0 && iteration % POINTS_PER_PAGE == 0) // if we filled the page and are moving onto a new one
            {
                // write to flash
                if (flushPageToFlash(current_page))
                {
                    Serial.println("Successfully wrote page to flash");
                }
                else
                {
                    Serial.println("Failed to write page to flash.");
                }
            }
        }
        break;
    }

    // status LED blinking
    uint32_t led_delay;
    uint32_t led_duration;
    switch (logging_state)
    {
    case LoggingState::IDLE:
        led_delay = STATUS_LED_IDLE_DELAY;
        led_duration = STATUS_LED_IDLE_DURATION;
        break;

    case LoggingState::RECORDING:
        led_delay = STATUS_LED_RECORDING_DELAY;
        led_duration = STATUS_LED_RECORDING_DURATION;
        break;
    }
    if (led_state)
    {
        if (current_time - last_led_time >= led_duration)
        {
            status_led.set(false); // turn off LED
            led_state = false;
            last_led_time = current_time; // reset timer
        }
    }
    else
    {
        if (current_time - last_led_time >= led_delay)
        {
            status_led.set(true); // turn on LED
            led_state = true;
            last_led_time = current_time; // reset timer
        }
    }

    delay(10); // small delay to avoid busy loop
}

void flash_init()
{
    Serial.println("Flash init");
    SPI.begin(FLASH_CLK, FLASH_DO, FLASH_DI, FLASH_CS);

    flash.begin();
    // flash.setClock(1000000); // requires experimentation
    flash_capacity = flash.getCapacity();
    Serial.printf("Flash initialized. Capacity: %lu bytes\n", flash_capacity);
}

void ap_init()
{
    Serial.println("WiFi AP init");
    WiFi.softAP(ssid, password);
    WiFi.softAPConfig(local_ip, gateway, subnet);
    Serial.println("WiFi AP configured");
    Serial.print("SSID: ");
    Serial.println(ssid);
    Serial.print("Password: ");
    Serial.println(password);
    Serial.print("AP IP Address: ");
    Serial.println(WiFi.softAPIP());
}

void webserver_init()
{
    Serial.println("Web server init");

    server.serveStatic("/htmx.min.js", SPIFFS, "/htmx.min.js");
    server.serveStatic("/favicon.ico", SPIFFS, "/favicon.ico");
    server.serveStatic("/logo.png", SPIFFS, "/logo.png");

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(200, "text/html", renderIndex()); });

    server.on("/toggle_status_led", HTTP_POST, [](AsyncWebServerRequest *request)
              {
        led_state = !led_state;
        status_led.set(led_state);
        request->send(200, "text/html", renderLedToggleDiv()); });

    server.on("/user_button_state", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(200, "text/html", renderUserButtonDiv()); });

    server.on("/toggle_recording", HTTP_POST, [](AsyncWebServerRequest *request)
              {
        if (logging_state == LoggingState::IDLE) {
            logging_state = LoggingState::RECORDING;
            Serial.println("Recording started.");
            last_logging_time = millis(); // Reset timer when starting
            start_time = millis(); // Reset start time for timestamp calculations
        } else if (logging_state == LoggingState::RECORDING) {
            logging_state = LoggingState::IDLE;
            flushPartialPage();
            Serial.println("Recording stopped.");
            // Optionally, write any remaining data in current_page if it's partially filled
            // This requires more complex logic to write a partial page or pad it. This could be a pain.
        }
        request->send(200, "text/html", renderRecordingToggleDiv()); });

    server.on("/set_rate", HTTP_POST, [](AsyncWebServerRequest *request)
              {
        if (request->hasParam("rate", true)) {
            int rate_val = request->getParam("rate", true)->value().toInt();
            if (rate_val > 0) {
                logging_interval = (uint32_t)rate_val;
                Serial.printf("Logging interval set to: %lu ms\n", logging_interval);
            } else {
                Serial.println("Invalid rate value received.");
            }
        }
        request->send(200, "text/html", renderTimestepInputDiv()); });

    server.on("/download_data", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        Serial.println("Data download requested.");

        if (logging_state != LoggingState::IDLE) {
            request->send(400, "text/plain", "Stop logging before downloading.");
            return;
        }

        AsyncResponseStream *response = request->beginResponseStream("text/csv");
        response->addHeader("Content-Disposition", "attachment; filename=\"cur_payload_log.csv\"");
        response->print("Iteration,Timestamp,NTC0_Temp,NTC1_Temp,TC0_Temp,TC1_Temp,TC2_Temp,TC3_Temp\n");

        if (iteration == 0) {
            response->print("No data logged yet.\n");
            request->send(response);
            return;
        }
        
        Page data_page_buffer;
        uint32_t current_read_address = 0;
        uint32_t points_retrieved = 0;
        uint32_t total_points_to_read = iteration;

        Serial.printf("Preparing to download %lu data points. Flash pointer at %lu.\n", total_points_to_read, flash_pointer);

        while (current_read_address < flash_pointer && points_retrieved < total_points_to_read) {
            if (!flash.readAnything(current_read_address, data_page_buffer)) {
                Serial.printf("Flash read error at address %lu\n", current_read_address);
                break;
            }

            for (int i = 0; i < POINTS_PER_PAGE; ++i) {
                if (points_retrieved >= total_points_to_read) break;

                const DataPoint& dp = data_page_buffer[i];
                response->printf("%lu,%lu,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n",
                                 dp.iteration, dp.timestamp, dp.ntc0_temp, dp.ntc1_temp,
                                 dp.tc0_temp, dp.tc1_temp, dp.tc2_temp, dp.tc3_temp);
                points_retrieved++;
            }
            current_read_address += sizeof(Page);
            
            delay(1); // yield in case of long downloads. maybe should yield longer even
        }
        
        Serial.printf("Sent %lu data points.\n", points_retrieved);
        request->send(response); });

    server.on("/flash_usage", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(200, "text/html", renderFlashDiv()); });

    server.on("/refresh_table", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(200, "text/html", renderTemperatureTable()); });

    server.on("/format_flash", HTTP_POST, [](AsyncWebServerRequest *request)
              {
        if (logging_state == LoggingState::RECORDING) {
            request->send(400, "text/plain", "Cannot format while recording.");
            return;
        }
        
        Serial.println("Formatting flash chip...");
        flash.eraseChip(); // Erase the flash
        
        // FIX: Reset local variables and persisted metadata in NVS
        flash_pointer = 0;
        iteration = 0;
        saveMetadata(); // Save the reset state
        
        Serial.println("Flash formatted successfully.");
        request->send(200, "text/html", renderFlashDiv()); });

    server.begin();
    Serial.println("Web server started");
}

void thermocouples_init()
{
    Serial.println("Thermocouples init");

    wire.begin(I2C_SDA, I2C_SCL, 85000);

    Serial.print("TC0 check: ");
    Serial.println(tc0.checkDeviceID() ? "OK" : "Failed");
    Serial.print("TC1 check: ");
    Serial.println(tc1.checkDeviceID() ? "OK" : "Failed");
    Serial.print("TC2 check: ");
    Serial.println(tc2.checkDeviceID() ? "OK" : "Failed");
    Serial.print("TC3 check: ");
    Serial.println(tc3.checkDeviceID() ? "OK" : "Failed");
}

inline void serializePage(const Page &page, Buffer &buffer)
{
    std::memcpy(buffer.data(), page.data(), buffer.size());
}

inline void deserializePage(const Buffer &buffer, Page &page)
{
    std::memcpy(page.data(), buffer.data(), buffer.size());
}

bool flushPageToFlash(const Page &page)
{
    // Check if flash is full
    if (flash_pointer + sizeof(Page) > flash_capacity) {
        Serial.println("Flash full! Write operation aborted.");
        logging_state = LoggingState::IDLE;
        return false;
    }

    // ensure that the NOR flash is erased to 0xFF before trying to write 0s in
    if (flash_pointer % SECTOR_SIZE == 0) { // if we are starting to write in a new sector
        Serial.printf("New sector address %lu. Erasing sector...\n", flash_pointer);
        if (!flash.eraseSector(flash_pointer)) {
            Serial.printf("Failed to erase sector. Logging stopped.\n");
            logging_state = LoggingState::IDLE;
            return false;
        }
        Serial.println("Sector erased successfully.");
    } 

    Serial.printf("Writing %d byte page to address %lu...\n", sizeof(page), flash_pointer);

    const uint8_t* data_ptr = reinterpret_cast<const uint8_t*>(&page); // treat page as a byte array

    for (size_t i = 0; i < sizeof(page); ++i) { // for each byte in the page
        flash.writeByte(flash_pointer + i, data_ptr[i]); // write to proper address
    }

    // Optional verification of written data
    /*
    Serial.println("Verifying written data...");
    Page read_back_page; // Create a temporary page object to read data into
    
    // Use readAnything, which seems to be more reliable.
    if (!flash.readAnything(flash_pointer, read_back_page)) {
         Serial.println("FATAL: Failed to read back data for verification.");
         return false;
    }

    // Compare the original data with the data read back from flash.
    if (memcmp(&page, &read_back_page, sizeof(page)) == 0) {
        Serial.println("Write verification successful.");
    } else {
        Serial.println("Write verification failed due to mismatch.");
        return false; 
    }
    */
    
    // Increment pointer and save metadata for persistence
    flash_pointer += sizeof(Page); // move flash pointer to next page
    saveMetadata(); // save current iteration and flash pointer to NVS    
    return true;
}

void flushPartialPage()
{
    uint32_t points_in_page = iteration % POINTS_PER_PAGE;
    if (points_in_page > 0)
    {
        printf("Flushing partial page with %lu points.\n", points_in_page);
        for (uint32_t i = points_in_page; i < POINTS_PER_PAGE; ++i)
        {
            current_page[i] = {}; // fill rest with zeros I guess
        }

        flushPageToFlash(current_page);
    }
}

void saveMetadata()
{
    preferences.begin("datalogger", false);
    preferences.putUInt("iter", iteration);
    preferences.putUInt("flash_ptr", flash_pointer);
    preferences.end();
    Serial.println("Metadata saved to non volatile storage.");
}

void loadMetadata()
{
    preferences.begin("datalogger", true);
    iteration = preferences.getUInt("iter", 0);
    flash_pointer = preferences.getUInt("flash_ptr", 0);
    preferences.end();
    Serial.printf("Metadata loaded: iteration=%lu, flash_pointer=%lu\n", iteration, flash_pointer);
}

// Dumps entire flash storage to Serial for debugging purposes
// this will be a long output. it should be entirely unformatted
// just writing each byte in hex format
void dumpFlash()
{
    // for (uint32_t addr = 0; addr < flash_capacity; addr += 16) {
    //     Serial.printf("%08X: ", addr);
    //     for (uint32_t i = 0; i < 16; ++i) {
    //         if (addr + i < flash_capacity) {
    //             Serial.printf("%02X ", flash.readByte(addr + i));
    //         } else {
    //             Serial.print("   ");
    //         }
    //     }
    //     Serial.println();
    // }

    Page data_page_buffer;
    uint32_t current_read_address = 0;
    uint32_t points_retrieved = 0;
    uint32_t total_points_to_read = iteration;

    Serial.printf("Preparing to download %lu data points. Flash pointer at %lu.\r\n", total_points_to_read, flash_pointer);

    Serial.printf("Iteration,Timestamp,NTC0_Temp,NTC1_Temp,TC0_Temp,TC1_Temp,TC2_Temp,TC3_Temp\r\n");

    while (current_read_address < flash_pointer && points_retrieved < total_points_to_read) {
        if (!flash.readAnything(current_read_address, data_page_buffer)) {
            Serial.printf("Flash read error at address %lu\n", current_read_address);
            break;
        }

        for (int i = 0; i < POINTS_PER_PAGE; ++i) {
            if (points_retrieved >= total_points_to_read) break;

            const DataPoint& dp = data_page_buffer[i];
            Serial.printf("%lu,%lu,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\r\n",
                                dp.iteration, dp.timestamp, dp.ntc0_temp, dp.ntc1_temp,
                                dp.tc0_temp, dp.tc1_temp, dp.tc2_temp, dp.tc3_temp);
            points_retrieved++;
        }
        current_read_address += sizeof(Page);
        
    }
    
    Serial.printf("Sent %lu data points.\r\n", points_retrieved);
}

String renderLedToggleDiv()
{
    String html = "<div id=\"led-toggle-div\">";
    html += "<label for=\"led-toggle\">Status LED:</label>";
    html += "<input type=\"checkbox\" id=\"led-toggle\" hx-post=\"/toggle_status_led\" hx-trigger=\"change\" hx-target=\"#led-toggle-div\" hx-swap=\"outerHTML\"";
    html += (led_state) ? " checked>" : ">";
    html += "<strong>";
    html += (led_state) ? "ON" : "OFF";
    html += "</strong></div>";
    return html;
}

String renderUserButtonDiv()
{
    String html = "<div id=\"user-button-div\">";
    html += "<button hx-get=\"/user_button_state\" hx-target=\"#user-button-div\" hx-swap=\"outerHTML\">Get User Button State</button>";
    bool button_is_pressed = user_btn.read();
    html += "<div>User Button State: <strong>";
    html += button_is_pressed ? "PRESSED" : "RELEASED";
    html += "</strong></div></div>";
    return html;
}

String renderRecordingToggleDiv()
{
    String html = "<div id=\"recording-toggle-div\">";
    html += "<div>Recording is currently <strong>";
    html += (logging_state == LoggingState::RECORDING) ? "ON" : "OFF";
    html += "</strong>.</div>";

    html += "<button hx-post=\"/toggle_recording\" hx-target=\"#recording-toggle-div\" hx-swap=\"outerHTML\" hx-trigger=\"click\">";
    html += (logging_state == LoggingState::RECORDING) ? "Stop Recording" : "Start Recording";
    html += "</button>";
    html += "</div>";
    return html;
}

String renderTimestepInputDiv()
{
    String html = "<div id=\"record-rate-div\">";
    html += "<div>Current recording interval: <strong>" + String(logging_interval) + "</strong> ms</div>";

    html += "<label for=\"record-rate\">Set recording interval:</label>";
    html += "<input type=\"number\" id=\"record-rate\" name=\"rate\" value=\"" + String(logging_interval) + "\" min=\"1\" hx-post=\"/set_rate\" hx-trigger=\"change\" hx-target=\"#record-rate-div\" hx-swap=\"outerHTML\">";
    html += " ms";
    html += "</div>";
    return html;
}

String renderDownloadDiv()
{
    String html = "<div id=\"download-data-div\">";
    html += "<div>Click the button below to download all logged temperature data in CSV format:</div>";
    // Using the <a> tag for downloads is better in my experience. The download attr gives a default filename.
    html += "<a href=\"/download_data\" download=\"datalog.csv\"><button type=\"button\">Download Data</button></a>";
    html += "</div>";
    return html;
}

String renderFlashDiv()
{
    String html = "<div id=\"flash-usage-div\">";

    float usage_percent = (flash_capacity > 0) ? ((float)flash_pointer / flash_capacity) * 100.0 : 0.0;
    html += "<div>Flash Usage: " + String(flash_pointer) + " / " + String(flash_capacity) + " bytes (<strong>" + String(usage_percent, 1) + "%</strong>)</div>";
    html += "<div>Total data points logged: <strong>" + String(iteration) + "</strong></div>";
    

    html += "<button hx-get=\"/flash_usage\" hx-target=\"#flash-usage-div\" hx-swap=\"outerHTML\" hx-trigger=\"click, every 10s\">Refresh Usage</button>";

    html += "<button hx-post=\"/format_flash\" hx-target=\"#flash-usage-div\" hx-swap=\"outerHTML\" ";
    html += "hx-confirm=\"Are you sure you want to erase ALL data?\">";
    html += "Format Flash Storage</button>";

    html += "</div>";
    return html;
}

String renderTemperatureTable()
{
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

String renderIndex()
{
    String html = "<!DOCTYPE html><html lang=\"en\"><head>";
    html += "<meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">";
    html += "<title>CUR Payload Datalogger</title>";
    html += "<link rel=\"icon\" href=\"/favicon.ico\" type=\"image/x-icon\">";
    html += "<style>body{font-family:sans-serif;margin:20px}div{margin-bottom:15px}table{border-collapse:collapse;margin-top:10px}td,th{border:1px solid #ccc;padding:8px;text-align:left}th{background-color:#f2f2f2}input[type='number']{width:80px;padding:4px}button{padding:8px 12px;margin:4px;background-color:#004e42;color:#fff;border:none;border-radius:4px;cursor:pointer}button:hover{background-color:#ffcd00;color:#000}a button{text-decoration:none}input,textarea,button,select,a{-webkit-tap-highlight-color: transparent;}</style>";
    html += "<script src=\"/htmx.min.js\"></script></head>";
    html += "<body>";
    html += "<img src=\"/logo.png\" alt=\"CUR Logo\" style=\"width:100px;\">";
    html += "<h1>CUR Payload Datalogger</h1>";
    // html += "<h2>Device Control & Status</h2>";
    // html += renderLedToggleDiv();
    // html += renderUserButtonDiv();
    // html += "<h2>Data Logging</h2>";
    html += renderRecordingToggleDiv();
    html += renderTimestepInputDiv();
    html += renderDownloadDiv();
    html += renderFlashDiv();
    html += "<h2>Live Temperature Sensors</h2>";
    // html += "<div><button hx-get=\"/refresh_table\" hx-target=\"#temperature-table\" hx-swap=\"outerHTML\" hx-trigger=\"click, every 1s\">Refresh Temperature Data</button></div>"; // outerHTML to replace table
    html += "<div id=\"temperature-table-container\" hx-get=\"/refresh_table\" hx-target=\"#temperature-table\" hx-swap=\"outerHTML\" hx-trigger=\"every 1s\">" + renderTemperatureTable() + "</div>";                                                                                       // Initial table load
    html += "<script>document.addEventListener('DOMContentLoaded', (event) => {htmx.process(document.body);});</script>";                                                           // Ensure HTMX processes dynamically added content if needed, or ensure refresh button targets correctly.
    html += "</body></html>";

    return html;
}