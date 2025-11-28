// web_ui.ino
// Web UI HTML builders and HTTP route setup

// Shared CSS and JS served as separate resources
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

// ---------------------------
// Home page HTML
// ---------------------------
String buildHomePageHtml() {
  String html;
  html =
    "<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
    "<link rel='stylesheet' href='/style.css'>"
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
    "<link rel='stylesheet' href='/style.css'>"
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
    "<link rel='stylesheet' href='/style.css'>"
    "<script src='/login.js' defer></script>"
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
    "</body></html>";
  return html;
}

String buildLoginResultHtml(const String &tableHtml, const String &errorText) {
  String html;
  html =
    "<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
    "<link rel='stylesheet' href='/style.css'>"
    "</head><body>"
    "<div class='card'>"
    "<h2>My access data</h2>";

  if (errorText.length() > 0) {
    html += "<div class='error'>" + errorText + "</div>";
  } else {
    html += "<div class='table-wrapper'>" + tableHtml + "</div>";
    html += "<h3>Backend call status</h3>";
    html += "<div class='table-wrapper'><table class='data-table'>";
    html += "<tr><td>HTTP code</td><td>" + String(lastStatsHttpCode) + "</td></tr>";
    html += "<tr><td>User</td><td>" + lastStatsUser + "</td></tr>";
    html += "<tr><td>Age (ms)</td><td>" + String(millis() - lastStatsMillis) + "</td></tr>";
    html += "</table></div>";
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
    "<link rel='stylesheet' href='/style.css'>"
    "</head><body>"
    "<div class='card'>"
    "<h2>System status</h2>"
    "<div class='table-wrapper'><table class='data-table'>"
    "<tr><th>Field</th><th>Value</th></tr>";

  html += "<tr><td>WiFi</td><td>" + String(WiFi.status() == WL_CONNECTED ? "CONNECTED" : "DISCONNECTED") + "</td></tr>";
  html += "<tr><td>Last RFID UID</td><td>" + lastUid + "</td></tr>";
  html += "<tr><td>Last RFID status</td><td>" + lastStatus + "</td></tr>";
  html += "<tr><td>Last RFID age (ms)</td><td>" + String(millis() - lastEventMillis) + "</td></tr>";
  html += "<tr><td>Last stats user</td><td>" + lastStatsUser + "</td></tr>";
  html += "<tr><td>Last stats HTTP</td><td>" + String(lastStatsHttpCode) + "</td></tr>";
  html += "<tr><td>Last stats error</td><td>" + (lastStatsError.length() ? lastStatsError : "none") + "</td></tr>";
  html += "</table></div>";

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
  // Static assets
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/css", MAIN_CSS);
  });

  server.on("/login.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "application/javascript", LOGIN_JS);
  });

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
    rfidTimeout = millis() + REGISTER_TIMEOUT_MS;
    setStatus(StatusCode::RegWait);

    String msg;
    msg = "Registration started.\n\n";
    msg += "User: " + tempUsername + "\n";
    msg += "Tap the new card on the reader within ";
    msg += String(REGISTER_TIMEOUT_MS / 1000);
    msg += " seconds.";

    updateIndicatorsForStatus();
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
        "<table class='data-table'>"
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
