#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>

void IRAM_ATTR countFlowPulse();
String elapsedLabel(uint32_t durationMs);
void drawDashboardFrame();
void updateDisplay();
void updateAlarmOutputs(uint32_t now);
void recordCompletedSession(uint32_t endedAtMs);
void handleDashboard();
void handleApi();

constexpr int FLOW_PIN = 27;
constexpr int BUZZER_PIN = 25;
constexpr int LEAK_SENSOR_PIN = 34;
constexpr int LEAK_LED_PIN = 26;
constexpr int TFT_CS = 4;
constexpr int TFT_DC = 16;
constexpr int TFT_RST = 17;
constexpr int TFT_SCK = 18;
constexpr int TFT_MOSI = 23;

// The sensor is dry around 4095. Treat a touch that lowers it below 4000 as wet.
constexpr int WET_THRESHOLD = 4000;
constexpr uint32_t SAMPLE_INTERVAL_MS = 1000;
constexpr uint32_t BUZZER_ON_MS = 2000;
constexpr uint32_t BUZZER_OFF_MS = 1000;
constexpr uint32_t LED_BLINK_MS = 400;
constexpr uint32_t FLOW_STOP_TIMEOUT_MS = 3000;
constexpr float PULSES_PER_LITRE = 450.0f;
constexpr uint8_t SESSION_COUNT = 5;

const char *AP_SSID = "SmartFlow-ESP32";
const char *AP_PASSWORD = "SmartFlow123";

constexpr uint16_t COLOUR_BG = ST77XX_BLACK;
constexpr uint16_t COLOUR_TEXT = ST77XX_WHITE;
constexpr uint16_t COLOUR_FLOW = ST77XX_CYAN;
constexpr uint16_t COLOUR_TOTAL = ST77XX_YELLOW;
constexpr uint16_t COLOUR_OK = ST77XX_GREEN;
constexpr uint16_t COLOUR_LEAK = ST77XX_RED;

Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_RST);
WebServer server(80);

volatile uint32_t flowPulses = 0;
uint32_t lastSampleMs = 0;
float currentFlowLitresPerMinute = 0.0f;
float totalLitres = 0.0f;
int leakReading = 4095;
bool leakDetected = false;
bool previousLeakDetected = false;
bool buzzerIsOn = false;
uint32_t buzzerStateChangedMs = 0;
bool ledIsOn = false;
uint32_t ledStateChangedMs = 0;

bool flowActive = false;
uint32_t activeFlowStartedMs = 0;
uint32_t lastFlowPulseMs = 0;
uint32_t lastFlowStartMs = 0;
uint32_t lastFlowEndMs = 0;
float activeFlowStartLitres = 0.0f;
uint32_t sessionStartMs[SESSION_COUNT] = {};
uint32_t sessionEndMs[SESSION_COUNT] = {};
float sessionLitres[SESSION_COUNT] = {};
uint8_t sessionWriteIndex = 0;
uint8_t sessionStored = 0;

float shownFlow = -1.0f;
float shownTotal = -1.0f;
bool shownLeak = true;

void IRAM_ATTR countFlowPulse() { flowPulses++; }

String elapsedLabel(uint32_t durationMs) {
  const uint32_t seconds = durationMs / 1000UL;
  char text[16];
  snprintf(text, sizeof(text), "%02lu:%02lu:%02lu", (unsigned long)(seconds / 3600UL),
           (unsigned long)((seconds % 3600UL) / 60UL), (unsigned long)(seconds % 60UL));
  return String(text);
}

void drawDashboardFrame() {
  tft.fillScreen(COLOUR_BG);
  tft.setTextWrap(false);
  tft.setTextSize(1);
  tft.setTextColor(COLOUR_TEXT);
  tft.setCursor(37, 4); tft.print("SMARTFLOW");
  tft.drawFastHLine(0, 16, tft.width(), ST77XX_BLUE);
  tft.setTextColor(COLOUR_FLOW); tft.setCursor(6, 24); tft.print("FLOW RATE");
  tft.setTextColor(COLOUR_TOTAL); tft.setCursor(6, 65); tft.print("TOTAL USED");
  tft.setTextColor(COLOUR_TEXT); tft.setCursor(6, 106); tft.print("LEAK STATUS");
}

