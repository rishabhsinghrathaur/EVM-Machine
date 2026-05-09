#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ============================================================
//  HARDWARE PINS
// ============================================================
#define SS_PIN  5
#define RST_PIN 4
#define BUZZER  15

LiquidCrystal_I2C lcd(0x27, 16, 2);
MFRC522 rfid(SS_PIN, RST_PIN);
WebServer server(80);

// Separate Preferences objects so both namespaces stay open
Preferences prefsVote;    // namespace "voting"
Preferences prefsUID;     // namespace "uids"

// ============================================================
//  WIFI ACCESS POINT
// ============================================================
const char* WIFI_SSID = "SchoolVoting";
const char* WIFI_PASS = "vote1234";

// ============================================================
//  DEFAULT RFID UIDS  (used when NVS is uninitialised)
// ============================================================
const String DEFAULT_MASTER      = "f5ecaa4";
const String DEFAULT_CANDIDATE_A = "f2236c5";
const String DEFAULT_CANDIDATE_B = "c7ef6c5";

// ============================================================
//  VOTING SECTIONS
// ============================================================
struct Section {
  const char* name;
  const char* key;
};

static const Section SECTIONS[] = {
  {"Sports Captain",  "sports"},
  {"Cultural Cap",    "cultural"},
  {"Red House",       "red"},
  {"Blue House",      "blue"},
  {"Green House",     "green"},
  {"Yellow House",    "yellow"},
};
static const int NUM_SECTIONS = sizeof(SECTIONS) / sizeof(SECTIONS[0]);

int currentSectionIdx = 0;
int votesA = 0;
int votesB = 0;

String currentSection() { return SECTIONS[currentSectionIdx].name; }
String currentKey()     { return SECTIONS[currentSectionIdx].key; }

// ============================================================
//  STATE MACHINE
// ============================================================
enum MachineState {
  ST_LOCKED,
  ST_VOTING_READY,
  ST_VOTE_CONFIRMED,
  ST_INVALID_CARD,
  ST_LOCKED_ALERT,
  ST_SECTION_LOADED,
  ST_RESET_PENDING,
  ST_RESET_CONFIRMED,
};

MachineState state = ST_LOCKED;
unsigned long stateSince = 0;
char lastCandidate = 0;     // 'A' or 'B' — used by the LCD after a vote

// Timing constants (milliseconds)
static const unsigned long T_DISPLAY   = 2000;   // general feedback duration
static const unsigned long T_SECTION   = 1500;   // "Section Loaded" splash
static const unsigned long T_RESET_WIN = 10000;  // how long reset-pending stays open

// -------------------------------------------------------
//  STATE HELPERS
// -------------------------------------------------------
void transitionTo(MachineState s) {
  state = s;
  stateSince = millis();
}

unsigned long msInState() { return millis() - stateSince; }

// ============================================================
//  RFID UID HELPERS  (read/write NVS "uids" namespace)
// ============================================================

String readUID(const char* key, const String& fallback) {
  String val = prefsUID.getString(key, "");
  return val.length() > 0 ? val : fallback;
}

String getMasterUID()      { return readUID("master", DEFAULT_MASTER); }
String getCandidateAUID()  { return readUID("candA",  DEFAULT_CANDIDATE_A); }
String getCandidateBUID()  { return readUID("candB",  DEFAULT_CANDIDATE_B); }

void setMasterUID(const String& v)     { prefsUID.putString("master", v); }
void setCandidateAUID(const String& v) { prefsUID.putString("candA", v); }
void setCandidateBUID(const String& v) { prefsUID.putString("candB", v); }

/** Write built-in defaults to NVS on very first boot. */
void initDefaultUIDs() {
  if (prefsUID.getString("master", "").length() == 0) {
    prefsUID.putString("master", DEFAULT_MASTER);
    prefsUID.putString("candA",  DEFAULT_CANDIDATE_A);
    prefsUID.putString("candB",  DEFAULT_CANDIDATE_B);
  }
}

