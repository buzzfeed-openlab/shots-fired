// This program based off of Particle's Cell Locate code

/******************************************************************************
  Copyright (c) 2015 Particle Industries, Inc.  All rights reserved.
  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation, either
  version 3 of the License, or (at your option) any later version.
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.
  You should have received a copy of the GNU Lesser General Public
  License along with this program; if not, see <http://www.gnu.org/licenses/>.
 ******************************************************************************
 *
 * COMPILE from firmware/modules $
 * make clean all PLATFORM_ID=10 APPDIR=~/code/fw-apps/cell-locate COMPILE_LTO=n DEBUG_BUILD=y -s program-dfu
 *
*/

#pragma SPARK_NO_PREPROCESSOR
#include "application.h"
#include "cellular_hal.h"

struct MDM_CELL_LOCATE {
    int day;
    int month;
    int year;
    int hour;
    int minute;
    int second;
    char lat[14];
    char lng[14];
    int altitude; // only for GNSS positioning
    int uncertainty;
    int speed; // only for GNSS positioning
    int direction; // only for GNSS positioning
    int vertical_acc; // only for GNSS positioning
    int sensor_used; // 0: the last fix in the internal database, 2: CellLocate(R) location information
    int sv_used; // only for GNSS positioning
    int antenna_status; // only for GNSS positioning
    int jamming_status; // only for GNSS positioning
    int count;
    bool ok;
    int size;

    MDM_CELL_LOCATE()
    {
        memset(this, 0, sizeof(*this));
        size = sizeof(*this);
    }
};

MDM_CELL_LOCATE _cell_locate;
bool displayed_once = false;
volatile uint32_t cellTimeout;
volatile uint32_t cellTimeStart;

// Requires local compile
// ALL_LEVEL, TRACE_LEVEL, DEBUG_LEVEL, INFO_LEVEL, WARN_LEVEL, ERROR_LEVEL, PANIC_LEVEL, NO_LOG_LEVEL
//SerialDebugOutput debugOutput(9600, ALL_LEVEL);

SYSTEM_MODE(AUTOMATIC);

#include "RF24.h"
// Adapted from BDub's example in his RF24 Library for Spark Core
// https://github.com/technobly/SparkCore-RF24
// Original example Copyright (C) 2011 J. Coliz <maniacbug@ymail.com>
// distributed under GNU General Public License version 2 as published by the Free Software Foundation.

int triggerState;
int waitTime=5000;
char latlong[128];
char outgoingData[128];
char* ownerName="OWNER_NAME";

//
// Hardware configuration
//

/*
  PINOUTS
  http://docs.spark.io/#/firmware/communication-spi
  http://maniacbug.wordpress.com/2011/11/02/getting-started-rf24/

  SPARK CORE    SHIELD SHIELD    NRF24L01+
  GND           GND              1 (GND)
  3V3 (3.3V)    3.3V             2 (3V3)
  D6 (CSN)      9  (D6)          3 (CE)
  A2 (SS)       10 (SS)          4 (CSN)
  A3 (SCK)      13 (SCK)         5 (SCK)
  A5 (MOSI)     11 (MOSI)        6 (MOSI)
  A4 (MISO)     12 (MISO)        7 (MISO)

  NOTE: Also place a 10-100uF cap across the power inputs of
        the NRF24L01+.  I/O o fthe NRF24 is 5V tolerant, but
        do NOT connect more than 3.3V to pin 1!!!
 */


RF24 radio(D6,A2);

// Radio pipe addresses for the 2 nodes to communicate.
const uint64_t pipes[2] = { 0xF0F0F0F0E1LL, 0xF0F0F0F0D2LL };

// Two roles: transmit and receive
typedef enum { role_transmit = 1, role_receive } role_e;

// The debug-friendly names of those roles
const char* role_friendly_name[] = { "invalid", "Transmitter", "Receiver"};

// The role of the current running sketch
role_e role = role_receive; // Start as a Receiver

void cell_locate_timeout_set(uint32_t timeout_ms) {
    cellTimeout = timeout_ms;
    cellTimeStart = millis();
}

bool is_cell_locate_timeout() {
    return (cellTimeout && ((millis()-cellTimeStart) > cellTimeout));
}

void cell_locate_timeout_clear() {
    cellTimeout = 0;
}

bool is_cell_locate_matched(MDM_CELL_LOCATE& loc) {
    return loc.ok;
}

