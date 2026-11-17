/*
 * Copyright (c) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_KERNEL_INTERNAL_MM_H
#define ZEPHYR_INCLUDE_KERNEL_INTERNAL_MM_H

#include <zephyr/sys/util.h>
#include <zephyr/toolchain.h>
#include <zephyr/devicetree.h>

/**
 * @defgroup kernel_mm_internal_apis Kernel Memory Management Internal APIs
 * @ingroup internal_api
 * @{
 */

/**
 * @def K_MEM_VIRT_OFFSET
 * @brief Address offset of permanent virtual mapping from physical address.
 *
 * This is the offset to subtract from a virtual address mapped in the
 * kernel's permanent mapping of RAM, to obtain its physical address.
 *
 *     virt_addr = phys_addr + K_MEM_VIRT_OFFSET
 *
 * This only works for virtual addresses within the interval
 * [CONFIG_KERNEL_VM_BASE, CONFIG_KERNEL_VM_BASE + DT_CHOSEN_SRAM_ADDR).
 *
 * These macros are intended for assembly, linker code, and static initializers.
 * Use with care.
 *
 * Note that when demand paging is active, these will only work with page
 * frames that are pinned to their virtual mapping at boot.
 *
 * TODO: This will likely need to move to an arch API or need additional
 * constraints defined.
 */
#ifdef CONFIG_MMU
#define K_MEM_VIRT_OFFSET	((CONFIG_KERNEL_VM_BASE + CONFIG_KERNEL_VM_OFFSET) - \
				 (DT_CHOSEN_SRAM_ADDR + CONFIG_SRAM_OFFSET))
#else
#define K_MEM_VIRT_OFFSET	0
#endif /* CONFIG_MMU */

#if DT_CHOSEN_SRAM_ADDR != 0
#define IS_SRAM_ADDRESS_LOWER(ADDR)  ((ADDR) >= DT_CHOSEN_SRAM_ADDR)
#else
#define IS_SRAM_ADDRESS_LOWER(ADDR)  true
#endif /* DT_CHOSEN_SRAM_ADDR != 0 */

#if (DT_CHOSEN_SRAM_ADDR + DT_CHOSEN_SRAM_SIZE) != 0
#define IS_SRAM_ADDRESS_UPPER(ADDR)              \
	((ADDR) < (DT_CHOSEN_SRAM_ADDR + DT_CHOSEN_SRAM_SIZE))
#else
#define IS_SRAM_ADDRESS_UPPER(ADDR)  false
#endif

#define IS_SRAM_ADDRESS(ADDR)            \
	(IS_SRAM_ADDRESS_LOWER(ADDR) &&  \
	 IS_SRAM_ADDRESS_UPPER(ADDR))

/**
 * @brief Get physical address from virtual address.
 *
 * This only works in the kernel's permanent mapping of RAM.
 *
 * @param virt Virtual address
 *
 * @return Physical address.
 */
#define K_MEM_PHYS_ADDR(virt)	((virt) - K_MEM_VIRT_OFFSET)

/**
 * @brief Get virtual address from physical address.
 *
 * This only works in the kernel's permanent mapping of RAM.
 *
 * @param phys Physical address
 *
 * @return Virtual address.
 */
#define K_MEM_VIRT_ADDR(phys)	((phys) + K_MEM_VIRT_OFFSET)

#if K_MEM_VIRT_OFFSET != 0
/**
 * @brief Kernel is mapped in virtual memory if defined.
 */
#define K_MEM_IS_VM_KERNEL 1
#ifdef CONFIG_XIP
#error "XIP and a virtual memory kernel are not allowed"
#endif
#endif

#ifndef _ASMLANGUAGE
#include <stdint.h>
#include <stddef.h>
#include <inttypes.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/sys/mem_manage.h>

#ifndef CONFIG_MMU
/** @cond INTERNAL_HIDDEN */

/*
 * A memory node has an alias if its reg property has a second entry named
 * after the first one with an "_alias" suffix, for example:
 *
 *     reg = <0x20000000 0x100000>, <0x50800000 0x100000>;
 *     reg-names = "dtcm", "dtcm_alias";
 *
 * The first entry is the address used by the CPU, the alias entry the address
 * used by other bus masters. Both entries must have the same size.
 */
