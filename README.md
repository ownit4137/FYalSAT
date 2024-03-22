# FYalSAT

## TAPA Flow

1. Prerequisites

- Alveo U250: https://www.xilinx.com/products/boards-and-kits/alveo/u250.html
- Vitis 2022.2: https://www.xilinx.com/products/design-tools/vitis.html
- TAPA: https://github.com/UCLA-VAST/tapa

2. Host compilation

```
g++ -o fysat -O2 fysat.cpp host.cpp -ltapa -lfrt -lglog -lgflags
```

3. Run TAPA

```
source run_tapa.sh
```

4. Generate bitstream

```
bash run/fysat_generate_bitstream.sh
```

5. Run tests

```
./fysat --bitstream=vitis_run_hw/fysat_xilinx_u250_gen3x16_xdma_4_1_202210_1.xclbin ../test/uf225-028.cnf 0 100000 10.0
./fysat --bitstream=vitis_run_hw/fysat_xilinx_u250_gen3x16_xdma_4_1_202210_1.xclbin ../test/p12-k7-001.cnf 0 100000 10.0
./fysat --bitstream=vitis_run_hw/fysat_xilinx_u250_gen3x16_xdma_4_1_202210_1.xclbin ../test/C3-2-31.cnf 0 100000 10.0
./fysat --bitstream=vitis_run_hw/fysat_xilinx_u250_gen3x16_xdma_4_1_202210_1.xclbin ../test/Gen_16k.cnf 0 100000 10.0
./fysat --bitstream=vitis_run_hw/fysat_xilinx_u250_gen3x16_xdma_4_1_202210_1.xclbin ../test/MVD_ADS_S6_6_5.cnf 0 100000 10.0
./fysat --bitstream=vitis_run_hw/fysat_xilinx_u250_gen3x16_xdma_4_1_202210_1.xclbin ../test/MVD_ADS_S10_5_6.cnf 0 100000 10.0
```



## Vitis Flow (not up-to-date)


1. Setup the environment

source <Vitis_install_path>/Vitis/2021.2/settings64.sh; 
source /opt/xilinx/xrt/setup.sh;

2. Build for hardware emulation

make run TARGET=hw_emu PLATFORM=xilinx_u280_xdma_201920_3 all

3. Build for the hardware

make TARGET=hw PLATFORM=xilinx_u280_xdma_201920_3 all

3-1. Run application
- put SAT instances in 'test' folder
$ <exe file> <XCLBIN file> <fileName> <seed> <maxFlips> <k> <r>

<exe file>: executable cile in './fyalsat_FPGA/'
<XCLBIN file>: .xclbin file in './fyalsat_FPGA/build_dir.hw.<platform>/'
<maxFlips>: The number of flips
<k>: The maximum number of variables allowed in a single clause
<r>: The maximum number of clauses where a variable can appear

ex) $ fsat.exe fsat.xclbin k3-r4.26-v600-c2556-043.cnf 0 10000000 16 640


