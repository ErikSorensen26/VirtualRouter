/**
 * @file GlobalAggregator.h
 * @brief Process-level summary route manager for EIGRP named mode.
 */

#ifndef EIGRP_GLOBAL_AGGREGATOR_H
#define EIGRP_GLOBAL_AGGREGATOR_H

namespace routing::eigrp
{
class Eigrp;
struct TopologyEntry;

/**
 * @brief Manages process-level summary routes configured via the
 *        @c summary-address command in EIGRP named mode.
 *
 * @c GlobalAggregator tracks which component prefixes are present in the RIB
 * and originates or withdraws the corresponding aggregate entry in the EIGRP
 * topology table when reachability changes.  Auto-summary (classful
 * summarisation at network boundaries) is also controlled here.
 *
 * @ingroup EIGRP_CORE
 */
class GlobalAggregator
{
public:
    /**
     * @brief Constructs a GlobalAggregator bound to the given EIGRP process.
     * @param process The owning EIGRP process instance.
     */
    GlobalAggregator(Eigrp& process);

    /**
     * @brief Installs a new process-level summary route into the topology table.
     * @param summary The topology entry describing the aggregate prefix to add.
     */
    void addSummary(TopologyEntry& summary);

    /**
     * @brief Recomputes and re-advertises an existing summary route after a
     *        component prefix change.
     * @param summary The topology entry of the summary to update.
     */
    void updateSummary(TopologyEntry& summary);

    /**
     * @brief Enables or disables classful auto-summarisation for this process.
     * @param enable @c true to enable auto-summary, @c false to disable.
     */
    void enableAutoSummary(bool enable);

    /**
     * @brief Recalculates all auto-summary entries, installing or withdrawing
     *        aggregates as component routes appear or disappear.
     */
    void recomputeAutoSummaries();

private:
    Eigrp& process; ///< Reference to the owning EIGRP process.
};
} // namespace routing

#endif // GLOBAL_AGGREGATOR_H