/* Cell Locate Callback */
int _cbLOCATE(int type, const char* buf, int len, MDM_CELL_LOCATE* data)
{
    if ((type == TYPE_PLUS) && data) {
        // DEBUG CODE TO SEE EACH LINE PARSED
        // char line[256];
        // strncpy(line, buf, len);
        // line[len] = '\0';
        // Serial.printf("LINE: %s",line);

        // <response_type> = 1:
        //+UULOC: <date>,<time>,<lat>,<long>,<alt>,<uncertainty>,<speed>,<direction>,
        //        <vertical_acc>,<sensor_used>,<SV_used>,<antenna_status>,<jamming_status>
        //+UULOC: 25/09/2013,10:13:29.000,45.7140971,13.7409172,266,17,0,0,18,1,6,3,9
        int count = 0;
        //
        // TODO: %f was not working for float on LAT/LONG, so opted for capturing strings for now
        if ( (count = sscanf(buf, "\r\n+UULOC: %d/%d/%d,%d:%d:%d.%*d,%[^,],%[^,],%d,%d,%d,%d,%d,%d,%d,%d,%d\r\n",
            &data->day,
            &data->month,
            &data->year,
            &data->hour,
            &data->minute,
            &data->second,
            data->lat,
            data->lng,
            &data->altitude,
            &data->uncertainty,
            &data->speed,
            &data->direction,
            &data->vertical_acc,
            &data->sensor_used,
            &data->sv_used,
            &data->antenna_status,
            &data->jamming_status) ) > 0 ) {
            // UULOC Matched
            data->count = count;
            data->ok = true;
        }
    }
    return WAIT;
}

int cell_locate(MDM_CELL_LOCATE& loc, uint32_t timeout_ms) {
    loc.count = 0;
    loc.ok = false;
    if (RESP_OK == Cellular.command(5000, "AT+ULOCCELL=0\r\n")) {
        if (RESP_OK == Cellular.command(_cbLOCATE, &loc, timeout_ms, "AT+ULOC=2,2,1,%d,5000\r\n", timeout_ms/1000)) {
            cell_locate_timeout_set(timeout_ms);
            if (loc.count > 0) {
                return loc.count;
            }
            return 0;
        }
        else {
            return -2;
            // Serial.println("Error! No Response from AT+LOC");
        }
    }
    // else Serial.println("Error! No Response from AT+ULOCCELL");
    return -1;
}

bool cell_locate_in_progress(MDM_CELL_LOCATE& loc) {
    if (!is_cell_locate_matched(loc) && !is_cell_locate_timeout()) {
        return true;
    }
    else {
        cell_locate_timeout_clear();
        return false;
    }
}

bool cell_locate_get_response(MDM_CELL_LOCATE& loc) {
    // Send empty string to check for URCs that were slow
    Cellular.command(_cbLOCATE, &loc, 1000, "");
    if (loc.count > 0) {
        return true;
    }
    return false;
}

void cell_locate_display(MDM_CELL_LOCATE& loc) {
    /* The whole kit-n-kaboodle */
    Serial.printlnf("\r\n%d/%d/%d,%d:%d:%d,LAT:%s,LONG:%s,%d,UNCERTAINTY:%d,SPEED:%d,%d,%d,%d,%d,%d,%d,MATCHED_COUNT:%d",
        loc.month,
        loc.day,
        loc.year,
        loc.hour,
        loc.minute,
        loc.second,
        loc.lat,
        loc.lng,
        loc.altitude,
        loc.uncertainty,
        loc.speed,
        loc.direction,
        loc.vertical_acc,
        loc.sensor_used,
        loc.sv_used,
        loc.antenna_status,
        loc.jamming_status,
        loc.count);
    sprintf(latlong,"%s,%s",loc.lat,loc.lng);
    sprintf(outgoingData,"%s,%s,%s",loc.lat,loc.lng,ownerName);
    /* A nice map URL */
    Serial.printlnf("\r\nhttps://www.google.com/maps?q=%s,%s\r\n",loc.lat,loc.lng);
}


// Unclear how much data this uses yet, may want to use a 3rd party SIM to play with this example
//STARTUP(cellular_credentials_set("broadband", "", "", NULL));

