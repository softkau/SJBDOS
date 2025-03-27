#include "ide.hpp"
#include "asmfunc.h"

// command / status port
#define 	ATA_SR_BSY		0x80
#define 	ATA_SR_DRDY		0x40
#define 	ATA_SR_DF		  0x20
#define 	ATA_SR_DSC		0x10
#define 	ATA_SR_DRQ		0x08
#define 	ATA_SR_CORR		0x04
#define 	ATA_SR_IDX		0x02
#define 	ATA_SR_ERR		0x01

// features / error port
#define 	ATA_ER_BBK		0x80
#define 	ATA_ER_UNC		0x40
#define 	ATA_ER_MC		  0x20
#define 	ATA_ER_IDNF		0x10
#define 	ATA_ER_MCR		0x08
#define 	ATA_ER_ABRT		0x04
#define 	ATA_ER_TK0NF	0x02
#define 	ATA_ER_AMNF		0x01

// ATA-Commands
#define		ATA_CMD_READ_PIO			0x20
#define		ATA_CMD_READ_PIO_EXT		0x24
#define		ATA_CMD_READ_DMA			0xC8
#define		ATA_CMD_READ_DMA_EXT		0x25
#define		ATA_CMD_WRITE_PIO			0x30
#define		ATA_CMD_WRITE_PIO_EXT		0x34
#define		ATA_CMD_WRITE_DMA			0xCA
#define		ATA_CMD_WRITE_DMA_EXT		0x35
#define		ATA_CMD_CACHE_FLUSH		0xE7
#define		ATA_CMD_CACHE_FLUSH_EXT	0xEA
#define		ATA_CMD_PACKET				0xA0
#define 	ATA_CMD_IDENTIFY_PACKET		0xA1
#define 	ATA_CMD_IDENTIFY			0xEC

// ATAPI-only commands
#define		ATAPI_CMD_READ		0xA8
#define		ATAPI_CMD_EJECT		0x1B

#define 	ATA_IDENT_DEVICETYPE	0
#define 	ATA_IDENT_CYLINDERS	2
#define 	ATA_IDENT_HEADS		6
#define 	ATA_IDENT_SECTORS   	12
#define 	ATA_IDENT_SERIAL	20
#define 	ATA_IDENT_MODEL		54
#define 	ATA_IDENT_CAPABILITIES	98
#define 	ATA_IDENT_FIELDVALID	106
#define 	ATA_IDENT_MAX_LBA	120
#define	ATA_IDENT_COMMANDSETS   164
#define 	ATA_IDENT_MAX_LBA_EXT	200

#define		ATA_MASTER		0x00
#define		ATA_SLAVE		0x01

#define		IDE_ATA			0x00
#define		IDE_ATAPI		0x01

// ATA-ATAPI Task-File
#define		ATA_REG_DATA		0x00
#define		ATA_REG_ERROR		0x01
#define		ATA_REG_FEATURES	0x01
#define		ATA_REG_SECCOUNT0	0x02
#define		ATA_REG_LBA0		0x03
#define		ATA_REG_LBA1		0x04
#define		ATA_REG_LBA2		0x05
#define		ATA_REG_HDDEVSEL	0x06
#define		ATA_REG_COMMAND		0x07
#define		ATA_REG_STATUS		0x07
#define		ATA_REG_SECCOUNT1	0x08
#define		ATA_REG_LBA3		0x09
#define		ATA_REG_LBA4		0x0A
#define		ATA_REG_LBA5		0x0B
#define		ATA_REG_CONTROL		0x0C
#define		ATA_REG_ALTSTATUS	0x0C
#define		ATA_REG_DEVADDRESS	0x0D

struct IdeChannel {
  uint16_t base;
  uint16_t ctrl;
  uint16_t bus_master_ide;
  uint8_t nIEN;
} channels[2];

uint8_t ide_buf[2048];
uint8_t ide_irq_invoked = 0;
uint8_t atapi_packet[12] = { 0xA8, };

