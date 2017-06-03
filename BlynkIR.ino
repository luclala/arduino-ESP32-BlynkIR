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
    Time input widget linked with V0 for Start time
    Time input widget linked with V1 for Stop time
    Value display linked to V3 to display the current time
    LED linked to V2 to display when AC is ON
    RTC widget 

  Program description:
    This Blynk app is used to control an AC unit with IR signal
    Works with a ESP32 dev board and IR LED
    The IR LED is connected to IO26 of the ESP32
    With the Blynk app you set the start and stop time
    and the program will send the start/stop IR command when the time comes

  Author
    L-A Boulanger
    
 *************************************************************/

//Includes
#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <TimeLib.h>
#include <WidgetRTC.h>
#include "driver/rmt.h"
#include "driver/periph_ctrl.h"
#include "soc/rmt_reg.h"


//Infrared remote peripheral defines
#define RMT_TX_CARRIER_EN    1   /*!< Enable carrier for IR transmitter test with IR led */
#define RMT_CARRIER_FREQ          38750
#define RMT_CARRIER_DUTY          45
#define RMT_TX_CHANNEL    RMT_CHANNEL_1     /*!< RMT channel for transmitter */
#define RMT_TX_GPIO_NUM   GPIO_NUM_26    /*!< GPIO number for transmitter signal */
#define RMT_CLK_DIV      100    /*!< RMT counter clock divider */
#define RMT_TICK_10_US    (80000000/RMT_CLK_DIV/100000)   /*!< RMT counter value for 10 us.(Source clock is APB clock) */

#define NEC_HEADER_HIGH_US    3200                         /*!< TECO protocol header: pulses for 3.2ms */
#define NEC_HEADER_LOW_US     1600                         /*!< TECO protocol header: low for 1.6ms*/
#define NEC_BIT_ONE_HIGH_US    400                         /*!< TECO protocol data bit 1: positive 400us */
#define NEC_BIT_ONE_LOW_US    1200                         /*!< TECO protocol data bit 1: negative 1200us */
#define NEC_BIT_ZERO_HIGH_US   400                         /*!< TECO protocol data bit 0: positive 400us */
#define NEC_BIT_ZERO_LOW_US    400                         /*!< TECO protocol data bit 0: negative 400us */
#define NEC_BIT_END              0                         /*!< NEC protocol end: */
#define NEC_BIT_MARGIN          20                         /*!< NEC parse margin time */

#define NEC_ITEM_DURATION(d)  ((d & 0x7fff)*10/RMT_TICK_10_US)  /*!< Parse duration time from memory register value */
#define NEC_DATA_ITEM_NUM   74  /*!< NEC code item number: header + 32bit data + end */
#define RMT_TX_DATA_NUM  1    /*!< NEC tx test data number */
#define rmt_item32_tIMEOUT_US  9500   /*!< RMT receiver timeout value(us) */


#define BLYNK_PRINT   Serial
#define GPIO_LED      GPIO_NUM_27 /*LED output pin 27 */



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

//IR functions
//Build register value of waveform for NEC one data bit
static inline void nec_fill_item_level(rmt_item32_t* item, int high_us, int low_us)
{
  item->level0 = 1;
  item->duration0 = (high_us) / 10 * RMT_TICK_10_US;
  item->level1 = 0;
  item->duration1 = (low_us) / 10 * RMT_TICK_10_US;
}


//Generate NEC header value
static void nec_fill_item_header(rmt_item32_t* item)
{
  nec_fill_item_level(item, NEC_HEADER_HIGH_US, NEC_HEADER_LOW_US);
}


//Generate NEC data bit 1
static void nec_fill_item_bit_one(rmt_item32_t* item)
{
  nec_fill_item_level(item, NEC_BIT_ONE_HIGH_US, NEC_BIT_ONE_LOW_US);
}


//Generate NEC data bit 0
static void nec_fill_item_bit_zero(rmt_item32_t* item)
{
  nec_fill_item_level(item, NEC_BIT_ZERO_HIGH_US, NEC_BIT_ZERO_LOW_US);
}


//Generate NEC end signal
static void nec_fill_item_end(rmt_item32_t* item)
{
  nec_fill_item_level(item, NEC_BIT_END, 0x7fff);
}