#define Z_MEM_ALIAS_NAME(node_id) UTIL_CAT(DT_STRING_TOKEN_BY_IDX(node_id, reg_names, 0), _alias)

#define Z_MEM_HAS_ALIAS(node_id)                                                                   \
	COND_CODE_1(DT_NODE_HAS_PROP(node_id, reg_names),                                          \
		    (DT_REG_HAS_NAME(node_id, Z_MEM_ALIAS_NAME(node_id))), (0))

/* Local base, alias base and size of memory node node_id */
#define Z_MEM_LOCAL_BASE(node_id) ((uintptr_t)DT_REG_ADDR(node_id))
#define Z_MEM_ALIAS_BASE(node_id)                                                                  \
	((uintptr_t)DT_REG_ADDR_BY_NAME(node_id, Z_MEM_ALIAS_NAME(node_id)))
#define Z_MEM_SIZE(node_id)       ((uintptr_t)DT_REG_SIZE(node_id))

#define Z_MEM_ALIAS_SIZE_CHECK(node_id)                                                            \
	IF_ENABLED(Z_MEM_HAS_ALIAS(node_id),                                                       \
		   (BUILD_ASSERT(DT_REG_SIZE_BY_NAME(node_id, Z_MEM_ALIAS_NAME(node_id)) ==        \
					 DT_REG_SIZE(node_id),                                     \
				 "alias of " DT_NODE_PATH(node_id) " differs in size");))

DT_FOREACH_STATUS_OKAY_NODE(Z_MEM_ALIAS_SIZE_CHECK)

/* Ternary term moving addr from base "from" to base "to" if it lies within the region */
#define Z_MEM_REMAP_TERM(addr, from, to, size)                                                     \
	(((addr) - (from)) < (size)) ? (((addr) - (from)) + (to)) :

#define Z_MEM_LOCAL_TO_ALIAS(node_id, addr)                                                        \
	IF_ENABLED(Z_MEM_HAS_ALIAS(node_id),                                                       \
		   (Z_MEM_REMAP_TERM(addr, Z_MEM_LOCAL_BASE(node_id), Z_MEM_ALIAS_BASE(node_id),   \
				     Z_MEM_SIZE(node_id))))

#define Z_MEM_ALIAS_TO_LOCAL(node_id, addr)                                                        \
	IF_ENABLED(Z_MEM_HAS_ALIAS(node_id),                                                       \
		   (Z_MEM_REMAP_TERM(addr, Z_MEM_ALIAS_BASE(node_id), Z_MEM_LOCAL_BASE(node_id),   \
				     Z_MEM_SIZE(node_id))))

/* Each node with an alias contributes one "cond ? remapped :" term */
static inline uintptr_t z_mem_local_to_alias(uintptr_t addr)
{
	return DT_FOREACH_STATUS_OKAY_NODE_VARGS(Z_MEM_LOCAL_TO_ALIAS, addr) addr;
}

static inline uintptr_t z_mem_alias_to_local(uintptr_t addr)
{
	return DT_FOREACH_STATUS_OKAY_NODE_VARGS(Z_MEM_ALIAS_TO_LOCAL, addr) addr;
}

/** @endcond */
#endif /* !CONFIG_MMU */

/**
 * @brief Get physical address from virtual address.
 *
 * This only works in the kernel's permanent mapping of RAM.
 *
 * Just like K_MEM_PHYS_ADDR() but with type safety and assertions.
 *
 * Without an MMU, an address in a memory node whose @c reg property has an
 * alias entry (named after the first entry with an @c _alias suffix) is
 * converted to the matching address in the alias, which is how other bus
 * masters reach that memory. If the address does not fall within any aliased
 * region, it is returned unchanged.
 *
 * @param virt Virtual address
 *
 * @return Physical address.
 */