struct IdeDevice {
  uint8_t reserved; // 0: empty, 1: drive exist
  uint8_t channel; // 0: primary, 1: secondary
  uint8_t drive; // 0: master drive, 1: slave drive
  uint16_t type; // 0: ATA, 1: ATAPI
  uint16_t signature;
  uint16_t capabilities;
  uint32_t commandsets; // supported command sets
  uint32_t size; // size in sectors
  uint8_t model[41];
} __attribute__((packed)) ide_devices[4];

void writeChannel(uint8_t ch, uint8_t reg, uint8_t data) {
  if (reg > 0x07 && reg < 0x0C)
    writeChannel(ch, ATA_REG_CONTROL, 0x80 | channels[ch].nIEN);
  
  if (reg < 0x08)
    IoOut8(channels[ch].base + reg - 0x00, data);
  else if (reg < 0x0C)
    IoOut8(channels[ch].base + reg - 0x06, data);
  else if (reg < 0x0E)
    IoOut8(channels[ch].ctrl + reg - 0x0A, data);
  else if (reg < 0x16)
    IoOut8(channels[ch].bus_master_ide + reg - 0x0E, data);

  if (reg > 0x07 && reg < 0x0C)
    writeChannel(ch, ATA_REG_CONTROL, channels[ch].nIEN);
}

uint8_t readChannel(unsigned ch, unsigned reg) {
  if (reg > 0x07 && reg < 0x0C)
    writeChannel(ch, ATA_REG_CONTROL, 0x80 | channels[ch].nIEN);

  uint8_t res = 0;
  if (reg < 0x08) res = IoIn8(channels[ch].base + reg - 0x00);
  else if (reg < 0x0C) res = IoIn8(channels[ch].base + reg - 0x06);
  else if (reg < 0x0E) res = IoIn8(channels[ch].ctrl + reg - 0x0A);
  else if (reg < 0x16) res = IoIn8(channels[ch].bus_master_ide + reg - 0x0E);

  if (reg > 0x07 && reg < 0x0C)
    writeChannel(ch, ATA_REG_CONTROL, channels[ch].nIEN);

  return res;
}

void ins(uint16_t port, uint8_t* dest, uint32_t count) {
  __asm__("rep insl" : "+D" (dest), "+c" (count), "=m" (*dest) : "d" (port) : "memory");
}

void readBuffer(uint8_t ch, uint8_t reg, uint8_t* buf, uint32_t quads) {
  if (reg > 0x07 && reg < 0x0C)
    writeChannel(ch, ATA_REG_CONTROL, 0x80 | channels[ch].nIEN);

  if (reg < 0x08) ins(channels[ch].base + reg - 0x00, buf, quads);
  else if (reg < 0x0C) ins(channels[ch].base + reg - 0x06, buf, quads);
  else if (reg < 0x0E) ins(channels[ch].ctrl + reg - 0x0A, buf, quads);
  else if (reg < 0x16) ins(channels[ch].bus_master_ide + reg - 0x0E, buf, quads);

  if (reg > 0x07 && reg < 0x0C)
    writeChannel(ch, ATA_REG_CONTROL, channels[ch].nIEN);
}

uint8_t doIdePolling(uint8_t ch, uint32_t advanced_check) {
  readChannel(ch, ATA_REG_ALTSTATUS);
  readChannel(ch, ATA_REG_ALTSTATUS);
  readChannel(ch, ATA_REG_ALTSTATUS);
  readChannel(ch, ATA_REG_ALTSTATUS);

  while (readChannel(ch, ATA_REG_STATUS) & ATA_SR_BSY);

  if (advanced_check) {
    uint8_t state = readChannel(ch, ATA_REG_STATUS);
    if (state & ATA_SR_ERR) return 2;
    if (state & ATA_SR_DF) return 1;
    if (!(state & ATA_SR_DRQ)) return 3;
  }
  return 0;
}

extern "C" int printk(const char* format, ...);

