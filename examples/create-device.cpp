#include "app-base.hpp"

class CreateDeviceApp : public AppBase {
public:
  CreateDeviceApp() : AppBase("Create Device") {}
};

int main() {
  try {
    CreateDeviceApp().run();
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return -1;
  }
}
