#include <stdio.h>
#include <math.h>

int ForwardBunker(int addr, int STRIDE, float RADIX);


int BackwardBunker(int addr, int STRIDE, float RADIX);

int main(void)
{
    int i;
    int RADIX = 2;

    int z1[100];

    int PhyAddress;
    int BunkerAddress;
    int PhyBackward[10];
    int STRIDE = 3;
    
    PhyAddress = 0x240;    
    BunkerAddress = ForwardBunker(PhyAddress, STRIDE,2);
    PhyBackward[0] = BackwardBunker(BunkerAddress, STRIDE,2);
    PhyBackward[1] = PhyBackward[0] + STRIDE;
        
    for (i = 0; i < RADIX; i++)
    {
        printf("Physical Address %#x, BunkerAddress %#x, BackwardAddress %#x,  \n", PhyAddress, BunkerAddress, PhyBackward[i]);
    }

    PhyAddress = 0x241;    
    BunkerAddress = ForwardBunker(PhyAddress, STRIDE,2);
    PhyBackward[0] = BackwardBunker(BunkerAddress, STRIDE,2);
    PhyBackward[1] = PhyBackward[0] + STRIDE;
        
    for (i = 0; i < RADIX; i++)
    {
        printf("Physical Address %#x, BunkerAddress %#x, BackwardAddress %#x,  \n", PhyAddress, BunkerAddress, PhyBackward[i]);
    }

    PhyAddress = 0x242;    
    BunkerAddress = ForwardBunker(PhyAddress, STRIDE,2);
    PhyBackward[0] = BackwardBunker(BunkerAddress, STRIDE,2);
    PhyBackward[1] = PhyBackward[0] + STRIDE;
        
    for (i = 0; i < RADIX; i++)
    {
        printf("Physical Address %#x, BunkerAddress %#x, BackwardAddress %#x,  \n", PhyAddress, BunkerAddress, PhyBackward[i]);
    }

    PhyAddress = 0x243;    
    BunkerAddress = ForwardBunker(PhyAddress, STRIDE,2);
    PhyBackward[0] = BackwardBunker(BunkerAddress, STRIDE,2);
    PhyBackward[1] = PhyBackward[0] + STRIDE;
        
    for (i = 0; i < RADIX; i++)
    {
        printf("Physical Address %#x, BunkerAddress %#x, BackwardAddress %#x,  \n", PhyAddress, BunkerAddress, PhyBackward[i]);
    }

    /*
    PhyAddress = 0x241;    
    BunkerAddress = ForwardBunker(PhyAddress, 3,2);
    PhyBackwardAddr = BackwardBunker(PhyBackward, BunkerAddress, 3,2);    
    for (i = 0; i < RADIX; i++)
    {
    //    PhyBackward[i] = *PhyBackwardAddr;
        printf("Physical Address %#x, BunkerAddress %#x, Backward %#x,  \n", PhyAddress, BunkerAddress, PhyBackward[i]);
     //   PhyBackwardAddr++;
    }

    PhyAddress = 0x242;    
    BunkerAddress = ForwardBunker(PhyAddress, 3,2);
    BackwardBunker(PhyBackward, BunkerAddress, 3,2);    
    for (i = 0; i < RADIX; i++)
    {
    //    PhyBackward[i] = *PhyBackwardAddr;
        printf("Physical Address %#x, BunkerAddress %#x, Backward %#x,  \n", PhyAddress, BunkerAddress, PhyBackward[i]);
     //   PhyBackwardAddr++;
    }

    PhyAddress = 0x243;
    BunkerAddress = ForwardBunker(PhyAddress, 3,2);
    BackwardBunker(PhyBackward, BunkerAddress, 3,2);    
    for (i = 0; i < RADIX; i++)
    {
    //    PhyBackward[i] = *PhyBackwardAddr;
        printf("Physical Address %#x, BunkerAddress %#x, Backward %#x,  \n", PhyAddress, BunkerAddress, PhyBackward[i]);
     //   PhyBackwardAddr++;
    }

    PhyAddress = 0x244;
    BunkerAddress = ForwardBunker(PhyAddress, 3,2);
    BackwardBunker(PhyBackward, BunkerAddress, 3,2);    
    for (i = 0; i < RADIX; i++)
    {
    //    PhyBackward[i] = *PhyBackwardAddr;
        printf("Physical Address %#x, BunkerAddress %#x, Backward %#x,  \n", PhyAddress, BunkerAddress, PhyBackward[i]);
     //   PhyBackwardAddr++;
    }*/



}


int ForwardBunker(int addr, int STRIDE, float RADIX)
{
    int Window;
    int BunkAddress;

    Window = (int) (RADIX * STRIDE);
    BunkAddress = addr / Window;
    BunkAddress = BunkAddress * STRIDE;
    BunkAddress = BunkAddress + ( (addr % Window) % STRIDE);

    return BunkAddress;    
    
}

int BackwardBunker(int addr, int STRIDE, float RADIX)
{
    int Window,PhyAddress;
    
    Window = (int) (RADIX * STRIDE);

    PhyAddress = addr / STRIDE;

    PhyAddress = PhyAddress * Window;

    PhyAddress = PhyAddress + ( (addr % STRIDE) % Window);
    
    return PhyAddress;

}
