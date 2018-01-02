/*
 * Copyright (c) 2016-2018, The Linux Foundation. All rights reserved.
 * Copyright (c) 2016, Linaro Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <err.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "hdlc.h"
#include "util.h"

#define DIAG_CMD_RSP_BAD_COMMAND			0x13
#define DIAG_CMD_RSP_BAD_PARAMS				0x14
#define DIAG_CMD_RSP_BAD_LENGTH				0x15

static int hdlc_enqueue(struct list_head *queue, uint8_t *msg, size_t msglen)
{
	uint8_t *outbuf;
	size_t outlen;

	outbuf = hdlc_encode(msg, msglen, &outlen);
	if (!outbuf)
		err(1, "failed to allocate hdlc destination buffer");

	queue_push(queue, outbuf, outlen);

	return 0;
}

static int diag_cmd_dispatch(struct diag_client *client, uint8_t *ptr,
			     size_t len)
{
	struct peripheral *peripheral;
	struct list_head *item;
	struct diag_cmd *dc;
	unsigned int key;
	int handled = 0;

	if (ptr[0] == DIAG_CMD_SUBSYS_DISPATCH)
		key = ptr[0] << 24 | ptr[1] << 16 | ptr[3] << 8 | ptr[2];
	else
		key = 0xff << 24 | 0xff << 16 | ptr[0];

	if (key == 0x4b320003) {
		return hdlc_enqueue(&client->outq, ptr, len);
	}

	list_for_each(item, &diag_cmds) {
		dc = container_of(item, struct diag_cmd, node);
		if (key < dc->first || key > dc->last)
			continue;

		peripheral = dc->peripheral;

		if (peripheral->features & DIAG_FEATURE_APPS_HDLC_ENCODE)
			queue_push(&peripheral->dataq, ptr, len);
		else
			hdlc_enqueue(&dc->peripheral->dataq, ptr, len);

		handled++;
	}

	return handled ? 0 : -ENOENT;
}

static void diag_rsp_bad_command(struct diag_client *client, uint8_t *msg,
				 size_t len, int error_code)
{
	uint8_t *buf;

	buf = malloc(len + 1);
	if (!buf)
		err(1, "failed to allocate error buffer");

	buf[0] = error_code;
	memcpy(buf + 1, msg, len);

	hdlc_enqueue(&client->outq, buf, len + 1);

	free(buf);
}

int diag_client_handle_command(struct diag_client *client, uint8_t *data, size_t len)
{
	int ret;

	ret = diag_cmd_dispatch(client, data, len);

	switch (ret) {
	case -ENOENT:
		diag_rsp_bad_command(client, data, len, DIAG_CMD_RSP_BAD_COMMAND);
		break;
	case -EINVAL:
		diag_rsp_bad_command(client, data, len, DIAG_CMD_RSP_BAD_PARAMS);
		break;
	case -EMSGSIZE:
		diag_rsp_bad_command(client, data, len, DIAG_CMD_RSP_BAD_LENGTH);
		break;
	default:
		break;
	}

	return 0;
}
