// server.js
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

// POST /register
app.post("/register", async (req, res) => {
  const { user, password, uid, roomID } = req.body;
  if (!user || !password || !uid || !roomID) {
    return res.status(400).json({ error: "Missing required fields" });
  }

  // UID + room pair uniqueness
  const exists = users.find(
    (u) => String(u.uid) === String(uid) && String(u.roomID) === String(roomID)
  );
  if (exists) {
    return res
      .status(409)
      .json({ error: "User with this UID for room already exists" });
  }

  if (!worksheet) {
    return res.status(500).json({ error: "Worksheet not available" });
  }

  const rowIdx = worksheet.lastRow ? worksheet.lastRow.number + 1 : 2;
  const row = worksheet.getRow(rowIdx);
  row.getCell(1).value = user;
  row.getCell(2).value = password;
  row.getCell(3).value = uid;
  row.getCell(4).value = 0; // counter
  row.getCell(5).value = roomID;
  row.getCell(6).value = "TRUE"; // access flag
  row.commit();

  await workbook.xlsx.writeFile(filePath);
  await loadUsers();

  return res.json({ message: "Registration successful" });
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
