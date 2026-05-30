ESP32 Arduino project for an Early Panic Detection and Alert System. It uses a MAX30102 sensor to monitor vital signs and sends alerts when panic symptoms are detected.
Main Components Used
1.ESP32 microcontroller
2.MAX30102 sensor for:
- Heart Rate (BPM)
- Blood Oxygen (SpO₂)
3.DFPlayer Mini module for voice/audio alerts
4.Buzzer as a backup alarm
5.Wi-Fi connectivity
6.Telegram Bot for notifications
7.Web Server to provide sensor data to a dashboard
Key Features

1. Vital Sign Monitoring
The system continuously measures:
- Heart Rate (BPM)
- Blood Oxygen Level (SpO₂)
- PPG (Photoplethysmography) signal characteristics
  
2. Panic Detection Algorithm
The code calculates:
- Heart rate
- Blood oxygen level
- PPG amplitude
- Pulse width ratio
- A panic score is generated based on conditions such as:
- Heart rate > 110 BPM
- Abnormal pulse width ratio
- Abnormal PPG amplitude
- High blood pressure thresholds (variables included)
- If the score reaches a certain level for 2 consecutive readings, a panic event is confirmed.

3. Audio Alert System
When panic is detected:
- Plays an alert sound (001.mp3) through DFPlayer Mini.
- If DFPlayer is unavailable, a buzzer alarm is activated.
- 
4. Telegram Notifications
The system sends:
- Panic alert messages
- Recovery messages when vitals return to normal
- Startup/online status notifications

Project Purpose:
This project is designed to:
- Monitor a person's physiological signals in real time.
- Detect early signs of panic or anxiety.
- Trigger audio alarms.
- Notify caregivers through Telegram.
- Provide live data to a monitoring dashboard.