//Build NEC 32bit waveform.
static int nec_build_items(int channel, rmt_item32_t* item)
{

  //1st byte 00110000
  nec_fill_item_header(item++);
  nec_fill_item_bit_zero(item++);
  nec_fill_item_bit_zero(item++);
  nec_fill_item_bit_one(item++);
  nec_fill_item_bit_one(item++);
  nec_fill_item_bit_zero(item++);
  nec_fill_item_bit_zero(item++);
  nec_fill_item_bit_zero(item++);
  nec_fill_item_bit_zero(item++);

  //2nd byte 11111111
  nec_fill_item_bit_one(item++);
  nec_fill_item_bit_one(item++);
  nec_fill_item_bit_one(item++);
  nec_fill_item_bit_one(item++);
  nec_fill_item_bit_one(item++);
  nec_fill_item_bit_one(item++);
  nec_fill_item_bit_one(item++);
  nec_fill_item_bit_one(item++);

  //3rd byte 01011111
  nec_fill_item_bit_zero(item++);
  nec_fill_item_bit_one(item++);
  nec_fill_item_bit_zero(item++);
  nec_fill_item_bit_one(item++);
  nec_fill_item_bit_one(item++);
  nec_fill_item_bit_one(item++);
  nec_fill_item_bit_one(item++);
  nec_fill_item_bit_one(item++);

  //4th byte 00111111
  nec_fill_item_bit_zero(item++);
  nec_fill_item_bit_zero(item++);
  nec_fill_item_bit_one(item++);
  nec_fill_item_bit_one(item++);
  nec_fill_item_bit_one(item++);
  nec_fill_item_bit_one(item++);
  nec_fill_item_bit_one(item++);
  nec_fill_item_bit_one(item++);

  //5th byte 00011111
  nec_fill_item_bit_zero(item++);
  nec_fill_item_bit_zero(item++);
  nec_fill_item_bit_zero(item++);
  nec_fill_item_bit_one(item++);
  nec_fill_item_bit_one(item++);
  nec_fill_item_bit_one(item++);
  nec_fill_item_bit_one(item++);
  nec_fill_item_bit_one(item++);

  //6th byte 00111010
  nec_fill_item_bit_zero(item++);
  nec_fill_item_bit_zero(item++);
  nec_fill_item_bit_one(item++);
  nec_fill_item_bit_one(item++);
  nec_fill_item_bit_one(item++);
  nec_fill_item_bit_zero(item++);
  nec_fill_item_bit_one(item++);
  nec_fill_item_bit_zero(item++);

  //7th byte 00011001
  nec_fill_item_bit_zero(item++);
  nec_fill_item_bit_zero(item++);
  nec_fill_item_bit_zero(item++);
  nec_fill_item_bit_one(item++);
  nec_fill_item_bit_one(item++);
  nec_fill_item_bit_zero(item++);
  nec_fill_item_bit_zero(item++);
  nec_fill_item_bit_one(item++);

  //8th byte 00100000
  nec_fill_item_bit_zero(item++);
  nec_fill_item_bit_zero(item++);
  nec_fill_item_bit_one(item++);
  nec_fill_item_bit_zero(item++);
  nec_fill_item_bit_zero(item++);
  nec_fill_item_bit_zero(item++);
  nec_fill_item_bit_zero(item++);
  nec_fill_item_bit_zero(item++);

  //9th byte 10000000
  nec_fill_item_bit_one(item++);
  nec_fill_item_bit_zero(item++);
  nec_fill_item_bit_zero(item++);
  nec_fill_item_bit_zero(item++);
  nec_fill_item_bit_zero(item++);
  nec_fill_item_bit_zero(item++);
  nec_fill_item_bit_zero(item++);
  nec_fill_item_bit_zero(item++);

  nec_fill_item_bit_zero(item++);
  /*
    i++;
    for(j = 0; j < 16; j++) {
      if(cmd_data & 0x1) {
          nec_fill_item_bit_one(item);
      } else {
          nec_fill_item_bit_zero(item);
      }
      item++;
      i++;
      cmd_data >>= 1;
    }
  */
  nec_fill_item_end(item);

}


//RMT transmitter initialization
static void nec_tx_init()
{
  rmt_config_t rmt_tx;
  rmt_tx.channel = RMT_TX_CHANNEL;
  rmt_tx.gpio_num = RMT_TX_GPIO_NUM;
  rmt_tx.mem_block_num = 1;
  rmt_tx.clk_div = RMT_CLK_DIV;
  rmt_tx.tx_config.loop_en = false;
  rmt_tx.tx_config.carrier_duty_percent = RMT_CARRIER_DUTY;
  rmt_tx.tx_config.carrier_freq_hz = RMT_CARRIER_FREQ;
  rmt_tx.tx_config.carrier_level = RMT_CARRIER_LEVEL_HIGH;
  rmt_tx.tx_config.carrier_en = RMT_TX_CARRIER_EN;
  rmt_tx.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
  rmt_tx.tx_config.idle_output_en = true;
  rmt_tx.rmt_mode = RMT_MODE_TX;
  rmt_config(&rmt_tx);
  rmt_driver_install(rmt_tx.channel, 0, 0);
}



//RMT transmitter demo, this task will periodically send NEC data. (100 * 32 bits each time.)
static void rmt_example_nec_tx_task() //void *pvParameters
{
  nec_tx_init();

  int nec_tx_num = RMT_TX_DATA_NUM;

  size_t size = (sizeof(rmt_item32_t) * NEC_DATA_ITEM_NUM * nec_tx_num);
  //each item represent a cycle of waveform.
  rmt_item32_t* item = (rmt_item32_t*) malloc(size);
  int item_num = NEC_DATA_ITEM_NUM * nec_tx_num;
  memset((void*) item, 0, size);

  nec_build_items(RMT_TX_CHANNEL, item);

  //To send data according to the waveform items.
  rmt_write_items(RMT_TX_CHANNEL, item, item_num, true);

  //Wait until sending is done.
  rmt_wait_tx_done(RMT_TX_CHANNEL);

  //before we free the data, make sure sending is already done.
  free(item);
  return;
}


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

  //Checks if it's time to turn on or off the AC
  if (startHour == currentHour && startMin == currentMin && AC_ON == false) {
    digitalWrite(GPIO_LED, HIGH);
    led1.on();
    rmt_example_nec_tx_task(); //sends IR start/stop command
    Serial.println("IR command sent");
    AC_ON = true;
  }

  else if (stopHour == currentHour && stopMin == currentMin && AC_ON == true) {
    digitalWrite(GPIO_LED, LOW);
    led1.off();
    rmt_example_nec_tx_task(); //sends IR start/stop command
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
