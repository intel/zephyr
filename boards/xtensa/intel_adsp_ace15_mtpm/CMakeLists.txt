set(RIMAGE ${BOARD_DIR}/support/rimage)
if(CONFIG_BOARD_INTEL_ADSP_ACE15_MTPM_SIM)
add_custom_target(
  gen_signed_image ALL
  COMMAND
  ${RIMAGE}
  -k ${BOARD_DIR}/support/dummy-key.pem
  -c ${BOARD_DIR}/support/ace10.toml
  -i 3
  ${PROJECT_BINARY_DIR}/boot.mod
  ${PROJECT_BINARY_DIR}/main.mod
  -o ${PROJECT_BINARY_DIR}/zephyr.ri
  DEPENDS gen_modules
)
endif()
