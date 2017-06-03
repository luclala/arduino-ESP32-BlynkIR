/*************************************************************
  Download latest Blynk library here:
    https://github.com/blynkkk/blynk-library/releases/latest

  Blynk is a platform with iOS and Android apps to control
  Arduino, Raspberry Pi and the likes over the Internet.
  You can easily build graphic interfaces for all your
  projects by simply dragging and dropping widgets.

    Downloads, docs, tutorials: http://www.blynk.cc
    Sketch generator:           http://examples.blynk.cc
    Blynk community:            http://community.blynk.cc
    Follow us:                  http://www.fb.com/blynkapp
                                http://twitter.com/blynk_app

  Blynk library is licensed under MIT license
  This example code is in public domain.

 *************************************************************

  App project setup:
    Time Input widget on V1 with only start time option.
 *************************************************************/

/* Comment this out to disable prints and save space */
#define BLYNK_PRINT Serial
#define GPIO_LED          GPIO_NUM_27 /*LED output pin 27 */

#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <TimeLib.h>
#include <WidgetRTC.h>

// You should get Auth Token in the Blynk App.
// Go to the Project Settings (nut icon).
char auth[] = "8162e03d9a8a47aba8d38af3c2453302";

// Your WiFi credentials.
// Set password to "" for open networks.
char ssid[] = "Poutineville";
char pass[] = "poutinesquicksquick";

BlynkTimer timer;
WidgetRTC rtc;
WidgetLED led1(V2);

String startTime, stopTime;
int startHour, startMin;
int stopHour, stopMin;
boolean AC_ON = false;

//Function that gets start time from Blynk app
BLYNK_WRITE(V0) {
  TimeInputParam t(param);
  startHour = t.getStartHour();
  startMin = t.getStartMinute();
  startTime = String(t.getStartHour()) + ":" + String(t.getStartMinute());
  Serial.println(String("Start time: ") + startTime);
}

//Function that gets stop time from Blynk app
BLYNK_WRITE(V1) {
  TimeInputParam t(param);
  stopHour = t.getStartHour();
  stopMin = t.getStartMinute();
  stopTime = String(t.getStartHour()) + ":" + String(t.getStartMinute());
  Serial.println(String("Stop time: ") + stopTime);
}

// Digital clock display of the time
void clockDisplay()
{
  // You can call hour(), minute(), ... at any time
  // Please see Time library examples for details

  String currentTime = String(hour()) + ":" + minute();
  int currentHour = hour();
  int currentMin = minute();
  //String currentDate = String(day()) + " " + month() + " " + year();
  Serial.print("Current time: ");
  Serial.println(currentTime);
  //Serial.print(currentDate);
  //Serial.println();

  // Send time to the App
  Blynk.virtualWrite(V3, currentTime);
  // Send date to the App
  //Blynk.virtualWrite(V2, currentDate);

  //
  if(startHour == currentHour && startMin == currentMin && AC_ON == false){
    digitalWrite(GPIO_LED, HIGH);
    led1.on();
    Serial.println("IR command sent");
    AC_ON = true;
  }

  else if(stopHour == currentHour && stopMin == currentMin && AC_ON == true){
    digitalWrite(GPIO_LED, LOW);
    led1.off();
    Serial.println("IR command sent");
    AC_ON = false;
  }
 
}

void setup()
{
  // Debug console
  Serial.begin(115200);
  pinMode(GPIO_LED, OUTPUT); 

  Blynk.begin(auth, ssid, pass);
  // You can also specify server:
  //Blynk.begin(auth, ssid, pass, "blynk-cloud.com", 8442);
  //Blynk.begin(auth, ssid, pass, IPAddress(192,168,1,100), 8442);

  // Begin synchronizing time
  rtc.begin();
  timer.setInterval(10000L, clockDisplay);

  led1.off();
}

void loop()
{
  Blynk.run();
  timer.run();
  
}
