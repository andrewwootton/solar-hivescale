/* aw_autonomo_Hivescale12.0
   2/8/16
   debug cleaned up to switch mySerial
   Ublox 3G modem and logs data to SD card 
   httpget works now that 3Gbee library has been updated to ignore unsolicited reponses
   uses serial output as using SerialUSB crashes with sleep
   power down/up hx711 and SPI to reduce power consumption
   includes debug to set interrupts every minute alternatively when debug off interrupts every hour
   both routines start with a GPRS call to update RTC from Internet Time Server
*/ 

// Comment DEBUG out for hourly reads with daily RTC sync  - ie normal production use
#define DEBUG 1  //debug sets read/send every minute with hourly RTC sync

// Use 1 or other of these next 2 for the serial output
#define mySerial SerialUSB     //OK to use this with debug but not with runmode as sleep crashes the serialUSB
//#define mySerial Serial  //use this without debug - but you will only see output if you connect a USB-Serial adapter on D0/D1

// ThingSpeak constants
#define URL ""
#define WRITE_API_KEY "VU1PON7FLKUTI5GC" //channel key for Autonomo

// Factors for calibrating and zeroing scale - input values previously obtained
#define calibration_factor -21500.0 //This value is obtained using the SparkFun_HX711_Calibration sketch
#define zero_factor 8350277 //This large value is obtained using the SparkFun_HX711_Calibration sketch

#include <RTCZero.h>
#include <Wire.h>
#include <Sodaq_BMP085.h>
#include <Sodaq_SHT2x.h> 
#include <Sodaq_3Gbee.h>
#include <SPI.h>
#include <SD.h>
#include <HX711.h>

// Define which pins the scale is connected to
#define DOUT 10  //use D10/D11 always on Grove connector for connection to HX711
#define CLK  11
HX711 scale(DOUT, CLK);

#define DATA_HEADER "Date/Time, SHT Temp, BMP Temp, Pressure, Humidity, Weight Kg,  Battery mV"   //Data header
#define FILE_NAME "DataLog.txt"    //The data log file

//Network constants
#define APN "internet"
#define APN_USER ""
#define APN_PASS ""
#define TIME_URL "time.sodaq.net"
#define TIME_ZONE 10.0  //set for Melbourne time
#define TIME_ZONE_SEC (TIME_ZONE * 3600)

//Seperators
#define FIRST_SEP "/update?"
#define OTHER_SEP "&"
#define LABEL_DATA_SEP "="
#define separator "," //this one is for the SD card file

//Data labels, cannot change for ThingSpeak
#define LABEL1 "field1"
#define LABEL2 "field2"
#define LABEL3 "field3"
#define LABEL4 "field4"
#define LABEL5 "field5"
#define LABEL6 "field6"

//These constants are used for reading the battery voltage
#define ADC_AREF 3.3
#define BATVOLTPIN BAT_VOLT 
#define BATVOLT_R1 4.7
#define BATVOLT_R2 10

//TPH BMP sensor
Sodaq_BMP085 bmp; 

RTCZero rtc;
#define ALARM_TRIGGER1 0  //this one is for the minutes trigger which fires on the hour
#define ALARM_TRIGGER2 1  //this one is used once a day at 0100

String url = URL;  //need this here so it is global
volatile boolean Flag; //used for interrupt service routine

void setup()////////////////////////////////////////////////////////
{ 
    mySerial.begin(57600); //Serial(D0/D1) is used for comms when sleep enabled (requires a FTDI-type UART-USB converter) 
    while((!mySerial) && (millis() < 5000));  //Wait for Serial or 5 seconds 
    #ifdef DEBUG
       mySerial.println("\n\nHivescale in debug mode (readings every min) with RTC sync via GPRS first");
    #else
       mySerial.println("\n\nHivescale in run mode (readings every hour) with RTC sync via GPRS first");
    #endif
    mySerial.println("Wait for initial 10 sec delay");

    delay(10000); //10 sec startup delay - leave this in to allow program reload on restart
  
    setupAlarms();
    setupSensors();
    setupComms();
    setupSleep();
    setupLogfile(); 
    syncRTC();         //sync the RTC immediately   
}
//=======================================================================

