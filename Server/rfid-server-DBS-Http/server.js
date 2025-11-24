/*
  server.js - Excel-backed access control backend

  Features:
  - Loads user records from users.xlsx into memory on startup.
  - Normalizes access flags and parses multi-room permissions from roomID (split by "|").
  - Provides helpers to find users by username/password or by UID.

  HTTP routes:
  - GET /stats
    - Input: query user, password
    - Output: JSON for that single user (no full table) or error codes for invalid/missing credentials.

  - GET /user/:uid?roomID=...
    - Checks if the UID exists and if the requested room is allowed.
    - Validates access flag.
    - On successful access, increments the user's counter and saves it back to users.xlsx.
    - Returns a JSON payload with user info and allowedRooms.

  - POST /register
    - Input: JSON body with user, password, uid, roomID.
    - Enforces UID + roomID uniqueness.
    - Appends a new row to users.xlsx with default counter = 0 and access = TRUE.
    - Reloads users into memory and returns a JSON success message.

  - GET /
    - Simple health/status endpoint that reports backend status and available routes.

  Persistence:
  - saveCounters() writes updated counter values back to the Excel worksheet.

  Logging (logs.xlsx):
  - Every HTTP request is appended to logs.xlsx.
  - Columns: Timestamp, Protocol, Method, Route, Status, Result, User, UID, RoomID, Message, IP.
  - Logs what protocol was used, what action was done, what status was returned, plus user/UID/room info when available.
*/

const express = require("express");
const ExcelJS = require("exceljs");

const app = express();
const PORT = 5000;

app.use(express.urlencoded({ extended: false }));
app.use(express.json());

// ----------------------
// Users workbook (users.xlsx)
// ----------------------
const workbook = new ExcelJS.Workbook();
const filePath = "users.xlsx";
let worksheet = null;
let users = [];

// ----------------------
// Logging workbook (logs.xlsx)
// ----------------------
const logWorkbook = new ExcelJS.Workbook();
const logFilePath = "logs.xlsx";
let logSheet = null;

async function initLogSheet() {
  try {
    await logWorkbook.xlsx.readFile(logFilePath);
    logSheet = logWorkbook.worksheets[0];
    if (!logSheet) {
      logSheet = logWorkbook.addWorksheet("Logs");
      logSheet.addRow([
        "Timestamp",
        "Protocol",
        "Method",
        "Route",
        "Status",
        "Result",
        "User",
        "UID",
        "RoomID",
        "Message",
        "IP",
      ]);
      await logWorkbook.xlsx.writeFile(logFilePath);
    }
  } catch (err) {
    logSheet = logWorkbook.addWorksheet("Logs");
    logSheet.addRow([
      "Timestamp",
      "Protocol",
      "Method",
      "Route",
      "Status",
      "Result",
      "User",
      "UID",
      "RoomID",
      "Message",
      "IP",
    ]);
    await logWorkbook.xlsx.writeFile(logFilePath);
  }
}

async function appendLog(entry) {
  if (!logSheet) return;

  const row = logSheet.addRow([
    entry.timestamp,
    entry.protocol,
    entry.method,
    entry.route,
    entry.status,
    entry.result,
    entry.user || "",
    entry.uid || "",
    entry.roomID || "",
    entry.message || "",
    entry.ip || "",
  ]);
  row.commit();

  try {
    await logWorkbook.xlsx.writeFile(logFilePath);
  } catch (err) {
    console.error("Failed to write logs.xlsx:", err);
  }
}

// ----------------------
// Data helpers
// ----------------------
function rowToUser(row, rowNumber) {
  const c = (i) => row.getCell(i).value;
  return {
    user: c(1),
    password: c(2),
    uid: c(3),
    counter: c(4) || 0,
    roomID: c(5),
    access: c(6),
    rowNumber,
  };
}

function normalizeAccess(access) {
  const s = String(access).toLowerCase();
  return s === "true" || s === "yes" || s === "1";
}

function parseAllowedRooms(roomID) {
  if (!roomID) return [];
  return String(roomID)
    .split("|")
    .map((r) => r.trim())
    .filter(Boolean);
}

function userPayload(user, extra = {}) {
  return {
    user: user.user,
    password: user.password,
    uid: user.uid,
    counter: user.counter,
    roomID: user.roomID,
    access: normalizeAccess(user.access),
    ...extra,
  };
}

