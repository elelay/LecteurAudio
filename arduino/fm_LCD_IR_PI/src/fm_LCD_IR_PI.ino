#include <Si4703_Breakout.h>
#include <Wire.h>
#include <IRremote.h>

#include <FastIO.h>
#include <I2CIO.h>
#include <LCD.h>
#include <LiquidCrystal_SR2W.h>
#include <LiquidCrystal_SR.h>
#include <LiquidCrystal_I2C.h>
#include <LiquidCrystal_SR3W.h>
#include <LiquidCrystal.h>
#include <EEPROM.h>

extern "C"
{
void *__dso_handle = NULL;
}
extern "C"
{
void *__cxa_atexit = NULL;
}


#define I2C_ADDR 0x27
#define BACKLIGHT_PIN 3
#define En_pin 2
#define Rw_pin 1
#define Rs_pin 0
#define D4_pin 4
#define D5_pin 5
#define D6_pin 6
#define D7_pin 7
LiquidCrystal_I2C lcd(I2C_ADDR,En_pin,Rw_pin,Rs_pin,D4_pin,D5_pin,D6_pin,D7_pin,BACKLIGHT_PIN,POSITIVE);
LCD *myLCD = &lcd;


int resetPin = 3;
int SDIO = A4;
int SCLK = A5;

Si4703_Breakout radio(resetPin, SDIO, SCLK);
int channel;
int volume;
char rdsBuffer[10];

typedef struct {
  int freq;
  const char* label;
} Station;

typedef enum {
  CLEAR,
  GO,
  PC,
  PS,
  OFF
} Cmd;


#define CMD_BUF_STR_LEN 10
String cmd_buf_str[CMD_BUF_STR_LEN];
char cmd_buf_str_avail = CMD_BUF_STR_LEN;
char cmd_buf_str_write = 0;

#define CMD_BUF_LEN 20
typedef struct {
  char c;
  char row;
  Cmd cmd;
} Command;
Command cmd_buf[CMD_BUF_LEN];
char cmd_buf_read, cmd_buf_write;
bool cmd_buf_full;
long lastAckedTime;


#define STATIONS_LENGTH 6
Station stations[STATIONS_LENGTH] = {
  { 935,  "FRANCE INTER"},
  { 1008, "RADIO RENNES"},
  { 940,  "CANAL B"},
  { 983,  "FRANCE CULTURE"},
  { 899,  "FRANCE MUSIQUE"},
  { 1055, "FRANCE INFO"}
};
int station = 0;
#define STATION_EEPROM_ADDR 29



#define BTN_HIGH_PIN 13
int isPressed = 0;
int isLongPressed = 0;
bool isRadioStopped = 0;
long lastPressedTime = 0;

#define IR_POWER 7
#define IR_RECV 6
IRrecv irrecv(IR_RECV);

#define RPI_LED_POWER 11
long rpi_off_time = 0;

typedef enum {
   S_WAITING,
   S_PI,
   S_PRINT_CHAR,
   S_PRINT_STRING,
   S_GO,
   S_CLEAR,
   S_CHAR,
   S_OFF,
   S_D_1_1,
   S_D_1_2,
   S_D_2_1,
   S_D_2_2,
   S_ERROR
} SerialState;

SerialState serialState = S_WAITING;

bool rpi = false;

void setup()
{
  int i;
  pinMode(IR_POWER, OUTPUT);
  digitalWrite(IR_POWER, HIGH);
  
  pinMode(BTN_HIGH_PIN, OUTPUT);
  digitalWrite(BTN_HIGH_PIN, LOW);
  
  pinMode(RPI_LED_POWER, OUTPUT);
  digitalWrite(RPI_LED_POWER, LOW);

  Serial.begin(9600);
  //inputString.reserve(30);

  for(i=0;i<CMD_BUF_STR_LEN;i++)
  {
    cmd_buf_str[i] = String();
    cmd_buf_str[i].reserve(17);
  }
  cmd_buf_read = cmd_buf_write = 0;
  cmd_buf_full = false;

  Serial.println("\n\nD: FM + LCD + IR");

  lcd.begin(16,2); // initialize the lcd
    
  station = loadStation();
  initRadio();
  Serial.println("D: Radio ON.");
    
  irrecv.enableIRIn();  // Start the receiver
  Serial.println("D: IR ON.");
  
  serialFlush();
}

