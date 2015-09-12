/*
 *  MikroTik Cloud Router Switch CRS125G support
 *
 *  Copyright (C) 2012 Stijn Tintel <stijn@linux-ipv6.be>
 *  Copyright (C) 2012 Gabor Juhos <juhosg@openwrt.org>
 *  Copyright (C) 2015 Charlie Smurthwaite <charlie@atechmedia.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */

#define pr_fmt(fmt) "crs125g: " fmt

#include <linux/phy.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/ath9k_platform.h>
#include <linux/ar8216_platform.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/spi/spi.h>
#include <linux/spi/flash.h>
#include <linux/routerboot.h>
#include <linux/gpio.h>

#include <asm/prom.h>
#include <asm/mach-ath79/ath79.h>
#include <asm/mach-ath79/ath79_spi_platform.h>
#include <asm/mach-ath79/ar71xx_regs.h>

#include "common.h"
#include "dev-eth.h"
#include "dev-m25p80.h"
#include "dev-nfc.h"
#include "dev-usb.h"
#include "dev-spi.h"
#include "dev-wmac.h"
#include "machtypes.h"
#include "routerboot.h"
#include "dev-leds-gpio.h"

#define CRS125G_GPIO_NAND_NCE	14

/* Partitions are copied from RB2011 and therefore likely incorrect. */
static struct mtd_partition crs125g_nand_partitions[] = {
        {
                .name   = "booter",
                .offset = 0,
                .size   = (256 * 1024),
                .mask_flags = MTD_WRITEABLE,
        },
        {
                .name   = "kernel",
                .offset = (256 * 1024),
                .size   = (4 * 1024 * 1024) - (256 * 1024),
        },
        {
                .name   = "rootfs",
                .offset = MTDPART_OFS_NXTBLK,
                .size   = MTDPART_SIZ_FULL,
        },
};

/* NAND works, copied from RB2011. Part: TC58DVG02D5TA00 */
static void crs125g_nand_select_chip(int chip_no)
{
        switch (chip_no) {
        case 0:
                gpio_set_value(CRS125G_GPIO_NAND_NCE, 0);
                break;
        default:
                gpio_set_value(CRS125G_GPIO_NAND_NCE, 1);
                break;
        }
        ndelay(500);
}

static struct nand_ecclayout crs125g_nand_ecclayout = {
        .eccbytes       = 6,
        .eccpos         = { 8, 9, 10, 13, 14, 15 },
        .oobavail       = 9,
        .oobfree        = { { 0, 4 }, { 6, 2 }, { 11, 2 }, { 4, 1 } }
};

/* Copied from RB2011. Don't know if this is necessary. */
static int crs125g_nand_scan_fixup(struct mtd_info *mtd)
{
        struct nand_chip *chip = mtd->priv;

        if (mtd->writesize == 512) {
                /*
                 * Use the OLD Yaffs-1 OOB layout, otherwise RouterBoot
                 * will not be able to find the kernel that we load.
                 */
                chip->ecc.layout = &crs125g_nand_ecclayout;
        }

        return 0;
}

void __init crs125g_nand_init(void)
{
        gpio_request_one(CRS125G_GPIO_NAND_NCE, GPIOF_OUT_INIT_HIGH, "NAND nCE");

        ath79_nfc_set_scan_fixup(crs125g_nand_scan_fixup);
        ath79_nfc_set_parts(crs125g_nand_partitions,
                            ARRAY_SIZE(crs125g_nand_partitions));
        ath79_nfc_set_select_chip(crs125g_nand_select_chip);
        ath79_nfc_set_swap_dma(true);
        ath79_register_nfc();
}

/* GPIO CS for LCD display. This device uses a shared MOSI/MISO Pin
 * nCS: 4
 * DCX: 5
 * SCK: 6
 * I/O: 7
 */
static struct ath79_spi_controller_data ath79_spi0_cdata =
{
   .cs_type = ATH79_SPI_CS_TYPE_GPIO,
   .cs_line = 4,
   .is_flash = false
};

/* Set to spidev for now, I haven't got a driver yet. */
static struct spi_board_info ath79_spi_info[] = {
   {
      .bus_num   = 0,
      .chip_select   = 0,
      .max_speed_hz   = 15 * 1000 * 1000,   
      .modalias   = "spidev",
      .mode      = SPI_MODE_0,
      .controller_data = &ath79_spi0_cdata,
   },
};

/* Only 2 LEDs appear to be available.
 * There is also speaker. It's here for documentation
 * until a speaker oscillator driver is available.
 */
static struct gpio_led crs125g_leds_gpio[] __initdata = {
  {
		.name		= "LCD",
		.gpio		= 2,
		.active_low	= 0,
		.default_state = LEDS_GPIO_DEFSTATE_OFF
	}, {
		.name		= "USR2",
		.gpio		= 11,
		.active_low	= 1,
		.default_state = LEDS_GPIO_DEFSTATE_ON
	}, {
		.name		= "SPEAKER",
		.gpio		= 22,
		.active_low	= 0,
		.default_state = LEDS_GPIO_DEFSTATE_OFF
	}
};

static void __init crs125g_setup(void)
{
	const struct rb_info *info;
  static struct ath79_spi_platform_data ath79_spi_data;

	info = rb_init_info((void *) KSEG1ADDR(0x1f000000), 0x10000);
  crs125g_nand_init();
  
  ath79_spi_data.bus_num = 0;
  ath79_spi_data.num_chipselect = ARRAY_SIZE(ath79_spi_info);
  ath79_register_spi(&ath79_spi_data, ath79_spi_info, ARRAY_SIZE(ath79_spi_info));

	ath79_setup_ar934x_eth_cfg(AR934X_ETH_CFG_RGMII_GMAC0 |
           AR934X_ETH_CFG_RXD_DELAY |
				   AR934X_ETH_CFG_SW_ONLY_MODE);

  /* eth0 is connected to a QCA8513L. We can't program the switch but we can
   * exchange traffic with the configuration set by the Mikrotik bootloader. */
	ath79_register_mdio(0, 0x0);
	ath79_init_mac(ath79_eth0_data.mac_addr, ath79_mac_base, 0);
	ath79_eth0_data.phy_if_mode = PHY_INTERFACE_MODE_RGMII;
	ath79_eth0_data.mii_bus_dev = &ath79_mdio0_device.dev;
	ath79_eth0_data.speed = SPEED_1000;
	ath79_eth0_data.duplex = DUPLEX_FULL;
	ath79_register_eth(0);

  /* eth1 is connected to the internal switch. This is likely useless. */
	//ath79_register_mdio(1, 0x0);
	//ath79_init_mac(ath79_eth1_data.mac_addr, ath79_mac_base, 1);
	//ath79_eth1_data.phy_if_mode = PHY_INTERFACE_MODE_GMII;
	//ath79_register_eth(1);
	
	ath79_register_leds_gpio(-1, ARRAY_SIZE(crs125g_leds_gpio), crs125g_leds_gpio);
}

MIPS_MACHINE(ATH79_MACH_CRS125G, "crs125g", "RouterBOARD CRS125-24G-1S", crs125g_setup);