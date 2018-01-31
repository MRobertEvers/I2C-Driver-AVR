/*
 * two_wire.c
 *
 * Much of this is taken from the official arduino library source.
 * Created: 1/28/2018 3:36:21 PM
 *  Author: Matthew
 */ 
 #include "two_wire.h"
 #include <avr/io.h>
 #include <avr/interrupt.h>
 #include <compat/twi.h>

 // Enables the TWI Module. Manual Chapter 26.
 #define TWCR_ENABLE (1 << TWEN)

 // Enables the hardware to generate the acknowledge response.
 // Generates ack response in the situations defined in 26.5.5.
 #define TWCR_ACK (1 << TWEA)

 // Enables the hardware to generate the TWINT flag.
 #define TWCR_INTERRUPTS_ENABLE (1 << TWIE)

 // When set to 1. The TWINT bit is cleared. This indicates that our software
 // is done handling this step of the I2C protocol.
 #define TWCR_INTERRUPT_HANDLED (1 << TWINT)

 // Tells the hardware to send the start signal.
 #define TWCR_SEND_START (1 << TWSTA)

 // Tells the hardware to send the stop signal.
 #define TWCR_SEND_STOP (1 << TWSTO)

 #define MTWCR_SET_ON TWCR_ENABLE | TWCR_INTERRUPTS_ENABLE | TWCR_ACK
 #define MTWCR_SET_ON_NO_ACK TWCR_ENABLE | TWCR_INTERRUPTS_ENABLE


 #define cbi(sfr, bit) (_SFR_BYTE(sfr) &= ~(1 << bit))
 #define sbi(sfr, bit) (_SFR_BYTE(sfr) |= (1 << bit))

 static volatile unsigned char twi_State = TWI_AVAILABLE;
 static volatile unsigned char twi_SendStop = 1;
 static volatile unsigned char twi_InRepStart = 0;
 static volatile unsigned char twi_LastError = 0;
 static volatile unsigned char twi_Last_Slave_Address_And_RW = 0;

 static unsigned char twi_Master_Buffer[TWI_BUFFER_LENGTH];
 static volatile unsigned char twi_Master_Buffer_Length = 0;
 static volatile unsigned char twi_Master_Buffer_Index = 0;


static void twi_Set_BR(unsigned int aiFreq)
{
   TWBR = ((CLOCK_FREQUENCY / aiFreq) - 16) / 2 /* *PreScaler */;
}

static void handler_TWI_Repeated_Start(void)
{
   // Hold the bus. We've already sent the NACK if RX.
   twi_InRepStart = 1;	// we're gonna send the START
   // don't enable the interrupt. We'll generate the start, but we
   // avoid handling the interrupt until we're in the next transaction,
   // at the point where we would normally issue the start.
   TWCR = TWCR_ENABLE | TWCR_INTERRUPT_HANDLED | TWCR_SEND_START;
   twi_State = TWI_AVAILABLE;
}

static void handler_TWI_Set_Handled_Ack_Next(unsigned char abShouldAck)
{
   // Note that this ack applies to the NEXT time we receive data.
   // We are setting the control register to say that it should send
   // ACK when appropriate.
   if(abShouldAck)
   {
      TWCR = MTWCR_SET_ON | TWCR_INTERRUPT_HANDLED;
   }
   else
   {
      TWCR = MTWCR_SET_ON_NO_ACK | TWCR_INTERRUPT_HANDLED;
   }
}

static void handler_TWI_Stop(void)
{
     // send stop condition
     TWCR = MTWCR_SET_ON | TWCR_INTERRUPT_HANDLED | TWCR_SEND_STOP;

     // wait for stop condition to be exectued on bus
     // TWINT is not set after a stop condition!
     while(TWCR & (1 << TWSTO))
     {
        continue;
     }

     // update twi state
     twi_State = TWI_AVAILABLE;
}

static void handler_TWI_Start(void)
{
    // Repetitive start taken from Arduino Source.
    // if we're in a repeated start, then we've already sent the START
    // in the ISR. Don't do it again.
    if (twi_InRepStart)
    {
       twi_InRepStart = 0;	
       do
       {
          TWDR = twi_Last_Slave_Address_And_RW;
       } while(TWCR & (1 << TWWC));
       TWCR = TWCR_INTERRUPT_HANDLED | MTWCR_SET_ON;	// enable INTs, but not START
    }
    else
    {
       // send start condition
       TWCR = TWCR_INTERRUPT_HANDLED | MTWCR_SET_ON | TWCR_SEND_START;	// enable INTs
    }
}

static void twi_Master_TX_Handler(unsigned char aiStatus)
{
   switch (aiStatus)
   {
      case TW_START: // 0x08
      case TW_REP_START: // 0x10
         // Load SLA+W
         TWDR = twi_Last_Slave_Address_And_RW;
         handler_TWI_Set_Handled_Ack_Next(0);
         break;
      case TW_MT_SLA_ACK:
      case TW_MT_DATA_ACK:
         // Load data byte
         if( twi_Master_Buffer_Index < twi_Master_Buffer_Length )
         {
            // There is data to send.
            TWDR = twi_Master_Buffer[twi_Master_Buffer_Index++];
            handler_TWI_Set_Handled_Ack_Next(0);
         }
         else if (twi_SendStop)
         {
            // We're done
            handler_TWI_Stop();
         }
         else
         {
            handler_TWI_Repeated_Start();
         }
         break;
      case TW_MT_DATA_NACK:
      case TW_MT_SLA_NACK:
      case TW_MT_ARB_LOST:
         handler_TWI_Stop();
         twi_LastError = TW_STATUS;
         break;
   }
}

