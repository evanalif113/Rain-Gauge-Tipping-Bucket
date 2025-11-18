/*
 * KODE WEATHER STATION (RAIN GAUGE) ESP32 - VERSI PERBAIKAN
 * * Menggunakan: DS3231 (RTC + AT24C32 EEPROM), SSD1306 OLED, Tipping Bucket
 * * Fitur:
 * - Non-blocking loop (menggunakan millis() timers)
 * - Inisialisasi EEPROM aman (Magic Number) untuk perbaikan 'nan'
 * - Penyimpanan data andal ke EEPROM eksternal (AT24C32)
 */

// -----------------------------------------------------------------
// PENGATURAN YANG HARUS ANDA UBAH
// -----------------------------------------------------------------

// --- Pengaturan WiFi (Hanya untuk sinkronisasi waktu) ---
const char* ssid = "server";
const char* password = "jeris6467";

// --- Pengaturan Waktu (Timezone untuk NTP) ---
const long gmtOffset_sec = 25200; // GMT+7
const int daylightOffset_sec = 0; 

// --- Pengaturan Sensor Hujan ---
const int PIN_HUJAN = 5;         // Pin GPIO untuk sensor
const float MM_PER_TIP = 0.2794;   // KALIBRASI SENSOR ANDA
const long DEBOUNCE_MS = 100;      // Debounce interrupt

// --- Pengaturan Penyimpanan ---
// Simpan tiap 5 menit. SANGAT AMAN di EEPROM AT24C32 (1 Juta siklus tulis)
const long SAVE_INTERVAL_MS = 300000; // 5 Menit = 300.000 ms

// -----------------------------------------------------------------
// LIBRARY YANG DIBUTUHKAN
// -----------------------------------------------------------------
#include <WiFi.h>
#include "time.h" // Diperlukan untuk NTP
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RTClib.h> // Library untuk DS3231

// -----------------------------------------------------------------
// PENGATURAN OLED DAN RTC
// -----------------------------------------------------------------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

RTC_DS3231 rtc; // Buat objek untuk RTC DS3231

// -----------------------------------------------------------------
// PENGATURAN PENYIMPANAN EEPROM (UNTUK FIX 'nan')
// -----------------------------------------------------------------
// Alamat I2C untuk EEPROM di modul DS3231
#define EEPROM_I2C_ADDR 0x57

/* Peta Alamat Memori EEPROM: */
#define ADDR_RAINFALL   0  // Alamat untuk rainfallToday (float, 4 bytes)
#define ADDR_LASTDAY    4  // Alamat untuk lastCheckedDay (int, 4 bytes)
#define ADDR_MAGIC      8  // Alamat untuk Magic Number (int, 4 bytes)
#define MAGIC_NUMBER  12345  // "Kata sandi" unik kita untuk cek EEPROM baru

// -----------------------------------------------------------------
// VARIABEL GLOBAL
// -----------------------------------------------------------------

// --- Variabel Interrupt (WAJIB volatile) ---
volatile unsigned int tipCount = 0;
volatile long lastTipTime = 0;

// --- Variabel Akumulasi ---
float rainfallPerHour[24]; 
float rainfallToday = 0; // Akan dimuat dari EEPROM

// --- Variabel Laporan ---
float rainfallLastHour = 0;
float rainfallYesterday = 0; // Akan dimuat dari EEPROM jika boot di hari baru

// --- Variabel Pelacak Waktu ---
int lastCheckedHour = -1;
int lastCheckedDay = -1; // Akan dimuat dari EEPROM
unsigned long lastSaveTime = 0; // Timer untuk menyimpan ke EEPROM
unsigned long lastDisplayTime = 0; // Timer untuk update display & cek waktu

// --- Prototipe Fungsi ---
void updateDisplay(struct tm *timeinfo);
void getRTCTime(struct tm *timeinfo);
void syncRTCfromNTP();
void loadRainfallData();
void saveRainfallData();
void writeEEPROMFloat(int memAddr, float data);
float readEEPROMFloat(int memAddr);
void writeEEPROMInt(int memAddr, int data);
int readEEPROMInt(int memAddr);

// -----------------------------------------------------------------
// FUNGSI INTERRUPT (ISR)
// (WAJIB pakai IRAM_ATTR di ESP32)
// -----------------------------------------------------------------
void IRAM_ATTR hitungHujan() {
  if (millis() - lastTipTime > DEBOUNCE_MS) {
    tipCount++;
    lastTipTime = millis();
  }
}

