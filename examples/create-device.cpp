#include "app-base.hpp"

#include <fmt/format.h>

class CreateDeviceApp : public AppBase {
public:
  CreateDeviceApp() : AppBase("Create Device") {}
};

int main() {
  try {
    CreateDeviceApp().run();
  } catch (const std::exception &e) {
    fmt::print(stderr, "{}\n", e.what());
    return -1;
  }
}
