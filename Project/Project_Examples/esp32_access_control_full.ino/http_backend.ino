#include <WiFi.h>
#include <HTTPClient.h>

extern StatusCode lastStatusCode;
extern String lastStatus;
void setStatus(StatusCode code);
extern String registerUrl;
extern String accessBaseUrl;
extern String roomID;
extern String tempUsername;
extern String tempPassword;
extern String lastStatsError;
extern int lastStatsHttpCode;
extern String lastStatsRawJson;
extern String lastStatsUser;
extern unsigned long lastStatsMillis;
extern String lastLoginSuggestedUser;
extern String lastLoginSuggestedUid;

extern void resetRegistrationWindow();
extern void showMsg(const String &l1, const String &l2, const String &l3, bool serial);

// ==========================
// UTILITY IMPLEMENTATION
// ==========================
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

// ==========================
// REGISTRATION
// ==========================
void tryRegisterRFID(const String &uid) {
  if (tempUsername.isEmpty() || tempPassword.isEmpty()) {
    setStatus(StatusCode::RegErrNoParam);
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    setStatus(StatusCode::RegWifiErr);
    lastStatsError = "WiFi not connected";
    return;
  }

  HTTPClient http;
  http.setTimeout(2000);  // 2s Timeout
  http.begin(registerUrl);
  http.addHeader("Content-Type", "application/json");

  String body = "{";
  body += "\"user\":\"" + tempUsername + "\",";
  body += "\"password\":\"" + tempPassword + "\",";
  body += "\"uid\":\"" + uid + "\",";
  body += "\"roomID\":\"" + roomID + "\"";
  body += "}";

  Serial.print("REG: POST ");
  Serial.println(registerUrl);
  // Security: Do not log body containing password in production
  Serial.println("REG: Sending data...");

  int code = http.POST(body);
  String resp = http.getString();
  http.end();

  lastStatsHttpCode = code;
  lastStatsRawJson = resp;
  lastStatsMillis = millis();
  lastStatsUser = tempUsername;

  if (code >= 200 && code < 300) {
    setStatus(StatusCode::RegOk);
    lastStatsError = "";
  } else {
    setStatus(StatusCode::RegFail);
    lastStatsError = "REG HTTP " + String(code) + " body: " + resp;
  }
  resetRegistrationWindow();
}

// ==========================
// ACCESS CHECK
// ==========================
void checkAccessRFID(const String &uid) {
  if (WiFi.status() != WL_CONNECTED) {
    setStatus(StatusCode::AccessWifiErr);
    lastStatsError = "WiFi not connected";
    return;
  }

  HTTPClient http;
  http.setTimeout(2000);  // 2s Timeout
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
  lastStatsUser = uid;

  if (code <= 0) {
    setStatus(StatusCode::AccessHttpErr);
    lastStatsError = "ACCESS HTTP Error/Timeout";
    return;
  }

  if (code == 200) {
    String accessStr = extractJsonField(resp, "access");
    accessStr.toLowerCase();
    bool hasAccess = (accessStr == "true" || accessStr == "yes" || accessStr == "1");

    if (hasAccess) {
      setStatus(StatusCode::AccessOk);
      lastStatsError = "";
    } else {
      setStatus(StatusCode::AccessDenied);
      lastStatsError = "access field is false";
    }
  } else if (code == 403) {
    setStatus(StatusCode::AccessForbidden);
    lastStatsError = "ACCESS 403: " + resp;
  } else if (code == 404) {
    setStatus(StatusCode::AccessDenied);
    lastStatsError = "UID not found";
    lastLoginSuggestedUser = "";
  } else {
    setStatus(StatusCode::AccessFail);
    lastStatsError = "ACCESS HTTP " + String(code);
  }
}

// ==========================
// UID -> USER LOOKUP
// ==========================
void lookupUserByUid(const String &uid) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.setTimeout(2000);
  // Note: Hardcoded IP in original lookup URL replaced with logic?
  // Ideally, use a base URL, but for minimal change assuming same host:
  // Using extern accessBaseUrl base for consistency:
  String url = accessBaseUrl;
  // The original code had /uid-name/ endpoint. Assuming it exists on same host.
  // We'll reconstruct using registerUrl logic to find base.
  int slash = registerUrl.lastIndexOf('/');
  String base = registerUrl.substring(0, slash);
  url = base + "/uid-name/" + uid;

  http.begin(url);
  int code = http.GET();
  if (code == 200) {
    String resp = http.getString();
    String uname = extractJsonField(resp, "user");
    if (uname.length()) {
      lastLoginSuggestedUser = uname;
      lastLoginSuggestedUid = uid;
    }
  }
  http.end();
}

// ==========================
// DELETE USER
// ==========================
void deleteUser(const String &uid) {
  if (WiFi.status() != WL_CONNECTED) {
    setStatus(StatusCode::DeleteWifiErr);
    showMsg("Delete error", "WiFi", "", true);
    return;
  }

  HTTPClient http;
  http.setTimeout(2000);

  // Reconstruct Base URL
  int slash = registerUrl.lastIndexOf('/');
  String base = registerUrl.substring(0, slash);
  String url = base + "/user/" + uid;

  http.begin(url);
  int code = http.sendRequest("DELETE");
  http.end();

  if (code == 200) {
    setStatus(StatusCode::DeleteOk);
    showMsg("Deleted:", uid, "", true);
  } else if (code == 404) {
    setStatus(StatusCode::DeleteNotFound);
    showMsg("Delete failed", "UID not found", "", true);
  } else {
    setStatus(StatusCode::DeleteFail);
    showMsg("Delete error", "HTTP " + String(code), "", true);
  }
}

// ==========================
// DELETE ROOM FOR UID
// ==========================
void deleteRoomForUid(const String &uid, const String &room) {
  if (uid.isEmpty()) {
    setStatus(StatusCode::DeleteFail);
    showMsg("Delete error", "No UID", "", true);
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    setStatus(StatusCode::DeleteWifiErr);
    showMsg("Delete error", "WiFi", "", true);
    return;
  }

  int slash = registerUrl.lastIndexOf('/');
  String base = registerUrl.substring(0, slash);
  String url = base + "/room/" + uid + "/" + room;

  HTTPClient http;
  http.setTimeout(2000);
  http.begin(url);
  int code = http.sendRequest("DELETE");
  http.end();

  if (code == 200) {
    setStatus(StatusCode::DeleteOk);
    showMsg("Room removed", "UID: " + uid, "Room " + room, true);
  } else if (code == 400 || code == 404) {
    setStatus(StatusCode::DeleteNotFound);
    showMsg("Delete failed", "Not found", "", true);
  } else {
    setStatus(StatusCode::DeleteFail);
    showMsg("Delete error", "HTTP " + String(code), "", true);
  }
}