// This #include statement was automatically added by the Particle IDE.
#include "RF24.h"

// Adapted from BDub's example in his RF24 Library for Spark Core
// https://github.com/technobly/SparkCore-RF24
// Original example Copyright (C) 2011 J. Coliz <maniacbug@ymail.com>
// distributed under GNU General Public License version 2 as published by the Free Software Foundation.

// Don't need wifi for this demo
// SYSTEM_MODE(SEMI_AUTOMATIC);

#define triggerPin D7

int triggerState=0;

/*

Hardware configuration is:

-----------
photon - RF
-----------
GND - GND
3V3 - 3V3
D6  - CE
A2  - CSN
A3  - SCK
A5  - MOSI
A6  - MISO
-----------

vibration motor: D0 and GND
conductive yarn: A0 and 3V3
10K resistor: A0 and GND

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
role_e role = role_transmit; // Start as a Transmitter

void setup(void)
{

    Serial.begin(9600);

  // Set pin modes
  pinMode(triggerPin, INPUT_PULLDOWN);

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

void loop(void)
{

  if (role == role_transmit) {
    transmitData();
  }

}

void transmitData() {
  triggerState = digitalRead(triggerPin);

  // Switch to a Receiver before each transmission,
  // or this will only transmit once.
  radio.startListening();

  // Re-open the pipes for Tx'ing
  radio.openWritingPipe(pipes[0]);
  radio.openReadingPipe(1,pipes[1]);

  // First, stop listening so we can talk.
  radio.stopListening();

  // Take the time, and send it.  This will block until complete
  Serial.println("Now sending "+String(triggerState)+"...");
  if (triggerState==1) {
      bool ok=false;
      while (!ok) {
        Serial.println("Trying again...");
        ok = radio.write( &triggerState, sizeof(int) );
      }
      Serial.println("Success!");
      delay(2000);
    }
  else {
      bool ok = radio.write( &triggerState, sizeof(int) );
      if (ok) {
        Serial.println("ok...\n\r");
        Particle.publish("shots-fired-trigger",NULL,60,PRIVATE);
      }
      else {
        Serial.println(" failed.\n\r");
      }
  }

  // Try again 100 ms later
  delay(100);
}

int scale(int x, int xmin, int xmax, int min, int max) {
  int scaled = min+max*(x-xmin)/(xmax-xmin);
  return scaled;
}


//sets high and low bounds for an integer x
int restrict(int x, int min, int max) {
  if (x>max) {
    return max;
  }
  else if (x<min) {
    return min;
  }
  else {
    return x;
  }
}