void loop()
{
  long now;
  decode_results  results;
  String from_pi;
  int col, row;
  char c;
  bool ir_ok;

  now = millis();

  ir_ok = irrecv.decode(&results);
  if(ir_ok)
  {
    irrecv.resume();    
  }
  if(ir_ok && results.decode_type == NEC)
  {
    switch(results.value){
      case 0x1FE48B7:
        if(!rpi)
        {
          stopRadio();
        }
        Serial.println("IR: POWER");
        rpi = true;
        break;
      case 0x1FE58A7:
        Serial.println("IR: UP");
        break;
      case 0x1FE7887:
        Serial.println("IR: SETUP");
        break;
      case 0x1FE807F:
        Serial.println("IR: LEFT");
        break;
      case 0x1FE40BF:
        Serial.println("IR: ENTER");
        break;
      case 0x1FEC03F:
        Serial.println("IR: RIGHT");
        break;
      case 0x1FE20DF:
        Serial.println("IR: CARD");
        break;
      case 0x1FEA05F:
        Serial.println("IR: DOWN");
        break;
      case 0x1FE609F:
        Serial.println("IR: ROTATE");
        break;
      case 0x1FEE01F:
        if(!rpi)
        {
          station = (station+1) % STATIONS_LENGTH;
          saveStation(station);
          gotoStation();          
        }
        Serial.println("IR: PHOTO");
        break;
      case 0x1FE10EF:
        Serial.println("IR: SLIDE");
        break;
      case 0x1FE906F:
        Serial.println("IR: STOP");
        break;
      case 0x1FE50AF:
        Serial.println("IR: MUSIC");
        break;
      case 0x1FED827:
        Serial.println("IR: EXIT");
        if(rpi)
        {
          initRadio();
          rpi = false;
        }
        break;
      case 0x1FEF807:
        Serial.println("IR: VOL+");
        break;
      case 0x1FE30CF:
        Serial.println("IR: VIDEO");
        break;
      case 0x1FEB04F:
        Serial.println("IR: ZOOM");
      case 0x1FE708F:
        Serial.println("IR: VOL-");
        break;
      case 0xFFFFFFFF:
        break;
      default:
        Serial.print(results.value, HEX);
        Serial.println();
    }
  }
  
  serialEvent();
  if(cmd_buf_full || (cmd_buf_read != cmd_buf_write))
  {
     digitalWrite(RPI_LED_POWER, HIGH);
     
     Command cmd = cmd_buf[cmd_buf_read];
     Serial.print("ACK ");
     Serial.print((int)cmd.cmd);
     Serial.print(' ');
     Serial.print((int)cmd_buf_read);
     Serial.print(' ');
     Serial.print((int)cmd_buf_write);
     Serial.print(' ');
     lastAckedTime = now;
     if(cmd.cmd == PS)
     {
       from_pi = cmd_buf_str[cmd.c];
       Serial.print((int)cmd.c);
       Serial.print(' ');
       Serial.println(from_pi);
       lcd.print(from_pi);
       cmd_buf_str_avail++;
     }
     else if(cmd.cmd == PC)
     {
       lcd.print(cmd.c);
       Serial.println(cmd.c);
     }
     else if(cmd.cmd == CLEAR)
     {
       lcd.clear();
       Serial.println("CLEAR");
     }
     else if(cmd.cmd == OFF)
     {
       rpi_off_time = now;
       Serial.println("OFF");
     }
     else if(cmd.cmd == GO)
     {
       lcd.setCursor(cmd.c, cmd.row);
       Serial.print("GO ");
       Serial.print((int)cmd.c);
       Serial.print(' ');
       Serial.println((int)cmd.row);
     }
    
    cmd_buf_read = (cmd_buf_read + 1) % CMD_BUF_LEN;
    cmd_buf_full = false;
  } else {
    if( now - lastAckedTime > 5000)
    {
      Serial.println("ACK UNFREEZE");
      lastAckedTime = now;
    }
    if(rpi_off_time > 0 && (now - rpi_off_time > 10000))
    {
      Serial.println("RPI IS OFF");
      digitalWrite(RPI_LED_POWER, LOW);
      rpi_off_time = 0;
    }

  }
  delay(100);
}

void initRadio()
{
  radio.powerOn();
  radio.setVolume(15);
  gotoStation();

}

void stopRadio()
{
  digitalWrite(resetPin, LOW);
}

void gotoStation()
{
  radio.setChannel(stations[station].freq);
  lcd.clear();
  lcd.home (); // go home
  lcd.print(stations[station].label);
}

void serialFlush()
{
  while(Serial.available() > 0)
  {
    Serial.read();
  }
}

