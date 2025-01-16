#include <Wire.h>
#include <heltec.h>
#include "DHT.h"    // For DHT11 sensor
#include "images.h"
#include "header.h"
#include <WiFi.h>
#include "time.h"
#include <SD.h>     // SD card library
#include "credentials.h"

void handleClientRequest();
void logClientRequest(WiFiClient &client);
String readGraphDataFromSD();
void serveHtmlPage(WiFiClient &client, const String &graphData);

void flushLogCache(); // Forward declaration


DHT dht(DHTPIN, DHTTYPE);

String html;

// Wi-Fi and web server
WiFiServer server(80); // HTTP server
#define CACHE_SIZE 10
struct LogEntry {
    String time;
    float temperature;
    float humidity;
    float uptimeHours;
    int rssi;
};

LogEntry logCache[CACHE_SIZE];
int logIndex = 0;

#include <ESP_Mail_Client.h>

const char* ntpServer = "pool.ntp.org"; // Replace with your NTP server if needed

SMTPSession smtp;
void sendEmail(String subject, String message) {
    smtp.debug(1);

    ESP_Mail_Session session;
    session.server.host_name = smtp_server;
    session.server.port = smtp_port;
    session.login.email = smtp_user;
    session.login.password = smtp_pass;
    session.time.ntp_server = ntpServer;
    session.time.gmt_offset = gmtOffset_sec;
    session.time.day_light_offset = daylightOffset_sec;

    SMTP_Message email;
    email.sender.name = "Weather Station";
    email.sender.email = smtp_user;
    email.subject = subject;
    email.addRecipient("Ian", recipient_email);
    email.text.content = message;

    if (!smtp.connect(&session)) {
        Serial.println("Failed to connect to SMTP server!");
        return;
    }

    if (!MailClient.sendMail(&smtp, &email)) {
        Serial.println("Error sending email: " + smtp.errorReason());
    } else {
        Serial.println("Email sent successfully.");
    }
    smtp.closeSession();
}

void cacheLogData(String currentTime, float temperature, float humidity, float uptimeHours, int rssi) {
    logCache[logIndex++] = {currentTime, temperature, humidity, uptimeHours, rssi};
    if (logIndex >= CACHE_SIZE) {
        flushLogCache();
        logIndex = 0;
    }
}

void flushLogCache() {
    File file = SD.open("/data_log.txt", FILE_APPEND);
    if (file) {
        for (int i = 0; i < logIndex; i++) {
            file.print(logCache[i].time + ",");
            file.print(logCache[i].temperature, 1);
            file.print(",");
            file.print(logCache[i].humidity, 1);
            file.print(",");
            file.print(logCache[i].uptimeHours, 2);
            file.print(",");
            file.println(logCache[i].rssi);
        }
        file.close();
    } else {
        Serial.println("Failed to open file for writing");
    }
}

void monitorMemory() {
    static unsigned long lastHeapLogTime = 0;
    unsigned long currentMillis = millis();
    if (currentMillis - lastHeapLogTime >= 1000) { // Log every second
        Serial.print("Free heap: ");
        Serial.println(ESP.getFreeHeap());
        lastHeapLogTime = currentMillis;
    }
}

void displayLogo() {
    Heltec.display->clear();
    Heltec.display->drawXbm(0, 5, logo_width, logo_height, (const unsigned char *)logo_bits);
    Heltec.display->display();
    delay(2000); // Display logo for 2 seconds
    Heltec.display->clear(); // Clear screen after logo
}

