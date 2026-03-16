#ifndef PICO_CORE_FIFO_H
#define PICO_CORE_FIFO_H

// ============================================================================
// PicoCoreFifo.h
// Inter-core byte-stream FIFO for RP2040
//
// Sends MSG_TOTAL_BYTES as one logical message over the RP2040 32-bit hardware
// FIFO. Word splitting/joining is invisible to the caller.
//
// USAGE:
//   Send:
//     uint8_t msg[MSG_TOTAL_BYTES];
//     msg_set_type(msg, MSG_VALUE_UPDATE);
//     msg_set_id  (msg, 3);
//     float_to_msg(76.5f, msg);
//     fifo_send(msg);
//
//   Receive (blocking):
//     uint8_t msg[MSG_TOTAL_BYTES];
//     fifo_recv(msg);
//     float val = msg_to_float(msg);
//
//   Receive (non-blocking):
//     uint8_t msg[MSG_TOTAL_BYTES];
//     if (fifo_recv_ready(msg)) { ... }
//
// EXTENDING:
//   Change the geometry defines below. MSG_INT_BYTES is 2 by default so the
//   message is 5 bytes and is forced across two 32-bit FIFO words from the
//   start — this exercises the packing layer even at small sizes.
//   If MSG_TOTAL_BYTES ever exceeds 8 the static_assert will catch it at
//   compile time — split the message type or widen MSG_FIFO_WORDS budget.
// ============================================================================

#include <Arduino.h>
#include <stdint.h>
#include <string.h>

// ============================================================================
// MESSAGE GEOMETRY — defined by YOUR APP, not this library
//
// Before including this file, define the byte width of each message field:
//
//   #define MSG_TYPE_BYTES  1   // message type:      1 byte  = 256 types
//   #define MSG_ID_BYTES    1   // registry item id:  1 byte  = 256 items
//   #define MSG_INT_BYTES   2   // integer part:      2 bytes = 0..65535
//   #define MSG_FRAC_BYTES  1   // fractional part:   1 byte  = 0..99
//   #include <PicoCoreFifo.h>
//
// MSG_TOTAL_BYTES is derived automatically.
// If MSG_TOTAL_BYTES exceeds 8 a static_assert will fire at compile time.
// ============================================================================
#ifndef MSG_TYPE_BYTES
  #error "MSG_TYPE_BYTES not defined — define message geometry before #include <PicoCoreFifo.h>"
#endif
#ifndef MSG_ID_BYTES
  #error "MSG_ID_BYTES not defined — define message geometry before #include <PicoCoreFifo.h>"
#endif
#ifndef MSG_INT_BYTES
  #error "MSG_INT_BYTES not defined — define message geometry before #include <PicoCoreFifo.h>"
#endif
#ifndef MSG_FRAC_BYTES
  #error "MSG_FRAC_BYTES not defined — define message geometry before #include <PicoCoreFifo.h>"
#endif

#define MSG_TOTAL_BYTES (MSG_TYPE_BYTES + MSG_ID_BYTES + MSG_INT_BYTES + MSG_FRAC_BYTES)
// MSG_TOTAL_BYTES == 5 with defaults

// number of 32-bit FIFO words needed to carry MSG_TOTAL_BYTES
#define MSG_FIFO_WORDS  ((MSG_TOTAL_BYTES + 3) / 4)

// byte offsets into a message buffer — derived from geometry, never hardcoded
#define MSG_OFF_TYPE    0
#define MSG_OFF_ID      (MSG_OFF_TYPE + MSG_TYPE_BYTES)
#define MSG_OFF_INT     (MSG_OFF_ID   + MSG_ID_BYTES)
#define MSG_OFF_FRAC    (MSG_OFF_INT  + MSG_INT_BYTES)

// compile-time check — if MSG_TOTAL_BYTES > 8 you need to revisit fifo_send/recv
static_assert(MSG_TOTAL_BYTES <= 8, "MSG_TOTAL_BYTES > 8 bytes — update fifo_send/recv word budget");

// ============================================================================
// MESSAGE TYPES
// Add new types here as needed
// ============================================================================
#define MSG_NONE             0x00 
#define MSG_VALUE_UPDATE  0x01   // Core 0 -> Core 1: browser changed a registry value
#define MSG_VALUE_SYNC    0x02   // Core 1 -> Core 0: sensor updated a registry value
#define MSG_SVG_READY     0x03   // Core 1 -> Core 0: new svg buffer pointer is ready
#define MSG_REGISTRY_DONE 0x04   // Core 1 -> Core 0: startup registry copy complete

