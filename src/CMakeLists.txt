add_subdirectory(application)

if (WIN32 AND DV_SELF_CONTAINED)
  add_subdirectory(launcher)
endif()