// -------------------------------------------------------
//  READ RAW RFID UID
// -------------------------------------------------------
String getUID() {
  String uid;
  for (byte i = 0; i < rfid.uid.size; i++) uid += String(rfid.uid.uidByte[i], HEX);
  uid.toLowerCase();
  return uid;
}

// ============================================================
//  LCD DISPLAY
// ============================================================

void showLockedScreen() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(currentSection().substring(0, 16));
  lcd.setCursor(0, 1); lcd.print("Tap Master");
}

void showVotingReady() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Vote Enabled");
  lcd.setCursor(0, 1); lcd.print("Tap Candidate");
}

void showVoteConfirmed() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Vote Saved");
  lcd.setCursor(0, 1); lcd.print(lastCandidate == 'A' ? "Candidate A" : "Candidate B");
}

void showInvalidCard() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Invalid Card");
  lcd.setCursor(0, 1); lcd.print("Try Again");
}

void showMachineLocked() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Machine Locked");
  lcd.setCursor(0, 1); lcd.print("Use Master");
}

void showSectionLoaded() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(currentSection().substring(0, 16));
  lcd.setCursor(0, 1); lcd.print("Section Loaded");
}

void showResetPending() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Tap Master");
  lcd.setCursor(0, 1); lcd.print("to Reset Votes");
}

void showResetConfirmed() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Votes Reset");
  lcd.setCursor(0, 1); lcd.print(currentSection().substring(0, 16));
}

// ============================================================
//  BUZZER
// ============================================================
void masterBeep()  { tone(BUZZER, 1800, 200); }
void voteBeep()    { tone(BUZZER, 2500, 200); }
void errorBeep()   { tone(BUZZER,  500, 500); }
void confirmBeep() { tone(BUZZER, 2000, 150); }

// ============================================================
//  NVS VOTE PERSISTENCE  (namespace "voting")
// ============================================================
void saveVotes() {
  prefsVote.putInt((currentKey() + "A").c_str(), votesA);
  prefsVote.putInt((currentKey() + "B").c_str(), votesB);
}

void loadVotes() {
  votesA = prefsVote.getInt((currentKey() + "A").c_str(), 0);
  votesB = prefsVote.getInt((currentKey() + "B").c_str(), 0);
}

void resetVotes() { votesA = 0; votesB = 0; saveVotes(); }

// ============================================================
//  SERIAL AUDIT TRAIL
// ============================================================
void audit(const char* action) {
  Serial.print('[');
  Serial.print(millis());
  Serial.print(" ms] ");
  Serial.print(action);
  Serial.print(" | ");
  Serial.print(currentSection());
  Serial.print("  A=");
  Serial.print(votesA);
  Serial.print(" B=");
  Serial.println(votesB);
}

// ============================================================
//  RFID CARD HANDLER  (called once per loop when a card is detected)
// ============================================================
void handleRFID() {
  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial())   return;

  String uid = getUID();
  Serial.print("[RFID] ");
  Serial.println(uid);

  String master = getMasterUID();
  String candA  = getCandidateAUID();
  String candB  = getCandidateBUID();

  switch (state) {

    /* ---------- locked / reset-pending: only master card is honoured ---------- */
    case ST_LOCKED:
    case ST_RESET_PENDING:
      if (uid == master) {
        if (state == ST_RESET_PENDING) {
          resetVotes();
          showResetConfirmed();
          transitionTo(ST_RESET_CONFIRMED);
          confirmBeep();
          audit("Reset confirmed via master card");
        } else {
          showVotingReady();
          transitionTo(ST_VOTING_READY);
          masterBeep();
          audit("Master authenticated — voting enabled");
        }
      } else {
        showMachineLocked();              // always true for ST_LOCKED
        transitionTo(ST_LOCKED_ALERT);
        errorBeep();
        audit("Rejected non-master card");
      }
      break;

    /* ---------- voting mode: accept candidate cards ---------- */
    case ST_VOTING_READY:
      if (uid == master) {
        // Master re-tap refreshes the voting window
        showVotingReady();
        transitionTo(ST_VOTING_READY);
        masterBeep();
        audit("Master re-authenticated");
      } else if (uid == candA) {
        votesA++;
        saveVotes();
        lastCandidate = 'A';
        showVoteConfirmed();
        transitionTo(ST_VOTE_CONFIRMED);
        voteBeep();
        audit("Vote cast for Candidate A");
      } else if (uid == candB) {
        votesB++;
        saveVotes();
        lastCandidate = 'B';
        showVoteConfirmed();
        transitionTo(ST_VOTE_CONFIRMED);
        voteBeep();
        audit("Vote cast for Candidate B");
      } else {
        showInvalidCard();
        transitionTo(ST_INVALID_CARD);
        errorBeep();
        audit("Invalid card during voting");
      }
      break;

    /* ---------- transitional states: ignore further cards ---------- */
    default:
      break;
  }

  rfid.PICC_HaltA();
}

