#include <nes/nes.hpp>

namespace NES {

#include "serialization.cpp"
PPU ppu;

void PPU::Main() {
  ppu.main();
}

void PPU::main() {
  while(true) {
    if(scheduler.sync == Scheduler::SynchronizeMode::PPU) {
      scheduler.exit(Scheduler::ExitReason::SynchronizeEvent);
    }

    raster_scanline();
  }
}

void PPU::tick() {
  clock += 4;
  if(clock >= 0) co_switch(cpu.thread);
}

void PPU::scanline_edge() {
  if(status.ly == 241) {
    status.nmi = 1;
    if(status.nmi_enable) cpu.set_nmi_line(1);
  }
  if(status.ly == 261) {
    status.nmi = 0;
    cpu.set_nmi_line(0);
    status.sprite_zero_hit = 0;
  }
}

void PPU::frame_edge() {
  status.field ^= 1;
  interface->videoRefresh(buffer);
  scheduler.exit(Scheduler::ExitReason::FrameEvent);
}

void PPU::power() {
  reset();
}

void PPU::reset() {
  create(PPU::Main, 21477272);

  status.mdr = 0x00;
  status.field = 0;
  status.ly = 0;
  status.bus_data = 0x00;
  status.address_latch = 0;

  status.vaddr = 0x0000;
  status.taddr = 0x0000;
  status.xaddr = 0x00;

  //$2000
  status.nmi_enable = false;
  status.master_select = 0;
  status.sprite_size = 0;
  status.bg_addr = 0x0000;
  status.sprite_addr = 0x0000;
  status.vram_increment = 1;

  //$2001
  status.emphasis = 0;
  status.sprite_enable = false;
  status.bg_enable = false;
  status.sprite_edge_enable = false;
  status.bg_edge_enable = false;
  status.grayscale = false;

  //$2002
  status.nmi = false;
  status.sprite_zero_hit = false;
  status.sprite_overflow = false;

  //$2003
  status.oam_addr = 0x00;

  memset(buffer, 0, sizeof buffer);

  memset(ciram, 0, sizeof ciram);
  memset(cgram, 0, sizeof cgram);
  memset(oam, 0, sizeof oam);
}

uint8 PPU::read(uint16 addr) {
  uint8 result = 0x00;

  switch(addr & 7) {
  case 2:  //PPUSTATUS
    result |= status.nmi << 7;
    result |= status.sprite_zero_hit << 6;
    result |= status.sprite_overflow << 5;
    result |= status.mdr & 0x1f;
    status.nmi = 0;
    cpu.set_nmi_line(0);
    status.address_latch = 0;
    break;
  case 4:  //OAMDATA
    result = oam[status.oam_addr];
    if((status.oam_addr & 3) == 3) result &= 0xe3;
    break;
  case 7:  //PPUDATA
    addr = status.vaddr & 0x3fff;
    if(addr <= 0x1fff) {
      result = status.bus_data;
      status.bus_data = cartridge.chr_read(addr);
    } else if(addr <= 0x3eff) {
      result = status.bus_data;
      status.bus_data = cartridge.ciram_read(addr);
    } else if(addr <= 0x3fff) {
      result = cgram_read(addr);
      status.bus_data = cartridge.ciram_read(addr);
    }
    status.vaddr += status.vram_increment;
    break;
  }

  return result;
}

void PPU::write(uint16 addr, uint8 data) {
  status.mdr = data;

  switch(addr & 7) {
  case 0:  //PPUCTRL
    status.nmi_enable = data & 0x80;
    cpu.set_nmi_line(status.nmi_enable && status.nmi);
    status.master_select = data & 0x40;
    status.sprite_size = data & 0x20;
    status.bg_addr = (data & 0x10) ? 0x1000 : 0x0000;
    status.sprite_addr = (data & 0x08) ? 0x1000 : 0x0000;
    status.vram_increment = (data & 0x04) ? 32 : 1;
    status.taddr = (status.taddr & 0x73ff) | ((data & 0x03) << 10);
    return;
  case 1:  //PPUMASK
    status.emphasis = data >> 5;
    status.sprite_enable = data & 0x10;
    status.bg_enable = data & 0x08;
    status.sprite_edge_enable = data & 0x04;
    status.bg_edge_enable = data & 0x02;
    status.grayscale = data & 0x01;
    return;
  case 2:  //PPUSTATUS
    return;
  case 3:  //OAMADDR
    status.oam_addr = data;
    return;
  case 4:  //OAMDATA
    oam[status.oam_addr++] = data;
    return;
  case 5:  //PPUSCROLL
    if(status.address_latch == 0) {
      status.xaddr = data & 0x07;
      status.taddr = (status.taddr & 0x7fe0) | (data >> 3);
    } else {
      status.taddr = (status.taddr & 0x0c1f) | ((data & 0x07) << 12) | ((data >> 3) << 5);
    }
    status.address_latch ^= 1;
    return;
  case 6:  //PPUADDR
    if(status.address_latch == 0) {
      status.taddr = (status.taddr & 0x00ff) | ((data & 0x3f) << 8);
    } else {
      status.taddr = (status.taddr & 0x7f00) | data;
      status.vaddr = status.taddr;
    }
    status.address_latch ^= 1;
    return;
  case 7:  //PPUDATA
    addr = status.vaddr & 0x3fff;
    if(addr <= 0x1fff) {
      cartridge.chr_write(addr, data);
    } else if(addr <= 0x3eff) {
      cartridge.ciram_write(addr, data);
    } else if(addr <= 0x3fff) {
      cgram_write(addr, data);
    }
    status.vaddr += status.vram_increment;
    return;
  }
}

uint8 PPU::ciram_read(uint16 addr) {
  return ciram[addr & 0x07ff];
}

void PPU::ciram_write(uint16 addr, uint8 data) {
  ciram[addr & 0x07ff] = data;
}

uint8 PPU::cgram_read(uint16 addr) {
  if((addr & 0x13) == 0x10) addr &= ~0x10;
  uint8 data = cgram[addr & 0x1f];
  if(status.grayscale) data &= 0x30;
  return data;
}

void PPU::cgram_write(uint16 addr, uint8 data) {
  if((addr & 0x13) == 0x10) addr &= ~0x10;
  cgram[addr & 0x1f] = data;
}

//

//vaddr = 0yyy VHYY  YYYX XXXX
//yyy = fine Yscroll (y:d0-d2)
//V = V nametable (y:d8)
//H = H nametable (x:d8)
//YYYYY = Y nametable (y:d3-d7)
//XXXXX = X nametable (x:d3-d7)

bool PPU::raster_enable() const {
  return (status.bg_enable || status.sprite_enable);
}

unsigned PPU::nametable_addr() const {
  return 0x2000 + (status.vaddr & 0x0c00);
}

unsigned PPU::scrollx() const {
  return ((status.vaddr & 0x1f) << 3) | status.xaddr;
}

unsigned PPU::scrolly() const {
  return (((status.vaddr >> 5) & 0x1f) << 3) | ((status.vaddr >> 12) & 7);
}

unsigned PPU::sprite_height() const {
  return status.sprite_size == 0 ? 8 : 16;
}

//

uint8 PPU::chr_load(uint16 addr) {
  if(raster_enable() == false) return 0x00;
  return cartridge.chr_read(addr);
}

uint8 PPU::ciram_load(uint16 addr) {
  if(raster_enable() == false) return 0x00;
  return cartridge.ciram_read(addr);
}

//

void PPU::ly_increment() {
  if(++status.ly == 262) {
    status.ly = 0;
    frame_edge();
  }
  scanline_edge();
}

void PPU::scrollx_increment() {
  if(raster_enable() == false) return;
  status.vaddr = (status.vaddr & 0x7fe0) | ((status.vaddr + 0x0001) & 0x001f);
  if((status.vaddr & 0x001f) == 0x0000) {
    status.vaddr ^= 0x0400;
  }
}

void PPU::scrolly_increment() {
  if(raster_enable() == false) return;
  status.vaddr = (status.vaddr & 0x0fff) | ((status.vaddr + 0x1000) & 0x7000);
  if((status.vaddr & 0x7000) == 0x0000) {
    status.vaddr = (status.vaddr & 0x7c1f) | ((status.vaddr + 0x0020) & 0x03e0);
    if((status.vaddr & 0x03e0) == 0x03c0) {  //0x03c0 == 30 << 5; 30 * 8 = 240
      status.vaddr &= 0x7c1f;
      status.vaddr ^= 0x0800;
    }
  }
}

//

void PPU::raster_pixel(unsigned x) {
  uint16 *output = buffer + status.ly * 256;

  unsigned mask = 0x8000 >> (status.xaddr + x);
  unsigned palette = 0, object_palette = 0;
  bool object_priority = 0;
  palette |= (raster.tiledatalo & mask) ? 1 : 0;
  palette |= (raster.tiledatahi & mask) ? 2 : 0;
  if(palette) {
    unsigned attr = raster.attribute;
    if(mask >= 256) attr >>= 2;
    palette |= (attr & 3) << 2;
  }

  if(status.bg_edge_enable == false && status.lx < 8) palette = 0;

  for(signed sprite = 7; sprite >= 0; sprite--) {
    if(status.sprite_edge_enable == false && status.lx < 8) continue;
    if(raster.oam[sprite].id == 64) continue;

    unsigned spritex = status.lx - raster.oam[sprite].x;
    if(spritex >= 8) continue;

    if(raster.oam[sprite].attr & 0x40) spritex ^= 7;
    unsigned mask = 0x80 >> spritex;
    unsigned sprite_palette = 0;
    sprite_palette |= (raster.oam[sprite].tiledatalo & mask) ? 1 : 0;
    sprite_palette |= (raster.oam[sprite].tiledatahi & mask) ? 2 : 0;
    if(sprite_palette == 0) continue;

    if(raster.oam[sprite].id == 0 && palette) status.sprite_zero_hit = 1;
    sprite_palette |= (raster.oam[sprite].attr & 3) << 2;

    object_priority = raster.oam[sprite].attr & 0x20;
    object_palette = 16 + sprite_palette;
  }

  if(object_palette) {
    if(palette == 0 || object_priority == 0) palette = object_palette;
  }

  if(raster_enable() == false) palette = 0;
  output[status.lx++] = (status.emphasis << 6) | cgram_read(palette);
}

void PPU::raster_sprite() {
  if(status.sprite_enable == false) return;

  unsigned n = raster.oam_iterator++;
  signed ly = (status.ly == 261 ? -1 : status.ly);
  unsigned y = ly - oam[(n * 4) + 0];

  if(y >= sprite_height()) return;
  if(raster.oam_counter == 8) {
    status.sprite_overflow = 1;
    return;
  }

  raster.soam[raster.oam_counter].id = n;
  raster.soam[raster.oam_counter].y = y;
  raster.soam[raster.oam_counter].tile = oam[(n * 4) + 1];
  raster.soam[raster.oam_counter].attr = oam[(n * 4) + 2];
  raster.soam[raster.oam_counter].x = oam[(n * 4) + 3];
  raster.oam_counter++;
}

void PPU::raster_scanline() {
  if((status.ly >= 240 && status.ly <= 260)) {
    for(unsigned x = 0; x < 340; x++) tick();
    if(raster_enable() == false || status.field != 1 || status.ly != 240) tick();
    return ly_increment();
  }

  signed lx = 0, ly = (status.ly == 261 ? -1 : status.ly);
  status.lx = 0;

  raster.oam_iterator = 0;
  raster.oam_counter = 0;

  for(unsigned n = 0; n < 8; n++) {
    raster.soam[n].id = 64;
    raster.soam[n].tiledatalo = 0;
    raster.soam[n].tiledatahi = 0;
  }

  for(unsigned tile = 0; tile < 32; tile++) {  //  0-255
    unsigned nametable = ciram_load(status.vaddr & 0x0fff);
    unsigned tileaddr = status.bg_addr + (nametable << 4) + (scrolly() & 7);
    raster_pixel(0);
    tick();

    raster_pixel(1);
    tick();

    unsigned attribute = ciram_load(0x03c0 | (status.vaddr & 0x0fc0) | ((scrolly() >> 5) << 3) | (scrollx() >> 5));
    if(scrolly() & 16) attribute >>= 4;
    if(scrollx() & 16) attribute >>= 2;
    raster_pixel(2);
    tick();

    scrollx_increment();
    if(tile == 31) scrolly_increment();
    raster_pixel(3);
    raster_sprite();
    tick();

    unsigned tiledatalo = chr_load(tileaddr + 0);
    raster_pixel(4);
    tick();

    raster_pixel(5);
    tick();

    unsigned tiledatahi = chr_load(tileaddr + 8);
    raster_pixel(6);
    tick();

    raster_pixel(7);
    raster_sprite();
    tick();

    raster.nametable = (raster.nametable << 8) | nametable;
    raster.attribute = (raster.attribute << 2) | (attribute & 3);
    raster.tiledatalo = (raster.tiledatalo << 8) | tiledatalo;
    raster.tiledatahi = (raster.tiledatahi << 8) | tiledatahi;
  }

  for(unsigned n = 0; n < 8; n++) raster.oam[n] = raster.soam[n];

  for(unsigned sprite = 0; sprite < 8; sprite++) {  //256-319
    unsigned nametable = ciram_load(status.vaddr & 0x0fff);
    tick();

    if(raster_enable() && sprite == 0) status.vaddr = (status.vaddr & 0x7be0) | (status.taddr & 0x041f);  //257
    tick();

    unsigned attribute = ciram_load(0x03c0 | (status.vaddr & 0x0fc0) | ((scrolly() >> 5) << 3) | (scrollx() >> 5));
    unsigned tileaddr = (sprite_height() == 8)
    ? status.sprite_addr + raster.oam[sprite].tile * 16
    : ((raster.oam[sprite].tile & ~1) * 16) + ((raster.oam[sprite].tile & 1) * 0x1000);
    tick();
    tick();

    unsigned spritey = raster.oam[sprite].y;
    if(raster.oam[sprite].attr & 0x80) spritey ^= (sprite_height() - 1);
    tileaddr += spritey + (spritey & 8);
    if(raster.oam[sprite].id == 64) tileaddr = status.sprite_addr;

    raster.oam[sprite].tiledatalo = chr_load(tileaddr + 0);
    tick();
    tick();

    raster.oam[sprite].tiledatahi = chr_load(tileaddr + 8);
    tick();
    tick();

    if(raster_enable() && sprite == 6 && status.ly == 261) status.vaddr = status.taddr;  //304
  }

  for(unsigned tile = 0; tile < 2; tile++) {  //320-335
    unsigned nametable = ciram_load(status.vaddr & 0x0fff);
    unsigned tileaddr = status.bg_addr + (nametable << 4) + (scrolly() & 7);
    tick();
    tick();

    unsigned attribute = ciram_load(0x03c0 | (status.vaddr & 0x0fc0) | ((scrolly() >> 5) << 3) | (scrollx() >> 5));
    if(scrolly() & 16) attribute >>= 4;
    if(scrollx() & 16) attribute >>= 2;
    tick();

    scrollx_increment();
    tick();

    unsigned tiledatalo = chr_load(tileaddr + 0);
    tick();
    tick();

    unsigned tiledatahi = chr_load(tileaddr + 8);
    tick();
    tick();

    raster.nametable = (raster.nametable << 8) | nametable;
    raster.attribute = (raster.attribute << 2) | (attribute & 3);
    raster.tiledatalo = (raster.tiledatalo << 8) | tiledatalo;
    raster.tiledatahi = (raster.tiledatahi << 8) | tiledatahi;
  }

  //336-339
  unsigned nametable = ciram_load(status.vaddr & 0x0fff);
  tick();
  tick();

  unsigned attribute = ciram_load(0x03c0 | (status.vaddr & 0x0fc0) | ((scrolly() >> 5) << 3) | (scrollx() >> 5));
  tick();
  tick();

  //340
  tick();

  return ly_increment();
}

#if 0

//scanline-based PPU renderer

void PPU::raster_scanline() {
  if(raster_enable() == false || (status.ly >= 240 && status.ly <= 260)) {
    return tick(341);
  }

  uint32 *line = buffer + status.ly * 256;

  uint8 ioam[8][5];
  unsigned oamc = 0;

  unsigned sprite_height = status.sprite_size ? 16 : 8;
  for(unsigned n = 0; n < 64; n++) {
    unsigned y = oam[(n * 4) + 0] + 1;
    if(status.ly < y || status.ly >= y + sprite_height) continue;
    ioam[oamc][0] = y;
    ioam[oamc][1] = oam[(n * 4) + 1];
    ioam[oamc][2] = oam[(n * 4) + 2];
    ioam[oamc][3] = oam[(n * 4) + 3];
    ioam[oamc][4] = n;
    if(++oamc >= 8) break;
  }

  for(unsigned x = 0; x < 256; x++) {
    uint8 tile = cartridge.chr_read(0x2000 | (status.vaddr & 0x1fff));
    uint8 attr = cartridge.chr_read(0x23c0 | (status.vaddr & 0x0fc0) | ((scrolly() >> 5) << 3) | (scrollx() >> 5));

    if(scrollx() & 16) attr >>= 2;
    if(scrolly() & 16) attr >>= 4;

    unsigned tilex = (scrollx() + x) & 7;

    uint8 tdlo = cartridge.chr_read(status.bg_addr + (tile << 4) + 0 + (scrolly() & 7));
    uint8 tdhi = cartridge.chr_read(status.bg_addr + (tile << 4) + 8 + (scrolly() & 7));

    if(tilex == 7) scrollx_increment();

    unsigned mask = 0x80 >> tilex;
    unsigned palette = 0;
    if(status.bg_enable) {
      if(status.bg_edge_enable == true || x >= 8) {
        palette |= (tdlo & mask) ? 1 : 0;
        palette |= (tdhi & mask) ? 2 : 0;
        if(palette) palette |= (attr & 3) << 2;
      }
    }

    for(unsigned n = 0; n < oamc; n++) {
      if(x < ioam[n][3] || x >= ioam[n][3] + 8) continue;
      if(status.sprite_enable == false) continue;
      if(status.sprite_edge_enable == false && x < 8) continue;

      unsigned spritex = x - ioam[n][3];
      unsigned spritey = status.ly - ioam[n][0];

      unsigned addr = (sprite_height == 8)
      ? status.sprite_addr + ioam[n][1] * 16
      : ((ioam[n][1] & ~1) * 16) + ((ioam[n][1] & 1) * 0x1000);

      if(ioam[n][2] & 0x80) spritey ^= (sprite_height - 1);
      if(ioam[n][2] & 0x40) spritex ^= 7;
      if(spritey & 8) spritey += 8;

      tdlo = cartridge.chr_read(addr + 0 + spritey);
      tdhi = cartridge.chr_read(addr + 8 + spritey);

      mask = 0x80 >> spritex;
      unsigned sprite_palette = 0;
      sprite_palette |= (tdlo & mask) ? 1 : 0;
      sprite_palette |= (tdhi & mask) ? 2 : 0;
      if(sprite_palette == 0) continue;

      if(ioam[n][4] == 0) status.sprite_zero_hit = 1;
      sprite_palette |= (ioam[n][2] & 3) << 2;

      bool priority = ioam[n][2] & 0x20;
      if(priority == 0 || palette == 0) palette = 16 + sprite_palette;
      break;
    }

    *line++ = paletteRGB[cgram[palette]];
  }

  if(status.ly != 261) scrolly_increment();

  tick(257);  //257

  if(raster_enable()) {
    status.vaddr = (status.vaddr & 0x7be0) | (status.taddr & 0x041f);
  }

  tick(47);  //304

  if(status.ly == 261) status.vaddr = status.taddr;

  tick(37);  //341
}

#endif

}
