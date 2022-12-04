#include "AppBase-DirectX12.hpp"

class CreateDeviceApp : public AppBase<CreateDeviceApp> {
public:
  CreateDeviceApp() : AppBase("Create Device (DirectX 12)") {}
};

int main() {
  try {
    CreateDeviceApp().run();
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return -1;
  }
}
