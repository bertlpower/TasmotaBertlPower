/*
  xsns_33_ds3231.ino - ds3231 RTC chip, act like sensor support for Tasmota

  Copyright (C) 2021  Guy Elgabsi (guy.elg AT gmail.com)

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef USE_I2C
#ifdef USE_DS3231
/*********************************************************************************************\
   DS3231 - its a accurate RTC that used in the SONOFF for get time when you not have internet connection
            This is minimal library that use only for read/write time !
            We store UTC time in the DS3231 , so we can use the standart functions.
   HOWTO Use : first time, you must to have internet connection (use your mobile phone or try in other location).
               once you have ntp connection , the DS3231 internal clock will be updated automatically.
               you can now power off the device, from now and on the time is stored in the module and will
               be restored when the is no connection to NTP.
   Source: Guy Elgabsi with special thanks to Jack Christensen

   I2C Address: 0x68
  \*********************************************************************************************/

#define XSNS_33             33
#define XI2C_26             26  // See I2CDEVICES.md

#ifndef USE_GPS                 // USE_GPS provides it's own (better) NTP server so skip this one
#define DS3231_NTP_SERVER
#endif

//DS3232 I2C Address
#ifndef USE_RTC_ADDR
#define USE_RTC_ADDR 0x68
#endif

//DS3232 Register Addresses
#define RTC_SECONDS 0x00
#define RTC_MINUTES 0x01
#define RTC_HOURS 0x02
#define RTC_DAY 0x03
#define RTC_DATE 0x04
#define RTC_MONTH 0x05
#define RTC_YEAR 0x06
#define RTC_CONTROL 0x0E
#define RTC_STATUS 0x0F
//Control register bits
#define OSF 7
#define EOSC 7
#define BBSQW 6
#define CONV 5
#define RS2 4
#define RS1 3
#define INTCN 2

//Other
#define HR1224 6                   //Hours register 12 or 24 hour mode (24 hour mode==0)
#define CENTURY 7                  //Century bit in Month register
#define DYDT 6                     //Day/Date flag bit in alarm Day/Date registers
bool ds3231ReadStatus = false;
bool ds3231WriteStatus = false;    //flag, we want to read/write to DS3231 only once
bool DS3231chipDetected = false;

#ifdef DS3231_NTP_SERVER
#include "NTPServer.h"
#include "NTPPacket.h"

#define D_CMND_NTP "NTP"

const char S_JSON_NTP_COMMAND_NVALUE[] PROGMEM = "{\"" D_CMND_NTP "%s\":%d}";

const char kRTCTypes[] PROGMEM = "NTP";

#define NTP_MILLIS_OFFSET      50

NtpServer timeServer(PortUdp);

struct NTP_t {
  struct {
    uint32_t init:1;
    uint32_t runningNTP:1;
  } mode;
} NTP;
#endif  // DS3231_NTP_SERVER

/*----------------------------------------------------------------------*
  Detect the DS3231 Chip
  ----------------------------------------------------------------------*/
void DS3231Detect(void) {
  if (!I2cSetDevice(USE_RTC_ADDR)) { return; }

  if (I2cValidRead(USE_RTC_ADDR, RTC_STATUS, 1)) {
    I2cSetActiveFound(USE_RTC_ADDR, "DS3231");
    DS3231chipDetected = true;
  }
}

/*----------------------------------------------------------------------*
  BCD-to-Decimal conversion
  ----------------------------------------------------------------------*/
uint8_t bcd2dec(uint8_t n) {
  return n - 6 * (n >> 4);
}

/*----------------------------------------------------------------------*
   Decimal-to-BCD conversion
  ----------------------------------------------------------------------*/
uint8_t dec2bcd(uint8_t n) {
  return n + 6 * (n / 10);
}

/*----------------------------------------------------------------------*
   Read time from DS3231 and return the epoch time (second since 1-1-1970 00:00)
  ----------------------------------------------------------------------*/
uint32_t ReadFromDS3231(void) {
  TIME_T tm;
  tm.second = bcd2dec(I2cRead8(USE_RTC_ADDR, RTC_SECONDS));
  tm.minute = bcd2dec(I2cRead8(USE_RTC_ADDR, RTC_MINUTES));
  tm.hour = bcd2dec(I2cRead8(USE_RTC_ADDR, RTC_HOURS) & ~_BV(HR1224)); //assumes 24hr clock
  tm.day_of_week = I2cRead8(USE_RTC_ADDR, RTC_DAY);
  tm.day_of_month = bcd2dec(I2cRead8(USE_RTC_ADDR, RTC_DATE));
  tm.month = bcd2dec(I2cRead8(USE_RTC_ADDR, RTC_MONTH) & ~_BV(CENTURY)); //don't use the Century bit
  tm.year = bcd2dec(I2cRead8(USE_RTC_ADDR, RTC_YEAR));
  return MakeTime(tm);
}
/*----------------------------------------------------------------------*
   Get time as TIME_T and set the DS3231 time to this value
  ----------------------------------------------------------------------*/
