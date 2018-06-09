from m5.params import *
from m5.proxy import *
from MemObject import MemObject


class OriginalCache(MemObject): 
    type = 'OriginalCache'
    cxx_header = "learning_gem5/part2/original_cache.hh"

    cpu_side = VectorSlavePort("CPU side port, receives requests")
    mem_side = MasterPort("Memory side port, sends requests")

    latency = Param.Cycles(1, "Cycles taken on a hit or to resolve a miss")

    size = Param.MemorySize('16kB', "The size of the cache")

    system = Param.System(Parent.any, "The system this cache is part of")
