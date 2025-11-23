#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace vision {
namespace network {

// Network configuration mode
enum class IPMode {
    DHCP,
    Static,
    Unknown
};

// Network information
struct NetworkInfo {
    std::string hostname;
    std::string ip_address;
    IPMode ip_mode;
    std::string interface_name;

    nlohmann::json toJson() const;
};

// Get current network configuration
NetworkInfo getNetworkInfo();

// Get hostname
std::string getHostname();

// Get primary IP address
std::string getPrimaryIP();

// Check if using DHCP or static IP
IPMode getIPMode();

// Get list of network interfaces
std::vector<std::string> getNetworkInterfaces();

// Get current platform ("linux", "windows", "macos")
std::string getPlatform();

// Validate hostname (returns empty string if valid, error message if invalid)
std::string validateHostname(const std::string& hostname);

// Calculate static IP from team number (returns IP in format 10.TE.AM.15)
std::string calculateStaticIP(int teamNumber);

// Calculate default gateway from team number (returns IP in format 10.TE.AM.1)
std::string calculateDefaultGateway(int teamNumber);

// Set system hostname (Linux only, requires reboot)
bool setHostname(const std::string& hostname, std::string& error);

// Configure static IP on interface (Linux only, uses nmcli)
bool setStaticIP(const std::string& iface, const std::string& ip,
                 const std::string& gateway, const std::string& subnet, std::string& error);

// Configure DHCP on interface (Linux only, uses nmcli)
bool setDHCP(const std::string& iface, std::string& error);

} // namespace network
} // namespace vision
