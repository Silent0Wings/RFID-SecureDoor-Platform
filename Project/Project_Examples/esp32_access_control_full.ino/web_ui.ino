extern String roomID;
extern String tempUsername;
extern String tempPassword;
extern bool waitingForRFID;
extern unsigned long registrationStartTime;
extern const unsigned long REGISTER_TIMEOUT_MS;
extern void setStatus(StatusCode code);
extern void updateIndicatorsForStatus();
extern String lastStatsUser;
extern int lastStatsHttpCode;
extern String lastStatsRawJson;
extern unsigned long lastStatsMillis;
extern String lastStatsError;
extern String lastLoginSuggestedUser;
extern String lastLoginSuggestedUid;
extern String statsUrl;

// Defined in http_backend.ino
extern String extractJsonField(const String &src, const char *key);

static const char MAIN_CSS[] PROGMEM = R"rawliteral(
/* ... (CSS content identical to original, omitted for brevity) ... */
/* Assume standard CSS here */
body { background: #0e0e10; color: #e6e6e6; font-family: sans-serif; padding: 20px; }
.card { background: #1a1a1d; padding: 20px; border-radius: 8px; max-width: 600px; margin: auto; }
button { padding: 10px; width: 100%; background: #b06fff; border: none; font-weight: bold; cursor: pointer; }
input { width: 100%; padding: 10px; margin-bottom: 10px; background: #101014; color: white; border: 1px solid #444; }
.error { color: #ff4c4c; text-align: center; }
)rawliteral";

static const char LOGIN_JS[] PROGMEM = R"awljs(
(function(){
  function updateHint(){
    fetch('/login-hint').then(r=>r.ok?r.json():null).then(d=>{
        if(d&&d.user){var i=document.querySelector('input[name="user"]');if(i&&!i.value)i.value=d.user;}
      }).catch(e=>{});
  }
  setInterval(updateHint, 1000);
})();
)awljs";

String buildHomePageHtml() {
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<link rel='stylesheet' href='/style.css'></head><body><div class='card'>";
  html += "<h2>ESP32 Access Control</h2><p>Room " + roomID + "</p>";
  html += "<a class='button' href='/login' style='display:block;margin:5px 0;text-align:center;'>Admin login</a>";
  html += "<a class='button' href='/register' style='display:block;margin:5px 0;text-align:center;'>Register new card</a>";
  html += "<a class='button' href='/status' style='display:block;margin:5px 0;text-align:center;'>System status</a>";
  html += "</div></body></html>";
  return html;
}

String buildRegisterPageHtml(const String &message) {
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<link rel='stylesheet' href='/style.css'></head><body><div class='card'><h2>Register new card</h2>";
  html += "<form action='/register' method='GET'>";
  html += "<label>User:<input name='user' type='text' required></label>";
  html += "<label>Password:<input name='password' type='password' required></label>";
  html += "<button type='submit'>Start registration</button></form>";
  if (message.length()) html += "<div class='msg' style='margin-top:10px;white-space:pre-wrap'>" + message + "</div>";
  html += "<div class='links' style='text-align:center;margin-top:10px'><a href='/'>Home</a></div></div></body></html>";
  return html;
}

String buildLoginFormHtml() {
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<link rel='stylesheet' href='/style.css'><script src='/login.js' defer></script></head><body><div class='card'><h2>Admin login</h2>";
  html += "<form action='/login' method='GET'><label>User:<input name='user' type='text' required></label>";
  html += "<label>Password:<input name='password' type='password' required></label><button type='submit'>Fetch data</button></form>";
  html += "<div class='links' style='text-align:center;margin-top:10px'><a href='/'>Back</a></div></div></body></html>";
  return html;
}

String buildLoginResultHtml(const String &tableHtml, const String &errorText) {
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<link rel='stylesheet' href='/style.css'></head><body><div class='card'><h2>My access data</h2>";
  if (errorText.length() > 0) html += "<div class='error'>" + errorText + "</div>";
  else {
    html += "<div class='table-wrapper'>" + tableHtml + "</div>";
    html += "<p><b>Raw JSON:</b></p><pre>" + lastStatsRawJson + "</pre>";
  }
  html += "<div class='links' style='text-align:center;margin-top:10px'><a href='/login'>Back</a> | <a href='/'>Home</a></div></div></body></html>";
  return html;
}

String buildStatusPageHtml() {
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<link rel='stylesheet' href='/style.css'></head><body><div class='card'><h2>System status</h2>";
  html += "<table><tr><th>Field</th><th>Value</th></tr>";
  html += "<tr><td>WiFi</td><td>" + String(WiFi.status() == WL_CONNECTED ? "CONNECTED" : "DISCONNECTED") + "</td></tr>";
  html += "<tr><td>Stats Error</td><td>" + lastStatsError + "</td></tr></table>";
  if (lastStatsRawJson.length()) html += "<pre>" + lastStatsRawJson + "</pre>";
  html += "<div class='links' style='text-align:center;margin-top:10px'><a href='/'>Home</a></div></div>";
  html += "<script>setTimeout(function(){ location.reload(); }, 2000);</script></body></html>";
  return html;
}

void setupRoutes() {
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/css", MAIN_CSS);
  });
  
  server.on("/login.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "application/javascript", LOGIN_JS);
  });

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", buildHomePageHtml());
  });

  server.on("/login-hint", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{\"uid\":\"" + lastLoginSuggestedUid + "\",\"user\":\"" + lastLoginSuggestedUser + "\"}";
    request->send(200, "application/json", json);
  });

  server.on("/register", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("user") || !request->hasParam("password")) {
      request->send(200, "text/html", buildRegisterPageHtml(""));
      return;
    }

    String u = request->getParam("user")->value();
    String p = request->getParam("password")->value();

    // SECURITY: Input Validation
    if(u.length() < 3 || u.length() > 20) {
        request->send(200, "text/html", buildRegisterPageHtml("Error: Username must be 3-20 chars"));
        return;
    }
    if(p.length() < 4) {
        request->send(200, "text/html", buildRegisterPageHtml("Error: Password too short"));
        return;
    }

    tempUsername = u;
    tempPassword = p;

    waitingForRFID = true;
    registrationStartTime = millis(); // Fixed: Overflow safe Logic
    setStatus(StatusCode::RegWait);

    String msg = "Registration started.\nUser: " + tempUsername + "\nTap card within " + String(REGISTER_TIMEOUT_MS / 1000) + "s.";
    updateIndicatorsForStatus();
    request->send(200, "text/html", buildRegisterPageHtml(msg));
  });

  server.on("/login", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("user") || !request->hasParam("password")) {
      request->send(200, "text/html", buildLoginFormHtml());
      return;
    }
    String user = request->getParam("user")->value();
    String pass = request->getParam("password")->value();
    lastStatsUser = user;

    String userJson;
    String errorText;
    int httpCode = -1;

    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      http.setTimeout(2000); // 2s Timeout
      String url = String(statsUrl) + "?user=" + user + "&password=" + pass;
      http.begin(url);
      httpCode = http.GET();
      if (httpCode > 0) userJson = http.getString();
      else errorText = "HTTP request failed: " + String(httpCode);
      http.end();
    } else {
      errorText = "No WiFi connection.";
    }

    lastStatsHttpCode = httpCode;
    lastStatsRawJson = userJson;
    lastStatsMillis = millis();

    String tableHtml;
    if (errorText.isEmpty() && httpCode == 200) {
      String vUser = extractJsonField(userJson, "user");
      String vAccess = extractJsonField(userJson, "access");
      String vRoom = extractJsonField(userJson, "roomID");
      tableHtml = "<table class='data-table'><tr><td>User</td><td>" + vUser + "</td></tr>";
      tableHtml += "<tr><td>Access</td><td>" + vAccess + "</td></tr>";
      tableHtml += "<tr><td>Room</td><td>" + vRoom + "</td></tr></table>";
      lastStatsError = "";
    } else {
      if (errorText.isEmpty()) errorText = "Backend Error " + String(httpCode);
      lastStatsError = errorText;
    }
    request->send(200, "text/html", buildLoginResultHtml(tableHtml, errorText));
  });

  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", buildStatusPageHtml());
  });

  server.onNotFound([](AsyncWebServerRequest *request) {
    request->redirect("/");
  });
}