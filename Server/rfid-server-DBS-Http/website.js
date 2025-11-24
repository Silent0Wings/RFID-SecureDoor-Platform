// website.js
const express = require("express");

// Use your real email here
const ADMIN_EMAIL = "y_belgha@live.concordia.ca";

function esc(value) {
  return String(value == null ? "" : value)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

module.exports = function createWebsiteRouter({ getUsers, getLogSheet }) {
  const router = express.Router();

  function renderUsersTableHtml() {
    const users = getUsers();
    if (!users || users.length === 0) {
      return "<p>No user data available.</p>";
    }

    const rowsHtml = users
      .map(
        (u) => `
        <tr>
          <td>${esc(u.user)}</td>
          <td>${esc(u.password)}</td>
          <td>${esc(u.uid)}</td>
          <td>${esc(u.counter)}</td>
          <td>${esc(u.roomID)}</td>
          <td>${esc(u.access)}</td>
        </tr>`
      )
      .join("");

    return `
      <table border="1" cellspacing="0" cellpadding="4">
        <thead>
          <tr>
            <th>User</th>
            <th>Password</th>
            <th>UID</th>
            <th>Counter</th>
            <th>RoomID</th>
            <th>Access</th>
          </tr>
        </thead>
        <tbody>
          ${rowsHtml}
        </tbody>
      </table>
    `;
  }

  function renderLogsTableHtml() {
    const logSheet = getLogSheet();
    if (!logSheet) {
      return "<p>No logs available.</p>";
    }

    let headerHtml = "";
    let bodyHtml = "";

    logSheet.eachRow({ includeEmpty: false }, (row, rowNumber) => {
      const cells = row.values.slice(1).map((c) => esc(c));
      if (rowNumber === 1) {
        headerHtml = `
          <tr>
            ${cells.map((c) => `<th>${c}</th>`).join("")}
          </tr>`;
      } else {
        bodyHtml += `
          <tr>
            ${cells.map((c) => `<td>${c}</td>`).join("")}
          </tr>`;
      }
    });

    return `
      <table border="1" cellspacing="0" cellpadding="4">
        <thead>${headerHtml}</thead>
        <tbody>${bodyHtml}</tbody>
      </table>
    `;
  }

  // GET /admin - login form
  router.get("/admin", (req, res) => {
    res.send(`<!doctype html>
<html>
  <head>
    <meta charset="utf-8">
    <title>Admin Login</title>
  </head>
  <body>
    <h1>Admin panel</h1>
    <form method="POST" action="/admin">
      <label>Email:
        <input type="email" name="email" required>
      </label>
      <button type="submit">Login</button>
    </form>
  </body>
</html>`);
  });

  // POST /admin - check email and show XLSX tables
  router.post("/admin", (req, res) => {
    const { email } = req.body || {};

    if (email !== ADMIN_EMAIL) {
      return res.status(401).send(`<!doctype html>
<html>
  <head>
    <meta charset="utf-8">
    <title>Admin Login</title>
  </head>
  <body>
    <h1>Admin panel</h1>
    <p style="color:red;">Invalid email.</p>
    <form method="POST" action="/admin">
      <label>Email:
        <input type="email" name="email" required>
      </label>
      <button type="submit">Login</button>
    </form>
  </body>
</html>`);
    }

    const usersTable = renderUsersTableHtml();
    const logsTable = renderLogsTableHtml();

    res.send(`<!doctype html>
<html>
  <head>
    <meta charset="utf-8">
    <title>Admin Dashboard</title>
  </head>
  <body>
    <h1>Admin dashboard</h1>
    <h2>users.xlsx</h2>
    ${usersTable}
    <h2>logs.xlsx</h2>
    ${logsTable}
  </body>
</html>`);
  });

  return router;
};
