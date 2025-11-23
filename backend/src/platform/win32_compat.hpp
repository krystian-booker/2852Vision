#pragma once

// Windows workaround for missing htonll/ntohll in Drogon ORM headers
// These functions convert 64-bit integers between host and network byte order
// Windows Winsock2 only provides htonl (32-bit), not htonll (64-bit)

#ifdef _WIN32
#include <winsock2.h>
#include <cstdint>

#ifndef htonll
#define htonll(x) ((((uint64_t)htonl((uint32_t)(x))) << 32) + htonl((uint32_t)((x) >> 32)))
#endif

#ifndef ntohll
#define ntohll(x) ((((uint64_t)ntohl((uint32_t)(x))) << 32) + ntohl((uint32_t)((x) >> 32)))
#endif

#endif // _WIN32
