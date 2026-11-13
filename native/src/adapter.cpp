#include "adapter.h"

#include <iphlpapi.h>
#include <vector>

#pragma comment(lib, "iphlpapi.lib")

namespace
{
constexpr double kResetThresholdSeconds = 5.0;

bool ReadCounters(uint32_t index, uint32_t& inOctets, uint32_t& outOctets)
{
    MIB_IFROW row{};
    row.dwIndex = index;
    if (GetIfEntry(&row) != NO_ERROR) return false;

    inOctets = row.dwInOctets;
    outOctets = row.dwOutOctets;
    return true;
}

// Owns the GetAdaptersAddresses buffer so FriendlyName/Next stay valid while
// the caller iterates. Copying IP_ADAPTER_ADDRESSES out would leave dangling
// pointers into a freed buffer.
class AdapterList
{
public:
    AdapterList()
    {
        ULONG size = 16 * 1024;

        for (int attempt = 0; attempt < 3; ++attempt)
        {
            buffer_.resize(size);
            const ULONG result = GetAdaptersAddresses(
                AF_UNSPEC,
                GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                nullptr,
                reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer_.data()),
                &size);

            if (result == NO_ERROR)
            {
                valid_ = true;
                return;
            }

            if (result != ERROR_BUFFER_OVERFLOW) return;
        }
    }

    const IP_ADAPTER_ADDRESSES* First() const
    {
        return valid_ ? reinterpret_cast<const IP_ADAPTER_ADDRESSES*>(buffer_.data()) : nullptr;
    }

private:
    std::vector<BYTE> buffer_;
    bool valid_ = false;
};
} // namespace

void AdapterSampler::Select(uint32_t index)
{
    if (index == index_) return;
    index_ = index;
    hasBaseline_ = false;
}

void AdapterSampler::ResetBaseline()
{
    hasBaseline_ = false;
}

bool AdapterSampler::FollowDefaultRoute()
{
    const uint32_t index = DefaultInterfaceIndex();
    if (index == index_) return index != 0;
    Select(index);
    return index != 0;
}

bool AdapterSampler::TryReadRaw(uint32_t& inOctets, uint32_t& outOctets) const
{
    if (index_ == 0)
    {
        inOctets = 0;
        outOctets = 0;
        return false;
    }

    return ReadCounters(index_, inOctets, outOctets);
}

AdapterRate AdapterSampler::Sample()
{
    if (index_ == 0) return {};

    uint32_t inNow = 0;
    uint32_t outNow = 0;
    if (!ReadCounters(index_, inNow, outNow)) return {};

    const int64_t now = NowTicks();

    if (!hasBaseline_)
    {
        lastIn_ = inNow;
        lastOut_ = outNow;
        lastTicks_ = now;
        hasBaseline_ = true;
        return {};
    }

    const double seconds = TicksToSeconds(now - lastTicks_);
    if (seconds <= 0) return {};

    if (seconds > kResetThresholdSeconds)
    {
        lastIn_ = inNow;
        lastOut_ = outNow;
        lastTicks_ = now;
        return {};
    }

    const uint32_t deltaIn = inNow - lastIn_;
    const uint32_t deltaOut = outNow - lastOut_;

    lastIn_ = inNow;
    lastOut_ = outNow;
    lastTicks_ = now;

    return {deltaIn / seconds, deltaOut / seconds};
}

uint32_t DefaultInterfaceIndex()
{
    DWORD index = 0;
    if (GetBestInterface(0, &index) == NO_ERROR && index != 0) return static_cast<uint32_t>(index);

    const AdapterList adapters;
    for (const IP_ADAPTER_ADDRESSES* a = adapters.First(); a; a = a->Next)
    {
        if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        if (a->IfType == IF_TYPE_TUNNEL) continue;
        if (a->OperStatus != IfOperStatusUp) continue;
        if (a->FirstGatewayAddress == nullptr) continue;
        return a->IfIndex;
    }

    return 0;
}

std::wstring AdapterNameOf(uint32_t index)
{
    const AdapterList adapters;
    for (const IP_ADAPTER_ADDRESSES* a = adapters.First(); a; a = a->Next)
    {
        if (a->IfIndex == index) return a->FriendlyName ? a->FriendlyName : L"";
    }

    return {};
}
