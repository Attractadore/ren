target_sources(
  ren
  PRIVATE AutomaticExposure.cpp
          CameraExposure.cpp
          Exposure.cpp
          ManualExposure.cpp
          Opaque.cpp
          PostProcessing.cpp
          Upload.cpp)
target_include_directories(ren PRIVATE ${CMAKE_CURRENT_LIST_DIR}/..)