function findUserByCredentials(user, password) {
  const u = String(user);
  const p = String(password);
  return (
    users.find((x) => String(x.user) === u && String(x.password) === p) || null
  );
}

function findUserByUid(uid) {
  const target = String(uid);
  return users.find((u) => String(u.uid) === target) || null;
}

// ----------------------
// Excel load/save
// ----------------------
async function loadUsers() {
  users = [];
  worksheet = null;

  try {
    await workbook.xlsx.readFile(filePath);
    worksheet = workbook.worksheets[0];
    if (!worksheet) {
      console.warn("Excel has no worksheets.");
      return;
    }

    worksheet.eachRow((row, rowNumber) => {
      if (rowNumber === 1) return; // header
      const hasAny = [1, 2, 3, 4, 5, 6].some((i) => row.getCell(i).value);
      if (!hasAny) return;
      users.push(rowToUser(row, rowNumber));
    });

    console.log(`Loaded ${users.length} users`);
  } catch (err) {
    console.error("Error loading Excel file:", err);
  }
}

async function saveCounters() {
  if (!worksheet) return;
  for (const u of users) {
    const row = worksheet.getRow(u.rowNumber);
    row.getCell(4).value = u.counter;
    row.commit();
  }
  await workbook.xlsx.writeFile(filePath);
}

async function deleteUserByUid(uid) {
  if (!worksheet) return false;

  const target = users.find((u) => String(u.uid) === String(uid));
  if (!target) return false;

  const row = worksheet.getRow(target.rowNumber);
  for (let i = 1; i <= 6; i++) {
    row.getCell(i).value = null;
  }
  row.commit();

  await workbook.xlsx.writeFile(filePath);
  await loadUsers();

  return true;
}

// ----------------------
// Logging middleware
// ----------------------
app.use((req, res, next) => {
  const ip =
    req.headers["x-forwarded-for"] ||
    req.ip ||
    (req.socket && req.socket.remoteAddress) ||
    "";

  res.locals._log = {
    protocol: (req.protocol || "http").toUpperCase(),
    method: req.method,
    route: req.originalUrl,
    user: (req.body && req.body.user) || req.query.user || "",
    uid:
      (req.params && req.params.uid) ||
      (req.body && req.body.uid) ||
      req.query.uid ||
      "",
    roomID:
      (req.params && req.params.roomID) ||
      (req.body && req.body.roomID) ||
      req.query.roomID ||
      "",
    ip: ip,
    message: "",
  };

  res.on("finish", () => {
    const base = res.locals._log || {};
    const status = res.statusCode;
    const result = status >= 400 ? "ERROR" : "OK";

    appendLog({
      timestamp: new Date().toISOString(),
      protocol: base.protocol || (req.protocol || "http").toUpperCase(),
      method: base.method || req.method,
      route: base.route || req.originalUrl,
      status,
      result,
      user: base.user,
      uid: base.uid,
      roomID: base.roomID,
      message: base.message,
      ip: base.ip,
    }).catch((err) => {
      console.error("Failed to append log entry:", err);
    });
  });

  next();
});

// ----------------------
// Routes
// ----------------------

// GET /stats?user=...&password=...
// Only returns that single user's data
app.get("/stats", (req, res) => {
  const { user, password } = req.query;

  if (!users.length) {
    if (res.locals._log) {
      res.locals._log.message = "No user data available for /stats";
    }
    return res.status(404).json({ error: "No user data available" });
  }
  if (!user || !password) {
    if (res.locals._log) {
      res.locals._log.message = "Missing user or password in /stats";
    }
    return res.status(400).json({ error: "Missing user or password" });
  }

  const entry = findUserByCredentials(user, password);
  if (!entry) {
    if (res.locals._log) {
      res.locals._log.message = "Invalid credentials in /stats";
    }
    return res.status(401).json({ error: "Invalid credentials" });
  }

  if (res.locals._log) {
    res.locals._log.user = entry.user;
    res.locals._log.uid = entry.uid;
    res.locals._log.message = "Stats returned for user";
  }

  return res.status(200).json(userPayload(entry));
});

