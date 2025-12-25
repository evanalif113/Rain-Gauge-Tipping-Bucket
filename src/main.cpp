/*
 * KODE WEATHER STATION (RAIN GAUGE) ESP32 - SEDERHANA
 * * Menggunakan: SSD1306 OLED, Tipping Bucket
 * * Fitur:
 * - Non-blocking loop (menggunakan millis() timers)
 * - Menampilkan hasil bacaan sensor curah hujan ke OLED
 * - Tanpa WiFi, tanpa simpan ke EEPROM, tanpa RTC
 */

// -----------------------------------------------------------------
// PENGATURAN YANG HARUS ANDA UBAH
// -----------------------------------------------------------------

// --- Pengaturan Sensor Hujan ---
const int PIN_HUJAN = 5;           // Pin GPIO untuk sensor
const float MM_PER_TIP = 0.2794;   // KALIBRASI SENSOR ANDA
const long DEBOUNCE_MS = 100;      // Debounce interrupt

// -----------------------------------------------------------------
// LIBRARY YANG DIBUTUHKAN
// -----------------------------------------------------------------
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// -----------------------------------------------------------------
// PENGATURAN OLED
// -----------------------------------------------------------------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// -----------------------------------------------------------------
// VARIABEL GLOBAL
// -----------------------------------------------------------------

// --- Variabel Interrupt (WAJIB volatile) ---
volatile unsigned int tipCount = 0;
volatile long lastTipTime = 0;

// --- Variabel Akumulasi ---
float rainfallToday = 0.0;

// --- Timer tampilan ---
unsigned long lastDisplayTime = 0;

// --- Prototipe Fungsi ---
void updateDisplay();
void IRAM_ATTR hitungHujan();

// -----------------------------------------------------------------
// FUNGSI INTERRUPT (ISR)
// (WAJIB pakai IRAM_ATTR di ESP32)
// -----------------------------------------------------------------
void IRAM_ATTR hitungHujan() {
  long nowMs = millis();
  if (nowMs - lastTipTime > DEBOUNCE_MS) {
    tipCount++;
    lastTipTime = nowMs;
  }
}

// -----------------------------------------------------------------
// FUNGSI SETUP (Berjalan sekali saat boot)
// -----------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial.println("\nBooting Rain Gauge...");

  // --- Inisialisasi OLED ---
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("Alokasi SSD1306 gagal"));
    for (;;);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Booting...");
  display.println("Init I2C & OLED...");
  display.display();

  // --- Konfigurasi Pin Sensor ---
  pinMode(PIN_HUJAN, INPUT);

  // --- Pasang Interrupt ---
  attachInterrupt(PIN_HUJAN, hitungHujan, FALLING);

  display.setCursor(0, 20);
  display.println("Sistem Siap");
  display.display();
  delay(1000);
}

// -----------------------------------------------------------------
// FUNGSI LOOP (NON-BLOCKING)
// -----------------------------------------------------------------
void loop() {
  unsigned long currentTime = millis();

  // --- Proses Tip (jika ada) ---
  unsigned int tipsToProcess = 0;
  noInterrupts();
  if (tipCount > 0) {
    tipsToProcess = tipCount;
    tipCount = 0;
  }
  interrupts();

  if (tipsToProcess > 0) {
    float newRainfall = tipsToProcess * MM_PER_TIP;
    rainfallToday += newRainfall;
    Serial.print("TIP! +");
    Serial.print(newRainfall, 3);
    Serial.print(" mm | Total: ");
    Serial.print(rainfallToday, 3);
    Serial.println(" mm");
  }

  // --- Update Display setiap 500 ms ---
  if (currentTime - lastDisplayTime > 500) {
    lastDisplayTime = currentTime;
    updateDisplay();
  }
}

// -----------------------------------------------------------------
// FUNGSI UNTUK UPDATE TAMPILAN OLED
// -----------------------------------------------------------------
void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.println("--- RAIN GAUGE ---");

  display.setCursor(0, 16);
  display.print("Tips: ");
  display.print((unsigned long)(tipCount)); // nilai volatile current

  display.setCursor(0, 28);
  display.print("Curah Hujan:");
  display.setCursor(0, 40);
  display.print(rainfallToday, 2);
  display.print(" mm");

  display.setCursor(0, 56);
  display.print("WiFi/EEPROM: OFF");

  display.display();
}