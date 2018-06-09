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

#include "learning_gem5/part2/bunker_cache.hh"

#include "base/random.hh"
#include "debug/BunkerCache.hh"
#include "sim/system.hh"

BunkerCache::BunkerCache(BunkerCacheParams *params) :
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
BunkerCache::getMasterPort(const std::string& if_name, PortID idx)
{
    panic_if(idx != InvalidPortID, "This object doesn't support vector ports");

    // This is the name from the Python SimObject declaration in BunkerCache.py
    if (if_name == "mem_side") {
        return memPort;
    } else {
        // pass it along to our super class
        return MemObject::getMasterPort(if_name, idx);
    }
}

BaseSlavePort&
BunkerCache::getSlavePort(const std::string& if_name, PortID idx)
{
    // This is the name from the Python SimObject declaration (BunkerMemobj.py)
    if (if_name == "cpu_side" && idx < cpuPorts.size()) {
        // We should have already created all of the ports in the constructor
        return cpuPorts[idx];
    } else {
        // pass it along to our super class
        return MemObject::getSlavePort(if_name, idx);
    }
}

void
BunkerCache::CPUSidePort::sendPacket(PacketPtr pkt)
{
    // Note: This flow control is very simple since the cache is blocking.

    panic_if(blockedPacket != nullptr, "Should never try to send if blocked!");

    // If we can't send the packet across the port, store it for later.
    DPRINTF(BunkerCache, "Sending %s to CPU\n", pkt->print());
    if (!sendTimingResp(pkt)) {
        DPRINTF(BunkerCache, "failed!\n");
        blockedPacket = pkt;
    }
}

AddrRangeList
BunkerCache::CPUSidePort::getAddrRanges() const
{
    return owner->getAddrRanges();
}

void
BunkerCache::CPUSidePort::trySendRetry()
{
    if (needRetry && blockedPacket == nullptr) {
        // Only send a retry if the port is now completely free
        needRetry = false;
        DPRINTF(BunkerCache, "Sending retry req.\n");
        sendRetryReq();
    }
}

void
BunkerCache::CPUSidePort::recvFunctional(PacketPtr pkt)
{
    // Just forward to the cache.
    return owner->handleFunctional(pkt);
}

bool
BunkerCache::CPUSidePort::recvTimingReq(PacketPtr pkt)
{
    DPRINTF(BunkerCache, "CPUSide::recvTimingReq, Got request %s\n\n", pkt->print());

    if (blockedPacket || needRetry) {
        // The cache may not be able to send a reply if this is blocked
        DPRINTF(BunkerCache, "Request blocked\n");
        needRetry = true;
        return false;
    }
    // Just forward to the cache.
    if (!owner->handleRequest(pkt, id)) {
        DPRINTF(BunkerCache, "Request failed\n");
        // stalling
        needRetry = true;
        return false;
    } else {
        DPRINTF(BunkerCache, "CPUSide::recvTiming, owner->handleRequest returned. Request succeeded\n");
        return true;
    }
}

void
BunkerCache::CPUSidePort::recvRespRetry()
{
    // We should have a blocked packet if this function is called.
    assert(blockedPacket != nullptr);

    // Grab the blocked packet.
    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;

    DPRINTF(BunkerCache, "Retrying response pkt %s\n", pkt->print());
    // Try to resend it. It's possible that it fails again.
    sendPacket(pkt);

    // We may now be able to accept new packets
    trySendRetry();
}

void
BunkerCache::MemSidePort::sendPacket(PacketPtr pkt)
{
    // Note: This flow control is very simple since the cache is blocking.

    panic_if(blockedPacket != nullptr, "Should never try to send if blocked!");

    // If we can't send the packet across the port, store it for later.
    if (!sendTimingReq(pkt)) {
        blockedPacket = pkt;
    }
}

bool
BunkerCache::MemSidePort::recvTimingResp(PacketPtr pkt)
{
    // Just forward to the cache.
    return owner->handleResponse(pkt);
}

void
BunkerCache::MemSidePort::recvReqRetry()
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
BunkerCache::MemSidePort::recvRangeChange()
{
    owner->sendRangeChange();
}

