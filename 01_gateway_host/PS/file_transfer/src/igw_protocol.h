#ifndef IGW_PROTOCOL_H
#define IGW_PROTOCOL_H
#include <stdint.h>
#include <stddef.h>

/* Modbus RTU: CRC bytes are low byte first; register values are big endian. */
static inline uint16_t igw_crc16(const uint8_t *p, size_t n)
{
    uint16_t crc = 0xffff;
    size_t i;
    unsigned b;
    for (i = 0; i < n; ++i) {
        crc ^= p[i];
        for (b = 0; b < 8; ++b)
            crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0xa001) : (uint16_t)(crc >> 1);
    }
    return crc;
}
static inline int igw_valid_adu(const uint8_t *p, size_t n)
{
    return p && n >= 4 && n <= 256 && igw_crc16(p, n) == 0;
}
static inline size_t igw_append_crc(uint8_t *p, size_t n)
{
    uint16_t crc = igw_crc16(p, n);
    p[n] = (uint8_t)crc;
    p[n + 1] = (uint8_t)(crc >> 8);
    return n + 2;
}
static inline size_t igw_make_read(uint8_t out[8], unsigned unit, unsigned start, unsigned count)
{
    if (!unit || unit > 247 || start > 65535 || !count || count > 125 || count > 65536u - start)
        return 0;
    out[0] = (uint8_t)unit; out[1] = 3;
    out[2] = (uint8_t)(start >> 8); out[3] = (uint8_t)start;
    out[4] = (uint8_t)(count >> 8); out[5] = (uint8_t)count;
    return igw_append_crc(out, 6);
}
/* >0 normal reply, <0 exception code, 0 unrelated/invalid. */
static inline int igw_parse_read(const uint8_t *p, size_t n, unsigned unit,
                                 unsigned count, uint16_t *values)
{
    unsigned i;
    if (!igw_valid_adu(p, n) || !unit || unit > 247 || p[0] != unit || !count || count > 125)
        return 0;
    if (n == 5 && p[1] == 0x83 && p[2] != 0) return -(int)p[2];
    if (p[1] != 3 || n != 5u + 2u * count || p[2] != 2u * count) return 0;
    if (values) for (i = 0; i < count; ++i)
        values[i] = (uint16_t)(((unsigned)p[3 + 2*i] << 8) | p[4 + 2*i]);
    return 1;
}
/* Read-only test slave. Broadcast/other addresses receive no reply. */
static inline size_t igw_slave_reply(const uint8_t *p, size_t n, unsigned unit,
                                     const uint16_t *regs, unsigned reg_count,
                                     uint8_t out[256])
{
    unsigned start, count, i, exception = 0;
    size_t length;
    if (!unit || unit > 247 || !igw_valid_adu(p, n) || p[0] != unit) return 0;
    out[0] = (uint8_t)unit;
    if (p[1] != 3) exception = 1;
    else if (n != 8) exception = 3;
    else {
        start = ((unsigned)p[2] << 8) | p[3];
        count = ((unsigned)p[4] << 8) | p[5];
        if (!count || count > 125) exception = 3;
        else if (!regs || start >= reg_count || count > reg_count - start) exception = 2;
        else {
            out[1] = 3; out[2] = (uint8_t)(2 * count);
            for (i = 0; i < count; ++i) {
                out[3 + 2*i] = (uint8_t)(regs[start + i] >> 8);
                out[4 + 2*i] = (uint8_t)regs[start + i];
            }
            return igw_append_crc(out, 3u + 2u * count);
        }
    }
    out[1] = (uint8_t)(p[1] | 0x80); out[2] = (uint8_t)exception; length = 3;
    return igw_append_crc(out, length);
}
#endif
