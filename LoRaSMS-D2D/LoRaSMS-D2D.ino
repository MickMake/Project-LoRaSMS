#define ESP_TARGET

#include <SPI.h>
#include <RH_RF95.h>
#include "htmlServer.h"
#include "crc16.h"
//#include <CircularBuffer.h>
#include <RingBufCPP.h>

using namespace std;


// Make sure these are wired up correctly!
#ifdef ESP_TARGET
#define RFM95_RST	26
#define RFM95_CS	21
#define RFM95_INT	25
#else
#define RFM95_RST	4
#define RFM95_CS	5
#define RFM95_INT	3
#define	BUILTIN_LED	13
#endif

/* ESP32 wiring as in my video.
MISO    purple
MOSI    gray
CLK     brown
CS/NSS  red
RESET   orange
DIO0    yellow
Vcc     white
Gnd     black
*/

// Change to 434.0 or other frequency, must match RX's freq!
#define RF95_FREQ 434.0

// Singleton instance of the radio driver
RH_RF95 rf95(RFM95_CS, RFM95_INT);

// An arbitrary fixed maximum length of 128 bytes. You could increase it, but be careful of RX_BUFF_SIZE
// Increasing both will overflow RAM easily.
#define PACKET_LENGTH 128
#define HEADER_LENGTH 8
#define SMS_LENGTH    (PACKET_LENGTH - HEADER_LENGTH)

// I call this a "flexible union structure". Most messages will be under-sized.
// So I make sure the payload is right at the end. Then I only transmit what I need
// and not the full PACKET_LENGTH.
union smsEntry
{
	struct
	{
		uint8_t Type;	// Message type.
		uint8_t Size;	// Size of payload.
		uint16_t Index;	// Incremented counter of packets.
		uint16_t Time;	// Time sent.
		uint16_t CRC;	// CRC of payload.
		uint8_t payload[SMS_LENGTH];
	};

	uint8_t packet[PACKET_LENGTH];	// This array is used to push out throught the LoRa module.
};

// Hold message types.
enum
{
	typeACK	= 0x00,
	typeTX	= 0x01,
	typeRX	= 0x02,
	typeCMD	= 0x03,
};


// We have great gobs of RAM on the ESP32. You could increase this to 1024, but be careful!
#ifdef ESP_TARGET
#define RX_BUFF_SIZE  256
#define TX_BUFF_SIZE  256
#else
#define RX_BUFF_SIZE  5
#define TX_BUFF_SIZE  5
#endif

// I gave up on the CircularBuffer library.
//CircularBuffer<data::smsEntry, RX_BUFF_SIZE> RXbuffer;
//CircularBuffer<data::smsEntry, TX_BUFF_SIZE> TXbuffer;
RingBufCPP<smsEntry, RX_BUFF_SIZE> RXbuffer;
RingBufCPP<smsEntry, TX_BUFF_SIZE> TXbuffer;

smsEntry *Order[RX_BUFF_SIZE + TX_BUFF_SIZE];	// Keep track of the order of messages, not a nice way. Needs to change.
uint16_t Counter = 0x00;	// Used to keep track of total messages.


signed int recPacket(uint16_t WaitFor);
uint16_t CRC(uint8_t *Str, uint8_t Size);
uint8_t addRx(smsEntry *msg);
uint8_t addTx(uint8_t *PayLoad, uint8_t Size);
void sendMessage(uint8_t *PayLoad, uint8_t Size);
uint8_t getRx(uint8_t Index, uint8_t *printStr);
void BlinkError(void);
void BlinkNoReply(void);
bool checkACK(uint8_t txIndex, uint8_t rxIndex);
void rxWWW(void);
void sendACK(uint8_t msgIndex);
bool sendPacket(uint8_t msgIndex, uint16_t WaitFor);
void sendTest(void);
uint8_t getMessage(uint16_t msgIndex, uint8_t *printStr);


void setup()
{
	pinMode(BUILTIN_LED, OUTPUT);     
	pinMode(RFM95_RST, OUTPUT);
	digitalWrite(RFM95_RST, HIGH);

	// while (!Serial);
	Serial.begin(115200);
	delay(100);

#ifdef ESP_TARGET
	setup_wifi();
#endif
	PRINTLN("MickMake LoRa SMS");

	// manual reset
	digitalWrite(RFM95_RST, LOW);
	delay(10);
	digitalWrite(RFM95_RST, HIGH);
	delay(10);

	while (!rf95.init())
	{
		PRINTLN("LoRa radio init failed");
		while(1);
	}
	PRINTLN("LoRa radio init OK!");

	// Defaults after init are 434.0MHz, modulation GFSK_Rb250Fd250, +13dbM
	if (!rf95.setFrequency(RF95_FREQ))
	{
		PRINTLN("setFrequency failed");
		while(1);
	}
	PRINT("Set Freq to: "); PRINTLN(RF95_FREQ);

	// Defaults after init are 434.0MHz, 13dBm, Bw = 125 kHz, Cr = 4/5, Sf = 128chips/symbol, CRC on

	// The default transmitter power is 13dBm, using PA_BOOST.
	// If you are using RFM95/96/97/98 modules which uses the PA_BOOST transmitter pin, then 
	// you can set transmitter powers from 5 to 23 dBm:
	rf95.setTxPower(5, false);

	rf95.setModemConfig(RH_RF95::Bw125Cr48Sf4096);
	//rf95.setModemConfig(RH_RF95::Bw500Cr45Sf128);
	//rf95.setModemConfig(RH_RF95::Bw125Cr45Sf128);
	//rf95.printRegisters();
	rf95.setPreambleLength(8);

#ifdef ESP_TARGET
	//sendMessage((uint8_t *)"abcdefghijkl", 12);
#endif
}


