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
/* Global dark theme */
body {
  background: #0e0e10;
  font-family: Arial, Helvetica, sans-serif;
  color: #e6e6e6;
  margin: 0;
  padding: 40px;
}

/* Card container (used for all pages) */
.card {
  max-width: 600px;
  margin: auto;
  background: #1a1a1d;
  padding: 25px;
  border-radius: 10px;
  box-shadow: 0 0 15px rgba(128, 0, 255, 0.35);
}

/* Headings */
h1,
h2 {
  text-align: center;
  margin-top: 0;
  margin-bottom: 20px;
  color: #c084fc;
}

/* Links */
a {
  color: #c084fc;
  text-decoration: none;
}

a:hover {
  text-decoration: underline;
}

/* Menu buttons on home page */
a.button {
  display: block;
  margin: 0.5rem 0;
  padding: 10px 16px;
  border-radius: 6px;
  border: 1px solid #8b5cf6;
  background: #1a1a1d;
  color: #e6e6e6;
  text-decoration: none;
  text-align: center;
  transition: 0.2s;
}

a.button:hover {
  background: #2a1546;
}

/* Forms */
label {
  font-weight: bold;
  display: block;
  margin-bottom: 5px;
}

input[type=text],
input[type=password] {
  width: 100%;
  padding: 10px;
  background: #101014;
  color: #eee;
  border: 1px solid #3b2d4a;
  border-radius: 6px;
  margin-bottom: 15px;
  box-sizing: border-box;
  transition: 0.2s;
}

input[type=text]:focus,
input[type=password]:focus {
  outline: none;
  border-color: #b06fff;
  box-shadow: 0 0 6px #b06fff;
}

/* Buttons */
button {
  width: 100%;
  padding: 10px;
  background: #b06fff;
  border: none;
  color: #000;
  font-weight: bold;
  border-radius: 6px;
  cursor: pointer;
  transition: 0.2s;
}

button:hover {
  background: #d3a6ff;
}

/* General helpers */
.links {
  margin-top: 1rem;
  text-align: center;
}

.msg {
  margin-top: 0.5rem;
  white-space: pre-wrap;
}

/* Dashboard wrapper and tables */
.wrapper {
  max-width: 1200px;
  margin: auto;
  background: #1a1a1d;
  padding: 20px;
  border-radius: 10px;
  box-shadow: 0 0 15px rgba(128, 0, 255, 0.35);
}

.table-wrapper {
  overflow-x: auto;
  width: 100%;
  margin-top: 20px;
}

/* Data table */
.data-table {
  width: max-content;
  min-width: 100%;
  border-collapse: collapse;
  margin-bottom: 20px;
}

.data-table th {
  background: #8b5cf6;
  color: #ffffff;
  padding: 10px;
  white-space: nowrap;
  border-bottom: 2px solid #b694ff;
}

.data-table td {
  background: #151518;
  padding: 10px;
  border-bottom: 1px solid #2b2b2e;
  transition: 0.2s;
}

/* Hover effect */
.data-table tr:hover td {
  background: #2a1546;
  box-shadow: inset 0 0 10px rgba(176, 111, 255, 0.4);
}

.empty {
  padding: 10px;
  color: #777;
}

/* Error message */
.error {
  text-align: center;
  margin-bottom: 10px;
  color: #ff4c4c;
}

/* Preformatted JSON block */
pre {
  white-space: pre-wrap;
  font-size: 0.8rem;
  background: #101014;
  padding: 0.5rem;
  border-radius: 6px;
  border: 1px solid #2b2b2e;
}
)rawliteral";

