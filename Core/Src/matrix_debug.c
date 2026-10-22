#include "matrix_debug.h"
#include "matrix.h"
#include "usart.h"
#include "uart_rx.h"
#include "keyboard_runtime.h"
#include "usb_keyboard.h"
#include "usb_consumer.h"
#include "runtime_timing.h"
#include "power_manager.h"
#include <stdio.h>
#include <string.h>

static char rx_buffer[64];
static uint32_t rx_index;
static uint8_t discard_until_eol;
static uint32_t last_report_ms;
static uint32_t reported_ghost_changes;

static void Send(const char *text)
{
    /* Main-loop only. SysTick keeps scanning and USART1 IRQ queues RX
     * during blocking transmission. Replies remain serialized. */
    size_t remaining = strlen(text);
    while (remaining)
    {
        uint16_t count = remaining > 64U ? 64U : (uint16_t)remaining;
        PowerManager_Task(HAL_GetTick());
        /* <=5.6ms wire time; bounded timeout services power on UART error. */
        HAL_StatusTypeDef status = HAL_UART_Transmit(&huart1, (uint8_t *)text, count, 10U);
        PowerManager_Task(HAL_GetTick());
        if (status != HAL_OK) { PowerManager_Off(); return; }
        text += count; remaining -= count;
    }
}

static void SendGhost(const char *source, const AntiGhostState *ghost)
{
    char line[96];
    snprintf(line, sizeof(line), "GHOST,%s,%s,ROW_PAIRS,%lu,RECTANGLES,%lu\r\n",
             source, ghost->row_pair_count ? "ACTIVE" : "CLEAR",
             (unsigned long)ghost->row_pair_count,
             (unsigned long)ghost->rectangle_count);
    Send(line);
    for (uint32_t row = 0; row < MATRIX_ROWS; row++)
    {
        if (ghost->ambiguous[row] != 0U)
        {
            snprintf(line, sizeof(line), "AMBIGUOUS,%s,FPC%02u,0x%05lX\r\n",
                     source, (unsigned int)Matrix_DriveFpc[row],
                     (unsigned long)ghost->ambiguous[row]);
            Send(line);
        }
    }
}

static void Dump(uint8_t use_raw)
{
    MatrixSnapshot snapshot;
    Matrix_GetSnapshot(&snapshot);
    const uint32_t *matrix = use_raw ? snapshot.raw : snapshot.debounced;
    uint32_t count = 0;
    char line[80];

    Send("MATRIX_BEGIN\r\n");
    snprintf(line, sizeof(line), "MATRIX_STATE,%s,8,18\r\n",
             use_raw ? "RAW" : "DEBOUNCED");
    Send(line);
    snprintf(line, sizeof(line), "SCAN,%lu,TIME_MS,%lu\r\n",
             (unsigned long)snapshot.scan_count, (unsigned long)snapshot.timestamp_ms);
    Send(line);
    for (uint32_t row = 0; row < MATRIX_ROWS; row++)
    {
        for (uint32_t col = 0; col < MATRIX_COLS; col++)
        {
            if ((matrix[row] & (1UL << col)) != 0U)
            {
                snprintf(line, sizeof(line), "EDGE,FPC%02u,FPC%02u\r\n",
                         (unsigned int)Matrix_DriveFpc[row],
                         (unsigned int)Matrix_SenseFpc[col]);
                Send(line);
                count++;
            }
        }
    }
    snprintf(line, sizeof(line), "EDGE_COUNT,%lu\r\n", (unsigned long)count);
    Send(line);
    Send("MATRIX_END\r\n");
}

static void Info(void)
{
    MatrixSnapshot snapshot;
    Matrix_GetSnapshot(&snapshot);
    char line[128];
    Send("GS75 Matrix 8x18 Firmware\r\nMCU=STM32F411RET6\r\n");
    snprintf(line, sizeof(line), "SYSCLK=%lu\r\nUART=115200\r\n",
             (unsigned long)HAL_RCC_GetSysClockFreq());
    Send(line);
    snprintf(line, sizeof(line),
             "MATRIX_ROWS=%u\r\nMATRIX_COLS=%u\r\nSCAN_HZ=%u\r\nDEBOUNCE_MS=%u\r\n",
             MATRIX_ROWS, MATRIX_COLS, MATRIX_SCAN_HZ, MATRIX_DEBOUNCE_MS);
    Send(line);
    Send("DRIVE=17,18,19,20,21,22,23,24\r\n");
    Send("SENSE=1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,25,26\r\n");
    Send("ANTI_GHOST=DETECT_ONLY\r\n");
    Send("LOGICAL_GHOST=CONSERVATIVE\r\nKEYMAP_KEYS=102\r\n");
    snprintf(line, sizeof(line), "SCAN_COUNT=%lu\r\nSCAN_TIME_MS=%lu\r\nMAX_SCAN_US=%lu\r\n",
             (unsigned long)snapshot.scan_count, (unsigned long)snapshot.timestamp_ms,
             (unsigned long)snapshot.max_scan_us);
    Send(line);
}

