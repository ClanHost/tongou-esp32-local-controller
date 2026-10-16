/*
  Tongou TO-Q-SY1-WT-DC - ESP32-S3 Local Controller
  Arduino-ESP32 core 3.3.12

  Features:
    - Open SWITCHAP at 192.168.4.1
    - Connects to 2.4 wifi networks only
    - NAPT internet sharing
    - Tongou discovery by MAC using DHCP/ARP
    - Tuya protocol 3.5
    - ON/OFF with relay-state verification
    - Query relay state.command(
    - Minimal Serial Monitor output

  Serial Monitor: 115200 baud
*/

#include <WiFi.h>
#include <WebServer.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include <time.h>
#include <lwip/inet.h>

// ============================================================
// CONFIGURATION
// ============================================================

// UPSTREAM WI-FI CREDENTIALS
// The Wi-Fi network that the ESP32 connects to for internet access.
// NAPT allows devices connected to SWITCHAP to share this connection.
// Internet access is optional for local switch control.
static const char *UPLINK_SSID = "WIFI_UPLINK_SSID";
static const char *UPLINK_PASSWORD = "WIFI_UPLINK_PASSWORD";

// ESP32 ACCESS POINT NAME
// The Wi-Fi network created by the ESP32.
// Connect a phone, tablet, or computer to this network to access
// the controller web interface at http://192.168.4.1/.
// Choose any suitable network name.
static const char *AP_SSID = "ENTER_ESP_AP_SSID";

// TUYA DEVICE ID
// The unique identifier of the Tongou switch registered with Tuya.
// Used in protocol messages to identify the target device.
// Obtain this value from your Tuya device information.
static const char *TUYA_DEVICE_ID = "ENTER_TUYA_DEVICE_ID";

// TUYA LOCAL KEY
// A 16-character key used to authenticate and encrypt local
// communication with the switch using the Tuya 3.5 protocol.
// Obtain the local key for your specific device from Tuya device
// information or another supported local-key retrieval method.
// Keep this value private; never publish your actual key.
static const char *TUYA_LOCAL_KEY = "ENTER_TUYA_LOCAL_KEY";

// SWITCH MAC ADDRESS
// The six-byte hardware MAC address of the Tongou switch.
// Used to identify the correct device connected to the ESP32
// access point and discover its current IP address.
// Replace these bytes with your switch's actual MAC address.
static const uint8_t SWITCH_MAC[6] = {
  0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF
};

// TUYA LOCAL COMMUNICATION PORT
// TCP port used by the switch for local Tuya protocol communication.
// Port 6668 is used by this controller for Tuya 3.5 communication.
// Change this only if your device uses a different port.
static const uint16_t TUYA_PORT = 6668;

// ============================================================
// GLOBAL STATE
// ============================================================

static WebServer server(80);

static String switchIp = "0.0.0.0";
static String lastMessage = "Starting";
static String lastCommand = "None";

static uint32_t tuyaSeq = 1;
static uint32_t lastReconnectAttempt = 0;
static uint32_t lastSwitchScan = 0;

static bool switchFound = false;
static bool relayStateKnown = false;
static bool relayState = false;

static bool waitForSwitchIP(uint32_t timeoutMs = 10000);
static void discoverSwitchByMac();
static bool refreshRelayState(String &details);
static void handleStatus();
static void handleSwitch();
static void handleQuery();
static void connectUpstream();

// ============================================================
// MINIMAL WEBPAGE
// ============================================================