void connectToWiFi() {
    Heltec.display->drawString(0, 0, "Connecting to WiFi...");
    Heltec.display->display();

    WiFi.setTxPower(WIFI_POWER_17dBm); // Set maximum Wi-Fi transmission power
    WiFi.mode(WIFI_STA); // Station mode only
    IPAddress local_IP(192, 168, 254, 77); // Replace with a valid static IP for your network
    IPAddress gateway(192, 168, 254, 254);   // Replace with your network's gateway
    IPAddress subnet(255, 255, 255, 0);  // Replace with your subnet mask
    IPAddress dns(8, 8, 8, 8); // Google Public DNS
    //WiFi.config(local_IP, gateway, subnet);
    

    if (!WiFi.config(local_IP, gateway, subnet, dns)) {
        Serial.println("STA Failed to configure");
    }

    Serial.print("Connecting to WiFi...");
    WiFi.begin(ssid, password);

     unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
        delay(500); // Short wait to let Wi-Fi stack process
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWi-Fi connected!");
        Serial.println("IP Address: " + WiFi.localIP().toString());

        Heltec.display->clear();
        Heltec.display->drawString(0, 0, "WiFi Connected!");
        Heltec.display->drawString(0, 16, WiFi.localIP().toString());
        Heltec.display->display();
        delay(2000);
                
        // Ensure clear transition to next screen
        Heltec.display->clear();
        Heltec.display->display();
        delay(500);

        wifiConnectedTime = millis(); // Record the time when Wi-Fi is connected
        wifiConnectedOnce = true;    // Mark that Wi-Fi was connected

        server.begin(); // Start the web server
        Serial.println("Web server started.");

    } else {
        Serial.println("\nFailed to connect to Wi-Fi.");
        Heltec.display->clear();
        Heltec.display->drawString(0, 0, "WiFi Failed!");
        Heltec.display->display();
        delay(1000);
    }
}

void reconnectWiFi() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Wi-Fi lost. Attempting to reconnect...");
        //WiFi.disconnect(true); // Force disconnect
        delay(100);            // Small delay to reset Wi-Fi stack
        WiFi.begin(ssid, password); // Reconnect
        
        unsigned long startAttemptTime = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) { // 10s timeout
            Serial.print(".");
            delay(500);
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\nWi-Fi reconnected successfully!");
        } else {
            Serial.println("\nWi-Fi reconnection failed.");
        }
    }
}

void setupTime() {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServerIP);
    Serial.println("Waiting for NTP time sync...");
    Heltec.display->clear();
    Heltec.display->drawString(0, 0, "Waiting for...");
    Heltec.display->drawString(0, 20, "NTP time sync");
    Heltec.display->display();

    struct tm timeinfo;

    for (int i = 0; i < 1; i++) { // Retry up to 10 times
        if (getLocalTime(&timeinfo)) {
            Serial.println("Time synchronized with NTP.");
            Heltec.display->clear();
            Heltec.display->drawString(0, 0, "NTP Syncronized");
            Heltec.display->display();
            return;
        }
        delay(1000); // Wait 1 second before retrying
        Serial.println("Retrying NTP time sync...");
        Heltec.display->clear();
        Heltec.display->drawString(0, 0, "Retrying NTP");
        Heltec.display->drawString(0, 20, "...time sync");
        Heltec.display->display();
    }

    Serial.println("Failed to synchronize time with NTP.");
}

String getCurrentTime() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "Time Error!";
    }
    char timeString[30];
    strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return String(timeString);
}

void logDataToSD(String currentTime, float temperature, float humidity, float uptimeHours, int rssi) {
    File file = SD.open("/data_log.txt", FILE_APPEND);
    if (file) {
        file.print(currentTime + ",");
        file.print(temperature, 1);
        file.print(",");
        file.print(humidity, 1);
        file.print(",");
        file.print(uptimeHours, 2);
        file.print(",");
        file.println(rssi);
        file.close();
        Serial.println("Data logged to SD card.");
    } else {
        Serial.println("Failed to open file for writing");
        Heltec.display->clear();
        Heltec.display->drawString(0, 0, "SD Write Failed!");
        Heltec.display->display();
    }
}

void setupSDCard() {
    Heltec.display->clear();
    Heltec.display->drawString(0, 0, "Init SD Card...");
    Heltec.display->display();
    if (!SD.begin(SD_CS_PIN)) {
        Serial.println("SD Card Mount Failed");
        Heltec.display->drawString(0, 20, "SD Mount Failed!");
        Heltec.display->display();
        while (1); // Stop execution if SD card initialization fails
    }
    Serial.println("SD Card Initialized");
    Heltec.display->drawString(0, 20, "SD Initialized!");
    Heltec.display->display();
}

void handleClientRequest() {
    WiFiClient client = server.available();
    if (client) {
        logClientRequest(client);

        String graphData = readGraphDataFromSD();
        serveHtmlPage(client, graphData);

        client.stop();
        Serial.println("[" + String(millis()) + " ms] " + "Client disconnected.");
    }
}

void logClientRequest(WiFiClient &client) {
    Serial.print("[" + String(millis()) + " ms] Heap before streaming: ");
    Serial.println(ESP.getFreeHeap());
    Serial.println("[" + String(millis()) + " ms] New client connected.");

    String request = client.readStringUntil('\r');
    Serial.println("[" + String(millis()) + " ms] HTTP Request received:");
    Serial.println(request);
}