void SetDS3231Time (uint32_t epoch_time) {
  TIME_T tm;
  BreakTime(epoch_time, tm);
  I2cWrite8(USE_RTC_ADDR, RTC_SECONDS, dec2bcd(tm.second));
  I2cWrite8(USE_RTC_ADDR, RTC_MINUTES, dec2bcd(tm.minute));
  I2cWrite8(USE_RTC_ADDR, RTC_HOURS, dec2bcd(tm.hour));
  I2cWrite8(USE_RTC_ADDR, RTC_DAY, tm.day_of_week);
  I2cWrite8(USE_RTC_ADDR, RTC_DATE, dec2bcd(tm.day_of_month));
  I2cWrite8(USE_RTC_ADDR, RTC_MONTH, dec2bcd(tm.month));
  I2cWrite8(USE_RTC_ADDR, RTC_YEAR, dec2bcd(tm.year));
  I2cWrite8(USE_RTC_ADDR, RTC_STATUS, I2cRead8(USE_RTC_ADDR, RTC_STATUS) & ~_BV(OSF)); //clear the Oscillator Stop Flag
}

void DS3231EverySecond(void) {
  if (!ds3231ReadStatus && (Rtc.utc_time < START_VALID_TIME)) { // We still did not sync with NTP (time not valid) , so, read time  from DS3231

    uint32_t ds3231_time = ReadFromDS3231();  // Read UTC TIME from DS3231

    if (ds3231_time > START_VALID_TIME) {
      Rtc.utc_time = ds3231_time;
      RtcSync();
      AddLog(LOG_LEVEL_DEBUG, PSTR("DS3: Synched"));
//      Rtc.user_time_entry = true;             // Stop NTP sync and DS3231 time write
      ds3231ReadStatus = true;                // if time in DS3231 is valid, do  not update again
    }
  }
  else if (!ds3231WriteStatus && (Rtc.utc_time > START_VALID_TIME) && (abs((int32_t)(Rtc.utc_time - ReadFromDS3231())) > 10)) {  // If time is valid and has drifted from RTC more than 10 seconds
    AddLog(LOG_LEVEL_INFO, PSTR("DS3: Write Time from NTP (" D_UTC_TIME ") %s"), GetDateAndTime(DT_UTC).c_str());
    SetDS3231Time(Rtc.utc_time);              // Update the DS3231 time
    ds3231WriteStatus = true;
  }
#ifdef DS3231_NTP_SERVER
  if (NTP.mode.runningNTP) {
    timeServer.processOneRequest(Rtc.utc_time, NTP_MILLIS_OFFSET);
  }
#endif  // DS3231_NTP_SERVER
}

#ifdef DS3231_NTP_SERVER
/*********************************************************************************************\
 * NTP functions
\*********************************************************************************************/

void NTPSelectMode(uint16_t mode) {
  switch(mode){
    case 0:
      NTP.mode.runningNTP = false;
      break;
    case 1:
      if (timeServer.beginListening()) {
        NTP.mode.runningNTP = true;
      }
      break;
  }
}

bool NTPCmd(void) {
  bool serviced = true;
  if (XdrvMailbox.data_len > 0) {
    NTPSelectMode(XdrvMailbox.payload);
    Response_P(S_JSON_NTP_COMMAND_NVALUE, XdrvMailbox.command, XdrvMailbox.payload);
  }
  return serviced;
}
#endif  // DS3231_NTP_SERVER

/*********************************************************************************************\
 * Interface
\*********************************************************************************************/

bool Xsns33(uint8_t function) {
  if (!I2cEnabled(XI2C_26)) { return false; }

  bool result = false;

  if (FUNC_INIT == function) {
    DS3231Detect();
  }
  else if (DS3231chipDetected) {
    switch (function) {
#ifdef DS3231_NTP_SERVER
      case FUNC_COMMAND_SENSOR:
        if (XSNS_33 == XdrvMailbox.index) {
          result = NTPCmd();
        }
        break;
#endif  // DS3231_NTP_SERVER
      case FUNC_EVERY_SECOND:
        DS3231EverySecond();
        break;
    }
  }
  return result;
}

#endif // USE_DS3231
#endif // USE_I2C