// ============================================================================
// PACKING — bytes <-> 32-bit words
// int Registry::count-endian: byte 0 in bits 7:0 of word 0, byte 4 in bits 7:0 of word 1
// ============================================================================
static inline void msg_to_words(const uint8_t* msg, uint32_t* words) {
    memset(words, 0, MSG_FIFO_WORDS * sizeof(uint32_t));
    for (int i = 0; i < MSG_TOTAL_BYTES; i++)
        words[i / 4] |= ((uint32_t)msg[i] << (8 * (i % 4)));
}

static inline void words_to_msg(const uint32_t* words, uint8_t* msg) {
    for (int i = 0; i < MSG_TOTAL_BYTES; i++)
        msg[i] = (uint8_t)((words[i / 4] >> (8 * (i % 4))) & 0xFF);
}

// ============================================================================
// FIFO SEND / RECV
// These are the only functions callers should use.
// The hardware has separate FIFOs in each direction so both cores can call
// fifo_send simultaneously without collision.
// ============================================================================

// send MSG_TOTAL_BYTES to the other core — blocking

// non-blocking receive — returns true if a full message was available
// NOTE: only checks if the first word is ready — safe because fifo_send
// always pushes all words before returning, so if word 0 is there the
// rest are guaranteed to follow immediately


static inline bool fifo_send(const uint8_t* msg) {
    uint32_t words[MSG_FIFO_WORDS];
    msg_to_words(msg, words);
    for (int i = 0; i < MSG_FIFO_WORDS; i++)
        if (!rp2040.fifo.push_nb(words[i])) return false;
    return true;
}

static inline bool fifo_recv(uint8_t* msg) {
    if (rp2040.fifo.available() < MSG_FIFO_WORDS) return false;

    uint32_t words[MSG_FIFO_WORDS];
    for (int i = 0; i < MSG_FIFO_WORDS; i++) {
        rp2040.fifo.pop_nb(&words[i]);
    }
    words_to_msg(words, msg);
    return true;
}


// ============================================================================
// FIELD ACCESSORS
// Build and read message buffers by named field.
// INT field is big-endian within its multi-byte slot.
// ============================================================================
static inline void msg_set_type(uint8_t* msg, uint8_t type) {
    msg[MSG_OFF_TYPE] = type;
}
static inline void msg_set_id(uint8_t* msg, uint8_t id) {
    msg[MSG_OFF_ID] = id;
}
static inline void msg_set_int(uint8_t* msg, uint16_t val_int) {
    // big-endian within the INT slot
    for (int i = 0; i < MSG_INT_BYTES; i++)
        msg[MSG_OFF_INT + i] = (val_int >> (8 * (MSG_INT_BYTES - 1 - i))) & 0xFF;
}
static inline void msg_set_frac(uint8_t* msg, uint8_t val_frac) {
    msg[MSG_OFF_FRAC] = val_frac;
}

static inline uint8_t msg_get_type(const uint8_t* msg) {
    return msg[MSG_OFF_TYPE];
}
static inline uint8_t msg_get_id(const uint8_t* msg) {
    return msg[MSG_OFF_ID];
}
static inline uint16_t msg_get_int(const uint8_t* msg) {
    uint16_t v = 0;
    for (int i = 0; i < MSG_INT_BYTES; i++)
        v |= ((uint16_t)msg[MSG_OFF_INT + i] << (8 * (MSG_INT_BYTES - 1 - i)));
    return v;
}
static inline uint8_t msg_get_frac(const uint8_t* msg) {
    return msg[MSG_OFF_FRAC];
}

// ============================================================================
// FLOAT HELPERS
// ============================================================================
static inline float msg_to_float(const uint8_t* msg) {
    return (float)msg_get_int(msg) + (float)msg_get_frac(msg) / 100.0f;
}
static inline void float_to_msg(float v, uint8_t* msg) {
    if (v < 0.0f) v = 0.0f;
    uint16_t i = (uint16_t)v;
    uint8_t  f = (uint8_t)((v - (float)i) * 100.0f);
    msg_set_int(msg, i);
    msg_set_frac(msg, f);
}

#endif // PICO_CORE_FIFO_H