void updateDisplay() {
  if (currentFlowLitresPerMinute != shownFlow) {
    tft.fillRect(0, 35, tft.width(), 25, COLOUR_BG);
    tft.setTextColor(COLOUR_FLOW); tft.setTextSize(2); tft.setCursor(6, 38);
    tft.print(currentFlowLitresPerMinute, 2);
    tft.setTextSize(1); tft.setCursor(6, 56); tft.print("litres / minute");
    shownFlow = currentFlowLitresPerMinute;
  }
  if (totalLitres != shownTotal) {
    tft.fillRect(0, 76, tft.width(), 25, COLOUR_BG);
    tft.setTextColor(COLOUR_TOTAL); tft.setTextSize(2); tft.setCursor(6, 79);
    tft.print(totalLitres, 2);
    tft.setTextSize(1); tft.setCursor(6, 97); tft.print("litres used");
    shownTotal = totalLitres;
  }
  if (leakDetected != shownLeak) {
    tft.fillRect(0, 117, tft.width(), 37, COLOUR_BG);
    tft.setTextSize(2); tft.setTextColor(leakDetected ? COLOUR_LEAK : COLOUR_OK);
    tft.setCursor(6, 121); tft.print(leakDetected ? "WET!" : "DRY");
    tft.setTextSize(1); tft.setCursor(6, 143);
    tft.print(leakDetected ? "LEAK ALERT" : "SYSTEM SAFE");
    shownLeak = leakDetected;
  }
}

void updateAlarmOutputs(uint32_t now) {
  if (!leakDetected) {
    if (buzzerIsOn) digitalWrite(BUZZER_PIN, LOW);
    if (ledIsOn) digitalWrite(LEAK_LED_PIN, LOW);
    buzzerIsOn = false;
    ledIsOn = false;
    return;
  }

  if (leakDetected != previousLeakDetected) {
    // A new leak starts with a visible light and an immediate two-second warning sound.
    buzzerIsOn = true;
    ledIsOn = true;
    buzzerStateChangedMs = now;
    ledStateChangedMs = now;
    digitalWrite(BUZZER_PIN, HIGH);
    digitalWrite(LEAK_LED_PIN, HIGH);
  } else {
    if (buzzerIsOn && now - buzzerStateChangedMs >= BUZZER_ON_MS) {
      buzzerIsOn = false;
      buzzerStateChangedMs = now;
      digitalWrite(BUZZER_PIN, LOW);
    } else if (!buzzerIsOn && now - buzzerStateChangedMs >= BUZZER_OFF_MS) {
      buzzerIsOn = true;
      buzzerStateChangedMs = now;
      digitalWrite(BUZZER_PIN, HIGH);
    }
    if (now - ledStateChangedMs >= LED_BLINK_MS) {
      ledIsOn = !ledIsOn;
      ledStateChangedMs = now;
      digitalWrite(LEAK_LED_PIN, ledIsOn ? HIGH : LOW);
    }
  }
}

void recordCompletedSession(uint32_t endedAtMs) {
  sessionStartMs[sessionWriteIndex] = activeFlowStartedMs;
  sessionEndMs[sessionWriteIndex] = endedAtMs;
  sessionLitres[sessionWriteIndex] = totalLitres - activeFlowStartLitres;
  sessionWriteIndex = (sessionWriteIndex + 1) % SESSION_COUNT;
  if (sessionStored < SESSION_COUNT) sessionStored++;
}

