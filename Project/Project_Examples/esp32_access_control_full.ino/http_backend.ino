// http_backend.ino
// ==========================
// HTTP BACKEND FUNCTIONS
// ==========================

#include <WiFi.h>
#include <HTTPClient.h>

// Globals defined in main.ino
extern const char *registerUrl;
extern const char *accessBaseUrl;
extern String roomID;

extern String tempUsername;
extern String tempPassword;

extern String lastStatus;
extern String lastStatsError;
extern int lastStatsHttpCode;
extern String lastStatsRawJson;
extern String lastStatsUser;
extern unsigned long lastStatsMillis;

extern String lastLoginSuggestedUser;
extern String lastLoginSuggestedUid;

// Helpers implemented in main.ino
extern void resetRegistrationWindow();
extern String extractJsonField(const String &src, const char *key);
extern void showMsg(const String &l1, const String &l2, const String &l3, bool serial);

// ==========================
// REGISTRATION
// ==========================

// Registration: called when waitingForRFID == true
void tryRegisterRFID(const String &uid) {
  // no pending user data
  if (tempUsername.isEmpty() || tempPassword.isEmpty()) {
    Serial.println("REG: Missing username/password for registration.");
    lastStatus = "REG_ERR_NOPARAM";
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("REG: WiFi not connected.");
    lastStatus = "REG_WIFI_ERR";
    lastStatsError = "WiFi not connected";
    return;
  }

  HTTPClient http;
  http.begin(registerUrl);
  http.addHeader("Content-Type", "application/json");

  // minimal JSON body
  String body = "{";
  body += "\"user\":\"" + tempUsername + "\",";
  body += "\"password\":\"" + tempPassword + "\",";
  body += "\"uid\":\"" + uid + "\",";
  body += "\"roomID\":\"" + roomID + "\"";
  body += "}";

  Serial.print("REG: POST ");
  Serial.println(registerUrl);
  Serial.print("REG body: ");
  Serial.println(body);

  int code = http.POST(body);
  String resp = http.getString();
  http.end();

  lastStatsHttpCode = code;
  lastStatsRawJson = resp;
  lastStatsMillis = millis();
  lastStatsUser = tempUsername;

  if (code >= 200 && code < 300) {
    Serial.println("REG: OK from backend.");
    lastStatus = "REG_OK";
    lastStatsError = "";
  } else {
    Serial.print("REG: backend error code ");
    Serial.println(code);
    lastStatus = "REG_FAIL";
    lastStatsError = "REG HTTP " + String(code) + " body: " + resp;
  }

  // registration window is done
  resetRegistrationWindow();
}

// ==========================
// ACCESS CHECK
// ==========================

// Access check: called when waitingForRFID == false
void checkAccessRFID(const String &uid) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("ACCESS: WiFi not connected.");
    lastStatus = "ACCESS_WIFI_ERR";
    lastStatsError = "WiFi not connected";
    return;
  }

  HTTPClient http;
  String url = String(accessBaseUrl) + "/" + uid + "?roomID=" + roomID;
  Serial.print("ACCESS: GET ");
  Serial.println(url);

  http.begin(url);
  int code = http.GET();
  String resp = http.getString();
  http.end();

  lastStatsHttpCode = code;
  lastStatsRawJson = resp;
  lastStatsMillis = millis();
  lastStatsUser = uid;  // or keep last user if you map uid->user elsewhere

  if (code <= 0) {
    Serial.print("ACCESS: HTTP error ");
    Serial.println(code);
    lastStatus = "ACCESS_HTTP_ERR";
    lastStatsError = "ACCESS HTTP " + String(code);
    return;
  }

  if (code == 200) {
    // backend success, check "access" field
    String accessStr = extractJsonField(resp, "access");
    accessStr.toLowerCase();

    bool hasAccess =
      accessStr == "true" || accessStr == "yes" || accessStr == "1";

    if (hasAccess) {
      Serial.println("ACCESS: granted.");
      lastStatus = "ACCESS_OK";
      lastStatsError = "";
      // TODO: open door, turn green LED, etc
    } else {
      Serial.println("ACCESS: denied by backend payload.");
      lastStatus = "ACCESS_DENIED";
      lastStatsError = "access field is false";
      // TODO: red LED, buzzer, etc
    }
  } else if (code == 403) {
    Serial.println("ACCESS: 403 forbidden.");
    lastStatus = "ACCESS_FORBIDDEN";
    lastStatsError = "ACCESS 403: " + resp;
  } else if (code == 404) {
    lastStatus = "ACCESS_DENIED";
    lastStatsError = "UID not found";
    lastLoginSuggestedUser = "";
  } else {
    lastStatus = "ACCESS_FAIL";
    lastStatsError = "ACCESS HTTP " + String(code) + " body: " + resp;
  }
}

