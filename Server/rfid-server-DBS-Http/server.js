const express = require("express");
const bodyParser = require("body-parser");
const ExcelJS = require("exceljs");

const app = express();
const PORT = 5000;

app.use(bodyParser.urlencoded({ extended: false }));
app.use(bodyParser.json());

const workbook = new ExcelJS.Workbook();
const filePath = "users.xlsx";
let worksheet;
let users = [];

// Robust Excel loading function
async function loadUsers() {
  try {
    await workbook.xlsx.readFile(filePath);

    if (workbook.worksheets.length === 0) {
      console.warn("Excel file has no worksheets.");
      users = [];
      worksheet = null;
      return;
    }
    worksheet = workbook.worksheets[0];
    users = [];

    let hasData = false;
    worksheet.eachRow((row, rowNumber) => {
      if (rowNumber === 1) return; // skip header row

      if (
        !row.getCell(1).value &&
        !row.getCell(2).value &&
        !row.getCell(3).value &&
        !row.getCell(4).value &&
        !row.getCell(5).value &&
        !row.getCell(6).value
      ) {
        return;
      }
      hasData = true;

      const userObj = {
        user: row.getCell(1).value,
        password: row.getCell(2).value,
        uid: row.getCell(3).value,
        counter: row.getCell(4).value || 0,
        roomID: row.getCell(5).value, // Return as-is
        access: row.getCell(6).value,
        rowNumber,
      };
      users.push(userObj);
    });

    if (!hasData) {
      console.warn("Excel file loaded but has no user records.");
    }

    console.log("Loaded users:", JSON.stringify(users, null, 2));
  } catch (err) {
    console.error("Error loading Excel file:", err);
    users = [];
    worksheet = null;
  }
}

async function saveUsers() {
  if (!worksheet) {
    console.warn("No worksheet to save.");
    return;
  }
  users.forEach((user) => {
    const row = worksheet.getRow(user.rowNumber);
    row.getCell(4).value = user.counter;
    row.commit();
  });
  await workbook.xlsx.writeFile(filePath);
}

// GET /stats?user=...&password=...
// Returns table: user password uid counter roomID access
app.get("/stats", (req, res) => {
  const { user, password } = req.query;

  if (!users.length) {
    return res.status(404).type("text/plain").send("No user data available");
  }

  // simple credential check: require any row with same user+password
  const ok = users.some(
    (u) =>
      String(u.user) === String(user) && String(u.password) === String(password)
  );
  if (!ok) {
    return res.status(401).type("text/plain").send("Invalid credentials");
  }

  let out = "user\tpassword\tuid\tcounter\troomID\taccess\n";
  for (const u of users) {
    out += `${u.user}\t${u.password}\t${u.uid}\t${u.counter}\t${u.roomID}\t${u.access}\n`;
  }
  res.type("text/plain").send(out);
});

app.get("/user/:uid", (req, res) => {
  if (!users.length) {
    return res.status(404).json({ error: "No user data available" });
  }
  const uid = req.params.uid;
  const requestedRoomID = req.query.roomID;
  const user = users.find((u) => u.uid == uid);

  if (!user) {
    return res.status(404).json({ error: "UID not found" });
  }

  // Convert roomID to array by splitting on comma
  let allowedRooms = [];
  if (user.roomID) {
    allowedRooms = String(user.roomID)
      .split("|")
      .map((r) => r.trim())
      .filter((r) => r.length > 0); // Remove empty strings
  }

  console.log(
    `User: ${user.user}, Raw roomID: ${user.roomID}, Allowed rooms array:`,
    allowedRooms,
    `Requested: ${requestedRoomID}`
  );

  // If roomID is provided in query, check if it's in the allowed rooms array
  if (requestedRoomID) {
    if (!allowedRooms.includes(String(requestedRoomID))) {
      return res.status(403).json({
        error: `User ${user.user} does not have access to room ${requestedRoomID}`,
        user: user.user,
        uid: user.uid,
        roomID: user.roomID,
        allowedRoomsArray: allowedRooms,
        access: false,
      });
    }
  }

  // Check access flag
  const accessStr = String(user.access).toLowerCase();
  const hasAccess =
    accessStr === "true" || accessStr === "yes" || accessStr === "1";

  if (!hasAccess) {
    return res.status(403).json({
      user: user.user,
      uid: user.uid,
      roomID: user.roomID,
      allowedRoomsArray: allowedRooms,
      access: false,
      error: "Access denied",
    });
  }

  // User has access to this room
  const {
    user: username,
    password,
    uid: userUid,
    counter,
    roomID,
    access,
  } = user;
  res.json({
    user: username,
    password,
    uid: userUid,
    counter,
    roomID, // Return as-is, original format
    allowedRoomsArray: allowedRooms, // Return as array for clarity
    access: hasAccess,
  });
});

app.post("/register", async (req, res) => {
  const { user, password, uid, roomID } = req.body;
  if (!user || !password || !uid || !roomID) {
    return res.status(400).send("Missing required fields");
  }

  // Check for duplicate UID
  if (users.find((u) => u.uid === uid && u.roomID === roomID)) {
    return res.status(409).send("User with this UID for room already exists");
  }

  // Prepare Excel row addition
  const accessValue = "TRUE"; // or set based on admin, form, etc.
  const counterValue = 0; // new user

  // Get index for new row (after current last)
  const rowIdx = worksheet.lastRow.number + 1;
  let newRow = worksheet.getRow(rowIdx);
  newRow.getCell(1).value = user;
  newRow.getCell(2).value = password;
  newRow.getCell(3).value = uid;
  newRow.getCell(4).value = counterValue;
  newRow.getCell(5).value = roomID; // comma-separated rooms as string
  newRow.getCell(6).value = accessValue;
  newRow.commit();

  // Save workbook and reload users in memory
  await workbook.xlsx.writeFile(filePath);
  await loadUsers();
  console.log(`Registered new user: ${user}, UID: ${uid}, roomID: ${roomID}`);
  res.send("Registration successful");
});

loadUsers()
  .then(() => {
    app.listen(PORT, () => {
      console.log(`Server running on http://localhost:5000`);
    });
  })
  .catch((err) => {
    console.error("Failed to load Excel file:", err);
  });
