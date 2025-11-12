const express = require("express");
const bodyParser = require("body-parser");

const app = express();
const PORT = 5000; // You can change this port if needed

// Middleware to parse form data (application/x-www-form-urlencoded)
app.use(bodyParser.urlencoded({ extended: false }));
app.use(bodyParser.json());

// POST endpoint to receive UID
app.post("/post_endpoint", (req, res) => {
  const uid = req.body.uid;
  console.log("Received UID:", uid);
  // Add your logic here (e.g., logging, validation, storage)
  res.send("UID received");
});

app.listen(PORT, () => {
  console.log(`Server running on http://localhost:${PORT}`);
});
