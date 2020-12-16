#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define SOP '<'
#define EOP '>'

bool started = false;
bool ended = false;

char inData[80];
int i=0;

String incomingByte;
void setup() {
  Serial.begin(9600);
  Serial2.begin(9600);
}

void loop() {
  // put your main code here, to run repeatedly:
while(Serial2.available() > 0)
  {
    char inChar = Serial2.read();
    if(inChar == SOP)
    {
       i = 0;
       inData[i] = '\0';
       started = true;
       ended = false;
    }
    else if(inChar == EOP)
    {
       ended = true;
       break;
    }
    else
    {
      if(i < 79)
      {
        inData[i] = inChar;
        i++;
        inData[i] = '\0';
      }
    }
  }

  // We are here either because all pending serial
  // data has been read OR because an end of
  // packet marker arrived. Which is it?
  if(started && ended)
  {
    Serial.println(inData);
    // The end of packet marker arrived. Process the packet

    // Reset for the next packet
    started = false;
    ended = false;
    i = 0;
    inData[i] = '\0';
  }
  
}