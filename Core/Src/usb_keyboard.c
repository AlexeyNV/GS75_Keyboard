#include "usb_keyboard.h"
#include "hid_fn.h"
#include "usb_consumer.h"
#include "main.h"
#include <string.h>

typedef struct { uint32_t bits[KEY_STATE_WORDS]; } Accepted;
static Accepted queue[USB_KEYBOARD_QUEUE_CAPACITY], latest, sync_state;
static volatile uint32_t head, tail, depth, generation;
static volatile uint32_t queue_max, overflow_count, resync_count, tx_count;
static volatile uint8_t configured, protocol, idle, leds, busy, sync_pending, overflow_pending;
static uint8_t tx_report[HID_IN_PACKET_SIZE], last_report[HID_NKRO_SIZE];
static uint16_t last_size;
static uint32_t last_tx_ms;
/* One accepted snapshot is retired only after BOTH outputs have been staged.
 * Separate endpoints/buffers may progress independently unless a Fn transition
 * requires the previous function's release to complete before its replacement. */
static uint8_t stage_active, stage_keyboard, stage_consumer, stage_order;

static uint32_t Lock(void) { uint32_t mask = __get_PRIMASK(); __disable_irq(); return mask; }
static void Unlock(uint32_t mask) { __set_PRIMASK(mask); }

static void Sync(void)
{
    head = tail = depth = 0;
    sync_state = latest;
    sync_pending = 1;
    overflow_pending = 0;
    stage_active = stage_keyboard = stage_consumer = 0;
    generation++;
}

void UsbKeyboard_Resync(void)
{
    uint32_t mask = Lock();
    Sync();
    Unlock(mask);
}

void UsbKeyboard_Init(void)
{
    uint32_t mask = Lock();
    memset(&latest, 0, sizeof(latest));
    queue_max = overflow_count = resync_count = tx_count = 0;
    configured = busy = idle = leds = 0;
    protocol = HID_PROTOCOL_REPORT;
    last_size = 0;
    last_tx_ms = 0;
    UsbConsumer_Init();
    Sync();
    Unlock(mask);
}

void UsbKeyboard_Capture(const KeyboardState *state)
{
    uint32_t mask = Lock();
    if (memcmp(latest.bits, state->pressed, sizeof(latest.bits)) != 0)
    {
        memcpy(latest.bits, state->pressed, sizeof(latest.bits));
        if (!configured)
        {
            sync_state = latest; /* Offline history is not replayed on connection. */
        }
        else if (!overflow_pending)
        {
            if (depth == USB_KEYBOARD_QUEUE_CAPACITY)
            {
                overflow_count++;
                Sync();
                overflow_pending = 1;
            }
            else
            {
                queue[head] = latest;
                head = (head + 1U) % USB_KEYBOARD_QUEUE_CAPACITY;
                depth++;
                if (depth > queue_max) { queue_max = depth; }
            }
        }
        if (overflow_pending)
        {
            sync_state = latest; /* Coalesce only after emergency overflow. */
            /* An earlier partial resync may already have staged one endpoint.
             * Both outputs must be reconsidered for this newer snapshot. */
            stage_active = stage_keyboard = stage_consumer = 0;
            generation++;       /* Invalidate any stale main-loop candidate. */
        }
    }
    Unlock(mask);
}

void UsbKeyboard_Reset(void)
{
    uint32_t mask = Lock();
    configured = busy = idle = leds = 0;
    protocol = HID_PROTOCOL_REPORT;
    last_size = 0;
    UsbConsumer_Reset();
    Sync();
    Unlock(mask);
}

void UsbKeyboard_Configure(uint8_t value)
{
    uint32_t mask = Lock();
    configured = value != 0U;
    busy = 0;
    last_size = 0;
    UsbConsumer_Reset();
    Sync();
    Unlock(mask);
}

uint8_t UsbKeyboard_SetProtocol(uint8_t value)
{
    if (value > HID_PROTOCOL_REPORT) { return 0; }
    /* Abort queued hardware data before acknowledging the new protocol. */
    if (!UsbKeyboardPort_Cancel()) { return 0; }
    uint32_t mask = Lock();
    busy = 0;
    protocol = value;
    last_size = 0;
    Sync();
    Unlock(mask);
    return 1;
}
uint8_t UsbKeyboard_GetProtocol(void) { return protocol; }
void UsbKeyboard_SetIdle(uint8_t duration) { idle = duration; }
uint8_t UsbKeyboard_GetIdle(void) { return idle; }
void UsbKeyboard_SetLeds(uint8_t value) { leds = value & 0x1FU; }
uint8_t UsbKeyboard_GetLeds(void) { return leds; }

