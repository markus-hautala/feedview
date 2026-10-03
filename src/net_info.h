// Host name and local IPv4 addresses, for showing the web remote's address.
#pragma once

#include <string>
#include <vector>

std::string localHostName();

// Usable IPv4 addresses of this machine, best first: private LAN ranges, then others,
// then link-local (169.254.x.x - a direct cable with no DHCP). Loopback is left out.
std::vector<std::string> localIPv4Addresses();
