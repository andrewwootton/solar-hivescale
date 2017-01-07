/* aw_autonomo_Hivescale14.3
   14/12/16 latest 5/1/17
   starts at 1 min intervals to serial USB then switches to hourly on serial1(D0/D1) 
   NB don't connect 5v pin from USBSerial adapter as this alters battery voltage and affects HX711
   fixed wait for serialusb
   2 scales connected via separate HX711 amplifiers with 3G modem  TPH v2
   logs data to SD card
   includes debug to set interrupts every minute 
   alternatively when debug off interrupts every hour
   starts with GPRS call to update RTC from Internet Time Server
*/ 

#include <RTCZero.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Sodaq_3Gbee.h>
#include <SPI.h>
#include <SD.h>
#include <HX711.h>


//************************************************************************
//******requires separate calibration and zero factors for each scale******  
//these need changing to appropriate values obtained from calibration sketch
#define calibration_factor -22200.0 //This value is obtained using the SparkFun_HX711_Calibration sketch
#define zero_factor 8345082 //This large value is obtained using the SparkFun_HX711_Calibration sketch
#define calibration_factor2 -21200.0 //This value is obtained using the SparkFun_HX711_Calibration sketch
#define zero_factor2 8334572 //This large value is obtained using the SparkFun_HX711_Calibration sketch

//******ThingSpeak constants******
//this needs to be set to the write api key for your channel  
#define URL ""
#define WRITE_API_KEY "XXXXXXXXXXX" //channel key for Autonomo 
//*****************************************************************************



#define DOUT  10  //use D10/D11 always on Grove connector for connection to HX711 #1
#define CLK  11
#define DOUT2  4  //use D4/D5 always on Grove connector for connection to HX711 #2
#define CLK2  5

HX711 scale(DOUT, CLK);
HX711 scale2(DOUT2, CLK2);

#define MY_BEE_VCC  BEE_VCC

#define DATA_HEADER "Date/Time, SHT Temp, BMP Temp, Pressure, Humidity, Weight1 Kg, Weight2 Kg, Battery mV"   //Data header
#define FILE_NAME "DataLog.txt"    //The data log file

//Network constants
#define APN "internet"
#define APN_USERNAME ""
#define APN_PASSWORD ""

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
#define LABEL7 "field7"

//These constants are used for reading the battery voltage
#define ADC_AREF 3.3
#define BATVOLTPIN BAT_VOLT 
#define BATVOLT_R1 4.7
#define BATVOLT_R2 10

//TPH V2 sensor
#define BME_SCK 13
#define BME_MISO 12
#define BME_MOSI 11
#define BME_CS 10
#define SEALEVELPRESSURE_HPA (1013.25)
Adafruit_BME280 bme; // I2C

RTCZero rtc;
#define ALARM_TRIGGER1 0
#define ALARM_TRIGGER2 1

String url = URL;  //need this here so it is global
volatile boolean flag; //used for interrupt service routine
int initialcounter = 0;

void setup()//////////////////////////////////////////////////
{
  Serial.begin(57600);  //only outputs to serial if you connect a USB-Serial adapter on D0/D1
 
    while((!SerialUSB)&& (millis() < 5000));  //Wait for Serial or 5 seconds ; 
    SerialUSB.println();
    SerialUSB.println("\n\nWaiting for initial 10 sec delay");
    SerialUSB.println("\n\nHivescale will take readings every min for 10 readings then hourly");
    SerialUSB.println("Will perform a RTC sync via GPRS first");
    
    Serial.println();
    Serial.println("\n\nWaiting for initial 10 sec delay");
    Serial.println("\n\nHivescale will take readings every min for 10 readings then hourly");
    Serial.println("Will perform a RTC sync via GPRS first");
  
  delay(10000); //startup delay - leave this in for program reload on restart
 
  setupAlarms();
  setupSensors();
  setupComms();
  setupSleep();
  setupLogfile(); 
  syncRTC();         //sync the RTC immediately  
}
//=============================================end void setup loop

void loop()//////////////////////////////////////////////////////
{       
    if (initialcounter <10)
    {
      initialcounter++;         //increment counter
      SerialUSB.print("initialcounter = ");
      SerialUSB.println(initialcounter);
    }
    
    
    if (initialcounter < 10)
         {
      enterNosleep();    //call nosleep routine
         }
    else {
      enterSleep();    //call sleep routine
         }
    
    // on waking immediately take a reading and send/store 
    String url = createDataURL();          //for GPRS upload to ThingSpeak
    sendData(url);                         //send to GPRS
    String dataRec = createDataRecord();  //prepare for save to SD card
    logData(dataRec);                     //save to SD card
  
   if (flag){            //check if a RTCsync update due
    syncRTC();          //update RTC
    flag = false;      //clear flag
  }
  
  delay(2000);  // Stay awake for two seconds
}
//=============================================================end void loop

void setupAlarms()////////////////////////////////////////////////////////
{
  rtc.begin(); 
  rtc.setAlarmMinutes(ALARM_TRIGGER1);           //trigger every hour at 0 min mark
  rtc.enableAlarm(RTCZero::MATCH_MMSS);          //match on the hour
       
}
//=========================================================end setup alarms

