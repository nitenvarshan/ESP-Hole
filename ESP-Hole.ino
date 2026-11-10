/**
 * ============================================================
 *  ESP-Hole: The $4 Hardware DNS Sinkhole for ESP32
 *  A lightweight Pi-hole alternative for network-wide ad blocking.
 *
 *  Features:
 *    - UDP Port 53 DNS listener
 *    - 0.0.0.0 Sinkhole for ad networks, banners, and telemetry
 *    - Automatic router DNS forwarding with 8.8.8.8 fallback
 *    - Live Dark-Cyberpunk Web Dashboard on Port 80
 *
 *  GitHub: https://github.com/nitenvarshan/ESP-Hole
 * ============================================================
 */

#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>

// ── 1. Enter Your Wi-Fi Credentials Here ─────────────────────
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

// Upstream DNS server configuration (Port 53)
const uint16_t DNS_PORT = 53;

// ── 2. Hardware / Network Objects ────────────────────────────
WiFiUDP dnsServer;
WiFiUDP upstreamClient;
WebServer server(80);

// ── 3. Ad-Blocking Statistics & Log Buffer ───────────────────
unsigned long totalQueries   = 0;
unsigned long blockedQueries = 0;
unsigned long allowedQueries = 0;

struct LogEntry {
    char domain[64];
    bool blocked;
    unsigned long timestamp;
};

#define MAX_LOGS 15
LogEntry recentLogs[MAX_LOGS];
int logHead = 0;

void addLog(const char* domain, bool blocked) {
    strncpy(recentLogs[logHead].domain, domain, sizeof(recentLogs[logHead].domain) - 1);
    recentLogs[logHead].domain[sizeof(recentLogs[logHead].domain) - 1] = '\0';
    recentLogs[logHead].blocked = blocked;
    recentLogs[logHead].timestamp = millis();
    logHead = (logHead + 1) % MAX_LOGS;
}

// ── 4. Ad-Block Pattern Filters (SRAM Optimized) ─────────────
// Matches common ad networks, popups, telemetry, and tracking servers
const char* AD_KEYWORDS[] = {
    "doubleclick",
    "googleads",
    "adservice",
    "pagead",
    "admob",
    "unityads",
    "applovin",
    "applvn",
    "vungle",
    "ironsrc",
    "inmobi",
    "adcolony",
    "chartboost",
    "taboola",
    "outbrain",
    "criteo",
    "popads",
    "adroll",
    "scorecardresearch",
    "quantserve",
    "analytics.google",
    "telemetry",
    "metrics",
    "advertising",
    "adnxs",
    "amazon-adsystem",
    "rubiconproject",
    "openx",
    "pubmatic"
};

const int NUM_KEYWORDS = sizeof(AD_KEYWORDS) / sizeof(AD_KEYWORDS[0]);

bool isAdDomain(const char* domain) {
    String d = String(domain);
    d.toLowerCase();

    for (int i = 0; i < NUM_KEYWORDS; i++) {
        if (d.indexOf(AD_KEYWORDS[i]) >= 0) {
            return true;
        }
    }
    return false;
}

// ── 5. DNS Packet Parsing & Response Construction ────────────

// Extract domain string from raw DNS query payload
bool parseDNSQuery(uint8_t* buffer, int len, char* outDomain, int maxLen) {
    if (len < 12) return false;

    int pos = 12; // Skip 12-byte DNS header
    int outPos = 0;

    while (pos < len && buffer[pos] != 0) {
        int labelLen = buffer[pos++];
        if (pos + labelLen > len) return false;

        for (int i = 0; i < labelLen; i++) {
            if (outPos < maxLen - 1) {
                outDomain[outPos++] = (char)buffer[pos++];
            } else {
                pos++;
            }
        }
        if (buffer[pos] != 0 && outPos < maxLen - 1) {
            outDomain[outPos++] = '.';
        }
    }
    outDomain[outPos] = '\0';
    return (outPos > 0);
}