uint8_t printIdeError(uint32_t drive, uint8_t err) {
  if (err == 0) return 0;
  printk(" IDE:");
  if (err == 1) {
    printk("- Device Fault\n    ");
    err = 19;
  } else if (err == 2) {
    uint8_t st = readChannel(ide_devices[drive].channel, ATA_REG_ERROR);
    if (st & ATA_ER_AMNF) {
      printk("- No Address Mark Found\n     ");
      err = 7;
    }
    if (st & ATA_ER_TK0NF)	{printk("- No Media or Media Error\n     ");	err = 3;}
		if (st & ATA_ER_ABRT)	{printk("- Command Aborted\n     ");		err = 20;}
		if (st & ATA_ER_MCR)	{printk("- No Media or Media Error\n     ");	err = 3;}
		if (st & ATA_ER_IDNF)	{printk("- ID mark not Found\n     ");		err = 21;}
		if (st & ATA_ER_MC)	{printk("- No Media or Media Error\n     ");	err = 3;}
		if (st & ATA_ER_UNC)	{printk("- Uncorrectable Data Error\n     ");	err = 22;}
		if (st & ATA_ER_BBK)	{printk("- Bad Sectors\n     "); 		err = 13;}
  } else  if (err == 3)           {printk("- Reads Nothing\n     "); err = 23;}
	  else  if (err == 4)  {printk("- Write Protected\n     "); err = 8;}
	printk("- [%s %s] %s\n", 
		(const char *[]){"Primary","Secondary"}[ide_devices[drive].channel],
		(const char *[]){"Master", "Slave"}[ide_devices[drive].drive],
		ide_devices[drive].model);

	return err;
}

#define ATA_PRIMARY 0
#define ATA_SECONDARY 1

#include "logger.hpp"
#include "acpi.hpp"

void ide::initIDE(uint32_t bar0, uint32_t bar1, uint32_t bar2, uint32_t bar3, uint32_t bar4) {
  bar0 = (bar0 >> 2) << 2;
  bar1 = (bar1 >> 2) << 2;
  bar2 = (bar2 >> 2) << 2;
  bar3 = (bar3 >> 2) << 2;
  bar4 = (bar4 >> 2) << 2;
  channels[ATA_PRIMARY].base = bar0 ? bar0 : 0x1F0;
  channels[ATA_PRIMARY].ctrl = bar1 ? bar1 : 0x3F4;
  channels[ATA_SECONDARY].base = bar2 ? bar2 : 0x170;
  channels[ATA_SECONDARY].ctrl = bar3 ? bar3 : 0x374;
  channels[ATA_PRIMARY].bus_master_ide = bar4;
  channels[ATA_SECONDARY].bus_master_ide = bar4 + 8;

  Log(kWarn, "IDE: wc primary...\n");
  writeChannel(ATA_PRIMARY, ATA_REG_CONTROL, 2);
  writeChannel(ATA_SECONDARY, ATA_REG_CONTROL, 2);
  Log(kWarn, "IDE: wc primary done!\n");

  int count = 0;
  for (int i = 0; i < 2; i++)
    for (int j = 0; j < 2; j++) {
      ide_devices[count].reserved = 0;
      printk("IDE: wc hddevsel\n");
      writeChannel(i, ATA_REG_HDDEVSEL, 0xA0 | (j << 4));
      printk("IDE: wc hddevsel done\n");
      acpi::WaitMilliseconds(acpi::fadt, 1);
      printk("IDE: slept 1ms\n");
      printk("IDE: wc cmds...\n");
      writeChannel(i, ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
      printk("IDE: wc cmds done\n");
      acpi::WaitMilliseconds(acpi::fadt, 1);
      printk("IDE: slept 1ms\n");

      if (readChannel(i, ATA_REG_STATUS) == 0) continue;
      printk("IDE: dev EXIST\n");
      int err = 0;
      int dev_type = IDE_ATA;
      while (true) {
        uint8_t status = readChannel(i, ATA_REG_STATUS);
        if (status & ATA_SR_ERR) {
          err = 1;
          break;
        }
        if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ)) break;
      }
      if (err) {
        printk("IDE: err\n");
        uint8_t cl = readChannel(i, ATA_REG_LBA1);
        uint8_t ch = readChannel(i, ATA_REG_LBA2);

        if (cl == 0x14 && ch == 0xeb) dev_type = IDE_ATAPI;
        else if (cl == 0x69 && ch == 0x96) dev_type = IDE_ATAPI;
        else continue; // unknown type
        
        writeChannel(i, ATA_REG_COMMAND, ATA_CMD_IDENTIFY_PACKET);
        acpi::WaitMilliseconds(acpi::fadt, 1);
      }
      printk("IDE: reading ident packet\n");
      readBuffer(i, ATA_REG_DATA, ide_buf, 128);
      printk("IDE: done.\n");
      auto& dev = ide_devices[count];
      dev.reserved = 1;
      dev.type = dev_type;
      dev.channel = i;
      dev.drive = j;
      dev.signature = *reinterpret_cast<uint16_t*>(ide_buf + ATA_IDENT_DEVICETYPE);
      dev.capabilities = *reinterpret_cast<uint16_t*>(ide_buf + ATA_IDENT_CAPABILITIES);
      dev.commandsets = *reinterpret_cast<uint32_t*>(ide_buf + ATA_IDENT_COMMANDSETS);

      if (dev.commandsets & (1 << 26)) { // 48-bit addressing mode
        dev.size = *reinterpret_cast<uint32_t*>(ide_buf + ATA_IDENT_MAX_LBA_EXT);
      } else {
        dev.size = *reinterpret_cast<uint32_t*>(ide_buf + ATA_IDENT_MAX_LBA);
      }
      for (int k = 0; k < 40; k += 2) {
        dev.model[k] = ide_buf[ATA_IDENT_MODEL + k+1];
        dev.model[k+1] = ide_buf[ATA_IDENT_MODEL + k];
      }
      dev.model[40] = 0;
      count++;
    }

  const char* dev_type_str[] = { "ATA", "ATAPI" };
  for (auto& dev : ide_devices) {
    if (dev.reserved) {
      printk(" Found %s Drive %dB - %s\n", dev_type_str[dev.type], dev.size * 512, dev.model);
    }
  }
}

