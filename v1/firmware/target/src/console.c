/* USART2 console.  Section 11.3 asks for a UART broken out for printf, and
 * the bring-up procedure is unusable without one -- every stage in section 15
 * ends with a number you need to read off the board. */

#include "board.h"
#include "gpio.h"
#include "hal.h"
#include "stm32g474.h"

void console_init(uint32_t baud) {
    RCC->APB1ENR1 |= RCC_APB1ENR1_USART2EN;
    (void)RCC->APB1ENR1;

    gpio_pin_speed(PIN_UART_TX_PORT, PIN_UART_TX, 2u);
    gpio_pin_af(PIN_UART_TX_PORT, PIN_UART_TX, PIN_UART_AF);
    gpio_pin_af(PIN_UART_RX_PORT, PIN_UART_RX, PIN_UART_AF);
    gpio_pin_pull(PIN_UART_RX_PORT, PIN_UART_RX, GPIO_PULL_UP);

    CONSOLE_UART->CR1 = 0; /* disable before reconfiguring */
    CONSOLE_UART->CR2 = 0;
    CONSOLE_UART->CR3 = 0;

    /* 16x oversampling: BRR is just the divider, rounded rather than
     * truncated.  At 170 MHz and 115200 the exact divider is 1475.69, and
     * truncating costs 0.47% where rounding costs 0.02%.  UART tolerates
     * both, but rounding is free. */
    CONSOLE_UART->BRR = (BOARD_PCLK1_HZ + (baud / 2u)) / baud;

    CONSOLE_UART->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

void console_putc(char c) {
    while ((CONSOLE_UART->ISR & USART_ISR_TXE) == 0u) {
    }
    CONSOLE_UART->TDR = (uint8_t)c;
}

void console_write(const char *s) {
    if (s == NULL) {
        return;
    }
    while (*s) {
        if (*s == '\n') {
            console_putc('\r'); /* terminals, not files */
        }
        console_putc(*s++);
    }
}

int console_getc_timeout(uint32_t ms) {
    const uint32_t start = millis();
    while ((millis() - start) < ms) {
        if (CONSOLE_UART->ISR & USART_ISR_RXNE) {
            return (int)(CONSOLE_UART->RDR & 0xFFu);
        }
    }
    return -1;
}

/* newlib's stdout, so printf lands on the console.  Only _write is provided;
 * nothing here reads from stdin, and pulling in the rest of the syscall stubs
 * invites somebody to call malloc by accident. */
int _write(int fd, const char *buf, int len);
int _write(int fd, const char *buf, int len) {
    (void)fd;
    for (int i = 0; i < len; i++) {
        if (buf[i] == '\n') {
            console_putc('\r');
        }
        console_putc(buf[i]);
    }
    return len;
}