// Send a blocked DNS response (resolving domain to 0.0.0.0 sinkhole)
void sendBlockedResponse(IPAddress clientIP, uint16_t clientPort, uint8_t* queryBuf, int queryLen) {
    uint8_t resp[512];
    if (queryLen > 480) queryLen = 480;

    // Copy original query
    memcpy(resp, queryBuf, queryLen);

    // Set Header Flags: Standard response, No error, Authoritative (0x8180)
    resp[2] = 0x81;
    resp[3] = 0x80;

    // Set Answer Count = 1
    resp[6] = 0x00;
    resp[7] = 0x01;

    int pos = queryLen;

    // Answer RR:
    // Name pointer to question domain in header (offset 12 = 0x0C)
    resp[pos++] = 0xC0;
    resp[pos++] = 0x0C;

    // Type: A (IPv4 host address = 0x0001)
    resp[pos++] = 0x00;
    resp[pos++] = 0x01;

    // Class: IN (Internet = 0x0001)
    resp[pos++] = 0x00;
    resp[pos++] = 0x01;

    // TTL: 60 seconds (0x0000003C)
    resp[pos++] = 0x00;
    resp[pos++] = 0x00;
    resp[pos++] = 0x00;
    resp[pos++] = 0x3C;

    // Data Length: 4 bytes
    resp[pos++] = 0x00;
    resp[pos++] = 0x04;

    // SINKHOLE IP: 0.0.0.0 (Ad blocked!)
    resp[pos++] = 0x00;
    resp[pos++] = 0x00;
    resp[pos++] = 0x00;
    resp[pos++] = 0x00;

    dnsServer.beginPacket(clientIP, clientPort);
    dnsServer.write(resp, pos);
    dnsServer.endPacket();
}

// Forward clean query upstream (using router DNS or 8.8.8.8 fallback) and return answer
void forwardQueryUpstream(IPAddress clientIP, uint16_t clientPort, uint8_t* queryBuf, int queryLen) {
    IPAddress targetDNS = WiFi.dnsIP();
    if (targetDNS == INADDR_NONE || targetDNS[0] == 0) {
        targetDNS = IPAddress(8, 8, 8, 8);
    }

    upstreamClient.beginPacket(targetDNS, DNS_PORT);
    upstreamClient.write(queryBuf, queryLen);
    upstreamClient.endPacket();

    // Wait for response (up to 1200ms)
    unsigned long start = millis();
    int respLen = 0;
    while (millis() - start < 1200) {
        respLen = upstreamClient.parsePacket();
        if (respLen > 0) break;
        delay(4);
    }

    if (respLen > 0) {
        uint8_t respBuf[512];
        int readBytes = upstreamClient.read(respBuf, sizeof(respBuf));
        if (readBytes > 0) {
            dnsServer.beginPacket(clientIP, clientPort);
            dnsServer.write(respBuf, readBytes);
            dnsServer.endPacket();
            return;
        }
    }

    // Fallback: If router DNS failed, try Google DNS (8.8.8.8)
    if (targetDNS != IPAddress(8, 8, 8, 8)) {
        upstreamClient.beginPacket(IPAddress(8, 8, 8, 8), DNS_PORT);
        upstreamClient.write(queryBuf, queryLen);
        upstreamClient.endPacket();
        start = millis();
        while (millis() - start < 1000) {
            respLen = upstreamClient.parsePacket();
            if (respLen > 0) break;
            delay(4);
        }
        if (respLen > 0) {
            uint8_t respBuf[512];
            int readBytes = upstreamClient.read(respBuf, sizeof(respBuf));
            if (readBytes > 0) {
                dnsServer.beginPacket(clientIP, clientPort);
                dnsServer.write(respBuf, readBytes);
                dnsServer.endPacket();
            }
        }
    }
}

// ── 6. Web Dashboard (Live Cyberpunk Status Page) ─────────────

