
#include "bunker_cache/basic_l2cache.hh"

#include "base/random.hh"
#include "debug/BasicL2Cache.hh"
#include "sim/system.hh"

BasicL2Cache::BasicL2Cache(BasicL2CacheParams *params) : 
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
BasicL2Cache::getMasterPort(const std::string &if_name, PortID idx)
{
    panic_if(idx != InvalidPortID, "This object doesn't support vector ports");
    // name from the Python SimObject declaration in BasicL2Cache.py

    if (if_name == "mem_side") {
        return memPort;
    } else {
        return MemObject::getMasterPort(if_name,idx);
    }
}

BaseSlavePort&
BasicL2Cache::getSlavePort(const std::string &if_name, PortID idx)
{
    if (if_name == "l1_side") {
        return L1CachePort;
    } else {
        return MemObject::getSlavePort(if_name,idx);
    }
    
}

void
BasicL2Cache::L1SidePort::sendPacket(PacketPtr pkt)
{
    // Note: This flow control is very simple as cache is blocking

    panic_if(blockedPacket != nullptr, "Should never try to send if blocked");

    // if we can't send the pkt across port, store it for later.

    DPRINTF(BasicL2Cache, "Send %s to L1 Cahce\n", pkt->print());

    if (!sendTimingResp(pkt)) {
        DPRINTF(BasicL2Cache, "L2-->L1 sendPacket Failed\n");
        blockedPacket = pkt;
    }
}

AddrRangeList
BasicL2Cache::L1SidePort::getAddrRanges() const
{
    return owner->getAddrRanges();
}

void
BasicL2Cache::L1SidePort::trySendRetry()
{
    if (needRetry && blockedPacket == nullptr) {
        // Only send a retry if the port is not free
        needRetry = false;
        DPRINTF(BasicL2Cache, "Sending retyr req\n");
        sendRetryReq();
    }
}

void
BasicL2Cache::L1SidePort::recvFunctional(PacketPtr pkt)
{
    return owner->handleFunctional(pkt);
}

bool
BasicL2Cache::L1SidePort::recvTimingReq(PacketPtr pkt)
{
    DPRINTF(BasicL2Cache, "L1Side::recvTimingReq, Got request %s, Will now call handleRequest \n", pkt->print());
    
    if (blockedPacket || needRetry) {
        DPRINTF(BasicL2Cache," Request Blocked \n");
        needRetry = true;
        return false;
    } 
    if (!owner->handleRequest(pkt)) {
        DPRINTF(BasicL2Cache, "L1Side:recvTimingReq Request failed, owner->handleRequest returned false\n");
        needRetry = true;
        return false;
    } else {
        DPRINTF(BasicL2Cache, "L1Side::recvTimingReq, handleRequest returned true, Request Succeeded and is now scheduled \n");
        return true;
    }

}

void
BasicL2Cache::L1SidePort::recvRespRetry()
{
    // We should have a blocked packet if this function is called
    assert(blockedPacket != nullptr);

    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;
    DPRINTF(BasicL2Cache, "Retrying response pkt %s \n", pkt->print());

    sendPacket(pkt);

    trySendRetry();
}

void
BasicL2Cache::MemSidePort::sendPacket(PacketPtr pkt)
{
    panic_if(blockedPacket != nullptr, "MemSide Port Should never try to send if blocked");

    DPRINTF(BasicL2Cache,"Sending Packet to Memory \n");
    if (!sendTimingReq(pkt)) {
        DPRINTF(BasicL2Cache, "mem_Port.sendTimingReq failure, Saving in memPort.blockedPacket\n");
        blockedPacket = pkt;
    }
}


bool
BasicL2Cache::MemSidePort::chkBlockedPacket()
{
    if (blockedPacket != nullptr) {
        return false;
    } else {
        return true;
    }
}


bool
BasicL2Cache::MemSidePort::recvTimingResp(PacketPtr pkt)
{
    return owner->handleResponse(pkt);
}

void
BasicL2Cache::MemSidePort::recvReqRetry()
{
    assert(blockedPacket != nullptr);

    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;
    DPRINTF(BasicL2Cache, "recvReqRetry from MemSidePort, but why \n");
    sendPacket(pkt);
}

void
BasicL2Cache::MemSidePort::recvRangeChange()
{
    owner->sendRangeChange();
}

bool
BasicL2Cache::handleRequest(PacketPtr pkt)
{
    if (blocked) {
        DPRINTF(BasicL2Cache, "L2Cache::HandleReq, L2 Cache is blocked \n");
        return false;
    }

    DPRINTF(BasicL2Cache, "L2Cache::HandleReq, Got Request from L1: Phy Addr: %#x %s \n", pkt->getAddr(), pkt->print());
    if (pkt->req->hasVaddr()) {
        DPRINTF(BasicL2Cache, "L2Cache::HandleReq, Vaddr = %#x \n", pkt->req->getVaddr());
    } else {
        DPRINTF(BasicL2Cache, "L2Cache::HandleReq, Vaddr Not yet set, but why? \n");
    }

    blocked = true;

    DPRINTF(BasicL2Cache, "L2Cache::HandleReq, Scheduling Request after latency \n");

    schedule(new AccessEvent(this, pkt), clockEdge(latency));

    return true;

}

