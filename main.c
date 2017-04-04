//#include <stdint.h>
#include <stdio.h>
#include <string.h>

//подключение необходимых функций:
#include "delay.h"
#include "gpio.h"
#include "adc.h"
#include "interrupt.h"
#include "memory.h"

#include "main.h"
#include "radio.h"
#include "hcsr04.h"
#include "crc8.h"

#define EN_LED		1	// LED enable
#define EN_RF		1	// radio enable
#define EN_ADC		1	// ADC enable
#define EN_HCSR04	1	// Ultrasonic HC-SR04 sensor
#define EN_RTC		1	// RTC 1s timer
#define EN_SLEEP	1	// powersave enable
#if DEBUG
#define EN_UART		1	// use UART for debugging
#define UARTTXPIN	GPIO_PIN_ID_P0_3		// P0.3 - UART debug
#define UARTRXPIN	GPIO_PIN_ID_P0_4		// P0.4	- UART debug
#else
#define EN_UART		0	// use UART for debugging
#endif

#define VBATCH		ADC_CHANNEL_AIN0		// P0.0 - VBAT voltage
#define SOIL1CH		ADC_CHANNEL_AIN1		// P0.1 - soil sensor 1
#define SOIL2CH		ADC_CHANNEL_AIN2		// P0.2 - soil sensor 2
#define SOIL3CH		ADC_CHANNEL_AIN5		// P0.5 - soil sensor 3

#define IFPPIN		GPIO_PIN_ID_P0_6		// P0.6 - HC-SR04 ECHO pin

#define PUMPPIN		GPIO_PIN_ID_P1_2		// P1.6 - PUMP pin
#define LEDPIN		GPIO_PIN_ID_P1_4		// P1.4 - LED pin
#define TRIGPIN		GPIO_PIN_ID_P1_5		// P1.5 - HC-SR04 TRIG pin
#define PWR5VPIN	GPIO_PIN_ID_P1_6		// P1.6 - DC/DC 5V on pin

#define SOIL1MASK	0x1
#define SOIL2MASK	0x2
#define SOIL3MASK	0x4

#define PUMPTM		9000		// wait RF command 9000 * ~20uS = 180mS

#define NVM_START_ADDRESS	MEMORY_FLASH_NV_STD_END_START_ADDRESS
#define ENVM_START_ADDRESS	MEMORY_FLASH_NV_EXT_END_START_ADDRESS
#define ENVM_PAGE_NUM		MEMORY_FLASH_NV_EXT_END_FIRST_PAGE_NUM

CONFIG_T config;

#if EN_UART
#include "uart.h"
#endif

#if EN_SLEEP
#include "watchdog.h"
#include "pwr_clk_mgmt.h"
#endif

#if EN_RTC
#include "rtc2.h"
#include "pwr_clk_mgmt.h"
static volatile uint16_t rtccnt;

interrupt_isr_rtc2() {
	if (rtccnt > 0)
		rtccnt--;
}
#endif

// PUMP current state
static volatile uint8_t pump;

// halt
void halt(void) {
	while (1) {
#if EN_LED
		gpio_pin_val_complement(LEDPIN);
#endif
		delay_ms(250);
	}
}

// write NVM config to eNVM
static uint8_t write_config(void) {
	uint8_t ret = CRC8((uint8_t *) &config, sizeof(CONFIG_T) - 1);
	config.crcbyte = ret;
	if (memory_flash_erase_page(ENVM_PAGE_NUM) != MEMORY_FLASH_OK)
		return 0;
	if (memory_flash_write_bytes(ENVM_START_ADDRESS, sizeof(CONFIG_T), (uint8_t *) &config) != MEMORY_FLASH_OK)
		return 0;
	return 1;
}

static void read_config(uint16_t addr) {
	uint16_t i;
	memory_movx_accesses_data_memory();
	for (i = 0; i < sizeof(CONFIG_T); i++) {
		*((uint8_t*) &config + i) = *((__xdata uint8_t*) addr + i);
	}
}