// GET /user/:uid?roomID=101
app.get("/user/:uid", async (req, res) => {
  if (!users.length) {
    if (res.locals._log) {
      res.locals._log.message = "No user data available for /user";
    }
    return res.status(404).json({ error: "No user data available" });
  }

  const { uid } = req.params;
  const { roomID } = req.query;

  const user = findUserByUid(uid);
  if (!user) {
    if (res.locals._log) {
      res.locals._log.message = `UID ${uid} not found in /user`;
    }
    return res.status(404).json({ error: "UID not found" });
  }

  const allowedRooms = parseAllowedRooms(user.roomID);

  if (roomID && !allowedRooms.includes(String(roomID))) {
    if (res.locals._log) {
      res.locals._log.user = user.user;
      res.locals._log.uid = user.uid;
      res.locals._log.roomID = roomID;
      res.locals._log.message = `Access denied for room ${roomID}`;
    }
    return res.status(403).json({
      ...userPayload(user, { allowedRooms }),
      access: false,
      error: `User ${user.user} does not have access to room ${roomID}`,
    });
  }

  const hasAccess = normalizeAccess(user.access);
  if (!hasAccess) {
    if (res.locals._log) {
      res.locals._log.user = user.user;
      res.locals._log.uid = user.uid;
      res.locals._log.roomID = roomID || "";
      res.locals._log.message = "Access flag is false";
    }
    return res.status(403).json({
      ...userPayload(user, { allowedRooms }),
      access: false,
      error: "Access denied",
    });
  }

  user.counter = Number(user.counter || 0) + 1;
  try {
    await saveCounters();
  } catch (err) {
    console.error("Failed to save counters:", err);
  }

  if (res.locals._log) {
    res.locals._log.user = user.user;
    res.locals._log.uid = user.uid;
    res.locals._log.roomID = roomID || "";
    res.locals._log.message = "Access granted and counter incremented";
  }

  return res.json(userPayload(user, { allowedRooms }));
});

// GET /
app.get("/", (req, res) => {
  if (res.locals._log) {
    res.locals._log.message = "Health check";
  }
  res.json({
    status: "backend ok",
    routes: ["/stats", "/user/:uid", "/register", "/uid-name/:uid"],
  });
});

// GET /uid-name/:uid - lookup username for a given UID, no access check or counter increment
app.get("/uid-name/:uid", (req, res) => {
  if (!users.length) {
    if (res.locals._log) {
      res.locals._log.message = "No user data available for /uid-name";
    }
    return res.status(404).json({ error: "No user data available" });
  }

  const { uid } = req.params;
  const user = findUserByUid(uid);
  if (!user) {
    if (res.locals._log) {
      res.locals._log.message = `UID ${uid} not found in /uid-name`;
    }
    return res.status(404).json({ error: "UID not found" });
  }

  if (res.locals._log) {
    res.locals._log.user = user.user;
    res.locals._log.uid = user.uid;
    res.locals._log.message = "UID to username lookup success";
  }

  return res.json({ user: user.user, uid: user.uid });
});

// DELETE /user/:uid
app.delete("/user/:uid", async (req, res) => {
  if (!users.length) {
    if (res.locals._log) {
      res.locals._log.message = "No user data available for DELETE /user";
    }
    return res.status(404).json({ error: "No user data available" });
  }

  const { uid } = req.params;
  const found = users.find((u) => String(u.uid) === String(uid));

  if (!found) {
    if (res.locals._log) {
      res.locals._log.message = `UID ${uid} not found in DELETE /user`;
    }
    return res.status(404).json({ error: "UID not found" });
  }

  try {
    const ok = await deleteUserByUid(uid);
    if (!ok) {
      if (res.locals._log) {
        res.locals._log.message = "Failed to delete user from Excel";
      }
      return res.status(500).json({ error: "Failed to delete user" });
    }

    if (res.locals._log) {
      res.locals._log.user = found.user;
      res.locals._log.uid = uid;
      res.locals._log.message = "User deleted";
    }

    return res.json({
      message: "User deleted",
      uid: uid,
      user: found.user,
    });
  } catch (err) {
    if (res.locals._log) {
      res.locals._log.message = "Delete operation threw error";
    }
    return res
      .status(500)
      .json({ error: "Delete operation failed", details: err.message });
  }
});