void handleRoot() {
    float blockRate = (totalQueries > 0) ? ((float)blockedQueries / totalQueries * 100.0f) : 0.0f;

    String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<title>ESP-Hole Ad-Blocker</title>";
    html += "<style>";
    html += "body{background:#030712;color:#f8fafc;font-family:monospace;margin:0;padding:20px;display:flex;justify-content:center;}";
    html += ".card{max-width:550px;width:100%;background:#0f172a;border:1px solid #00f0ff;border-radius:12px;padding:20px;box-shadow:0 0 20px rgba(0,240,255,0.2);}";
    html += "h1{color:#00f0ff;margin:0 0 5px;font-size:20px;}";
    html += ".stats{display:grid;grid-template-columns:1fr 1fr 1fr;gap:10px;margin:20px 0;}";
    html += ".stat-box{background:#1e293b;padding:12px;border-radius:8px;text-align:center;}";
    html += ".val{font-size:22px;font-weight:bold;margin-top:4px;}";
    html += ".blocked{color:#ef4444;} .clean{color:#10b981;} .pct{color:#00f0ff;}";
    html += ".log-item{padding:8px;border-bottom:1px solid #1e293b;font-size:12px;display:flex;justify-content:space-between;word-break:break-all;}";
    html += ".tag{padding:2px 6px;border-radius:4px;font-size:10px;font-weight:bold;margin-left:8px;}";
    html += ".tag-block{background:#7f1d1d;color:#fca5a5;} .tag-pass{background:#064e3b;color:#6ee7b7;}";
    html += "</style>";
    html += "<meta http-equiv='refresh' content='3'>"; // Auto-refresh every 3s
    html += "</head><body><div class='card'>";
    html += "<h1>🛡️ ESP-HOLE AD-BLOCKER</h1>";
    html += "<div style='color:#64748b;font-size:12px;'>Hardware DNS Sinkhole • Running on Port 53</div>";

    html += "<div class='stats'>";
    html += "<div class='stat-box'><div style='color:#94a3b8;font-size:11px;'>TOTAL QUERIES</div><div class='val'>" + String(totalQueries) + "</div></div>";
    html += "<div class='stat-box'><div style='color:#94a3b8;font-size:11px;'>ADS BLOCKED</div><div class='val blocked'>" + String(blockedQueries) + "</div></div>";
    html += "<div class='stat-box'><div style='color:#94a3b8;font-size:11px;'>BLOCK RATE</div><div class='val pct'>" + String(blockRate, 1) + "%</div></div>";
    html += "</div>";

    html += "<div style='font-weight:bold;margin-bottom:8px;color:#cbd5e1;font-size:13px;'>Recent DNS Queries (Real-Time):</div>";
    html += "<div style='background:#030712;border-radius:8px;padding:6px;'>";

    int count = 0;
    for (int i = 0; i < MAX_LOGS; i++) {
        int idx = (logHead - 1 - i + MAX_LOGS) % MAX_LOGS;
        if (strlen(recentLogs[idx].domain) > 0) {
            count++;
            html += "<div class='log-item'>";
            html += "<span>" + String(recentLogs[idx].domain) + "</span>";
            if (recentLogs[idx].blocked) {
                html += "<span class='tag tag-block'>BLOCKED (0.0.0.0)</span>";
            } else {
                html += "<span class='tag tag-pass'>ALLOWED</span>";
            }
            html += "</div>";
        }
    }
    if (count == 0) {
        html += "<div style='padding:12px;color:#64748b;text-align:center;'>Waiting for devices to send DNS queries...</div>";
    }

    html += "</div>";
    html += "<div style='margin-top:15px;font-size:11px;color:#64748b;text-align:center;'>Set your phone DNS to: <b style='color:#00f0ff;'>" + WiFi.localIP().toString() + "</b></div>";
    html += "</div></body></html>";

    server.send(200, "text/html", html);
}

// ── 7. Setup & Main Loop ─────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("\n╔═══════════════════════════════════════════╗");
    Serial.println("║   ESP-HOLE (HARDWARE DNS SINKHOLE)        ║");
    Serial.println("╚═══════════════════════════════════════════╝");

    // Connect to local Wi-Fi
    Serial.printf("[WIFI] Connecting to '%s'...", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    while (WiFi.status() != WL_CONNECTED) {
        delay(400);
        Serial.print(".");
    }

    Serial.println(" OK!");
    Serial.printf("[WIFI] IP Address assigned: %s\n", WiFi.localIP().toString().c_str());

    // Start UDP DNS Server on port 53
    dnsServer.begin(DNS_PORT);
    upstreamClient.begin(0);
    Serial.println("[DNS]  Server started on UDP port 53");

    // Start Web Dashboard on port 80
    server.on("/", handleRoot);
    server.begin();
    Serial.printf("[WEB]  Live Dashboard running at: http://%s/\n\n", WiFi.localIP().toString().c_str());

    Serial.println("=================================================");
    Serial.println(" HOW TO TEST ON YOUR PHONE / LAPTOP:");
    Serial.println(" 1. Open your Wi-Fi Settings on your phone/PC.");
    Serial.println(" 2. Tap your Wi-Fi name -> Configure DNS -> Manual.");
    Serial.printf (" 3. Set Primary DNS to: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf (" 4. Open http://%s/ in your browser to see stats!\n", WiFi.localIP().toString().c_str());
    Serial.println("=================================================\n");
}

void loop() {
    // Handle Web Dashboard requests
    server.handleClient();

    // Check for incoming DNS queries
    int packetSize = dnsServer.parsePacket();
    if (packetSize > 0) {
        uint8_t buffer[512];
        int len = dnsServer.read(buffer, sizeof(buffer));

        if (len > 0) {
            totalQueries++;
            char domain[128];

            if (parseDNSQuery(buffer, len, domain, sizeof(domain))) {
                if (isAdDomain(domain)) {
                    blockedQueries++;
                    addLog(domain, true);
                    Serial.printf("[⛔ BLOCKED] %s  --> 0.0.0.0\n", domain);
                    sendBlockedResponse(dnsServer.remoteIP(), dnsServer.remotePort(), buffer, len);
                } else {
                    allowedQueries++;
                    addLog(domain, false);
                    Serial.printf("[✓ PASSED]  %s\n", domain);
                    forwardQueryUpstream(dnsServer.remoteIP(), dnsServer.remotePort(), buffer, len);
                }
            }
        }
    }
}
