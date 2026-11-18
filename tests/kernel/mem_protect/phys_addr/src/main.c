/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel/internal/mm.h>
#include <zephyr/ztest.h>

#define LOCAL_NODE DT_CHOSEN(zephyr_sram)

#define LOCAL_BASE ((uintptr_t)DT_REG_ADDR(LOCAL_NODE))
#define LOCAL_SIZE ((uintptr_t)DT_REG_SIZE(LOCAL_NODE))

/* Without an alias, both conversions must return the address unchanged */
#define ALIAS_BASE                                                                                 \
	COND_CODE_1(DT_REG_HAS_NAME(LOCAL_NODE, sram0_alias),                                      \
		    (((uintptr_t)DT_REG_ADDR_BY_NAME(LOCAL_NODE, sram0_alias))), (LOCAL_BASE))

/**
 * @brief Verify that k_mem_phys_addr() returns the alias of a local address.
 *
 * Test steps:
 * - Convert the first and last byte of the RAM region and a static buffer.
 *
 * Expected result:
 * - Each address is moved by the offset between the alias and the region,
 *   or returned unchanged if the region has no alias.
 *
 * @see k_mem_phys_addr()
 */
ZTEST(phys_addr, test_k_mem_phys_addr_alias)
{
	static uint32_t buf[4];
	uintptr_t local = (uintptr_t)buf;

	zassert_equal(k_mem_phys_addr((void *)LOCAL_BASE), ALIAS_BASE);
	zassert_equal(k_mem_phys_addr((void *)(LOCAL_BASE + LOCAL_SIZE - 1U)),
		      ALIAS_BASE + LOCAL_SIZE - 1U);
	zassert_equal(k_mem_phys_addr(buf), local - LOCAL_BASE + ALIAS_BASE);
}

/**
 * @brief Verify that k_mem_virt_addr() returns the local address of an alias.
 *
 * Test steps:
 * - Convert the first and last byte of the alias region.
 *
 * Expected result:
 * - Each address is moved back to the matching byte of the RAM region. Without
 *   an alias, the alias addresses are the RAM addresses themselves and are
 *   returned unchanged.
 *
 * @see k_mem_virt_addr()
 */
ZTEST(phys_addr, test_k_mem_virt_addr_alias)
{
	zassert_equal_ptr(k_mem_virt_addr(ALIAS_BASE), (void *)LOCAL_BASE);
	zassert_equal_ptr(k_mem_virt_addr(ALIAS_BASE + LOCAL_SIZE - 1U),
			  (void *)(LOCAL_BASE + LOCAL_SIZE - 1U));
}

/**
 * @brief Verify that converting to the alias and back is lossless.
 *
 * Test steps:
 * - Convert a static buffer and a stack variable with k_mem_phys_addr(),
 *   then convert the result with k_mem_virt_addr().
 *
 * Expected result:
 * - The original pointers are returned.
 *
 * @see k_mem_phys_addr(), k_mem_virt_addr()
 */
ZTEST(phys_addr, test_k_mem_phys_virt_round_trip)
{
	static uint8_t buf[16];
	uint32_t stack_var;

	zassert_equal_ptr(k_mem_virt_addr(k_mem_phys_addr(buf)), buf);
	zassert_equal_ptr(k_mem_virt_addr(k_mem_phys_addr(&stack_var)), &stack_var);
}

ZTEST_SUITE(phys_addr, NULL, NULL, NULL, NULL, NULL);
