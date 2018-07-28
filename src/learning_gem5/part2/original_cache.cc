/*
 * Copyright (c) 2017 Jason Lowe-Power
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Authors: Jason Lowe-Power
 */

#include "learning_gem5/part2/original_cache.hh"

#include "base/random.hh"
#include "debug/OriginalCache.hh"
#include "sim/system.hh"

OriginalCache::OriginalCache(OriginalCacheParams *params) :
    MemObject(params),
    latency(params->latency),
    blockSize(params->system->cacheLineSize()),
    capacity(params->size / blockSize),
    memPort(params->name + ".mem_side", this),
    blocked(false), outstandingPacket(nullptr), waitingPortId(-1)
{
    // Since the CPU side ports are a vector of ports, create an instance of
    // the CPUSidePort for each connection. This member of params is
    // automatically created depending on the name of the vector port and
    // holds the number of connections to this port name
    for (int i = 0; i < params->port_cpu_side_connection_count; ++i) {
        cpuPorts.emplace_back(name() + csprintf(".cpu_side[%d]", i), i, this);
    }
}

BaseMasterPort&
OriginalCache::getMasterPort(const std::string& if_name, PortID idx)
{
    panic_if(idx != InvalidPortID, "This object doesn't support vector ports");

    // This is the name from the Python SimObject declaration in OriginalCache.py
    if (if_name == "mem_side") {
        return memPort;
    } else {
        // pass it along to our super class
        return MemObject::getMasterPort(if_name, idx);
    }
}

BaseSlavePort&
OriginalCache::getSlavePort(const std::string& if_name, PortID idx)
{
    // This is the name from the Python SimObject declaration (OriginalMemobj.py)
    if (if_name == "cpu_side" && idx < cpuPorts.size()) {
        // We should have already created all of the ports in the constructor
        return cpuPorts[idx];
    } else {
        // pass it along to our super class
        return MemObject::getSlavePort(if_name, idx);
    }
}

void
OriginalCache::CPUSidePort::sendPacket(PacketPtr pkt)
{
    // Note: This flow control is very simple since the cache is blocking.

    panic_if(blockedPacket != nullptr, "Should never try to send if blocked!");

    // If we can't send the packet across the port, store it for later.
    DPRINTF(OriginalCache, "Sending %s to CPU\n", pkt->print());
    if (!sendTimingResp(pkt)) {
        DPRINTF(OriginalCache, "failed!\n");
        blockedPacket = pkt;
    }
}

AddrRangeList
OriginalCache::CPUSidePort::getAddrRanges() const
{
    return owner->getAddrRanges();
}

void
OriginalCache::CPUSidePort::trySendRetry()
{
    if (needRetry && blockedPacket == nullptr) {
        // Only send a retry if the port is now completely free
        needRetry = false;
        DPRINTF(OriginalCache, "Sending retry req.\n");
        sendRetryReq();
    }
}

void
OriginalCache::CPUSidePort::recvFunctional(PacketPtr pkt)
{
    // Just forward to the cache.
    return owner->handleFunctional(pkt);
}

bool
OriginalCache::CPUSidePort::recvTimingReq(PacketPtr pkt)
{
//    DPRINTF(OriginalCache, "CPUSide::recvTimingReq, Got request %s\n", pkt->print());

    if (blockedPacket || needRetry) {
        // The cache may not be able to send a reply if this is blocked
        DPRINTF(OriginalCache, "Request blocked\n");
        needRetry = true;
        return false;
    }
    // Just forward to the cache.
    if (!owner->handleRequest(pkt, id)) {
        DPRINTF(OriginalCache, "CPUSide:recvTimingReq Request failed, owner->handleRequest returned false\n");
        // stalling
        needRetry = true;
        return false;
    } else {
  //      DPRINTF(OriginalCache, "CPUSide::recvTiming, Request succeeded\n");
        return true;
    }
}