static void twi_Master_RX_Handler(unsigned char aiStatus)
{
   switch (aiStatus)
   {
      case TW_START: // 0x08
      case TW_REP_START: // 0x10
         // Load SLA+R
         TWDR = twi_Last_Slave_Address_And_RW;
         handler_TWI_Set_Handled_Ack_Next(1); // Clear the TWINT so that the TWI module knows we're done.
         break;
      case TW_MR_SLA_ACK:
         // Set the TWCR so that when a data byte is received, ACK is returned.
         handler_TWI_Set_Handled_Ack_Next(1); 
         break;
      case TW_MR_DATA_ACK:
         // Data has been received. If there is room
         // in the buffer, store it.
         if( twi_Master_Buffer_Index < twi_Master_Buffer_Length )
         {
            twi_Master_Buffer[twi_Master_Buffer_Index++] = TWDR;
            // Set the TWCR so that if another data byte is received, NACK is returned because
            // we are out of memory.
            handler_TWI_Set_Handled_Ack_Next(twi_Master_Buffer_Index < twi_Master_Buffer_Length);
         }
         else
         {
            handler_TWI_Repeated_Start();
         }
         break;
      case TW_MR_DATA_NACK:
      case TW_MR_SLA_NACK:
      case TW_MR_ARB_LOST:
         handler_TWI_Stop();
         twi_LastError = TW_STATUS;
         break;
   }
}

void twi_Slave_TX_Handler(void)
{

}

void twi_Init(void)
{
  twi_State = TWI_AVAILABLE;

  cbi(TWSR, TWPS0);
  cbi(TWSR, TWPS1);
  twi_Set_BR(SCL_FREQUENCY);

  // From summary of all TWI transmissions pg 269.
  TWCR = MTWCR_SET_ON;
}

unsigned char twi_Write( unsigned char address, unsigned char* data,
                         unsigned char length, unsigned char wait,
                         unsigned char sendStop )
{
    // Stop if we try to send more info than we can buffer.
    if(TWI_BUFFER_LENGTH < length)
    {
       return 1;
    }

    // Wait until the TWI module is available.
    while(twi_State != TWI_AVAILABLE)
    {
       continue;
    }

    twi_State = TWI_MASTER_TX;
    twi_SendStop = sendStop;

    // Reset Error State
    twi_LastError = 0xFF;

    // initialize buffer iteration vars
    twi_Master_Buffer_Index= 0;
    twi_Master_Buffer_Length = length;
    
    // copy data to twi buffer
    for(unsigned char i = 0; i < length; ++i)
    {
       twi_Master_Buffer[i] = data[i];
    }
    
    // build sla+w, slave device address + w bit
    twi_Last_Slave_Address_And_RW = TW_WRITE;
    twi_Last_Slave_Address_And_RW |= address << 1;
   
    handler_TWI_Start();

    // wait for write operation to complete
    while(wait && (TWI_MASTER_TX == twi_State))
    {
       continue;
    }
    
    if (twi_LastError == 0xFF)
      return 0;	// success
    else if (twi_LastError == TW_MT_SLA_NACK)
      return 2;	// error: address send, nack received
    else if (twi_LastError == TW_MT_DATA_NACK)
      return 3;	// error: data send, nack received
    else
      return 4;	// other twi error
}

unsigned char twi_Read( unsigned char address, unsigned char* outputBuffer,
                        unsigned char oBufLength, unsigned char sendStop )
{
    // wait until twi is ready, become master transmitter
    while(twi_State != TWI_AVAILABLE)
    {
       continue;
    }

    twi_State = TWI_MASTER_RX;
    twi_SendStop = sendStop;
    twi_InRepStart = 0;

    twi_LastError = 0xFF;

   twi_Master_Buffer_Index= 0;
   if( oBufLength <= TWI_BUFFER_LENGTH )
   {
      twi_Master_Buffer_Length = oBufLength;
   }
   else
   {
      twi_Master_Buffer_Length = TWI_BUFFER_LENGTH;
   }

   twi_Last_Slave_Address_And_RW = TW_READ;
   twi_Last_Slave_Address_And_RW |= address << 1;

   handler_TWI_Start();

   // Wait for read completion.
   while(twi_State != TWI_AVAILABLE)
   {
      continue;
   }

   for(unsigned int i = 0; i < twi_Master_Buffer_Index; i++)
   {
      outputBuffer[i] = twi_Master_Buffer[i];
   }

   return twi_Master_Buffer_Index;
}

ISR(TWI_vect)
{
   switch (twi_State)
   {
      case TWI_MASTER_TX:
         twi_Master_TX_Handler(TW_STATUS);
         break;
      case TWI_MASTER_RX:
         twi_Master_RX_Handler(TW_STATUS);
         break;
      case TWI_SLAVE_TX:
      case TWI_SLAVE_RX:
      default:
         break;
   }
}