static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="theme-color" content="#101820">
<title>Tongou</title>
<style>
*{box-sizing:border-box}
body{
  font-family:system-ui,sans-serif;
  max-width:360px;
  margin:35px auto;
  padding:0 16px;
  background:#101820;
  color:#eef2f7;
  text-align:center
}
h1{font-size:1.5rem;margin-bottom:6px}
#state{font-size:2rem;font-weight:700;margin:22px 0 8px}
#ip{font-size:.85rem;color:#aebdca;margin-bottom:22px}
.buttons{display:grid;grid-template-columns:1fr 1fr;gap:10px}
button{
  border:0;border-radius:9px;padding:14px 10px;
  color:white;font-size:1rem;font-weight:600;
  cursor:pointer
}
button:disabled{opacity:.5}
.on{background:#218c55}
.off{background:#b53a3a}
.query{background:#455a70;grid-column:1/3}
#result{min-height:24px;margin-top:18px;font-size:.9rem}
</style>
</head>
<body>
<h1>Tongou Control</h1>
<div id="state">Loading...</div>
<div id="ip">Searching for switch...</div>

<div class="buttons">
  <button class="on" id="on" onclick="sendSwitchCommand('on')">ON</button>
  <button class="off" id="off" onclick="sendSwitchCommand('off')">OFF</button>
  <button class="query" id="query" onclick="querySwitch()">Query</button>
</div>

<div id="result">Ready</div>

<script>
const $ = id => document.getElementById(id);

async function refresh() {
  try {
    const r = await fetch('/api/status', {cache:'no-store'});
    const j = await r.json();

    $('state').textContent =
      j.relayKnown ? (j.relayOn ? 'ON' : 'OFF') : 'Unknown';

    $('state').style.color = j.relayKnown
      ? (j.relayOn ? '#58df91' : '#ff7777') : '#eef2f7';

    $('ip').textContent =
      j.switchFound ? j.switchIP : 'Switch not found';
  } catch(e) {
    $('result').textContent = 'Connection lost';
  }
}

async function request(path) {
  ['on','off','query'].forEach(id => $(id).disabled = true);

  try {
    const r = await fetch(path, {
      method:'POST',
      cache:'no-store'
    });
    const body = await r.text();
    $('result').textContent = body;
  } catch(e) {
    $('result').textContent = 'Request failed';
  } finally {
    ['on','off','query'].forEach(id => $(id).disabled = false);
    await refresh();
  }
}

function sendSwitchCommand(state) {
  request('/api/switch?state=' + state);
}

function querySwitch() {
  request('/api/query');
}

refresh();
setInterval(refresh, 5000);
</script>
</body>
</html>
)HTML";

// ============================================================
// BYTE HELPERS
// ============================================================

static void put32(uint8_t *p, uint32_t v) {
  p[0] = v >> 24;
  p[1] = v >> 16;
  p[2] = v >> 8;
  p[3] = v;
}

static uint32_t get32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) |
         ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) |
         (uint32_t)p[3];
}

// ============================================================
// CRYPTO HELPERS
// ============================================================

static bool hmac256(
    const uint8_t *key, size_t keyLen,
    const uint8_t *data, size_t len,
    uint8_t out[32]) {

  const mbedtls_md_info_t *md =
      mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

  return md &&
      mbedtls_md_hmac(md, key, keyLen, data, len, out) == 0;
}

static bool gcmEncrypt(
    const uint8_t *key, const uint8_t iv[12],
    const uint8_t *aad, size_t aadLen,
    const uint8_t *plain, size_t len,
    uint8_t *cipher, uint8_t tag[16]) {

  mbedtls_gcm_context g;
  mbedtls_gcm_init(&g);

  int rc = mbedtls_gcm_setkey(
      &g, MBEDTLS_CIPHER_ID_AES, key, 128);

  if (rc == 0) {
    rc = mbedtls_gcm_crypt_and_tag(
        &g, MBEDTLS_GCM_ENCRYPT, len,
        iv, 12, aad, aadLen,
        plain, cipher, 16, tag);
  }

  mbedtls_gcm_free(&g);
  return rc == 0;
}

static bool gcmDecrypt(
    const uint8_t *key, const uint8_t iv[12],
    const uint8_t *aad, size_t aadLen,
    const uint8_t *cipher, size_t len,
    const uint8_t tag[16], uint8_t *plain) {

  mbedtls_gcm_context g;
  mbedtls_gcm_init(&g);

  int rc = mbedtls_gcm_setkey(
      &g, MBEDTLS_CIPHER_ID_AES, key, 128);

  if (rc == 0) {
    rc = mbedtls_gcm_auth_decrypt(
        &g, len, iv, 12,
        aad, aadLen, tag, 16, cipher, plain);
  }

  mbedtls_gcm_free(&g);
  return rc == 0;
}

