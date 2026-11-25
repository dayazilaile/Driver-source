#include "../native/driver_status.h"
#include <assert.h>
#include <stdio.h>
static void compare(const char *a, const char *b, int expected) {
    int valid = 0; assert(driver_version_compare(a,b,&valid)==expected && valid);
}
int main(void) {
    compare("10.0.22621.10","10.0.22621.2",1);
    compare("31.0.15.5161","31.0.15.5233",-1);
    compare("1.2","1.2.0.0",0);
    compare("1.0.65535.0","1.0.65534.99",1);
    compare("01.002.3.4","1.2.3.4",0);
    const char *bad[]={"", " ", "2026-09-29", "v1.2", "1..2", "1.2.", "1.2.3.4.5", "1.2.65536.0", "99999999999999", "1.2beta", "-1.2"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        int valid=1; driver_version_compare(bad[i],"1.2.3.4",&valid); assert(!valid);
    }
    assert(driver_advice(1,0,0,0,"1.2.3.4","")==DRIVER_NO_SOURCE);
    assert(driver_advice(1,0,0,1,"1.2.3.4","")==DRIVER_LOCAL_MATCH);
    assert(driver_advice(0,0,0,1,"","")==DRIVER_LOCAL_MATCH);
    assert(driver_advice(0,1,1,0,"","1.2.3.4")==DRIVER_CAN_INSTALL);
    assert(driver_advice(1,1,1,1,"1.2.3.4","1.2.4.0")==DRIVER_CAN_UPDATE);
    assert(driver_advice(1,1,1,1,"1.2.3.4","1.2.3.4")==DRIVER_CAN_REINSTALL);
    assert(driver_advice(1,1,1,1,"1.3.0.0","1.2.3.4")==DRIVER_KEEP_NEWER);
    assert(driver_advice(1,1,0,1,"1.2.3.4","9.0.0.0")==DRIVER_VERSION_UNCERTAIN);
    assert(driver_advice(1,1,1,1,"","1.2.3.4")==DRIVER_VERSION_UNCERTAIN);
    assert(driver_advice(1,1,1,1,"1.2.3.4","2026-09-29")==DRIVER_VERSION_UNCERTAIN);
    puts("Numeric version comparisons, source-less installed drivers, provider mismatch and downgrade advice passed");
    return 0;
}
