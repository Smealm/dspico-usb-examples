#include "Activity.h"

volatile u64 gHostReadBytes = 0;
volatile u64 gHostWriteBytes = 0;

void ActivityReset()
{
    gHostReadBytes = 0;
    gHostWriteBytes = 0;
}