void serialEvent() {
  bool ok;
  while (Serial.available()) {
    // get the new byte:
    char inChar = (char)Serial.read();
    
    switch(serialState){

     case S_WAITING:
       if(inChar == 'P')
       {
         serialState = S_PI;
       }
       break;
     case S_PI:
       if(cmd_buf_full)
       {
          Serial.println("cmd_buf full");
          serialState = S_ERROR;
       }
       else
       {
         if(inChar == 'C')
         {
           serialState = S_PRINT_CHAR;
           cmd_buf[cmd_buf_write].cmd = PC;
         }
         else if(inChar == 'S')
         {
           if(cmd_buf_str_avail > 0)
           {
             serialState = S_PRINT_STRING;
             cmd_buf[cmd_buf_write].cmd = PS;
             cmd_buf[cmd_buf_write].c = cmd_buf_str_write;
             cmd_buf_str[cmd_buf_str_write].remove(0);
           }
           else
           {
             Serial.println("cmd_buf_str full");
             serialState = S_ERROR;
           }
         }
         else if(inChar == 'G')
         {
           serialState = S_GO;
           cmd_buf[cmd_buf_write].cmd = GO;
         }
         else if(inChar == 'L')
         {
           serialState = S_CLEAR;
           cmd_buf[cmd_buf_write].cmd = CLEAR;
         }
         else if(inChar == 'F')
         {
           serialState = S_OFF;
           cmd_buf[cmd_buf_write].cmd = OFF;
         }
         else
         {
           Serial.print("Invalid CMD ");
           Serial.println(inChar);
           serialState = S_ERROR;
         }
       }
       break;
     case S_PRINT_CHAR:
       cmd_buf[cmd_buf_write].cmd = PC;
       cmd_buf[cmd_buf_write].c = inChar;
       serialState = S_CHAR;
       break;
     case S_PRINT_STRING:
       if(inChar == '\n')
       {
         cmd_buf_str_write = (cmd_buf_str_write + 1) % CMD_BUF_STR_LEN;
         cmd_buf_str_avail--;
         validCmd();
         serialState = S_WAITING;
       }
       else
       {
         cmd_buf_str[cmd_buf_str_write] += inChar;
       }
       break;
     case S_GO:
       if(inChar >= '0' && inChar <= '9')
       {
         cmd_buf[cmd_buf_write].c = inChar - '0';
         serialState = S_D_1_1;
       }
       else
       {
         Serial.print("Invalid D1_1 ");
         Serial.println(inChar);
         serialState = S_ERROR;
       }
       break;
     case S_CLEAR:
     case S_OFF:
     case S_CHAR:
     case S_D_2_2:
       if(inChar == '\n')
       {
         validCmd();
         serialState = S_WAITING;
       }
       else
       {
         Serial.print("Invalid \\n ");
         Serial.println(inChar);
         serialState = S_ERROR;
       }
       break;
     case S_D_1_1:
       if(inChar >= '0' && inChar <= '9')
       {
         cmd_buf[cmd_buf_write].c = (inChar - '0') + cmd_buf[cmd_buf_write].c * 10;
         serialState = S_D_1_2;
       }
       else
       {
         Serial.print("Invalid D1_2 ");
         Serial.println(inChar);
         serialState = S_ERROR;
       }
       break;
     case S_D_1_2:
       if(inChar >= '0' && inChar <= '9')
       {
         cmd_buf[cmd_buf_write].row = inChar - '0';
         serialState = S_D_2_1;
       }
       else
       {
         Serial.print("Invalid D2_1 ");
         Serial.println(inChar);
         serialState = S_ERROR;
       }
       break;
     case S_D_2_1:
       if(inChar >= '0' && inChar <= '9')
       {
         cmd_buf[cmd_buf_write].row = (inChar - '0') + cmd_buf[cmd_buf_write].row * 10;
         serialState = S_D_2_2;
//         Serial.print("GO ");
//         Serial.print((int)cmd_buf[cmd_buf_write].c);
//         Serial.print(' ');
//         Serial.print(cmd_buf[cmd_buf_write].row);
//         Serial.println(inChar);
       }
       else
       {
         Serial.print("Invalid D2_2 ");
         Serial.println(inChar);
         serialState = S_ERROR;
       }
       break;
     case S_ERROR:
     default:
       if(inChar == '\n')
       {
         serialState = S_WAITING;
       }
    }
  }
}

void validCmd()
{
  Serial.print((int)cmd_buf_write);
  Serial.print(" VALID ");
  Serial.println(cmd_buf[cmd_buf_write].cmd);
  cmd_buf_write = (cmd_buf_write + 1) % CMD_BUF_LEN;
  cmd_buf_full = cmd_buf_write == cmd_buf_read;
}

int loadStation()
{
  byte value;
  
  value = EEPROM.read(STATION_EEPROM_ADDR);

  return (int)value;
}

void saveStation(int new_station)
{
  byte value;

  value = (byte) new_station;
  EEPROM.update(STATION_EEPROM_ADDR, value);
}

