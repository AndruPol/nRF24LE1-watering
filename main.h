/*
 * main.h
 *
 *  Created on: 10 июля 2016 г.
 *      Author: andru
 */

#ifndef MAIN_H_
#define MAIN_H_

#define DEBUG	0	// debug output via UART
#define printf	printf_tiny

#define EN_AES	1
#define MSGLEN	sizeof(MESSAGE_T)

typedef struct CONFIG CONFIG_T;
struct CONFIG {
	uint8_t version;	// configuration version
	uint8_t deviceID;	// this device ID
	uint8_t channel;	// radio channel: 0-199
	uint8_t datarate;	// data rate: 1-3
	uint8_t autoask;	// auto ask
	uint8_t srvaddr[5];	// server address to send
	uint8_t devaddr[5];	// server address to send
	uint8_t maxsend;	// max message send retries
	uint16_t sleeptm;	// wakeup by watchdog timeout, s ~8.5min max (0x1-0x1FF LSB first)
	uint8_t soilen;		// soil moisture sensor enable in bit: 0 - soil1, 1 - soil2, 2 -soil3
	uint16_t rangelow;	// distance range too low
	uint16_t waittm;	// server response wait timeout
	uint16_t vbatlow;	// battery low
#if EN_AES
	uint8_t useaes;		// use aes encryption
	uint8_t aeskey[16];	// aes encryption key
#endif
	uint8_t crcbyte;	// CRC8 sizeof(config) - 1
};

typedef enum {
	ADDR_VBAT = 0,		// BATTERY voltage (ADC)
	ADDR_SOIL1,			// SOIL 1 (ADC)
	ADDR_SOIL2,
	ADDR_SOIL3,
	ADDR_HCSR04,
	ADDR_PUMP,
	CFG_SLEEP,			// uint16_t sleeptm;
	CFG_SOIL,			// uint8_t soilen;
	CFG_RANGE,			// uint16_t rangelow;
	CFG_WAIT,			// uint16_t waittm;
	CFG_VBAT,			// uint16_t vbatlow;
} address_t;

typedef enum {
	SENSOR_INFO = 0,	// not implemented
	SENSOR_DATA,
	SENSOR_ERROR,
	SENSOR_CMD,
} msgtype_t;

typedef enum {
	DS1820 = 0,
	BH1750,
	DHT,
	BMP085,
	ADC,
	HCSR04,
} sensortype_t;

typedef enum {
	TEMPERATURE = 0,
	HUMIDITY,
	PRESSURE,
	LIGHT,
	VOLTAGE,
	DISTANCE,
} valuetype_t;

typedef enum {
	CMD_CFGREAD = 1,	// read configuration value
	CMD_CFGWRITE,		// write configuration value
	CMD_RESET,			// reset device
	CMD_SENSREAD = 10,	// read sensor value
	CMD_ON = 20,		// ON
	CMD_OFF,			// OFF
	CMD_ONTM,			// ON timeout (S) message.data.iValue
	CMD_OFFTM,			// OFF timeout (S) message.data.iValue
	GET_REG,			// get modbus register
	SET_REG,			// set modbus register
} command_t;

// message format
typedef struct MESSAGE MESSAGE_T;
struct MESSAGE {
	msgtype_t msgType;			// message type: 0 - info, 1 - sensor value, 2 - sensor error, 3 - command
	uint8_t deviceID;			// remote device ID
	sensortype_t sensorType;	// sensor type: 0 - DS1820, 1 - BH1750, 2 - DHT сенсор, 3 - BMP085, 4 - ADC
	valuetype_t valueType;		// value type: 0 - temperature, 1 - humidity, 2 - pressure, 3 - light, 4 - voltage
	uint8_t address;			// internal sensor address
	command_t command;			// command
	uint8_t error;				// sensor error code
	uint8_t notused[5];			//
	union	{					// sensor value depend of sensor type
		float	fValue;
		int32_t	iValue;
		uint8_t cValue[4];
	} data;
};

typedef enum {
	PUMP_OK = 0,				// PUMP no error
	PUMP_RANGELOW,
	PUMP_HCSR04ERR,
	PUMP_VBATLOW,
	PUMP_CMDERR,
	PUMP_PARAMERR,
} pumperr_t;

typedef enum {
	VBAT_OK = 0,				// not used
	VBAT_LOW,
} vbaterr_t;

extern CONFIG_T config;

#endif /* MAIN_H_ */
