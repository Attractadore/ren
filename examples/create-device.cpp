#include "app-base.hpp"

class CreateDeviceApp : public AppBase {
public:
  CreateDeviceApp() : AppBase("Create Device") {}

  [[nodiscard]] static auto run() -> int {
    return AppBase::run<CreateDeviceApp>();
  }
};

int main() { return CreateDeviceApp::run(); }