String readGraphDataFromSD() {
    String graphData = "";
    Serial.println("[" + String(millis()) + " ms] Opening SD file for reading...");
    File file = SD.open("/data_log.txt", FILE_READ);

    if (file) {
        Serial.println("[" + String(millis()) + " ms] Transmitting data to webpage...");
        const int averageLineLength = 50; // Average bytes per line
        int skipInterval = 600;          // Include every 600th line
        int numPoints = 100;            // Number of points to include in the graph

        int startByte = max(0, (int)file.size() - (numPoints * skipInterval * averageLineLength));
        file.seek(startByte);

        int lineCount = 0;
        while (file.available()) {
            String line = file.readStringUntil('\n');
            line.trim();

            if (lineCount % skipInterval == 0) {
                graphData += line + "\\n";
                Serial.print("[" + String(millis()) + " ms] Sent line: " + line);
                Serial.println(" (Row " + String(lineCount) + ")");
            }

            lineCount++;
        }
        file.close();
    } else {
        graphData = "No data available";
        Serial.println("[" + String(millis()) + " ms] Failed to open file.");
    }

    return graphData;
}

void serveHtmlPage(WiFiClient &client, const String &graphData) {
    String html = "<!DOCTYPE html><html><head><title>ESP32 Weather Station</title>";
    html += "<meta charset='UTF-8'>";
    html += "<script src='https://cdn.jsdelivr.net/npm/chart.js'></script></head>";
    html += "<meta http-equiv='refresh' content='600'>"; // Refresh every 600 seconds
    html += "<body><h1>ESP32 Weather Station</h1>";
    html += "<canvas id='dataChart' width='400' height='200'></canvas>";
    html += "<p style='text-align:right;'>Last Updated: " + getCurrentTime() + "</p>";
    html += "<script>";
    html += "const ctx = document.getElementById('dataChart').getContext('2d');";
    html += "const rawData = `" + graphData + "`.split('\\n');";
    html += "const labels = []; const tempData = []; const humData = []; const rssiData = [];";
    html += "rawData.forEach(line => {";
    html += "  const [time, temp, hum, uptime, rssi] = line.split(',');";
    html += "  labels.push(time.trim());";
    html += "  tempData.push(parseFloat(temp));";
    html += "  humData.push(parseFloat(hum));";
    html += "  rssiData.push(parseInt(rssi));";
    html += "});";
    html += "new Chart(ctx, {";
    html += "  type: 'line',";
    html += "  data: {";
    html += "    labels: labels,";
    html += "    datasets: [";
    html += "      { label: 'Temperature (°F)', data: tempData, borderColor: 'red', borderWidth: 1 },";
    html += "      { label: 'Humidity (%)', data: humData, borderColor: 'blue', borderWidth: 1 },";
    html += "      { label: 'RSSI (dBm)', data: rssiData, borderColor: 'green', borderWidth: 1 }";
    html += "    ]";
    html += "  },";
    html += "  options: { responsive: true, scales: { y: { beginAtZero: true } } }";
    html += "});";
    html += "</script></body></html>";

    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html");
    client.println("Connection: close");
    client.println();
    client.println(html);
}

void checkTimeSync() {
    static bool timeSynced = false;
    static unsigned long lastSyncAttempt = 0;

    if (!timeSynced && millis() - lastSyncAttempt > 5000) { // Retry every 5 seconds
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            Serial.println("Time synchronized!");
            timeSynced = true;
        } else {
            Serial.println("Retrying NTP sync...");
        }
        lastSyncAttempt = millis();
    }
}

void setup() {
    Serial.begin(115200);
    html.reserve(2048); // Reserve enough memory to avoid fragmentation

    // Initialize the OLED display
    Heltec.begin(true /*Display Enable*/, false /*LoRa Disable*/, true /*Serial Enable*/);
    Heltec.display->setFont(ArialMT_Plain_16);  // Use default font

    // Show the logo
    displayLogo();

    // Initialize DHT sensor
    dht.begin();

    // Connect to Wi-Fi
    connectToWiFi();

    // Synchronize time with NTP
    setupTime();

    // Initialize SD card
    setupSDCard();
}

