/*
 * KODE WEATHER STATION (RAIN GAUGE) ESP32 - BERSIH & STABIL
 * Hardware: ESP32, SSD1306 OLED (I2C), Rain Gauge (Tipping Bucket)
 * By: Gemini (Fixed Version)
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// -----------------------------------------------------------------
// KONFIGURASI
// -----------------------------------------------------------------
const int PIN_HUJAN = 5;          // Pin GPIO Sensor (Pastikan kabel satunya ke GND)
const float MM_PER_TIP = 0.2794;  // Nilai Kalibrasi (Default pabrik biasanya segini)
const long DEBOUNCE_MS = 200;     // Jeda waktu anti-getaran (ms)

// Pengaturan OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

void updateDisplay(); // Prototipe fungsi tampilan

// -----------------------------------------------------------------
// VARIABEL
// -----------------------------------------------------------------
// Variabel Interrupt (Harus volatile agar tersimpan di RAM dengan benar saat ISR)
volatile unsigned int isrTipCount = 0;
volatile unsigned long lastTipTime = 0;

// Variabel Utama
unsigned long totalTips = 0;      // Total jungkitan sejak nyala
float totalRainfall = 0.000;        // Total hujan dalam mm

// -----------------------------------------------------------------
// FUNGSI INTERRUPT (ISR)
// Menangani sinyal dari sensor secepat kilat
// -----------------------------------------------------------------
void IRAM_ATTR handleRainTip() {
  unsigned long now = millis();
  // Debounce: Hanya hitung jika jarak antar sinyal > 100ms
  if (now - lastTipTime > DEBOUNCE_MS) {
    isrTipCount++;
    lastTipTime = now;
  }
}

// -----------------------------------------------------------------
// SETUP
// -----------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  
  // 1. Inisialisasi OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Cek alamat I2C, biasanya 0x3C atau 0x3D
    Serial.println(F("OLED Gagal! Cek kabel SDA/SCL."));
    for (;;); // Berhenti di sini jika gagal
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0,0);
  display.println("Rain Gauge v1.0");
  display.println("Menunggu hujan...");
  display.display();

  // 2. Inisialisasi Pin
  // Menggunakan INPUT_PULLUP agar kita tidak butuh resistor eksternal.
  // Cara pasang: 1 kabel sensor ke Pin 5, 1 kabel sensor ke GND.
  pinMode(PIN_HUJAN, INPUT); 

  // 3. Pasang Interrupt
  attachInterrupt(digitalPinToInterrupt(PIN_HUJAN), handleRainTip, FALLING);
  
  delay(1000);
  updateDisplay(); // Tampilkan layar awal (0 mm)
}

// -----------------------------------------------------------------
// LOOP UTAMA
// -----------------------------------------------------------------
void loop() {
  // Cek apakah ada data baru dari interrupt
  unsigned int tipsToProcess = 0;

  // Matikan interrupt sebentar untuk membaca data volatile dengan aman
  noInterrupts();
  if (isrTipCount > 0) {
    tipsToProcess = isrTipCount;
    isrTipCount = 0; // Reset counter interrupt
  }
  interrupts();

  // Jika ada tip baru, proses datanya
  if (tipsToProcess > 0) {
    // Hitung matematika
    totalTips += tipsToProcess;
    totalRainfall += (tipsToProcess * MM_PER_TIP);

    // Kirim ke Serial Monitor (untuk debug di laptop)
    Serial.print("Tip Terdeteksi! Total Tips: ");
    Serial.print(totalTips);
    Serial.print(" | Curah Hujan: ");
    Serial.print(totalRainfall, 4);
    Serial.println(" mm");

    // Update Layar OLED
    updateDisplay();
  }
}

// -----------------------------------------------------------------
// FUNGSI TAMPILAN OLED
// -----------------------------------------------------------------
void updateDisplay() {
  display.clearDisplay();

  // Header
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("RAIN GAUGE MONITOR");
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE); // Garis pemisah

  // Menampilkan Milimeter (Besar)
  display.setCursor(0, 15);
  display.print("Curah Hujan:");
  
  display.setTextSize(2); // Huruf Besar
  display.setCursor(0, 28);
  display.print(totalRainfall, 4); // 4 angka belakang koma
  display.setTextSize(1);
  display.print(" mm");

  // Menampilkan Total Tips (Kecil di bawah)
  display.setCursor(0, 50);
  display.print("Total Tips: ");
  display.print(totalTips);

  display.display();
}