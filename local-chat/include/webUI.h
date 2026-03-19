#pragma once
#include <Arduino.h>

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>ESP32 ChatRoom v1.0</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <style>
    body { background-color: #c0c0c0; color: #000000; font-family: "Times New Roman", Times, serif; margin: 10px; }
    h1 { color: #000080; text-align: center; font-style: italic; border-bottom: 3px double #808080; padding-bottom: 5px; }
    #chat-box { background-color: #ffffff; border: 3px inset #ffffff; height: 60vh; overflow-y: scroll; padding: 5px; margin-bottom: 10px; font-size: 16px; }
    .msg { margin-bottom: 4px; }
    .sys-msg { color: #ff0000; font-weight: bold; }
    .controls-row { display: flex; align-items: center; gap: 8px; margin-bottom: 10px; }
    input[type="text"] { font-family: "Courier New", Courier, monospace; font-size: 16px; border: 2px inset #ffffff; padding: 2px; }
    #username-input { width: 140px; font-size: 14px; }
    #setname-btn { padding: 2px 10px; font-size: 14px; }
    #msg-input { flex: 1; min-width: 0; }
    button { font-family: "Times New Roman", Times, serif; font-size: 16px; font-weight: bold; background-color: #c0c0c0; border: 2px outset #ffffff; padding: 2px 15px; cursor: pointer; color: #000; }
    button:active { border: 2px inset #ffffff; }
    hr { border: 1px inset #fff; }
    .footer { text-align: center; font-size: 12px; margin-top: 15px; color: #555; }
  </style>
</head>
<body>
  <h1><marquee width="80%" behavior="alternate">Welcome to ESP32 LAN ChatRoom!</marquee></h1>
  <div id="chat-box"></div>
  <div class="controls-row">
    <input type="text" id="username-input" placeholder="Enter your username..." autocomplete="off" maxlength="20" onkeypress="handleUsernameEnter(event)">
    <button id="setname-btn" onclick="setUsername()">Set Name</button>
    <input type="text" id="msg-input" placeholder="Type your message..." autocomplete="off" onkeypress="handleEnter(event)">
    <button onclick="sendMessage()">Send</button>
  </div>
  <hr>
  <div class="footer">
    <p>made with love, curiosity by tuan phung</p>
  </div>
  <script>
    const gateway = `ws://${window.location.hostname}/ws`;
    let websocket;
    let username = "";
    const chatBox = document.getElementById('chat-box');
    const msgInput = document.getElementById('msg-input');
    const usernameInput = document.getElementById('username-input');

    function sanitizeName(name) {
      return (name || '')
        .replace(/[\r\n\t]+/g, ' ')
        .replace(/[^\x20-\x7E]/g, '')
        .replace(/[:]/g, '')
        .replace(/\s+/g, ' ')
        .trim()
        .slice(0, 20);
    }

    function sanitizeMessage(msg) {
      return (msg || '')
        .replace(/[\r\n\t]+/g, ' ')
        .replace(/[^\x20-\x7E]/g, '')
        .replace(/\s+/g, ' ')
        .trim()
        .slice(0, 160);
    }

    function requestUsername(forcePrompt = false) {
      let typedName = sanitizeName(usernameInput.value);
      if (!typedName || forcePrompt) {
        const prompted = prompt('username:');
        typedName = sanitizeName(prompted || '');
      }

      if (!typedName) {
        typedName = `Guest-${Math.floor(Math.random() * 900 + 100)}`;
      }

      username = typedName;
      usernameInput.value = username;
      msgInput.disabled = false;

      if (websocket && websocket.readyState === WebSocket.OPEN) {
        websocket.send(`__setname__:${username}`);
      }
    }

    function initWebSocket() {
      websocket = new WebSocket(gateway);
      websocket.onclose = () => setTimeout(initWebSocket, 2000);
      websocket.onopen = () => requestUsername(true);
      websocket.onmessage = (e) => printMsg(e.data, 'msg');
    }

    function printMsg(text, className = 'msg') {
      const el = document.createElement('div');
      if (text.startsWith('SYS:')) {
        el.className = 'sys-msg';
      } else {
        el.className = className;
      }
      el.textContent = text;
      chatBox.appendChild(el);
      chatBox.scrollTop = chatBox.scrollHeight;
    }

    function setUsername() {
      requestUsername(false);
    }

    function sendMessage() {
      const input = document.getElementById('msg-input');
      const msg = sanitizeMessage(input.value);
      if (!username) {
        requestUsername(true);
        return;
      }

      if (msg && websocket && websocket.readyState === WebSocket.OPEN) {
        websocket.send(msg);
        input.value = "";
      }
    }
    function handleEnter(e) { if (e.key === 'Enter') sendMessage(); }
    function handleUsernameEnter(e) { if (e.key === 'Enter') setUsername(); }
    msgInput.disabled = true;
    window.onload = initWebSocket;
  </script>
</body>
</html>
)rawliteral";