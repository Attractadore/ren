#include "Editor.hpp"

int main(int argc, const char *argv[]) {
  ren::ScratchArena::init_for_thread();
  ren::launch_job_server();

  ren::EditorContext ctx;
  ren::init_editor(argc, argv, &ctx);
  ren::run_editor(&ctx);
  ren::quit_editor(&ctx);

  ren::stop_job_server();
}
