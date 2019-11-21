#ifndef __SUB_SESSION_HPP__
#define __SUB_SESSION_HPP__

#include <OnDemandServerMediaSubsession.hh>

class StreamReplicator;
class FramedSource;
class RTPSink;

class RecordServerMediaSubsession final : public OnDemandServerMediaSubsession
{

public:
    static RecordServerMediaSubsession *createNew(UsageEnvironment &env, StreamReplicator *replicator, size_t bit_rate = 100);

protected:
    StreamReplicator *replicator_;
    size_t bit_rate_;
    RecordServerMediaSubsession(UsageEnvironment &env, StreamReplicator *replicator, size_t);
    FramedSource *createNewStreamSource(unsigned, unsigned &) override;
    RTPSink *createNewRTPSink(Groupsock *, unsigned char, FramedSource *) override;
};

#endif