void
OriginalCache::CPUSidePort::recvRespRetry()
{
    // We should have a blocked packet if this function is called.
    assert(blockedPacket != nullptr);

    // Grab the blocked packet.
    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;

    DPRINTF(OriginalCache, "Retrying response pkt %s\n", pkt->print());
    // Try to resend it. It's possible that it fails again.
    sendPacket(pkt);

    // We may now be able to accept new packets
    trySendRetry();
}

void
OriginalCache::MemSidePort::sendPacket(PacketPtr pkt)
{
    // Note: This flow control is very simple since the cache is blocking.

    panic_if(blockedPacket != nullptr, "Should never try to send if blocked!");

    // If we can't send the packet across the port, store it for later.
    if (!sendTimingReq(pkt)) {
        blockedPacket = pkt;
    }
}

bool
OriginalCache::MemSidePort::recvTimingResp(PacketPtr pkt)
{
    // Just forward to the cache.
    return owner->handleResponse(pkt);
}

void
OriginalCache::MemSidePort::recvReqRetry()
{
    // We should have a blocked packet if this function is called.
    assert(blockedPacket != nullptr);

    // Grab the blocked packet.
    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;

    // Try to resend it. It's possible that it fails again.
    sendPacket(pkt);
}

void
OriginalCache::MemSidePort::recvRangeChange()
{
    owner->sendRangeChange();
}

bool
OriginalCache::handleRequest(PacketPtr pkt, int port_id)
{
    if (blocked) {
        // There is currently an outstanding request so we can't respond. Stall
        return false;
    }
    
    DPRINTF(OriginalCache, " \n\n  Orig-Cach::hanReq. Got request from CPUSidePort:   for addr %#x \n", \
                         pkt->getAddr());
    if(pkt->req->hasVaddr())
    {
        DPRINTF(OriginalCache, " \t Orig-Cach::handReq. Vaddr = %#x \n ", pkt->req->getVaddr());
    }
    else
    {
        DPRINTF(OriginalCache, " \t Orig-Cach::handReq. Vaddr Not yet set, But WHY?\n ");
    
    }


    // This cache is now blocked waiting for the response to this packet.
    blocked = true;

    // Store the port for when we get the response
    assert(waitingPortId == -1);
    waitingPortId = port_id;

    // Schedule an event after cache access latency to actually access
    schedule(new AccessEvent(this, pkt), clockEdge(latency));

    return true;
}

bool
OriginalCache::handleResponse(PacketPtr pkt)
{
    assert(blocked);
    DPRINTF(OriginalCache, "OriginalCache::handleResponse: Got response for addr %#x\n", pkt->getAddr());

    // For now assume that inserts are off of the critical path and don't count
    // for any added latency.
    insert(pkt);

    missLatency.sample(curTick() - missTime);

    if (outstandingPacket != nullptr) {
        DPRINTF(OriginalCache, "OriginalCache::handleResponse. Copying data from new packet to old\n");
        // We had to upgrade a previous packet. We can functionally deal with
        // the cache access now. It better be a hit.
        DPRINTF(OriginalCache, "OriginalCache::handleResponse. Now calling accessFunctional from handleResponse\n");
        bool hit M5_VAR_USED = accessFunctional(outstandingPacket);
        panic_if(!hit, "Should always hit after inserting");
        outstandingPacket->makeResponse();
        delete pkt; // We may need to delay this, I'm not sure.
        pkt = outstandingPacket;
        outstandingPacket = nullptr;
    } // else, pkt contains the data it needs

    sendResponse(pkt);

    return true;
}