// -----------------------------------------------------------------
// FUNGSI SETUP (Berjalan sekali saat boot)
// -----------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial.println("\nBooting Weather Station...");

  // --- 0. Inisialisasi OLED ---
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    Serial.println(F("Alokasi SSD1306 gagal"));
    for(;;);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println("Booting...");
  display.println("Inisialisasi I2C...");
  display.display();

  // --- 1. Inisialisasi RTC DS3231 ---
  if (!rtc.begin()) {
    Serial.println("Tidak dapat menemukan RTC DS3231!");
    display.println("RTC Gagal!");
    display.display();
    while (1);
  }
  
  Serial.println("RTC DS3231 Siap.");
  display.println("RTC Siap.");
  display.display();
  delay(500);

  // --- 2. Konfigurasi Pin Sensor ---
  pinMode(PIN_HUJAN, INPUT_PULLUP);

  // --- 3. Inisialisasi WiFi & Sinkronisasi NTP ---
  // (Logika WiFi Anda sudah bagus, tidak diubah)
  display.println("Menghubungkan WiFi...");
  display.display();
  Serial.print("Menghubungkan ke WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  
  int wifi_retries = 20; // Coba selama 10 detik
  while (WiFi.status() != WL_CONNECTED && wifi_retries > 0) {
    delay(500);
    Serial.print(".");
    wifi_retries--;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Terhubung!");
    display.println("WiFi Terhubung!");
    display.display();
    delay(500);
    if (rtc.lostPower()) {
        Serial.println("RTC kehilangan daya, sinkronisasi dari NTP.");
        syncRTCfromNTP();
    }
  } else {
    Serial.println("\nGagal terhubung WiFi.");
    display.println("WiFi Gagal!");
    display.display();
    delay(1500);
    if (rtc.lostPower()) {
        Serial.println("RTC kehilangan daya & NTP gagal. Set waktu dari kompilasi.");
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
  }
  
  // --- 4. Inisialisasi Variabel & MEMUAT DATA DARI EEPROM ---
  for (int i = 0; i < 24; i++) {
    rainfallPerHour[i] = 0; // Array per jam SELALU reset saat boot
  }
  
  // Memuat rainfallToday, rainfallYesterday, & lastCheckedDay
  loadRainfallData(); 
  
  // Dapatkan waktu awal dari RTC
  struct tm timeinfo;
  getRTCTime(&timeinfo);
  Serial.print("Waktu saat ini dari RTC: ");
  Serial.println(&timeinfo, "%A, %d %B %Y %H:%M:%S");

  // Set HANYA jam. Hari sudah di-set oleh loadRainfallData()
  lastCheckedHour = timeinfo.tm_hour;
  
  // --- 5. Pasang Interrupt (SETELAH SEMUA SIAP) ---
  attachInterrupt(PIN_HUJAN, hitungHujan, FALLING);
  
  Serial.println("\nSistem Siap. Menunggu data hujan...");
  display.clearDisplay();
  display.setCursor(0,0);
  display.println("Sistem Siap");
  display.display();
  delay(1500); // Tampilkan "Sistem Siap" selama 1.5 detik
}

// -----------------------------------------------------------------
// FUNGSI LOOP (NON-BLOCKING)
// -----------------------------------------------------------------
void loop() {
  
  unsigned long currentTime = millis(); // Ambil waktu 'millis' sekali
  
  // --- 1. Cek Apakah Ada Tip Baru (Secepat Mungkin) ---
  unsigned int tipsToProcess = 0;
  noInterrupts();
  if (tipCount > 0) {
    tipsToProcess = tipCount;
    tipCount = 0;
  }
  interrupts();

  // --- 2. Proses Tip (jika ada) ---
  if (tipsToProcess > 0) {
    struct tm timeinfo;
    getRTCTime(&timeinfo);
    
    float newRainfall = tipsToProcess * MM_PER_TIP;
    rainfallPerHour[timeinfo.tm_hour] += newRainfall;
    rainfallToday += newRainfall;

    Serial.print("TIP! "); Serial.print(newRainfall); Serial.println(" mm");
  }

  // --- 3. Cek Waktu & Update Display (Setiap 1 Detik) ---
  if (currentTime - lastDisplayTime > 1000) {
    lastDisplayTime = currentTime; // Reset timer display

    struct tm timeinfo;
    getRTCTime(&timeinfo);

    // Perbarui Tampilan OLED
    updateDisplay(&timeinfo);

    int currentHour = timeinfo.tm_hour;
    int currentDay = timeinfo.tm_mday;

    // --- A. Cek Pergantian HARI (prioritas tertinggi) ---
    if (currentDay != lastCheckedDay) {
      Serial.println("---------------------------------------------");
      Serial.println("PERGANTIAN HARI (Tengah Malam)");

      // Coba sinkronisasi ulang RTC dengan NTP setiap hari
      syncRTCfromNTP();

      // 1. Simpan total hari SEBELUM direset
      rainfallYesterday = rainfallToday;
      
      // 2. Simpan data jam terakhir (jam 23)
      rainfallLastHour = rainfallPerHour[23];

      // 3. Laporkan data
      Serial.print(">> LAPORAN JAM 23:00 : "); Serial.println(rainfallLastHour);
      Serial.print(">> LAPORAN HARIAN (Kemarin): "); Serial.println(rainfallYesterday);
      Serial.println("---------------------------------------------");

      // 4. RESET SEMUA AKUMULATOR HARI INI
      rainfallToday = 0;
      for (int i = 0; i < 24; i++) {
        rainfallPerHour[i] = 0;
      }
      
      // 5. Update pelacak waktu
      lastCheckedDay = currentDay;
      lastCheckedHour = currentHour; // (sekarang jam 0)
      
      // 6. SEGERA SIMPAN status reset (0.0) ke EEPROM
      saveRainfallData();
    }
    
    // --- B. Cek Pergantian JAM (jika hari masih sama) ---
    else if (currentHour != lastCheckedHour) {
      
      rainfallLastHour = rainfallPerHour[lastCheckedHour]; // Simpan data jam lalu

      Serial.println("---------------------------------------------");
      Serial.print(">> LAPORAN JAM "); Serial.print(lastCheckedHour);
      Serial.print(":00 : "); Serial.println(rainfallLastHour);
      Serial.println("---------------------------------------------");

      lastCheckedHour = currentHour;
    }
  } // Akhir dari blok 'if (millis() - lastDisplayTime)'

  // --- 4. Simpan Data Periodik (Setiap 5 Menit) ---
  if (currentTime - lastSaveTime > SAVE_INTERVAL_MS) {
    lastSaveTime = currentTime; // Reset timer simpan
    saveRainfallData(); // Panggil fungsi simpan
  }
} // Akhir dari loop()

// -----------------------------------------------------------------
// FUNGSI UNTUK SINKRONISASI RTC DARI SERVER NTP
// -----------------------------------------------------------------
void syncRTCfromNTP() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi tidak terhubung. Tidak bisa sinkronisasi NTP.");
    return;
  }
  Serial.println("Sinkronisasi Waktu (NTP) ke RTC...");
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Sinkronisasi NTP...");
  display.display();
  
  configTime(gmtOffset_sec, daylightOffset_sec, "id.pool.ntp.org");
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 10000)) { // Tunggu hingga 10 detik
    Serial.println("Gagal mendapatkan waktu dari NTP.");
    display.println("NTP Gagal!");
    display.display();
    delay(2000);
    return;
  }
  Serial.print("Waktu NTP berhasil didapat: ");
  Serial.println(&timeinfo, "%A, %d %B %Y %H:%M:%S");
  rtc.adjust(DateTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec));
  Serial.println("RTC telah disinkronkan dengan NTP.");
  display.println("RTC Sinkron!");
  display.display();
  delay(1500);
}

