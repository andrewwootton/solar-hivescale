/* aw_autonomo_Hivescale5.1
   25/5/16 latest 13/8/16
   updated to TPH2
   power down/up hx711
   changed to serial2 output as using SerialUSB crashes with sleep
   removed gprson/off and switch off/on SPI to reduce power consumption
   includes TPH and logs data to SD card
   structured with sleep
   includes debug to set interrupts every minute alternatively when debug off interrupts every hour
   sends via GPRS for data and both routines start with a GPRS call to update RTC from Internet Time Server
*/ 

//#define DEBUG 1  //debug sets read/send every minute with hourly RTC sync
// comment DEBUG out for hourly reads with RTC sync daily - ie normal production use

#include <RTCZero.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <GPRSbee.h>
#include <SPI.h>
#include <SD.h>
#include <HX711.h>

#define calibration_factor -21500.0 //This value is obtained using the SparkFun_HX711_Calibration sketch
#define zero_factor 8391842 //This large value is obtained using the SparkFun_HX711_Calibration sketch

#define DOUT  10  //use D0/D1 always on Grove connector for connection to HX711
#define CLK  11

HX711 scale(DOUT, CLK);

#define DATA_HEADER "Date/Time, SHT Temp, BMP Temp, Pressure, Humidity, Weight Kg,  Battery mV"   //Data header
#define FILE_NAME "DataLog.txt"    //The data log file

//Network constants
#define APN "internet"
#define APN_USERNAME ""
#define APN_PASSWORD ""

#define TIME_URL "time.sodaq.net"
#define TIME_ZONE 10.0  //set for Melbourne time
#define TIME_ZONE_SEC (TIME_ZONE * 3600)

//SpeakThings constants
#define URL "api.thingspeak.com/update"
#define WRITE_API_KEY "DC8CH35UJ1HSYMI2" //channel key for Autonomo

//Seperators
#define FIRST_SEP "?"
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

void setup()////////////////////////////////////////////////////////
{
  Serial2.begin(9600);
 
  #ifdef DEBUG
    while(!SerialUSB); 
    SerialUSB.println();
    SerialUSB.println("Hivescale in debug mode (readings every min) - wait for 10 sec delay");
    SerialUSB.println("Will perform a RTC sync via GPRS first");
   #else 
    Serial2.println();
    Serial2.println("Hivescale in non-debug mode (readings every hour) - wait for 10 sec delay");
    Serial2.println("Will perform a RTC sync via GPRS first");
  #endif
  setupAlarms();
  setupSensors();
  setupComms();
  setupSleep();
  setupLogfile(); 
  syncRTC();         //sync the RTC immediately 
   
  delay(10000); //startup delay - leave this in for program reload on restart
}
//////////////////////////end void setup loop///////////////////////

void loop()///////////////////////////////////////////////////////
{   
    
    #ifdef DEBUG
    SerialUSB.println("Going to nosleep");
    enterNosleep();    //call sleep routine
    SerialUSB.println("Returning and taking reading");
    #else
    Serial2.println("Going to sleep");
    enterSleep();    //call sleep routine
    Serial2.println("Waking and taking reading");
    #endif
    
    // on waking immediately take a reading and send/store 
    String url = createDataURL();          //for GPRS upload to ThingSpeak
    sendData(url);                         //send to GPRS
    String dataRec = createDataRecord();  //prepare for save to SD card
    logData(dataRec);                     //save to SD card
  

  blinkLED();           //set LED blinking to show when awake

  if (flag){            //check if a RTCsync update due
    syncRTC();          //update RTC
    flag = false;      //clear flag
  }
  
  delay(2000);  // Stay awake for two seconds
}
////////////////////////////end void loop///////////////////////////////////////////////////

