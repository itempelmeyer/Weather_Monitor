// Time zone configuration
const char* ntpServerIP = "pool.ntp.org"; // Replace with a resolved IP for pool.ntp.org
const long gmtOffset_sec = -21600;  // GMT-6 for CST
const int daylightOffset_sec = 3600; // Daylight saving time offset

// DHT sensor settings
#define DHTPIN 4       // GPIO pin connected to the DHT11
#define DHTTYPE DHT11  // DHT11 sensor type


// SD Card settings
#define SD_CS_PIN 5    // GPIO pin connected to SD card module's CS pin

unsigned long wifiConnectedTime = 0; // Time when Wi-Fi connection was established
bool wifiConnectedOnce = false;     // Tracks if Wi-Fi was successfully connected

float celsiusToFahrenheit(float celsius) {
    return (celsius * 9.0 / 5.0) + 32.0;
}
