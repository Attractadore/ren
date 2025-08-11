#include "AppBase.hpp"

class CreateDeviceApp : public AppBase {
public:
  auto init() -> Result<void> { return AppBase::init("Create Device"); }

  [[nodiscard]] static auto run() -> int {
    return AppBase::run<CreateDeviceApp>();
  }
};

int main() { return CreateDeviceApp::run(); }
