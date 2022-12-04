#include "AppBase.hpp"

class CreateDeviceApp : public AppBase<CreateDeviceApp> {
public:
  CreateDeviceApp() : AppBase("Create Device (Vulkan)") {}
};

int main() {
  try {
    CreateDeviceApp().run();
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return -1;
  }
}
