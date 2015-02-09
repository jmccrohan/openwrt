/*
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 *
 *   Copyright (C) 2009-2013 John Crispin <blogic@openwrt.org>
 */

#include <linux/module.h>

#include <asm/mach-ralink/ralink_regs.h>

#include "ralink_soc_eth.h"
#include "mdio_rt2880.h"

#define RT3883_RSTCTRL_FE		BIT(21)

static void rt3883_fe_reset(void)
{
	fe_reset(RT3883_RSTCTRL_FE);
}

static int rt3883_fwd_config(struct fe_priv *priv)
{
	int ret;

	ret = fe_set_clock_cycle(priv);
	if (ret)
		return ret;

	fe_fwd_config(priv);
	fe_w32(FE_PSE_FQFC_CFG_256Q, FE_PSE_FQ_CFG);
	fe_csum_config(priv);

	return ret;
}

static void rt3883_init_data(struct fe_soc_data *data,
		struct net_device *netdev)
{
	struct fe_priv *priv = netdev_priv(netdev);

	priv->flags = FE_FLAG_PADDING_64B | FE_FLAG_PADDING_BUG |
		FE_FLAG_JUMBO_FRAME;
	netdev->hw_features = NETIF_F_SG | NETIF_F_IP_CSUM |
		NETIF_F_RXCSUM | NETIF_F_HW_VLAN_CTAG_TX;
}

static struct fe_soc_data rt3883_data = {
	.mac = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 },
	.init_data = rt3883_init_data,
	.reset_fe = rt3883_fe_reset,
	.fwd_config = rt3883_fwd_config,
	.pdma_glo_cfg = FE_PDMA_SIZE_8DWORDS,
	.rx_int = FE_RX_DONE_INT,
	.tx_int = FE_TX_DONE_INT,
	.status_int = FE_CNT_GDM_AF,
	.checksum_bit = RX_DMA_L4VALID,
	.mdio_read = rt2880_mdio_read,
	.mdio_write = rt2880_mdio_write,
	.mdio_adjust_link = rt2880_mdio_link_adjust,
	.port_init = rt2880_port_init,
};

const struct of_device_id of_fe_match[] = {
	{ .compatible = "ralink,rt3883-eth", .data = &rt3883_data },
	{},
};

MODULE_DEVICE_TABLE(of, of_fe_match);