void setupLogfile()//////////////////////////////////////////////////////////////////////////
{ 
    if (!SD.begin(SS_2)){   //Initialise the SD card 
       
       #ifdef DEBUG
       SerialUSB.println("Error: SD card failed to initialise or is missing. Program hang");
       #else
       Serial2.println("Error: SD card failed to initialise or is missing. Program hang");
       #endif
       
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
/////////////////////////////////end setupLogfile////////////////////////////////////////////

void logData(String rec)////////////////////////////////////////////////////////////////////
{ 
    SPI.begin(); //need to start the serial peripheral interface as we now normally keep it off
    File logFile = SD.open(FILE_NAME, FILE_WRITE);  //Re-open the file   
    logFile.println(rec);                           //Write the CSV data  
    logFile.close();                               //Close the file to save it
    SPI.end();         //close the serial peripheral interface as this draws current 
}
//////////////////////////////////////////end logData//////////////////////////////////////////

String createDataRecord()//////////////////////////////////////////////////////////////////////
{
    //Create a String type data record in csv format
    //TimeDate, TempSHT21, TempBMP, PressureBMP, HumiditySHT21
    String data = getDateTime() + ", ";
    data += String(bme.readTemperature())  + ", ";
    data += String(bme.readPressure() / 100.0F) + ", ";
    data += String(bme.readAltitude(SEALEVELPRESSURE_HPA)) + ", ";
    data += String(bme.readHumidity()) + ", ";
    scale.power_up();
    data += String(scale.get_units()) + ", ";
    scale.power_down();
    data += String(getRealBatteryVoltageMV());

    #ifdef DEBUG
      SerialUSB.println(data);
    #else
      Serial2.println(data);
    #endif

      
  return data;
}
////////////////////////////////////////////end createDataRecord//////////////////////////////////////

String createDataURL()////////////////////////////////////////////////////////////////////////
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
    url += String(LABEL_DATA_SEP) + String(getRealBatteryVoltageMV());    

    #ifdef DEBUG
    SerialUSB.println(url);  
    #else
    Serial2.println(url);  
    #endif

     
    return url;  
}
//////////////////////////end createDataURL/////////////////////////////////////////////////////

float getRealBatteryVoltageMV()///////////////////////////////////////////////////////////////////
{
  uint16_t batteryVoltage = analogRead(BATVOLTPIN);
  return (ADC_AREF / 1.023) * (BATVOLT_R1 + BATVOLT_R2) / BATVOLT_R2 * batteryVoltage;
}
////////////////////////////////////////////////end getRealBatteryVoltageMV loop/////////////////

void setupAlarms()////////////////////////////////////////////////////////////////////////////////
{
  rtc.begin();
 
 #ifdef DEBUG                                    //when debugging use minutes otherwise hourly
  rtc.setAlarmSeconds(ALARM_TRIGGER1);            //trigger every minute at 0sec mark
  rtc.enableAlarm(RTCZero::MATCH_SS);            //use this one for every minute 
 #else
  rtc.setAlarmMinutes(ALARM_TRIGGER1);           //trigger every hour at 0min mark
  rtc.enableAlarm(RTCZero::MATCH_MMSS);        //use this one for every hour
  //rtc.enableAlarm(RTCZero::MATCH_SS);        //temp every minute test 
 #endif 
}
///////////////////////////////////////end setup alarms//////////////////////////////////

void  setupSensors()////////////////////////////////////////////////////////////////////
{ 
  Wire.begin();   //Initialise the wire protocol for the TPH sensors   
  bme.begin();  //Initialise the TPH V2 BME280 sensor
  scale.set_scale(calibration_factor); //This value is obtained by using the SparkFun_HX711_Calibration sketch
  scale.set_offset(zero_factor); //Zero out the scale using a previously known zero_factor
}
///////////////////////////////////end setup sensors//////////////////////////////////////

void  setupComms()///////////////////////////////////////////////////////////////////////
{
  pinMode(13, OUTPUT);  // Set LED pin as output
  Serial1.begin(57600);
  gprsbee.initAutonomoSIM800(Serial1, BEE_VCC, BEEDTR, BEERTS);  //NB this order is for rev 3, ?rev4 onwards needs RTS, DTR?
  // gprsbee.setDiag(SerialUSB); //Uncomment to debug the GPRSbee with the serial monitor 
}
/////////////////////////////////////////end setup comms//////////////////////////

void setupSleep()///////////////////////////////////////////////////////////
{
  SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;  // Set sleep mode
  rtc.attachInterrupt(RTC_ISR);       // Attach ISR
}
//////////////////////////////////////end setup sleep/////////////////////////

void RTC_ISR()////////////////////////////////////////////////////
{
#ifdef DEBUG  
if(rtc.getMinutes()== ALARM_TRIGGER2)  //check to see if RTCsync due (at 1min past)
{
  flag=true;   //set flag for RTCsync
}
  SerialUSB.println("*****************entered Interrupt Service Routine*******************");
  // Shouldn't really do much (eg print) in ISR but this is only a debug
  SerialUSB.println(getDateTime());

#else    //in non-debug mode ISR is kept to a minimum 
if (rtc.getHours() == ALARM_TRIGGER2)   //check to see if RTCsync due (at 0100)
{
 flag=true;   //set flag for RTCsync
  }
#endif
}
/////////////////////////////////////end RTC_ISR///////////////////////////////////

void enterSleep()///////////////////////////////////////////////////////////////////
{  
  USB->DEVICE.CTRLA.reg &= ~USB_CTRLA_ENABLE;  // Disable USB
  __WFI();    //Enter sleep mode
 
  // ...Sleep
   
  USB->DEVICE.CTRLA.reg |= USB_CTRLA_ENABLE;  // Enable USB
}
////////////////////////end enterSleep//////////////////////////////////////////////////

void sendData(String url)/////////////////////////////////////////////////////////////////////////
{
   //gprsbee.on();   //on waking send url with readings to GPRS
   char result[20] = "";
   gprsbee.doHTTPGET(APN, APN_USERNAME, APN_PASSWORD, url.c_str(), result, sizeof(result));
   Serial2.println("GPRS send result = " + String(result));
  // gprsbee.off();
}
/////////////////////////////////end sendData////////////////////////////////////

void enterNosleep()///////////////////////////////////////////////////////////////////
{ 
    String url = createDataURL();          //so we can see readings
    String dataRec = createDataRecord();  //prepare for save to SD card
    logData(dataRec);                     //save to SD card

    SerialUSB.print("URL String = ");
    SerialUSB.println(url.c_str());
    SerialUSB.print("Datarecord String = ");
    SerialUSB.println(dataRec); 
    SerialUSB.println();

    delay(10000);
}
///////////////////////////////////////end Nosleep//////////////////////////////////////

void syncRTC()/////////////////////////////////////////////////////////////////////////
{
    
    char buffer[512];
    memset(buffer, '\0', sizeof(buffer));
    bool retval = gprsbee.doHTTPGET(APN, APN_USERNAME, APN_PASSWORD, 
    TIME_URL, buffer, sizeof(buffer));
    #ifdef DEBUG
    SerialUSB.print("GET result: ");
    SerialUSB.println(retval);
    #else
    Serial2.print("GET result: ");
    Serial2.println(retval);
    #endif
   
      
   if (retval){  //only change time if successfully retrieved from Internet Time Server     
       char *ptr;  //Convert the time stamp to unsigned long
       uint32_t newTs = strtoul(buffer, &ptr, 0);
       newTs += 3 + TIME_ZONE_SEC;  //Add the timezone difference plus a few seconds to compensate for transmission and processing delay

        #ifdef DEBUG
        SerialUSB.print("newTs: ");
        SerialUSB.println(newTs);
        #else
        Serial2.print("newTs: ");
        Serial2.println(newTs);
        #endif


        
        rtc.setEpoch(newTs);   //now change to new (local) Time  
    }
}
/////////////////////////////////end syncRTC/////////////////////////////////////////////

void blinkLED()////////////////////////////////////////////////////////////////////////
{
  digitalWrite(13, HIGH); // LED on
  delay(1000);
  digitalWrite(13, LOW); // LED off
}
///////////////////////////////////end blinkLED////////////////////////////////////////

String getDateTime()//////////////////////////////////////////////////////////////////
{
  String dateTimeStr;

  dateTimeStr = String(rtc.getDay())+ "/";
  dateTimeStr +=String(rtc.getMonth())+ "/";
  dateTimeStr +=String(rtc.getYear())+ " ";
  dateTimeStr +=String(rtc.getHours())+ ":";
  dateTimeStr +=String(rtc.getMinutes())+ ":";
  dateTimeStr +=String(rtc.getSeconds());

  return dateTimeStr;
}///////////////////////////////end getDateTime loop/////////////////////////////////////
