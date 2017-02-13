nRF24LE1-watering
===============

nRF24LE1 remote watering module for my smarthome system.
Ultrasonic HC-SR04 distance meter, soil moisture sensors, AES encryption

based on great nRF24LE1 SDK https://github.com/DeanCording/nRF24LE1_SDK

**Pinout module:**

`VBATCH		ADC_CHANNEL_AIN0		// P0.0 - VBAT voltage pin`

`SOIL1CH	ADC_CHANNEL_AIN1		// P0.1 - Soil sensor 1`

`SOIL2CH	ADC_CHANNEL_AIN2		// P0.2 - Soil sensor 2`

`UARTTXPIN	GPIO_PIN_ID_P0_3		// P0.3 - UART TX (debug)`

`UARTRXPIN	GPIO_PIN_ID_P0_4		// P0.4 - UART RX (debug)`

`SOIL3CH	ADC_CHANNEL_AIN6		// P0.6 - Soil sensor 3`

`IFPPIN		GPIO_PIN_ID_P0_6		// P0.6 - HC-SR04 ECHO pin`

`PWR5VPIN	GPIO_PIN_ID_P1_2		// P1.2 - power 5V pin`

`LEDPIN		GPIO_PIN_ID_P1_4		// P1.4 - LED pin`

`TRIGPIN	GPIO_PIN_ID_P1_5		// P1.5 - HC-SR04 TRIG pin`

`PUMPPIN	GPIO_PIN_ID_P1_6		// P1.6 - Pump pin`

