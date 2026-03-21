// GlobalAggregator.h

#ifndef EIGRP_GLOBAL_AGGREGATOR_H
#define EIGRP_GLOBAL_AGGREGATOR_H

namespace EIGRP
{
class Eigrp;
struct TopologyEntry;

class GlobalAggregator
{
public:
    GlobalAggregator(Eigrp& base);

    void addSummary(TopologyEntry& summary);
    void updateSummary(TopologyEntry& summary);
    void enableAutoSummary(bool enable);
    void recomputeAutoSummaries();

private:
    Eigrp& base;
};
}


#endif // GLOBAL_AGGREGATOR_H
