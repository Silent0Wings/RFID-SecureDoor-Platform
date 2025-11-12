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

// Load the Excel file and cache users array on server startup
async function loadUsers() {
  await workbook.xlsx.readFile(filePath);
  worksheet = workbook.worksheets[0];

  // Extract data from worksheet rows starting from row 2 (assuming headers in row 1)
  users = [];
  worksheet.eachRow((row, rowNumber) => {
    if (rowNumber === 1) return; // skip header row

    const userObj = {
      user: row.getCell(1).value,
      password: row.getCell(2).value,
      uid: row.getCell(3).value,
      counter: row.getCell(4).value || 0,
      rowNumber, // keep track of the row number for updating later
    };
    users.push(userObj);
  });
}

// Save updated users back to worksheet and file
async function saveUsers() {
  users.forEach((user) => {
    const row = worksheet.getRow(user.rowNumber);
    row.getCell(4).value = user.counter; // Update counter cell
    row.commit();
  });
  await workbook.xlsx.writeFile(filePath);
}

app.get("/user/:uid", (req, res) => {
  const uid = req.params.uid;
  const user = users.find((u) => u.uid == uid);

  if (user) {
    // Return user info except for rowNumber (used internally)
    const { user: username, password, uid, counter } = user;
    res.json({ user: username, password, uid, counter });
  } else {
    res.status(404).json({ error: "UID not found" });
  }
});


app.post("/post_endpoint", async (req, res) => {
  const uid = req.body.uid;
  console.log("Received UID:", uid);

  const user = users.find((u) => u.uid == uid);

  if (user) {
    user.counter++;
    await saveUsers();
    res.send(`UID ${uid} recognized. Counter updated.`);
  } else {
    res.status(404).send("UID not found");
  }
});

// Initialize server by loading users first
loadUsers()
  .then(() => {
    app.listen(PORT, () => {
      console.log(`Server running on http://localhost:${PORT}`);
    });
  })
  .catch((err) => {
    console.error("Failed to load Excel file:", err);
  });
