idf_component_register(
  SRCS "src/SHT3x.cpp"
  INCLUDE_DIRS "include"
  PRIV_REQUIRES esp_timer freertos
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
