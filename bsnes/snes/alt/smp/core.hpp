void tick();
alwaysinline void op_io();
debugvirtual alwaysinline uint8 op_read(uint16 addr);
debugvirtual alwaysinline void op_write(uint16 addr, uint8 data);
debugvirtual alwaysinline void op_step();
static const unsigned cycle_count_table[256];
uint64 cycle_table_cpu[256];
unsigned cycle_table_dsp[256];
uint64 cycle_step_cpu;

uint8  op_adc (uint8  x, uint8  y);
uint16 op_addw(uint16 x, uint16 y);
uint8  op_and (uint8  x, uint8  y);
uint8  op_cmp (uint8  x, uint8  y);
uint16 op_cmpw(uint16 x, uint16 y);
uint8  op_eor (uint8  x, uint8  y);
uint8  op_inc (uint8  x);
uint8  op_dec (uint8  x);
uint8  op_or  (uint8  x, uint8  y);
uint8  op_sbc (uint8  x, uint8  y);
uint16 op_subw(uint16 x, uint16 y);
uint8  op_asl (uint8  x);
uint8  op_lsr (uint8  x);
uint8  op_rol (uint8  x);
uint8  op_ror (uint8  x);
