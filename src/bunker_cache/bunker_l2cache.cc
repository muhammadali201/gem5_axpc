
#include "bunker_cache/bunker_l2cache.hh"

#include "base/random.hh"
#include "debug/BunkerL2Cache.hh"
#include "debug/BunkerRange.hh"
#include "sim/system.hh"

BunkerL2Cache::BunkerL2Cache(BunkerL2CacheParams *params) :
    MemObject(params),
    latency(params->latency),
    blockSize(params->system->cacheLineSize()),
    capacity(params->size / blockSize),
    radix(params->radix),
    stride(params->stride),
    L1CachePort(params->name + ".l1_side", this),
    memPort(params->name + ".mem_side", this),
    blocked(false),
    outstandingPacket(nullptr)
{

}

BaseMasterPort&
BunkerL2Cache::getMasterPort(const std::string &if_name, PortID idx)
{
    panic_if(idx != InvalidPortID, "This object doesn't support vector ports");
    // name from the Python SimObject declaration in BunkerL2Cache.py

    if (if_name == "mem_side") {
        return memPort;
    } else {
        return MemObject::getMasterPort(if_name,idx);
    }
}

BaseSlavePort&
BunkerL2Cache::getSlavePort(const std::string &if_name, PortID idx)
{
    if (if_name == "l1_side") {
        return L1CachePort;
    } else {
        return MemObject::getSlavePort(if_name,idx);
    }
}

void
BunkerL2Cache::L1SidePort::sendPacket(PacketPtr pkt)
{
    // Note: This flow control is very simple as cache is blocking

    panic_if(blockedPacket != nullptr, "Should never try to send if blocked");

    // if we can't send the pkt across port, store it for later.

    DPRINTF(BunkerL2Cache, "Send %s to L1 Cahce\n", pkt->print());

    if (!sendTimingResp(pkt)) {
        DPRINTF(BunkerL2Cache, "L2-->L1 sendPacket Failed\n");
        blockedPacket = pkt;
    }
}

AddrRangeList
BunkerL2Cache::L1SidePort::getAddrRanges() const
{
    return owner->getAddrRanges();
}

void
BunkerL2Cache::L1SidePort::trySendRetry()
{
    if (needRetry && blockedPacket == nullptr) {
        // Only send a retry if the port is not free
        needRetry = false;
        DPRINTF(BunkerL2Cache, "Sending retyr req\n");
        sendRetryReq();
    }
}

void
BunkerL2Cache::L1SidePort::recvFunctional(PacketPtr pkt)
{
    return owner->handleFunctional(pkt);
}

bool
BunkerL2Cache::L1SidePort::recvTimingReq(PacketPtr pkt)
{
    DPRINTF(BunkerL2Cache,"L1Side::recvTimingReq, Got request %s," \
                          " Will now call handleRequest \n", pkt->print());
    if (blockedPacket || needRetry) {
        DPRINTF(BunkerL2Cache," Request Blocked \n");
        needRetry = true;
        return false;
    }
    if (!owner->handleRequest(pkt)) {
        DPRINTF(BunkerL2Cache,"L1Side:recvTimingReq Request failed,"\
                              " owner->handleRequest returned false\n");
        needRetry = true;
        return false;
    } else {
        DPRINTF(BunkerL2Cache,"L1Side::recvTimingReq, handReq return "\
                              "true Req Succeeded and is now scheduled \n");
        return true;
    }

}

void
BunkerL2Cache::L1SidePort::recvRespRetry()
{
    // We should have a blocked packet if this function is called
    assert(blockedPacket != nullptr);

    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;
    DPRINTF(BunkerL2Cache,"Retrying response pkt %s \n", pkt->print());

    sendPacket(pkt);

    trySendRetry();
}

void
BunkerL2Cache::MemSidePort::sendPacket(PacketPtr pkt)
{
    panic_if(blockedPacket != nullptr, "MemSide Port Should never"\
                                       " try to send if blocked");

    DPRINTF(BunkerL2Cache,"Sending Packet to Memory \n");
    if (!sendTimingReq(pkt)) {
        DPRINTF(BunkerL2Cache, "mem_Port.sendTimingReq failure,"\
                               " Saving in memPort.blockedPacket\n");
        blockedPacket = pkt;
    }
}


bool
BunkerL2Cache::MemSidePort::chkBlockedPacket()
{
    if (blockedPacket != nullptr) {
        return false;
    } else {
        return true;
    }
}


bool
BunkerL2Cache::MemSidePort::recvTimingResp(PacketPtr pkt)
{
    return owner->handleResponse(pkt);
}

void
BunkerL2Cache::MemSidePort::recvReqRetry()
{
    assert(blockedPacket != nullptr);

    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;
    DPRINTF(BunkerL2Cache, "recvReqRetry from MemSidePort, but why \n");
    sendPacket(pkt);
}

void
BunkerL2Cache::MemSidePort::recvRangeChange()
{
    owner->sendRangeChange();
}

