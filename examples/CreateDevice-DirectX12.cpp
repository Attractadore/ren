#include "AppBase-DirectX12.hpp"

class CreateDeviceApp : public AppBase<CreateDeviceApp> {
public:
  CreateDeviceApp() : AppBase("Create Device (DirectX 12)") {}
};

int main() { CreateDeviceApp().run(); }
