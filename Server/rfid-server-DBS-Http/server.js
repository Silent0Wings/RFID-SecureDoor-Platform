// server.js
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
*/

const express = require("express");
const ExcelJS = require("exceljs");

const app = express();
const PORT = 5000;

app.use(express.urlencoded({ extended: false }));
app.use(express.json());

const workbook = new ExcelJS.Workbook();
const filePath = "users.xlsx";
let worksheet = null;
let users = [];

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

  // Clear the row cells (remove the user from Excel)
  const row = worksheet.getRow(target.rowNumber);
  for (let i = 1; i <= 6; i++) {
    row.getCell(i).value = null;
  }
  row.commit();

  // Save file
  await workbook.xlsx.writeFile(filePath);

  // Reload the in-memory users array
  await loadUsers();

  return true;
}

// ----------------------
// Routes
// ----------------------

// GET /stats?user=...&password=...
// Only returns that single user's data
app.get("/stats", (req, res) => {
  const { user, password } = req.query;

  if (!users.length) {
    return res.status(404).json({ error: "No user data available" });
  }
  if (!user || !password) {
    return res.status(400).json({ error: "Missing user or password" });
  }

  const entry = findUserByCredentials(user, password);
  if (!entry) {
    return res.status(401).json({ error: "Invalid credentials" });
  }

  return res.status(200).json(userPayload(entry));
});

// GET /user/:uid?roomID=101
app.get("/user/:uid", async (req, res) => {
  if (!users.length) {
    return res.status(404).json({ error: "No user data available" });
  }

  const { uid } = req.params;
  const { roomID } = req.query;

  const user = findUserByUid(uid);
  if (!user) {
    return res.status(404).json({ error: "UID not found" });
  }

  const allowedRooms = parseAllowedRooms(user.roomID);

  // room check
  if (roomID && !allowedRooms.includes(String(roomID))) {
    return res.status(403).json({
      ...userPayload(user, { allowedRooms }),
      access: false,
      error: `User ${user.user} does not have access to room ${roomID}`,
    });
  }

  const hasAccess = normalizeAccess(user.access);
  if (!hasAccess) {
    return res.status(403).json({
      ...userPayload(user, { allowedRooms }),
      access: false,
      error: "Access denied",
    });
  }

  // at this point: ACCESS GRANTED -> increment counter
  user.counter = Number(user.counter || 0) + 1;
  try {
    await saveCounters(); // write back to Excel
  } catch (err) {
    console.error("Failed to save counters:", err);
    // still return success; just log the error
  }

  return res.json(userPayload(user, { allowedRooms }));
});

app.get("/", (req, res) => {
  res.json({
    status: "backend ok",
    routes: ["/stats", "/user/:uid", "/register"],
  });
});

// GET /uid-name/:uid - lookup username for a given UID, no access check or counter increment
app.get("/uid-name/:uid", (req, res) => {
  if (!users.length) {
    return res.status(404).json({ error: "No user data available" });
  }

  const { uid } = req.params;
  const user = findUserByUid(uid);
  if (!user) {
    return res.status(404).json({ error: "UID not found" });
  }

  return res.json({ user: user.user, uid: user.uid });
});

// DELETE /user/:uid
app.delete("/user/:uid", async (req, res) => {
  if (!users.length) {
    return res.status(404).json({ error: "No user data available" });
  }

  const { uid } = req.params;
  const found = users.find((u) => String(u.uid) === String(uid));

  if (!found) {
    return res.status(404).json({ error: "UID not found" });
  }

  try {
    const ok = await deleteUserByUid(uid);
    if (!ok) {
      return res.status(500).json({ error: "Failed to delete user" });
    }

    return res.json({
      message: "User deleted",
      uid: uid,
      user: found.user,
    });
  } catch (err) {
    return res
      .status(500)
      .json({ error: "Delete operation failed", details: err.message });
  }
});

// DELETE A ROOM FROM A USER (SAFE AND UNIQUE ROUTE)
app.delete("/room/:uid/:roomID", async (req, res) => {
  if (!users.length) {
    return res.status(404).json({ error: "No user data available" });
  }

  const { uid, roomID } = req.params;
  const user = findUserByUid(uid);
  if (!user) {
    return res.status(404).json({ error: "UID not found" });
  }

  const currentRooms = parseAllowedRooms(user.roomID);
  const newRooms = currentRooms.filter((r) => r !== String(roomID));

  if (newRooms.length === currentRooms.length) {
    return res.status(400).json({ error: "Room not found for this user" });
  }

  if (newRooms.length === 0) {
    const ok = await deleteUserByUid(uid);
    if (!ok) {
      return res.status(500).json({ error: "Failed to delete user" });
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
    return res.status(400).json({ error: "Missing required fields" });
  }

  if (!worksheet) {
    return res.status(500).json({ error: "Worksheet not available" });
  }

  // Check if UID already exists
  const existing = users.find((u) => String(u.uid) === String(uid));

  // --------------------------------------------
  // CASE 1: UID already exists → append roomID
  // --------------------------------------------
  if (existing) {
    const currentRooms = parseAllowedRooms(existing.roomID);

    // Do not duplicate the room
    if (!currentRooms.includes(String(roomID))) {
      currentRooms.push(String(roomID));
    }

    const newRoomString = currentRooms.join("|");

    // Update Excel row
    const row = worksheet.getRow(existing.rowNumber);
    row.getCell(5).value = newRoomString;
    row.commit();

    await workbook.xlsx.writeFile(filePath);
    await loadUsers();

    return res.json({
      message: "Room appended to existing UID",
      uid: uid,
      rooms: newRoomString,
    });
  }

  // --------------------------------------------
  // CASE 2: UID does not exist → create new row
  // --------------------------------------------
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

  return res.json({ message: "Registration successful (new UID)" });
});

// Bootstrap
loadUsers()
  .then(() => {
    app.listen(PORT, () => {
      console.log(`Server running on http://localhost:${PORT}`);
    });
  })
  .catch((err) => {
    console.error("Failed to load Excel file:", err);
  });
