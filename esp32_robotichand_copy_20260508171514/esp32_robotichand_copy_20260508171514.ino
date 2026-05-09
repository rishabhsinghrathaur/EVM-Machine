#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ---------------- LCD ----------------
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ---------------- RFID ----------------
#define SS_PIN 5
#define RST_PIN 4
MFRC522 rfid(SS_PIN, RST_PIN);

// ---------------- Buzzer ----------------
#define BUZZER 15

// ---------------- Preferences ----------------
Preferences preferences;

// ---------------- WiFi ----------------
const char* ssid = "SchoolVoting";
const char* password = "vote1234";

WebServer server(80);

// ---------------- RFID UIDs ----------------
String MASTER_UID = "f5ecaa4";
String CANDIDATE_A_UID = "f2236c5";
String CANDIDATE_B_UID = "c7ef6c5";

// ---------------- Voting Variables ----------------
bool voteEnabled = false;

String currentSection = "Sports Captain";
String currentKey = "sports";

int votesA = 0;
int votesB = 0;

// ======================================================
// BUZZER SOUNDS
// ======================================================

void masterBeep() {

  tone(BUZZER, 1500, 120);
  delay(150);
  tone(BUZZER, 1800, 120);
}

void voteBeep() {

  tone(BUZZER, 2500, 200);
}

void errorBeep() {

  tone(BUZZER, 500, 500);
}

// ======================================================
// SAVE & LOAD
// ======================================================

void saveVotes() {

  preferences.putInt((currentKey + "A").c_str(), votesA);
  preferences.putInt((currentKey + "B").c_str(), votesB);
}

void loadVotes() {

  votesA = preferences.getInt((currentKey + "A").c_str(), 0);
  votesB = preferences.getInt((currentKey + "B").c_str(), 0);
}

// ======================================================
// RFID UID
// ======================================================

String getUID() {

  String uid = "";

  for (byte i = 0; i < rfid.uid.size; i++) {

    uid += String(rfid.uid.uidByte[i], HEX);
  }

  uid.toLowerCase();

  return uid;
}

// ======================================================
// LCD
// ======================================================

void showLockedScreen() {

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print(currentSection.substring(0,16));

  lcd.setCursor(0, 1);
  lcd.print("Tap Master");
}

// ======================================================
// CHANGE SECTION
// ======================================================

void changeSection(String sectionName, String key) {

  currentSection = sectionName;
  currentKey = key;

  loadVotes();

  lcd.clear();

  lcd.setCursor(0,0);
  lcd.print(sectionName.substring(0,16));

  lcd.setCursor(0,1);
  lcd.print("Section Loaded");

  delay(1500);

  showLockedScreen();

  server.sendHeader("Location", "/");
  server.send(303);
}

// ======================================================
// SETUP
// ======================================================

void setup() {

  Serial.begin(115200);

  pinMode(BUZZER, OUTPUT);

  // LCD
  lcd.init();
  lcd.backlight();

  lcd.setCursor(0,0);
  lcd.print("Voting Machine");

  lcd.setCursor(0,1);
  lcd.print("Starting...");

  // RFID
  SPI.begin();
  rfid.PCD_Init();

  // Preferences
  preferences.begin("voting", false);

  loadVotes();

  // WiFi Hotspot
  WiFi.softAP(ssid, password);

  Serial.println(WiFi.softAPIP());

  // ---------------- Web Routes ----------------

  server.on("/", handleRoot);

  server.on("/sports", []() {
    changeSection("Sports Captain", "sports");
  });

  server.on("/cultural", []() {
    changeSection("Cultural Cap", "cultural");
  });

  server.on("/red", []() {
    changeSection("Red House", "red");
  });

  server.on("/blue", []() {
    changeSection("Blue House", "blue");
  });

  server.on("/green", []() {
    changeSection("Green House", "green");
  });

  server.on("/yellow", []() {
    changeSection("Yellow House", "yellow");
  });

  server.on("/reset", handleReset);

  server.begin();

  delay(2000);

  showLockedScreen();
}

// ======================================================
// LOOP
// ======================================================

