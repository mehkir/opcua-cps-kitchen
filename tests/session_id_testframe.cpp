#include <iostream>
// uncomment to disable assert()
// #define NDEBUG
#include <cassert>
#include "session_id.hpp"
 
// Use (void) to silence unused warnings.
#define assertm(exp, msg) assert((void(msg), exp))

int main(int argc, char* argv[]) {
    session_id s1(1,3);
    session_id s2(3,1);
    assertm(s1 == s1, "Failed");
    assertm(s2 == s2, "Failed");
    assertm(s1 <= s2, "Failed");
    assertm(s1 < s2, "Failed");
    assertm(s2 > s1, "Failed");
    assertm(s2 >= s1, "Failed");
    assertm(s2 >= s2, "Failed");
    assertm(s1 >= s1, "Failed");
    return 0;
}