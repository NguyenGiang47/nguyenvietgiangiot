/*
  ============================================================
  BAI 1.2 - CAU HINH UART BAUD RATE DONG QUA NUT NHAN
  Arduino UNO
  ============================================================

  Ket noi:
    D2  -> Button -> GND
    D13 -> 220 ohm -> LED -> GND

    D1/TX -> RXD Virtual Terminal
    D0/RX <- TXD Virtual Terminal

  Yeu cau:
    - Giu nut > 3 giay: NORMAL <-> CONFIG

    Trong CONFIG:
      + Nhan 1 lan -> 9600 bps
      + Nhan 2 lan -> 115200 bps

    LED:
      + Nhay nhanh khi vao CONFIG
      + Nhay 1 lan neu chon 9600
      + Nhay 2 lan neu chon 115200

    Sau khi cau hinh:
      + Giu nut > 3 giay de quay lai NORMAL
  ============================================================
*/

#define BUTTON_PIN 2
#define LED_PIN    13

// ============================================================
// CAC THOI GIAN
// ============================================================

// Nhan giu 3 giay
const unsigned long LONG_PRESS_TIME = 3000;

// Chong doi nut
const unsigned long DEBOUNCE_TIME = 40;

// Thoi gian cho click thu 2
const unsigned long DOUBLE_CLICK_TIME = 500;


// ============================================================
// TRANG THAI HE THONG
// ============================================================

enum SystemMode
{
  NORMAL_MODE,
  CONFIG_MODE
};

SystemMode currentMode = NORMAL_MODE;


// ============================================================
// UART
// ============================================================

unsigned long currentBaud = 9600;


// ============================================================
// BIEN NUT NHAN
// ============================================================

// Trang thai doc truc tiep
bool lastRawButton = HIGH;

// Trang thai da debounce
bool buttonState = HIGH;

// Thoi diem thay doi nut
unsigned long debounceStart = 0;

// Thoi diem bat dau nhan
unsigned long pressStartTime = 0;

// Danh dau long press da xu ly
bool longPressHandled = false;


// ============================================================
// CLICK
// ============================================================

byte clickCount = 0;

unsigned long lastClickTime = 0;


// ============================================================
// HAM NHAY LED
// ============================================================

void blinkLED(byte numberOfTimes,
              unsigned int onTime,
              unsigned int offTime)
{
  for (byte i = 0; i < numberOfTimes; i++)
  {
    digitalWrite(LED_PIN, HIGH);
    delay(onTime);

    digitalWrite(LED_PIN, LOW);
    delay(offTime);
  }
}


// ============================================================
// LED BAO VAO CONFIG
// ============================================================

void blinkEnterConfig()
{
  // Nhay nhanh 5 lan
  blinkLED(5, 80, 80);
}


// ============================================================
// DOI BAUD RATE
// ============================================================

void setBaudRate(unsigned long newBaud)
{
  Serial.println();
  Serial.println("--------------------------------");

  // Neu baud khong thay doi
  if (newBaud == currentBaud)
  {
    Serial.print("UART dang o ");
    Serial.print(currentBaud);
    Serial.println(" bps");

    Serial.println("Cau hinh UART thanh cong.");
    Serial.println("--------------------------------");

    return;
  }

  // Thong bao nay gui bang baud rate CU
  Serial.print("Chuyen UART tu ");
  Serial.print(currentBaud);
  Serial.print(" sang ");
  Serial.print(newBaud);
  Serial.println(" bps...");

  Serial.flush();

  delay(100);

  // Tat UART
  Serial.end();

  // Cap nhat baud
  currentBaud = newBaud;

  // Khoi tao lai UART voi baud moi
  Serial.begin(currentBaud);

  delay(100);

  // Chu y:
  // Tu dong nay tro di duoc gui bang BAUD RATE MOI

  Serial.println();
  Serial.println("================================");
  Serial.println("UART CONFIGURATION SUCCESS");
  Serial.print("BAUD RATE = ");
  Serial.print(currentBaud);
  Serial.println(" bps");
  Serial.println("================================");
}


// ============================================================
// VAO CONFIG MODE
// ============================================================

void enterConfigMode()
{
  currentMode = CONFIG_MODE;

  // Xoa click cu
  clickCount = 0;

  Serial.println();
  Serial.println("================================");
  Serial.println("ENTER CONFIG MODE");
  Serial.println("================================");
  Serial.println("1 CLICK  -> 9600 bps");
  Serial.println("2 CLICKS -> 115200 bps");
  Serial.println("Hold > 3s -> EXIT CONFIG");
  Serial.println();

  // LED nhay nhanh bao vao config
  blinkEnterConfig();
}


// ============================================================
// THOAT CONFIG MODE
// ============================================================

