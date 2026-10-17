#include "x11/wm.h"

int main(int argc, char** argv)
{
    ttr::Wm wm;
    if (!wm.init(&argc, argv))
        return 1;
    return wm.run();
}