bool
BunkerL2Cache::handleRequest(PacketPtr pkt)
{
    if (blocked) {
        DPRINTF(BunkerL2Cache, "L2Cache::HandleReq, L2 Cache is blocked \n");
        return false;
    }

    DPRINTF(BunkerL2Cache, "L2Cache::HandReq, Got Request from L1:"\
                           " Phy Addr: %#x %s \n", pkt->getAddr(),\
                            pkt->print());
    if (pkt->req->hasVaddr()) {
        DPRINTF(BunkerL2Cache, "L2Cache::HandleReq, Vaddr = %#x \n",\
                               pkt->req->getVaddr());
    } else {
        DPRINTF(BunkerL2Cache, "L2Cache::HandleReq, Vaddr Not yet"\
                               " set, but why? \n");
    }

    blocked = true;

    DPRINTF(BunkerL2Cache, "L2Cache::HandleReq, Scheduling Req"\
                           " after latency \n");

    schedule(new AccessEvent(this, pkt), clockEdge(latency));

    return true;

}

bool
BunkerL2Cache::handleResponse(PacketPtr pkt)
{
    assert(blocked);
    DPRINTF(BunkerL2Cache, "L2Cache::handleResp, Got Response from mem Addr:"\
                           " %#x \n", pkt->getAddr());

    // For now, assume that inserts are out of critical path
    // and don't add latency

    insert(pkt);

    missLatency.sample(curTick() - missTime);

    if (outstandingPacket != nullptr) {
        DPRINTF(BunkerL2Cache, "L2Cache::handleResp calling accessFunc \n");
        bool hit M5_VAR_USED = accessFunctional(outstandingPacket);
        panic_if(!hit, "Should always hit after inserting");
        outstandingPacket->makeResponse();
        delete pkt;
        pkt = outstandingPacket;
        outstandingPacket = nullptr;
    }
    sendResponse(pkt);
    return true;
}

void
BunkerL2Cache::sendResponse(PacketPtr pkt)
{
    assert(blocked);
    blocked = false;
    L1CachePort.sendPacket(pkt); // Forward to L1cache port

    // If L1 cache needs to send a retry, it should do it now as
    //  now L2 cache is free (unblocked)
    L1CachePort.trySendRetry();
}

void
BunkerL2Cache::handleFunctional(PacketPtr pkt)
{
    if (accessFunctional(pkt)) {
        pkt->makeResponse();
    } else {
        memPort.sendFunctional(pkt);
    }
}

void
BunkerL2Cache::accessTiming(PacketPtr pkt)
{
    bool hit = accessFunctional(pkt);
    DPRINTF(BunkerL2Cache, "L2Cache::accssTim, Latency Complete."\
                           " Now serving request \n");
    DPRINTF(BunkerL2Cache, "L2Cache::accessTiming %s for pkt: %s \n",\
                            hit ? "Hit" : "Miss", pkt->print());

    if (hit) {
        hits++;
        DDUMP(BunkerL2Cache, pkt->getConstPtr<uint8_t>(), pkt->getSize());
        if (pkt->isWriteback() || pkt->isWrite()) {
            DPRINTF(BunkerL2Cache, "Hit was for WritebackDirty so "\
                                   "do nothing \n");
            blocked = false;
            L1CachePort.trySendRetry();

        } else {
            pkt->makeResponse();
            sendResponse(pkt);
        }
    } else {
        misses++;
        missTime = curTick();

        Addr addr = pkt->getAddr();
        Addr block_addr = pkt->getBlockAddr(blockSize);
        unsigned size = pkt->getSize();
        // if pkt is of block size and alligned.
        if (addr == block_addr && size == blockSize) {
            DPRINTF(BunkerL2Cache, "L2Cac::accTim, Miss in L2,"\
                                   "memPort.sendPacket\n");
            DPRINTF(BunkerL2Cache, "L2Cache::accTim, Pkt is alligned and blk"\
                                   "sized so not saving in outstandingpkt\n");
            DPRINTF(BunkerL2Cache, "L2Cache::accTim, Pkt type is %s  \n",\
                                    pkt->print());
            DPRINTF(BunkerL2Cache, "If this is WritebackDirty pkt, memory may"\
                                   "not respond and L2 will remain blocked\n");
            if (pkt->isWriteback()) {
                DPRINTF(BunkerL2Cache, "this Miss was for WritebackDirty pkt,"
                                       "I will call insert here\n");
                insert(pkt);
                DPRINTF(BunkerL2Cache, "and will now unblock the cache Only"\
                                       "if insert successfully happened \n");
                assert(blocked);
                blocked = false;
                L1CachePort.trySendRetry();
            } else {
                DPRINTF(BunkerL2Cache, "this Miss was for ordinary pkt, so "\
                                       " calling  memPort.sendPacket(pkt)\n");
                memPort.sendPacket(pkt);
            }
        } else {
            DPRINTF(BunkerL2Cache, "L2Cache::accTim Miss in L2,"\
                                   " Pkt addr Not alligned \n");
            // Upgrading pkt to block size
            panic_if(addr = block_addr + size > blockSize, " Cannot handle "\
                                     "access that spans multiple cache lines");
            assert(pkt->needsResponse());
            MemCmd cmd;

            if (pkt->isWrite()) {
                cmd = MemCmd::ReadReq;
            } else if (pkt->isRead()) {
                cmd = MemCmd::ReadReq;
            } else {
                panic("Unknown pkt type in upgraded size");
            }
             // Create a new block sized pkt
            PacketPtr new_pkt = new Packet(pkt->req, cmd, blockSize);
            new_pkt->allocate();

            // New pkt should now be alligned.

            assert(new_pkt->getAddr() == new_pkt->getBlockAddr(blockSize));
            outstandingPacket = pkt;
            DPRINTF(BunkerL2Cache, "L2Cache::accTim, Miss in L2, "\
                                   "memPort.sendPacket \n");
            memPort.sendPacket(new_pkt);
        }
    }
}