static bool readExact(
    WiFiClient &c, uint8_t *b, size_t len,
    uint32_t timeoutMs = 5000) {

  size_t got = 0;
  uint32_t start = millis();

  while (got < len && millis() - start < timeoutMs) {
    int available = c.available();

    if (available > 0) {
      int r = c.read(
          b + got,
          min((size_t)available, len - got));

      if (r > 0) got += r;
    } else if (!c.connected()) {
      break;
    } else {
      delay(2);
    }
  }

  return got == len;
}

// ============================================================
// TUYA 3.5 FRAME EXCHANGE
// ============================================================

static bool exchange35(
    WiFiClient &c, uint32_t cmd,
    const uint8_t *plain, size_t plainLen,
    const uint8_t *key,
    uint8_t *reply, size_t replyCap,
    size_t &replyLen) {

  replyLen = 0;

  if (plainLen > 1200) return false;

  const size_t bodyLen = 12 + plainLen + 16;
  const size_t frameLen = 18 + bodyLen + 4;

  uint8_t *f = (uint8_t *)malloc(frameLen);
  if (!f) return false;

  f[0] = 0x00;
  f[1] = 0x00;
  f[2] = 0x66;
  f[3] = 0x99;
  f[4] = 0x00;
  f[5] = 0x00;

  put32(f + 6, tuyaSeq++);
  put32(f + 10, cmd);
  put32(f + 14, bodyLen);

  uint8_t iv[12];
  esp_fill_random(iv, sizeof(iv));
  memcpy(f + 18, iv, sizeof(iv));

  uint8_t *cipher = f + 30;
  uint8_t *tag = cipher + plainLen;

  bool ok = gcmEncrypt(
      key, iv, f + 4, 14,
      plain, plainLen, cipher, tag);

  if (!ok) {
    free(f);
    return false;
  }

  f[frameLen - 4] = 0x00;
  f[frameLen - 3] = 0x00;
  f[frameLen - 2] = 0x99;
  f[frameLen - 1] = 0x66;

  size_t sent = c.write(f, frameLen);
  free(f);

  if (sent != frameLen) return false;

  if (cmd == 0x05) return true;

  uint8_t h[18];

  if (!readExact(c, h, sizeof(h))) return false;

  if (h[0] != 0x00 || h[1] != 0x00 ||
      h[2] != 0x66 || h[3] != 0x99) {
    return false;
  }

  uint32_t n = get32(h + 14);
  if (n < 28 || n > 2048) return false;

  uint8_t *b = (uint8_t *)malloc(n);
  if (!b) return false;

  uint8_t foot[4];

  bool got = readExact(c, b, n) &&
             readExact(c, foot, sizeof(foot));

  if (!got || foot[0] != 0x00 || foot[1] != 0x00 ||
      foot[2] != 0x99 || foot[3] != 0x66) {
    free(b);
    return false;
  }

  size_t ctLen = n - 28;

  if (ctLen > replyCap) {
    free(b);
    return false;
  }

  ok = gcmDecrypt(
      key, b, h + 4, 14,
      b + 12, ctLen, b + 12 + ctLen, reply);

  if (ok) replyLen = ctLen;

  free(b);
  return ok;
}

// ============================================================
// TUYA 3.5 HANDSHAKE
// ============================================================

