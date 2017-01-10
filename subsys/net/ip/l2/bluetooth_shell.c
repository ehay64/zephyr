/*
 * Copyright (c) 2017 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <kernel.h>
#include <toolchain.h>
#include <sections.h>
#include <string.h>
#include <errno.h>

#include <shell/shell.h>
#include <misc/printk.h>

#include <net/net_core.h>
#include <net/net_l2.h>
#include <net/net_if.h>
#include <net/bt.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>

#define BT_SHELL_MODULE "net_bt"

static int char2hex(const char *c, uint8_t *x)
{
	if (*c >= '0' && *c <= '9') {
		*x = *c - '0';
	} else if (*c >= 'a' && *c <= 'f') {
		*x = *c - 'a' + 10;
	} else if (*c >= 'A' && *c <= 'F') {
		*x = *c - 'A' + 10;
	} else {
		return -EINVAL;
	}

	return 0;
}

static int str2bt_addr_le(const char *str, const char *type, bt_addr_le_t *addr)
{
	int i, j;
	uint8_t tmp;

	if (strlen(str) != 17) {
		return -EINVAL;
	}

	for (i = 5, j = 1; *str != '\0'; str++, j++) {
		if (!(j % 3) && (*str != ':')) {
			return -EINVAL;
		} else if (*str == ':') {
			i--;
			continue;
		}

		addr->a.val[i] = addr->a.val[i] << 4;

		if (char2hex(str, &tmp) < 0) {
			return -EINVAL;
		}

		addr->a.val[i] |= tmp;
	}

	if (!strcmp(type, "public") || !strcmp(type, "(public)")) {
		addr->type = BT_ADDR_LE_PUBLIC;
	} else if (!strcmp(type, "random") || !strcmp(type, "(random)")) {
		addr->type = BT_ADDR_LE_RANDOM;
	} else {
		return -EINVAL;
	}

	return 0;
}

static int shell_cmd_connect(int argc, char *argv[])
{
	int err;
	bt_addr_le_t addr;
	struct net_if *iface = net_if_get_default();

	if (argc < 3) {
		return -EINVAL;
	}

	err = str2bt_addr_le(argv[1], argv[2], &addr);
	if (err) {
		printk("Invalid peer address (err %d)\n", err);
		return 0;
	}

	if (net_mgmt(NET_REQUEST_BT_CONNECT, iface, &addr, sizeof(addr))) {
		printk("Connection failed\n");
	} else {
		printk("Connection pending\n");
	}

	return 0;
}

static int shell_cmd_scan(int argc, char *argv[])
{
	struct net_if *iface = net_if_get_default();

	if (argc < 2) {
		return -EINVAL;
	}

	if (net_mgmt(NET_REQUEST_BT_SCAN, iface, argv[1], strlen(argv[1]))) {
		printk("Scan failed\n");
	} else {
		printk("Scan in progress\n");
	}

	return 0;
}

static struct shell_cmd bt_commands[] = {
	{ "connect", shell_cmd_connect,
		"<address: XX:XX:XX:XX:XX:XX> <type: (public|random)>" },
	{ "scan", shell_cmd_scan, "<on/off/active/passive>" },
	{ NULL, NULL, NULL },
};

SHELL_REGISTER(BT_SHELL_MODULE, bt_commands);
