/**
 * @file PeerTemplate.h
 * @brief BGP peer template configuration: inbound and outbound policies.
 */

#ifndef BGP_PEER_TEMPLATE_H
#define BGP_PEER_TEMPLATE_H

#include <string>
#include <unordered_map>
#include <cstring>

#include "configs/registry/router/BgpRegistry.h"
#include "bgp/BgpTypes.hpp"

namespace routing::bgp
{
class BgpProcess;

/**
 * @brief Named peer group: shared session and per-AF policy configuration for a set of neighbors.
 * @ingroup BGP_NEIGHBOR
 *
 * A PeerGroup bundles one session-level configuration registry with zero or
 * more per-AFI policy registries.  Any neighbor that references this peer group
 * by name inherits its configuration values unless the neighbor overrides them
 * with a more specific setting.
 *
 * Per-AF registries are created lazily on first access via @ref getAfConfigs
 * to avoid allocating configuration blocks for address families the operator
 * never configures.
 *
 * ## Lifecycle & Ownership
 * Owned by @ref PeerTemplateTable inside @ref NeighborTable.  Non-copyable
 * and non-movable because the configuration @ref config::Reference objects
 * register callbacks back into the process and must not relocate in memory.
 *
 * @see PeerSessionTemplate, PeerPolicyTemplate, NeighborTable
 */
class PeerGroup
{
public:
    /**
     * @brief Constructs a peer group with the given name and owning process.
     *
     * Allocates a session configuration registry under the process's config
     * namespace.  Per-AF registries are deferred until first use.
     *
     * @param groupName Unique name for this peer group within the process.
     * @param proc      Owning BGP process; used for config registry allocation.
     */
    PeerGroup(const std::string& groupName, BgpProcess& proc);
    ~PeerGroup() = default;

    PeerGroup(const PeerGroup&) = delete;
    PeerGroup& operator=(const PeerGroup&) = delete;
    PeerGroup(PeerGroup&&) = delete;
    PeerGroup& operator=(PeerGroup&&) = delete;

    const std::string name; ///< Unique name of this peer group within the BGP process.

    config::BgpNeighborSessionRegistry& getSessionConfigs()
    {
        return sessionConfigs;
    }

    const config::BgpNeighborSessionRegistry& getSessionConfigs() const
    {
        return sessionConfigs;
    }

    /**
     * @brief Return the per-AFI neighbor configuration for @p afi, creating it on first access.
     *
     * @param afi Address family identifier for which configuration is requested.
     * @return Pointer to the per-AF config registry, never null.
     */
    config::BgpNeighborRegistry* getAfConfigs(const AfiSafi& afi);
    const config::BgpNeighborRegistry* getAfConfigs(const AfiSafi& afi) const;

private:
    BgpProcess& process;
    config::BgpNeighborSessionRegistry& sessionConfigs;
    mutable std::unordered_map<AfiSafi, std::reference_wrapper<config::BgpNeighborRegistry>> afConfigs; ///< Lazily populated per-AF config registries.
};

/**
 * @brief Named session template: reusable session-level parameters shared across neighbors.
 * @ingroup BGP_NEIGHBOR
 *
 * A PeerSessionTemplate carries TCP and BGP session attributes (timers,
 * password, update-source, etc.) that can be applied to any number of
 * neighbors by name.  Unlike @ref PeerGroup, it has no per-AF configuration;
 * it covers only the transport and FSM layer.
 *
 * ## Lifecycle & Ownership
 * Owned by @ref PeerTemplateTable.  Non-copyable and non-movable for the
 * same reasons as @ref PeerGroup.
 *
 * @see PeerGroup, PeerPolicyTemplate
 */
class PeerSessionTemplate
{
public:
    /**
     * @brief Constructs a session template with the given name and owning process.
     *
     * @param groupName Unique name for this template within the process.
     * @param proc      Owning BGP process; used for config registry allocation.
     */
    PeerSessionTemplate(const std::string& groupName, BgpProcess& proc);
    ~PeerSessionTemplate() = default;

    PeerSessionTemplate(const PeerSessionTemplate&) = delete;
    PeerSessionTemplate& operator=(const PeerSessionTemplate&) = delete;
    PeerSessionTemplate(PeerSessionTemplate&&) = delete;
    PeerSessionTemplate& operator=(PeerSessionTemplate&&) = delete;

    const std::string name; ///< Unique name of this session template within the BGP process.

    config::BgpNeighborSessionRegistry& getConfigs()
    {
        return configs;
    }

