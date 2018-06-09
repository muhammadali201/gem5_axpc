
#include <cstdio>
#include <random>
#include "m5ops.h"

  int main()
  {
    const int N = 2500;
    double X[N], Y[N], alpha = 0.5;
    

    for (int i = 0; i < N; ++i)
    {
      X[i] = 4;
      Y[i] = 8;
    }

    // Start of daxpy loop
     m5_dump_reset_stats(0,0);
    for (int i = 0; i < N; ++i)
    {
      Y[i] = alpha * X[i] + Y[i];
    }
    m5_dump_reset_stats(0,0);
  // End of daxpy loop

    double sum = 0;
    for (int i = 0; i < N; ++i)
    {
      sum += Y[i];
    }
    printf("%lf\n", sum);
    return 0; 
  }