// -----------------------------------------------------------------
// FUNGSI UNTUK MENDAPATKAN WAKTU DARI RTC DS3231
// -----------------------------------------------------------------
void getRTCTime(struct tm *timeinfo) {
  DateTime now = rtc.now();
  timeinfo->tm_year = now.year() - 1900;
  timeinfo->tm_mon = now.month() - 1;
  timeinfo->tm_mday = now.day();
  timeinfo->tm_hour = now.hour();
  timeinfo->tm_min = now.minute();
  timeinfo->tm_sec = now.second();
  timeinfo->tm_wday = now.dayOfTheWeek();
}

// -----------------------------------------------------------------
// FUNGSI UNTUK UPDATE TAMPILAN OLED
// -----------------------------------------------------------------
void updateDisplay(struct tm *timeinfo) {
  char timeStr[9];
  char dateStr[20];
  strftime(timeStr, sizeof(timeStr), "%H:%M:%S", timeinfo);
  strftime(dateStr, sizeof(dateStr), "%d-%m", timeinfo);

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("--- RAIN GAUGE ---");
  display.setCursor(0, 10);
  display.print(timeStr);
  display.setCursor(70, 10);
  display.print(dateStr);
  display.drawLine(0, 20, 127, 20, SSD1306_WHITE);
  display.setCursor(0, 25);
  display.print("Jam Ini: ");
  display.print(rainfallPerHour[timeinfo->tm_hour], 2);
  display.print(" mm");
  display.setCursor(0, 35);
  display.print("Hari Ini: ");
  display.print(rainfallToday, 2);
  display.print(" mm");
  display.setCursor(0, 45);
  display.print("Kemarin: ");
  display.print(rainfallYesterday, 2);
  display.print(" mm");
  display.setCursor(0, 56);
  if (WiFi.status() == WL_CONNECTED) {
    display.print("WiFi: Terhubung");
  } else {
    display.print("WiFi: Terputus");
  }
  display.display();
}