static void UsbInfo(void)
{
    UsbKeyboardInfo info;
    UsbKeyboard_GetInfo(&info);
    UsbConsumerInfo consumer;
    UsbConsumer_GetInfo(&consumer);
    uint8_t consumer_bits = UsbKeyboard_GetConsumerBits();
    char line[96];
    static const char *const states[] = {"OFF", "DEFAULT", "ADDRESSED", "CONFIGURED", "SUSPENDED"};
    snprintf(line, sizeof(line), "USB_STATE=%s\r\nUSB_CONFIGURED=%lu\r\nUSB_PROTOCOL=%s\r\n",
             info.state < 5U ? states[info.state] : "UNKNOWN",
             (unsigned long)info.configured, info.protocol ? "REPORT" : "BOOT");
    Send(line);
    snprintf(line, sizeof(line), "USB_IDLE=%lu\r\nUSB_LED_MASK=0x%02lX\r\nUSB_TX_COUNT=%lu\r\n",
             (unsigned long)info.idle, (unsigned long)info.led_mask, (unsigned long)info.tx_count);
    Send(line);
    snprintf(line, sizeof(line), "USB_QUEUE_DEPTH=%lu\r\nUSB_QUEUE_MAX=%lu\r\nUSB_QUEUE_OVERFLOW=%lu\r\n",
             (unsigned long)info.queue_depth, (unsigned long)info.queue_max, (unsigned long)info.queue_overflow);
    Send(line);
    snprintf(line, sizeof(line), "USB_RESYNC_COUNT=%lu\r\nUSB_LAST_REPORT_SIZE=%lu\r\nMAX_RUNTIME_US=%lu\r\n",
             (unsigned long)info.resync_count, (unsigned long)info.last_report_size,
             (unsigned long)RuntimeTiming_MaxUs());
    Send(line);
    snprintf(line, sizeof(line), "USB_CONSUMER_BITS=0x%02X\r\nUSB_CONSUMER_TX_COUNT=%lu\r\n",
             (unsigned int)consumer_bits, (unsigned long)consumer.tx_count);
    Send(line);
    snprintf(line, sizeof(line), "USB_CONSUMER_LAST_REPORT=0x%02lX\r\nUSB_CONSUMER_BUSY=%lu\r\n",
             (unsigned long)consumer.last_report, (unsigned long)consumer.busy);
    Send(line);
    snprintf(line, sizeof(line), "USB_CONSUMER_RESYNC_COUNT=%lu\r\n",
             (unsigned long)consumer.resync_count);
    Send(line);
}

static void GhostDump(void)
{
    MatrixSnapshot snapshot;
    Matrix_GetSnapshot(&snapshot);
    AntiGhostState raw_ghost;
    AntiGhost_Analyze(snapshot.raw, &raw_ghost);
    char line[96];
    Send("GHOST_BEGIN\r\n");
    snprintf(line, sizeof(line), "SCAN,%lu,TIME_MS,%lu,CHANGES,%lu\r\n",
             (unsigned long)snapshot.scan_count, (unsigned long)snapshot.timestamp_ms,
             (unsigned long)snapshot.ghost_changes);
    Send(line);
    SendGhost("RAW", &raw_ghost);
    SendGhost("DEBOUNCED", &snapshot.ghost);
    Send("GHOST_END\r\n");
}

static void SendKeySet(const char *kind, const uint32_t keys[KEY_STATE_WORDS])
{
    uint32_t count = 0;
    char line[80];
    for (uint32_t id = 1; id < KEY_ID_COUNT; id++)
    {
        if (Keyboard_KeyIsSet(keys, (KeyId)id))
        {
            snprintf(line, sizeof(line), "%s,%s\r\n", kind, Keymap_Name((KeyId)id));
            Send(line);
            count++;
        }
    }
    snprintf(line, sizeof(line), "%s_COUNT,%lu\r\n", kind, (unsigned long)count);
    Send(line);
}

