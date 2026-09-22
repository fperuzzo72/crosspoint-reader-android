#pragma once
#include <cstdint>

// Network state from the framework rather than from getifaddrs.
//
// The reason is NOT what this comment said before. It claimed Android 11 closes
// NETLINK access to ordinary apps and that getifaddrs therefore stopped seeing
// the interfaces. Measured on the device, with both answers side by side in the
// log, that is false: getifaddrs works here.
//
// The reason that remains, and it is a good one on its own: ConnectivityManager
// distinguishes "has an address" from "has internet". A hotel captive portal
// gives the first and not the second, and NET_CAPABILITY_VALIDATED is the
// difference. getifaddrs has no way to answer that.
//
// Recorded: the original cause of the symptom (the network picker opening with
// Wi-Fi already on) remains unexplained. The fix works; the diagnosis first
// given for it was wrong.

namespace crosspoint::android {

// true when there is an active AND validated network.
bool netIsOnline();

// Local IPv4 in network order, same as sin_addr. Zero when there is none.
uint32_t netLocalIpV4();

}  // namespace crosspoint::android
