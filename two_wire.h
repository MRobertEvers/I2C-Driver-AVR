/*
 * two_wire.h
 *
 * Created: 1/28/2018 3:36:11 PM
 *  Author: Matthew
 */ 


#ifndef TWO_WIRE_H_
#define TWO_WIRE_H_

// Since much of the TWI module is interrupt based
// this is how we track the desired behavior of our module.
// These modes are outlined in manual 26.7.
#define TWI_AVAILABLE   0
#define TWI_MASTER_RX   1
#define TWI_MASTER_TX   2
#define TWI_SLAVE_RX    3
#define TWI_SLAVE_TX    4

#define TWI_BUFFER_LENGTH 32

#define CLOCK_FREQUENCY 20000000
#define SCL_FREQUENCY 100000L

void twi_Init(void);
unsigned char twi_Write( unsigned char address, unsigned char* data,
                           unsigned char length, unsigned char wait,
                           unsigned char sendStop );

unsigned char twi_Read( unsigned char address, unsigned char* outputBuffer,
                        unsigned char oBufLength, unsigned char sendStop );



#endif /* TWO_WIRE_H_ */