static bool negotiate35(
    WiFiClient &c, uint8_t sessionKey[16]) {

  uint8_t clientNonce[16];
  uint8_t resp[128];
  uint8_t want[32];
  uint8_t got[32];
  uint8_t finish[32];
  size_t n = 0;

  esp_fill_random(clientNonce, sizeof(clientNonce));

  if (!exchange35(
          c, 0x03, clientNonce, sizeof(clientNonce),
          (const uint8_t *)TUYA_LOCAL_KEY,
          resp, sizeof(resp), n)) {
    lastMessage = "Tuya START handshake failed";
    return false;
  }

  if (n < 52) {
    lastMessage = "Tuya handshake response too short";
    return false;
  }

  if (get32(resp) != 0) {
    lastMessage = "Tuya handshake returned an error";
    return false;
  }

  uint8_t deviceNonce[16];
  memcpy(deviceNonce, resp + 4, 16);
  memcpy(got, resp + 20, 32);

  if (!hmac256(
          (const uint8_t *)TUYA_LOCAL_KEY, 16,
          clientNonce, sizeof(clientNonce), want) ||
      memcmp(want, got, sizeof(want)) != 0) {
    lastMessage = "Tuya handshake HMAC verification failed";
    return false;
  }

  if (!hmac256(
          (const uint8_t *)TUYA_LOCAL_KEY, 16,
          deviceNonce, sizeof(deviceNonce), finish)) {
    lastMessage = "Tuya FINISH HMAC calculation failed";
    return false;
  }

  if (!exchange35(
          c, 0x05, finish, sizeof(finish),
          (const uint8_t *)TUYA_LOCAL_KEY,
          resp, sizeof(resp), n)) {
    lastMessage = "Tuya FINISH failed";
    return false;
  }

  uint8_t mixed[16];
  uint8_t encrypted[16];
  uint8_t tag[16];

  for (size_t i = 0; i < 16; i++) {
    mixed[i] = clientNonce[i] ^ deviceNonce[i];
  }

  if (!gcmEncrypt(
          (const uint8_t *)TUYA_LOCAL_KEY,
          clientNonce, nullptr, 0,
          mixed, sizeof(mixed), encrypted, tag)) {
    lastMessage = "Tuya session key derivation failed";
    return false;
  }

  memcpy(sessionKey, encrypted, sizeof(encrypted));
  return true;
}

// ============================================================
// TUYA JSON PAYLOADS
// ============================================================

static String queryJson() {
  time_t now = time(nullptr);
  if (now < 1700000000) now = 1700000000;

  return String("{\"gwId\":\"") + TUYA_DEVICE_ID +
      "\",\"devId\":\"" + TUYA_DEVICE_ID +
      "\",\"uid\":\"" + TUYA_DEVICE_ID +
      "\",\"t\":\"" + String((long)now) + "\"}";
}

static String controlJson(bool on) {
  time_t now = time(nullptr);
  if (now < 1700000000) now = 1700000000;

  return String("{\"protocol\":5,\"t\":") +
      String((long)now) +
      ",\"data\":{\"dps\":{\"1\":" +
      (on ? "true" : "false") + "}}}";
}

// ============================================================
// RELAY RESPONSE PARSING
// ============================================================

static bool parseRelayState(
    const String &json, bool &state) {

  int dpsPos = json.indexOf("\"dps\"");
  if (dpsPos < 0) return false;

  int dpPos = json.indexOf("\"1\"", dpsPos);
  if (dpPos < 0) return false;

  int colon = json.indexOf(':', dpPos);
  if (colon < 0) return false;

  int p = colon + 1;

  while (p < (int)json.length() &&
         (json[p] == ' ' || json[p] == '\t' ||
          json[p] == '\r' || json[p] == '\n')) {
    p++;
  }

  if (json.startsWith("true", p)) {
    state = true;
    return true;
  }

  if (json.startsWith("false", p)) {
    state = false;
    return true;
  }

  return false;
}

// ============================================================
// TUYA REQUEST
// ============================================================

