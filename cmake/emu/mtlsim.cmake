# SPDX-License-Identifier: Apache-2.0

find_program(
  MTLSIM
  mtlsim
  )
set(MTLSIM ${BOARD_DIR}/support/mtlsim.py)

set(MTLSIM_FLAGS
	--rom ${BOARD_DIR}/support/dsp_rom_mtl_sim.hex
	--sim ${BOARD_DIR}/support/dsp_fw_sim
	--rimage ${APPLICATION_BINARY_DIR}/zephyr/zephyr.ri
  )

add_custom_target(run_mtlsim
  COMMAND
  ${MTLSIM}
  ${MTLSIM_FLAGS}
  WORKING_DIRECTORY ${APPLICATION_BINARY_DIR}
  DEPENDS gen_signed_image
  USES_TERMINAL
  )
