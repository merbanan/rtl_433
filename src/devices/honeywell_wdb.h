/* honeywell_wdb.h - hand-written helpers included by generated decoder. */
#pragma once

static inline bool honeywell_wdb_validate_packet(bitbuffer_t *bitbuffer)
{
    /* The decode function has already selected the repeated row and
       extracted b = bitbuffer->bb[row].  We re-derive it from row 0
       of the (possibly invert-modified) bitbuffer that the pipeline
       left behind. */
    uint8_t *b = bitbuffer->bb[0];
    int parity = parity_bytes(b, 6);

    if ((!b[0] && !b[2] && !b[4] && !b[5]) ||
            (b[0] == 0xff && b[2] == 0xff && b[4] == 0xff && b[5] == 0xff))
        return false;

    if (parity)
        return false;

    return true;
}

static const char *honeywell_wdb_subtype(int class_raw)
{
    switch (class_raw) {
    case 0x1: return "PIR-Motion";
    case 0x2: return "Doorbell";
    default: return "Unknown";
    }
}

static const char *honeywell_wdb_alert(int alert_raw)
{
    switch (alert_raw) {
    case 0x0: return "Normal";
    case 0x1:
    case 0x2: return "High";
    case 0x3: return "Full";
    default: return "Unknown";
    }
}
