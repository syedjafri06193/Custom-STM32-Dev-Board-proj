/* Board definition: the pinout and the clock plan, in one place.
 *
 * The design document specifies the parts and the architecture but not a
 * pinout, so this is the one chosen here and the reasoning for each choice is
 * written down next to it.  A pinout is the hardest thing to change after
 * layout, and "why is the CAN on PB8/PB9?" is exactly the question nobody can
 * answer six months later.
 */

#ifndef BOARD_H
#define BOARD_H

#include "stm32g474.h"

/* ------------------------------------------------------------------ clocks */

/* 16 MHz crystal.  Section 7.2 of the design document makes the crystal
 * non-negotiable: the FDCAN bit timing tolerates roughly 0.5% of oscillator
 * error and the HSI16's +/-1% does not fit inside that.  can_timing.c turns
 * that claim into an assertion the host tests run on every build.
 *
 * 16 MHz / 4 = 4 MHz PLL input (spec range 2.66-16 MHz)
 *     x 85    = 340 MHz VCO   (spec range 96-344 MHz)
 *     / 2 (R) = 170 MHz SYSCLK -- the part's maximum, and boost mode
 *     / 2 (Q) = 170 MHz FDCAN kernel clock
 *
 * The FDCAN kernel clock is deliberately taken from PLLQ rather than PCLK1 so
 * that the bit timing stops depending on the APB prescaler.  Somebody halving
 * PCLK1 for a power experiment should not silently halve every CAN bit rate
 * on the bus. */
#define BOARD_HSE_HZ 16000000u
#define BOARD_PLL_M 4u
#define BOARD_PLL_N 85u
#define BOARD_PLL_R 2u
#define BOARD_PLL_Q 2u
#define BOARD_SYSCLK_HZ 170000000u
#define BOARD_HCLK_HZ 170000000u
#define BOARD_PCLK1_HZ 170000000u
#define BOARD_PCLK2_HZ 170000000u
#define BOARD_FDCAN_CLK_HZ 170000000u

/* --------------------------------------------------------------- GPIO map */

/* Debug and console (section 11.3: a real SWD header and a UART for printf). */
#define PIN_SWDIO_PORT GPIOA
#define PIN_SWDIO 13
#define PIN_SWCLK_PORT GPIOA
#define PIN_SWCLK 14

#define CONSOLE_UART USART2
#define PIN_UART_TX_PORT GPIOA
#define PIN_UART_TX 2
#define PIN_UART_RX_PORT GPIOA
#define PIN_UART_RX 3
#define PIN_UART_AF 7u
#define CONSOLE_BAUD 115200u

/* MCO.  Bring-up step 12 says to verify the crystal by measuring the system
 * clock on MCO, not by probing the crystal pins -- a scope probe on OSC_IN
 * loads the resonator and can stop it oscillating, which looks exactly like a
 * dead crystal. */
#define PIN_MCO_PORT GPIOA
#define PIN_MCO 8
#define PIN_MCO_AF 0u

/* SPI1 to the ADS1220, across the ADuM4154 barrier.  Mode 1 (CPOL=0,CPHA=1),
 * which is what the ADS1220 wants. */
#define ISO_SPI SPI1
#define PIN_SPI_SCK_PORT GPIOA
#define PIN_SPI_SCK 5
#define PIN_SPI_MISO_PORT GPIOA
#define PIN_SPI_MISO 6
#define PIN_SPI_MOSI_PORT GPIOA
#define PIN_SPI_MOSI 7
#define PIN_SPI_AF 5u

/* CS is a plain GPIO, not SPI1_NSS.  The ADS1220 latches a command on CS
 * rising, so CS has to frame a whole transaction rather than toggle per byte
 * the way hardware NSS does. */
#define PIN_ADC_CS_PORT GPIOA
#define PIN_ADC_CS 4
#define PIN_ADC_DRDY_PORT GPIOB
#define PIN_ADC_DRDY 0

/* VBUS sense divider -> ADC1_IN1 on PA0 (section 6.6). */
#define PIN_VBUS_SENSE_PORT GPIOA
#define PIN_VBUS_SENSE 0
#define VBUS_ADC_CHANNEL 1u

/* FDCAN1 on PB8/PB9 rather than the PA11/PA12 alternative, because those two
 * pins are also USB_DM/USB_DP.  Spending them on CAN forecloses ever adding a
 * USB device interface, and on a board whose whole front end is a USB-C
 * connector that is a bad trade for two pins. */
#define PIN_CAN_RX_PORT GPIOB
#define PIN_CAN_RX 8
#define PIN_CAN_TX_PORT GPIOB
#define PIN_CAN_TX 9
#define PIN_CAN_AF 9u
#define PIN_CAN_STB_PORT GPIOB /* transceiver standby, active high */
#define PIN_CAN_STB 12

/* UCPD1 CC lines.  These are analog-mode pins with no alternate function
 * selection; the peripheral owns the pad. */
#define PIN_CC1_PORT GPIOB
#define PIN_CC1 6
#define PIN_CC2_PORT GPIOB
#define PIN_CC2 4

/* TCPP01-M12.  DBn released once the MCU is alive and has taken over the CC
 * lines -- see section 6.2, the bricking trap. */
#define PIN_TCPP_DBN_PORT GPIOB
#define PIN_TCPP_DBN 5

/* Load switch gating the bulk capacitance.  Section 6.3: Type-C bounds sink
 * bypass capacitance to about 10 uF before a contract exists, so the 470 uF
 * of bulk stays disconnected until the contract is explicit. */
#define PIN_BULK_EN_PORT GPIOB
#define PIN_BULK_EN 10

/* Isolated DC-DC enable.  Being able to turn it off in firmware is what makes
 * the section 16 noise measurement -- island on bench power versus island on
 * the converter -- a two-line change rather than a soldering job. */
#define PIN_ISO_PWR_EN_PORT GPIOC
#define PIN_ISO_PWR_EN 4

/* LEDs (section 11.3).  Rail LEDs are hardware-only; these are the three the
 * firmware drives. */
#define PIN_LED_USER_PORT GPIOB
#define PIN_LED_USER 13
#define PIN_LED_CAN_PORT GPIOB
#define PIN_LED_CAN 14
#define PIN_LED_FAULT_PORT GPIOB
#define PIN_LED_FAULT 15

/* ------------------------------------------------------- board parameters */

/* What this board will ask a source for.  20 V is the SPR ceiling and the
 * firmware never requests EPR -- pd_policy.c enforces that, and vbus.c treats
 * anything above 21 V as a violated invariant rather than a reading. */
#define BOARD_PD_MAX_MV 20000
#define BOARD_PD_MIN_MV 5000
#define BOARD_PD_OPERATING_MA 1500
#define BOARD_BULK_UF 470
#define BOARD_PRECONTRACT_UF 10

#define BOARD_CAN_NOMINAL_BPS 500000u
#define BOARD_CAN_DATA_BPS 2000000u
#define BOARD_CAN_SAMPLE_POINT_PERMIL 875u
#define BOARD_CAN_DATA_SAMPLE_POINT_PERMIL 800u
#define BOARD_CAN_TRANSCEIVER_LOOP_NS 110u /* TCAN1042 */

#endif /* BOARD_H */
