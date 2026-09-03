#include <SoftwareSerial.h>

#define RX_PIN 4        // D2 (GPIO4) – nối với TX của UNO
#define TX_PIN 5        // D1 (GPIO5) – nối với RX của UNO
#define LED_WEMOS 13    // D6 (GPIO12) – đèn trên Wemos (điều khiển từ UNO)

SoftwareSerial mySerial(RX_PIN, TX_PIN);

void setup() {
  Serial.begin(115200);      // Serial Monitor của Wemos
  mySerial.begin(9600);      // Giao tiếp với UNO

  pinMode(LED_WEMOS, OUTPUT);
  digitalWrite(LED_WEMOS, LOW);

  Serial.println("=== WEMOS D1 READY ===");
  Serial.println("Gửi 1 hoặc 0 từ Serial Monitor này để điều khiển đèn UNO.");
  Serial.println("Nhận 1 hoặc 0 từ UNO để điều khiển đèn Wemos.");
}

void loop() {
  // ---------- NHẬN TỪ UNO (điều khiển đèn Wemos) ----------
  if (mySerial.available()) {
    char c = mySerial.read();
    Serial.print("Nhận từ UNO: ");
    Serial.println(c);

    if (c == '1') {
      digitalWrite(LED_WEMOS, HIGH);
      Serial.println("=> Bật đèn Wemos (D6)");
    } 
    else if (c == '0') {
      digitalWrite(LED_WEMOS, LOW);
      Serial.println("=> Tắt đèn Wemos (D6)");
    }
  }

  // ---------- GỬI TỪ SERIAL MONITOR WEMOS (điều khiển đèn UNO) ----------
  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == '1' || cmd == '0') {
      mySerial.print(cmd);      // Gửi sang UNO (không kèm xuống dòng)
      Serial.print("Đã gửi sang UNO: ");
      Serial.println(cmd);
    }
  }
}