const char DASHBOARD_HTML[] PROGMEM = R"HTML(
<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="theme-color" content="#07121f"><title>SmartFlow | Water Control</title><style>
:root{--navy:#071a2d;--blue:#1687dc;--aqua:#13c4c0;--paper:#eef3f8;--card:#fff;--ink:#14263b;--muted:#6e8194;--line:#dce5ed;--green:#11875d;--greenbg:#e3f6ed;--red:#d33545;--redbg:#fff0f2;--gold:#c68611;--shadow:0 9px 26px #16304d14}*{box-sizing:border-box}body{margin:0;background:var(--paper);color:var(--ink);font:15px Inter,Segoe UI,Arial,sans-serif}main{max-width:1280px;margin:auto;padding:30px 34px 42px}.top{display:flex;align-items:center;justify-content:space-between;gap:20px;background:var(--navy);border-radius:16px;padding:24px 27px;color:#fff;box-shadow:var(--shadow)}.brand{font-size:30px;font-weight:800;letter-spacing:.2px}.brand b{color:var(--aqua)}.sub{opacity:.72;margin-top:5px}.live{font-size:12px;font-weight:750;letter-spacing:.8px;padding:10px 13px;border:1px solid #3aab8b;border-radius:20px;color:#bcf8df}.dot{display:inline-block;width:8px;height:8px;border-radius:50%;background:#4ae29d;margin-right:7px}.grid{display:grid;grid-template-columns:repeat(4,1fr);gap:18px;margin:22px 0}.card,.panel{background:var(--card);border:1px solid var(--line);box-shadow:var(--shadow);border-radius:14px}.card{padding:20px;border-top:4px solid var(--blue)}.card:nth-child(2){border-color:var(--line);border-top-color:var(--gold)}.card:nth-child(3){border-color:var(--line);border-top-color:var(--green)}.card:nth-child(4){border-color:var(--line);border-top-color:var(--aqua)}.label{font-size:11px;color:var(--muted);font-weight:800;letter-spacing:1.15px}.value{font-size:29px;line-height:1.15;font-weight:780;margin-top:14px;letter-spacing:-.4px}.blue{color:var(--blue)}.gold{color:var(--gold)}.green{color:var(--green)}.red{color:var(--red)}.lower{display:grid;grid-template-columns:1fr 1.65fr;gap:18px}.panel{padding:23px}.activity{display:flex;min-height:134px;flex-direction:column;justify-content:space-between}.activity .value{font-size:22px}.status{display:inline-block;width:max-content;margin-top:17px;border-radius:20px;padding:9px 12px;font-size:12px;font-weight:800}.safe{color:var(--green);background:var(--greenbg)}.alert{color:var(--red);background:var(--redbg)}table{width:100%;border-collapse:collapse;margin-top:12px}th,td{text-align:left;padding:13px 9px;border-bottom:1px solid var(--line)}th{font-size:11px;letter-spacing:.9px;color:var(--muted)}tr:last-child td{border-bottom:0}.empty{text-align:center;color:var(--muted);padding:24px}.modal{display:none;position:fixed;inset:0;background:#061424a6;z-index:5;align-items:center;justify-content:center;padding:20px}.modal.show{display:flex}.alertbox{width:min(460px,100%);text-align:center;background:#fff;border-radius:18px;overflow:hidden;box-shadow:0 18px 50px #0006}.alerthead{padding:27px;background:var(--red);color:#fff;font-size:24px;font-weight:800}.alertbody{padding:24px;color:var(--ink)}.alertbody p{color:var(--muted);line-height:1.5}.alertbox button{border:0;border-radius:8px;padding:11px 18px;background:var(--navy);color:#fff;font-weight:700;cursor:pointer}.analysis{margin-top:18px}.analysisTop{display:flex;align-items:end;justify-content:space-between;gap:18px;flex-wrap:wrap}.tabs{display:flex;gap:8px;flex-wrap:wrap}.tab{border:1px solid var(--line);background:#fff;color:var(--muted);border-radius:8px;padding:9px 13px;font-weight:800;cursor:pointer}.tab.on{background:var(--navy);border-color:var(--navy);color:#fff}.rate{display:flex;align-items:center;gap:8px;color:var(--muted);font-size:13px;font-weight:700}.rate input{width:86px;padding:9px;border:1px solid var(--line);border-radius:7px;font:inherit;color:var(--ink)}.analysisGrid{display:grid;grid-template-columns:repeat(3,1fr);gap:14px;margin-top:20px}.mini{border:1px solid var(--line);border-radius:11px;padding:16px;background:#f9fbfd}.mini .value{font-size:24px}.hint{color:var(--muted);font-size:12px;line-height:1.45;margin:18px 0 0}@media(max-width:860px){.analysisGrid{grid-template-columns:1fr 1fr}}@media(max-width:860px){main{padding:18px}.grid{grid-template-columns:repeat(2,1fr)}.lower{grid-template-columns:1fr}.top{padding:20px}}@media(max-width:480px){main{padding:12px}.top{border-radius:12px;align-items:flex-start;flex-direction:column}.grid{gap:10px;margin:12px 0}.card{padding:15px}.value{font-size:22px}.brand{font-size:26px}.panel{padding:16px}th,td{padding:10px 4px;font-size:12px}}
</style></head><body><main><header class="top"><div><div class="brand">Smart<b>Flow</b></div><div class="sub">Desktop water monitoring and leak protection</div></div><div class="live" id="live"><span class="dot"></span>LIVE CONNECTION</div></header><section class="grid"><div class="card"><div class="label">LIVE FLOW</div><div class="value blue" id="flow">--</div></div><div class="card"><div class="label">WATER USED</div><div class="value gold" id="total">--</div></div><div class="card"><div class="label">LEAK SENSOR</div><div class="value" id="leak">--</div></div><div class="card"><div class="label">CURRENT FLOW TIME</div><div class="value green" id="active">--</div></div></section><section class="lower"><section class="panel activity"><div><div class="label">SYSTEM ACTIVITY</div><div class="value blue" id="window">Waiting for water flow</div></div><div class="status safe" id="badge">SYSTEM SAFE</div></section><section class="panel"><div class="label">RECENT WATER USE</div><table><thead><tr><th>START</th><th>STOP</th><th>DURATION</th><th>WATER USED</th></tr></thead><tbody id="sessions"><tr><td colspan="4" class="empty">No completed water-use session yet.</td></tr></tbody></table></section></section><section class="panel analysis"><div class="analysisTop"><div><div class="label">WATER ANALYSIS</div><div class="tabs"><button class="tab on" data-period="daily" onclick="setPeriod('daily')">DAILY</button><button class="tab" data-period="weekly" onclick="setPeriod('weekly')">WEEKLY</button><button class="tab" data-period="monthly" onclick="setPeriod('monthly')">MONTHLY</button></div></div><label class="rate">YOUR WATER PRICE: Rs <input id="rate" type="number" min="0" step="0.01" value="0" inputmode="decimal" onchange="saveRate()"> / litre</label></div><div class="analysisGrid"><div class="mini"><div class="label" id="periodName">TODAY'S USE</div><div class="value blue" id="periodLitres">0.00 L</div></div><div class="mini"><div class="label">WATER COST</div><div class="value gold" id="periodCost">Rs 0.00</div></div><div class="mini"><div class="label">TOTAL ON DEVICE</div><div class="value green" id="deviceTotal">0.00 L</div></div></div><p class="hint">Daily, weekly, and monthly figures are saved in this browser while this dashboard is open. They use your computer or phone's local date and the price you enter.</p></section></main><div class="modal" id="modal"><div class="alertbox"><div class="alerthead">LEAK DETECTED</div><div class="alertbody"><strong>Water has touched the leak sensor.</strong><p>The warning LED is blinking and the buzzer is sounding. Dry the sensor and check the nearby pipe, joint, or container.</p><button onclick="closeAlert()">I AM CHECKING IT</button></div></div></div><script>const e=id=>document.getElementById(id);let wasWet=false,closedForThisLeak=false,lastSessionKey='';const empty='<tr><td colspan="4" class="empty">No completed water-use session yet.</td></tr>';function clock(ms,u){if(!ms)return '';return new Date(Date.now()-u+ms).toLocaleTimeString([], {hour:'2-digit',minute:'2-digit',second:'2-digit'})}function row(s,u){return `<tr><td>${clock(s.fromMs,u)}</td><td>${clock(s.toMs,u)}</td><td>${s.duration}</td><td>${s.litres.toFixed(2)} L</td></tr>`}let period='daily',lastTotal=null;const storeKey='smartflow-water-history-v1';function dayKey(d){return d.getFullYear()+'-'+String(d.getMonth()+1).padStart(2,'0')+'-'+String(d.getDate()).padStart(2,'0')}function loadHistory(){try{return JSON.parse(localStorage.getItem(storeKey)||'{}')}catch(x){return {}}}function saveHistory(h){localStorage.setItem(storeKey,JSON.stringify(h))}function saveRate(){localStorage.setItem('smartflow-water-rate',e('rate').value);showAnalysis()}function setPeriod(p){period=p;document.querySelectorAll('.tab').forEach(b=>b.classList.toggle('on',b.dataset.period===p));showAnalysis()}function periodUse(h,now){if(period==='daily')return h[dayKey(now)]||0;if(period==='weekly'){let n=0;for(let i=0;i<7;i++){let d=new Date(now);d.setDate(now.getDate()-i);n+=h[dayKey(d)]||0}return n}let n=0;Object.keys(h).forEach(k=>{let d=new Date(k+'T00:00:00');if(d.getFullYear()===now.getFullYear()&&d.getMonth()===now.getMonth())n+=h[k]||0});return n}function showAnalysis(){const now=new Date(),h=loadHistory(),litres=periodUse(h,now),rate=Math.max(0,parseFloat(e('rate').value)||0),names={daily:"TODAY'S USE",weekly:'LAST 7 DAYS',monthly:'THIS MONTH'};e('periodName').textContent=names[period];e('periodLitres').textContent=litres.toFixed(2)+' L';e('periodCost').textContent='Rs '+(litres*rate).toFixed(2)}function trackWater(total){e('deviceTotal').textContent=total.toFixed(2)+' L';if(lastTotal!==null&&total>=lastTotal){let added=total-lastTotal;if(added>0&&added<20){let h=loadHistory(),k=dayKey(new Date());h[k]=(h[k]||0)+added;saveHistory(h)}}lastTotal=total;showAnalysis()}function closeAlert(){e('modal').classList.remove('show');closedForThisLeak=true}async function refresh(){try{const r=await fetch('/api?x='+Date.now(),{cache:'no-store'});if(!r.ok)throw 0;const d=await r.json(),wet=d.leak;l=e('leak');l.textContent=wet?'WET - LEAK':'DRY - SAFE';l.className='value '+(wet?'red':'green');e('badge').textContent=wet?'LEAK ALERT - CHECK NOW':'SYSTEM SAFE';e('badge').className='status '+(wet?'alert':'safe');e('flow').textContent=d.flow.toFixed(2)+' L/min';e('total').textContent=d.total.toFixed(2)+' L';trackWater(d.total);e('active').textContent=d.active?d.activeTime:'Not flowing';e('window').textContent=d.active?'Water started at '+clock(d.fromMs,d.uptime):d.lastEndMs?'Last flow stopped at '+clock(d.lastEndMs,d.uptime):'Waiting for water flow';const key=JSON.stringify(d.sessions);if(key!==lastSessionKey){e('sessions').innerHTML=d.sessions.length?d.sessions.map(s=>row(s,d.uptime)).join(''):empty;lastSessionKey=key}e('live').innerHTML='<span class="dot"></span>LIVE CONNECTION';if(wet&&!wasWet&&!closedForThisLeak)e('modal').classList.add('show');if(!wet)closedForThisLeak=false;wasWet=wet}catch(x){e('live').textContent='RECONNECTING TO DEVICE'}}e('rate').value=localStorage.getItem('smartflow-water-rate')||'0';showAnalysis();refresh();setInterval(refresh,500);</script></body></html>
)HTML";

