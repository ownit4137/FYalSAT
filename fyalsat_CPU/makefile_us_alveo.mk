############################## Setting up Project Variables ##############################
TARGET := hw
VPP_LDFLAGS :=
include ./utils.mk

TEMP_DIR := ./_x.$(TARGET).$(XSA)
BUILD_DIR := ./build_dir.$(TARGET).$(XSA)

LINK_OUTPUT := $(BUILD_DIR)/fysat.link.xclbin
PACKAGE_OUT = ./package.$(TARGET)

VPP_PFLAGS := 
CMD_ARGS = $(BUILD_DIR)/fysat.xclbin
CXXFLAGS += -I$(XILINX_XRT)/include -I$(XILINX_VIVADO)/include -Wall -O0 -g -std=c++1y
LDFLAGS += -L$(XILINX_XRT)/lib -pthread -lOpenCL

########################## Checking if PLATFORM in allowlist #######################
PLATFORM_BLOCKLIST += nodma 
############################## Setting up Host Variables ##############################
#Include Required Host Source Files
CXXFLAGS += -I$(XF_PROJ_ROOT)/common/includes/xcl2 -I$(XF_PROJ_ROOT)/common/include
HOST_SRCS += $(XF_PROJ_ROOT)/common/includes/xcl2/xcl2.cpp ./src/host.cpp 
# Host compiler global settings
CXXFLAGS += -fmessage-length=0
LDFLAGS += -lrt -lstdc++ 

############################## Setting up Kernel Variables ##############################
# Kernel compiler global settings
VPP_FLAGS += --save-temps

EXECUTABLE = ./fyalsat.exe
EMCONFIG_DIR = $(TEMP_DIR)


# CMD_ARGS = $(BUILD_DIR)/fysat.xclbin k7-r90-v60-c5267.cnf 0 100000000 0 0
# CMD_ARGS = $(BUILD_DIR)/fysat.xclbin k3-r4.26-v600-c2556-043.cnf 13480327 10000000 16 16
# CMD_ARGS = $(BUILD_DIR)/fysat.xclbin p12-k7-001.cnf 0 10000000 16 640
# CMD_ARGS = $(BUILD_DIR)/fysat.xclbin C3-2-31.cnf 0 5000000 16 15000
# CMD_ARGS = $(BUILD_DIR)/fysat.xclbin olsq2_cnf_small/9_6_2.txt 0 10000000 0 0 # 1690171019 962
# CMD_ARGS = $(BUILD_DIR)/fysat.xclbin n_queens32.dimacs 1689498293 1000 32 640
# CMD_ARGS = $(BUILD_DIR)/fysat.xclbin si2-b03m-m800.cnf 0 10 0 0
# CMD_ARGS = $(BUILD_DIR)/fysat.xclbin ../../../../sat_sw/d_2022/MVD_ADS_S10_5_6.cnf 0 10 0 0
# CMD_ARGS = $(BUILD_DIR)/fysat.xclbin ../../../../sat_sw/d_2022/MVD_ADS_S10_5_6.cnf 1691165424 10 0 0
CMD_ARGS = $(BUILD_DIR)/fysat.xclbin ../../../../sat_sw/gen/test.cnf 0 1000 0 0


VPP_FLAGS += --connectivity.sp fysat_1.ol_len_off:HBM[1]
VPP_FLAGS += --connectivity.sp fysat_1.cls_len_off:HBM[0]
VPP_FLAGS += --connectivity.sp fysat_1.flipcnt:HBM[2]
VPP_FLAGS += --connectivity.sp fysat_1.ClauseList:DDR[0]
VPP_FLAGS += --connectivity.sp fysat_1.VarsOccList:DDR[1]
# VPP_FLAGS += --connectivity.sp fysat_1.ol_len_off:DDR[0]
# VPP_FLAGS += --connectivity.sp fysat_1.cls_len_off:DDR[1]


############################## Setting Targets ##############################
.PHONY: all clean cleanall docs emconfig
all: check-platform check-device check-vitis $(EXECUTABLE) $(BUILD_DIR)/fysat.xclbin emconfig

.PHONY: host
host: $(EXECUTABLE)

.PHONY: build
build: check-vitis check-device $(BUILD_DIR)/fysat.xclbin

.PHONY: xclbin
xclbin: build

############################## Setting Rules for Binary Containers (Building Kernels) ##############################
$(TEMP_DIR)/fysat.xo: src/fysat.cpp
	mkdir -p $(TEMP_DIR)
	v++ -c $(VPP_FLAGS) -t $(TARGET) --platform $(PLATFORM) -k fysat --temp_dir $(TEMP_DIR)  -I'$(<D)' -o'$@' '$<'

$(BUILD_DIR)/fysat.xclbin: $(TEMP_DIR)/fysat.xo
	mkdir -p $(BUILD_DIR)
	v++ -l $(VPP_FLAGS) $(VPP_LDFLAGS) -t $(TARGET) --platform $(PLATFORM) --temp_dir $(TEMP_DIR) -o'$(LINK_OUTPUT)' $(+)
	v++ -p $(LINK_OUTPUT) $(VPP_FLAGS) -t $(TARGET) --platform $(PLATFORM) --package.out_dir $(PACKAGE_OUT) -o $(BUILD_DIR)/fysat.xclbin

############################## Setting Rules for Host (Building Host Executable) ############################## -fPIC -mcmodel=large 
$(EXECUTABLE): $(HOST_SRCS) | check-xrt
		g++ -mcmodel=large -o $@ $^ $(CXXFLAGS) $(LDFLAGS)

emconfig:$(EMCONFIG_DIR)/emconfig.json
$(EMCONFIG_DIR)/emconfig.json:
	emconfigutil --platform $(PLATFORM) --od $(EMCONFIG_DIR)

############################## Setting Essential Checks and Running Rules ##############################
run: all
ifeq ($(TARGET),$(filter $(TARGET),sw_emu hw_emu))
	cp -rf $(EMCONFIG_DIR)/emconfig.json .
	XCL_EMULATION_MODE=$(TARGET) $(EXECUTABLE) $(CMD_ARGS)
else
	$(EXECUTABLE) $(CMD_ARGS)
endif

.PHONY: test
test: $(EXECUTABLE)
ifeq ($(TARGET),$(filter $(TARGET),sw_emu hw_emu))
	XCL_EMULATION_MODE=$(TARGET) $(EXECUTABLE) $(CMD_ARGS)
else
	$(EXECUTABLE) $(CMD_ARGS)
endif

############################## Cleaning Rules ##############################
# Cleaning stuff
clean:
	-$(RMDIR) $(EXECUTABLE) $(XCLBIN)/{*sw_emu*,*hw_emu*} 
	-$(RMDIR) profile_* TempConfig system_estimate.xtxt *.rpt *.csv 
	-$(RMDIR) src/*.ll *v++* .Xil emconfig.json dltmp* xmltmp* *.log *.jou *.wcfg *.wdb

cleanall: clean
	-$(RMDIR) build_dir*
	-$(RMDIR) package.*
	-$(RMDIR) _x* *xclbin.run_summary qemu-memory-_* emulation _vimage pl* start_simulation.sh *.xclbin .run .ipcache

