
#include <gtest/gtest.h>

// TODO: n timeouts
// TODO: 2 timeouts with infinite attempts
// TODO: cancel with onError and infinite attempts
// TODO: call connect twice while pending
// TODO: call connect when already connected

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}