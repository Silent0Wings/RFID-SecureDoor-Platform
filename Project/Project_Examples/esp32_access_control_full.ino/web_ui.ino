// web_ui.ino
// Web UI HTML builders and HTTP route setup

// ---------------------------
// Home page HTML
// ---------------------------
String buildHomePageHtml() {
  String html;
  html =
    "<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
    "<style>"
    "body{font-family:Arial,Helvetica,sans-serif;margin:0;padding:1rem;text-align:center;}"
    ".card{max-width:480px;margin:0 auto;border:1px solid #ccc;"
    "padding:1rem;border-radius:8px;}"
    "a.button{display:block;margin:0.5rem 0;padding:0.5rem 1rem;"
    "border:1px solid #007bff;border-radius:4px;text-decoration:none;"
    "color:#007bff;}"
    "</style>"
    "</head><body>"
    "<div class='card'>"
    "<h2>ESP32 Access Control</h2>"
    "<p>Room "
    + roomID + "</p>"
               "<a class='button' href='/login'>Admin login</a>"
               "<a class='button' href='/register'>Register new card</a>"
               "<a class='button' href='/status'>System status</a>"
               "</div>"
               "</body></html>";
  return html;
}

String buildRegisterPageHtml(const String &message) {
  String html;
  html =
    "<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
    "<style>"
    "body{font-family:Arial,Helvetica,sans-serif;margin:0;padding:1rem;}"
    ".card{max-width:480px;margin:0 auto;border:1px solid #ccc;"
    "padding:1rem;border-radius:8px;}"
    "label{display:block;margin-top:0.5rem;}"
    "input[type=text],input[type=password]{width:100%;padding:0.5rem;"
    "margin-top:0.25rem;box-sizing:border-box;}"
    "button{margin-top:1rem;padding:0.5rem 1rem;width:100%;}"
    ".msg{margin-top:0.5rem;white-space:pre-wrap;}"
    ".links{margin-top:1rem;text-align:center;}"
    "a{text-decoration:none;}"
    "</style>"
    "</head><body>"
    "<div class='card'>"
    "<h2>Register new card</h2>"
    "<form action='/register' method='GET'>"
    "<label>User:<input name='user' type='text' required></label>"
    "<label>Password:<input name='password' type='password' required></label>"
    "<button type='submit'>Start registration</button>"
    "</form>";

  if (message.length()) {
    html += "<div class='msg'>" + message + "</div>";
  }

  html +=
    "<div class='links'>"
    "<a href='/'>Home</a> | <a href='/status'>Status</a>"
    "</div>"
    "</div></body></html>";

  return html;
}

// Simple JSON extractor for flat keys
String extractJsonField(const String &src, const char *key) {
  String pattern = String("\"") + key + "\"";
  int p = src.indexOf(pattern);
  if (p < 0) return "";
  p = src.indexOf(':', p);
  if (p < 0) return "";
  p++;
  while (p < (int)src.length() && (src[p] == ' ' || src[p] == '\"')) p++;
  String out;
  while (p < (int)src.length() && src[p] != '\"' && src[p] != ',' && src[p] != '}') {
    out += src[p++];
  }
  return out;
}

// ---------------------------
// HTML builders
// ---------------------------
String buildLoginFormHtml() {
  String html;
  html =
    "<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
    "<style>"
    "body{font-family:Arial,Helvetica,sans-serif;margin:0;padding:1rem;}"
    ".card{max-width:480px;margin:0 auto;border:1px solid #ccc;"
    "padding:1rem;border-radius:8px;}"
    "label{display:block;margin-top:0.5rem;}"
    "input[type=text],input[type=password]{width:100%;padding:0.5rem;"
    "margin-top:0.25rem;box-sizing:border-box;}"
    "button{margin-top:1rem;padding:0.5rem 1rem;width:100%;}"
    ".links{margin-top:1rem;text-align:center;}"
    "a{text-decoration:none;}"
    "</style>"
    "</head><body>"
    "<div class='card'>"
    "<h2>Admin login</h2>"
    "<form action='/login' method='GET'>"
    "<label>User:<input name='user' type='text' required></label>"
    "<label>Password:<input name='password' type='password' required></label>"
    "<button type='submit'>Fetch my data</button>"
    "</form>"
    "<div class='links'><a href='/'>Back</a></div>"
    "</div>"
    "<script>"
    "setInterval(function(){"
    "fetch('/login-hint')"
    ".then(function(r){if(!r.ok) return null; return r.json();})"
    ".then(function(d){"
    "if(!d || !d.user) return;"
    "var inp = document.querySelector('input[name=\"user\"]');"
    "if(inp && !inp.value){ inp.value = d.user; }"
    "})"
    ".catch(function(e){});"
    "},1000);"
    "</script>"
    "</body></html>";
  return html;
}