bool
BasicL2Cache::handleResponse(PacketPtr pkt)
{
    assert(blocked);
    DPRINTF(BasicL2Cache, "L2Cache::handleResp, Got Response from mem Addr: %#x \n", pkt->getAddr());

    // For now, assume that inserts are out of critical path and don't add latency

    insert(pkt);

    missLatency.sample(curTick() - missTime);

    if (outstandingPacket != nullptr) {
        DPRINTF(BasicL2Cache, "L2Cache::handleResp calling accessFunctional \n");
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
BasicL2Cache::sendResponse(PacketPtr pkt)
{
    assert(blocked);
    blocked = false;
    L1CachePort.sendPacket(pkt); // Forward to L1cache port

    // If L1 cache needs to send a retry, it should do it now as now L2 cache is free (unblocked)
    L1CachePort.trySendRetry();    
}

void
BasicL2Cache::handleFunctional(PacketPtr pkt)
{
    if (accessFunctional(pkt)) {
        pkt->makeResponse();
    } else {
        memPort.sendFunctional(pkt);
    }
}   

void
BasicL2Cache::accessTiming(PacketPtr pkt)
{
    bool hit = accessFunctional(pkt);
    DPRINTF(BasicL2Cache, "L2Cache::accssTiming, Latency Complete. Now serving request \n");
    DPRINTF(BasicL2Cache, "L2Cache::accessTiming %s for pkt: %s \n", hit ? "Hit" : "Miss", pkt->print());

    if (hit) {
        hits++;
        DDUMP(BasicL2Cache, pkt->getConstPtr<uint8_t>(), pkt->getSize());
        if (pkt->isWriteback() || pkt->isWrite()) {
            DPRINTF(BasicL2Cache, "Hit was for WritebackDirty so do nothing \n");
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
            DPRINTF(BasicL2Cache, "L2Cache::accTim, Miss in L2, memPort.sendPacket \n");
            DPRINTF(BasicL2Cache, "L2Cache::accTim, Pkt is alligned and block sized so not saving in outstandingpkt \n");
            DPRINTF(BasicL2Cache, "L2Cache::accTim, Pkt type is %s  \n", pkt->print());
            DPRINTF(BasicL2Cache, "If this is WritebackDirty pkt, memory may not respond and L2 will remain blocked  \n");
             
            if (pkt->isWriteback()) {
                DPRINTF(BasicL2Cache, "this Miss was for WritebackDirty pkt, I will immediately call insert here \n");
                insert(pkt);
                DPRINTF(BasicL2Cache, " and will now unblock the cache  Only if insert successfully happened \n");
                assert(blocked);
                blocked = false;
                L1CachePort.trySendRetry();
            } else {
                DPRINTF(BasicL2Cache, "this Miss was for ordinary pkt, so calling memPort.sendPacket(pkt)\n");
                memPort.sendPacket(pkt);
            }
        } else {
             
            DPRINTF(BasicL2Cache, "L2Cache::accTim Miss in L2, Pkt addr Not alligned \n");
            // Upgrading pkt to block size
            panic_if(addr = block_addr + size > blockSize, " Cannot handle access that spans multiple cache lines");
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
            DPRINTF(BasicL2Cache, "L2Cache::accTim, Miss in L2, memPort.sendPacket \n");
            memPort.sendPacket(new_pkt);
        }
    }        
}

bool
BasicL2Cache::accessFunctional(PacketPtr pkt)
{
    Addr block_addr = pkt->getBlockAddr(blockSize);

    DPRINTF(BasicL2Cache, "L2:accFunc \t pkt addr %#x, \t pkt size %u \t blk addr %#x blk size %d \n ", \
                            pkt->getAddr(), pkt->getSize(), block_addr, blockSize);
    if (pkt->req->hasVaddr()) {
        DPRINTF(BasicL2Cache, "L2:accFunc Vaddr %#x \n", pkt->req->getVaddr());
    } else {
        DPRINTF(BasicL2Cache, "L2Cache::accessFunc pkt has no Vaddr \n");
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
BasicL2Cache::insert(PacketPtr pkt)
{
    DPRINTF(BasicL2Cache, " L2Cache::insert, (Called from handleResponse) \n");
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
        
        DPRINTF(BasicL2Cache, "L2Cache::insert Removing addr %#x\n", block->first);
        RequestPtr req = new Request(block->first, blockSize, 0, 0);
        PacketPtr new_pkt = new Packet(req, MemCmd::WritebackDirty, blockSize);
        new_pkt->dataDynamic(block->second);
        DPRINTF(BasicL2Cache, "L2Cache::insert Writing packet back %s\n", new_pkt->print());
        
     /*   if (!memPort.sendTimingReq(new_pkt)) {
            DPRINTF(BasicL2Cache, "sendTimingReq Failed for WritebackDirty, what should I do \n");
        //    memPort.blockedPacket = pkt;
        }*/
        DPRINTF(BasicL2Cache, "Calling memPort.sendPacket(new_pkt) from insert \n");
        memPort.sendPacket(new_pkt);

        cacheStore.erase(block->first);
    }
    DPRINTF(BasicL2Cache, "L2Cache::insert: Inserting in L2 %s\n", pkt->print());
    DDUMP(BasicL2Cache, pkt->getConstPtr<uint8_t>(), blockSize); // what is happening here? google DDUMP

    // allocate space for cache block stat

    uint8_t *data = new uint8_t[blockSize] ;

    // insert the data and address into cacheStore
    cacheStore[pkt->getAddr()] = data;
    
    // Write data into cache
    pkt->writeDataToBlock(data, blockSize);
}

AddrRangeList
BasicL2Cache::getAddrRanges() const
{
    DPRINTF(BasicL2Cache, "Sending new ranges \n");
    return memPort.getAddrRanges();

}

void
BasicL2Cache::sendRangeChange() const
{
    L1CachePort.sendRangeChange();
}

void
BasicL2Cache::regStats()
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

BasicL2Cache*
BasicL2CacheParams::create()
{
    return new BasicL2Cache(this);
}