// ==========================
// UID -> USER LOOKUP
// ==========================
void lookupUserByUid(const String &uid) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = String("http://172.28.219.124:5000/uid-name/") + uid;
  Serial.print("LOOKUP: GET ");
  Serial.println(url);

  http.begin(url);
  int code = http.GET();
  if (code == 200) {
    String resp = http.getString();
    String uname = extractJsonField(resp, "user");
    if (uname.length()) {
      lastLoginSuggestedUser = uname;
      lastLoginSuggestedUid = uid;
      Serial.print("LOOKUP: got username ");
      Serial.println(uname);
    }
  }
  http.end();
}

// ==========================
// DELETE USER
// ==========================
void deleteUser(const String &uid) {
  if (WiFi.status() != WL_CONNECTED) {
    lastStatus = "DELETE_WIFI_ERR";
    showMsg("Delete error", "WiFi", "", true);
    return;
  }

  HTTPClient http;
  String url = String("http://172.28.219.124:5000/user/") + uid;

  http.begin(url);
  int code = http.sendRequest("DELETE");
  String resp = http.getString();
  http.end();

  if (code == 200) {
    lastStatus = "DELETE_OK";
    showMsg("Deleted:", uid, "", true);
  } else if (code == 404) {
    lastStatus = "DELETE_NOTFOUND";
    showMsg("Delete failed", "UID not found", "", true);
  } else {
    lastStatus = "DELETE_FAIL";
    showMsg("Delete error", "HTTP " + String(code), "", true);
  }
}

// ==========================
// DELETE ROOM FOR UID
// ==========================
void deleteRoomForUid(const String &uid, const String &room) {
  Serial.print("DELETE ROOM CALL uid=[");
  Serial.print(uid);
  Serial.print("] room=[");
  Serial.print(room);
  Serial.println("]");

  if (uid.isEmpty()) {
    lastStatus = "DELETE_FAIL";
    showMsg("Delete error", "No UID", "", true);
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    lastStatus = "DELETE_WIFI_ERR";
    showMsg("Delete error", "WiFi", "", true);
    return;
  }

  String url = "http://172.28.219.124:5000/room/" + uid + "/" + room;

  Serial.print("DELETE URL: ");
  Serial.println(url);

  HTTPClient http;
  http.begin(url);
  int code = http.sendRequest("DELETE");
  String resp = http.getString();
  http.end();

  Serial.print("DELETE HTTP code: ");
  Serial.println(code);
  Serial.print("DELETE resp: ");
  Serial.println(resp);

  if (code == 200) {
    lastStatus = "DELETE_OK";
    showMsg("Room removed", "UID: " + uid, "Room " + room, true);
  } else if (code == 400) {
    lastStatus = "DELETE_NOTFOUND";
    showMsg("Delete failed", "Room not found", "", true);
  } else if (code == 404) {
    lastStatus = "DELETE_NOTFOUND";
    showMsg("Delete failed", "UID not found", "", true);
  } else {
    lastStatus = "DELETE_FAIL";
    showMsg("Delete error", "HTTP " + String(code), "", true);
  }
}
