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
      // ignore completely empty rows
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
        roomID: row.getCell(5).value,
        access: row.getCell(6).value, // e.g. TRUE/FALSE or YES/NO (case-insensitive)
        rowNumber,
      };
      users.push(userObj);
    });

    if (!hasData) {
      console.warn("Excel file loaded but has no user records.");
    }
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
    row.getCell(4).value = user.counter; // Update counter cell
    row.commit();
  });
  await workbook.xlsx.writeFile(filePath);
}

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

  // If roomID is provided, optionally check match
  if (
    requestedRoomID &&
    user.roomID &&
    String(user.roomID) !== String(requestedRoomID)
  ) {
    return res.status(403).json({ error: "RoomID does not match for user" });
  }

  // Check access flag ('access' column) for TRUE/YES (case insensitive)
  const accessStr = String(user.access).toLowerCase();
  const hasAccess =
    accessStr === "true" || accessStr === "yes" || accessStr === "1";

  if (!hasAccess) {
    return res.status(403).json({
      user: user.user,
      uid: user.uid,
      roomID: user.roomID,
      access: false,
      error: "Access denied",
    });
  }

  // User has access
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
    roomID,
    access: hasAccess,
  });
});

app.post("/post_endpoint", async (req, res) => {
  if (!users.length) {
    return res.status(404).send("No user data available");
  }
  const uid = req.body.uid;
  console.log("Received UID:", uid);

  const user = users.find((u) => u.uid == uid);

  if (!user) {
    return res.status(404).send("UID not found");
  }

  // Check access flag before allowing counter update
  const accessStr = String(user.access).toLowerCase();
  const hasAccess =
    accessStr === "true" || accessStr === "yes" || accessStr === "1";

  if (!hasAccess) {
    return res.status(403).send("Access denied");
  }

  user.counter++;
  await saveUsers();
  res.send(`UID ${uid} recognized. Counter updated.`);
});

loadUsers()
  .then(() => {
    app.listen(PORT, () => {
      console.log(`Server running on http://localhost:${PORT}`);
    });
  })
  .catch((err) => {
    console.error("Failed to load Excel file:", err);
  });