void loop()
{
#ifdef ESP_TARGET
	wifi_loop();
#endif

	//sendMessage((uint8_t *)"abcdefgh", 8);
	signed int pIndex = recPacket(100);
	if (pIndex != -1)
	{
		sendACK(pIndex);
		PRINT("RxE:"); PRINT(RXbuffer.numElements());
		PRINT(" TxE:"); PRINTLN(TXbuffer.numElements());
	}

	//delay(2000);
}


void sendMessage(uint8_t *PayLoad, uint8_t Size)
{
	uint8_t txI = addTx(PayLoad, Size);
	sendPacket(txI, 5000);

	//uint8_t foo[128];
	//getMessage(txI, foo);
	//PRINT("SMS:");
	//PRINTLN((char *)foo);
}


uint8_t addTx(uint8_t *PayLoad, uint8_t Size)
{
	smsEntry TXmsg;

	TXmsg.Type = typeTX;
	TXmsg.Size = Size;
	TXmsg.Index = Counter;
	TXmsg.Time = (uint16_t)millis();
	TXmsg.CRC = (uint16_t)CRC(PayLoad, Size);
	memcpy((uint8_t *)TXmsg.payload, (uint8_t *)PayLoad, Size);
	TXbuffer.add(TXmsg);
	Order[Counter] = TXbuffer.peek(TXbuffer.numElements() - 1);
	PRINT("Size:"); PRINTLN((uint8_t)TXmsg.Size);

	// PRINT("Tx "); printPacket(&TXmsg);
	// PRINT("TxE:"); PRINTLN(TXbuffer.numElements());

	Counter++;
	// Need to reduce ring buffer to avoid over-run.
	if (TXbuffer.isFull())
	{
		TXbuffer.pull(&TXmsg);
		//Counter--;
	}

	return(TXbuffer.numElements()-1);  // Return the last index in the RB.
}


uint8_t addRx(smsEntry *msg)
{
	smsEntry RXmsg;

	if (msg->Type != typeACK)
		RXmsg.Type = typeRX;
	else
		RXmsg.Type = msg->Type;

	RXmsg.Size = (uint8_t)msg->Size;
	RXmsg.Index = Counter;
	RXmsg.Time = (uint16_t)msg->Time;
	RXmsg.CRC = (uint16_t)msg->CRC;
	memcpy((uint8_t *)RXmsg.payload, (uint8_t *)(msg->payload), msg->Size);
	RXbuffer.add(RXmsg);
	Order[Counter] = RXbuffer.peek(RXbuffer.numElements()-1);

	// smsEntry *msg2 = RXbuffer.peek(RXbuffer.numElements()-1); PRINT("Rx "); printPacket(msg2);
	// PRINT("RxE:"); PRINTLN(RXbuffer.numElements());

	Counter++;
	// Need to reduce ring buffer to avoid over-run.
	if (RXbuffer.isFull())
	{
		RXbuffer.pull(&RXmsg);
		//Counter--;
	}

	return(RXbuffer.numElements()-1);  // Return the last index in the RB.
}


void sendACK(uint8_t msgIndex)
{
	if (RXbuffer.isEmpty())
		return;

	smsEntry *RXmsg = RXbuffer.peek(msgIndex);
	PRINT("ACK "); printPacket(RXmsg);

	if (RXmsg->Type == typeACK)
	{
		//RXbuffer.pull(RXmsg);
		//Counter--;
		return;
	}

	smsEntry TXmsg;
	TXmsg.Type = typeACK;
	TXmsg.Size = (uint8_t)RXmsg->Size;
	TXmsg.Index = (uint16_t)RXmsg->Index;
	TXmsg.Time = (uint16_t)RXmsg->Time;
	TXmsg.CRC = (uint16_t)RXmsg->CRC;
	memcpy((uint8_t *)TXmsg.payload, (uint8_t *)(RXmsg->payload), RXmsg->Size);
	PRINT("Tx(ACK) "); printPacket(&TXmsg);

	rf95.send((uint8_t *)(TXmsg.packet), HEADER_LENGTH + TXmsg.Size);

	digitalWrite(BUILTIN_LED, LOW);
	rf95.waitPacketSent();
}


uint8_t getRx(uint8_t msgIndex, uint8_t *printStr)
{
	if (RXbuffer.isEmpty())
		return(0);

	smsEntry *RXmsg = RXbuffer.peek(msgIndex);
	memcpy((char *)printStr, (uint8_t *)(RXmsg->payload), RXmsg->Size);
	return(1);
}