void  setupSensors()///////////////////////////////////////////////////////
{ 
  Wire.begin();   //Initialise the wire protocol for the TPH sensors   
  bme.begin();  //Initialise the TPH V2 BME280 sensor
  scale.set_scale(calibration_factor); //This value is obtained by using the SparkFun_HX711_Calibration sketch
  scale.set_offset(zero_factor); //Zero out the scale using a previously known zero_factor
  scale2.set_scale(calibration_factor2); //This value is obtained by using the SparkFun_HX711_Calibration sketch
  scale2.set_offset(zero_factor2); //Zero out the scale using a previously known zero_factor
}
//===================================================end setup sensors

void  setupComms()///////////////////////////////////////////////
{
   pinMode(13, OUTPUT);  // Set LED pin as output
   Serial1.begin(sodaq_3gbee.getDefaultBaudrate());
   //sodaq_3gbee.setDiag(SerialUSB); // optional (don't use with serial2 !)  
   sodaq_3gbee.init(Serial1, BEE_VCC, BEEDTR, BEECTS);
   sodaq_3gbee.setApn(APN, APN_USERNAME, APN_PASSWORD); 
}
//=====================================================end setup comms

void setupSleep()//////////////////////////////////////////////////
{
  SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;  // Set sleep mode
  rtc.attachInterrupt(RTC_ISR);       // Attach ISR
}
//=====================================================end setup sleep

void setupLogfile()////////////////////////////////////////////////////
{ 
    if (!SD.begin(SS_2)){   //Initialise the SD card 
       
       if (initialcounter < 10)
            {
       SerialUSB.println("Error: SD card failed to initialise or is missing. Program hang");
            }
       else {
       Serial.println("Error: SD card failed to initialise or is missing. Program hang");
            }
       
       while (true); //Hang 
    }
   
    bool oldFile = SD.exists(FILE_NAME);   //Check if the file already exists
    File logFile = SD.open(FILE_NAME, FILE_WRITE);  //Open the file in write mode
   
    if (!oldFile){
       logFile.println(DATA_HEADER);  //Add header information if the file did not already exist
    }
    logFile.close();   //Close the file to save it
    SPI.end();         //close the serial peripheral interface as this draws current 
}
//================================================end setupLogfile

void logData(String rec)/////////////////////////////////////////////
{ 
    SPI.begin(); //need to start the serial peripheral interface as we now normally keep it off
    File logFile = SD.open(FILE_NAME, FILE_WRITE);  //Re-open the file   
    logFile.println(rec);                           //Write the CSV data  
    logFile.close();                               //Close the file to save it
    SPI.end();         //close the serial peripheral interface as this draws current 
}
//===============================================end logData

String createDataRecord()///////////////////////////////////////
{
    //Create a String type data record in csv format
    //TimeDate, TempSHT21, TempBMP, PressureBMP, HumiditySHT21, Weight 1 , Weight2, BatterymV
    String data = getDateTime() + ", ";
     data += String(bme.readTemperature())  + ", ";
    data += String(bme.readPressure() / 100.0F) + ", ";
    data += String(bme.readAltitude(SEALEVELPRESSURE_HPA))  + ", ";
    data += String(bme.readHumidity()) + ", ";
    scale.power_up();
      data += String(scale.get_units()) + ", ";
    scale.power_down();
    scale2.power_up();
      data += String(scale2.get_units()) + ", ";
    scale2.power_down();
    data += String(getRealBatteryVoltageMV());

    if (initialcounter < 10)
         {
      SerialUSB.print("\nDataRecord = ");
      SerialUSB.println(data);
         }
    else {
      Serial.print("\nDataRecord = ");
      Serial.println(data);
         }

      
  return data;
}
//==============================================end createDataRecord

String createDataURL()////////////////////////////////////////////
{
    //Construct data URL
    url = URL;
  
    //Add key followed by each field
    url += String(FIRST_SEP) + String("key");
    url += String(LABEL_DATA_SEP) + String(WRITE_API_KEY);
 
    url += String(OTHER_SEP) + String(LABEL1);
    url += String(LABEL_DATA_SEP) + String(bme.readTemperature());
 
    url += String(OTHER_SEP) + String(LABEL2);
    url += String(LABEL_DATA_SEP) + String(bme.readPressure()/100.0F);
 
    url += String(OTHER_SEP) + String(LABEL3);
    url += String(LABEL_DATA_SEP) + String(bme.readAltitude(SEALEVELPRESSURE_HPA));
 
    url += String(OTHER_SEP) + String(LABEL4);
    url += String(LABEL_DATA_SEP) + String(bme.readHumidity());

    url += String(OTHER_SEP) + String(LABEL5);
    scale.power_up();
    url += String(LABEL_DATA_SEP) + String(scale.get_units());    
    scale.power_down();

    url += String(OTHER_SEP) + String(LABEL6);
    scale2.power_up();
    url += String(LABEL_DATA_SEP) + String(scale2.get_units());    
    scale2.power_down();
    
    url += String(OTHER_SEP) + String(LABEL7);
    url += String(LABEL_DATA_SEP) + String(getRealBatteryVoltageMV());    

    if (initialcounter < 10)
         {
    SerialUSB.print("\n\nURL constructed= ");
    SerialUSB.println(url);  
         }
    else {
    Serial.print("\n\nURL constructed= ");
    Serial.println(url);  
         }
   
    return url;  
}
//=========================================end createDataURL

