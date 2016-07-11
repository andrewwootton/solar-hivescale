/* aw_autonomo_Hivescale8
   8/7/16 latest 11/7/16
   use this one to work on
   works with serial 2 provided verbose modem is off and does not use alarm - delay 60min instead
   uses 3G ublox modem  2nd attempt at getting this right! Now working fork from 7
   power down/up hx711
   changed to serial2 output as using SerialUSB crashes with sleep
   removed gprson/off and switch off/on SPI to reduce power consumption
   includes TPH and logs data to SD card
   structured with sleep
   includes debug to set interrupts every minute alternatively when debug off interrupts every hour
   sends via GPRS for data and both routines start with a GPRS call to update RTC from Internet Time Server
*/ 

#include <Sodaq_3Gbee.h>
#include <RTCZero.h>

#define MySerial Serial2
#define MY_BEE_VCC  BEE_VCC

#define modemSerial Serial1

#define APN "internet"
#define APN_USER NULL
#define APN_PASS NULL

//SpeakThings constants
#define URL ""
#define WRITE_API_KEY "VU1PON7FLKUTI5GC" //channel key for Autonomo Batterytest channel

//Seperators
#define FIRST_SEP "/update?"
#define OTHER_SEP "&"
#define LABEL_DATA_SEP "="
#define separator "," //this one is for the SD card file

//Data labels, cannot change for ThingSpeak
#define LABEL1 "field1"
//#define LABEL2 "field2"
//#define LABEL3 "field3"
//#define LABEL4 "field4"
//#define LABEL5 "field5"
//#define LABEL6 "field6"

//These constants are used for reading the battery voltage
#define ADC_AREF 3.3
#define BATVOLTPIN BAT_VOLT 
#define BATVOLT_R1 4.7
#define BATVOLT_R2 10

String url = URL;  //need this here so it is global

RTCZero rtc;
#define ALARM_TRIGGER1 0
#define ALARM_TRIGGER2 1
//=========================================================

void setup()////////////////////////
{
   MySerial.begin(57600);
   while (!MySerial) { }  // wait for Serial to initialize
   MySerial.println("==========================");
   MySerial.println("Startup delay - wait 10sec");
   delay(10000); //startup delay - leave this in for program reload on restart
  // setupAlarms();
   setupComms(); 
 //  setupSleep(); 
}
//===================================

void loop()////////////////////////////////////////
{
  
  enterSleep();    //call sleep routine
  
  String url = createDataURL();
  MySerial.println("sending to modem " + String(url) );
  sendData(url); 
  MySerial.println("now entering 60 min delay");
  delay(3600000); // 1 hour delay before next loop
  MySerial.println("continuing after 60 min delay");
}
//====================================================

void setupAlarms()/////////////////////////////////////////
{
  rtc.begin();                                 
  rtc.setAlarmSeconds(ALARM_TRIGGER1);            //trigger every minute at 0sec mark
  rtc.enableAlarm(RTCZero::MATCH_SS);            //use this one for every minute 
}
//==========================================================

void setupComms()//////////////////////
{
    modemSerial.begin(sodaq_3gbee.getDefaultBaudrate());
    delay(500);
    MySerial.println("**Bootup**");
    sodaq_3gbee.setDiag(MySerial); // optional (don't use with serial2 !)   
    sodaq_3gbee.init(Serial1, BEE_VCC, BEEDTR, BEECTS);
    delay(500);
    sodaq_3gbee.setApn(APN, APN_USER, APN_PASS); 
}
//=====================================================

void setupSleep()///////////////////////////////////////////////////////////
{
  SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;  // Set sleep mode
  rtc.attachInterrupt(RTC_ISR);       // Attach ISR
}
//======================================================================

void RTC_ISR()////////////////////////////////////////////////////
{
   MySerial.println("**********entered Interrupt Service Routine**************");
}
//=======================================================================

void enterSleep()/////////////////////////////////////////////////////////
{  
  MySerial.println("Going to sleep");
  MySerial.flush();
  //USB->DEVICE.CTRLA.reg &= ~USB_CTRLA_ENABLE;  // Disable USB
  delay(1000);
  
  //__WFI();    //Enter sleep mode
   
 // USB->DEVICE.CTRLA.reg |= USB_CTRLA_ENABLE;  // Enable USB
  delay(1000);
  MySerial.println("Waking");
}
//===========================================================================

String createDataURL()/////////////////////////////////////////////
{
    //Construct data URL
    url = URL;
  
    //Add key followed by each field
    url += String(FIRST_SEP) + String("key");
    url += String(LABEL_DATA_SEP) + String(WRITE_API_KEY);
 
//    url += String(OTHER_SEP) + String(LABEL1);
 //   url += String(LABEL_DATA_SEP) + String(SHT2x.GetTemperature());
 
 //   url += String(OTHER_SEP) + String(LABEL2);
 //   url += String(LABEL_DATA_SEP) + String(bmp.readTemperature());
 
 //   url += String(OTHER_SEP) + String(LABEL3);
 //   url += String(LABEL_DATA_SEP) + String(bmp.readPressure() / 100);
 
 //  url += String(OTHER_SEP) + String(LABEL4);
 //   url += String(LABEL_DATA_SEP) + String(SHT2x.GetHumidity());

  //  url += String(OTHER_SEP) + String(LABEL5);
 //   scale.power_up();
 //   url += String(LABEL_DATA_SEP) + String(scale.get_units());    
 //   scale.power_down();
    
    url += String(OTHER_SEP) + String(LABEL1);
    url += String(LABEL_DATA_SEP) + String(getRealBatteryVoltageMV());    

   MySerial.println("url = " + String(url));
    return url;  
}
//===========================/end createDataURL==================================

void sendData(String url)///////////////////////////////////
{
sodaq_3gbee.on();
  if (sodaq_3gbee.connect()) {
        MySerial.println();
        MySerial.println("Modem connected to the apn successfully.");
        MySerial.println();
         }
    else {
         MySerial.println();
         MySerial.println("Modem failed to connect to the apn!");
         MySerial.println();
    }
        char httpBuffer[1024];
        size_t size = sodaq_3gbee.httpRequest("api.thingspeak.com", 80, url.c_str(), GET, httpBuffer, sizeof(httpBuffer));
        printToLen(httpBuffer, size);
             
        delay(3000);
          if (sodaq_3gbee.disconnect()) {
                MySerial.println("Modem was successfully disconnected.");
            }
            else{
              MySerial.println("Modem not successfully disconnected.");
            }       
    sodaq_3gbee.off();
}
//=========================================

float getRealBatteryVoltageMV()//////////////////////////////////////
{
  uint16_t batteryVoltage = analogRead(BATVOLTPIN);
  return (ADC_AREF / 1.023) * (BATVOLT_R1 + BATVOLT_R2) / BATVOLT_R2 * batteryVoltage;
}
//===========================================================

void printToLen(const char* buffer, size_t len)///////////
{
    MySerial.println("Modem response (httpBuffer) =");
    for (size_t i = 0; i < len; i++) {
         MySerial.print(buffer[i]);
    }
         MySerial.println();
}
//========================================================
