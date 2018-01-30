/*
 * two_wire.c
 *
 * Created: 1/28/2018 3:36:21 PM
 *  Author: Matthew
 */ 
 #include "two_wire.h"
 #include <avr/io.h>
 #include <avr/interrupt.h>
 #include <compat/twi.h>

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

 static inline unsigned char control_TWI_Enable(void)
 {
   return (1 << TWEN);
 }

static inline unsigned char control_TWI_Acknowledge(void)
{
   return (1 << TWEA);
}

static inline unsigned char control_TWI_Interrupts_On(void)
{
   return (1 << TWIE);
}

static inline unsigned char control_TWI_Handled(void)
{
   // Writing one to this bit clears it.
   return (1 << TWINT);
}

static inline unsigned char control_TWI_Send_Start(void)
{
   return (1 << TWSTA);
}

static inline unsigned char control_TWI_Send_Stop(void)
{
   return (1 << TWSTO);
}

static inline unsigned char mcontrol_TWI_On_State(void)
{
   return control_TWI_Enable() | control_TWI_Interrupts_On() | control_TWI_Acknowledge();
}

static inline unsigned char mcontrol_TWI_On_NoAck(void)
{
   return control_TWI_Enable() | control_TWI_Interrupts_On();
}

static void twi_Set_BR(unsigned int aiFreq)
{
   TWBR = ((CLOCK_FREQUENCY / aiFreq) - 16) / 2 /* *PreScaler */;
}

static void handler_TWI_Reply(unsigned char ack)
{
   // transmit master read ready signal, with or without ack
   if(ack)
   {
      TWCR = mcontrol_TWI_On_State() | control_TWI_Handled();
   }
   else
   {
      TWCR = mcontrol_TWI_On_NoAck() | control_TWI_Handled();
   }
}

static void handler_TWI_Stop(void)
{
     // send stop condition
     TWCR = mcontrol_TWI_On_State() | control_TWI_Handled() | control_TWI_Send_Stop();

     // wait for stop condition to be exectued on bus
     // TWINT is not set after a stop condition!
     while(TWCR & (1 << TWSTO))
     {
        continue;
     }

     // update twi state
     twi_State = TWI_AVAILABLE;
}

static void twi_Master_TX_Handler(unsigned char aiStatus)
{
   switch (aiStatus)
   {
      case TW_START: // 0x08
      case TW_REP_START: // 0x10
         // Load SLA+W
         TWDR = twi_Last_Slave_Address_And_RW;
         handler_TWI_Reply(1);
         break;
      case TW_MT_SLA_ACK:
      case TW_MT_DATA_ACK:
         // Load data byte
         if( twi_Master_Buffer_Index < twi_Master_Buffer_Length )
         {
            // There is data to send.
            TWDR = twi_Master_Buffer[twi_Master_Buffer_Index++];
            handler_TWI_Reply(1);
         }
         else if (twi_SendStop)
         {
            // We're done
            handler_TWI_Stop();
         }
         else
         {
            // Hold the bus
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
         handler_TWI_Reply(1); // Clear the TWINT so that the TWI module knows we're done.
         break;
      case TW_MT_SLA_ACK:
      case TW_MT_DATA_ACK:
         // Load data byte
         if( twi_Master_Buffer_Index < twi_Master_Buffer_Length )
         {
            // There is data to send.
            TWDR = twi_Master_Buffer[twi_Master_Buffer_Index++];
            handler_TWI_Reply(1); // Clear the TWINT so that the TWI module knows we're done.
         }
         else if (twi_SendStop)
         {
            // We're done
            handler_TWI_Stop();
         }
         else
         {
            // Hold the bus
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

void twi_Init(void)
{
  // initialize state
  twi_State = TWI_AVAILABLE;

  // initialize twi prescaler and bit rate
  sbi(TWSR, TWPS0);
  sbi(TWSR, TWPS1);
  twi_Set_BR(SCL_FREQUENCY);

  // From summary of all TWI transmissions pg 269.
  TWCR = control_TWI_Interrupts_On() | control_TWI_Enable() | control_TWI_Acknowledge();
}

unsigned char twi_WriteTo( unsigned char address, unsigned char* data,
                           unsigned char length, unsigned char wait,
                           unsigned char sendStop )
{
    // ensure data will fit into buffer
    if(TWI_BUFFER_LENGTH < length)
    {
       return 1;
    }

    // wait until twi is ready, become master transmitter
    while(twi_State != TWI_AVAILABLE)
    {
       continue;
    }

    twi_State = TWI_MASTER_TX;
    twi_SendStop = sendStop;
    // reset error state (0xFF.. no error occured)
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
    
    // if we're in a repeated start, then we've already sent the START
    // in the ISR. Don't do it again.
    //
    if (twi_InRepStart) 
    {
       // if we're in the repeated start state, then we've already sent the start,
       // (@@@ we hope), and the TWI statemachine is just waiting for the address byte.
       // We need to remove ourselves from the repeated start state before we enable interrupts,
       // since the ISR is ASYNC, and we could get confused if we hit the ISR before cleaning
       // up. Also, don't enable the START interrupt. There may be one pending from the
       // repeated start that we sent outselves, and that would really confuse things.
       twi_InRepStart = 0;			// remember, we're dealing with an ASYNC ISR
       do
       {
          TWDR = twi_Last_Slave_Address_And_RW;
       } while(TWCR & (1 << TWWC));
       TWCR = control_TWI_Handled() | mcontrol_TWI_On_State();	// enable INTs, but not START
    }
    else
    // send start condition
    TWCR = control_TWI_Handled() | mcontrol_TWI_On_State() | control_TWI_Send_Start();	// enable INTs

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