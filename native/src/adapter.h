#pragma once

#include "common.h"

struct AdapterRate
{
    double down = 0;
    double up = 0;
};

// Port of Services/AdapterSampler.cs: GetIfEntry counters are 32-bit and wrap,
// so deltas use unsigned subtraction and a gap longer than 5 s drops the sample.
class AdapterSampler
{
public:
    void Select(uint32_t index);
    uint32_t Index() const { return index_; }

    bool FollowDefaultRoute();
    AdapterRate Sample();
    void ResetBaseline();

    // Raw 32-bit counters, used by the acceptance probe.
    bool TryReadRaw(uint32_t& inOctets, uint32_t& outOctets) const;

private:
    uint32_t index_ = 0;
    uint32_t lastIn_ = 0;
    uint32_t lastOut_ = 0;
    int64_t lastTicks_ = 0;
    bool hasBaseline_ = false;
};

// Port of Services/DefaultRoute.cs: GetBestInterface(0) first, then the first
// up, non-loopback adapter that has a gateway.
uint32_t DefaultInterfaceIndex();

// FriendlyName of an interface ("WLAN 2"), empty when it is gone.
std::wstring AdapterNameOf(uint32_t index);
