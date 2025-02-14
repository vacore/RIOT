/*
 * Copyright (C) 2020 Nalys
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     tests
 * @{
 *
 * @file
 * @brief       Test application for the candev abstraction
 *
 * @author      Wouter Symons <wosym@airsantelmo.com>
 * @author      Toon Stegen <tstegen@nalys-group.com>
 *
 * @}
 */

#include <assert.h>
#include <errno.h>
#include <isrpipe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "shell.h"
#include "test_utils/expect.h"
#include "can/device.h"

#if IS_USED(MODULE_PERIPH_CAN)

#include "periph/can.h"
#include "can_params.h"

static can_t periph_dev;

#elif defined(MODULE_MCP2515)
#include "candev_mcp2515.h"
#include "mcp2515_params.h"

static candev_mcp2515_t mcp2515_dev;

#else
/* add includes for other candev drivers here */
#endif

#define ENABLE_DEBUG 0
#include <debug.h>

/* Default is not using loopback test mode */
#ifndef CONFIG_USE_LOOPBACK_MODE
#define CONFIG_USE_LOOPBACK_MODE        0
#endif

#define RX_RINGBUFFER_SIZE 128      /* Needs to be a power of 2! */
static isrpipe_t rxbuf;
static uint8_t rx_ringbuf[RX_RINGBUFFER_SIZE];

static candev_t *candev = NULL;

static int _send(int argc, char **argv)
{
    int ret = 0;

    struct can_frame frame = {
        .can_id = 1,
        .can_dlc = 3,
        .data[0] = 0xAB,
        .data[1] = 0xCD,
        .data[2] = 0xEF,
    };

    if (argc > 1) {
        if (argc > 1 + CAN_MAX_DLEN) {
            printf("Could not send. Maximum CAN-bytes: %d\n", CAN_MAX_DLEN);
            return -1;
        }
        for (int i = 1; i < argc; i++) {
            frame.data[i - 1] = atoi(argv[i]);
        }
        frame.can_dlc = argc - 1;
    }

    ret = candev->driver->send(candev, &frame);
    if (ret < 0) {
        puts("Failed to send CAN-message!");
    }
    else {
        DEBUG("sent using mailbox: %d\n", ret);
    }

    return 0;
}

static int _receive(int argc, char **argv)
{
    int n = 1;

    if (argc > 1) {
        n = strtol(argv[1], NULL, 10);
        if (n < 1) {
            puts("Usage: receive <number>");
            return -1;
        }
    }

    for (int i = 0; i < n; i++) {
        struct can_frame frame;

        puts("Reading from Rxbuf...");
        isrpipe_read(&rxbuf, (uint8_t *)&(frame.can_id), sizeof(frame.can_id));
        frame.can_id &= 0x1FFFFFFF; /* clear invalid bits */
        isrpipe_read(&rxbuf, (uint8_t *)&(frame.can_dlc), 1);
        printf("id: %" PRIx32 " dlc: %" PRIx8, frame.can_id, frame.can_dlc);
        if (frame.can_dlc > 0) {
            printf(" data: ");
            isrpipe_read(&rxbuf, frame.data, frame.can_dlc); /* data */
            for (int i = 0; i < frame.can_dlc; i++) {
                printf("0x%X ", frame.data[i]);
            }
        }
        putchar('\n');
    }

    return 0;
}

static const shell_command_t shell_commands[] = {
    { "send", "send some data", _send },
    { "receive", "receive some data", _receive },
    { NULL, NULL, NULL }
};

