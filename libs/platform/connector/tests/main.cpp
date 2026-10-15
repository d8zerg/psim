#include <gtest/gtest.h>

#include "psim/platform/connector/grpc_process.hpp"

int main(int argc, char** argv) {
  const psim::platform::connector::GrpcProcess grpc;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