bool
BunkerCache::handleRequest(PacketPtr pkt, int port_id)
{
    if (blocked) {
        // There is currently an outstanding request so we can't respond. Stall
        return false;
    }
    
    DPRINTF(BunkerCache, " BunkerCache::handleRequest. Got request from CPUSidePort: \n\n  for addr %#x\n\n", \
                         pkt->getAddr());
    if(pkt->req->hasVaddr())
    {
        DPRINTF(BunkerCache, " BunkerCache::handleRequest. Vaddr = %#x \n ", pkt->req->getVaddr());
    }
    else
    {
        DPRINTF(BunkerCache, " BunkerCache::handleRequest. Vaddr Not yet set, But WHY?\n ");
    
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
BunkerCache::handleResponse(PacketPtr pkt)
{
    assert(blocked);
    DPRINTF(BunkerCache, "BunkerCache::handleResponse: Got response for addr %#x\n", pkt->getAddr());

    // For now assume that inserts are off of the critical path and don't count
    // for any added latency.
    if ((pkt->getAddr() >= 0xD3D40) && (pkt->getAddr() <= 0xF7DB0)) {
        DPRINTF(BunkerCache, "handleResponse, inserting in Bunker Cache Store \n");
        Bunkerinsert(pkt);
    } else {
        insert(pkt);
    }

    missLatency.sample(curTick() - missTime);

    if (outstandingPacket != nullptr) {
        DPRINTF(BunkerCache, "BunkerCache::handleResponse. Copying data from new packet to old\n");
        // We had to upgrade a previous packet. We can functionally deal with
        // the cache access now. It better be a hit.
        DPRINTF(BunkerCache, "BunkerCache::handleResponse. Now calling accessFunctional from handleResponse\n");
        if ((pkt->getAddr() >= 0xD3D40) && (pkt->getAddr() <= 0xF7DB0)) {
            bool BunkerHit M5_VAR_USED = BunkeraccessFunctional(outstandingPacket);
            panic_if(!BunkerHit, "BunkerHit, Should always hit after inserting.");
        } else {
            bool hit M5_VAR_USED = accessFunctional(outstandingPacket);
            panic_if(!hit, "Should always hit after inserting");

         }
            
        outstandingPacket->makeResponse();
        delete pkt; // We may need to delay this, I'm not sure.
        pkt = outstandingPacket;
        outstandingPacket = nullptr;
    } // else, pkt contains the data it needs

    sendResponse(pkt);

    return true;
}

void BunkerCache::sendResponse(PacketPtr pkt)
{
    assert(blocked);
//    DPRINTF(BunkerCache, "BunkerCache::sendResponse Sending resp for addr %#x\n", pkt->getAddr());

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
BunkerCache::handleFunctional(PacketPtr pkt)
{
    if (accessFunctional(pkt)) {
        pkt->makeResponse();
    } else {
        memPort.sendFunctional(pkt);
    }
}

void
BunkerCache::accessTiming(PacketPtr pkt)
{
    Addr addr = pkt->getAddr();
    Addr block_addr = pkt->getBlockAddr(blockSize);
    bool hit;
    uint8_t *PhyDir_Data = new uint8_t[1];
    PhyDir_Data[0] = 1;


    if ((block_addr >= 0xD3D40) && (block_addr <= 0xF7DB0) ) {
            
           hit = BunkeraccessFunctional(pkt);
           DPRINTF(BunkerCache, "accessTiming BunkerRange %s for packet: %s\n", hit ? "Hit" : "Miss",
            pkt->print());

          // Add the corresponding phy address in directory if its not already present.
         
         auto it = PhyDirectory.find(block_addr);
         if (it == PhyDirectory.end()) {
            DPRINTF(BunkerCache, "accessTiming, BunkerRange Directory Search, Corresponding Phy addr %#x"
                                 " Not found in Phydirectory\n", block_addr);
            PhyDirectory[block_addr] = PhyDir_Data;
         }
    } else {
           hit = accessFunctional(pkt);
    }

    DPRINTF(BunkerCache, "BunkerCache::accessTiming %s for packet: %s\n", hit ? "Hit" : "Miss",
            pkt->print());

    if (hit) {
        // Respond to the CPU side
        hits++; // update stats
        DDUMP(BunkerCache, pkt->getConstPtr<uint8_t>(), pkt->getSize());
        pkt->makeResponse();
        sendResponse(pkt);
    } else {
        misses++; // update stats
        missTime = curTick();
        // Forward to the memory side.
        // We can't directly forward the packet unless it is exactly the size
        // of the cache line, and aligned. Check for that here.
        unsigned size = pkt->getSize();
        if (addr == block_addr && size == blockSize) {
            // Aligned and block size. We can just forward.
            DPRINTF(BunkerCache, "BunkerCache::accessTiming. Pkt->size == blocksize and addr == block_addr forwarding packet\n");
            memPort.sendPacket(pkt);
        } else {
            DPRINTF(BunkerCache, "BunkerCache::accessTiming Upgrading packet to block size\n");
           
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

           /* DPRINTF(BunkerCache, "\n\n accessTiming, After Upgrading Packet to Block Size \n");
            DPRINTF(BunkerCache, "\n\n accessTiming,\n\n New Packet Size %d,\n" 
                                   " New Packet Physical Address %#x\n"
                                   " New Packet Virtual Address %#x \n"
                                   " Block Address %#x\n", \
                                    new_pkt->getSize(), new_pkt->getAddr(), new_pkt->req->getVaddr(), \
                                    new_pkt->getBlockAddr(blockSize));
                                   
           */

            // Save the old packet
            outstandingPacket = pkt;

      //      DPRINTF(BunkerCache, "accessTiming forwarding packet to mem Port (in case of Miss)\n");
            memPort.sendPacket(new_pkt);
        }
    }
}

bool
BunkerCache::accessFunctional(PacketPtr pkt)
{
    Addr block_addr = pkt->getBlockAddr(blockSize);

    DPRINTF(BunkerCache," Bunker Cache accessFunctional\n\n, pkt addr %#x \n pkt size %ud \n"
                          " block size %d \n block Address %#x \n ", \
                          pkt->getAddr(), pkt->getSize(), blockSize, block_addr);
    


    if(pkt->req->hasVaddr())
    {
        DPRINTF(BunkerCache, " BunkerCache::accessFunctional. Vaddr = %#x \n\n ", pkt->req->getVaddr());
    }
    else
    {
        DPRINTF(BunkerCache, " BunkerCache::accessFunctional. Vaddr Not yet set, But WHY?\n\n ");
    
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

bool
BunkerCache::BunkeraccessFunctional(PacketPtr pkt)
{
    int STRIDE = 3;
    float RADIX  = 3.0;
    Addr block_addr = pkt->getBlockAddr(blockSize);
    Addr bunk_addr = BunkAddressCompute(block_addr,STRIDE,RADIX);
    

    DPRINTF(BunkerCache," BunkeraccessFunctional\n\n, pkt addr %#x \n pkt size %ud \n"
                          " block size %d \n block Address %#x \n bunk Addr %#x \n ", \
                          pkt->getAddr(), pkt->getSize(), blockSize, block_addr, bunk_addr);
    

    if(pkt->req->hasVaddr())
    {
        DPRINTF(BunkerCache, " BunkeraccessFunctional. Vaddr = %#x \n\n ", pkt->req->getVaddr());
    }
    else
    {
        DPRINTF(BunkerCache, " BunkeraccessFunctional. Vaddr Not yet set, But WHY?\n\n ");
    
    }

    auto it = BunkercacheStore.find(bunk_addr);
    if (it != BunkercacheStore.end()) {
        if (pkt->isWrite()) {
            // Write the data into the block in the cache
            pkt->writeDataToBlock(it->second, blockSize);
        } else if (pkt->isRead()) {
            // Read the data out of the cache block into the packet
            pkt->setDataFromBlock(it->second, blockSize);
        } else {
            panic("BunkeraccessFunctional Unknown packet type!");
        }
        return true;
    }
    DPRINTF(BunkerCache, "BunkeraccessFunctional, pkt not found in bunker cache. returning false \n ");
    return false;
}

void
BunkerCache::insert(PacketPtr pkt)
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

        DPRINTF(BunkerCache, "BunkerCache::insert Removing addr %#x\n", block->first);

        // Write back the data.
        // Create a new request-packet pair
        RequestPtr req = new Request(block->first, blockSize, 0, 0);
        PacketPtr new_pkt = new Packet(req, MemCmd::WritebackDirty, blockSize);
        new_pkt->dataDynamic(block->second); // This will be deleted later

        DPRINTF(BunkerCache, "BunkerCache::insert Writing packet back %s\n", pkt->print());
        // Send the write to memory
        memPort.sendTimingReq(new_pkt);

        // Delete this entry
        cacheStore.erase(block->first);
    }

    DPRINTF(BunkerCache, "BunkerCache::insert: Inserting %s\n", pkt->print());
    DDUMP(BunkerCache, pkt->getConstPtr<uint8_t>(), blockSize);

    // Allocate space for the cache block data
    uint8_t *data = new uint8_t[blockSize];

    // Insert the data and address into the cache store
    cacheStore[pkt->getAddr()] = data;

    // Write the data into the cache
    pkt->writeDataToBlock(data, blockSize);
}

AddrRangeList
BunkerCache::getAddrRanges() const
{
    DPRINTF(BunkerCache, "Sending new ranges\n");
    // Just use the same ranges as whatever is on the memory side.
    return memPort.getAddrRanges();
}

void
BunkerCache::sendRangeChange() const
{
    for (auto& port : cpuPorts) {
        port.sendRangeChange();
    }
}


void
BunkerCache::Bunkerinsert(PacketPtr pkt)
{
    Addr Phy_block_addr[10];
    int STRIDE  = 3;
    float RADIX = 3.0;
    int i;
    Addr block_addr = pkt->getBlockAddr(blockSize);
    Addr bunk_addr = BunkAddressCompute(block_addr,STRIDE,RADIX);

    // The packet should be aligned.
    assert(pkt->getAddr() ==  pkt->getBlockAddr(blockSize));
    // The address should not be in the cache
    assert(BunkercacheStore.find(bunk_addr) == BunkercacheStore.end());
    // The pkt should be a response
    assert(pkt->isResponse());

    if (BunkercacheStore.size() >= capacity) {
        // Select random thing to evict. This is a little convoluted since we
        // are using a std::unordered_map. See http://bit.ly/2hrnLP2
        int bucket, bucket_size;
        do {
            bucket = random_mt.random(0, (int)BunkercacheStore.bucket_count() - 1);
        } while ( (bucket_size = BunkercacheStore.bucket_size(bucket)) == 0 );
        auto block = std::next(BunkercacheStore.begin(bucket),
                               random_mt.random(0, bucket_size - 1));

        DPRINTF(BunkerCache, "Bunkerinsert Removing Bunker addr %#x\n", block->first);

        // Identified the Bunker Address to Evict from Bunker Cache. Calculate Corresponding Physical Address for write back.

        Phy_block_addr[0] = BackwardBunkAddressCompute(block->first,STRIDE,RADIX);
        for (i = 1; i < RADIX; i++)
        {
            Phy_block_addr[i] = Phy_block_addr[0] + (i * STRIDE);
        }

        // Now Phy_block_addr has all the physical addresses corresponding to this bunk_addr.
        // Look for each Phy_block_addr in Directory

        for (i = 0; i < RADIX; i++)
        {
            auto it = PhyDirectory.find(Phy_block_addr[i]);
            if (it != cacheStore.end()) {

                // Write back the data.
                // Create a new request-packet pair
                // The first argument to Request constructor is "Physical Address"
                RequestPtr req = new Request(it->first, blockSize, 0, 0);
                PacketPtr new_pkt = new Packet(req, MemCmd::WritebackDirty, blockSize);
                // Get data of evicted block, pass it to packet. 
                // Packet Data comes from Bunker Cache (using bunk_addr), and not from PhyDirectory.
                new_pkt->dataDynamic(block->second); // This will be deleted later

                DPRINTF(BunkerCache, "BunkerCache::Bunkerinsert Sending block data to "
                                     " all corresponding physical address in Memory."
                                     " bunk address %#x, Physical Addr in Directory  = %#x,"
                                     " Writing packet back %s\n",  \
                                     bunk_addr, it->first, pkt->print());
                // Send the write to memory
                memPort.sendTimingReq(new_pkt);
                PhyDirectory.erase(it->first);

            }
            DPRINTF(BunkerCache, "The Phy_block_addr[%d] = %#x  not found in directory...Why? "
                                 "Because not all phy address (corresponding"
                                 " to this bunk address may necessarily be in directory\n", \
                                 i, Phy_block_addr[i]);
        } // end for loop for each PhyAddress. 

       
        // Delete the entry from Bunker Cache
        BunkercacheStore.erase(block->first);
    
    } // end if size() > capacity. 

    DPRINTF(BunkerCache, "BunkerCache::Bunkerinsert: Inserting %s\n", pkt->print());
    DDUMP(BunkerCache, pkt->getConstPtr<uint8_t>(), blockSize);

    // Allocate space for the cache block data
    uint8_t *data = new uint8_t[blockSize];

    // Insert the data and address into the cache store
    BunkercacheStore[bunk_addr] = data;

    // Write the data into the cache
    pkt->writeDataToBlock(data, blockSize);
}


void
BunkerCache::regStats()
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

Addr
BunkerCache::BunkAddressCompute(Addr block_addr, int STRIDE, float RADIX)
{
    int Window;
    float BunkAddress_Temp;

    Window = int (RADIX * STRIDE);

    BunkAddress_Temp = block_addr / Window;

    BunkAddress_Temp = BunkAddress_Temp * STRIDE;

    BunkAddress_Temp = BunkAddress_Temp + ((block_addr % Window) % STRIDE);

    return (Addr) BunkAddress_Temp;
}


Addr 
BunkerCache::BackwardBunkAddressCompute(Addr block_addr, int STRIDE, float RADIX)
{
    int Window;
    float PhyAddress;
    Window = (int) (RADIX * STRIDE);

    PhyAddress = block_addr / STRIDE;

    PhyAddress = PhyAddress * Window;

    PhyAddress = PhyAddress + ( (block_addr % STRIDE) % Window);
    
    return (Addr) PhyAddress;

}
BunkerCache*
BunkerCacheParams::create()
{
    return new BunkerCache(this);
}