String buildLoginResultHtml(const String &tableHtml, const String &errorText) {
  String html;
  html =
    "<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
    "<style>"
    "body{font-family:Arial,Helvetica,sans-serif;margin:0;padding:1rem;}"
    ".card{max-width:480px;margin:0 auto;border:1px solid #ccc;"
    "padding:1rem;border-radius:8px;}"
    "h2{margin-top:0;}"
    ".error{color:#b00020;margin-top:0.5rem;white-space:pre-wrap;"
    "word-wrap:break-word;}"
    "table{width:100%;border-collapse:collapse;margin-top:0.5rem;}"
    "th,td{border:1px solid #ccc;padding:0.4rem;text-align:left;"
    "font-size:0.9rem;}"
    ".links{margin-top:1rem;text-align:center;}"
    "a{text-decoration:none;}"
    "pre{white-space:pre-wrap;font-size:0.8rem;"
    "background:#f5f5f5;padding:0.5rem;}"
    "</style>"
    "</head><body>"
    "<div class='card'>"
    "<h2>My access data</h2>";

  if (errorText.length() > 0) {
    html += "<div class='error'>" + errorText + "</div>";
  } else {
    html += tableHtml;
    html += "<h3>Backend call status</h3>";
    html += "<table>";
    html += "<tr><td>HTTP code</td><td>" + String(lastStatsHttpCode) + "</td></tr>";
    html += "<tr><td>User</td><td>" + lastStatsUser + "</td></tr>";
    html += "<tr><td>Age (ms)</td><td>" + String(millis() - lastStatsMillis) + "</td></tr>";
    html += "</table>";
    html += "<h3>Raw JSON</h3><pre>" + lastStatsRawJson + "</pre>";
  }

  html +=
    "<div class='links'>"
    "<a href='/login'>Back to login</a><br>"
    "<a href='/status'>System status</a><br>"
    "<a href='/'>Home</a>"
    "</div>"
    "</div></body></html>";

  return html;
}

String buildStatusPageHtml() {
  String html;
  html =
    "<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
    "<style>"
    "body{font-family:Arial,Helvetica,sans-serif;margin:0;padding:1rem;}"
    ".card{max-width:600px;margin:0 auto;border:1px solid #ccc;"
    "padding:1rem;border-radius:8px;}"
    "table{width:100%;border-collapse:collapse;margin-top:0.5rem;}"
    "th,td{border:1px solid #ccc;padding:0.4rem;text-align:left;"
    "font-size:0.9rem;}"
    "pre{white-space:pre-wrap;font-size:0.8rem;"
    "background:#f5f5f5;padding:0.5rem;}"
    ".links{text-align:center;margin-top:1rem;}"
    "</style>"
    "</head><body>"
    "<div class='card'>"
    "<h2>System status</h2>"
    "<table>"
    "<tr><th>Field</th><th>Value</th></tr>";

  html += "<tr><td>WiFi</td><td>" + String(WiFi.status() == WL_CONNECTED ? "CONNECTED" : "DISCONNECTED") + "</td></tr>";
  html += "<tr><td>Last RFID UID</td><td>" + lastUid + "</td></tr>";
  html += "<tr><td>Last RFID status</td><td>" + lastStatus + "</td></tr>";
  html += "<tr><td>Last RFID age (ms)</td><td>" + String(millis() - lastEventMillis) + "</td></tr>";
  html += "<tr><td>Last stats user</td><td>" + lastStatsUser + "</td></tr>";
  html += "<tr><td>Last stats HTTP</td><td>" + String(lastStatsHttpCode) + "</td></tr>";
  html += "<tr><td>Last stats error</td><td>" + (lastStatsError.length() ? lastStatsError : "none") + "</td></tr>";
  html += "</table>";

  if (lastStatsRawJson.length()) {
    html += "<h3>Last stats JSON</h3><pre>" + lastStatsRawJson + "</pre>";
  }

  html +=
    "<div class='links'><a href='/'>Home</a> | <a href='/login'>Login</a></div>"
    "</div>"
    "<script>"
    "setTimeout(function(){ location.reload(); }, 2000);"
    "</script>"
    "</body></html>";

  return html;
}