float getRealBatteryVoltageMV()///////////////////////////
{
  uint16_t batteryVoltage = analogRead(BATVOLTPIN);
  return (ADC_AREF / 1.023) * (BATVOLT_R1 + BATVOLT_R2) / BATVOLT_R2 * batteryVoltage;
}
//================================end getRealBatteryVoltageMV loop

void RTC_ISR()////////////////////////////////////////
{
if (initialcounter < 10)
  { 
  SerialUSB.println("*****************entered Interrupt Service Routine*******************");
  // Shouldn't really do much (eg print) in ISR but this is only a debug
  SerialUSB.println(getDateTime());
  }

else  {  //in non-debug mode ISR is kept to a minimum 
         if (rtc.getHours() == ALARM_TRIGGER2)   //check to see if RTCsync due (at 0100)
            {
              flag=true;   //set flag for RTCsync
            }
      }
}
//===============================================end RTC_ISR

void enterSleep()/////////////////////////////////////////
{  
  USB->DEVICE.CTRLA.reg &= ~USB_CTRLA_ENABLE;  // Disable USB
  __WFI();    //Enter sleep mode
 
  // ...Sleep
   
  USB->DEVICE.CTRLA.reg |= USB_CTRLA_ENABLE;  // Enable USB
}
//===========================================end enterSleep

void enterNosleep()////////////////////////////////////
{     
    SerialUSB.println("Nosleep delay 60 sec");
    delay(60000);
    SerialUSB.println("Returning from Nosleep");
}
//================================================end Nosleep

void sendData(String url)//////////////////////////////
{  
sodaq_3gbee.on();
  if (sodaq_3gbee.connect()) {
        Serial.println("\nModem connected to the apn successfully.");        
        blinkLED1();
    }
    else {
         Serial.println("\nModem failed to connect to the apn!");
         blinkLED2();
    }
        //char httpBuffer[1024];
        char httpBuffer[2048];
        Serial.print("\n\nurl.c_str()= ");
        Serial.println(url.c_str());
        
        Serial.print("\n\nsizeof(httpBuffer)= ");
        Serial.println(sizeof(httpBuffer));
        
        size_t size = sodaq_3gbee.httpRequest("api.thingspeak.com", 80, url.c_str(), GET, httpBuffer, sizeof(httpBuffer));
        printToLen(httpBuffer, size);           
        delay(3000);
          if (sodaq_3gbee.disconnect()) {          
             Serial.println("Modem was successfully disconnected.");    
            }
          else{
             Serial.println("Modem not successfully disconnected.");
            }       
    sodaq_3gbee.off();
}
//================================================end sendData

void printToLen(const char* buffer, size_t len)////////////////
{ 
    Serial.println("Modem response (httpBuffer) =");
    for (size_t i = 0; i < len; i++) {
         Serial.print(buffer[i]);
    }
         Serial.println();
}
//====================================end  printToLen


void syncRTC()/////////////////////////////////////////
{   
   sodaq_3gbee.on();
   if (sodaq_3gbee.connect()) {
            Serial.println("\r\nModem connected to the apn successfully.\r\n");        
            blinkLED1();
    }
    else {
         
            Serial.println("\r\nModem failed to connect to the apn!\r\n");        
            blinkLED2();
    }
    char buffer[1024];
    memset(buffer, '\0', sizeof(buffer));
    bool retval = sodaq_3gbee.httpRequest(TIME_URL, 80, "/",  GET, buffer, sizeof(buffer));
    
    Serial.print("GET result: "); 
    Serial.println(retval);   // 1 if successful
    Serial.print("buffer: ");
    Serial.println(buffer); 
    
 if (retval){  //only change time if successfully retrieved from Internet Time Server  
     char *ptr = strstr(buffer, "\r\n\r\n"); //move pointer to end of html header
     uint32_t newTs = 0;
     if (ptr != NULL) { //make sure that the substring was found
          newTs = strtoul(ptr, NULL, 0);  //convert time signal to long integer
     }       
     newTs += 3 + TIME_ZONE_SEC;  //Add the timezone difference plus a few seconds to compensate for transmission and processing delay
     rtc.setEpoch(newTs);   //now change to new (local) Time
     Serial.println("Time is now: " + getDateTime());
  }
    if (sodaq_3gbee.disconnect()) {          
            Serial.println("\r\nModem disconnected successfully.\r\n");        
    }
    else{
            Serial.println("\r\nModem not disconnected.\r\n");                     
    }    
   sodaq_3gbee.off();
}
//=============================================end syncRTC

void blinkLED1()///////////////////////////////////////////
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
//==================================================

void blinkLED2()/////////////////////////////////////
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
//===============================================

String getDateTime()/////////////////////////////
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
//======================end getDateTime loop