// DELETE /room/:uid/:roomID
app.delete("/room/:uid/:roomID", async (req, res) => {
  if (!users.length) {
    if (res.locals._log) {
      res.locals._log.message = "No user data available for DELETE /room";
    }
    return res.status(404).json({ error: "No user data available" });
  }

  const { uid, roomID } = req.params;
  const user = findUserByUid(uid);
  if (!user) {
    if (res.locals._log) {
      res.locals._log.message = `UID ${uid} not found in DELETE /room`;
    }
    return res.status(404).json({ error: "UID not found" });
  }

  const currentRooms = parseAllowedRooms(user.roomID);
  const newRooms = currentRooms.filter((r) => r !== String(roomID));

  if (newRooms.length === currentRooms.length) {
    if (res.locals._log) {
      res.locals._log.user = user.user;
      res.locals._log.uid = user.uid;
      res.locals._log.roomID = roomID;
      res.locals._log.message = "Room not found for this user";
    }
    return res.status(400).json({ error: "Room not found for this user" });
  }

  if (newRooms.length === 0) {
    const ok = await deleteUserByUid(uid);
    if (!ok) {
      if (res.locals._log) {
        res.locals._log.message =
          "Failed to delete user after last room removed";
      }
      return res.status(500).json({ error: "Failed to delete user" });
    }
    if (res.locals._log) {
      res.locals._log.user = user.user;
      res.locals._log.uid = uid;
      res.locals._log.roomID = roomID;
      res.locals._log.message = "User deleted because no rooms remain";
    }
    return res.json({
      message: "User deleted because no rooms remain",
      uid,
      removedRoom: roomID,
    });
  }

  const newRoomString = newRooms.join("|");

  const row = worksheet.getRow(user.rowNumber);
  row.getCell(5).value = newRoomString;
  row.commit();
  await workbook.xlsx.writeFile(filePath);
  await loadUsers();

  if (res.locals._log) {
    res.locals._log.user = user.user;
    res.locals._log.uid = uid;
    res.locals._log.roomID = roomID;
    res.locals._log.message = "Room removed from user";
  }

  return res.json({
    message: "Room removed",
    uid,
    removedRoom: roomID,
    rooms: newRoomString,
  });
});

// POST /register
app.post("/register", async (req, res) => {
  const { user, password, uid, roomID } = req.body;
  if (!user || !password || !uid || !roomID) {
    if (res.locals._log) {
      res.locals._log.message = "Missing required fields in /register";
    }
    return res.status(400).json({ error: "Missing required fields" });
  }

  if (!worksheet) {
    if (res.locals._log) {
      res.locals._log.message = "Worksheet not available in /register";
    }
    return res.status(500).json({ error: "Worksheet not available" });
  }

  const existing = users.find((u) => String(u.uid) === String(uid));

  // CASE 1: UID already exists -> append roomID
  if (existing) {
    const currentRooms = parseAllowedRooms(existing.roomID);

    if (!currentRooms.includes(String(roomID))) {
      currentRooms.push(String(roomID));
    }

    const newRoomString = currentRooms.join("|");

    const row = worksheet.getRow(existing.rowNumber);
    row.getCell(5).value = newRoomString;
    row.commit();

    await workbook.xlsx.writeFile(filePath);
    await loadUsers();

    if (res.locals._log) {
      res.locals._log.user = existing.user;
      res.locals._log.uid = existing.uid;
      res.locals._log.roomID = roomID;
      res.locals._log.message = "Room appended to existing UID";
    }

    return res.json({
      message: "Room appended to existing UID",
      uid: uid,
      rooms: newRoomString,
    });
  }

  // CASE 2: UID does not exist -> create new row
  const rowIdx = worksheet.lastRow ? worksheet.lastRow.number + 1 : 2;
  const row = worksheet.getRow(rowIdx);
  row.getCell(1).value = user;
  row.getCell(2).value = password;
  row.getCell(3).value = uid;
  row.getCell(4).value = 0; // counter
  row.getCell(5).value = roomID;
  row.getCell(6).value = "TRUE";
  row.commit();

  await workbook.xlsx.writeFile(filePath);
  await loadUsers();

  if (res.locals._log) {
    res.locals._log.user = user;
    res.locals._log.uid = uid;
    res.locals._log.roomID = roomID;
    res.locals._log.message = "Registration successful for new UID";
  }

  return res.json({ message: "Registration successful (new UID)" });
});

// ----------------------
// Bootstrap
// ----------------------
Promise.all([loadUsers(), initLogSheet()])
  .then(() => {
    app.listen(PORT, () => {
      console.log(`Server running on http://localhost:${PORT}`);
    });
  })
  .catch((err) => {
    console.error("Failed to initialize:", err);
  });