uint8_t accessATA(uint8_t direction, uint8_t drive, uint32_t lba, uint8_t num_sectors, uint64_t rdi) {
  uint8_t lba_mode;
  bool dma;
  uint8_t lba_io[6];
  uint32_t channel = ide_devices[drive].channel;
  uint32_t slavebit = ide_devices[drive].drive;
  uint32_t bus = channels[channel].base; // IO port base
  uint32_t words = 256; // chunk size read in insw instruction
  uint8_t head;
  // CHS mode exclusive
  uint16_t cylinder;
  uint8_t sect;
  // disable irq
  writeChannel(channel, ATA_REG_CONTROL, channels[channel].nIEN = ide_irq_invoked + 0x2);
  if (lba >= 0x10000000) {
    lba_mode = 2;
    lba_io[0] = static_cast<uint8_t>(lba >> 0);
    lba_io[1] = static_cast<uint8_t>(lba >> 8);
    lba_io[2] = static_cast<uint8_t>(lba >> 16);
    lba_io[3] = static_cast<uint8_t>(lba >> 24);
    lba_io[4] = 0;
    lba_io[5] = 0;
    head = 0;
  } else if (ide_devices[drive].capabilities & 0x200) {
    lba_mode = 1;
    lba_io[0] = static_cast<uint8_t>(lba >> 0);
    lba_io[1] = static_cast<uint8_t>(lba >> 8);
    lba_io[2] = static_cast<uint8_t>(lba >> 16);
    lba_io[3] = 0;
    lba_io[4] = 0;
    lba_io[5] = 0;
    head = static_cast<uint8_t>((lba >> 24) & 0x0f);
  } else {
    lba_mode = 0;
    sect = (lba % 63) + 1;
    cylinder = (lba + 1 - sect) / (16 * 63);
    lba_io[0] = sect;
    lba_io[1] = static_cast<uint8_t>(cylinder >> 0);
    lba_io[2] = static_cast<uint8_t>(cylinder >> 8);
    lba_io[3] = 0;
    lba_io[4] = 0;
    lba_io[5] = 0;
    head = (lba + 1 - sect) % (16 * 63) / 63;
  }
  // use only PIO mode, not DMA mode
  dma = false;
  while (readChannel(channel, ATA_REG_STATUS) & ATA_SR_BSY);
  
  // driver select!
  if (lba_mode == 0) {
    writeChannel(channel, ATA_REG_HDDEVSEL, head | (slavebit << 4) | 0xA0);
  } else {
    writeChannel(channel, ATA_REG_HDDEVSEL, head | (slavebit << 4) | 0xE0);
  }
  
  if (lba_mode == 2) {
    writeChannel(channel, ATA_REG_SECCOUNT1, 0);
    writeChannel(channel, ATA_REG_LBA3, lba_io[3]);
    writeChannel(channel, ATA_REG_LBA4, lba_io[4]);
    writeChannel(channel, ATA_REG_LBA5, lba_io[5]);
  }
  writeChannel(channel, ATA_REG_SECCOUNT0, num_sectors);
  writeChannel(channel, ATA_REG_LBA0, lba_io[0]);
  writeChannel(channel, ATA_REG_LBA1, lba_io[1]);
  writeChannel(channel, ATA_REG_LBA2, lba_io[2]);
  static const uint8_t cmds[2][2][3] = {
    { // direction = 0, read
      { ATA_CMD_READ_PIO, ATA_CMD_READ_PIO, ATA_CMD_READ_PIO_EXT }, // pio mode
      { ATA_CMD_READ_DMA, ATA_CMD_READ_DMA, ATA_CMD_READ_DMA_EXT }, // dma mode
    },
    { // direction = 1, write
      { ATA_CMD_WRITE_PIO, ATA_CMD_WRITE_PIO, ATA_CMD_WRITE_PIO_EXT }, // pio mode
      { ATA_CMD_WRITE_DMA, ATA_CMD_WRITE_DMA, ATA_CMD_WRITE_DMA_EXT }, // dma mode
    },
  };
  static const uint8_t flush_cmds[3] = {
    ATA_CMD_CACHE_FLUSH, ATA_CMD_CACHE_FLUSH, ATA_CMD_CACHE_FLUSH_EXT
  };

  writeChannel(channel, ATA_REG_COMMAND, cmds[direction][dma][lba_mode]);
  if (dma) {
    printk("no dma support\n");
    while (1) __asm__("hlt");
  } else {
    if (direction == 0) {
      for (uint8_t i = 0; i < num_sectors; i++) {
        if (uint8_t err = doIdePolling(channel, 1)) {
          return err;
        }
        printk("insw\n");
        asm("rep insw" ::"c"(words), "d"(bus), "D"(rdi));
        rdi += (words * 2);
      }
    }
    else {
      for (uint8_t i = 0; i < num_sectors; i++) {
        if (uint8_t err = doIdePolling(channel, 1)) {
          return err;
        }
        asm("rep outsw" ::"c"(words), "d"(bus), "S"(rdi));
        rdi += (words * 2);
      }
      writeChannel(channel, ATA_REG_COMMAND, flush_cmds[lba_mode]);
      doIdePolling(channel, 0);
    }
  }
  return 0;
}

void ide::readSectors(uint8_t drive, uint8_t num_sectors, uint32_t lba, void* buf) {
  if (drive > 3 || ide_devices[drive].reserved == 0) return;

  if ((lba + num_sectors) > ide_devices[drive].size && ide_devices[drive].type == IDE_ATA) return;

  if (ide_devices[drive].type == IDE_ATA) {
    auto err = accessATA(0, drive, lba, num_sectors, reinterpret_cast<uint64_t>(buf));
    if (err) printIdeError(drive, err);
  } else {
    // do nothing here yet
  }
}

void ide::writeSectors(uint8_t drive, uint8_t num_sectors, uint32_t lba, const void* buf) {
  if (drive > 3 || ide_devices[drive].reserved == 0) return;

  if ((lba + num_sectors) > ide_devices[drive].size && ide_devices[drive].type == IDE_ATA) return;

  if (ide_devices[drive].type == IDE_ATA) {
    auto err = accessATA(1, drive, lba, num_sectors, reinterpret_cast<uint64_t>(buf));
    if (err) printIdeError(drive, err);
  } else {
    // do nothing here yet
  }
}