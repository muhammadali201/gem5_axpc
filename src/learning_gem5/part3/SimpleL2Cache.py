
from m5.params import *
from m5.proxy import *
from MemObject import MemObject


class SimpleL2Cache(MemObject):
    type = 'SimpleL2Cache'
    cxx_header = "learning_gem5/part3/simple_l2cache.hh"

    l1_side  = SlavePort("L1 Cache Side Port, receives requests")
    mem_side = MasterPort("Memory side port, sends requests")

    latency = Param.Cycles(1, "Cycles taken on a hit or to resolve a miss")
    
    size = Param.MemorySize('16kB', "the size of the cache ")

    system = Param.System(Parent.any, "The system this cache is part of ")

    radix = Param.Unsigned(1,"Radix for Bunker Cache")

    stride = Param.Unsigned(1,"Stride for Bunker Cache") 