    const config::BgpNeighborSessionRegistry& getConfigs() const
    {
        return configs;
    }

private:
    config::BgpNeighborSessionRegistry configs;
};

/**
 * @brief Named policy template: reusable per-AF inbound/outbound policy parameters.
 * @ingroup BGP_NEIGHBOR
 *
 * A PeerPolicyTemplate carries route-map, filter, and community policy
 * settings that operate at the address-family level.  Multiple neighbors can
 * reference the same policy template to share a common import/export policy
 * without repeating configuration.
 *
 * ## Lifecycle & Ownership
 * Owned by @ref PeerTemplateTable.  Non-copyable and non-movable for the
 * same reasons as @ref PeerGroup.
 *
 * @see PeerGroup, PeerSessionTemplate
 */
class PeerPolicyTemplate
{
public:
    /**
     * @brief Constructs a policy template with the given name and owning process.
     *
     * @param groupName Unique name for this template within the process.
     * @param proc      Owning BGP process; used for config registry allocation.
     */
    PeerPolicyTemplate(const std::string& groupName, BgpProcess& proc);
    ~PeerPolicyTemplate() = default;

    PeerPolicyTemplate(const PeerPolicyTemplate&) = delete;
    PeerPolicyTemplate& operator=(const PeerPolicyTemplate&) = delete;
    PeerPolicyTemplate(PeerPolicyTemplate&&) = delete;
    PeerPolicyTemplate& operator=(PeerPolicyTemplate&&) = delete;

    const std::string name; ///< Unique name of this policy template within the BGP process.

    config::BgpNeighborRegistry& getConfigs()
    {
        return configs;
    }

    const config::BgpNeighborRegistry& getConfigs() const
    {
        return configs;
    }

private:
    config::BgpNeighborRegistry& configs;
};

/**
 * @brief Container for all peer groups and reusable templates within one BGP process.
 * @ingroup BGP_NEIGHBOR
 *
 * PeerTemplateTable owns the three template collections used by a BGP process:
 * - **Peer groups** (@ref PeerGroup): combine session and per-AF policy config.
 * - **Session templates** (@ref PeerSessionTemplate): session-layer parameters only.
 * - **Policy templates** (@ref PeerPolicyTemplate): per-AF policy parameters only.
 *
 * The @ref sync method reconciles all three collections with the current
 * configuration registry, adding newly configured templates and removing
 * those that were deleted.  It also re-wires neighbor back-pointers to the
 * updated template objects so that configuration reads by the FSM always
 * resolve to the current template state.
 *
 * ## Lifecycle & Ownership
 * Owned by @ref NeighborTable, which in turn is owned by @ref BgpProcess.
 * Template objects are stored by value in unordered maps; since they are
 * non-movable, emplacement must use in-place construction.
 *
 * @see PeerGroup, PeerSessionTemplate, PeerPolicyTemplate, NeighborTable
 */
class PeerTemplateTable
{
public:
    /**
     * @brief Constructs an empty template table bound to @p proc.
     *
     * @param proc Owning BGP process; forwarded to each template object on creation.
     */
    explicit PeerTemplateTable(BgpProcess& proc);

    /**
     * @brief Reconcile all template collections with the current configuration registry.
     *
     * Adds templates that appear in configuration but not yet in the live maps,
     * removes those that have been deleted from configuration, and re-wires any
     * neighbor pointers that reference updated template objects.
     */
    void sync();

    /**
     * @brief Create a new peer group with the given name.
     *
     * @param name Unique peer group name within this process.
     * @return Reference to the newly created PeerGroup.
     */
    PeerGroup& createPeerGroup(const std::string& name);
    void removePeerGroup(const std::string& name);

    /**
     * @brief Create a new session template with the given name.
     *
     * @param name Unique session template name within this process.
     * @return Reference to the newly created PeerSessionTemplate.
     */
    PeerSessionTemplate& createPeerSessionTemplate(const std::string& name);
    void removePeerSessionTemplate(const std::string& name);

    /**
     * @brief Create a new policy template with the given name.
     *
     * @param name Unique policy template name within this process.
     * @return Reference to the newly created PeerPolicyTemplate.
     */
    PeerPolicyTemplate& createPeerPolicyTemplate(const std::string& name);
    void removePeerPolicyTemplate(const std::string& name);

    PeerGroup* lookupPeerGroup(const std::string& name);
    const PeerGroup* lookupPeerGroup(const std::string& name) const;
    PeerSessionTemplate* lookupPeerSessionTemplate(const std::string& name);
    const PeerSessionTemplate* lookupPeerSessionTemplate(const std::string& name) const;
    PeerPolicyTemplate* lookupPeerPolicyTemplate(const std::string& name);
    const PeerPolicyTemplate* lookupPeerPolicyTemplate(const std::string& name) const;

private:
    void syncPeerGroups();
    void syncPeerSessionTemplates();
    void syncPeerPolicyTemplates();

    BgpProcess& process;
    std::unordered_map<std::string, PeerGroup> peerGroups;                       ///< Active peer groups, keyed by name.
    std::unordered_map<std::string, PeerSessionTemplate> peerSessionTemplates;   ///< Active session templates, keyed by name.
    std::unordered_map<std::string, PeerPolicyTemplate> peerPolicyTemplates;     ///< Active policy templates, keyed by name.
};
} // namespace routing::bgp

#endif // BGP_PEER_TEMPLATE_H
