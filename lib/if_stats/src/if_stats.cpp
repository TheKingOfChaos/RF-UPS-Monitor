#include "if_stats.h"

FrameKind classifyFrame(const uint8_t* frame, size_t length) {
    if (frame == nullptr || length < 6) return FrameKind::Unicast;
    bool allOnes = true;
    for (int i = 0; i < 6; i++) {
        if (frame[i] != 0xFF) allOnes = false;
    }
    if (allOnes) return FrameKind::Broadcast;
    // Group bit: lowest bit of the first byte of the destination address
    return (frame[0] & 0x01) ? FrameKind::Multicast : FrameKind::Unicast;
}

void ifCountFrame(IfCounters& c, const uint8_t* frame, size_t length, bool outbound) {
    FrameKind kind = classifyFrame(frame, length);
    if (outbound) {
        c.outOctets += length;
        if (kind == FrameKind::Unicast) c.outUcastPkts++;
        else if (kind == FrameKind::Multicast) c.outMulticastPkts++;
        else c.outBroadcastPkts++;
    } else {
        c.inOctets += length;
        if (kind == FrameKind::Unicast) c.inUcastPkts++;
        else if (kind == FrameKind::Multicast) c.inMulticastPkts++;
        else c.inBroadcastPkts++;
    }
}

PhyStatus decodePhyCfgr(uint8_t reg) {
    PhyStatus p;
    p.linkUp = (reg & 0x01) != 0;
    p.speed100 = (reg & 0x02) != 0;
    p.fullDuplex = (reg & 0x04) != 0;
    return p;
}

uint32_t phySpeedBps(const PhyStatus& phy) {
    if (!phy.linkUp) return 0;
    return phy.speed100 ? 100000000u : 10000000u;
}