void loop()///////////////////////////////////////////////////////
{      
    #ifdef DEBUG
       enterNosleep();    //call nosleep routine
    #else
       enterSleep();    //call sleep routine
    #endif
    
 //on waking immediately take a reading and send/store 
    String url = createDataURL();          //for GPRS upload to ThingSpeak
    sendData(url);                         //send to GPRS
    String dataRec = createDataRecord();  //prepare for save to SD card
    logData(dataRec);                     //save to SD card

  if (Flag){            //check if a RTCsync update due
     syncRTC();          //update RTC
     Flag = false;      //clear Flag
  }
  
  delay(2000);  // Stay awake for two seconds
}
//====================================================================

void setupAlarms()/////////////////////////////////////////
{
   rtc.begin();                                 
   #ifdef DEBUG 
      rtc.setAlarmSeconds(ALARM_TRIGGER1);        //trigger at 0sec mark
      rtc.enableAlarm(RTCZero::MATCH_SS);         //use this to fire every minute
   #else
      rtc.setAlarmMinutes(ALARM_TRIGGER1);        //trigger at 0min mark
      rtc.enableAlarm(RTCZero::MATCH_MMSS);       //use this to fire every hour 
   #endif  
}
//==========================================================

void  setupSensors()///////////////////////////////////////////////////////
{ 
  Wire.begin();   //Initialise the wire protocol for the TPH sensors   
  bmp.begin();  //Initialise the TPH BMP sensor
  scale.set_scale(calibration_factor); //This value is obtained by using the SparkFun_HX711_Calibration sketch
  scale.set_offset(zero_factor); //Zero out the scale using a previously known zero_factor
}
//==================================================================================

void setupComms()//////////////////////
{
    pinMode(13, OUTPUT);  // Set LED pin as output
    Serial1.begin(sodaq_3gbee.getDefaultBaudrate());
    delay(500);   //? necessary
    //sodaq_3gbee.setDiag(Serial); // optional verbose modem responses   
    sodaq_3gbee.init(Serial1, BEE_VCC, BEEDTR, BEECTS);
    delay(500);   //? necessary
    sodaq_3gbee.setApn(APN, APN_USER, APN_PASS); 
    delay(500);   //? necessary
}
//=====================================================

void setupSleep()///////////////////////////////////////////////////////////
{
   SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;  // Set sleep mode
   rtc.attachInterrupt(RTC_ISR);       // Attach ISR
}
//======================================================================

void setupLogfile()///////////////////////////////////////////////////////////
{ 
    if (!SD.begin(SS_2)){   //Initialise the SD card         
       mySerial.println("Error: SD card failed to initialise or is missing. Program hang");    
       while (true); //Hang if no SD card present
    }
   
    bool oldFile = SD.exists(FILE_NAME);   //Check if the file already exists
    File logFile = SD.open(FILE_NAME, FILE_WRITE);  //Open the file in write mode
   
    if (!oldFile){
       logFile.println(DATA_HEADER);  //Add header information if the file did not already exist
    }
    logFile.close();   //Close the file to save it
    SPI.end();         //close the serial peripheral interface as this draws current 
}
//==============================================================================

void RTC_ISR()////////////////////////////////////////////////////
{
   #ifdef DEBUG  
      if(rtc.getMinutes()== ALARM_TRIGGER2)  //check to see if RTCsync due (at 1min past the hour)
        {
         Flag=true;   //set Flag for RTCsync
        }
      mySerial.println("*****************Interrupt Service Routine*******************");
      // Shouldn't really do much (eg print) in ISR but this is only a debug
      mySerial.println(getDateTime());

   #else    //in non-debug mode ISR is kept to a minimum 
       if (rtc.getHours() == ALARM_TRIGGER2)   //check to see if RTCsync due (at 0100)
       {
         Flag=true;   //set Flag for RTCsync
       }
   #endif
}
//======================================================================

