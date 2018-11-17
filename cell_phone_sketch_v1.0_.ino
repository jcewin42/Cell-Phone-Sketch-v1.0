/*
 * TODO: silent mode (as an option ;))
 */

#include <Adafruit_FONA.h> 
#include <SoftwareSerial.h> 
#include <Adafruit_GFX.h> 
#include <Adafruit_PCD8544.h> 
#include <Keypad.h> 

char incomingCallNumber;

bool connected; 
bool silent;

String cellDate, cellTime;

int signalQuality; // used to draw signal strength bars 
int8_t RSSI;

int16_t batteryPercentage;

int callStatus, prevCallStatus;

unsigned long lastConnectionStatusCheckTime, lastClockCheckTime, lastSignalQualityCheckTime, lastBatteryPercentageCheckTime, noteStartTime; 

char number[20];
char name[20];

#define NAME_OR_NUMBER() (name[0] == 0 ? (number == 0 ? "Unknown" : number) : name)

// Feather 32u4 
#define FONA_RX  9 
#define FONA_TX  8 
#define FONA_RST 4 
#define FONA_RI  7 

int callVolume = 25; // default volume percentages
int ringVolume = 75; 

//int alertSoundType = 8; // 1, 7, 9, and 11 are decent

SoftwareSerial fonaSS = SoftwareSerial(FONA_TX, FONA_RX); 
SoftwareSerial *fonaSerial = &fonaSS; 

Adafruit_FONA fona = Adafruit_FONA(FONA_RST); 

// Nokia 5110/3310 
#define scePin  13    // SCE - Chip select, pin 3 on LCD. 
#define rstPin  12    // RST - Reset, pin 4 on LCD. 
#define dcPin   11    // DC - Data/Command, pin 5 on LCD. 
#define sdinPin MOSI  // DN(MOSI) - Serial data, pin 6 on LCD. 
#define sclkPin SCK   // SCLK - Serial clock, pin 7 on LCD. 
#define blPin   10    // LED - Backlight LED, pin 8 on LCD. 

#define SCREEN_WIDTH 14 
#define ENTRY_SIZE   20 

int contrast = 55;

Adafruit_PCD8544 screen = Adafruit_PCD8544(sclkPin, sdinPin, dcPin, scePin, rstPin); 

// Keypad
const byte ROWS = 6; 
const byte COLS = 3; 
char keys[ROWS][COLS] = {
  {'!','U','?'},
  {'L','D','R'},
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
  {'*','0','#'}
};
byte rowPins[ROWS] = {A5, A4, A3, A2, A1, A0};
byte colPins[COLS] = {2, 1, 0};

Keypad keypad = Keypad( makeKeymap(keys), rowPins, colPins, ROWS, COLS );

enum Mode {
  NOMODE, 
  LOCKED, 
  HOME, 
  DIAL, 
};
Mode mode = LOCKED, prevmode;
bool initmode, back;

unsigned long lastKeyPressTime;

bool unlocking, blank;

void setup () 
{
  pinMode(FONA_RI, INPUT);
  pinMode(FONA_RST, OUTPUT);
  
  Serial.begin(115200);
  
  // turn on the display
  pinMode(blPin, OUTPUT);
  digitalWrite(blPin, HIGH);
  
  screen.begin(contrast);
  screen.clearDisplay();
  screen.setCursor(0,0);
  screen.display();
  
  screen.println("checking for");
  screen.println("FONA-");
  screen.display();
  
  // restart the SIM800H
  digitalWrite(FONA_RST, LOW);
  delay(50);
  digitalWrite(FONA_RST, HIGH);
  
  // see if the FONA is there
  fonaSerial->begin(4800);
  
  // see if the FONA is responding
  if (!fona.begin(*fonaSerial))
  {
    screen.println("FONA not found.");
    screen.println("reset the FONA");
    screen.println("or reupload the");
    screen.println("sketch.");
    screen.display();
    while(1);
  }
  screen.println("found FONA."); 
  screen.display(); 
  delay(300);
  
  screen.clearDisplay();
  screen.println("connecting-"); 
  screen.display(); 
  while (1) 
  {
    if (fona.getNetworkStatus() == 1) 
    {
      break; 
    }
    delay(250); 
  }
  screen.println("connected."); 
  screen.display(); 
  delay(300);

  screen.clearDisplay();
  screen.println("initializing-");
  screen.display();
  
  // set the audio channel to external mic and speaker
  fona.setAudio(FONA_EXTAUDIO);

  // get initial values
  signalQuality = fona.getRSSI();
  RSSI = getRSSI();
  batteryPercentage = getBatteryPercentage();
  cellDateTime();

  screen.println("initialized.");
  screen.display();
  delay(300);
  
  Serial.println("entering loop"); // for debugging purposes
}

