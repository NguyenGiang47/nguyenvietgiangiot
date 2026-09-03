#include <Arduino.h>

// ================== CẤU HÌNH CHÂN ==================
#define BUTTON_PIN    0     // D3
#define LED_PIN       2     // D4

// ================== HẰNG SỐ ==================
const unsigned long PRESS_TIMEOUT = 800;        // Timeout cho double click
const unsigned long LONG_PRESS_TIME = 3000;     // 3 giây

// ================== BIẾN TOÀN CỤC ==================
enum Mode { MODE_RUN, MODE_CONFIG };
Mode currentMode = MODE_RUN;

unsigned long buttonPressStart = 0;
bool buttonWasPressed = false;
bool longPressHandled = false;

uint8_t pressCount = 0;
unsigned long lastPressTime = 0;

uint32_t currentBaud = 115200;
bool configApplied = false;

// ================== HÀM HỖ TRỢ ==================

void setBaudRate(uint32_t baud) {
  if (currentBaud == baud) {
    Serial.println("  [!] Baud rate da duoc chon tu truoc");
    return;
  }
  
  Serial.flush();
  delay(50);
  Serial.end();
  delay(50);
  Serial.begin(baud);
  delay(50);
  currentBaud = baud;
  
  Serial.println(">>> DA CHUYEN SANG " + String(baud) + " bps");
}

void setLED(bool state) {
  digitalWrite(LED_PIN, state ? HIGH : LOW);
}

void blinkLED(int times, int onTime = 150, int offTime = 150) {
  for (int i = 0; i < times; i++) {
    setLED(true);
    delay(onTime);
    setLED(false);
    if (i < times - 1) delay(offTime);
  }
}

void fastBlinkEnterConfig() {
  for (int i = 0; i < 8; i++) {
    setLED(true);
    delay(70);
    setLED(false);
    delay(70);
  }
}

void blinkError() {
  blinkLED(5, 80, 80);
}

// Hàm hiển thị trạng thái hiện tại
void printCurrentStatus() {
  Serial.println("----------------------------------------");
  Serial.print("Che do: ");
  Serial.println(currentMode == MODE_RUN ? "VAN HANH" : "CAU HINH");
  Serial.print("Baud rate: ");
  Serial.print(currentBaud);
  Serial.println(" bps");
  Serial.println("----------------------------------------");
}

// ================== SETUP ==================
void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  setLED(false);

  Serial.begin(currentBaud);
  delay(500);

  Serial.println("\n========== HE THONG CAU HINH UART DONG ==========");
  Serial.println("Board: Wemos D1 Mini");
  Serial.println("LED ngoai (Active HIGH)");
  printCurrentStatus();
  Serial.println("HUONG DAN:");
  Serial.println("  - Nhan giu >3s: Vao che do cau hinh");
  Serial.println("  - Trong che do cau hinh:");
  Serial.println("    * Nhan 1 lan -> 9600 bps");
  Serial.println("    * Nhan 2 lan -> 115200 bps");
  Serial.println("  - Nhan giu >3s: Thoat ve van hanh");
  Serial.println("================================================\n");
}

// ================== LOOP ==================
void loop() {
  bool buttonState = digitalRead(BUTTON_PIN) == LOW;
  unsigned long currentMillis = millis();

  // ---------- Xử lý nhấn giữ (Long Press) ----------
  if (buttonState && !buttonWasPressed) {
    buttonPressStart = currentMillis;
    buttonWasPressed = true;
    longPressHandled = false;
    configApplied = false;
  }

  if (buttonState && buttonWasPressed && !longPressHandled) {
    if (currentMillis - buttonPressStart >= LONG_PRESS_TIME) {
      longPressHandled = true;

      if (currentMode == MODE_RUN) {
        // Chuyển từ RUN sang CONFIG
        currentMode = MODE_CONFIG;
        pressCount = 0;
        lastPressTime = 0;
        configApplied = false;
        fastBlinkEnterConfig();
        
        Serial.println("\n>>> DA VAO CHE DO CAU HINH <<<");
        Serial.println("  [1 lan] -> 9600 bps");
        Serial.println("  [2 lan] -> 115200 bps");
        Serial.println("  [giu >3s] -> thoat ve van hanh");
        printCurrentStatus();
        Serial.println("  [CHUA CHON] Hay nhan 1 hoac 2 lan...\n");
      } 
      else {
        // Chuyển từ CONFIG sang RUN
        currentMode = MODE_RUN;
        pressCount = 0;
        blinkLED(3, 200, 200);
        
        Serial.println("\n>>> DA TRO VE CHE DO VAN HANH <<<");
        printCurrentStatus();
        Serial.println();
      }
    }
  }

  // ---------- Xử lý nhả nút (Short Press) ----------
  if (!buttonState && buttonWasPressed) {
    unsigned long pressDuration = currentMillis - buttonPressStart;
    buttonWasPressed = false;

    if (pressDuration < LONG_PRESS_TIME && 
        currentMode == MODE_CONFIG && 
        !longPressHandled) {
      
      if (configApplied) {
        Serial.println("  [!] Da chon roi! Hay thoat ra de chon lai");
        blinkLED(2, 100, 100);
      } else {
        pressCount++;
        lastPressTime = currentMillis;
        
        Serial.print("  [Nhan] Lan thu: ");
        Serial.print(pressCount);
        
        if (pressCount == 1) {
          Serial.println(" (Chuan bi chon 9600 bps)");
        } else if (pressCount == 2) {
          Serial.println(" (Chuan bi chon 115200 bps)");
        } else {
          Serial.println(" (Khong hop le!)");
        }
        Serial.println("  Vui long cho 800ms de xac nhan...");
        
        blinkLED(1, 100, 50);
      }
    }
  }

  // ---------- Xử lý chọn baud rate sau timeout ----------
  if (currentMode == MODE_CONFIG && pressCount > 0 && !configApplied) {
    if (currentMillis - lastPressTime > PRESS_TIMEOUT) {
      
      if (pressCount == 1) {
        Serial.println("\n[XAC NHAN] Chon 9600 bps");
        setBaudRate(9600);
        blinkLED(1, 300, 200);
        configApplied = true;
        Serial.println("  [HOAN TAT] Da chon xong!");
        Serial.println("  Nhan giu >3s de thoat ve van hanh");
        printCurrentStatus();
        
      } else if (pressCount == 2) {
        Serial.println("\n[XAC NHAN] Chon 115200 bps");
        setBaudRate(115200);
        blinkLED(2, 300, 200);
        configApplied = true;
        Serial.println("  [HOAN TAT] Da chon xong!");
        Serial.println("  Nhan giu >3s de thoat ve van hanh");
        printCurrentStatus();
        
      } else {
        Serial.print("\n[LOI] So lan nhan khong hop le: ");
        Serial.print(pressCount);
        Serial.println(" (chi 1 hoac 2 lan)");
        blinkError();
        pressCount = 0;
        Serial.println("  Hay thu lai (nhan 1 hoac 2 lan)\n");
      }
    }
  }

  // ---------- Xử lý nếu đang ở chế độ vận hành ----------
  if (currentMode == MODE_RUN) {
    static unsigned long lastSendTime = 0;
    if (currentMillis - lastSendTime > 10000) {
      lastSendTime = currentMillis;
      Serial.print("[VAN HANH] Time: ");
      Serial.print(currentMillis);
      Serial.print(" ms | Baud rate: ");
      Serial.print(currentBaud);
      Serial.println(" bps");
    }
  }

  delay(10);
}