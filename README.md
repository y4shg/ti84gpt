# **TI-84 Plus CE ChatGPT USB Client**

This project brings a fully interactive ChatGPT-style chat interface to the **TI-84 Plus CE** calculator using USB communication.
It features a scrolling chat UI, message bubbles, user input mode, and seamless USB data transfer between the calculator and a connected host (such as a PC browser or server running a ChatGPT backend).

---

## **📌 Features**

### **Chat Interface**

* Smooth, colored chat bubbles (blue for user, green for AI).
* Auto-wrapping text with custom bubble width detection.
* Scroll system using arrow keys.
* Persistent message history stored using linked lists.

### **Input System**

* A full-screen typing UI.
* Supports letters mapped to the keypad.
* Backspace, send, and cancel operations.
* Automatic scrolling to latest message.

### **USB Communication**

* Uses **usbdrvce** to handle transfers.
* Bulk IN/OUT endpoints for sending messages and receiving AI responses.
* Automatic reconnection handling.
* Data buffered until newline (`\n`) before displaying the AI's message.

### **Graphics**

* Built with **graphx** from the CE toolchain.
* Buffered drawing for smooth rendering.
* Custom UI layout:

  * Title bar
  * Footer instructions
  * Chat window

---

## **📂 File Overview**

This repository contains:

* `main.c` — Full application source code
* `Makefile` — Build configuration for the CE toolchain
* `README.md` — Documentation (this file)

---

## **🛠 Requirements**

### **Software**

* **CE Toolchain** ([https://github.com/CE-Programming/toolchain](https://github.com/CE-Programming/toolchain))
* **Libraries:**

  * `graphx`
  * `keypadc`
  * `usbdrvce`
  * `tice` / `ti/getcsc`

### **Hardware**

* TI-84 Plus CE (OS supporting USB host)
* USB cable (calculator → computer)
* A host program to send and receive text (e.g., WebUSB script or Python backend)

---

## **🎮 Controls**

| Key         | Action                       |
| ----------- | ---------------------------- |
| **2nd**     | Open typing screen           |
| **Up/Down** | Scroll chat                  |
| **Enter**   | Send message (in input mode) |
| **Del**     | Backspace (in input mode)    |
| **Clear**   | Quit or cancel input         |

---

## **📡 USB Communication**

### Input → Host

Messages are sent immediately after pressing **Enter**, using:

```c
usb_BulkTransfer(global.in, inputBuffer, inputIndex, 0, NULL);
```

### Host → Calculator

Incoming chunks are collected until a newline:

```c
char* newline = strchr(receiveBuffer, '\n');
```

Once received, the full message is displayed as an AI bubble.

---

## **🧱 Code Structure**

### **Major Components**

* `drawMessages()` — Draws chat history with bubble rendering
* `drawInputScreen()` — Displays text-entry UI
* `takeInput()` — Handles input mode and keypress-to-text conversion
* `sendMessage()` — Adds user message + sends via USB
* `handleBulkOut()` — Processes incoming USB data
* `handleUsbEvent()` — Manages connections
* `wrapText()` — Line wrapping for bubbles
* `Message` struct — Linked list holding full conversation

---

## **🚀 Setup & Build**

1. Install the CE Toolchain
2. Clone this repository
3. Build the project:

```
make
```

4. Send the generated `.8xp` file to your calculator
5. Connect your USB cable and run your host backend
6. Launch the program on your calculator

---

## **🎨 UI Preview (Description)**

* **Top bar**: status ("ChatGPT — Ready" / "Waiting...")
* **Scrollable chat region**: bubbles with wrapped text
* **Bottom bar**: usage instructions
* **Input mode**: full-screen editor with blinking cursor