static inline uintptr_t k_mem_phys_addr(void *virt)
{
	uintptr_t addr = (uintptr_t)virt;

#if defined(CONFIG_KERNEL_VM_USE_CUSTOM_MEM_RANGE_CHECK)
	__ASSERT(sys_mm_is_virt_addr_in_range(virt),
		 "address %p not in permanent mappings", virt);
#elif defined(CONFIG_MMU)
#if CONFIG_KERNEL_VM_BASE != 0
	/* Skipped when the VM base is 0, where the comparison is always true and
	 * some compilers warn about it (-Wtype-limits).
	 */
	__ASSERT(addr >= (uintptr_t)CONFIG_KERNEL_VM_BASE, "address %p below VM base", virt);
#endif /* CONFIG_KERNEL_VM_BASE != 0 */
	__ASSERT((addr - (uintptr_t)CONFIG_KERNEL_VM_BASE) < (uintptr_t)CONFIG_KERNEL_VM_SIZE,
		 "address %p above VM limit", virt);
#else
	/* Should be identity-mapped */
	__ASSERT(IS_SRAM_ADDRESS(addr),
		 "physical address 0x%lx not in RAM",
		 (unsigned long)addr);
#endif /* CONFIG_MMU */

	/* TODO add assertion that this page is pinned to boot mapping,
	 * the above checks won't be sufficient with demand paging
	 */

#ifdef CONFIG_MMU
	return K_MEM_PHYS_ADDR(addr);
#else
	return z_mem_local_to_alias(addr);
#endif /* CONFIG_MMU */
}

/**
 * @brief Get virtual address from physical address.
 *
 * This only works in the kernel's permanent mapping of RAM.
 *
 * Just like K_MEM_VIRT_ADDR() but with type safety and assertions.
 *
 * Without an MMU, an address in the alias entry of a memory node's @c reg
 * property is converted back to the matching address in the first entry. If
 * the address does not fall within an aliased region, it is returned
 * unchanged.
 *
 * @param phys Physical address
 *
 * @return Virtual address.
 */
static inline void *k_mem_virt_addr(uintptr_t phys)
{
#ifdef CONFIG_MMU
	uintptr_t addr = phys;
#else
	uintptr_t addr = z_mem_alias_to_local(phys);
#endif /* CONFIG_MMU */

#if defined(CONFIG_KERNEL_VM_USE_CUSTOM_MEM_RANGE_CHECK)
	__ASSERT(sys_mm_is_phys_addr_in_range(addr),
		"physical address 0x%lx not in RAM", (unsigned long)addr);
#else
	__ASSERT(IS_SRAM_ADDRESS(addr),
		 "physical address 0x%lx not in RAM", (unsigned long)addr);
#endif /* CONFIG_KERNEL_VM_USE_CUSTOM_MEM_RANGE_CHECK */

	/* TODO add assertion that this page frame is pinned to boot mapping,
	 * the above check won't be sufficient with demand paging
	 */

	return (void *)K_MEM_VIRT_ADDR(addr);
}

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Map a physical memory region into the kernel's virtual address space
 *
 * This function is intended for mapping memory-mapped I/O regions into
 * the virtual address space. Given a physical address and a size, return a
 * linear address representing the base of where the physical region is mapped
 * in the virtual address space for the Zephyr kernel.
 *
 * The memory mapped via this function must be unmapped using
 * k_mem_unmap_phys_bare().
 *
 * This function alters the active page tables in the area reserved
 * for the kernel. This function will choose the virtual address
 * and return it to the caller.
 *
 * Portable code should never assume that phys_addr and linear_addr will
 * be equal.
 *
 * Caching and access properties are controlled by the 'flags' parameter.
 * Unused bits in 'flags' are reserved for future expansion.
 * A caching mode must be selected. By default, the region is read-only
 * with user access and code execution forbidden. This policy is changed
 * by passing K_MEM_CACHE_* and K_MEM_PERM_* macros into the 'flags' parameter.
 *
 * If there is insufficient virtual address space for the mapping this will
 * generate a kernel panic.
 *
 * This API is only available if CONFIG_MMU is enabled.
 *
 * It is highly discouraged to use this function to map system RAM page
 * frames. It may conflict with anonymous memory mappings and demand paging
 * and produce undefined behavior.  Do not use this for RAM unless you know
 * exactly what you are doing. If you need a chunk of memory, use k_mem_map().
 * If you need a contiguous buffer of physical memory, statically declare it
 * and pin it at build time, it will be mapped when the system boots.
 *
 * This API is part of infrastructure still under development and may
 * change.
 *
 * @param[out] virt_ptr Output virtual address storage location
 * @param[in]  phys Physical address base of the memory region
 * @param[in]  size Size of the memory region
 * @param[in]  flags Caching mode and access flags, see K_MAP_* macros
 */