void OriginalCache::sendResponse(PacketPtr pkt)
{
    assert(blocked);
 //   DPRINTF(OriginalCache, "OriginalCache::sendResponse Sending resp for addr %#x\n", pkt->getAddr());

    int port = waitingPortId;

    // The packet is now done. We're about to put it in the port, no need for
    // this object to continue to stall.
    // We need to free the resource before sending the packet in case the CPU
    // tries to send another request immediately (e.g., in the same callchain).
    blocked = false;
    waitingPortId = -1;

    // Simply forward to the memory port
    cpuPorts[port].sendPacket(pkt);

    // For each of the cpu ports, if it needs to send a retry, it should do it
    // now since this memory object may be unblocked now.
    for (auto& port : cpuPorts) {
        port.trySendRetry();
    }
}

void
OriginalCache::handleFunctional(PacketPtr pkt)
{
    if (accessFunctional(pkt)) {
        pkt->makeResponse();
    } else {
        memPort.sendFunctional(pkt);
    }
}

void
OriginalCache::accessTiming(PacketPtr pkt)
{
    bool hit = accessFunctional(pkt);

    DPRINTF(OriginalCache, "OriginalCache::accessTiming %s for packet: %s\n", hit ? "Hit" : "Miss",
            pkt->print());

    if (hit) {
        // Respond to the CPU side
        hits++; // update stats
        DDUMP(OriginalCache, pkt->getConstPtr<uint8_t>(), pkt->getSize());
        pkt->makeResponse();
        sendResponse(pkt);
    } else {
        misses++; // update stats
        missTime = curTick();
        // Forward to the memory side.
        // We can't directly forward the packet unless it is exactly the size
        // of the cache line, and aligned. Check for that here.
        Addr addr = pkt->getAddr();
        Addr block_addr = pkt->getBlockAddr(blockSize);
        unsigned size = pkt->getSize();
        if (addr == block_addr && size == blockSize) {
            // Aligned and block size. We can just forward.
   //         DPRINTF(OriginalCache, "OriginalCache::accessTiming. Pkt->size == blocksize and addr == block_addr forwarding packet\n");
            memPort.sendPacket(pkt);
        } else {
     //       DPRINTF(OriginalCache, "OriginalCache::accessTiming Upgrading packet to block size\n");
           
            panic_if(addr - block_addr + size > blockSize,
                     "Cannot handle accesses that span multiple cache lines");
            // Unaligned access to one cache block
            assert(pkt->needsResponse());
            MemCmd cmd;
            if (pkt->isWrite() || pkt->isRead()) {
                // Read the data from memory to write into the block.
                // We'll write the data in the cache (i.e., a writeback cache)
                cmd = MemCmd::ReadReq;
            } else {
                panic("Unknown packet type in upgrade size");
            }

            // Create a new packet that is blockSize
            PacketPtr new_pkt = new Packet(pkt->req, cmd, blockSize);
            new_pkt->allocate();

            // Should now be block aligned
            assert(new_pkt->getAddr() == new_pkt->getBlockAddr(blockSize));

       /*     DPRINTF(OriginalCache, "\n\n accessTiming, After Upgrading Packet to Block Size \n");
            DPRINTF(OriginalCache, "\n\n accessTiming,\n\n New Packet Size %d,\n" 
                                   " New Packet Physical Address %#x\n"
                                   " New Packet Virtual Address %#x \n"
                                   " Block Address %#x\n", \
                                    new_pkt->getSize(), new_pkt->getAddr(), new_pkt->req->getVaddr(), \
                                    new_pkt->getBlockAddr(blockSize));
                                   
        */

            // Save the old packet
            outstandingPacket = pkt;

        //    DPRINTF(OriginalCache, "accessTiming forwarding packet to mem Port (in case of Miss)\n");
            memPort.sendPacket(new_pkt);
        }
    }
}

