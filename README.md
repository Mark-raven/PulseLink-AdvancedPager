# 📟 PulseLink

A portable smart pager built using the **ESP32-S3** and **ESP-IDF**, designed for real-time wireless communication over **Bluetooth Low Energy (BLE)** and **Wi-Fi**. LinkPager combines embedded software, wireless networking, and an intuitive OLED-based user interface into a compact handheld communication device.

---

## 📖 Overview

LinkPager demonstrates the practical implementation of modern embedded systems by integrating BLE messaging, Wi-Fi communication, wireless scanning, and a menu-driven interface. The device allows users to exchange messages with a mobile device, scan nearby BLE devices and Wi-Fi networks, receive notifications through a buzzer, and navigate using dedicated hardware buttons.

The project is developed entirely in **Embedded C** using **ESP-IDF** and follows a modular FreeRTOS task-based architecture.

---

# ✨ Features

### 📡 Bluetooth Low Energy (BLE)
- BLE GATT Server implementation
- Real-time message reception
- Send predefined quick replies
- Automatic connection status indication
- BLE device scanning

### 📶 Wi-Fi Communication
- TCP Socket Server
- Phone-to-device messaging
- Device-to-phone messaging
- Wi-Fi network scanning with RSSI display
- Local network communication

### 🖥️ User Interface
- 128×64 SSD1306 OLED Display
- Menu-driven navigation
- Four-button interface
- Connection status display
- Incoming message display
- Signal strength visualization

### 🔔 Notifications
- Piezo buzzer alerts
- Incoming message notifications
- Button feedback tones

### ⚙️ Embedded Software
- Developed in Embedded C
- ESP-IDF Framework
- FreeRTOS multitasking
- Event-driven architecture
- Modular source code
- GPIO, I²C and Socket programming

---

# 🛠 Hardware

- ESP32-S3 Development Board
- SSD1306 128×64 I²C OLED Display
- Push Buttons
- Piezo Buzzer
- 3.7V Li-ion/Li-Po Battery
- USB-C Programming Interface

---

# 💻 Software Stack

- ESP-IDF
- Embedded C
- FreeRTOS
- NimBLE Stack
- Wi-Fi Driver
- BSD Socket API
- SSD1306 OLED Driver

---

# 📂 Project Structure

```
LinkPager/
│
├── main/
│   ├── app_bt.c
│   ├── ssd1306.c
│   ├── ssd1306.h
│   ├── bt_hci_common.c
│   └── ...
│
├── CMakeLists.txt
├── sdkconfig.defaults
├── README.md
└── ...
```

---

# 🚀 Features Demonstrated

- Bluetooth Low Energy (BLE)
- Wi-Fi Communication
- TCP Socket Programming
- BLE GATT Services
- FreeRTOS Task Management
- OLED Graphics
- GPIO Handling
- Embedded User Interface Design
- Wireless Device Discovery
- Event-driven Programming

---

# 📱 Applications

- Smart Pager
- Industrial Communication Device
- Hospital Notification System
- Factory Alert System
- Embedded Systems Learning Platform
- IoT Communication Device

---

# 🔮 Future Enhancements

- 🔒 End-to-end encrypted messaging
- 🌐 Mesh networking
- 👥 Group messaging
- 📝 Message history
- 📱 Android & iOS companion application
- 📍 GPS integration
- 🔋 Battery level monitoring
- ☁️ OTA firmware updates
- 😴 Low-power sleep modes
- 🔐 Secure device pairing

---

# 📷 Demo

<img width="728" height="1600" alt="LinkPulse-1" src="https://github.com/user-attachments/assets/569e2b74-8147-47e5-a4f7-295003b4048d" />


<img width="1600" height="728" alt="LinkPulse-2" src="https://github.com/user-attachments/assets/82e1fbf7-e7ea-468a-9d58-c8087a4a5a2f" />


<img width="728" height="1600" alt="LinkPulse-1" src="https://github.com/user-attachments/assets/489c196d-d2fe-45c4-a934-d2496a7e16e6" />



Example:

- Main Menu
- BLE Messaging
- Wi-Fi Messaging
- BLE Scanner
- Wi-Fi Scanner

---

# 👨‍💻 Author

**Mark Gerald**

Embedded Software Engineer

GitHub: https://github.com/Mark-raven

---

# 📄 License

This project is licensed under the **MIT License**.

Feel free to use, modify, and distribute this project for educational and personal purposes.
