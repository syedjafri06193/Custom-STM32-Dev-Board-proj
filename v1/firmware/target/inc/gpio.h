/* Pin-level primitives, shared by the drivers. */

#ifndef TARGET_GPIO_H
#define TARGET_GPIO_H

#include <stdbool.h>
#include <stdint.h>

#include "stm32g474.h"

#define GPIO_MODE_INPUT 0u
#define GPIO_MODE_OUTPUT 1u
#define GPIO_MODE_AF 2u
#define GPIO_MODE_ANALOG 3u

#define GPIO_PULL_NONE 0u
#define GPIO_PULL_UP 1u
#define GPIO_PULL_DOWN 2u

void gpio_pin_mode(GPIO_TypeDef *port, uint32_t pin, uint32_t mode);
void gpio_pin_af(GPIO_TypeDef *port, uint32_t pin, uint32_t af);
void gpio_pin_pull(GPIO_TypeDef *port, uint32_t pin, uint32_t pull);
void gpio_pin_speed(GPIO_TypeDef *port, uint32_t pin, uint32_t speed);
void gpio_pin_write(GPIO_TypeDef *port, uint32_t pin, bool high);
bool gpio_pin_read(GPIO_TypeDef *port, uint32_t pin);

#endif /* TARGET_GPIO_H */