void handleDashboard() { server.send_P(200, "text/html", DASHBOARD_HTML); }

void handleApi() {
  const uint32_t now = millis();
  String json = "{\"flow\":" + String(currentFlowLitresPerMinute, 2);
  json += ",\"total\":" + String(totalLitres, 2);
  json += ",\"leak\":" + String(leakDetected ? "true" : "false");
  json += ",\"active\":" + String(flowActive ? "true" : "false");
  json += ",\"uptime\":" + String(now);
  json += ",\"activeTime\":\"" + elapsedLabel(flowActive ? now - activeFlowStartedMs : 0) + "\"";
  json += ",\"fromMs\":" + String(flowActive ? activeFlowStartedMs : lastFlowStartMs);
  json += ",\"lastEndMs\":" + String(lastFlowEndMs);
  json += ",\"sessions\":[";
  for (uint8_t count = 0; count < sessionStored; count++) {
    int index = (sessionWriteIndex + SESSION_COUNT - 1 - count) % SESSION_COUNT;
    if (count) json += ',';
    json += "{\"fromMs\":" + String(sessionStartMs[index]) + ",\"toMs\":" + String(sessionEndMs[index]) + ",\"duration\":\"" + elapsedLabel(sessionEndMs[index] - sessionStartMs[index]) + "\",\"litres\":" + String(sessionLitres[index], 2) + "}";
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);
  pinMode(FLOW_PIN, INPUT); pinMode(BUZZER_PIN, OUTPUT); pinMode(LEAK_SENSOR_PIN, INPUT); pinMode(LEAK_LED_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW); digitalWrite(LEAK_LED_PIN, LOW);
  SPI.begin(TFT_SCK, -1, TFT_MOSI, TFT_CS);
  tft.initR(INITR_BLACKTAB); tft.setRotation(0); drawDashboardFrame(); shownLeak = !leakDetected; updateDisplay();
  attachInterrupt(digitalPinToInterrupt(FLOW_PIN), countFlowPulse, RISING);
  WiFi.mode(WIFI_AP); WiFi.softAP(AP_SSID, AP_PASSWORD);
  server.on("/", handleDashboard); server.on("/api", handleApi); server.begin();
  Serial.println("SmartFlow monitor ready.");
}