void loop ()
{
  if (mode == LOCKED) digitalWrite(blPin, LOW);
  else digitalWrite(blPin, HIGH);

  char key = keypad.getKey();

  if (millis() - lastClockCheckTime > 60000)
  {
    cellDateTime();
    lastClockCheckTime = millis();  
  }

  screen.clearDisplay();
  screen.setCursor(0,0);
  screen.setTextColor(BLACK);

  callStatus = fona.getCallStatus();

  if (callStatus == 2) // unresponsive
  {
    connected = false; 
    callStatus = 0; // ready
    prevCallStatus = 2; 
  }
  
  switch (callStatus) 
  {
    case 0: // ready
      if (prevCallStatus != 2)
      {
        connected = true;
      }
      
      if (prevCallStatus == 3 || prevCallStatus == 4)
      {
        mode == HOME;
      }
      
      if ((mode == HOME || (mode == LOCKED && !unlocking)) && millis() - lastSignalQualityCheckTime > 1000)
      {
        signalQuality = fona.getRSSI();
        RSSI = getRSSI();
        lastSignalQualityCheckTime = millis();
      }

      initmode = (mode != prevmode) && !back;
      back = false;
      prevmode = mode;

      if ((mode == HOME || (mode == LOCKED && unlocking)) && connected)
      {
        screen.setTextColor(WHITE, BLACK); 
        screen.print("     ");
        screen.print(cellTime);
        screen.print("    ");

        screen.setTextColor(BLACK);

        if (signalQuality != 99)
        {
          for (int i = 1; i <= (signalQuality + 4) / 6; i++)
          {
            screen.drawFastVLine(i, 7 - i, i, WHITE);
          }
        }

        if (millis() - lastBatteryPercentageCheckTime > 60000)
        {
          batteryPercentage = getBatteryPercentage();
          lastBatteryPercentageCheckTime = millis();
        }

        screen.drawFastHLine(SCREEN_WIDTH * 6 - 5, 0, 3, WHITE); // top of battery
        screen.drawFastVLine(SCREEN_WIDTH * 6 - 6, 1, 6, WHITE); // left side of battery
        screen.drawFastVLine(SCREEN_WIDTH * 6 - 2, 1, 6, WHITE); // right side of battery
        for (int i = 0; i < map(batteryPercentage, 0, 100, 0, 6); i++)
        {
          screen.drawFastHLine(SCREEN_WIDTH * 6 - 5, 6 - i, 3, WHITE);
        }
        screen.print(RSSI);
        screen.print(" dBm");
      }

      if ((mode == HOME || (mode == LOCKED && unlocking)) && (!connected))
      {
        screen.setTextColor(WHITE, BLACK);
        screen.print("no signal     ");

        screen.setTextColor(BLACK);

        if (millis() - lastBatteryPercentageCheckTime > 60000)
        {
          batteryPercentage = getBatteryPercentage();
          lastBatteryPercentageCheckTime = millis();
        }

        screen.drawFastHLine(SCREEN_WIDTH * 6 - 5, 0, 3, WHITE); // top of battery
        screen.drawFastVLine(SCREEN_WIDTH * 6 - 6, 1, 6, WHITE); // left side of battery
        screen.drawFastVLine(SCREEN_WIDTH * 6 - 2, 1, 6, WHITE); // right side of battery
        for (int i = 0; i < map(batteryPercentage, 0, 100, 0, 6); i++)
        {
          screen.drawFastHLine(SCREEN_WIDTH * 6 - 5, 6 - i, 3, WHITE);
        }
      }

      if (mode == LOCKED)
      {
        if (initmode) 
        {
          unlocking = false;
          blank = false;
        }

        if (unlocking)
        {
          softKeys("unlock", "");
          if (key == 'L') { mode = HOME; unlocking = false; }
          if (millis() - lastKeyPressTime > 3000) unlocking = false;
          blank = false;
        }
        else
        {
          if (key)
          {
            screen.begin(contrast);
            unlocking = true;
            lastKeyPressTime = millis();
          }

          if (!blank)
          {
            screen.display(); // since there's no call to softKeys()
            blank = true;
          }
        }
      }
      else if (mode == HOME)
      {
        softKeys("lock", "");

        if ((key >= '0' && key <= '9') || key == '#')
        {
          lastKeyPressTime = millis();
          number[0] = key;
          number[1] = 0;
          mode = DIAL;
        }
        else if (key == 'L')
        {
          mode = LOCKED;
        }
      }
      else if (mode == DIAL)
      {
        numberInput(key, number, sizeof(number));
        softKeys("back", "call");

        if (key == 'L')
        {
          mode = HOME;
        }
        else if (key == 'R')
        {
          if (strlen(number) > 0)
          {
            name[0] = 0;
            fona.callPhone(number);
          }
        }
      }
      break;

    case 4: // call in progress
      if (prevCallStatus != 4)
      {
        fona.setVolume(callVolume);
      }
      
      screen.println("calling:");
      
      if (mode == DIAL) // if the number being called was just dialed by the user
      {
        screen.print(NAME_OR_NUMBER()); 
      }
      else
      {
        screen.println(incomingCallNumber);
      }
      
      softKeys("end", "");

      if (key == 'U' || key == 'D')
      {
        callVolume = constrain(callVolume + (key == 'U' ? 5 : -5), 0, 100);
        fona.setVolume(callVolume);
      }

      if (key == 'L')
      {
        fona.hangUp();
        mode = HOME;
      }
      break;

    case 3: // ringing
      if (prevCallStatus != 3)
      {
        blank = false;
        name[0] = 0;
        number[0] = 0;
        fona.setVolume(ringVolume);
      }
      incomingCallNumber = fona.incomingCallNumber(incomingCallNumber);
      Serial.println("incoming:");
      Serial.println(incomingCallNumber);
      screen.println("incoming:");
      screen.println(incomingCallNumber);
      softKeys("end", "answer");
      
      if (key == 'L')
      {
        mode = HOME;
        fona.hangUp();
      }

      if (key == 'R')
      {
        fona.pickUp();
      }
      break; 
  }
  prevCallStatus = callStatus;
  delay(10); // slow down the program
}

