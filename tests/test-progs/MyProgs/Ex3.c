#include <stdio.h>
#include <math.h>
#include <stdlib.h>


int main(void)
{
    int x1[100];
    int y1[100];
    int i = 0;
    int h = 3;
    int w = 2;
    int **Matrix;

    int z1[100];

    int *x40, *x42;
    
    x40 = x1 + 40;
    

    Matrix = (int **) malloc(h*sizeof(int));

    printf(" After 1st Doub Poinr alloc sizeof(int) = %d, sizeof(int*) =%d,\
             sizeof(int**) = %d \n", sizeof(int), sizeof(int*), sizeof(int**));
    printf(" Matrix = %p \n",Matrix);
    printf(" &Matrix = %p \n",&Matrix);
    printf(" Matrix + 1 = %p \n", Matrix+1);
    printf(" Matirx + 2 = %p \n", Matrix+2);
    printf(" Matirx + 3 = %p \n", Matrix+3);
    printf(" Matirx + 4 = %p \n", Matrix+4);
    printf(" Matirx + 5 = %p \n", Matrix+5);
    printf(" Matirx[0] = %p \n", Matrix[0]);
    printf(" Matirx[1] = %p \n", Matrix[1]);
    printf(" Matirx[2] = %p \n", Matrix[2]);
    printf(" Matirx[3] = %p \n", Matrix[3]);
    printf(" &Matirx[0] = %p \n", &Matrix[0]);
    printf(" &Matirx[1] = %p \n", &Matrix[1]);



    for (i=0; i < h; i++)
    {
        Matrix[i] = (int *) malloc(w * sizeof(int));
    }
    
    printf(" After Second Double Pointer allocation  \n");
    printf(" Matrix = %p \n",Matrix);
    printf(" Matrix+1 = %p \n",Matrix+1);
    printf(" Matirx[0] = %p \n", Matrix[0]);
    printf(" Matirx[1] = %p \n", Matrix[1]);
    printf(" Matirx[2] = %p \n", Matrix[2]);
    printf(" Matirx[3] = %p \n", Matrix[3]);
    printf(" Matirx[4] = %p \n", Matrix[4]);
    printf(" Matirx[5] = %p \n", Matrix[5]);
    printf(" Matirx[6] = %p \n", Matrix[6]);
    printf(" Matirx[0][0] = %p \n", Matrix[0][0]);
    printf(" Matirx[0][1] = %p \n", Matrix[0][1]);
    printf(" &Matirx[0] = %p \n", &Matrix[0]);
    printf(" &Matirx[1] = %p \n", &Matrix[1]);
    printf(" &Matirx[0][0] = %p \n", &Matrix[0][0]);
    printf(" &Matirx[0][1] = %p \n", &Matrix[0][1]);
    printf(" &Matirx[0][2] = %p \n", &Matrix[0][2]);
    printf(" &Matirx[0][3] = %p \n", &Matrix[0][3]);
    printf(" &Matirx[0][4] = %p \n", &Matrix[0][4]);
    printf(" &Matirx[0][5] = %p \n", &Matrix[0][5]);
    printf(" &Matirx[0][6] = %p \n", &Matrix[0][6]);
    printf(" &Matirx[0][7] = %p \n", &Matrix[0][7]);
    printf(" &Matirx[0][8] = %p \n", &Matrix[0][8]);
    printf(" &Matirx[0][9] = %p \n", &Matrix[0][9]);
    printf(" &Matirx[1][0] = %p \n", &Matrix[1][0]);
    printf(" &Matirx[1][1] = %p \n", &Matrix[1][1]);
    printf(" &Matirx[1][2] = %p \n", &Matrix[1][2]);
    printf(" &Matirx[1][3] = %p \n", &Matrix[1][3]);
    printf(" &Matirx[1][4] = %p \n", &Matrix[1][4]);
    printf(" &Matirx[1][5] = %p \n", &Matrix[1][5]);



/*    printf("From-App: size of int %d 40*size of int %lu\n",(int) sizeof(int), 40*sizeof(int) ); 
    printf("From-App: Value of Matrix = %p, \n \
            (Addr) Value of &Matrix = %p, \n \
            (Addr) Value of &Matrix[0] = %p, \n \
            (Addr) Value of &Matrix[0][0] = %p \n",Matrix, &Matrix, &Matrix[0], &Matrix[0][0]  ); 
  */  
/*     printf("From-App: y1 = %p, \n \
            Addr of y1[0] = %p, \n \
            Addr of y1[1] = %p, \n  \
            x1[2] = %p y1[99] = %p \n",&y1, &y1[0], &y1[1], &y1[2], &y1[99]  ); 
  */  

   // printf("From-App: Pre-Calc, Addr of x1 %p\n",(void *) x1);
   // printf("From-App: Pre-Calc, Addr of y1 %p\n",(void *) y1);
   // printf("From-App: Pre-Calc, Addr of z1 %p\n",(void *) z1);


    for(i = 0; i<=40; i++)
    {
        x1[i] = 20;
        y1[i] = 40;

    }


    for(i = 41; i<=99; i++)
    {
        x1[i] = 1120;
        y1[i] = 2341;

    }


    for(i = 0; i<=99; i++)
    {
        z1[i] = x1[i] * y1[i]; 
    }

    printf("From-App: Result: z1[0] = %d\n", z1[0]);
    printf("From-App: Result: z1[20] = %d\n", z1[20]);
    printf("From-App: Result: z1[35] = %d\n", z1[35]);
    printf("From-App: Result: z1[46] = %d\n", z1[46]);
    printf("From-App: Result: z1[59] = %d\n", z1[59]);

/*    printf("From-App: Addr of x1 %p\n",(void *) x1);
    printf("From-App: Data of x1[0] %d \n",x1[0]);
    printf("From-App: Address of x40 %p\n",x40);
    printf("From-App: Data of x40 %d\n",*x40);
    printf("From-App: Address of x1[40] %p\n",&x1[40]); 
    printf("From-App: Data of x1[40] %d \n",x1[40]);
    printf("From-App: Data of *x40  %d \n",*x40);
*/

    
    


}