static bool tuyaRequest(
    uint32_t cmd, const String &json,
    String &replySummary) {

  if (strlen(TUYA_LOCAL_KEY) != 16 ||
      strcmp(TUYA_LOCAL_KEY,
             "YOUR_16_CHARACTER_LOCAL_KEY") == 0) {
    lastMessage = "Configure the 16-character Tuya local key";
    return false;
  }

  if (!switchFound) {
    lastMessage = "Switch not found";
    return false;
  }

  IPAddress dest;
  if (!dest.fromString(switchIp)) {
    lastMessage = "Invalid switch IP";
    return false;
  }

  WiFiClient c;
  c.setTimeout(5000);

  if (!c.connect(dest, TUYA_PORT, 5000)) {
    lastMessage = "Cannot connect to switch";
    c.stop();
    return false;
  }

  uint8_t sessionKey[16];
  uint8_t response[1200];
  size_t responseLen = 0;

  if (!negotiate35(c, sessionKey)) {
    c.stop();
    return false;
  }

  uint8_t payload[1400];
  size_t payloadLen = 0;

  if (cmd != 0x10) {
    memcpy(payload, "3.5", 3);
    memset(payload + 3, 0, 12);
    payloadLen = 15;
  }

  if (json.length() > sizeof(payload) - payloadLen) {
    c.stop();
    lastMessage = "JSON payload too long";
    return false;
  }

  memcpy(payload + payloadLen, json.c_str(), json.length());
  payloadLen += json.length();

  bool ok = exchange35(
      c, cmd, payload, payloadLen,
      sessionKey, response, sizeof(response), responseLen);

  c.stop();

  if (!ok) {
    lastMessage = "Tuya response failed authentication or timed out";
    return false;
  }

  if (responseLen < 4) {
    lastMessage = "Tuya response too short";
    return false;
  }

  uint32_t retcode = get32(response);

  if (retcode != 0) {
    lastMessage = "Tuya device error " + String(retcode);
    return false;
  }

  size_t offset = 4;

  if (responseLen >= offset + 15 &&
      response[offset] == '3' &&
      response[offset + 1] == '.' &&
      response[offset + 2] == '5') {
    offset += 15;
  }

  String body;
  body.reserve(responseLen - offset);

  for (size_t i = offset; i < responseLen; i++) {
    body += (char)response[i];
  }

  replySummary = body.length()
      ? body
      : "Acknowledgement received";

  lastMessage = "Tuya request successful";
  return true;
}

// ============================================================
// QUERY AND VERIFY RELAY STATE
// ============================================================

static bool refreshRelayState(String &details) {
  String reply;

  if (!tuyaRequest(0x10, queryJson(), reply)) {
    relayStateKnown = false;
    details = lastMessage;
    return false;
  }

  bool reportedState = false;

  if (!parseRelayState(reply, reportedState)) {
    relayStateKnown = false;
    details = "Relay state not found in response";
    lastMessage = details;
    return false;
  }

  relayState = reportedState;
  relayStateKnown = true;

  details = String("Relay state confirmed: ") +
      (relayState ? "ON" : "OFF");

  lastMessage = details;
  return true;
}

// ============================================================
// HTTP HANDLERS
// ============================================================

static void sendText(int code, const String &body) {
  server.send(code, "text/plain; charset=utf-8", body);
}

static void handleStatus() {
  String j = "{";
  j += "\"switchIP\":\"" + switchIp + "\",";
  j += "\"switchFound\":" +
       String(switchFound ? "true" : "false") + ",";
  j += "\"relayKnown\":" +
       String(relayStateKnown ? "true" : "false") + ",";
  j += "\"relayOn\":" +
       String(relayState ? "true" : "false") + "}";

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", j);
}

static void handleQuery() {
  if (!waitForSwitchIP(10000)) {
    relayStateKnown = false;
    lastMessage = "Switch has no discoverable IP address";
    sendText(503, lastMessage);
    return;
  }

  String details;

  if (refreshRelayState(details)) {
    lastCommand = "Query";
    sendText(200, details);
  } else {
    sendText(502, details);
  }
}

static void handleSwitch() {
  if (!server.hasArg("state") ||
      (server.arg("state") != "on" &&
       server.arg("state") != "off")) {
    sendText(400, "Expected state=on or state=off");
    return;
  }

  if (!waitForSwitchIP(10000)) {
    lastCommand = "Failed";
    lastMessage = "Switch has no discoverable IP address";
    sendText(503, lastMessage);
    return;
  }

  bool requestedOn = server.arg("state") == "on";
  String commandReply;

  if (!tuyaRequest(
          0x0D, controlJson(requestedOn), commandReply)) {
    relayStateKnown = false;
    lastCommand = requestedOn ? "ON failed" : "OFF failed";
    sendText(502, lastMessage);
    return;
  }

  String details;

  if (!refreshRelayState(details)) {
    lastCommand = "State unconfirmed";
    sendText(502,
        "Command sent, but state could not be verified: " +
        details);
    return;
  }

  if (relayState != requestedOn) {
    lastCommand = "State mismatch";
    sendText(502, "State mismatch. " + details);
    return;
  }

  lastCommand = requestedOn ? "ON confirmed" : "OFF confirmed";
  sendText(200, String("Success. ") + details);
}

