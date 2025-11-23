#include "utils/network_utils.hpp"
#include <spdlog/spdlog.h>
#include <fstream>
#include <sstream>
#include <array>
#include <cstdlib>
#include <set>
#include <cctype>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>
#endif

namespace vision {
namespace network {

nlohmann::json NetworkInfo::toJson() const {
    std::string modeStr;
    switch (ip_mode) {
        case IPMode::DHCP: modeStr = "dhcp"; break;
        case IPMode::Static: modeStr = "static"; break;
        default: modeStr = "unknown"; break;
    }

    return nlohmann::json{
        {"hostname", hostname},
        {"ip_address", ip_address},
        {"ip_mode", modeStr},
        {"interface", interface_name}
    };
}

std::string getHostname() {
    char hostname[256];

#ifdef _WIN32
    DWORD size = sizeof(hostname);
    if (GetComputerNameA(hostname, &size)) {
        return std::string(hostname);
    }
#else
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        return std::string(hostname);
    }
#endif

    return "unknown";
}

std::string getPrimaryIP() {
#ifdef _WIN32
    // Windows implementation using GetAdaptersAddresses
    ULONG bufferSize = 15000;
    std::vector<uint8_t> buffer(bufferSize);
    PIP_ADAPTER_ADDRESSES addresses = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());

    DWORD result = GetAdaptersAddresses(
        AF_INET,
        GAA_FLAG_INCLUDE_PREFIX,
        nullptr,
        addresses,
        &bufferSize
    );

    if (result == ERROR_BUFFER_OVERFLOW) {
        buffer.resize(bufferSize);
        addresses = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());
        result = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, nullptr, addresses, &bufferSize);
    }

    if (result == NO_ERROR) {
        for (PIP_ADAPTER_ADDRESSES addr = addresses; addr != nullptr; addr = addr->Next) {
            // Skip loopback and disconnected adapters
            if (addr->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
            if (addr->OperStatus != IfOperStatusUp) continue;

            for (PIP_ADAPTER_UNICAST_ADDRESS unicast = addr->FirstUnicastAddress;
                 unicast != nullptr;
                 unicast = unicast->Next) {

                sockaddr_in* sockaddr = reinterpret_cast<sockaddr_in*>(unicast->Address.lpSockaddr);
                char ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &sockaddr->sin_addr, ip, sizeof(ip));

                std::string ipStr(ip);
                // Skip 127.x.x.x
                if (ipStr.substr(0, 4) != "127.") {
                    return ipStr;
                }
            }
        }
    }
#else
    // Linux implementation
    struct ifaddrs* ifaddr;
    if (getifaddrs(&ifaddr) == -1) {
        return "0.0.0.0";
    }

    std::string result = "0.0.0.0";
    for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;

        // Skip loopback
        std::string ifname(ifa->ifa_name);
        if (ifname == "lo") continue;

        struct sockaddr_in* addr = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr);
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));

        result = std::string(ip);
        break;  // Take first non-loopback interface
    }

    freeifaddrs(ifaddr);
    return result;
#endif

    return "0.0.0.0";
}

IPMode getIPMode() {
#ifdef __linux__
    // Check NetworkManager or dhclient for DHCP status
    // This is a simplified check - might need adaptation for different systems

    // Check if dhclient is running for any interface
    FILE* pipe = popen("pgrep dhclient 2>/dev/null", "r");
    if (pipe) {
        char buffer[128];
        if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            pclose(pipe);
            return IPMode::DHCP;
        }
        pclose(pipe);
    }

    // Check NetworkManager
    pipe = popen("nmcli -t -f NAME,DEVICE,TYPE connection show --active 2>/dev/null | head -1", "r");
    if (pipe) {
        char buffer[256];
        if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            pclose(pipe);
            // Further analysis would be needed to determine DHCP vs Static
            return IPMode::Unknown;
        }
        pclose(pipe);
    }

    return IPMode::Unknown;
#elif _WIN32
    // Windows - check adapter settings
    // This would require more complex WMI queries
    return IPMode::Unknown;
#else
    return IPMode::Unknown;
#endif
}

NetworkInfo getNetworkInfo() {
    NetworkInfo info;
    info.hostname = getHostname();
    info.ip_address = getPrimaryIP();
    info.ip_mode = getIPMode();

#ifdef __linux__
    // Try to get primary interface name
    struct ifaddrs* ifaddr;
    if (getifaddrs(&ifaddr) == 0) {
        for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == nullptr) continue;
            if (ifa->ifa_addr->sa_family != AF_INET) continue;

            std::string ifname(ifa->ifa_name);
            if (ifname != "lo") {
                info.interface_name = ifname;
                break;
            }
        }
        freeifaddrs(ifaddr);
    }
