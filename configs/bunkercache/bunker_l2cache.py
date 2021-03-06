# Authors: Muhammad Ali Akhtar.
# 14-06-2018 - 5:16 AM 29th Ramazan. Removed all the Jason's introductory ...
# default comments

import optparse
import sys
import os

# import the m5 (gem5) library created when gem5 is built
import m5
# import all of the SimObjects
from m5.objects import *

# create the system we are going to simulate
system = System()

# Set the clock fequency of the system (and all of its children)
system.clk_domain = SrcClockDomain()
system.clk_domain.clock = '1GHz'
system.clk_domain.voltage_domain = VoltageDomain()

# Set up the system
system.mem_mode = 'timing'               # Use timing accesses
system.mem_ranges = [AddrRange('1024MB')] # Create an address range

# Create a simple CPU
system.cpu = TimingSimpleCPU()

# Create a memory bus, a coherent crossbar, in this case
system.membus = SystemXBar()

# Create a simple cache
system.cache = BasicL1Cache(size='128kB')

# Connect the I and D cache ports of the CPU to the memobj.
# Since cpu_side is a vector port, each time one of these is connected, it will
# create a new instance of the CPUSidePort class
system.cpu.icache_port = system.cache.cpu_side
system.cpu.dcache_port = system.cache.cpu_side


# L2 Cache

system.l2bus = L2XBar()
# Hook the cache up to the memory bus
system.cache.mem_side = system.l2bus.slave

system.l2cache = BunkerL2Cache(size='1024kB')
system.l2cache.l1_side = system.l2bus.master
system.l2cache.mem_side = system.membus.slave
system.l2cache.latency = 3
# create the interrupt controller for the CPU and connect to the membus
system.cpu.createInterruptController()
system.cpu.interrupts[0].pio = system.membus.master
system.cpu.interrupts[0].int_master = system.membus.slave
system.cpu.interrupts[0].int_slave = system.membus.master

# Create a DDR3 memory controller and connect it to the membus
system.mem_ctrl = DDR3_1600_8x8()
system.mem_ctrl.range = system.mem_ranges[0]
system.mem_ctrl.port = system.membus.master

# Connect the system up to the membus
system.system_port = system.membus.slave

# Create a process for a simple "Hello World" application
process = Process()
# Set the command
# cmd is a list which begins with the executable (like argv)
process.cmd = ['tests/test-progs/MyProgs/kmean_2',\
               '../../axbench/applications/kmeans/test.data/input/4.rgb',\
               '4_out.rgb']
#process.cmd = ['tests/test-progs/MyProgs/Ex3.o']
# Set the cpu to use the process as its workload and create thread contexts
system.cpu.workload = process
system.cpu.createThreads()

# set up the root SimObject and start the simulation
root = Root(full_system = False, system = system)
# instantiate all of the objects we've created above
m5.instantiate()

print "Beginning simulation!"
exit_event = m5.simulate()
print 'Exiting @ tick %i because %s' % (m5.curTick(), exit_event.getCause())
