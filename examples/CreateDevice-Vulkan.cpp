#include "AppBase.hpp"

class CreateDeviceApp : public AppBase<CreateDeviceApp> {
public:
  CreateDeviceApp() : AppBase("Create Device (Vulkan)") {}
};

int main() { CreateDeviceApp().run(); }