#if EN_ADC
#define ADC_SAMPLES		6
static uint16_t adc_get_middle(adc_channel_t channel) {
	uint8_t i;
	uint16_t current, middle = 0;

	for (i = 0; i < ADC_SAMPLES; i++) {
		current = adc_start_single_conversion_get_value(channel);
		if (i == 0) {
			middle = current;
		} else {
			middle += current;
			middle = middle >> 1;
		}
	}
	return middle;
}
#endif

static void send_vbat(uint16_t *vbat) {
	MESSAGE_T message = {0};

	*vbat = adc_get_middle(VBATCH);
	message.deviceID = config.deviceID;
	message.msgType = SENSOR_DATA;
	message.sensorType = ADC;
	message.valueType = VOLTAGE;
	message.address = ADDR_VBAT;
	message.data.iValue = *vbat;
#if DEBUG
	printf("vbat=%d, vlow=%d\r\n", *vbat, config.vbatlow);
#endif
	rfsend(&message);
	if (*vbat < config.vbatlow) {
		message.msgType = SENSOR_ERROR;
		message.error = VBAT_LOW;
		rfsend(&message);
	}
}

static void send_soil(uint8_t addr) {
	MESSAGE_T message = {0};
	uint16_t soil = 0;

	message.deviceID = config.deviceID;
	message.msgType = SENSOR_DATA;
	message.sensorType = ADC;
	message.valueType = HUMIDITY;
	switch (addr) {
	case ADDR_SOIL1:
		soil = adc_get_middle(SOIL1CH);
		message.address = ADDR_SOIL1;
		message.data.iValue = soil;
		break;
	case ADDR_SOIL2:
		soil = adc_get_middle(SOIL2CH);
		message.address = ADDR_SOIL2;
		message.data.iValue = soil;
		break;
	case ADDR_SOIL3:
		soil = adc_get_middle(SOIL3CH);
		message.address = ADDR_SOIL3;
		message.data.iValue = soil;
		break;
	default:
		break;
	}
#if DEBUG
	printf("soil%d=%d\r\n", addr, soil);
#endif
	rfsend(&message);
}

// HC-SR04 read
static hcsr04_state_t send_hcsr04(uint16_t *range) {
	MESSAGE_T message;
	uint8_t ret;

	message.deviceID = config.deviceID;
	message.sensorType = HCSR04;
	message.valueType = DISTANCE;
	message.address = ADDR_HCSR04;

	ret = hcsr04_read(range);
#if DEBUG
	printf("range=%d, range low=%d\r\n", *range, config.rangelow);
#endif
	if (ret == HCSR04_OK) {
		message.msgType = SENSOR_DATA;
		message.data.iValue = *range;
	} else {
		message.msgType = SENSOR_ERROR;
		message.data.iValue = *range;
		message.error = ret;
#if DEBUG
		printf("range error %d\r\n", ret);
#endif
	}
	rfsend(&message);
	return ret;
}

void power5v_set(uint8_t flag) {
	if (flag) {
		// 5V power on
		gpio_pin_configure(PWR5VPIN,
			GPIO_PIN_CONFIG_OPTION_DIR_OUTPUT |
			GPIO_PIN_CONFIG_OPTION_OUTPUT_VAL_SET |
			GPIO_PIN_CONFIG_OPTION_PIN_MODE_OUTPUT_BUFFER_NORMAL_DRIVE_STRENGTH
		);
	} else {
		// 5V power off
		gpio_pin_configure(PWR5VPIN,
			GPIO_PIN_CONFIG_OPTION_DIR_OUTPUT |
			GPIO_PIN_CONFIG_OPTION_OUTPUT_VAL_CLEAR |
			GPIO_PIN_CONFIG_OPTION_PIN_MODE_OUTPUT_BUFFER_NORMAL_DRIVE_STRENGTH
		);
	}
}

void pump_set(uint8_t flag) {
	pump = flag != 0;
	if (flag) {
		gpio_pin_configure(PUMPPIN,
			GPIO_PIN_CONFIG_OPTION_DIR_OUTPUT |
			GPIO_PIN_CONFIG_OPTION_OUTPUT_VAL_SET |
			GPIO_PIN_CONFIG_OPTION_PIN_MODE_OUTPUT_BUFFER_NORMAL_DRIVE_STRENGTH
		);
	} else {
		gpio_pin_configure(PUMPPIN,
			GPIO_PIN_CONFIG_OPTION_DIR_OUTPUT |
			GPIO_PIN_CONFIG_OPTION_OUTPUT_VAL_CLEAR |
			GPIO_PIN_CONFIG_OPTION_PIN_MODE_OUTPUT_BUFFER_NORMAL_DRIVE_STRENGTH
		);
	}
}

