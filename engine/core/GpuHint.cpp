#if defined(_WIN32)
  #if defined(_MSC_VER)
    #define EXPORT_VAR __declspec(dllexport)
  #else
    #define EXPORT_VAR __attribute__((dllexport))
  #endif

extern "C" {
    // NVIDIA Optimus hint
    EXPORT_VAR unsigned long NvOptimusEnablement = 0x00000001;
    // AMD switchable graphics hint
    EXPORT_VAR int AmdPowerXpressRequestHighPerformance = 1;
}
#endif