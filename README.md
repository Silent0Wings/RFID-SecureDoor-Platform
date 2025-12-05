# RFID Access Control System (ESP32 / TTGO LoRa32)

<img src="Project/Presentation/ESP%20Side/img/Circuit.png" width="400"><img src="Project/Presentation/ESP%20Side/img/tech_stack.drawio.png" width="400">
<img src="Project/Presentation/ESP%20Side/img/ESP32%20Firmware%20Modules%20-%20File%20Level%20View.png" width="400"><img src="Project/Presentation/ESP%20Side/img/Web%20UI%20Routing.png" width="400">

A complete ESP32-based RFID access control prototype using an MFRC522 reader, OLED display, RGB LED, buzzer, capacitive touch sensor, relay output, and a Node.js backend with Excel-based storage.

This system uses **two backends**:
- **Embedded HTTP server on the ESP32** serving `/`, `/register`, `/login`, `/status` for local diagnostics.  
- **External Node.js backend** acting as the main authority for authentication, registration, deletion, and logging.

All communication between the ESP32 and the Node.js backend is done via **REST endpoints using JSON**.

---
![Example](Project/Example.gif)

## ▶️ How It Works (Quick Example)

1. Admin opens `/register` on ESP32.
2. Enters user info.
3. Taps a blank RFID card during registration window.
4. ESP32 sends `POST /register` (JSON) to backend.
5. Backend stores user → Excel.
6. User taps again → backend decision → LED + buzzer + OLED feedback.

---

## 🖥️ Backend Servers (Node.js)

The `Server` folder contains three backend implementations:

- `rfid-server` – basic prototype for early testing
- `rfid-server-DBS` – version with ExcelJS storage
- `rfid-server-DBS-Http` – final HTTP + JSON REST backend used by the ESP32

Each backend must install dependencies using:

---

## 📄 Important PDFs

- **Documentation.pdf** – Full system report, diagrams, risks, and architecture.  
  [Documentation.pdf](Documentation.pdf)

- **Plan.pdf** – Progress plan, subsystem status, tasks, and updated diagrams.  
  [Plan.pdf](Plan.pdf)

- **Proposal.pdf** – Initial design proposal, architecture overview, risks, validation metrics.  
  [Proposal.pdf](Proposal.pdf)

---

## 🔧 System Overview

- **ESP32 TTGO LoRa32** runs the access control logic, reads RFID cards, handles UI feedback, and serves embedded web pages.
- **MFRC522 RFID Reader** for UID read pipeline.
- **OLED + RGB LED + Buzzer** provide clear state feedback.
- **Capacitive Touch Button** controls delete-room and delete-user modes.
- **AsyncWebServer (ESP32)** exposes:
  - `/`
  - `/register`
  - `/login`
  - `/status`
- **Node.js + Excel Backend** manages:
  - Registration  
  - Access validation  
  - Logging  
  - User deletion  
  - Auto-refresh admin dashboard  
- **REST + JSON communication** between ESP32 and backend:
  - `POST /register`
  - `GET /user/:uid?roomID=...`
  - `DELETE /user/:uid`
  - `GET /stats`

---

## 🧩 System Architecture Overview

The system consists of **two cooperating backends**:

1. **Embedded HTTP Backend (ESP32)**
   - Serves `/`, `/register`, `/login`, `/status`
   - Used for diagnostics, local interaction, and minimal UI
   - Implements basic device-side routing and status reporting

2. **External Node.js Backend**
   - Main authority for authentication, registration, and deletion
   - Stores users and logs in Excel (users.xlsx, logs.xlsx)
   - Provides RESTful JSON endpoints:
     - `POST /register`
     - `GET /user/:uid?roomID=...`
     - `DELETE /user/:uid`
     - `GET /stats`
   - Drives the admin dashboard (auto-refresh)

Communication between the ESP32 and backend is strictly **REST over HTTP with JSON payloads**.

---

## ✨ Features

- RFID registration and authentication
- REST-based access checks (JSON)
- OLED, RGB LED, buzzer, and touch-button UI
- Delete-room and delete-user modes
- Embedded local ESP32 web server
- Node.js backend with Excel persistence
- Auto-refresh admin dashboard
- Multi-room simulation using potentiometer

---

## 📷 Key Diagrams (from PDFs)

- **System Architecture**  
  See *Documentation.pdf* (page 1)  
  → [Documentation.pdf](Documentation.pdf)

- **Firmware Module Structure**  
  See *Documentation.pdf* (pages 1–2)  
  → [Documentation.pdf](Documentation.pdf)

- **RFID Registration Flow**  
  See *Documentation.pdf* (page 2)  
  → [Documentation.pdf](Documentation.pdf)

- **Access Validation Sequence**  
  See *Documentation.pdf* (page 3)  
  → [Documentation.pdf](Documentation.pdf)

- **Circuit & Wiring Diagram**  
  See *Documentation.pdf* (page 4)  
  → [Documentation.pdf](Documentation.pdf)

- **System Risk Map**  
  See *Documentation.pdf* (page 6)  
  → [Documentation.pdf](Documentation.pdf)

---

Key documents:  
- [Documentation.pdf](Documentation.pdf)  
- [Plan.pdf](Plan.pdf)  
- [Proposal.pdf](Proposal.pdf)
