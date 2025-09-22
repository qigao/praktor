#pragma once

#include "phmap/phmap.h"

#include <cstdint>
#include <string>

/**
 * @brief Represents the connection details for a service endpoint.
 *
 * This struct encapsulates all information a client needs to establish a
 * connection, such as the protocol, address, and authentication credentials.
 */
struct Endpoint {
    std::string host;         // e.g., "service.example.com" or an IP address
    uint16_t port;            // e.g., 8080
    std::string path;         // e.g., "/api/v2/updates" (optional)
    std::string auth_token;   // e.g., "Bearer a1b2c3d4..." (optional)

    /**
     * @brief Constructs the full URL for this endpoint.
     * @return The complete URL string, e.g., "https://service.com:8080/api".
     */
    inline std::string full_url(std::string const& scheme) const {

        std::string url = scheme + "://" + host;
        if ((scheme == "http" && port != 80) || (scheme == "https" && port != 443) || (scheme == "ws" && port != 80) ||
            (scheme == "wss" && port != 443)) {
            url += ":" + std::to_string(port);
        }
        if (!path.empty()) {
            if (path.front() != '/') url += '/';
            url += path;
        }
        return url;
    };
};

/**
 * @brief Represents a physical node in the distributed system.
 *
 * Contains not only the network endpoint but also metadata for load balancing,
 * operational control, fault tolerance, and application-specific logic.
 */
struct HostNode {

    // --- Core Identity & Connectivity ---
    std::string id;

    phmap::flat_hash_map<std::string, Endpoint> endpoints;

    /**
     * @brief The operational state of the node, used for intelligent traffic routing.
     */
    enum class State {
        /// @brief Starting up, not ready to serve traffic (e.g., warming caches).
        INITIALIZING,
        /// @brief Healthy and ready to serve traffic.
        ACTIVE,
        /// @brief Healthy but near capacity. Should be avoided for new assignments if possible.
        BUSY,
        /// @brief Gracefully shutting down. Should not accept new assignments.
        DRAINING,
        /// @brief Manually taken out of rotation by an operator for maintenance.
        INACTIVE,
        /// @brief Unreachable or crashed, as determined by a health checker.
        OFFLINE
    };

    State state = State::ACTIVE;

    // --- Load Balancing & Application Metadata ---
    uint32_t weight = 100;   // Proportional weight (e.g., 100 is standard).
    std::string version;     // e.g., "v2.1.4", for canary deployments.

    // --- Topology for Fault Tolerance ---
    std::string region;    // e.g., "us-east-1"
    std::string rack_id;   // e.g., "rack-42"

    /**
     * @brief Equality comparison based on the unique, stable ID.
     */
    bool operator==(HostNode const& other) const { return id == other.id; }
};
