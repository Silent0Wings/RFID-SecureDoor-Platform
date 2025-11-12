// All web handlers. Robust HTML, comments, visible logout, admin panel

void handleLogin() {
  String msg;
  bool lockedOutNow = (currentState == LOCKED_OUT);
  if (server.hasArg("DISCONNECT")) {
    server.sendHeader("Set-Cookie", "ESPSESSIONID=0");
    setStateIdle();
    currentState = IDLE;
    msg = "<span class='success'>Logged out.</span>";
  }
  if (server.hasArg("USERNAME") && server.hasArg("PASSWORD") && !lockedOutNow) {
    if (checkAuth(server.arg("USERNAME"), server.arg("PASSWORD"))) {
      currentState = AUTH_SUCCESS;
      setStateAuthSuccess();
      server.sendHeader("Location", "/");
      server.sendHeader("Set-Cookie", "ESPSESSIONID=1");
      server.send(301);
      return;
    } else {
      currentState = AUTH_FAIL;
      setStateAuthFail();
      msg = "<div class='error'>Wrong username/password! Try again.</div>";
    }
  }
  String lockedMsg = lockedOutNow ? "<div class='error'>Too many failed attempts. Locked out for 2 minutes.</div>" : "";
  String content = "<!-- SOEN422 Lab 3 Login Page -->";
  content += "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
  content += "<style>body{font-family:Arial;max-width:400px;margin:auto;}input[type='text'],input[type='password']{width:90%;padding:8px;margin:8px auto;}input[type='submit']{padding:8px 16px;margin:8px 0;}.error{color:red;}.success{color:green;}</style></head>";
  content += "<body><h2>Access Control Login</h2>";
  content += "<form action='/login' method='POST'>";
  content += "Username: <input type='text' name='USERNAME' placeholder='username'><br>";
  content += "Password: <input type='password' name='PASSWORD' placeholder='password'><br>";
  content += "<input type='submit' value='Login'></form>";
  content += lockedMsg + msg;
  content += "<hr><a href='/admin'>Admin Panel</a><br></body></html>";
  server.send(200, "text/html", content);
}

void handleRoot() {
  if (currentState != AUTH_SUCCESS) {
    server.sendHeader("Location", "/login");
    server.send(301);
    return;
  }
  String content = "<!-- SOEN422 Lab 3 Root Page -->";
  content += "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
  content += "<style>body{font-family:Arial;}input[type='submit']{padding:8px 16px;color:white;background:red;}</style></head><body>";
  content += "<h2>Access Granted!</h2><p><span class='success'>The lock has been released.</span></p>";
  content += "<form action='/login' method='GET'><input type='hidden' name='DISCONNECT' value='YES'><input type='submit' value='Logout'></form>";
  content += "<hr><a href='/admin'>Go to Admin Panel</a></body></html>";
  server.send(200, "text/html", content);
}

void handleNotFound() {
  String content = "<!-- SOEN422 Lab 3 404 Page -->";
  content += "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
  content += "<style>body{font-family:Arial;}.error{color:red;}</style></head>";
  content += "<body><h2 class='error'>404: Page Not Found</h2>";
  content += "<a href='/'>Go Home</a><br></body></html>";
  server.send(404, "text/html", content);
}

// BONUS: admin credentials change panel
void handleAdmin() {
  String msg;
  if (server.hasArg("NEWUSER") && server.hasArg("NEWPASS")) {
    String nuser = server.arg("NEWUSER");
    String npass = server.arg("NEWPASS");
    if (nuser.length() > 0 && npass.length() > 0) {
      nuser.toCharArray(storedUsername, 16);
      npass.toCharArray(storedPassword, 16);
      msg = "<span class='success'>Credentials updated!</span>";
    } else {
      msg = "<div class='error'>Username and password cannot be empty.</div>";
    }
  }
  String content = "<!-- SOEN422 Lab 3 Admin Panel -->";
  content += "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'><style>body{font-family:Arial;max-width:400px;margin:auto;}input[type='text'],input[type='password']{width:90%;padding:8px;}.success{color:green;}.error{color:red;}</style></head>";
  content += "<body><h2>Admin Panel</h2><form method='POST'>";
  content += "New Username: <input name='NEWUSER'><br>";
  content += "New Password: <input type='password' name='NEWPASS'><br>";
  content += "<input type='submit' value='Update Credentials'>";
  content += "</form>" + msg;
  content += "<hr><a href='/'>Home</a></body></html>";
  server.send(200, "text/html", content);
}
