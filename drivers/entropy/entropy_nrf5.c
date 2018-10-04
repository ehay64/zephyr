/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 * Copyright (c) 2017 Exati Tecnologia Ltda.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <entropy.h>
#include <atomic.h>
#include <soc.h>
#include "nrf_rng.h"

/*
 * The nRF5 RNG HW has several characteristics that need to be taken
 * into account by the driver to achieve energy efficient generation
 * of entropy.
 *
 * The RNG does not support continuously DMA'ing entropy into RAM,
 * values must be read out by the CPU byte-by-byte. But once started,
 * it will continue to generate bytes until stopped.
 *
 * The generation time for byte 0 after starting generation (with BIAS
 * correction) is:
 *
 * nRF51822 - 677us
 * nRF52810 - 248us
 * nRF52840 - 248us
 *
 * The generation time for byte N >= 1 after starting generation (with
 * BIAS correction) is:
 *
 * nRF51822 - 677us
 * nRF52810 - 120us
 * nRF52840 - 120us
 *
 * Due to the first byte in a stream of bytes being more costly on
 * some platforms a "water system" inspired algorithm is used to
 * ammortize the cost of the first byte.
 *
 * The algorithm will delay generation of entropy until the amount of
 * bytes goes below THRESHOLD, at which point it will generate entropy
 * until the BUF_LEN limit is reached.
 *
 * The entropy level is checked at the end of every consumption of
 * entropy.
 *
 * The algorithm and HW together has these characteristics:
 *
 * Setting a low threshold will highly ammortize the extra 120us cost
 * of the first byte on nRF52.
 *
 * Setting a high threshold will minimize the time spent waiting for
 * entropy.
 *
 * To minimize power consumption the threshold should either be set
 * low or high depending on the HFCLK-usage pattern of other
 * components.
 *
 * If the threshold is set close to the BUF_LEN, and the system
 * happens to anyway be using the HFCLK for several hundred us after
 * entropy is requested there will be no extra current-consumption for
 * keeping clocks running for entropy generation.
 *
 */

struct rng_pool {
	u8_t count;
	u8_t threshold;
	u8_t first;
	u8_t last;
	u8_t rand[0];
};

#define RNG_POOL_DEFINE(name, len) u8_t name[sizeof(struct rng_pool) + len] __aligned(4)

#define RNG_POOL_ISR_LEN (CONFIG_ENTROPY_NRF5_ISR_BUF_LEN + 1)
#define RNG_POOL_THR_LEN (CONFIG_ENTROPY_NRF5_THR_BUF_LEN + 1)

struct entropy_nrf5_dev_data {
	struct k_sem sem_lock;
	struct k_sem sem_sync;

	RNG_POOL_DEFINE(isr, RNG_POOL_ISR_LEN);
	RNG_POOL_DEFINE(thr, RNG_POOL_THR_LEN);
};

#define DEV_DATA(dev) \
	((struct entropy_nrf5_dev_data *)(dev)->driver_data)

static int random_byte_get(void)
{
	int retval = -EAGAIN;
	unsigned int key;

	key = irq_lock();

	if (nrf_rng_event_get(NRF_RNG_EVENT_VALRDY)) {
		retval = nrf_rng_random_value_get();
		nrf_rng_event_clear(NRF_RNG_EVENT_VALRDY);
	}

	irq_unlock(key);

	return retval;
}

#pragma GCC push_options
#if defined(CONFIG_BT_CTLR_FAST_ENC)
#pragma GCC optimize ("Ofast")
#endif
static inline u8_t rng_pool_get(struct rng_pool *rng, u8_t octets, u8_t *rand)
{
	u8_t first, last, avail, remaining, *d, *s;

	__ASSERT_NO_MSG(rng);

	first = rng->first;
	last = rng->last;

	d = &rand[octets];
	s = &rng->rand[first];

	if (first <= last) {
		/* copy octets from contiguous memory */
		avail = last - first;
		if (octets < avail) {
			remaining = avail - octets;
			avail = octets;
		} else {
			remaining = 0;
		}

		first += avail;
		octets -= avail;

		while (avail--) {
			*(--d) = *s++;
		}

		rng->first = first;
	} else {
		/* copy octets from split halves - until end of array */
		avail = rng->count - first;
		if (octets < avail) {
			remaining = avail + last - octets;
			avail = octets;
			first += avail;
		} else {
			remaining = last;
			first = 0;
		}

		octets -= avail;

		while (avail--) {
			*(--d) = *s++;
		}

		/* copy from beginning of array - until ring buffer last idx */
		if (octets && last) {
			s = &rng->rand[0];

			if (octets < last) {
				remaining = last - octets;
				last = octets;
			} else {
				remaining = 0;
			}

			first = last;
			octets -= last;

			while (last--) {
				*(--d) = *s++;
			}
		}

		rng->first = first;
	}

	if (remaining < rng->threshold) {
		nrf_rng_task_trigger(NRF_RNG_TASK_START);
	}

	return octets;
}
#pragma GCC pop_options

static int rng_pool_put(struct rng_pool *rng, bool store, u8_t byte)
{
	u8_t last;

	if (!rng) {
		return -ENOBUFS;
	}

	last = rng->last + 1;
	if (last == rng->count) {
		last = 0;
	}

	if (last == rng->first) {
		/* this condition should not happen, but due to probable race,
		 * new value could be generated before NRF_RNG task is stopped.
		 */
		return -ENOBUFS;
	}

	if (!store) {
		return -EBUSY;
	}

	rng->rand[rng->last] = byte;
	rng->last = last;

	last = rng->last + 1;
	if (last == rng->count) {
		last = 0;
	}

	if (last == rng->first) {
		return 0;
	}

	return -EBUSY;
}

