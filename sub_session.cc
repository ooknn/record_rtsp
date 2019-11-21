#include "sub_session.hpp"
#include <StreamReplicator.hh>
#include <H264VideoRTPSink.hh>
#include <H264VideoStreamDiscreteFramer.hh>
#include <iostream>

RecordServerMediaSubsession *RecordServerMediaSubsession::createNew(UsageEnvironment &env,
                                                                    StreamReplicator *replicator,
                                                                    size_t bit_rate)
{
    return new RecordServerMediaSubsession(env, replicator, bit_rate);
}

RecordServerMediaSubsession::RecordServerMediaSubsession(UsageEnvironment &env,
                                                         StreamReplicator *replicator,
                                                         size_t bit_rate)
    : OnDemandServerMediaSubsession(env, False)
    , replicator_(replicator)
    , bit_rate_(bit_rate)
{

    std::cout << "  estimated bitrate of " << bit_rate_ << " (kbps) is created\n";
}

FramedSource *RecordServerMediaSubsession::createNewStreamSource(unsigned clientSessionId, unsigned &bit_rate)
{

    bit_rate = static_cast<unsigned int>(this->bit_rate_);
    auto source = replicator_->createStreamReplica();
    return H264VideoStreamDiscreteFramer::createNew(envir(), source);
}

RTPSink *RecordServerMediaSubsession::createNewRTPSink(Groupsock *rtpGroupsock,
                                                       unsigned char rtpPayloadTypeIfDynamic,
                                                       FramedSource *inputSource)
{
    return H264VideoRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic);
}