// ============================================================
//  STATE MACHINE TICK  (non-blocking timed transitions)
// ============================================================
void tickStateMachine() {
  switch (state) {

    case ST_VOTE_CONFIRMED:
    case ST_INVALID_CARD:
    case ST_LOCKED_ALERT:
      if (msInState() >= T_DISPLAY) {
        transitionTo(ST_LOCKED);
        showLockedScreen();
      }
      break;

    case ST_SECTION_LOADED:
      if (msInState() >= T_SECTION) {
        transitionTo(ST_LOCKED);
        showLockedScreen();
      }
      break;

    case ST_RESET_PENDING:
      if (msInState() >= T_RESET_WIN) {
        transitionTo(ST_LOCKED);
        showLockedScreen();
        audit("Reset cancelled (timeout)");
      }
      break;

    case ST_RESET_CONFIRMED:
      if (msInState() >= T_DISPLAY) {
        transitionTo(ST_LOCKED);
        showLockedScreen();
      }
      break;

    default:
      // ST_LOCKED / ST_VOTING_READY — wait indefinitely for RFID
      break;
  }
}

// ============================================================
//  WEB UI
// ============================================================

void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>EVM Machine</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,sans-serif;background:#f0f2f5;padding:16px;min-height:100vh;display:flex;align-items:center;justify-content:center}
.card{background:#fff;padding:24px;border-radius:16px;box-shadow:0 2px 12px rgba(0,0,0,.08);max-width:420px;width:100%}
h1{font-size:22px;text-align:center;margin-bottom:16px;color:#1a1a1a}
#state-indicator{text-align:center;padding:10px;font-size:15px;font-weight:600;border-radius:10px;margin-bottom:16px;transition:background .2s}
.s-green{background:#dcfce7;color:#166534}
.s-red{background:#fee2e2;color:#991b1b}
.s-blue{background:#dbeafe;color:#1e40af}
.s-yellow{background:#fef3c7;color:#92400e}
.vote-grid{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-bottom:16px}
.vote-box{background:#f8fafc;border:1px solid #e2e8f0;border-radius:12px;padding:16px;text-align:center}
.vote-box .lbl{font-size:13px;color:#64748b;margin-bottom:4px}
.vote-box .ct{font-size:32px;font-weight:700;color:#0f172a}
.sec-label{font-size:13px;color:#64748b;margin-bottom:8px;text-align:center}
.sec-grid{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-bottom:16px}
.sec-grid button{width:100%;padding:12px;border:none;border-radius:10px;font-size:14px;cursor:pointer;color:#fff;background:#334155;transition:opacity .15s}
.sec-grid button.act{background:#2563eb;font-weight:600;box-shadow:0 0 0 2px #93c5fd}
.sec-grid button:hover{opacity:.85}
#reset-btn{width:100%;padding:12px;border:none;border-radius:10px;font-size:14px;cursor:pointer;color:#fff;background:#dc2626;margin-bottom:8px;transition:opacity .15s}
#reset-btn:disabled{background:#fca5a5;cursor:not-allowed}
#reset-btn:not(:disabled):hover{opacity:.85}
.stog{background:transparent;border:1px solid #e2e8f0;border-radius:10px;padding:12px;font-size:14px;cursor:pointer;width:100%;color:#475569;text-align:center;margin-top:4px}
.spane{display:none;margin-top:12px;padding:16px;background:#f8fafc;border-radius:10px;border:1px solid #e2e8f0}
.spane.op{display:block}
.spane label{font-size:12px;color:#64748b;display:block;margin-bottom:4px;margin-top:12px}
.spane label:first-child{margin-top:0}
.spane input{width:100%;padding:8px 10px;border:1px solid #cbd5e1;border-radius:8px;font-size:14px;font-family:monospace;background:#fff;color:#0f172a}
.spane .sv{background:#16a34a;color:#fff;border:none;border-radius:8px;padding:10px;width:100%;font-size:14px;cursor:pointer;margin-top:16px}
.spane .sv:hover{opacity:.9}
#uid-msg{font-size:12px;margin-top:8px;text-align:center;color:#64748b}
</style>
</head>
<body>
<div class="card">
<h1>EVM Machine</h1>
<div id="state-indicator" class="s-yellow">Connecting…</div>
<div class="vote-grid">
<div class="vote-box"><div class="lbl">Candidate A</div><div class="ct" id="votes-a">0</div></div>
<div class="vote-box"><div class="lbl">Candidate B</div><div class="ct" id="votes-b">0</div></div>
</div>
<div class="sec-label">Section: <strong id="section-name">—</strong></div>
<div class="sec-grid">
)rawliteral";

  for (int i = 0; i < NUM_SECTIONS; i++) {
    html += "<button onclick=\"location.href='/" + String(SECTIONS[i].key) + "'\">"
            + String(SECTIONS[i].name) + "</button>";
  }

  html += R"rawliteral(
</div>
<button id="reset-btn" onclick="requestReset()">Reset This Section</button>
<button class="stog" onclick="toggleSettings()">⚙ RFID Card Settings</button>
<div class="spane" id="settings-panel">
<label>Master Card UID</label>
<input id="uid-master" type="text" placeholder="f5ecaa4">
<label>Candidate A UID</label>
<input id="uid-candA" type="text" placeholder="f2236c5">
<label>Candidate B UID</label>
<input id="uid-candB" type="text" placeholder="c7ef6c5">
<button class="sv" onclick="saveUIDs()">Save UIDs</button>
<div id="uid-msg"></div>
</div>
</div>

<script>
const SI=document.getElementById('state-indicator');
const VA=document.getElementById('votes-a');
const VB=document.getElementById('votes-b');
const SN=document.getElementById('section-name');
const RB=document.getElementById('reset-btn');

function setState(cls,label){SI.className=cls;SI.textContent=label;}

async function poll(){
  try{
    const r=await fetch('/api/status');
    const d=await r.json();
    VA.textContent=d.votesA; VB.textContent=d.votesB; SN.textContent=d.section;
    const s=d.state;
    if(s==='locked')        setState('s-yellow','Locked — Tap Master Card');
    else if(s==='voting_ready') setState('s-green','Voting Enabled — Tap Candidate');
    else if(s==='vote_recorded') setState('s-blue','Vote Recorded');
    else if(s==='reset_pending') setState('s-yellow','Reset Pending — Tap Master to confirm');
    else if(s==='reset_confirmed') setState('s-green','Votes Reset');
    else setState('s-red',d.stateLabel||'—');
    RB.disabled=(s==='voting_ready'||s==='reset_pending');
  }catch(e){}
}

async function requestReset(){
  if(!confirm('Reset all votes for '+SN.textContent+'?')) return;
  await fetch('/api/reset',{method:'POST'});
}

function toggleSettings(){
  const p=document.getElementById('settings-panel');
  p.classList.toggle('op');
  if(p.classList.contains('op')) loadUIDs();
}

async function loadUIDs(){
  try{
    const r=await fetch('/api/uids'); const d=await r.json();
    document.getElementById('uid-master').value=d.master||'';
    document.getElementById('uid-candA').value=d.candidateA||'';
    document.getElementById('uid-candB').value=d.candidateB||'';
  }catch(e){}
}

async function saveUIDs(){
  const b=new URLSearchParams();
  b.set('master',document.getElementById('uid-master').value.trim());
  b.set('candA',document.getElementById('uid-candA').value.trim());
  b.set('candB',document.getElementById('uid-candB').value.trim());
  const r=await fetch('/api/uids',{method:'POST',body:b});
  const d=await r.json();
  document.getElementById('uid-msg').textContent=d.ok?'Saved ✓':'Error: '+d.error;
}

setInterval(poll,2000); poll();
</script>
</body>
</html>
)rawliteral";

  server.send(200, "text/html", html);
}

// -------------------------------------------------------
//  JSON API  —  /api/status
// -------------------------------------------------------
void handleAPIStatus() {
  const char* label;
  switch (state) {
    case ST_LOCKED:          label = "locked";         break;
    case ST_VOTING_READY:    label = "voting_ready";   break;
    case ST_VOTE_CONFIRMED:  label = "vote_recorded";  break;
    case ST_RESET_PENDING:   label = "reset_pending";  break;
    case ST_RESET_CONFIRMED: label = "reset_confirmed";break;
    default:                 label = "locked";
  }

  String json = "{";
  json += "\"section\":\""    + currentSection() + "\",";
  json += "\"sectionKey\":\"" + currentKey()     + "\",";
  json += "\"votesA\":"       + String(votesA)   + ",";
  json += "\"votesB\":"       + String(votesB)   + ",";
  json += "\"state\":\""      + String(label)    + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

// -------------------------------------------------------
//  JSON API  —  POST /api/reset   (triggers master-card confirmation)
// -------------------------------------------------------
void handleAPIReset() {
  transitionTo(ST_RESET_PENDING);
  showResetPending();
  audit("Reset requested via web UI");
  server.send(200, "application/json", "{\"ok\":true}");
}

// -------------------------------------------------------
//  JSON API  —  GET|POST /api/uids
// -------------------------------------------------------
void handleAPIUIDs() {
  if (server.method() == HTTP_POST) {
    String master = server.arg("master");
    String candA  = server.arg("candA");
    String candB  = server.arg("candB");

    if (master.length() && master.length() < 20) setMasterUID(master);
    if (candA.length()  && candA.length()  < 20) setCandidateAUID(candA);
    if (candB.length()  && candB.length()  < 20) setCandidateBUID(candB);

    audit("UIDs updated via web UI");
    server.send(200, "application/json", "{\"ok\":true}");
  } else {
    String json = "{";
    json += "\"master\":\""     + getMasterUID()     + "\",";
    json += "\"candidateA\":\"" + getCandidateAUID() + "\",";
    json += "\"candidateB\":\"" + getCandidateBUID() + "\"";
    json += "}";
    server.send(200, "application/json", json);
  }
}

// ============================================================
//  CHANGE VOTING SECTION
// ============================================================
void changeSection(int idx) {
  currentSectionIdx = idx;
  loadVotes();
  showSectionLoaded();
  transitionTo(ST_SECTION_LOADED);
  audit("Section changed");
  server.sendHeader("Location", "/");
  server.send(303);
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  pinMode(BUZZER, OUTPUT);

  // LCD splash
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0); lcd.print("Voting Machine");
  lcd.setCursor(0, 1); lcd.print("Starting...");

  SPI.begin();
  rfid.PCD_Init();

  prefsVote.begin("voting", false);
  prefsUID.begin("uids", false);
  initDefaultUIDs();

  loadVotes();

  // WiFi soft-AP
  WiFi.softAP(WIFI_SSID, WIFI_PASS);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  // ---- Web routes ----
  server.on("/",              handleRoot);
  server.on("/api/status",    handleAPIStatus);
  server.on("/api/reset",     handleAPIReset);
  server.on("/api/uids",      handleAPIUIDs);

  for (int i = 0; i < NUM_SECTIONS; i++) {
    int idx = i;  // explicit copy so the lambda captures correctly
    String path = String("/") + SECTIONS[i].key;
    server.on(path.c_str(), [idx]() { changeSection(idx); });
  }

  server.begin();

  delay(2000);   // one blocking delay is acceptable during boot
  showLockedScreen();

  audit("System booted");
}

// ============================================================
//  MAIN LOOP
// ============================================================
void loop() {
  server.handleClient();
  tickStateMachine();
  handleRFID();
}