void setup()
{
  Serial.begin(9600);
  // Setup and configure rf radio
  radio.begin();

  if ( role == role_transmit )
  {
    radio.openWritingPipe(pipes[0]);
    radio.openReadingPipe(1,pipes[1]);
  }
  else
  {
    radio.openWritingPipe(pipes[1]);
    radio.openReadingPipe(1,pipes[0]);
  }

  // Start listening
  radio.startListening();

  // Dump the configuration of the rf unit for debugging
  radio.printDetails();
  
}

void loop()
{
    if (Serial.available() > 0)
    {
        char c = Serial.read();
        Serial.printf("Hey, you said \'%c\', so I'm gunna: ", c);
        if (c == 'l' || c == 'L') {
            Serial.println("run Cell Locate...");
            uint32_t timeout = 60000;
            displayed_once = false;
            int ret = cell_locate(_cell_locate, timeout);
            if (ret >= 8) {
                /* Got the response immediately */
                cell_locate_display(_cell_locate);
                displayed_once = true;
            }
            else if (ret == 0) {
                /* ret == 0, still waiting for the URC
                 * Check for cell locate response, and display it. */
                Serial.print("Waiting for URC ");
                while (cell_locate_in_progress(_cell_locate)) {
                    /* still waiting for URC */
                    if (cell_locate_get_response(_cell_locate)) {
                        cell_locate_display(_cell_locate);
                        displayed_once = true;
                    }
                    if (!displayed_once) Serial.print(".");
                }
                Serial.println();
                // Serial.println("Not in progress");

                /* We timed out, but maybe we have a response that includes LAT/LONG coords */
                if (!displayed_once && _cell_locate.count >= 8) {
                    cell_locate_display(_cell_locate);
                    displayed_once = true;
                }
            }
            else {
                /* ret == -1 */
                Serial.println("Cell Locate Error!");
            }
        }
        else if (c == 'a' || c == 'A') {
            // This is required for CellLocate to work properly, but should already be set by default.
            // Just in case though, here it is for you to use if you are not getting CellLocate results.
            Serial.print("Reprogram the AssistNow server to u-blox factory default: ");
            if (RESP_OK == Cellular.command(5000, "AT+UGAOP=\"eval1-les.services.u-blox.com\",46434,1000,0\r\n")) {
                Serial.println("OK!");
            }
            else {
                Serial.println("Error!");
            }
        }
        else {
            Serial.println("ignore you because you're not speaking my language!");
        }
        while (Serial.available()) Serial.read(); // Flush the input buffer
    }
      // if there is data ready
  if ( radio.available() )
  {
    // Dump the payloads until we've gotten everything
    int value;
    bool done = false;
    while (!false) {
      // Fetch the payload, and see if this was the last one.
      done = radio.read( &value, sizeof(int) );

      // Spew it
      Serial.print("Got payload "); Serial.println(value);
      // Delay just a little bit
      // use this value
      triggerState=value;
      if (value==1) {
          digitalWrite(D7,HIGH);
          Serial.println("run Cell Locate...");
            uint32_t timeout = 60000;
            displayed_once = false;
            int ret = cell_locate(_cell_locate, timeout);
            if (ret >= 8) {
                /* Got the response immediately */
                cell_locate_display(_cell_locate);
                displayed_once = true;
            }
            else if (ret == 0) {
                /* ret == 0, still waiting for the URC
                 * Check for cell locate response, and display it. */
                Serial.print("Waiting for URC ");
                while (cell_locate_in_progress(_cell_locate)) {
                    /* still waiting for URC */
                    if (cell_locate_get_response(_cell_locate)) {
                        cell_locate_display(_cell_locate);
                        displayed_once = true;
                        Particle.publish("shots-fired",outgoingData,60,PRIVATE);
                    }
                    if (!displayed_once) Serial.print(".");
                }
                Serial.println();
                // Serial.println("Not in progress");

                /* We timed out, but maybe we have a response that includes LAT/LONG coords */
                if (!displayed_once && _cell_locate.count >= 8) {
                    cell_locate_display(_cell_locate);
                    displayed_once = true;
                }
            }
            else {
                /* ret == -1 */
                Serial.println("Cell Locate Error!");
            }
        }
      else {digitalWrite(D7,LOW);}
      delay(20);
    }

    // Switch to a transmitter after each received payload
    // or this will only receive once
    radio.stopListening();

    // Re-open the pipes for Rx'ing
    radio.openWritingPipe(pipes[1]);
    radio.openReadingPipe(1,pipes[0]);

    // Now, resume listening so we catch the next packets.
    radio.startListening();
  }
}