uint8_t getMessage(uint16_t msgIndex, uint8_t *printStr)
{
	uint8_t Type;

	if (RXbuffer.isEmpty() && TXbuffer.isEmpty())
		return(0);

	if (Order[msgIndex]->Type != typeACK)
	{
		memcpy(printStr, Order[msgIndex]->payload, Order[msgIndex]->Size);
		printStr[Order[msgIndex]->Size + 0x00] = 0x00;
		Type = Order[msgIndex]->Type;
	}

	return(Type);
}


uint16_t numMessages(void)
{
	return(Counter);
}


bool sendPacket(uint8_t msgIndex, uint16_t WaitFor)
{
	if (TXbuffer.isEmpty())
		return(0);

	smsEntry *TXmsg = TXbuffer.peek(msgIndex);
	PRINT("Tx "); printPacket(TXmsg);

	rf95.send((uint8_t *)TXmsg->packet, HEADER_LENGTH + TXmsg->Size);

	digitalWrite(BUILTIN_LED, HIGH);
	rf95.waitPacketSent();

	signed int rxI = recPacket(WaitFor);
	//PRINT("F:"); PRINTLN(rxI);
	if (rxI == -1)
	{
		PRINTLN("Timeout on ACK!");
		return(0);
	}
	else
	{
		if (!checkACK(msgIndex, rxI))
			return(0);
	}

	digitalWrite(BUILTIN_LED, LOW);

	return(1);
}


bool checkACK(uint8_t txIndex, uint8_t rxIndex)
{
	if (TXbuffer.isEmpty() || RXbuffer.isEmpty())
		return(0);

	smsEntry *TXmsg = TXbuffer.peek(txIndex);
	smsEntry *RXmsg = RXbuffer.peek(rxIndex);

	// PRINT("check Tx("); PRINT(txIndex); PRINT("):"); printPacket(TXmsg); PRINT("check Rx("); PRINT(rxIndex); PRINT("):"); printPacket(RXmsg);

	if (RXmsg->Type == typeACK)
	{
		if ((TXmsg->Time == RXmsg->Time) && (TXmsg->Size == RXmsg->Size) && (TXmsg->CRC == RXmsg->CRC))
		{
			if (strncmp((char *)(RXmsg->payload), (char *)(TXmsg->payload), TXmsg->Size) == 0x00)
			{
				PRINT("ACK: OK ");
				PRINTLN(RXbuffer.numElements());
				//smsEntry RXmsg;
				//RXbuffer.pull(&RXmsg);
				//Counter--;
				return(1);
			}
			PRINTLN("ACK: NOK1");
		}
		PRINTLN("ACK: NOK2");
		//smsEntry RXmsg;
		//RXbuffer.pull(&RXmsg);
		//Counter--;
	}
	PRINT("ACK: NOK3 ");

	return(0);
}


signed int recPacket(uint16_t WaitFor)
{
	uint8_t buf[SMS_LENGTH];
	uint8_t len = sizeof(buf);
	signed int RetVal = -1;
	WaitFor /= 10;
	smsEntry RXmsg;

	while(WaitFor--)
	{
		if (rf95.waitAvailableTimeout(10))
		{
			if (rf95.recv((uint8_t *)(RXmsg.packet), &len))
			{
				RetVal = addRx(&RXmsg);
				WaitFor = 0;
			}
		}
	}

	return(RetVal);
}


void printPacket(smsEntry *msg)
{
	PRINT(" t:");
	PRINT(msg->Type, HEX);
	PRINT(" s:");
	PRINT(msg->Size, HEX);
	PRINT(" i:");
	PRINT(msg->Index, HEX);
	PRINT(" w:");
	PRINT(msg->Time, HEX);
	PRINT(" c:");
	PRINT(msg->CRC, HEX);
	PRINT(" p:");
	uint8_t p_size = HEADER_LENGTH + msg->Size;

	for(uint8_t i = 0x00; (i < p_size); i++)
	{
		char temp[4];
		sprintf(temp, "%.2X:", (uint8_t)(msg->packet[i]));
		PRINT(temp);
	}
	PRINTLN("");
}


void BlinkError(void)
{
	for(int i=0; (i<3); i++)
	{
		digitalWrite(BUILTIN_LED, HIGH);
		delay(100);
		digitalWrite(BUILTIN_LED, LOW);
		delay(100);
	}
}


void BlinkNoReply(void)
{
	for(int i=0; (i<3); i++)
	{
		digitalWrite(BUILTIN_LED, HIGH);
		delay(300);
		digitalWrite(BUILTIN_LED, LOW);
		delay(200);
	}
}


uint16_t CRC(uint8_t *Str, uint8_t Size)
{
	CRC16 crc;
	char CRCcheck[SMS_LENGTH];
	memcpy((char *)CRCcheck, (uint8_t *)Str, Size);

	crc.processBuffer((char *)CRCcheck, Size);

	return(crc.getCrc());
}

