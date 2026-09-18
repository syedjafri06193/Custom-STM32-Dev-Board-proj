/* SPI1 across the ADuM4154 barrier to the ADS1220.
 *
 * Two things constrain the clock rate, and the isolator is the tighter one.
 *
 *  - The ADS1220 itself is good to about 10 MHz.
 *  - The round trip through the isolator is not.  SCLK goes out through a
 *    forward channel, the ADS1220 answers, and DOUT comes back through a
 *    reverse channel.  Each crossing costs propagation delay, and the master
 *    samples MISO assuming it arrived promptly.  Add the two delays plus the
 *    part's own t_DOPD and the safe rate is a few megahertz, not ten.  The
 *    SPIsolator family exists because of exactly this, and section 4.3 is
 *    where the design document counts the channels.
 *
 * 170 MHz / 64 = 2.66 MHz, which is comfortable and still reads a 24-bit
 * conversion in about 12 us -- nothing here is throughput-bound at 20 SPS.
 */

#include "ads1220.h"
#include "board.h"
#include "gpio.h"
#include "hal.h"
#include "stm32g474.h"

#define SPI_BR_DIV64 5u /* BR[2:0] = 5 -> fPCLK/64 */

static int spi_xfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len);
static void spi_cs(void *ctx, bool asserted);

void iso_spi_init(void) {
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
    (void)RCC->APB2ENR;

    gpio_pin_speed(PIN_SPI_SCK_PORT, PIN_SPI_SCK, 2u);
    gpio_pin_speed(PIN_SPI_MOSI_PORT, PIN_SPI_MOSI, 2u);
    gpio_pin_af(PIN_SPI_SCK_PORT, PIN_SPI_SCK, PIN_SPI_AF);
    gpio_pin_af(PIN_SPI_MISO_PORT, PIN_SPI_MISO, PIN_SPI_AF);
    gpio_pin_af(PIN_SPI_MOSI_PORT, PIN_SPI_MOSI, PIN_SPI_AF);

    ISO_SPI->CR1 = 0;

    /* 8-bit frames, and FRXTH so RXNE fires on a byte rather than waiting for
     * a half-word that never comes.  Forgetting FRXTH is the classic way to
     * get an STM32 SPI that transmits fine and never receives. */
    ISO_SPI->CR2 = (7u << SPI_CR2_DS_Pos) | SPI_CR2_FRXTH;

    /* Mode 1 (CPOL = 0, CPHA = 1): the ADS1220 drives DOUT on the falling
     * edge of SCLK and expects DIN sampled on the rising edge. */
    ISO_SPI->CR1 = SPI_CR1_MSTR | SPI_CR1_CPHA | (SPI_BR_DIV64 << SPI_CR1_BR_Pos) |
                   SPI_CR1_SSM | SPI_CR1_SSI;
    ISO_SPI->CR1 |= SPI_CR1_SPE;
}

static int spi_xfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len) {
    (void)ctx;
    if (tx == NULL || len == 0u) {
        return -1;
    }

    volatile uint8_t *const dr = (volatile uint8_t *)&ISO_SPI->DR;

    for (size_t i = 0; i < len; i++) {
        while ((ISO_SPI->SR & SPI_SR_TXE) == 0u) {
        }
        *dr = tx[i];
        while ((ISO_SPI->SR & SPI_SR_RXNE) == 0u) {
        }
        const uint8_t got = *dr;
        if (rx != NULL) {
            rx[i] = got;
        }
    }

    while (ISO_SPI->SR & SPI_SR_BSY) {
    }
    return 0;
}

static void spi_cs(void *ctx, bool asserted) {
    (void)ctx;
    /* Active low, and with the setup/hold the ADS1220 asks for.  The part
     * latches its command on CS rising, so a CS that is merely "eventually"
     * high is a part that answers once and then goes quiet -- which is what
     * the host test chip_select_is_asserted_once_per_transaction pins down. */
    if (asserted) {
        gpio_pin_write(PIN_ADC_CS_PORT, PIN_ADC_CS, false);
        delay_us(1);
    } else {
        delay_us(1);
        gpio_pin_write(PIN_ADC_CS_PORT, PIN_ADC_CS, true);
        delay_us(1);
    }
}

void iso_spi_bind(ads1220_t *dev) {
    if (dev == NULL) {
        return;
    }
    dev->xfer = spi_xfer;
    dev->cs = spi_cs;
    dev->ctx = NULL;
}

bool iso_adc_data_ready(void) {
    /* /DRDY: the part pulls it low when a conversion is available. */
    return !gpio_pin_read(PIN_ADC_DRDY_PORT, PIN_ADC_DRDY);
}
