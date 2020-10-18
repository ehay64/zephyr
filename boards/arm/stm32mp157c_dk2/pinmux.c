/*
 * Copyright (c) 2019 STMicroelectronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <kernel.h>
#include <device.h>
#include <init.h>
#include <drivers/pinmux.h>
#include <sys/sys_io.h>

#include <pinmux/stm32/pinmux_stm32.h>

/* pin assignments for STM32MP157c_dk2 board */
static const struct pin_config pinconf[] = {
#if DT_NODE_HAS_STATUS(DT_NODELABEL(spi4), okay) && CONFIG_SPI
#ifdef CONFIG_SPI_STM32_USE_HW_SS
	{STM32_PIN_PE11, STM32MP1X_PINMUX_FUNC_PE11_SPI4_NSS},
#endif /* CONFIG_SPI_STM32_USE_HW_SS */
	{STM32_PIN_PE12, STM32MP1X_PINMUX_FUNC_PE12_SPI4_SCK},
	{STM32_PIN_PE13, STM32MP1X_PINMUX_FUNC_PE13_SPI4_MISO |
		STM32_OSPEEDR_VERY_HIGH_SPEED},
	{STM32_PIN_PE14, STM32MP1X_PINMUX_FUNC_PE14_SPI4_MOSI},
#endif
#if DT_NODE_HAS_STATUS(DT_NODELABEL(spi5), okay) && CONFIG_SPI
#ifdef CONFIG_SPI_STM32_USE_HW_SS
	{STM32_PIN_PF6, STM32MP1X_PINMUX_FUNC_PF6_SPI5_NSS},
#endif /* CONFIG_SPI_STM32_USE_HW_SS */
	{STM32_PIN_PF7, STM32MP1X_PINMUX_FUNC_PF7_SPI5_SCK},
	{STM32_PIN_PF8, STM32MP1X_PINMUX_FUNC_PF8_SPI5_MISO |
		STM32_OSPEEDR_VERY_HIGH_SPEED},
	{STM32_PIN_PF9, STM32MP1X_PINMUX_FUNC_PF9_SPI5_MOSI},
#endif
};

static int pinmux_stm32_init(const struct device *port)
{
	ARG_UNUSED(port);

	stm32_setup_pins(pinconf, ARRAY_SIZE(pinconf));

	return 0;
}

SYS_INIT(pinmux_stm32_init, PRE_KERNEL_1,
	 CONFIG_PINMUX_STM32_DEVICE_INITIALIZATION_PRIORITY);