void enterSleep()/////////////////////////////////////////////////////////
{  
   mySerial.println("Going to sleep");
   mySerial.flush();
   USB->DEVICE.CTRLA.reg &= ~USB_CTRLA_ENABLE;  // Disable USB
   delay(1000);
  
   __WFI();    //Enter sleep mode
   
   USB->DEVICE.CTRLA.reg |= USB_CTRLA_ENABLE;  // Enable USB
   delay(1000);
   mySerial.println("Waking");
}
//===========================================================================

void enterNosleep()////////////////////////////////////////////////////////////
{ 
    mySerial.println("Nosleep delay 60 sec");
    delay(60000);
    mySerial.println("Returning from Nosleep");
}
//========================================================================

String createDataURL()///////////////////////////////////////////////////////////////
{
    //Construct data URL
    url = URL;
  
    //Add key followed by each field
    url += String(FIRST_SEP) + String("key");
    url += String(LABEL_DATA_SEP) + String(WRITE_API_KEY);
 
    url += String(OTHER_SEP) + String(LABEL1);
    url += String(LABEL_DATA_SEP) + String(SHT2x.GetTemperature());
 
    url += String(OTHER_SEP) + String(LABEL2);
    url += String(LABEL_DATA_SEP) + String(bmp.readTemperature());
 
    url += String(OTHER_SEP) + String(LABEL3);
    url += String(LABEL_DATA_SEP) + String(bmp.readPressure() / 100);
 
    url += String(OTHER_SEP) + String(LABEL4);
    url += String(LABEL_DATA_SEP) + String(SHT2x.GetHumidity());

    url += String(OTHER_SEP) + String(LABEL5);
    scale.power_up();
    url += String(LABEL_DATA_SEP) + String(scale.get_units());    
    scale.power_down();
    
    url += String(OTHER_SEP) + String(LABEL6);
    url += String(LABEL_DATA_SEP) + String(getRealBatteryVoltageMV());    

    mySerial.println(url);    
    return url;  
}
//=======================================================================

void sendData(String url)///////////////////////////////////
{  
   sodaq_3gbee.on();
   if (sodaq_3gbee.connect()) {    
            mySerial.println("\r\nModem connected to the apn successfully.\r\n");                    
            blinkLED1();
        }
   else {       
            mySerial.println("\r\nModem failed to connect to the apn!\r\n");               
            blinkLED2();
        }
        char httpBuffer[1024];
        size_t size = sodaq_3gbee.httpRequest("api.thingspeak.com", 80, url.c_str(), GET, httpBuffer, sizeof(httpBuffer));
        printToLen(httpBuffer, size);           
        delay(3000);
          if (sodaq_3gbee.disconnect()) {          
             mySerial.println("Modem was successfully disconnected.");    
            }
          else{
             mySerial.println("Modem not successfully disconnected.");
            }       
    sodaq_3gbee.off();
}
//===========================================================

void printToLen(const char* buffer, size_t len)////////////////////////////
{ 
    mySerial.println("Modem response (httpBuffer) =");
    for (size_t i = 0; i < len; i++) {
         mySerial.print(buffer[i]);
    }
         mySerial.println();
}
//========================================================

String createDataRecord()/////////////////////////////////////////////
{
    //Create a String type data record in csv format for SD card storage
    //TimeDate, TempSHT21, TempBMP, PressureBMP, HumiditySHT21, Weight, Battery Volts
    String data = getDateTime() + ", ";
    data += String(SHT2x.GetTemperature())  + ", ";
    data += String(bmp.readTemperature()) + ", ";
    data += String(bmp.readPressure() / 100)  + ", ";
    data += String(SHT2x.GetHumidity()) + ", ";
    scale.power_up();    //HX711 draws significant power
    data += String(scale.get_units()) + ", ";
    scale.power_down();  //HX711 draws significant power
    data += String(getRealBatteryVoltageMV());
   
    mySerial.println(data);    
    return data;
}
//===========================================================================