static void KeysDump(void)
{
    KeyboardSnapshot snapshot;
    KeyboardRuntime_GetSnapshot(&snapshot);
    char line[80];
    Send("KEYS_BEGIN\r\n");
    snprintf(line, sizeof(line), "SCAN,%lu,TIME_MS,%lu\r\n",
             (unsigned long)snapshot.scan_count, (unsigned long)snapshot.timestamp_ms);
    Send(line);
    Send("POLICY,CONSERVATIVE\r\n");
    SendKeySet("KEY", snapshot.keys.pressed);
    SendKeySet("SUPPRESSED", snapshot.keys.suppressed);
    SendKeySet("AMBIGUOUS", snapshot.keys.ambiguous);
    Send("KEYS_END\r\n");
}

static void Command(void)
{
    if (strcmp(rx_buffer, "PING") == 0)
    {
        Send("PONG\r\n");
    }
    else if (strcmp(rx_buffer, "INFO") == 0)
    {
        Info();
    }
    else if (strcmp(rx_buffer, "DUMP") == 0)
    {
        Dump(0);
    }
    else if (strcmp(rx_buffer, "RAW") == 0)
    {
        Dump(1);
    }
    else if (strcmp(rx_buffer, "GHOST") == 0)
    {
        GhostDump();
    }
    else if (strcmp(rx_buffer, "KEYS") == 0)
    {
        KeysDump();
    }
    else if (strcmp(rx_buffer, "USBINFO") == 0)
    {
        UsbInfo();
    }
    else if (PowerDebug_Command(rx_buffer, Send))
    {
        /* All power commands execute in main context. */
    }
    else
    {
        Send("ERR: UNKNOWN COMMAND\r\n");
    }
}

void MatrixDebug_Init(void)
{
    rx_index = 0;
    discard_until_eol = 0;
    last_report_ms = HAL_GetTick();
    reported_ghost_changes = 0;
    UartRx_Init();
    Send("\r\nGS75 Matrix 8x18 Firmware\r\n");
    Send("Commands: PING, INFO, DUMP, RAW, GHOST, KEYS, USBINFO\r\nREADY\r\n");
}

static void ReportGhostChanges(void)
{
    uint32_t now = HAL_GetTick();
    if ((uint32_t)(now - last_report_ms) >= 100U)
    {
        last_report_ms = now;
        MatrixSnapshot snapshot;
        Matrix_GetSnapshot(&snapshot);
        if (snapshot.ghost_changes != reported_ghost_changes)
        {
            /* Coalesce diagnostic output, not matrix state. The counter
             * reveals transitions that occurred while UART was busy. */
            char line[80];
            snprintf(line, sizeof(line), "GHOST_EVENT,%lu,TIME_MS,%lu\r\n",
                     (unsigned long)snapshot.ghost_changes,
                     (unsigned long)snapshot.timestamp_ms);
            Send(line);
            SendGhost("DEBOUNCED", &snapshot.ghost);
            reported_ghost_changes = snapshot.ghost_changes;
        }
    }
}

void MatrixDebug_Process(void)
{
    uint8_t byte;
    UartRxResult result = UartRx_Pop(&byte);
    if (result == UART_RX_OVERFLOW || result == UART_RX_ERROR)
    {
        rx_index = 0;
        discard_until_eol = 1;
        Send(result == UART_RX_OVERFLOW ? "ERR: RX OVERFLOW\r\n" :
                                         "ERR: UART RX ERROR\r\n");
    }
    else if (result == UART_RX_BYTE)
    {
        if (discard_until_eol)
        {
            if (byte == '\r' || byte == '\n')
            {
                discard_until_eol = 0;
            }
        }
        else if (byte == '\r' || byte == '\n')
        {
            if (rx_index != 0U)
            {
                rx_buffer[rx_index] = '\0';
                Command();
                rx_index = 0;
            }
        }
        else if (rx_index < sizeof(rx_buffer) - 1U)
        {
            rx_buffer[rx_index++] = (char)byte;
        }
        else
        {
            rx_index = 0;
            discard_until_eol = 1;
            Send("ERR: BUFFER OVERFLOW\r\n");
        }
    }

    ReportGhostChanges();
}