void k_mem_map_phys_bare(uint8_t **virt_ptr, uintptr_t phys, size_t size,
			 uint32_t flags);

/**
 * Unmap a virtual memory region from kernel's virtual address space.
 *
 * This function is intended to be used by drivers and early boot routines
 * where temporary memory mappings need to be made. This allows these
 * memory mappings to be discarded once they are no longer needed.
 *
 * This function alters the active page tables in the area reserved
 * for the kernel.
 *
 * This will align the input parameters to page boundaries so that
 * this can be used with the virtual address as returned by
 * k_mem_map_phys_bare().
 *
 * This API is only available if CONFIG_MMU is enabled.
 *
 * It is highly discouraged to use this function to unmap memory mappings.
 * It may conflict with anonymous memory mappings and demand paging and
 * produce undefined behavior. Do not use this unless you know exactly
 * what you are doing.
 *
 * This API is part of infrastructure still under development and may
 * change.
 *
 * @param virt Starting address of the virtual address region to be unmapped.
 * @param size Size of the virtual address region
 */
void k_mem_unmap_phys_bare(uint8_t *virt, size_t size);

/**
 * Map memory into virtual address space with guard pages.
 *
 * This maps memory into virtual address space with a preceding and
 * a succeeding guard pages. The memory mapped via this function must be
 * unmapped using k_mem_unmap_phys_guard().
 *
 * This function maps a contiguous physical memory region into kernel's
 * virtual address space with a preceding and a succeeding guard pages.
 * Given a physical address and a size, return a linear address representing
 * the base of where the physical region is mapped in the virtual address
 * space for the Zephyr kernel.
 *
 * This function alters the active page tables in the area reserved
 * for the kernel. This function will choose the virtual address
 * and return it to the caller.
 *
 * If user thread access control needs to be managed in any way, do not enable
 * K_MEM_PERM_USER flags here; instead manage the region's permissions
 * with memory domain APIs after the mapping has been established. Setting
 * K_MEM_PERM_USER here will allow all user threads to access this memory
 * which is usually undesirable.
 *
 * Unless K_MEM_MAP_UNINIT is used, the returned memory will be zeroed.
 *
 * The returned virtual memory pointer will be page-aligned. The size
 * parameter, and any base address for re-mapping purposes must be page-
 * aligned.
 *
 * Note that the allocation includes two guard pages immediately before
 * and after the requested region. The total size of the allocation will be
 * the requested size plus the size of these two guard pages.
 *
 * Many K_MEM_MAP_* flags have been implemented to alter the behavior of this
 * function, with details in the documentation for these flags.
 *
 * @see k_mem_map() for additional information if called via that.
 *
 * @param phys Physical address base of the memory region if not requesting
 *             anonymous memory. Must be page-aligned.
 * @param size Size of the memory mapping. This must be page-aligned.
 * @param flags K_MEM_PERM_*, K_MEM_MAP_* control flags.
 * @param is_anon True if requesting mapping with anonymous memory.
 *
 * @return The mapped memory location, or NULL if insufficient virtual address
 *         space, insufficient physical memory to establish the mapping,
 *         or insufficient memory for paging structures.
 */
void *k_mem_map_phys_guard(uintptr_t phys, size_t size, uint32_t flags, bool is_anon);

/**
 * Un-map memory mapped via k_mem_map_phys_guard().
 *
 * This removes the memory mappings for the provided page-aligned region,
 * and the two guard pages surrounding the region.
 *
 * This function alters the active page tables in the area reserved
 * for the kernel.
 *
 * @see k_mem_unmap() for additional information if called via that.
 *
 * @note Calling this function on a region which was not mapped via
 *       k_mem_map_phys_guard() to begin with is undefined behavior.
 *
 * @param addr Page-aligned memory region base virtual address
 * @param size Page-aligned memory region size
 * @param is_anon True if the mapped memory is from anonymous memory.
 */
void k_mem_unmap_phys_guard(void *addr, size_t size, bool is_anon);

#ifdef __cplusplus
}
#endif

/** @} */

#endif /* !_ASMLANGUAGE */
#endif /* ZEPHYR_INCLUDE_KERNEL_INTERNAL_MM_H */
