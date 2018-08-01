/*
*   Authors: Muhammad Ali Akhtar
*/

#ifndef __BUNKER_CACHE_BUNKER_L2CACHE_HH__
#define __BUNKER_CACHE_BUNKER_L2CACHE_HH__

#include <unordered_map>

#include "mem/mem_object.hh"
#include "params/BunkerL2Cache.hh"

/**
* A very simple cache object. Has a fully-associative data store
* with random replacements.
* Fully Blocking. Only a single request can be outstanding at a time
* This cache is a "writeBack Cache" what is this?
* Adding some extra comments
*/

class BunkerL2Cache : public MemObject
{
    private:

        /*
            Port on the L1 Side (Connnects to L2Bus).
            Receives requests from L1 Cache (Data + Instructions)
            The port forwards requests to the owner (L2Cache)
        */

        class L1SidePort : public SlavePort
        {
            private:

                BunkerL2Cache *owner;
                bool needRetry;
                PacketPtr blockedPacket;
                /* If we tried to send the packet and it was blocked,
                *  store it here
                */
            public:

                L1SidePort(const std::string &name, BunkerL2Cache *owner) :
                    SlavePort(name, owner), owner(owner), needRetry(false),
                    blockedPacket(nullptr)
                {

                }

                /*
                * Send pkt across this port. Called by owner.
                */

                void sendPacket(PacketPtr pkt);
                               /*
                * Get the list of non-overlapping address ranges the
                * owner is responsible for.
                * All Slave ports must override this function and return
                * a populated
                * list with at least one item
                */

                AddrRangeList getAddrRanges() const override;

                /*
                * Send retry to peer port if needed.
                */

                void trySendRetry();

            protected:
                /*
                * Receive an atomic request pkt from peer Master port
                *  (L1 Cache). Not needed here
                */

                Tick recvAtomic(PacketPtr pkt) override
                { panic("recvAtomic unimplemented"); }


                /*
                * Receive a functional rqt pkt from Master.
                * Performs debug access, updating / reading data in place.
                */

                void recvFunctional(PacketPtr pkt) override;

                /*
                * Receive a timing request from Master. Return weather
                *  this object
                * can consume a pkt.
                * if return = false, we will call sendRetry() when we
                * can try to
                * receive this again
                */
                bool recvTimingReq(PacketPtr pkt) override;

                /*
                * Called by the master port if sendTimingResp was
                * called on this slave port (causingng recvTimingResp
                * to be called on the
                * master port) and
                * was unsuccessful.
                */
                void recvRespRetry() override;

        };

        /*
        * Port on the Memory side that receives responses.
        */

        class MemSidePort : public MasterPort
        {
            private:
                BunkerL2Cache *owner;
                PacketPtr blockedPacket;
            public:
                /* Constructor */
                MemSidePort(const std::string &name, BunkerL2Cache *owner) :
                    MasterPort(name,owner),
                    owner(owner),
                    blockedPacket(nullptr)
                {

                }

                /* Send pkt across this prot */

                void sendPacket(PacketPtr pkt);
                bool chkBlockedPacket();


            protected:

                /*
                * Receive Timing Response from Slave port
                */

                bool recvTimingResp(PacketPtr pkt) override;

                void recvReqRetry() override;

                void recvRangeChange() override;
        };

        /*
        *   Handle request from L1 Cache. Called from Slave port (L1 Side)
        *   on timing re
        */

        bool handleRequest(PacketPtr pkt);

        /*
        *   Handle response from Memory side. Called from Memory (Master) port
        */

        bool handleResponse(PacketPtr pkt);

        /*
        *   Send Pkt to L1 Cache)
        */

        void sendResponse(PacketPtr pkt);

        /*
        *  Handle pkt functionally. update on write and get the data on read.
        *  called from Slave port (l1cache)
        */

        void handleFunctional(PacketPtr pkt);

        /*
        *   Timing Access
        */
        void accessTiming(PacketPtr pkt);

        /*
        * this is where we actually update / read flash. Executed on both
        * timing and functional access
        */

        bool accessFunctional(PacketPtr pkt);

        /*
        * insert block into cache. if there is not room left in cache, evict
        * the random entry to make room
        */

        void insert(PacketPtr pkt);

        /*
        * Return the address ranges cache is responsible for. Just use the
        * same as the next upper
        * level hierarchy.
        */
        AddrRangeList getAddrRanges() const;

        /*
        * Tell L1 cache / CPU to ask for our memory ranges
        */
        void sendRangeChange() const;

        const Cycles latency;

        const unsigned blockSize;

        const unsigned capacity;
        const float  radix;
        const unsigned stride;

        L1SidePort L1CachePort;

        MemSidePort memPort;

        bool blocked;
        PacketPtr outstandingPacket;

        Tick missTime;

        /*
        *   An incrediblly simple cache storage. Maps block addresses to data
        */
        std::unordered_map<Addr, uint8_t*> cacheStore;

        class AccessEvent : public Event
        {
            private:
                BunkerL2Cache *cache;

                PacketPtr pkt;
            public:
                AccessEvent(BunkerL2Cache *cache, PacketPtr pkt) :
                    Event(Default_Pri, AutoDelete),
                    cache(cache),
                    pkt(pkt)
                {

                }

                void process() override {
                    cache->accessTiming(pkt);
                }

        };

        friend class AccessEvent;

        // Statistics
        Stats::Scalar hits;
        Stats::Scalar misses;
        Stats::Histogram missLatency;
        Stats::Formula hitRatio;

        public:

            /*
            * COnstructor
            */

            BunkerL2Cache(BunkerL2CacheParams *params);

            virtual BaseMasterPort&
            getMasterPort(const std::string &if_name, PortID idx) override;

            virtual BaseSlavePort&
            getSlavePort(const std::string &if_name, PortID idx) override;

            void regStats() override;

};

#endif