#elif _WIN32
    info.interface_name = "Ethernet";  // Default
#endif

    return info;
}

std::vector<std::string> getNetworkInterfaces() {
    std::vector<std::string> interfaces;

#ifdef __linux__
    struct ifaddrs* ifaddr;
    if (getifaddrs(&ifaddr) == 0) {
        std::set<std::string> seen;
        for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
            if (ifa->ifa_name == nullptr) continue;

            std::string ifname(ifa->ifa_name);
            // Skip loopback
            if (ifname == "lo") continue;

            // Only add each interface once
            if (seen.find(ifname) == seen.end()) {
                seen.insert(ifname);
                interfaces.push_back(ifname);
            }
        }
        freeifaddrs(ifaddr);
    }
#endif

    return interfaces;
}

std::string getPlatform() {
#ifdef _WIN32
    return "windows";
#elif __APPLE__
    return "macos";
#elif __linux__
    return "linux";
#else
    return "unknown";
#endif
}

std::string validateHostname(const std::string& hostname) {
    if (hostname.empty()) {
        return "Hostname cannot be empty";
    }

    if (hostname.length() > 63) {
        return "Hostname must be 63 characters or less";
    }

    // Check first character (must be alphanumeric)
    if (!std::isalnum(static_cast<unsigned char>(hostname[0]))) {
        return "Hostname must start with a letter or number";
    }

    // Check last character (must be alphanumeric)
    if (!std::isalnum(static_cast<unsigned char>(hostname.back()))) {
        return "Hostname must end with a letter or number";
    }

    // Check all characters
    for (size_t i = 0; i < hostname.length(); ++i) {
        char c = hostname[i];
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-') {
            return "Hostname can only contain letters, numbers, and hyphens";
        }

        // Check for consecutive hyphens
        if (c == '-' && i > 0 && hostname[i-1] == '-') {
            return "Hostname cannot contain consecutive hyphens";
        }
    }

    return "";  // Valid
}

std::string calculateStaticIP(int teamNumber) {
    // Team number must be between 1 and 99999
    if (teamNumber < 1 || teamNumber > 99999) {
        return "10.0.0.15";
    }

    // Pad to 5 digits: 2852 → "02852"
    char padded[6];
    snprintf(padded, sizeof(padded), "%05d", teamNumber);

    // TE = first 3 digits as number (removes leading zeros): "028" → 28
    int te = (padded[0] - '0') * 100 + (padded[1] - '0') * 10 + (padded[2] - '0');
    // AM = last 2 digits as number: "52" → 52
    int am = (padded[3] - '0') * 10 + (padded[4] - '0');

    char ip[20];
    snprintf(ip, sizeof(ip), "10.%d.%d.15", te, am);
    return std::string(ip);
}

std::string calculateDefaultGateway(int teamNumber) {
    // Team number must be between 1 and 99999
    if (teamNumber < 1 || teamNumber > 99999) {
        return "10.0.0.1";
    }

    // Same logic as calculateStaticIP but with .1 instead of .15
    char padded[6];
    snprintf(padded, sizeof(padded), "%05d", teamNumber);

    int te = (padded[0] - '0') * 100 + (padded[1] - '0') * 10 + (padded[2] - '0');
    int am = (padded[3] - '0') * 10 + (padded[4] - '0');

    char gateway[20];
    snprintf(gateway, sizeof(gateway), "10.%d.%d.1", te, am);
    return std::string(gateway);
}

bool setHostname(const std::string& hostname, std::string& error) {
#ifdef __linux__
    // Validate hostname first
    std::string validationError = validateHostname(hostname);
    if (!validationError.empty()) {
        error = validationError;
        return false;
    }

    // Use hostnamectl to set the hostname (will apply after reboot)
    std::string cmd = "hostnamectl set-hostname " + hostname + " 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        error = "Failed to execute hostnamectl command";
        return false;
    }

    char buffer[256];
    std::string output;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }

    int exitCode = pclose(pipe);
    if (exitCode != 0) {
        error = output.empty() ? "hostnamectl command failed" : output;
        return false;
    }

    spdlog::info("Hostname set to '{}' (will apply after reboot)", hostname);
    return true;
#else
    error = "Setting hostname is only supported on Linux";
    return false;
#endif
}

