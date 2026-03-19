#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

#include "config.h"
#include "webUI.h"

bool isAPMode = false;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

const int MAX_HISTORY = 15;
String chatHistory[MAX_HISTORY];
int historyIndex = 0;
int totalMessages = 0;

const int MAX_CLIENTS = 20;
uint32_t clientIds[MAX_CLIENTS];
String clientNames[MAX_CLIENTS];
int clientCount = 0;

volatile unsigned long ledTurnedOnAt = 0;
volatile bool isLedOn = false;
unsigned long lastCleanup = 0;
unsigned long lastWiFiCheck = 0;

bool lastButtonState = HIGH;
unsigned long lastPressTime = 0;

void blinkModeIndicator(uint8_t blinkCount)
{
  for (uint8_t i = 0; i < blinkCount; i++)
  {
    digitalWrite(LED_PIN, HIGH);
    delay(120);
    digitalWrite(LED_PIN, LOW);
    if (i < blinkCount - 1)
    {
      delay(120);
    }
  }
}

String sanitizeChatText(const String &input, size_t maxLen, bool isName)
{
  String out = "";
  out.reserve(maxLen);

  for (size_t i = 0; i < input.length(); i++)
  {
    char c = input.charAt(i);

    if (c == '\r' || c == '\n' || c == '\t')
    {
      c = ' ';
    }

    if ((uint8_t)c < 32)
    {
      continue;
    }

    if (isName && c == ':')
    {
      continue;
    }

    out += c;
    if (out.length() >= maxLen)
    {
      break;
    }
  }

  out.trim();
  return out;
}

int findClientIndex(uint32_t id)
{
  for (int i = 0; i < clientCount; i++)
  {
    if (clientIds[i] == id)
      return i;
  }
  return -1;
}

String getClientDisplayName(AsyncWebSocketClient *client)
{
  int idx = findClientIndex(client->id());
  if (idx >= 0 && clientNames[idx].length() > 0)
  {
    return clientNames[idx];
  }
  return "User " + String(client->id());
}

void setClientName(AsyncWebSocketClient *client, const String &name)
{
  int idx = findClientIndex(client->id());
  if (idx >= 0)
  {
    clientNames[idx] = name;
    return;
  }

  if (clientCount >= MAX_CLIENTS)
  {
    return;
  }

  clientIds[clientCount] = client->id();
  clientNames[clientCount] = name;
  clientCount++;
}

void removeClient(AsyncWebSocketClient *client)
{
  int idx = findClientIndex(client->id());
  if (idx < 0)
    return;

  for (int i = idx; i < clientCount - 1; i++)
  {
    clientIds[i] = clientIds[i + 1];
    clientNames[i] = clientNames[i + 1];
  }
  clientCount--;
}

// ==========================================
// LOGIC WEBSOCKET
// ==========================================
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len, AsyncWebSocketClient *client)
{
  String message = "";
  for (size_t i = 0; i < len; i++)
  {
    message += (char)data[i];
  }
  message = sanitizeChatText(message, 180, false);
  if (message.length() == 0)
    return;

  const String setNamePrefix = "__setname__:";
  if (message.startsWith(setNamePrefix))
  {
    String name = message.substring(setNamePrefix.length());
    name = sanitizeChatText(name, 20, true);
    if (name.length() == 0)
      return;

    String oldName = getClientDisplayName(client);
    setClientName(client, name);
    String newName = getClientDisplayName(client);

    if (oldName == newName)
    {
      client->text("SYS: User " + newName + ".");
    }
    else
    {
      ws.textAll("SYS: " + oldName + " changed their name to " + newName + ".");
    }
    return;
  }

  String finalMsg = getClientDisplayName(client) + ": " + message;
  chatHistory[historyIndex] = finalMsg;
  historyIndex = (historyIndex + 1) % MAX_HISTORY;
  if (totalMessages < MAX_HISTORY)
    totalMessages++;

  ws.textAll(finalMsg);

  digitalWrite(LED_PIN, HIGH);
  ledTurnedOnAt = millis();
  isLedOn = true;
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
  if (type == WS_EVT_CONNECT)
  {
    for (int i = 0; i < totalMessages; i++)
    {
      int index = (historyIndex - totalMessages + i + MAX_HISTORY) % MAX_HISTORY;
      client->text(chatHistory[index]);
    }
    ws.textAll("SYS: User " + String(client->id()) + " has joined.");
    client->text("SYS: Please enter your username to start chatting.");
  }
  else if (type == WS_EVT_DISCONNECT)
  {
    String displayName = getClientDisplayName(client);
    ws.textAll("SYS: " + displayName + " has left.");
    removeClient(client);
  }
  else if (type == WS_EVT_DATA)
  {
    handleWebSocketMessage(arg, data, len, client);
  }
}

// ==========================================
// WIFI MODE
// ==========================================
void applyWiFiMode()
{
  WiFi.disconnect();
  delay(200);

  if (isAPMode)
  {
    Serial.println("\n[!] Access Point");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ap_ssid, ap_password);
    Serial.print("broadcasting! IP: ");
    Serial.println(WiFi.softAPIP());
  }
  else
  {
    Serial.println("\n[!] Station Mode");
    WiFi.mode(WIFI_STA);
    WiFi.begin(sta_ssid, sta_password);

    Serial.print("connecting");
    int timeout = 0;
    while (WiFi.status() != WL_CONNECTED && timeout < 20)
    {
      delay(500);
      Serial.print(".");
      timeout++;
    }

    if (WiFi.status() == WL_CONNECTED)
    {
      Serial.println("\nsuccess!");
      Serial.print("IP: ");
      Serial.println(WiFi.localIP().toString());
    }
    else
    {
      Serial.println("\nfailed to connect, double check credentials!");
    }
  }

  blinkModeIndicator(isAPMode ? 1 : 3);
}

// ==========================================
// SETUP
// ==========================================
void setup()
{
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(LED_PIN, LOW);

  WiFi.setSleep(false);

  applyWiFiMode();

  ws.onEvent(onEvent);
  server.addHandler(&ws);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send_P(200, "text/html", index_html); });

  server.begin();
  Serial.println("\nREADY READY READY");
}

// ==========================================
// LOOP
// ==========================================
void loop()
{
  // flick upon new message
  if (isLedOn && (millis() - ledTurnedOnAt > 100))
  {
    digitalWrite(LED_PIN, LOW);
    isLedOn = false;
  }

  if (millis() - lastCleanup > 2000)
  {
    ws.cleanupClients();
    lastCleanup = millis();
  }

  bool currentButtonState = digitalRead(BUTTON_PIN);
  if (currentButtonState == LOW && lastButtonState == HIGH && (millis() - lastPressTime > 300))
  {
    Serial.println("\nswitching mode...");
    isAPMode = !isAPMode;
    // ws.textAll("SYS: Switching WiFi mode. Please refresh the page in 5 seconds!");
    applyWiFiMode();
    lastPressTime = millis();
  }
  lastButtonState = currentButtonState;

  if (!isAPMode && WiFi.status() != WL_CONNECTED && (millis() - lastWiFiCheck > 10000))
  {
    Serial.println("router disconnected, reconnecting...");
    WiFi.disconnect();
    WiFi.begin(sta_ssid, sta_password);
    lastWiFiCheck = millis();
  }
}