void loop() {
  const uint32_t now = millis();
  server.handleClient();
  updateAlarmOutputs(now);
  previousLeakDetected = leakDetected;
  if (now - lastSampleMs < SAMPLE_INTERVAL_MS) return;

  noInterrupts(); uint32_t pulses = flowPulses; flowPulses = 0; interrupts();
  currentFlowLitresPerMinute = pulses / 7.5f;
  totalLitres += pulses / PULSES_PER_LITRE;
  leakReading = analogRead(LEAK_SENSOR_PIN);
  leakDetected = leakReading < WET_THRESHOLD;
  if (pulses > 0) {
    lastFlowPulseMs = now;
    if (!flowActive) { flowActive = true; activeFlowStartedMs = now; activeFlowStartLitres = totalLitres - (pulses / PULSES_PER_LITRE); lastFlowStartMs = activeFlowStartedMs; }
  } else if (flowActive && now - lastFlowPulseMs >= FLOW_STOP_TIMEOUT_MS) {
    flowActive = false; lastFlowEndMs = lastFlowPulseMs; recordCompletedSession(lastFlowEndMs);
  }
  updateDisplay();
  Serial.printf("Pulses/s: %lu | Flow: %.2f L/min | Total: %.2f L | Leak sensor: %d | %s\n", (unsigned long)pulses, currentFlowLitresPerMinute, totalLitres, leakReading, leakDetected ? "WET / LEAK" : "DRY");
  lastSampleMs = now;
}
