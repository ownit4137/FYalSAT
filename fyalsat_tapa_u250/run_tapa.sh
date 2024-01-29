#! /bin/bash

WORK_DIR=run
mkdir -p "${WORK_DIR}"

platform=xilinx_u250_gen3x16_xdma_4_1_202210_1
TOP=fysat

tapac \
  --work-dir "${WORK_DIR}" \
  --top ${TOP} \
  --platform $platform \
  --clock-period 3 \
  -o "${WORK_DIR}/${TOP}.xo" \
  --floorplan-output "${WORK_DIR}/${TOP}_floorplan.tcl" \
  --min-area-limit 0.9 \
  --max-area-limit 0.85 \
  --connectivity link_config4.ini \
  --read-only-args ClauseList_0 \
  --read-only-args VarsOccList_0 \
  --read-only-args ol_len_off_0 \
  --write-only-args Result_0 \
  fysat.cpp