void exitConfigMode()
{
  currentMode = NORMAL_MODE;

  clickCount = 0;

  digitalWrite(LED_PIN, LOW);

  Serial.println();
  Serial.println("================================");
  Serial.println("EXIT CONFIG MODE");
  Serial.println("NORMAL OPERATION");
  Serial.print("CURRENT BAUD RATE = ");
  Serial.print(currentBaud);
  Serial.println(" bps");
  Serial.println("================================");
}


// ============================================================
// CHON 9600
// ============================================================

void select9600()
{
  Serial.println();
  Serial.println("1 CLICK DETECTED");
  Serial.println("SELECT: 9600 bps");

  // LED nhay 1 lan
  blinkLED(1, 250, 200);

  // Doi baud
  setBaudRate(9600);
}


// ============================================================
// CHON 115200
// ============================================================

void select115200()
{
  Serial.println();
  Serial.println("2 CLICKS DETECTED");
  Serial.println("SELECT: 115200 bps");

  // LED nhay 2 lan
  blinkLED(2, 250, 200);

  // Doi baud
  setBaudRate(115200);
}


// ============================================================
// XU LY CLICK
// ============================================================

void processClicks()
{
  if (currentMode != CONFIG_MODE)
  {
    clickCount = 0;
    return;
  }

  // Neu da co 2 click thi xu ly ngay
  if (clickCount >= 2)
  {
    clickCount = 0;

    select115200();

    return;
  }

  // Neu chi co 1 click:
  // cho het khoang DOUBLE_CLICK_TIME
  if (clickCount == 1)
  {
    if (millis() - lastClickTime >= DOUBLE_CLICK_TIME)
    {
      clickCount = 0;

      select9600();
    }
  }
}


// ============================================================
// XU LY NUT NHAN
// ============================================================

void readButton()
{
  bool rawButton = digitalRead(BUTTON_PIN);

  // ----------------------------------------------------------
  // DEBOUNCE
  // ----------------------------------------------------------

  if (rawButton != lastRawButton)
  {
    debounceStart = millis();
  }

  if (millis() - debounceStart >= DEBOUNCE_TIME)
  {
    if (rawButton != buttonState)
    {
      buttonState = rawButton;

      // ======================================================
      // BAT DAU NHAN NUT
      // ======================================================

      if (buttonState == LOW)
      {
        pressStartTime = millis();

        longPressHandled = false;
      }

      // ======================================================
      // THA NUT
      // ======================================================

      else
      {
        // Neu day khong phai la long press
        if (!longPressHandled)
        {
          // Chi dem click trong CONFIG MODE
          if (currentMode == CONFIG_MODE)
          {
            clickCount++;

            lastClickTime = millis();
          }
        }
      }
    }
  }

  lastRawButton = rawButton;


  // ----------------------------------------------------------
  // KIEM TRA NHAN GIU > 3 GIAY
  // ----------------------------------------------------------

  if (buttonState == LOW &&
      longPressHandled == false)
  {
    if (millis() - pressStartTime >= LONG_PRESS_TIME)
    {
      longPressHandled = true;

      // Long press khong duoc tinh la click
      clickCount = 0;

      // NORMAL -> CONFIG
      if (currentMode == NORMAL_MODE)
      {
        enterConfigMode();
      }

      // CONFIG -> NORMAL
      else
      {
        exitConfigMode();
      }
    }
  }
}


// ============================================================
// SETUP
// ============================================================

void setup()
{
  // Button noi D2 xuong GND
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // LED
  pinMode(LED_PIN, OUTPUT);

  digitalWrite(LED_PIN, LOW);

  // Khoi dong UART tai 9600
  Serial.begin(currentBaud);

  delay(300);

  Serial.println();
  Serial.println("================================");
  Serial.println("UART DYNAMIC BAUD RATE SYSTEM");
  Serial.println("ARDUINO UNO");
  Serial.println("================================");
  Serial.println("MODE      : NORMAL");
  Serial.println("BAUD RATE : 9600 bps");
  Serial.println();
  Serial.println("Hold BUTTON > 3 seconds");
  Serial.println("to enter CONFIG MODE.");
  Serial.println("================================");
}


// ============================================================
// LOOP
// ============================================================

void loop()
{
  // Doc va xu ly nut
  readButton();

  // Xu ly single / double click
  processClicks();


  // ==========================================================
  // CHE DO VAN HANH BINH THUONG
  // ==========================================================

  if (currentMode == NORMAL_MODE)
  {
    /*
       Dat chuong trinh van hanh binh thuong tai day.

       Khong nen Serial.print lien tuc o day
       vi Virtual Terminal se bi day chu.
    */
  }


  // ==========================================================
  // CONFIG MODE
  // ==========================================================

  else
  {
    /*
       Trong CONFIG MODE:
          1 click  = 9600
          2 clicks = 115200

       Viec xu ly nam trong processClicks().
    */
  }
}