static const char LOGIN_JS[] PROGMEM = R"awljs(
(function(){
  function updateHint(){
    fetch('/login-hint')
      .then(function(r){ if(!r.ok) return null; return r.json(); })
      .then(function(d){
        if(!d || !d.user) return;
        var inp = document.querySelector('input[name="user"]');
        if(inp && !inp.value){ inp.value = d.user; }
      })
      .catch(function(e){});
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

  if (errorText.length() > 0) {
    html += "<div class='error'>" + errorText + "</div>";
  } else {
    // Pretty-print JSON for login page also
    String prettyJson = lastStatsRawJson;
    prettyJson.replace("{", "{\n  ");
    prettyJson.replace(",", ",\n  ");
    prettyJson.replace("}", "\n}");

    html += "<div class='table-wrapper'>" + tableHtml + "</div>";
    html += "<p><b>Raw JSON:</b></p>";

    html += "<pre style='white-space:pre-wrap;"
            "overflow-wrap:break-word;"
            "overflow-x:auto;"
            "max-width:100%;"
            "padding:10px;"
            "background:#101014;"
            "border:1px solid #2b2b2e;"
            "border-radius:6px;'>"
            + prettyJson + "</pre>";
  }

  html += "<div class='links' style='text-align:center;margin-top:10px'>"
          "<a href='/login'>Back</a> | <a href='/'>Home</a>"
          "</div></div></body></html>";

  return html;
}

String buildStatusPageHtml() {
  String prettyJson = lastStatsRawJson;
  prettyJson.replace("{", "{\n  ");
  prettyJson.replace(",", ",\n  ");
  prettyJson.replace("}", "\n}");

  String wifiState = (WiFi.status() == WL_CONNECTED ? "CONNECTED" : "DISCONNECTED");
  String wifiColor = (wifiState == "CONNECTED" ? "#4CF04C" : "#FF4C4C");
  String httpColor = (lastStatsHttpCode == 200 ? "#4CF04C" : "#FF4C4C");

  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<link rel='stylesheet' href='/style.css'>";

  // Collapsible box JS
  html += "<script>";
  html += "function toggleJson(){"
          "var x=document.getElementById('jsonbox');"
          "if(x.style.display==='none'){x.style.display='block';}"
          "else{x.style.display='none';}"
          "}";
  html += "</script>";

  html += "</head><body><div class='card'>";

  html += "<h2>System Status</h2>";

  html += "<table class='data-table'>";
  html += "<tr><th>Field</th><th>Value</th></tr>";

  // WiFi row (green/red)
  html += "<tr><td>WiFi</td><td style='color:" + wifiColor + "'>";
  html += (wifiState == "CONNECTED" ? "[OK] " : "[ERR] ") + wifiState;
  html += "</td></tr>";

  // HTTP row (green/red)
  html += "<tr><td>Last HTTP Code</td><td style='color:" + httpColor + "'>";
  html += (lastStatsHttpCode == 200 ? "[OK] " : "[ERR] ") + String(lastStatsHttpCode);
  html += "</td></tr>";

  // Error row
  html += "<tr><td>Stats Error</td><td>";
  html += (lastStatsError.length() ? lastStatsError : "None");
  html += "</td></tr>";

  // Last user row
  html += "<tr><td>Last User</td><td>";
  html += (lastStatsUser.length() ? lastStatsUser : "None");
  html += "</td></tr>";

  html += "</table>";

  // Collapsible JSON
  if (lastStatsRawJson.length()) {
    html += "<h3>Last Backend Response</h3>";
    html += "<a href='#' onclick='toggleJson();return false;' "
            "style='color:#b06fff;font-weight:bold;'>[ Toggle JSON ]</a>";

    html += "<pre id='jsonbox' "
            "style='white-space:pre-wrap;"
            "overflow-wrap:break-word;"
            "overflow-x:auto;"
            "max-height:300px;"
            "padding:10px;"
            "background:#101014;"
            "border:1px solid #2b2b2e;"
            "border-radius:6px;"
            "margin-top:10px;"
            "display:none;'>"
            + prettyJson + "</pre>";
  }

  html += "<div class='links'><a href='/'>Home</a></div>";
  html += "</div>";

  // Auto refresh
  html += "<script>setTimeout(function(){ location.reload(); }, 2000);</script>";

  html += "</body></html>";

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
    if (u.length() < 3 || u.length() > 20) {
      request->send(200, "text/html", buildRegisterPageHtml("Error: Username must be 3-20 chars"));
      return;
    }
    if (p.length() < 4) {
      request->send(200, "text/html", buildRegisterPageHtml("Error: Password too short"));
      return;
    }

    tempUsername = u;
    tempPassword = p;

    waitingForRFID = true;
    registrationStartTime = millis();
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
      http.setTimeout(2000);  // 2s Timeout
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