// ============================================================
// SWITCH DISCOVERY: DHCP, THEN ARP
// ============================================================

static void discoverSwitchByMac() {
  wifi_sta_list_t stations = {};

  if (esp_wifi_ap_get_sta_list(&stations) != ESP_OK) {
    switchFound = false;
    switchIp = "0.0.0.0";
    return;
  }

  for (int i = 0; i < stations.num; i++) {
    const uint8_t *m = stations.sta[i].mac;

    if (memcmp(m, SWITCH_MAC, 6) != 0) continue;

    esp_netif_pair_mac_ip_t pair = {};
    memcpy(pair.mac, m, 6);

    esp_err_t err = esp_netif_dhcps_get_clients_by_mac(
        WiFi.AP.netif(), 1, &pair);

    if (err != ESP_OK || pair.ip.addr == 0) {
      memset(&pair.ip, 0, sizeof(pair.ip));

      err = esp_netif_arp_get_client_by_mac(
          WiFi.AP.netif(), &pair);
    }

    if (err != ESP_OK || pair.ip.addr == 0) {
      switchFound = false;
      switchIp = "0.0.0.0";
      return;
    }

    IPAddress ip(
        pair.ip.addr & 0xff,
        (pair.ip.addr >> 8) & 0xff,
        (pair.ip.addr >> 16) & 0xff,
        (pair.ip.addr >> 24) & 0xff);

    if (ip == IPAddress(0, 0, 0, 0)) {
      switchFound = false;
      switchIp = "0.0.0.0";
      return;
    }

    switchIp = ip.toString();
    switchFound = true;
    return;
  }

  switchFound = false;
  switchIp = "0.0.0.0";
}

static bool waitForSwitchIP(uint32_t timeoutMs) {
  uint32_t started = millis();

  while (millis() - started < timeoutMs) {
    discoverSwitchByMac();

    IPAddress ip;
    if (switchFound &&
        ip.fromString(switchIp) &&
        ip != IPAddress(0, 0, 0, 0)) {
      return true;
    }

    delay(250);
  }

  return false;
}

// ============================================================
// UPSTREAM WI-FI:
// ============================================================

static void connectUpstream() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.printf("[Wi-Fi] Connecting to %s", UPLINK_SSID);

  WiFi.begin(UPLINK_SSID, UPLINK_PASSWORD);

  uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - started < 20000) {
    delay(250);
  }

  if (WiFi.status() == WL_CONNECTED) {
    WiFi.AP.enableNAPT(true);
    Serial.printf(" - connected, IP %s\n",
                  WiFi.localIP().toString().c_str());
  } else {
    Serial.println(" - failed; will retry");
  }
}

// ============================================================
// SETUP
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(500);

  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);

  IPAddress apIP(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);

  if (!WiFi.softAPConfig(apIP, gateway, subnet)) {
    Serial.println("[Error] AP configuration failed");
  }

  if (!WiFi.softAP(AP_SSID)) {
    Serial.println("[Error] Could not start SWITCHAP");
  }

  if (!WiFi.AP.enableNAPT(true)) {
    Serial.println("[Warning] NAPT could not be enabled");
  }

  Serial.printf("\n[Ready] %s at http://192.168.4.1/\n",
                AP_SSID);

  connectUpstream();

  server.on("/", HTTP_GET, []() {
    server.sendHeader("Cache-Control",
                      "no-store, no-cache, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.send_P(
        200, "text/html; charset=utf-8", INDEX_HTML);
  });

  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/switch", HTTP_POST, handleSwitch);
  server.on("/api/query", HTTP_POST, handleQuery);

  server.onNotFound([]() {
    sendText(404, "Not found");
  });

  server.begin();
  Serial.println("[Ready] Web server started");
}

// ============================================================
// MAIN LOOP
// ============================================================

void loop() {
  server.handleClient();

  if (millis() - lastSwitchScan >= 3000) {
    lastSwitchScan = millis();
    discoverSwitchByMac();
  }

  if (WiFi.status() != WL_CONNECTED &&
      millis() - lastReconnectAttempt >= 30000) {
    lastReconnectAttempt = millis();
    connectUpstream();
  }

  delay(2);
}