bool
OriginalCache::accessFunctional(PacketPtr pkt)
{
    Addr block_addr = pkt->getBlockAddr(blockSize);

    DPRINTF(OriginalCache," Orig-Cach::accFunc \t  pkt Addr %#x \t pkt size %u "
                          " \t blk size %d \t blk Addr %#x \n\n ", \
                          pkt->getAddr(), pkt->getSize(), blockSize, block_addr);
    


    if(pkt->req->hasVaddr())
    {
        DPRINTF(OriginalCache, " Orig-Cach::accFunc. Vaddr = %#x \n ", pkt->req->getVaddr());
    }
    else
    {
        DPRINTF(OriginalCache, " OriginalCache::accessFunctional. Vaddr Not yet set, But WHY?\n\n ");
    
    }

  


    auto it = cacheStore.find(block_addr);
    if (it != cacheStore.end()) {
        if (pkt->isWrite()) {
            // Write the data into the block in the cache
            pkt->writeDataToBlock(it->second, blockSize);
        } else if (pkt->isRead()) {
            // Read the data out of the cache block into the packet
            pkt->setDataFromBlock(it->second, blockSize);
        } else {
            panic("Unknown packet type!");
        }
        return true;
    }
    return false;
}

void
OriginalCache::insert(PacketPtr pkt)
{
    // The packet should be aligned.
    assert(pkt->getAddr() ==  pkt->getBlockAddr(blockSize));
    // The address should not be in the cache
    assert(cacheStore.find(pkt->getAddr()) == cacheStore.end());
    // The pkt should be a response
    assert(pkt->isResponse());

    if (cacheStore.size() >= capacity) {
        // Select random thing to evict. This is a little convoluted since we
        // are using a std::unordered_map. See http://bit.ly/2hrnLP2
        int bucket, bucket_size;
        do {
            bucket = random_mt.random(0, (int)cacheStore.bucket_count() - 1);
        } while ( (bucket_size = cacheStore.bucket_size(bucket)) == 0 );
        auto block = std::next(cacheStore.begin(bucket),
                               random_mt.random(0, bucket_size - 1));

        DPRINTF(OriginalCache, "OriginalCache::insert Removing addr %#x\n", block->first);

        // Write back the data.
        // Create a new request-packet pair
        RequestPtr req = new Request(block->first, blockSize, 0, 0);
        PacketPtr new_pkt = new Packet(req, MemCmd::WritebackDirty, blockSize);
        new_pkt->dataDynamic(block->second); // This will be deleted later

        DPRINTF(OriginalCache, "Orig-Cache::insert Writing packet back %s\n", new_pkt->print());
        // Send the write to memory
        memPort.sendTimingReq(new_pkt);

        // Delete this entry
        cacheStore.erase(block->first);
    }

    DPRINTF(OriginalCache, "OriginalCache::insert: Inserting %s\n",\
                            pkt->print());
    DDUMP(OriginalCache, pkt->getConstPtr<uint8_t>(), blockSize);

    // Allocate space for the cache block data
    uint8_t *data = new uint8_t[blockSize];

    // Insert the data and address into the cache store
    cacheStore[pkt->getAddr()] = data;

    // Write the data into the cache
    pkt->writeDataToBlock(data, blockSize);
}

AddrRangeList
OriginalCache::getAddrRanges() const
{
    DPRINTF(OriginalCache, "Sending new ranges\n");
    // Just use the same ranges as whatever is on the memory side.
    return memPort.getAddrRanges();
}

void
OriginalCache::sendRangeChange() const
{
    for (auto& port : cpuPorts) {
        port.sendRangeChange();
    }
}

void
OriginalCache::regStats()
{
    // If you don't do this you get errors about uninitialized stats.
    MemObject::regStats();

    hits.name(name() + ".hits")
        .desc("Number of hits")
        ;

    misses.name(name() + ".misses")
        .desc("Number of misses")
        ;

    missLatency.name(name() + ".missLatency")
        .desc("Ticks for misses to the cache")
        .init(16) // number of buckets
        ;

    hitRatio.name(name() + ".hitRatio")
        .desc("The ratio of hits to the total accesses to the cache")
        ;

    hitRatio = hits / (hits + misses);

}


OriginalCache*
OriginalCacheParams::create()
{
    return new OriginalCache(this);
}
