Notes:
======
TODO:  
1. Modes needed - Master Receive, Slave Transmit, Slave Receive.
2. Documentation.
3. Change inline functions to #Defines
4. Current state for use with HTI6k33. Document this. [Manual](https://cdn-shop.adafruit.com/datasheets/ht16K33v110.pdf).
5. Enable internal pullups. Relies on external pull ups at the moment. 


HTI6k33:
========
Eventually, my notes for this device will end up in a more appropriate location, but for now...
The manual for this is a bit tricky to interpret. Here are some notes.

Setup
-----
The display can be turned on by sending the following commands to I2C address 0x70
1. 0x81 // The display setup command is 0x8a. a = 1 turns the display on. (Pg. 11 of the manual)  
2. 0x21 // The system setup command is 0x2a. a = 1 turns on the system clock. (Pg. 10 of the manual)  

Writing to Display
------------------
Writing to the display takes 17 bytes.
The first byte is the starting column. The next bytes are each column consecutively.

Column Meaning
--------------
The IC that comes with the HTI6K33 has '16' columns. Of which, only 8 are connected.
The connected columns are the even columns. I.E. the 0s below indicate the unconnected columns.

    buffer[0] = 2;    // Starting Column index. Start at column '2'. i.e. column of index 1 (starting count at 0)
    buffer[1] = 0x80; // This is the value for the first (read: Zeroth) row.
    buffer[2] = 0;
    buffer[3] = 1;
    buffer[4] = 0;
    buffer[5] = 2;
    buffer[6] = 0;
    buffer[7] = 4;
    buffer[8] = 0;
    buffer[9] = 8;
    buffer[10] = 0;
    buffer[11] = 0x10;
    buffer[12] = 0;
    buffer[13] = 0x20;
    buffer[14] = 0;
    buffer[15] = 0x40;
    buffer[16] = 0;
	
Row Meaning
-----------
Each bit in the char corresponding to a buffer position in the buffer above represents an LED in that column.
The most significant bit of the MSB is really the Zeroth column. Then the least significant bit of the LSB is the first column... So You need to shift the bits and put the MS(bit) into the LS(bit). Something like `temp = temp >> 1 | (temp << 7);`
	