static void _can_event_callback(candev_t *dev, candev_event_t event, void *arg)
{
    (void)arg;
    struct can_frame *frame;

    switch (event) {
    case CANDEV_EVENT_ISR:
        DEBUG("_can_event: CANDEV_EVENT_ISR\n");
        dev->driver->isr(candev);
        break;
    case CANDEV_EVENT_WAKE_UP:
        DEBUG("_can_event: CANDEV_EVENT_WAKE_UP\n");
        break;
    case CANDEV_EVENT_TX_CONFIRMATION:
        DEBUG("_can_event: CANDEV_EVENT_TX_CONFIRMATION\n");
        break;
    case CANDEV_EVENT_TX_ERROR:
        DEBUG("_can_event: CANDEV_EVENT_TX_ERROR\n");
        break;
    case CANDEV_EVENT_RX_INDICATION:
        DEBUG("_can_event: CANDEV_EVENT_RX_INDICATION\n");

        frame = (struct can_frame *)arg;

        DEBUG("            id: %" PRIx32 " dlc: %" PRIx8 " data: ", frame->can_id & 0x1FFFFFFF,
              frame->can_dlc);
        for (uint8_t i = 0; i < frame->can_dlc; i++) {
            DEBUG("0x%X ", frame->data[i]);
        }
        DEBUG_PUTS("");

        /* Store in buffer until user requests the data */
        isrpipe_write(&rxbuf, (uint8_t *)&(frame->can_id), sizeof(frame->can_id));
        isrpipe_write_one(&rxbuf, frame->can_dlc);
        isrpipe_write(&rxbuf, frame->data, frame->can_dlc);

        break;
    case CANDEV_EVENT_RX_ERROR:
        DEBUG("_can_event: CANDEV_EVENT_RX_ERROR\n");
        break;
    case CANDEV_EVENT_BUS_OFF:
        dev->state = CAN_STATE_BUS_OFF;
        break;
    case CANDEV_EVENT_ERROR_PASSIVE:
        dev->state = CAN_STATE_ERROR_PASSIVE;
        break;
    case CANDEV_EVENT_ERROR_WARNING:
        dev->state = CAN_STATE_ERROR_WARNING;
        break;
    default:
        DEBUG("_can_event: unknown event\n");
        break;
    }
}

int main(void)
{

    puts("candev test application\n");

    isrpipe_init(&rxbuf, (uint8_t *)rx_ringbuf, sizeof(rx_ringbuf));
#if IS_USED(MODULE_PERIPH_CAN)
    puts("Initializing CAN periph device");
    can_init(&periph_dev, &(candev_conf[0]));    /* vcan0 on native */
    candev = (candev_t *)&periph_dev;
#elif  defined(MODULE_MCP2515)
    puts("Initializing MCP2515");
    candev_mcp2515_init(&mcp2515_dev, &candev_mcp2515_conf[0]);
    candev = (candev_t *)&mcp2515_dev;

#else
    /* add initialization for other candev drivers here */
#endif

    expect(candev);

    candev->event_callback = _can_event_callback;
    candev->isr_arg = NULL;

    candev->driver->init(candev);

if (IS_ACTIVE(CONFIG_USE_LOOPBACK_MODE)) {
    puts("Switching to loopback mode");
    /* set to loopback test mode */
    canopt_state_t mode = CANOPT_STATE_LOOPBACK;
    candev->driver->set(candev, CANOPT_STATE, &mode, sizeof(mode));
}

    if (IS_ACTIVE(MCP2515_RECV_FILTER_EN)) {
        /* CAN filters examples */
        struct can_filter filter[3];
        filter[0].can_mask = 0x7FF;
        filter[0].can_id = 0x001;   /* messages with CAN ID 0x001 will be received in mailbox 0 */
        filter[1].can_mask = 0x7FF;
        filter[1].can_id = 0x003;   /* messages with CAN ID 0x003 will be received in mailbox 0 */
        filter[2].can_mask = 0x7FF;
        filter[2].can_id = 0x002;   /* messages with CAN ID 0x002 will be received in mailbox 1 */
        for (uint8_t i = 0; i < 3; i++) {
            candev->driver->set_filter(candev, &filter[i]);
        }
        /* All other messages won't be received */
    }

    char line_buf[SHELL_DEFAULT_BUFSIZE];
    shell_run(shell_commands, line_buf, SHELL_DEFAULT_BUFSIZE);

    return 0;
}
