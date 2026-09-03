#include <SoftwareSerial.h>

#define RX_PIN 10
#define TX_PIN 9
#define LED_PIN 13     // ĐÈN NGOÀI (không phải onboard)

SoftwareSerial mySerial(RX_PIN, TX_PIN);

void setup() {
  Serial.begin(9600);
  mySerial.begin(9600);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.println("=== ARDUINO UNO (LED ngoài chân 8) ===");
  Serial.println("Gửi 1/0 để điều khiển đèn Wemos D6.");
  Serial.println("Nhận 1/0 từ Wemos để điều khiển đèn UNO chân 8.");
}

void loop() {
  // Nhận từ Wemos -> điều khiển đèn UNO
  if (mySerial.available()) {
    char c = mySerial.read();
    Serial.print("Nhận từ Wemos: ");
    Serial.println(c);
    if (c == '1') {
      digitalWrite(LED_PIN, HIGH);
      Serial.println("=> Bật đèn UNO (chân 8)");
    } else if (c == '0') {
      digitalWrite(LED_PIN, LOW);
      Serial.println("=> Tắt đèn UNO (chân 8)");
    }
  }

  // Gửi từ Serial Monitor UNO -> điều khiển đèn Wemos
  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == '1' || cmd == '0') {
      mySerial.print(cmd);
      Serial.print("Đã gửi sang Wemos: ");
      Serial.println(cmd);
    }
  }
}