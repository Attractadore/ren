#include "app-base.hpp"

#include <fmt/format.h>

class CreateDeviceApp : public AppBase {
public:
  CreateDeviceApp() : AppBase("Create Device") {}

  [[nodiscard]] static auto run() -> int {
    return AppBase::run<CreateDeviceApp>();
  }
};

int main() { return CreateDeviceApp::run(); }
