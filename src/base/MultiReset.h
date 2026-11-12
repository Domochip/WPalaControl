#ifndef MultiReset_h
#define MultiReset_h

#include <Arduino.h>

// Requests Rescue Mode when the reset button is pressed several times in a row
class MultiReset
{
public:
    static bool check();
    static void run();

private:
    static bool _pending;
};

#endif