// ---------------------------
// Route setup
// ---------------------------
void setupRoutes() {
  // HOME
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", buildHomePageHtml());
  });

  // LOGIN HINT - JSON with last suggested username from scanned card
  server.on("/login-hint", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"uid\":\"" + lastLoginSuggestedUid + "\",";
    json += "\"user\":\"" + lastLoginSuggestedUser + "\"";
    json += "}";
    request->send(200, "application/json", json);
  });

  // REGISTER
  server.on("/register", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("user") || !request->hasParam("password")) {
      request->send(200, "text/html", buildRegisterPageHtml(""));
      return;
    }

    tempUsername = request->getParam("user")->value();
    tempPassword = request->getParam("password")->value();

    waitingForRFID = true;
    rfidTimeout = millis() + 15000;
    lastStatus = "REG_WAIT";

    updateIndicatorsForStatus();

    String msg = "Registration started.\n\n"
                 "User: "
                 + tempUsername + "\n"
                                  "Tap the new card on the reader within 15 seconds.";
    request->send(200, "text/html", buildRegisterPageHtml(msg));
  });

  // LOGIN
  server.on("/login", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("user") || !request->hasParam("password")) {
      request->send(200, "text/html", buildLoginFormHtml());
      return;
    }

    const String user = request->getParam("user")->value();
    const String pass = request->getParam("password")->value();
    lastStatsUser = user;

    String userJson;
    String errorText;
    int httpCode = -1;

    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      const String url = String(statsUrl) + "?user=" + user + "&password=" + pass;

      http.begin(url);
      httpCode = http.GET();
      if (httpCode > 0) {
        userJson = http.getString();
      } else {
        errorText = "HTTP request failed, code " + String(httpCode);
      }
      http.end();
    } else {
      errorText = "No WiFi connection.";
    }

    lastStatsHttpCode = httpCode;
    lastStatsRawJson = userJson;
    lastStatsMillis = millis();

    String tableHtml;

    if (errorText.isEmpty() && httpCode == 200) {
      lastLoginSuggestedUser = "";
      lastLoginSuggestedUid = "";

      const String vUser = extractJsonField(userJson, "user");
      const String vPass = extractJsonField(userJson, "password");
      const String vUid = extractJsonField(userJson, "uid");
      const String vCounter = extractJsonField(userJson, "counter");
      const String vRoom = extractJsonField(userJson, "roomID");
      const String vAccess = extractJsonField(userJson, "access");

      tableHtml =
        "<table>"
        "<tr><th>Field</th><th>Value</th></tr>"
        "<tr><td>user</td><td>"
        + vUser + "</td></tr>"
                  "<tr><td>password</td><td>"
        + vPass + "</td></tr>"
                  "<tr><td>uid</td><td>"
        + vUid + "</td></tr>"
                 "<tr><td>counter</td><td>"
        + vCounter + "</td></tr>"
                     "<tr><td>roomID</td><td>"
        + vRoom + "</td></tr>"
                  "<tr><td>access</td><td>"
        + vAccess + "</td></tr>"
                    "</table>";

      lastStatsError = "";
    } else {
      if (errorText.isEmpty()) {
        errorText = "Backend HTTP " + String(httpCode) + ". Body: " + userJson;
      }
      lastStatsError = errorText;
    }

    request->send(200, "text/html", buildLoginResultHtml(tableHtml, errorText));
  });

  // STATUS
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", buildStatusPageHtml());
  });

  // 404 -> home
  server.onNotFound([](AsyncWebServerRequest *request) {
    request->redirect("/");
  });
}
