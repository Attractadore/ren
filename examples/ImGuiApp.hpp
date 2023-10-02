#pragma once
#include "AppBase.hpp"

#include <imgui.h>

class ImGuiApp : public AppBase {
public:
  ImGuiApp(const char *name);

protected:
  [[nodiscard]] auto process_event(const SDL_Event &e) -> Result<void> override;

  [[nodiscard]] auto iterate(unsigned, unsigned, std::chrono::nanoseconds)
      -> Result<void> override;

private:
  struct Deleter {
    static void operator()(ImGuiContext *context) noexcept;
  };

  std::unique_ptr<ImGuiContext, Deleter> m_imgui_context;
};