void logData(String rec)///////////////////////////////////////////////
{ 
    SPI.begin(); //start the serial peripheral interface as normally kept off
    File logFile = SD.open(FILE_NAME, FILE_WRITE);  //Re-open the file   
    logFile.println(rec);                           //Write the CSV data  
    logFile.close();                               //Close the file to save it
    SPI.end();         //close the serial peripheral interface as this draws power 
}
//=====================================================================================

float getRealBatteryVoltageMV()///////////////////////////////////////////////
{
  uint16_t batteryVoltage = analogRead(BATVOLTPIN);
  return (ADC_AREF / 1.023) * (BATVOLT_R1 + BATVOLT_R2) / BATVOLT_R2 * batteryVoltage;
}
//==============================================================================

void syncRTC()///////////////////////////////////////////////////////////////
{   
   sodaq_3gbee.on();
   if (sodaq_3gbee.connect()) {
            mySerial.println("\r\nModem connected to the apn successfully.\r\n");        
            blinkLED1();
    }
    else {
         
            mySerial.println("\r\nModem failed to connect to the apn!\r\n");        
            blinkLED2();
    }
    char buffer[1024];
    memset(buffer, '\0', sizeof(buffer));
    bool retval = sodaq_3gbee.httpRequest(TIME_URL, 80, "/",  GET, buffer, sizeof(buffer));
    
    mySerial.print("GET result: "); 
    mySerial.println(retval);   // 1 if successful
    mySerial.print("buffer: ");
    mySerial.println(buffer); 
    
 if (retval){  //only change time if successfully retrieved from Internet Time Server  
     char *ptr = strstr(buffer, "\r\n\r\n"); //move pointer to end of html header
     uint32_t newTs = 0;
     if (ptr != NULL) { //make sure that the substring was found
          newTs = strtoul(ptr, NULL, 0);  //convert time signal to long integer
     }       
     newTs += 3 + TIME_ZONE_SEC;  //Add the timezone difference plus a few seconds to compensate for transmission and processing delay
     rtc.setEpoch(newTs);   //now change to new (local) Time
     mySerial.println("Time is now: " + getDateTime());
  }
    if (sodaq_3gbee.disconnect()) {          
            mySerial.println("\r\nModem disconnected successfully.\r\n");        
    }
    else{
            mySerial.println("\r\nModem not disconnected.\r\n");                     
    }    
   sodaq_3gbee.off();
}
//===============================================================

String getDateTime()//////////////////////////////////////
{
  String dateTimeStr;
  dateTimeStr = String(rtc.getDay())+ "/";
  dateTimeStr +=String(rtc.getMonth())+ "/";
  dateTimeStr +=String(rtc.getYear())+ " ";
  dateTimeStr +=String(rtc.getHours())+ ":";
  dateTimeStr +=String(rtc.getMinutes())+ ":";
  dateTimeStr +=String(rtc.getSeconds());
  return dateTimeStr;
}
//===============================================================

void blinkLED1()////////////////////////////////////////////////////
{                      //indicate success with slow increasing blink
  digitalWrite(13, HIGH); // LED on
  delay(500);
  digitalWrite(13, LOW); // LED off
  delay(500);
  digitalWrite(13, HIGH); // LED on
  delay(1000);
  digitalWrite(13, LOW); // LED off
  delay(500);
  digitalWrite(13, HIGH); // LED on
  delay(2000);
  digitalWrite(13, LOW); // LED off
  delay(500);
  digitalWrite(13, HIGH); // LED on
  delay(3000);
  digitalWrite(13, LOW); // LED off
}
//==============================================================

void blinkLED2()////////////////////////////////////////////////////
{                         //indicate failure with rapid even blink
  digitalWrite(13, HIGH); // LED on
  delay(250);
  digitalWrite(13, LOW); // LED off
  delay(250);
  digitalWrite(13, HIGH); // LED on
  delay(250);
  digitalWrite(13, LOW); // LED off
  delay(250);
  digitalWrite(13, HIGH); // LED on
  delay(250);
  digitalWrite(13, LOW); // LED off
  delay(250);
  digitalWrite(13, HIGH); // LED on
  delay(250);
  digitalWrite(13, LOW); // LED off
}
//==============================================================