void send_pump(uint8_t state, uint8_t error) {
	MESSAGE_T message;

#if DEBUG
		printf("pump state=%d, error=%d\r\n", state, error);
#endif
	message.deviceID = config.deviceID;
	message.address = ADDR_PUMP;
	message.msgType = SENSOR_INFO;
	message.data.iValue = state;
	rfsend(&message);

	if (error) {
		message.msgType = SENSOR_ERROR;
		message.error = error;
		rfsend(&message);
	}
}

void send_config(uint8_t addr, uint16_t value) {
	MESSAGE_T message;

#if DEBUG
		printf("addr=%d, value=%d\r\n", addr, (uint16_t) value);
#endif
	message.msgType = SENSOR_INFO;
	message.deviceID = config.deviceID;
	message.address = addr;
	message.data.iValue = (uint16_t) value;
	rfsend(&message);
}

void send_config_err(uint8_t addr, uint8_t errcode) {
	MESSAGE_T message;

#if DEBUG
		printf("addr=%d, config error=%d\r\n", addr, errcode);
#endif
	message.msgType = SENSOR_ERROR;
	message.deviceID = config.deviceID;
	message.address = addr;
	message.error = errcode;
	rfsend(&message);
}


// main
void main(void) {

	// start variable definition
	uint8_t ret, cmd;
	uint16_t value;

	MESSAGE_T message;

	// start program code
#if EN_LED
	// настроим порт LED
	gpio_pin_configure(LEDPIN,
		GPIO_PIN_CONFIG_OPTION_DIR_OUTPUT |
		GPIO_PIN_CONFIG_OPTION_OUTPUT_VAL_CLEAR |
		GPIO_PIN_CONFIG_OPTION_PIN_MODE_OUTPUT_BUFFER_NORMAL_DRIVE_STRENGTH
		);
#endif

#if EN_UART
	// Setup UART pins
	gpio_pin_configure(GPIO_PIN_ID_FUNC_RXD,
		GPIO_PIN_CONFIG_OPTION_DIR_INPUT |
		GPIO_PIN_CONFIG_OPTION_PIN_MODE_INPUT_BUFFER_ON_NO_RESISTORS
		);

	gpio_pin_configure(GPIO_PIN_ID_FUNC_TXD,
		GPIO_PIN_CONFIG_OPTION_DIR_OUTPUT |
		GPIO_PIN_CONFIG_OPTION_OUTPUT_VAL_SET |
		GPIO_PIN_CONFIG_OPTION_PIN_MODE_OUTPUT_BUFFER_NORMAL_DRIVE_STRENGTH
		);

	uart_configure_8_n_1_38400();
#endif

#if DEBUG
		printf("program start\r\n");
#endif

	read_config(NVM_START_ADDRESS);
	ret = CRC8((uint8_t *) &config, sizeof(CONFIG_T)-1);
	if (config.crcbyte != ret) {
		// NVM config wrong stop work
		halt();
	}

	value = config.version;
	read_config(ENVM_START_ADDRESS);
	ret = CRC8((uint8_t *) &config, sizeof(CONFIG_T)-1);
	if (config.crcbyte != ret || config.version != value) {
		read_config(NVM_START_ADDRESS);
		if (!write_config()) {
			// config write error stop work
			halt();
		}
	}

	// 5V power on
	power5v_set(1);

	// default PUMP power off
	pump_set(0);

#if EN_RF
 	radio_init();
#endif

#if EN_ADC
	adc_configure((uint16_t) ADC_CONFIG_OPTION_RESOLUTION_12_BITS |
		ADC_CONFIG_OPTION_REF_SELECT_VDD  |
		ADC_CONFIG_OPTION_SAMPLING_SINGLE_STEP |
		ADC_CONFIG_OPTION_ACQ_TIME_12_US |
		ADC_CONFIG_OPTION_RESULT_JUSTIFICATION_RIGHT
		);
#endif

	hcsr04_init();

#if EN_RTC
	//CLKLF is not running, so enable RCOSC32K and wait until it is ready
	pwr_clk_mgmt_clklf_configure(PWR_CLK_MGMT_CLKLF_CONFIG_OPTION_CLK_SRC_RCOSC32K);
	pwr_clk_mgmt_wait_until_clklf_is_ready();

	interrupt_control_rtc2_enable();
	rtc2_configure(
		RTC2_CONFIG_OPTION_COMPARE_MODE_0_RESET_AT_IRQ,
		32767);			// 1s
#endif

	interrupt_control_global_enable();

	delay_ms(20);

	// main loop
	while(1) {

#if EN_LED
		gpio_pin_val_complement(LEDPIN);
#endif

#if DEBUG
		printf("loop next\r\n");
#endif

#if EN_ADC
		// read & send VBAT value
		if (!pump) {
			send_vbat(&value);
		} else {
			value = adc_get_middle(VBATCH);
		}
		// check battery voltage
		if (value < config.vbatlow) {
			if (pump) {
				pump_set(0);
				send_pump(pump, PUMP_VBATLOW);
				if (RTC2CON & RTC2CON_ENABLE) {
					rtc2_stop();
				}
			}
			goto VBATLOW;
		}
#endif

		if (pump) {
			// check PUMP delay counter
			if (RTC2CON & RTC2CON_ENABLE) {
				if (rtccnt == 0) {
					rtc2_stop();
					pump_set(0);
					send_pump(pump, PUMP_OK);
					goto WAITCMD;
				}
			}
			// check range
			ret = hcsr04_read(&value);
			if (ret == HCSR04_OK) {
				if (value > config.rangelow) {
					pump_set(0);
					send_pump(pump, PUMP_RANGELOW);
				} else {
					send_pump(pump, PUMP_OK);
				}
			} else {
				pump_set(0);
				send_pump(pump, PUMP_HCSR04ERR);
			}
		}

WAITCMD:

		memset(&message, 0, MSGLEN);
		// wait command from smarthome gateway
		if (!pump) {
			cmd = rfread(&message, config.waittm);
		} else {
			cmd = rfread(&message, PUMPTM);
		}

		if (cmd) {
			if (message.deviceID == config.deviceID && message.msgType == SENSOR_CMD) {
#if DEBUG
				printf("\r\ncommand: %d\r\n", message.command);
				printf("address: %d\r\n", message.address);
				printf("param: %d\r\n\r\n", (uint16_t) message.data.iValue);
#endif
				// команда чтения показаний
				if (message.command == CMD_SENSREAD) {
					if (pump) break;
					switch (message.address) {
					case ADDR_VBAT:
						send_vbat(&value);
						break;
					case ADDR_SOIL1:
						send_soil(ADDR_SOIL1);
						break;
					case ADDR_SOIL2:
						send_soil(ADDR_SOIL2);
						break;
					case ADDR_SOIL3:
						send_soil(ADDR_SOIL3);
						break;
					case ADDR_HCSR04:
						send_hcsr04(&value);
						break;
					case ADDR_PUMP:
						send_pump(pump, PUMP_OK);
						break;
					default:
						break;
					}
				} else if (message.command >= CMD_ON) {
					if (message.address != ADDR_PUMP) break;
					switch (message.command) {
					case CMD_ON:
						if (pump) break;
						delay_ms(50);
						ret = hcsr04_read(&value);
						if (ret != HCSR04_OK) {
							send_pump(pump, PUMP_HCSR04ERR);
							break;
						}
						if (value > config.rangelow) {
							send_pump(pump, PUMP_RANGELOW);
							break;
						}
						pump_set(1);
						send_pump(pump, PUMP_OK);
						break;
					case CMD_OFF:
						pump_set(0);
						send_pump(pump, PUMP_OK);
						break;
					case CMD_ONTM:
						if (pump) break;
						if (message.data.iValue == 0) {
							send_pump(pump, PUMP_PARAMERR);
							break;
						}
						ret = hcsr04_read(&value);
						if (ret != HCSR04_OK) {
							send_pump(pump, PUMP_HCSR04ERR);
							break;
						}
						if (value > config.rangelow) {
							send_pump(pump, PUMP_RANGELOW);
							break;
						}
						rtccnt = message.data.iValue;
						rtc2_run();
						pump_set(1);
						send_pump(pump, PUMP_OK);
						break;
					default:
						send_pump(pump, PUMP_CMDERR);
						break;
					}
				} else if (message.command < CMD_SENSREAD) {
					if (pump) break;
					if (!(message.command == CMD_CFGREAD || message.command == CMD_CFGWRITE)) break;
					switch (message.address) {
					case CFG_SLEEP:
						if (message.command == CMD_CFGREAD) {
							send_config(CFG_SLEEP, config.sleeptm);
						} else {
							config.sleeptm = (uint16_t) message.data.iValue;
							if (!write_config())	{
								send_config_err(CFG_SLEEP, 1);
								break;
							}
							send_config(CFG_SLEEP, config.sleeptm);
						}
						break;
					case CFG_SOIL:
						if (message.command == CMD_CFGREAD) {
							send_config(CFG_SOIL, config.soilen);
						} else {
							config.soilen = (uint8_t) message.data.iValue;
							if (!write_config()) {
								send_config_err(CFG_SOIL, 1);
								break;
							}
							send_config(CFG_SOIL, config.soilen);
						}
						break;
					case CFG_RANGE:
						if (message.command == CMD_CFGREAD) {
							send_config(CFG_RANGE, config.rangelow);
						} else {
							config.rangelow = (uint16_t) message.data.iValue;
							if (!write_config()) {
								send_config_err(CFG_RANGE, 1);
								break;
							}
							send_config(CFG_RANGE, config.rangelow);
						}
						break;
					case CFG_WAIT:
						if (message.command == CMD_CFGREAD) {
							send_config(CFG_WAIT, config.waittm);
						} else {
							config.waittm = (uint16_t) message.data.iValue;
							if (!write_config()) {
								send_config_err(CFG_WAIT, 1);
								break;
							}
							send_config(CFG_WAIT, config.waittm);
						}
						break;
					case CFG_VBAT:
						if (message.command == CMD_CFGREAD) {
							send_config(CFG_VBAT, config.vbatlow);
						} else {
							config.vbatlow = (uint16_t) message.data.iValue;
							if (!write_config()) {
								send_config_err(CFG_VBAT, 1);
								break;
							}
							send_config(CFG_VBAT, config.vbatlow);
						}
						break;
					default:
						break;
					}
				}
			}

		}

		if (pump) continue;
		if (cmd) goto WAITCMD;

		// read & send SOIL1 value
		if (config.soilen & SOIL1MASK) {
			send_soil(ADDR_SOIL1);
		}
		// read & send SOIL2 value
		if (config.soilen & SOIL2MASK) {
			send_soil(ADDR_SOIL2);
		}
		// read & send SOIL3 value
		if (config.soilen & SOIL3MASK) {
			send_soil(ADDR_SOIL3);
		}
		send_hcsr04(&value);
		send_pump(pump, PUMP_OK);

// battery low force to powersave
VBATLOW:

#if EN_SLEEP
		if (config.sleeptm > 0) {
			// clear unneeded pins
			gpio_pin_val_clear(LEDPIN);
			pump_set(0);
			power5v_set(0);
			adc_power_down();
			rfpwrDown();

			watchdog_setup();
			watchdog_set_wdsv_count(watchdog_calc_timeout_from_sec(config.sleeptm));
			pwr_clk_mgmt_op_mode_configure(
				PWR_CLK_MGMT_OP_MODE_CONFIG_OPTION_RUN_WD_NORMALLY
				| PWR_CLK_MGMT_OP_MODE_CONFIG_OPTION_RETENTION_LATCH_LOCKED
			);

	    	pwr_clk_mgmt_enter_pwr_mode_memory_ret_tmr_on(); // 1mkA
		}
#endif

		delay_s(10);
	} // main loop

}