void keepWiFiAlive() {
    static unsigned long lastCheckTime = 0;
    if (millis() - lastCheckTime > 10000) { // Check every 10 seconds
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("Wi-Fi disconnected. Attempting to reconnect...");
            reconnectWiFi();
        } else {
            Serial.println("Wi-Fi connection is stable.");
        }
        lastCheckTime = millis();
    }
}

void logWiFiStatus() {
    int status = WiFi.status();
    switch (status) {
        case WL_CONNECTED: Serial.println("Wi-Fi Status: Connected"); break;
        case WL_NO_SSID_AVAIL: Serial.println("Wi-Fi Status: SSID Not Available"); break;
        case WL_CONNECT_FAILED: Serial.println("Wi-Fi Status: Connection Failed"); break;
        case WL_CONNECTION_LOST: Serial.println("Wi-Fi Status: Connection Lost"); break;
        case WL_DISCONNECTED: Serial.println("Wi-Fi Status: Disconnected"); break;
        default: Serial.println("Wi-Fi Status: Unknown");
    }
}

void serialPrint (String currentTime, float temperature, float temperatureF, float humidity, float uptimeHours){
    Serial.println("===== Weather Station Data =====");
    Serial.println("[" + String(millis()) + " ms] " + "Time: " + currentTime);
    Serial.println("Temperature: " + String(temperature, 1) + " °C");
    Serial.println("Temperature: " + String(temperatureF, 1) + " °F");
    Serial.println("Humidity: " + String(humidity, 1) + " %");
    Serial.println("Uptime: " + String(uptimeHours, 2) + " h");
    Serial.println("RSSI: " + String(WiFi.RSSI()) + " dBm");
    Serial.println("Status: " + String(WiFi.status()));
    Serial.println("==============================");
}

void heltecDisplay(String currentTime, float temperatureF, float humidity, float uptimeHours){
    Heltec.display->clear();
    Heltec.display->drawString(0, 0, currentTime);
    Heltec.display->drawString(0, 16, "Temp: " + String(temperatureF, 1) + " °F");
    Heltec.display->drawString(0, 32, "Humidity: " + String(humidity, 1) + " %");
    Heltec.display->drawString(0, 48, "Uptime: " + String(uptimeHours, 2) + " h");
    Heltec.display->display();
}

void loop() {
    static unsigned long lastEmailTime = 0;
    static bool tempAboveZero = false;

    static unsigned long lastLogTime = 0;
    static unsigned long lastClientCheckTime = 0;
    static unsigned long lastStatusTime = 0;

    unsigned long currentMillis = millis();

    monitorMemory();


    // Log Wi-Fi status every 5 seconds
    if (currentMillis - lastStatusTime >= 5000) {
        logWiFiStatus();
        lastStatusTime = currentMillis;
    }

    // Log data to SD card and update displays every second
    if (currentMillis - lastLogTime >= 1000) {
        float humidity = dht.readHumidity();
        float temperature = dht.readTemperature();
        String currentTime = getCurrentTime();
        float temperatureF = celsiusToFahrenheit(temperature);
        int rssi = WiFi.RSSI();
        float uptimeHours = wifiConnectedOnce ? (float)(currentMillis - wifiConnectedTime) / (3600000.0) : 0;

        cacheLogData(currentTime, temperatureF, humidity, uptimeHours, rssi);
        heltecDisplay(currentTime, temperatureF, humidity, uptimeHours);
        serialPrint(currentTime, temperature, temperatureF, humidity, uptimeHours);
       
        // Check temperature threshold and send email if needed
        if (temperatureF > 32) {
            if (!tempAboveZero || (currentMillis - lastEmailTime >= 3600000)) {
                sendEmail("Freezer Temperature Alert", "Temperature has risen above 32°F, Currently " + String(temperatureF, 1) + "°F");
                lastEmailTime = currentMillis;
            }
            tempAboveZero = true;
        } else {
            tempAboveZero = false; // Reset when temperature drops below 0°C
        }

        lastLogTime = currentMillis;
    }

    // Handle client requests every 200ms
    if (currentMillis - lastClientCheckTime >= 200) {
        handleClientRequest();
        lastClientCheckTime = currentMillis;
    }

    // Check Wi-Fi and keep connection alive
    if (WiFi.status() != WL_CONNECTED) {
        reconnectWiFi();
    } else {
        keepWiFiAlive();
    }

    // Check time synchronization
    checkTimeSync();
}