void loop() {

  server.handleClient();

  if (!rfid.PICC_IsNewCardPresent()) return;

  if (!rfid.PICC_ReadCardSerial()) return;

  String uid = getUID();

  Serial.println(uid);

  // ======================================================
  // MASTER CARD
  // ======================================================

  if (uid == MASTER_UID) {

    voteEnabled = true;

    lcd.clear();

    lcd.setCursor(0,0);
    lcd.print("Vote Enabled");

    lcd.setCursor(0,1);
    lcd.print("Tap Candidate");

    masterBeep();

    delay(1000);

    return;
  }

  // ======================================================
  // VOTING
  // ======================================================

  if (voteEnabled) {

    // ---------------- Candidate A ----------------

    if (uid == CANDIDATE_A_UID) {

      votesA++;

      saveVotes();

      lcd.clear();

      lcd.setCursor(0,0);
      lcd.print("Vote Saved");

      lcd.setCursor(0,1);
      lcd.print("Candidate A");

      voteBeep();

      voteEnabled = false;

      delay(2000);

      showLockedScreen();
    }

    // ---------------- Candidate B ----------------

    else if (uid == CANDIDATE_B_UID) {

      votesB++;

      saveVotes();

      lcd.clear();

      lcd.setCursor(0,0);
      lcd.print("Vote Saved");

      lcd.setCursor(0,1);
      lcd.print("Candidate B");

      voteBeep();

      voteEnabled = false;

      delay(2000);

      showLockedScreen();
    }

    // ---------------- Invalid Card ----------------

    else {

      lcd.clear();

      lcd.setCursor(0,0);
      lcd.print("Invalid Card");

      lcd.setCursor(0,1);
      lcd.print("Try Again");

      errorBeep();

      delay(2000);

      showLockedScreen();
    }
  }

  // ======================================================
  // MACHINE LOCKED
  // ======================================================

  else {

    lcd.clear();

    lcd.setCursor(0,0);
    lcd.print("Machine Locked");

    lcd.setCursor(0,1);
    lcd.print("Use Master");

    errorBeep();

    delay(2000);

    showLockedScreen();
  }

  rfid.PICC_HaltA();
}

// ======================================================
// RESET CURRENT SECTION
// ======================================================

void handleReset() {

  votesA = 0;
  votesB = 0;

  saveVotes();

  lcd.clear();

  lcd.setCursor(0,0);
  lcd.print("Votes Reset");

  lcd.setCursor(0,1);
  lcd.print(currentSection.substring(0,16));

  delay(2000);

  showLockedScreen();

  server.sendHeader("Location", "/");
  server.send(303);
}

// ======================================================
// MOBILE FRIENDLY WEB UI
// ======================================================

void handleRoot() {

  String html = R"rawliteral(

<!DOCTYPE html>
<html>

<head>

<meta name='viewport' content='width=device-width, initial-scale=1'>

<title>Voting Machine</title>

<style>

body{
font-family:Arial;
background:#f2f2f2;
padding:15px;
text-align:center;
}

.card{
background:white;
padding:20px;
border-radius:15px;
box-shadow:0 2px 10px rgba(0,0,0,0.1);
max-width:400px;
margin:auto;
}

button{
width:100%;
padding:15px;
margin-top:10px;
border:none;
border-radius:12px;
font-size:18px;
background:#111;
color:white;
}

.voteBox{
background:#fafafa;
padding:10px;
border-radius:10px;
margin-top:10px;
font-size:18px;
}

h2{
margin-top:0;
}

</style>

</head>

<body>

<div class='card'>

<h2>School Voting</h2>

)rawliteral";

  html += "<div class='voteBox'><b>Current Section:</b><br>" + currentSection + "</div>";

  html += "<div class='voteBox'>Candidate A Votes: <b>" + String(votesA) + "</b><br><br>Candidate B Votes: <b>" + String(votesB) + "</b></div>";

  html += "<button onclick=\"location.href='/cultural'\">Cultural Captain</button>";

  html += "<button onclick=\"location.href='/sports'\">Sports Captain</button>";

  html += "<button onclick=\"location.href='/red'\">Red House</button>";

  html += "<button onclick=\"location.href='/blue'\">Blue House</button>";

  html += "<button onclick=\"location.href='/green'\">Green House</button>";

  html += "<button onclick=\"location.href='/yellow'\">Yellow House</button>";

  html += "<button style='background:red' onclick=\"location.href='/reset'\">Reset Current Section</button>";

  html += "</div></body></html>";

  server.send(200, "text/html", html);
}
