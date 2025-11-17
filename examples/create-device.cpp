#include "AppBase.hpp"

class CreateDeviceApp : public AppBase {
public:
  void init() { AppBase::init("Create Device"); }

  static void run() { AppBase::run<CreateDeviceApp>(); }
};

int main() {
  ren::ScratchArena::init_for_thread();
  CreateDeviceApp::run();
}
