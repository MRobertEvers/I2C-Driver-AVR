/*
 * I2CDriver.c
 *
 * Created: 1/28/2018 12:56:34 PM
 * Author : Matthew
 */ 
 #include <avr/interrupt.h>
#include <avr/io.h>

#include "two_wire.h"
unsigned char buffer[17];
unsigned char oscBuff[1];
unsigned char onBuff[1];
unsigned char brightBuff[1];
int main(void)
{
   sei();
   unsigned char counter = 0;
    /* Replace with your application code */
    while (1)
    {
        // paint one LED per row. The HT16K33 internal memory looks like
        // a 8x16 bit matrix (8 rows, 16 columns)
        onBuff[0] = 0x81;
        oscBuff[0] = 0x21;
        buffer[0] = 0;
        for (uint8_t i=1; i<17; i+=2)
        {
            unsigned char temp = 1 << i/2;
            temp = temp >> 1 | (temp << 7);
           buffer[i] = temp;
        }
        twi_Init();

        // This is from the LED Doc. Will work on this magic document.
        twi_Write(0x70, oscBuff, 1, 1, 1);
        twi_Write(0x70, onBuff, 1, 1, 1);

        twi_Write(0x70, buffer, 17, 1, 1);

        counter++;
        if (counter >= 16) counter = 0;
    }
}

