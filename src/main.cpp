/*
 * KODE WEATHER STATION - CALIBRATION MODE (RAW DATA)
 * Fitur: 
 * - Fokus mencatat Tip Count & Interval
 * - Tabel Web lebih simpel (Tip ke-X, Jam, Interval)
 * - Export Excel (.csv) otomatis menyesuaikan
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WebServer.h>
#include <RTClib.h> 

// --- KONFIGURASI WIFI ---
const char* ssid = "server";     
const char* password = "jeris6467";  

const int PIN_HUJAN = 5;
const long DEBOUNCE_MS = 200;
const int MAX_LOGS = 150; // Saya naikkan dikit biar muat lebih banyak data

// --- OBJEK HARDWARE ---
Adafruit_SSD1306 display(128, 64, &Wire, -1);
RTC_DS3231 rtc; 
WebServer server(80);

void updateDisplay();

// --- STRUKTUR DATA LOG (YANG BARU) ---
struct LogData {
  int tip_ke;       // Tip ke berapa
  String jam;       // Waktu kejadian
  String interval;  // Selisih waktu
};

LogData history[MAX_LOGS]; 
int logCount = 0;          

// --- VARIABEL GLOBAL ---
volatile unsigned int isrTipCount = 0;
volatile unsigned long lastTipTimeMs = 0;

unsigned long totalTips = 0;
DateTime lastTipTimestamp;

// --- ISR ---
void IRAM_ATTR handleRainTip() {
  unsigned long now = millis();
  if (now - lastTipTimeMs > DEBOUNCE_MS) {
    isrTipCount++;
    lastTipTimeMs = now;
  }
}

// --- FUNGSI RESET DATA ---
void handleReset() {
  totalTips = 0;
  logCount = 0; 
  server.send(200, "text/html", "<h1>Data Reset Berhasil!</h1><a href='/'>Kembali</a>");
  updateDisplay();
}

// --- FUNGSI WEB SERVER UTAMA ---
void handleRoot() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Rain Gauge Raw Data</title>";
  html += "<style>";
  html += "body{font-family:Arial; text-align:center; margin:20px;}";
  html += "table{width:100%; border-collapse:collapse; margin-top:20px;}";
  html += "th, td{border:1px solid #ddd; padding:8px; text-align:center;}";
  html += "th{background-color:#28a745; color:white;}"; // Warna Hijau biar fresh
  html += "tr:nth-child(even){background-color:#f2f2f2;}";
  html += ".btn{background:#007bff; color:white; padding:10px 20px; text-decoration:none; border-radius:5px; margin:5px; display:inline-block; cursor:pointer; border:none;}";
  html += ".btn-reset{background:#dc3545;}";
  html += "</style>";
  
  // JS Download CSV
  html += "<script>";
  html += "function downloadCSV() {";
  html += "  var csv = [];";
  html += "  var rows = document.querySelectorAll('table tr');";
  html += "  for (var i = 0; i < rows.length; i++) {";
  html += "    var row = [], cols = rows[i].querySelectorAll('td, th');";
  html += "    for (var j = 0; j < cols.length; j++) row.push(cols[j].innerText);";
  html += "    csv.push(row.join(','));";
  html += "  }";
  html += "  var csvFile = new Blob([csv.join('\\n')], {type: 'text/csv'});";
  html += "  var downloadLink = document.createElement('a');";
  html += "  downloadLink.download = 'raw_data_kalibrasi.csv';";
  html += "  downloadLink.href = window.URL.createObjectURL(csvFile);";
  html += "  downloadLink.style.display = 'none';";
  html += "  document.body.appendChild(downloadLink);";
  html += "  downloadLink.click();";
  html += "}";
  html += "</script>";
  
  html += "</head><body>";
  
  html += "<h1>Data Mentah Kalibrasi</h1>";
  html += "<h3>Total Tips: " + String(totalTips) + "</h3>";
  
  html += "<button class='btn' onclick='downloadCSV()'>Download Excel (.csv)</button>";
  html += "<a href='/' class='btn' style='background:#6c757d;'>Refresh</a>";
  html += "<a href='/reset' class='btn btn-reset' onclick=\"return confirm('Yakin hapus data?')\">Reset Data</a>";

  // --- TABEL SIMPEL (SESUAI REQUEST) ---
  html += "<table>";
  html += "<tr><th>Tip Ke-</th><th>Waktu (Jam)</th><th>Interval (Detik)</th></tr>";
  
  for(int i=0; i<logCount; i++) {
    html += "<tr>";
    html += "<td>" + String(history[i].tip_ke) + "</td>";
    html += "<td>" + history[i].jam + "</td>";
    html += "<td>" + history[i].interval + "</td>";
    html += "</tr>";
  }
  
  html += "</table>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

// -----------------------------------------------------------------
// SETUP
// -----------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { for(;;); }
  display.clearDisplay(); display.setTextColor(SSD1306_WHITE);
  
  if (!rtc.begin()) { while (1); }
  lastTipTimestamp = rtc.now(); 

  WiFi.begin(ssid, password);
  display.setCursor(0,0); display.println("Connect WiFi..."); display.display();
  while (WiFi.status() != WL_CONNECTED) { delay(500); }
  
  server.on("/", handleRoot);
  server.on("/reset", handleReset);
  server.begin();

  pinMode(PIN_HUJAN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_HUJAN), handleRainTip, FALLING);

  updateDisplay();
}

// -----------------------------------------------------------------
// LOOP
// -----------------------------------------------------------------
void loop() {
  server.handleClient();

  unsigned int tipsToProcess = 0;
  noInterrupts();
  if (isrTipCount > 0) {
    tipsToProcess = isrTipCount;
    isrTipCount = 0;
  }
  interrupts();

  if (tipsToProcess > 0) {
    DateTime now = rtc.now();
    TimeSpan diff = now - lastTipTimestamp;
    
    // Format Interval (HH:MM:SS)
    char bufInt[20];
    sprintf(bufInt, "%02d:%02d:%02d", diff.hours(), diff.minutes(), diff.seconds());
    
    // Format Waktu Kejadian
    char bufJam[20];
    sprintf(bufJam, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());

    lastTipTimestamp = now;
    totalTips += tipsToProcess;

    // --- SIMPAN KE LOG (VERSI SIMPEL) ---
    if (logCount < MAX_LOGS) {
      history[logCount].tip_ke = totalTips;
      history[logCount].jam = String(bufJam);
      history[logCount].interval = String(bufInt);
      logCount++;
    }

    updateDisplay();
  }
}

// -----------------------------------------------------------------
// UPDATE OLED
// -----------------------------------------------------------------
void updateDisplay() {
  display.clearDisplay();
  
  // Header IP
  display.setTextSize(1);
  display.setCursor(0, 0); display.print("IP: "); display.println(WiFi.localIP());
  display.drawLine(0, 9, 128, 9, SSD1306_WHITE);

  // Info Utama: Total Tips
  display.setCursor(0, 20); display.print("TOTAL TIPS:");
  display.setTextSize(3); // Angka Gede banget biar kebaca pas kalibrasi
  display.setCursor(0, 35); display.print(totalTips);
  
  // Info Memory Log
  display.setTextSize(1);
  display.setCursor(80, 55); display.print("Log:"); display.print(logCount);
  
  display.display();
}