bool setStaticIP(const std::string& iface, const std::string& ip,
                 const std::string& gateway, const std::string& subnet, std::string& error) {
#ifdef __linux__
    // Get current connection name for the interface
    std::string getConnCmd = "nmcli -t -f NAME,DEVICE connection show | grep ':" + iface + "$' | cut -d: -f1";
    FILE* pipe = popen(getConnCmd.c_str(), "r");
    if (!pipe) {
        error = "Failed to execute nmcli command";
        return false;
    }

    char buffer[256];
    std::string connectionName;
    if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        connectionName = buffer;
        // Remove trailing newline
        connectionName.erase(connectionName.find_last_not_of("\n\r") + 1);
    }
    pclose(pipe);

    if (connectionName.empty()) {
        // Create a new connection for this interface
        connectionName = "static-" + iface;
        std::string createCmd = "nmcli connection add type ethernet con-name '" + connectionName +
                                "' ifname " + iface + " 2>&1";
        pipe = popen(createCmd.c_str(), "r");
        if (!pipe) {
            error = "Failed to create network connection";
            return false;
        }
        std::string output;
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            output += buffer;
        }
        if (pclose(pipe) != 0) {
            error = output.empty() ? "Failed to create connection" : output;
            return false;
        }
    }

    // Calculate prefix from subnet mask (simple approach for common masks)
    int prefix = 24;  // Default to /24
    if (subnet == "255.255.255.0") prefix = 24;
    else if (subnet == "255.255.0.0") prefix = 16;
    else if (subnet == "255.0.0.0") prefix = 8;
    else if (subnet == "255.255.255.128") prefix = 25;
    else if (subnet == "255.255.255.192") prefix = 26;

    // Configure static IP
    std::string configCmd = "nmcli connection modify '" + connectionName + "' "
                           "ipv4.method manual "
                           "ipv4.addresses " + ip + "/" + std::to_string(prefix) + " "
                           "ipv4.gateway " + gateway + " 2>&1";

    pipe = popen(configCmd.c_str(), "r");
    if (!pipe) {
        error = "Failed to configure static IP";
        return false;
    }

    std::string output;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }

    int exitCode = pclose(pipe);
    if (exitCode != 0) {
        error = output.empty() ? "nmcli configuration failed" : output;
        return false;
    }

    // Bring up the connection
    std::string upCmd = "nmcli connection up '" + connectionName + "' 2>&1";
    pipe = popen(upCmd.c_str(), "r");
    if (!pipe) {
        error = "Failed to activate connection";
        return false;
    }

    output.clear();
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }

    exitCode = pclose(pipe);
    if (exitCode != 0) {
        error = output.empty() ? "Failed to activate connection" : output;
        return false;
    }

    spdlog::info("Static IP {} configured on interface {}", ip, iface);
    return true;
#else
    error = "Static IP configuration is only supported on Linux";
    return false;
#endif
}

bool setDHCP(const std::string& iface, std::string& error) {
#ifdef __linux__
    // Get current connection name for the interface
    std::string getConnCmd = "nmcli -t -f NAME,DEVICE connection show | grep ':" + iface + "$' | cut -d: -f1";
    FILE* pipe = popen(getConnCmd.c_str(), "r");
    if (!pipe) {
        error = "Failed to execute nmcli command";
        return false;
    }

    char buffer[256];
    std::string connectionName;
    if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        connectionName = buffer;
        connectionName.erase(connectionName.find_last_not_of("\n\r") + 1);
    }
    pclose(pipe);

    if (connectionName.empty()) {
        error = "No connection found for interface " + iface;
        return false;
    }

    // Configure DHCP
    std::string configCmd = "nmcli connection modify '" + connectionName + "' "
                           "ipv4.method auto "
                           "ipv4.addresses '' "
                           "ipv4.gateway '' 2>&1";

    pipe = popen(configCmd.c_str(), "r");
    if (!pipe) {
        error = "Failed to configure DHCP";
        return false;
    }

    std::string output;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }

    int exitCode = pclose(pipe);
    if (exitCode != 0) {
        error = output.empty() ? "nmcli configuration failed" : output;
        return false;
    }

    // Bring up the connection
    std::string upCmd = "nmcli connection up '" + connectionName + "' 2>&1";
    pipe = popen(upCmd.c_str(), "r");
    if (!pipe) {
        error = "Failed to activate connection";
        return false;
    }

    output.clear();
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }

    exitCode = pclose(pipe);
    if (exitCode != 0) {
        error = output.empty() ? "Failed to activate connection" : output;
        return false;
    }

    spdlog::info("DHCP configured on interface {}", iface);
    return true;
#else
    error = "DHCP configuration is only supported on Linux";
    return false;
#endif
}

} // namespace network
} // namespace vision
