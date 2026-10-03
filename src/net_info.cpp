#include "net_info.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <unistd.h>
#endif

namespace {

// 0 = private LAN, 1 = other, 2 = link-local
int rank(uint32_t ip) {
    const uint8_t a = ip >> 24, b = (ip >> 16) & 0xff;
    if (a == 192 && b == 168) return 0;
    if (a == 10) return 0;
    if (a == 172 && b >= 16 && b <= 31) return 0;
    if (a == 169 && b == 254) return 2;
    return 1;
}

void addAddress(std::vector<std::pair<int, std::string>>& out, uint32_t hostOrderIp) {
    if ((hostOrderIp >> 24) == 127 || hostOrderIp == 0) return;
    char buf[INET_ADDRSTRLEN] = {};
    in_addr a{};
    a.s_addr = htonl(hostOrderIp);
    if (!inet_ntop(AF_INET, &a, buf, sizeof buf)) return;
    std::string s = buf;
    for (const auto& e : out)
        if (e.second == s) return;
    out.push_back({rank(hostOrderIp), s});
}

}  // namespace

std::string localHostName() {
#if defined(_WIN32)
    wchar_t wbuf[256];
    DWORD n = 256;
    if (GetComputerNameExW(ComputerNameDnsHostname, wbuf, &n) && n > 0) {
        char buf[512];
        int len = WideCharToMultiByte(CP_UTF8, 0, wbuf, int(n), buf, sizeof buf - 1, nullptr, nullptr);
        if (len > 0) return std::string(buf, size_t(len));
    }
    return "this-computer";
#else
    char buf[256] = {};
    if (gethostname(buf, sizeof buf - 1) != 0 || !buf[0]) return "this-computer";
    std::string s = buf;
    // macOS often reports "name.local"; keep just the name.
    if (s.size() > 6 && s.compare(s.size() - 6, 6, ".local") == 0) s.resize(s.size() - 6);
    return s;
#endif
}

std::vector<std::string> localIPv4Addresses() {
    std::vector<std::pair<int, std::string>> found;
#if defined(_WIN32)
    ULONG size = 16 * 1024;
    std::vector<unsigned char> buf(size);
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG rc = GetAdaptersAddresses(AF_INET, flags, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &size);
    if (rc == ERROR_BUFFER_OVERFLOW) {
        buf.resize(size);
        rc = GetAdaptersAddresses(AF_INET, flags, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &size);
    }
    if (rc == NO_ERROR) {
        for (auto* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()); a; a = a->Next) {
            if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
            for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
                if (!u->Address.lpSockaddr || u->Address.lpSockaddr->sa_family != AF_INET) continue;
                auto* sin = reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr);
                addAddress(found, ntohl(sin->sin_addr.s_addr));
            }
        }
    }
#else
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) == 0) {
        for (ifaddrs* i = list; i; i = i->ifa_next) {
            if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET) continue;
            if (!(i->ifa_flags & IFF_UP) || (i->ifa_flags & IFF_LOOPBACK)) continue;
            auto* sin = reinterpret_cast<sockaddr_in*>(i->ifa_addr);
            addAddress(found, ntohl(sin->sin_addr.s_addr));
        }
        freeifaddrs(list);
    }
#endif
    std::stable_sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<std::string> out;
    for (auto& f : found) out.push_back(f.second);
    return out;
}