bool
BunkerL2Cache::accessFunctional(PacketPtr pkt)
{
    Addr block_addr = pkt->getBlockAddr(blockSize);
    if (pkt->req->hasVaddr()) {
        DPRINTF(BunkerL2Cache, "L2:accFunc Vaddr %#x \n",\
                                pkt->req->getVaddr());
    } else {
        DPRINTF(BunkerL2Cache, "L2Cache::accessFunc pkt has no Vaddr \n");
    }
    auto it = cacheStore.find(block_addr);
    if (it != cacheStore.end()) {
        if (pkt->isWrite()) {
            pkt->writeDataToBlock(it->second, blockSize);
        } else if (pkt->isRead()) {
            pkt->setDataFromBlock(it->second, blockSize);
        } else {
            panic("Unknown pkt type");
        }
        return true;
    }
    return false;
}


void
BunkerL2Cache::insert(PacketPtr pkt)
{
    DPRINTF(BunkerL2Cache, "L2Cache::insert, Call from handleResponse)\n");
    assert(pkt->getAddr() == pkt->getBlockAddr(blockSize));

    assert(cacheStore.find(pkt->getAddr()) == cacheStore.end());

    assert(pkt->isResponse() || pkt->isWriteback());

    if (cacheStore.size() >= capacity) {
        // Select random block to evict.

        int bucket, bucket_size;
        do {
            bucket = random_mt.random(0, (int) cacheStore.bucket_count() - 1);
        } while ( (bucket_size = cacheStore.bucket_size(bucket)) == 0 );
        auto block = std::next(cacheStore.begin(bucket),
                               random_mt.random(0, bucket_size - 1));
        DPRINTF(BunkerL2Cache, "L2Cache::insert Removing addr %#x\n",\
                                block->first);
        RequestPtr req = new Request(block->first, blockSize, 0, 0);
        PacketPtr new_pkt = new Packet(req, MemCmd::WritebackDirty, blockSize);
        new_pkt->dataDynamic(block->second);
        DPRINTF(BunkerL2Cache, "L2Cache::insert Writing packet back %s\n",\
                               new_pkt->print());
     /*   if (!memPort.sendTimingReq(new_pkt)) {
            DPRINTF(BunkerL2Cache, "sendTimingReq Failed for WritebackDirty,
             what should I do \n");
        //    memPort.blockedPacket = pkt;
        }*/
        DPRINTF(BunkerL2Cache, "Calling memPort.sendPacket(new_pkt)"
                               " from insert \n");
        memPort.sendPacket(new_pkt);

        cacheStore.erase(block->first);
    }
    DPRINTF(BunkerL2Cache, "L2Cache::insert: Inserting in L2 %s\n",\
                            pkt->print());
    // What is happening here? googgle DDUMP
    DDUMP(BunkerL2Cache, pkt->getConstPtr<uint8_t>(), blockSize);

    // allocate space for cache block stat

    uint8_t *data = new uint8_t[blockSize] ;

    // insert the data and address into cacheStore
    cacheStore[pkt->getAddr()] = data;
    // Write data into cache
    pkt->writeDataToBlock(data, blockSize);
}

AddrRangeList
BunkerL2Cache::getAddrRanges() const
{
    DPRINTF(BunkerL2Cache, "Sending new ranges \n");
    return memPort.getAddrRanges();

}

void
BunkerL2Cache::sendRangeChange() const
{
    L1CachePort.sendRangeChange();
}

void
BunkerL2Cache::regStats()
{
    // If you don't do this, u get errors about uninitialized stats

    MemObject::regStats();
    hits.name(name() + ".hits")
        .desc("Number of hits")
        ;

    misses.name(name() + ".misses")
        .desc("Number of misses")
        ;

    missLatency.name(name() + ".missLatency")
        .desc("Ticks for misses for the cace")
        .init(16)
        ;

    hitRatio.name(name() + ".hitRatio")
        .desc("Ratio of hits to the total access to the cache")
        ;

    hitRatio = hits / (hits + misses);
}

BunkerL2Cache*
BunkerL2CacheParams::create()
{
    return new BunkerL2Cache(this);
}









