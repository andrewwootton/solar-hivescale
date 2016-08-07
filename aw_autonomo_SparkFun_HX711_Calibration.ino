/*
 aw_autonomo_SparkFun_HX711_Calibration
 25/5/16
 changed to DOUT 0 and CLK 1
 changed Serial to SerialUSB
 changed to kg
 changed adjustments to +/-1000 instead of 10
 changed starting calibrtion factor to 1,250,000 to get into ballpark
 
 Example using the SparkFun HX711 breakout board with a scale
 By: Nathan Seidle
 SparkFun Electronics
 Date: November 19th, 2014
 License: This code is public domain but you buy me a beer if you use this and we meet someday (Beerware license).
 
 This is the calibration sketch. Use it to determine the calibration_factor that the main example uses. It also
 outputs the zero_factor useful for projects that have a permanent mass on the scale in between power cycles.
 
 Setup your scale and start the sketch WITHOUT a weight on the scale
 Once readings are displayed place the weight on the scale
 Press +/- or a/z to adjust the calibration_factor until the output readings match the known weight
 Use this calibration_factor on the example sketch
 
 This example assumes pounds (lbs). If you prefer kilograms, change the SerialUSB.print(" lbs"); line to kg. The
 calibration factor will be significantly different but it will be linearly related to lbs (1 lbs = 0.453592 kg).
 
 Your calibration factor may be very positive or very negative. It all depends on the setup of your scale system
 and the direction the sensors deflect from zero state

 This example code uses bogde's excellent library: https://github.com/bogde/HX711
 bogde's library is released under a GNU GENERAL PUBLIC LICENSE

 Arduino pin 2 -> HX711 CLK
 3 -> DOUT
 5V -> VCC
 GND -> GND
 
 Most any pin on the Arduino Uno will be compatible with DOUT/CLK.
 
 The HX711 board can be powered from 2.7V to 5V so the Arduino 5V power should be fine.
 
*/

#include "HX711.h"

#define DOUT  4//10
#define CLK  5//11

HX711 scale(DOUT, CLK);

float calibration_factor = -43500.0; //-7050 worked for my 440lb max scale setup

void setup() {
  while(!SerialUSB);
  
  SerialUSB.println("HX711 calibration sketch");
  SerialUSB.println("Remove all weight from scale");
  SerialUSB.println("After readings begin, place known weight on scale");
  SerialUSB.println("Press + or a to increase calibration factor");
  SerialUSB.println("Press - or z to decrease calibration factor");

  scale.set_scale();
  scale.tare();	//Reset the scale to 0

  long zero_factor = scale.read_average(); //Get a baseline reading
  SerialUSB.print("Zero factor: "); //This can be used to remove the need to tare the scale. Useful in permanent scale projects.
  SerialUSB.println(zero_factor);
}

void loop() {
  scale.set_scale(calibration_factor); //Adjust to this calibration factor

  SerialUSB.print("Reading: ");
  SerialUSB.print(scale.get_units(), 1);
  SerialUSB.print(" kg"); //Change this to kg and re-adjust the calibration factor if you follow SI units like a sane person
  SerialUSB.print(" calibration_factor: ");
  SerialUSB.print(calibration_factor);
  SerialUSB.println();

  if(SerialUSB.available())
  {
    char temp = SerialUSB.read();
    if(temp == '+' || temp == 'a')
      calibration_factor += 1000;
    else if(temp == '-' || temp == 'z')
      calibration_factor -= 1000;
  }
}