static uint16_t Build(const Accepted *keys, uint8_t selected, uint8_t *report, uint8_t *consumer)
{
    KeyboardState logical = {0};
    HidEffectiveState effective;
    memcpy(logical.pressed, keys->bits, sizeof(keys->bits));
    HidFn_TranslateOutputs(&logical, &effective, consumer);
    if (selected == HID_PROTOCOL_BOOT)
    {
        HidBootReport boot;
        HidReport_BuildBoot(&effective, &boot);
        memcpy(report, &boot, sizeof(boot));
        return sizeof(boot);
    }
    HidNkroReport nkro;
    HidReport_BuildNkro(&effective, &nkro);
    memcpy(report, &nkro, sizeof(nkro));
    return sizeof(nkro);
}

uint16_t UsbKeyboard_GetReport(uint8_t *report)
{
    uint32_t mask = Lock();
    Accepted keys = latest;
    uint8_t selected = protocol;
    Unlock(mask);
    uint8_t consumer;
    return Build(&keys, selected, report, &consumer);
}

uint8_t UsbKeyboard_GetConsumerBits(void)
{
    uint32_t mask = Lock();
    Accepted keys = latest;
    Unlock(mask);
    uint8_t report[HID_NKRO_SIZE], consumer;
    (void)Build(&keys, HID_PROTOCOL_REPORT, report, &consumer);
    return consumer;
}

void UsbKeyboard_TxComplete(uint32_t now_ms)
{
    uint32_t mask = Lock();
    if (busy)
    {
        busy = 0;
        tx_count++;
        last_tx_ms = now_ms;
    }
    Unlock(mask);
}

static uint8_t TryKeyboard(const uint8_t *candidate, uint16_t length, uint8_t force, uint8_t due)
{
    uint8_t changed = last_size != length || memcmp(candidate, last_report, length) != 0;
    if (!force && !changed && !due) { return 1; }
    if (busy) { return 0; }
    memcpy(tx_report, candidate, length);
    if (!UsbKeyboardPort_Send(tx_report, length)) { return 0; }
    busy = 1;
    memcpy(last_report, candidate, length);
    last_size = length;
    return 1;
}

void UsbKeyboard_Task(uint32_t now_ms)
{
    uint32_t mask = Lock();
    if (!configured) { Unlock(mask); return; }
    uint8_t force = sync_pending;
    uint8_t queued = !force && depth != 0U;
    uint8_t due = idle != 0U && (uint32_t)(now_ms - last_tx_ms) >= (uint32_t)idle * 4U;
    if (!force && !queued && !due) { Unlock(mask); return; }
    Accepted keys = force ? sync_state : queued ? queue[tail] : latest;
    uint32_t version = generation;
    uint8_t selected = protocol;
    Unlock(mask);

    /* Translation and report construction never hold off RX or scanning. */
    uint8_t candidate[HID_NKRO_SIZE];
    uint8_t consumer;
    uint16_t length = Build(&keys, selected, candidate, &consumer);

    mask = Lock();
    if (!configured || generation != version) { Unlock(mask); return; }
    /* SET_IDLE can preempt report construction; do not use a stale deadline. */
    due = idle != 0U && (uint32_t)(now_ms - last_tx_ms) >= (uint32_t)idle * 4U;
    if (!force && !queued)
    {
        /* Idle belongs only to interface 0, never creates a shared queue stage. */
        (void)TryKeyboard(candidate, length, 0, due);
        Unlock(mask); return;
    }
    if (!stage_active)
    {
        uint8_t previous = UsbConsumer_LastBits();
        stage_order = (previous & (uint8_t)~consumer) ? 2U :
                      (consumer & (uint8_t)~previous) ? 1U : 0U;
        stage_active = 1;
    }
    if (stage_order == 2U) /* Consumer release before restoring a base key. */
    {
        if (!stage_consumer) { stage_consumer = UsbConsumer_TrySend(consumer, force); }
        if (!stage_consumer || UsbConsumer_Busy()) { Unlock(mask); return; }
    }
    if (!stage_keyboard) { stage_keyboard = TryKeyboard(candidate, length, force, due); }
    if (stage_order == 1U && (!stage_keyboard || busy))
    {
        /* Base keyboard release must reach the host before Consumer activation. */
        Unlock(mask); return;
    }
    if (!stage_consumer) { stage_consumer = UsbConsumer_TrySend(consumer, force); }
    if (!stage_keyboard || !stage_consumer) { Unlock(mask); return; }
    stage_active = stage_keyboard = stage_consumer = 0;
    if (force)
    {
        sync_pending = overflow_pending = 0;
        resync_count++;
    }
    else if (queued)
    {
        tail = (tail + 1U) % USB_KEYBOARD_QUEUE_CAPACITY;
        depth--;
    }
    Unlock(mask);
}

void UsbKeyboard_GetInfo(UsbKeyboardInfo *info)
{
    uint32_t mask = Lock();
    info->state = UsbKeyboardPort_State();
    info->configured = configured;
    info->protocol = protocol;
    info->idle = idle;
    info->led_mask = leds;
    info->tx_count = tx_count;
    info->queue_depth = depth;
    info->queue_max = queue_max;
    info->queue_overflow = overflow_count;
    info->resync_count = resync_count;
    info->last_report_size = last_size;
    Unlock(mask);
}