void softKeys(char *left, char *right)
{
  screen.setCursor(0, 40);
  screen.setTextColor(WHITE, BLACK);
  screen.print(left);
  screen.setTextColor(BLACK);
  for (int i = 0; i < SCREEN_WIDTH - strlen(left) - strlen(right); i++) screen.print(" ");
  screen.setTextColor(WHITE, BLACK);
  screen.print(right);
  screen.display();
}

int8_t getRSSI()
{
  uint8_t n = fona.getRSSI();
  int8_t r;
  
  if (n == 0) r = -115;
  if (n == 1) r = -111;
  if (n == 31) r = -52;
  if ((n >= 2) && (n <= 30)) r = map(n, 2, 30, -110, -54);
  
  return(r);
}

uint16_t getBatteryPercentage()
{
  uint16_t n;
  
  if (!fona.getBattPercent(&n));
  else return(n);
}

void numberInput(char key, char *buf, int len)
{
  int i = strlen(buf);
  
  if (i > 0 && (buf[i - 1] == '*' || buf[i - 1] == '#' || buf[i - 1] == '+') && millis() - lastKeyPressTime <= 1000) {
    for (int j = 0; j < strlen(buf) - 1; j++) screen.print(buf[j]);
    screen.setTextColor(WHITE, BLACK);
    screen.print(buf[strlen(buf) - 1]);
    screen.setTextColor(BLACK);
  } else {
    screen.print(buf);
    screen.setTextColor(WHITE, BLACK);
    screen.print(" ");
    screen.setTextColor(BLACK);
  }
  
  if (key >= '0' && key <= '9') {
    if (i < len - 1) { buf[i] = key; buf[i + 1] = 0; }
  }
  if (key == '*') {
    if (i > 0) { buf[i - 1] = 0; }
  }
  if (key == '#') {
    if (i > 0 && (buf[i - 1] == '*' || buf[i - 1] == '#' || buf[i - 1] == '+') && millis() - lastKeyPressTime <= 1000) {
      lastKeyPressTime = millis();
      if (buf[i - 1] == '#') buf[i - 1] = '*';
      else if (buf[i - 1] == '*') buf[i - 1] = '+';
      else if (buf[i - 1] == '+') buf[i - 1] = '#';
    } else {
      if (i < len - 1) { buf[i] = '#'; buf[i + 1] = 0; lastKeyPressTime = millis(); }
    }
  }
}

void cellDateTime()
{
  char buffer[23];
  fona.getTime(buffer, 23); // Make sure reply buffer is at least 23 bytes!
  String Str1 = buffer;
  
  String Str2;
  int trailingSpaces = 0;
  int month = Str1.substring(4,6).toInt();
  Str2.concat(month);
  Str2.concat("/");
  if (month < 10) trailingSpaces++;
  int day = Str1.substring(7,9).toInt();
  Str2.concat(day);
  Str2.concat("/");
  if (day < 10) trailingSpaces++;
  Str2.concat(Str1.substring(1,3));
  for (int i = 0; i < trailingSpaces; i++) Str2.concat(" ");
  cellDate = Str2;
  
  cellTime = Str1.substring(10,15);
}

