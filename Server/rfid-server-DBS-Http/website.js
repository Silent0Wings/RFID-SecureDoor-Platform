// website.js
const express = require("express");

const ADMIN_EMAIL = "y_belgha@live.concordia.ca";
const ADMIN_PASSWORD = "test123"; // change this

function esc(v) {
  return String(v == null ? "" : v);
}

module.exports = function createWebsiteRouter({ getUsers, getLogSheet }) {
  const router = express.Router();

  // GET /admin — login page
  router.get("/admin", (req, res) => {
    res.render("login", { error: null });
  });

  // POST /admin — validate login, show dashboard
  router.post("/admin", (req, res) => {
    const { email, password } = req.body;

    if (email !== ADMIN_EMAIL || password !== ADMIN_PASSWORD) {
      return res.status(401).render("login", { error: "Invalid credentials" });
    }

    const users = getUsers() || [];
    const logs = getLogSheet() || null;

    // Build users table
    const usersRows = users.map((u) => ({
      user: esc(u.user),
      password: esc(u.password),
      uid: esc(u.uid),
      counter: esc(u.counter),
      roomID: esc(u.roomID),
      access: esc(u.access),
    }));

    // Build logs table
    let logsRows = [];
    let logsHeader = [];

    if (logs) {
      logsHeader = logs
        .getRow(1)
        .values.slice(1)
        .map((c) => esc(c));

      logs.eachRow({ includeEmpty: false }, (row, rowNumber) => {
        if (rowNumber === 1) return;
        logsRows.push(row.values.slice(1).map((c) => esc(c)));
      });
    }

    res.render("dashboard", {
      users: usersRows,
      logs: logsRows,
      logHeader: logsHeader,
    });
  });

    // GET /admin/data - JSON for auto-refresh
  router.get("/admin/data", (req, res) => {
    const users = getUsers() || [];
    const logs = getLogSheet() || null;

    const usersRows = users.map((u) => ({
      user: esc(u.user),
      password: esc(u.password),
      uid: esc(u.uid),
      counter: esc(u.counter),
      roomID: esc(u.roomID),
      access: esc(u.access),
    }));

    let logsRows = [];
    let logsHeader = [];

    if (logs) {
      logsHeader = logs
        .getRow(1)
        .values.slice(1)
        .map((c) => esc(c));

      logs.eachRow({ includeEmpty: false }, (row, rowNumber) => {
        if (rowNumber === 1) return;
        logsRows.push(row.values.slice(1).map((c) => esc(c)));
      });
    }

    res.json({
      users: usersRows,
      logs: logsRows,
      logHeader: logsHeader,
    });
  });


  return router;
};