// =================================================================
//
// FUNGSI-FUNGSI EEPROM (AT24C32) DENGAN PERBAIKAN 'nan'
//
// =================================================================

// --- Fungsi Helper untuk Menulis/Membaca EEPROM ---
void writeEEPROM(int memAddr, const byte* data, int len) {
  Wire.beginTransmission(EEPROM_I2C_ADDR);
  Wire.write((byte)(memAddr >> 8));   // High Byte
  Wire.write((byte)(memAddr & 0xFF)); // Low Byte
  Wire.write(data, len);
  Wire.endTransmission();
  delay(5); // Jeda 5ms agar EEPROM selesai menulis
}

void readEEPROM(int memAddr, byte* data, int len) {
  Wire.beginTransmission(EEPROM_I2C_ADDR);
  Wire.write((byte)(memAddr >> 8));   // High Byte
  Wire.write((byte)(memAddr & 0xFF)); // Low Byte
  Wire.endTransmission();
  Wire.requestFrom(EEPROM_I2C_ADDR, len);
  for (int i = 0; i < len; i++) {
    if (Wire.available()) {
      data[i] = Wire.read();
    }
  }
}

// --- Fungsi Spesifik untuk Tipe Data ---
void writeEEPROMFloat(int memAddr, float data) {
  writeEEPROM(memAddr, (byte*)&data, sizeof(float));
}
float readEEPROMFloat(int memAddr) {
  float data = 0.0;
  readEEPROM(memAddr, (byte*)&data, sizeof(float));
  return data;
}
void writeEEPROMInt(int memAddr, int data) {
  writeEEPROM(memAddr, (byte*)&data, sizeof(int));
}
int readEEPROMInt(int memAddr) {
  int data = 0;
  readEEPROM(memAddr, (byte*)&data, sizeof(int));
  return data;
}

// --- Fungsi Inisialisasi dan Simpan (LOGIKA UTAMA) ---

void loadRainfallData() {
  Serial.println("Membaca data dari EEPROM AT24C32...");
  
  // 1. Baca Magic Number (dari alamat 8)
  int magic = readEEPROMInt(ADDR_MAGIC);
  
  if (magic != MAGIC_NUMBER) {
    // --- EEPROM KOSONG / BARU (FIX UNTUK 'nan') ---
    Serial.println("EEPROM baru terdeteksi. Inisialisasi...");
    
    // Set nilai default di RAM
    rainfallToday = 0.0;
    rainfallYesterday = 0.0;
    lastCheckedDay = rtc.now().day();
    
    // Tulis nilai default ini ke EEPROM
    writeEEPROMFloat(ADDR_RAINFALL, 0.0);
    writeEEPROMInt(ADDR_LASTDAY, lastCheckedDay);
    
    // TULIS MAGIC NUMBER agar boot berikutnya normal
    writeEEPROMInt(ADDR_MAGIC, MAGIC_NUMBER);
    
  } else {
    // --- EEPROM NORMAL (Sudah pernah dipakai) ---
    Serial.println("Magic number OK. Memuat data...");
    DateTime now = rtc.now();
    int savedDay = readEEPROMInt(ADDR_LASTDAY);
    
    if (savedDay == now.day()) {
      // KASUS 1: Boot di hari yang sama (misal mati listrik)
      Serial.println("Data dari hari ini ditemukan, memuat...");
      rainfallToday = readEEPROMFloat(ADDR_RAINFALL);
      lastCheckedDay = savedDay;
      // Data 'yesterday' tidak relevan, biarkan 0
      
    } else {
      // KASUS 2: Boot di hari BARU (misal mati semalaman)
      Serial.println("Data tersimpan sudah basi (hari kemarin).");
      // Nilai yang tersimpan di EEPROM (ADDR_RAINFALL) adalah total KEMARIN
      rainfallYesterday = readEEPROMFloat(ADDR_RAINFALL);
      
      // Reset data hari ini
      rainfallToday = 0.0;
      lastCheckedDay = now.day();
      
      // Simpan status baru ini ke EEPROM
      saveRainfallData();
    }
  }
  
  Serial.print("Data dimuat: Hari Ini="); Serial.print(rainfallToday);
  Serial.print("mm, Kemarin="); Serial.print(rainfallYesterday);
  Serial.println("mm");
}

void saveRainfallData() {
  Serial.print("Menyimpan data ke EEPROM... ");
  
  // Simpan total harian saat ini
  writeEEPROMFloat(ADDR_RAINFALL, rainfallToday);
  
  // Simpan hari saat ini, untuk validasi saat boot
  writeEEPROMInt(ADDR_LASTDAY, lastCheckedDay); 

  Serial.println("Selesai.");
}