static void isr(void *arg)
{
	struct device *device = arg;
	struct entropy_nrf5_dev_data *dev_data = DEV_DATA(device);
	int byte, ret;

	byte = random_byte_get();
	if (byte < 0) {
		return;
	}

	ret = rng_pool_put((struct rng_pool *)dev_data->isr, true, byte);
	if (ret != -EBUSY) {
		ret = rng_pool_put((struct rng_pool *)dev_data->thr,
			           (ret == -ENOBUFS), byte);
		k_sem_give(&dev_data->sem_sync);
	}

	if (ret != -EBUSY) {
		nrf_rng_task_trigger(NRF_RNG_TASK_STOP);
	}
}

static void rng_pool_init(struct rng_pool *rng, u8_t len, u8_t threshold)
{
	rng->count = len;
	rng->threshold = threshold;
	rng->first = rng->last = 0;
}

static int entropy_nrf5_get_entropy(struct device *device, u8_t *buf, u16_t len)
{
	struct entropy_nrf5_dev_data *dev_data = DEV_DATA(device);

	while (len) {
		u8_t len8;

		if (len > UINT8_MAX) {
			len8 = UINT8_MAX;
		} else {
			len8 = len;
		}
		len -= len8;

		while (len8) {
			k_sem_take(&dev_data->sem_lock, K_FOREVER);
			len8 = rng_pool_get((struct rng_pool *)dev_data->thr,
					    len8, buf);
			k_sem_give(&dev_data->sem_lock);
			if (len8) {
				/* Sleep until next interrupt */
				k_sem_take(&dev_data->sem_sync, K_FOREVER);
			}
		}
	}

	return 0;
}

static int entropy_nrf5_get_entropy_isr(struct device *dev, u8_t *buf, u16_t len,
					u32_t flags)
{
	struct entropy_nrf5_dev_data *dev_data = DEV_DATA(dev);
	u16_t cnt = len;

	if (!(flags & ENTROPY_BUSYWAIT)) {
		return rng_pool_get((struct rng_pool *)dev_data->isr, len, buf);
	}

	if (len) {
		unsigned int key;
		int irq_enabled;

		key = irq_lock();
		irq_enabled = irq_is_enabled(RNG_IRQn);
		irq_disable(RNG_IRQn);
		irq_unlock(key);

		nrf_rng_event_clear(NRF_RNG_EVENT_VALRDY);
		nrf_rng_task_trigger(NRF_RNG_TASK_START);

		do {
			int byte;

			while (!nrf_rng_event_get(NRF_RNG_EVENT_VALRDY)) {
				__WFE();
				__SEV();
				__WFE();
			}

			byte = random_byte_get();
			NVIC_ClearPendingIRQ(RNG_IRQn);

			if (byte < 0) {
				continue;
			}

			buf[--len] = byte;
		} while (len);

		if (irq_enabled) {
			irq_enable(RNG_IRQn);
		}
	}

	return cnt;
}

static struct entropy_nrf5_dev_data entropy_nrf5_data;
static int entropy_nrf5_init(struct device *device);

static const struct entropy_driver_api entropy_nrf5_api_funcs = {
	.get_entropy = entropy_nrf5_get_entropy,
	.get_entropy_isr = entropy_nrf5_get_entropy_isr
};

DEVICE_AND_API_INIT(entropy_nrf5, CONFIG_ENTROPY_NAME,
		    entropy_nrf5_init, &entropy_nrf5_data, NULL,
		    PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEVICE,
		    &entropy_nrf5_api_funcs);

static int entropy_nrf5_init(struct device *device)
{
	struct entropy_nrf5_dev_data *dev_data = DEV_DATA(device);

	/* Locking semaphore initialized to 1 (unlocked) */
	k_sem_init(&dev_data->sem_lock, 1, 1);
	/* Synching semaphore */
	k_sem_init(&dev_data->sem_sync, 0, 1);

	rng_pool_init((struct rng_pool *)dev_data->thr, RNG_POOL_THR_LEN,
	              CONFIG_ENTROPY_NRF5_THR_THRESHOLD);
	rng_pool_init((struct rng_pool *)dev_data->isr, RNG_POOL_ISR_LEN,
	              CONFIG_ENTROPY_NRF5_ISR_THRESHOLD);

	/* Enable or disable bias correction */
	if (IS_ENABLED(CONFIG_ENTROPY_NRF5_BIAS_CORRECTION)) {
		nrf_rng_error_correction_enable();
	} else {
		nrf_rng_error_correction_disable();
	}

	nrf_rng_event_clear(NRF_RNG_EVENT_VALRDY);
	nrf_rng_int_enable(NRF_RNG_INT_VALRDY_MASK);
	nrf_rng_task_trigger(NRF_RNG_TASK_START);

	IRQ_CONNECT(RNG_IRQn, CONFIG_ENTROPY_NRF5_PRI, isr,
		    DEVICE_GET(entropy_nrf5), 0);
	irq_enable(RNG_IRQn);

	return 0;
}

u8_t entropy_nrf_get_entropy_isr(struct device *dev, u8_t *buf, u8_t len)
{
	ARG_UNUSED(dev);
	return rng_pool_get((struct rng_pool *)entropy_nrf5_data.isr, len, buf);
}

