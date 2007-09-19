/*
 * Copyright (C) 2006, 2007 OpenWrt.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/moduleparam.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/version.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/skbuff.h>
#include <linux/mii.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <asm/gpio.h>

MODULE_AUTHOR("Eugene Konev");
MODULE_DESCRIPTION("TI AR7 ethernet driver (CPMAC)");
MODULE_LICENSE("GPL");

static int rx_ring_size = 64;
static int disable_napi;
static int debug_level = 8;
static int dumb_switch;

module_param(rx_ring_size, int, 0644);
module_param(disable_napi, int, 0644);
/* Next 2 are only used in cpmac_probe, so it's pointless to change them */
module_param(debug_level, int, 0444);
module_param(dumb_switch, int, 0444);

MODULE_PARM_DESC(rx_ring_size, "Size of rx ring (in skbs)");
MODULE_PARM_DESC(disable_napi, "Disable NAPI polling");
MODULE_PARM_DESC(debug_level, "Number of NETIF_MSG bits to enable");
MODULE_PARM_DESC(dumb_switch, "Assume switch is not connected to MDIO bus");

/* frame size + 802.1q tag */
#define CPMAC_SKB_SIZE		(ETH_FRAME_LEN + 4)
#define CPMAC_TX_RING_SIZE	8

/* Ethernet registers */
#define CPMAC_TX_CONTROL		0x0004
#define CPMAC_TX_TEARDOWN		0x0008
#define CPMAC_RX_CONTROL		0x0014
#define CPMAC_RX_TEARDOWN		0x0018
#define CPMAC_MBP			0x0100
# define MBP_RXPASSCRC			0x40000000
# define MBP_RXQOS			0x20000000
# define MBP_RXNOCHAIN			0x10000000
# define MBP_RXCMF			0x01000000
# define MBP_RXSHORT			0x00800000
# define MBP_RXCEF			0x00400000
# define MBP_RXPROMISC			0x00200000
# define MBP_PROMISCCHAN(channel)	(((channel) & 0x7) << 16)
# define MBP_RXBCAST			0x00002000
# define MBP_BCASTCHAN(channel)		(((channel) & 0x7) << 8)
# define MBP_RXMCAST			0x00000020
# define MBP_MCASTCHAN(channel)		((channel) & 0x7)
#define CPMAC_UNICAST_ENABLE		0x0104
#define CPMAC_UNICAST_CLEAR		0x0108
#define CPMAC_MAX_LENGTH		0x010c
#define CPMAC_BUFFER_OFFSET		0x0110
#define CPMAC_MAC_CONTROL		0x0160
# define MAC_TXPTYPE			0x00000200
# define MAC_TXPACE			0x00000040
# define MAC_MII			0x00000020
# define MAC_TXFLOW			0x00000010
# define MAC_RXFLOW			0x00000008
# define MAC_MTEST			0x00000004
# define MAC_LOOPBACK			0x00000002
# define MAC_FDX			0x00000001
#define CPMAC_MAC_STATUS		0x0164
# define MAC_STATUS_QOS			0x00000004
# define MAC_STATUS_RXFLOW		0x00000002
# define MAC_STATUS_TXFLOW		0x00000001
#define CPMAC_TX_INT_ENABLE		0x0178
#define CPMAC_TX_INT_CLEAR		0x017c
#define CPMAC_MAC_INT_VECTOR		0x0180
# define MAC_INT_STATUS			0x00080000
# define MAC_INT_HOST			0x00040000
# define MAC_INT_RX			0x00020000
# define MAC_INT_TX			0x00010000
#define CPMAC_MAC_EOI_VECTOR		0x0184
#define CPMAC_RX_INT_ENABLE		0x0198
#define CPMAC_RX_INT_CLEAR		0x019c
#define CPMAC_MAC_INT_ENABLE		0x01a8
#define CPMAC_MAC_INT_CLEAR		0x01ac
#define CPMAC_MAC_ADDR_LO(channel) 	(0x01b0 + (channel) * 4)
#define CPMAC_MAC_ADDR_MID		0x01d0
#define CPMAC_MAC_ADDR_HI		0x01d4
#define CPMAC_MAC_HASH_LO		0x01d8
#define CPMAC_MAC_HASH_HI		0x01dc
#define CPMAC_TX_PTR(channel)		(0x0600 + (channel) * 4)
#define CPMAC_RX_PTR(channel)		(0x0620 + (channel) * 4)
#define CPMAC_TX_ACK(channel)		(0x0640 + (channel) * 4)
#define CPMAC_RX_ACK(channel)		(0x0660 + (channel) * 4)
#define CPMAC_REG_END			0x0680
/* 
 * Rx/Tx statistics
 * TODO: use some of them to fill stats in cpmac_stats()
 */
#define CPMAC_STATS_RX_GOOD		0x0200
#define CPMAC_STATS_RX_BCAST		0x0204
#define CPMAC_STATS_RX_MCAST		0x0208
#define CPMAC_STATS_RX_PAUSE		0x020c
#define CPMAC_STATS_RX_CRC		0x0210
#define CPMAC_STATS_RX_ALIGN		0x0214
#define CPMAC_STATS_RX_OVER		0x0218
#define CPMAC_STATS_RX_JABBER		0x021c
#define CPMAC_STATS_RX_UNDER		0x0220
#define CPMAC_STATS_RX_FRAG		0x0224
#define CPMAC_STATS_RX_FILTER		0x0228
#define CPMAC_STATS_RX_QOSFILTER	0x022c
#define CPMAC_STATS_RX_OCTETS		0x0230

#define CPMAC_STATS_TX_GOOD		0x0234
#define CPMAC_STATS_TX_BCAST		0x0238
#define CPMAC_STATS_TX_MCAST		0x023c
#define CPMAC_STATS_TX_PAUSE		0x0240
#define CPMAC_STATS_TX_DEFER		0x0244
#define CPMAC_STATS_TX_COLLISION	0x0248
#define CPMAC_STATS_TX_SINGLECOLL	0x024c
#define CPMAC_STATS_TX_MULTICOLL	0x0250
#define CPMAC_STATS_TX_EXCESSCOLL	0x0254
#define CPMAC_STATS_TX_LATECOLL		0x0258
#define CPMAC_STATS_TX_UNDERRUN		0x025c
#define CPMAC_STATS_TX_CARRIERSENSE	0x0260
#define CPMAC_STATS_TX_OCTETS		0x0264

#define cpmac_read(base, reg)		(readl((void __iomem *)(base) + (reg)))
#define cpmac_write(base, reg, val)	(writel(val, (void __iomem *)(base) + \
						(reg)))

/* MDIO bus */
#define CPMAC_MDIO_VERSION		0x0000
#define CPMAC_MDIO_CONTROL		0x0004
# define MDIOC_IDLE			0x80000000
# define MDIOC_ENABLE			0x40000000
# define MDIOC_PREAMBLE			0x00100000
# define MDIOC_FAULT			0x00080000
# define MDIOC_FAULTDETECT		0x00040000
# define MDIOC_INTTEST			0x00020000
# define MDIOC_CLKDIV(div)		((div) & 0xff)
#define CPMAC_MDIO_ALIVE		0x0008
#define CPMAC_MDIO_LINK			0x000c
#define CPMAC_MDIO_ACCESS(channel)	(0x0080 + (channel) * 8)
# define MDIO_BUSY			0x80000000
# define MDIO_WRITE			0x40000000
# define MDIO_REG(reg)			(((reg) & 0x1f) << 21)
# define MDIO_PHY(phy)			(((phy) & 0x1f) << 16)
# define MDIO_DATA(data)		((data) & 0xffff)
#define CPMAC_MDIO_PHYSEL(channel)	(0x0084 + (channel) * 8)
# define PHYSEL_LINKSEL			0x00000040
# define PHYSEL_LINKINT			0x00000020

struct cpmac_desc {
	u32 hw_next;
	u32 hw_data;
	u16 buflen;
	u16 bufflags;
	u16 datalen;
	u16 dataflags;
#define CPMAC_SOP			0x8000
#define CPMAC_EOP			0x4000
#define CPMAC_OWN			0x2000
#define CPMAC_EOQ			0x1000
	struct sk_buff *skb;
	struct cpmac_desc *next;
	dma_addr_t mapping;
	dma_addr_t data_mapping;
};

struct cpmac_priv {
	struct net_device_stats stats;
	spinlock_t lock;
	struct cpmac_desc *rx_head;
	int tx_head, tx_tail;
	struct cpmac_desc *desc_ring;
	dma_addr_t dma_ring;
	void __iomem *regs;
	struct mii_bus *mii_bus;
	struct phy_device *phy;
	char phy_name[BUS_ID_SIZE];
	struct plat_cpmac_data *config;
	int oldlink, oldspeed, oldduplex;
	u32 msg_enable;
	struct net_device *dev;
	struct work_struct alloc_work;
};

static irqreturn_t cpmac_irq(int, void *);
static void cpmac_reset(struct net_device *dev);
static void cpmac_hw_init(struct net_device *dev);
static int cpmac_stop(struct net_device *dev);
static int cpmac_open(struct net_device *dev);

static void cpmac_dump_regs(struct net_device *dev)
{
	int i;
	struct cpmac_priv *priv = netdev_priv(dev);
	for (i = 0; i < CPMAC_REG_END; i += 4) {
		if (i % 16 == 0) {
			if (i)
				printk("\n");
			printk(KERN_DEBUG "%s: reg[%p]:", dev->name,
			       priv->regs + i);
		}
		printk(" %08x", cpmac_read(priv->regs, i));
	}
	printk("\n");
}

static void cpmac_dump_desc(struct net_device *dev, struct cpmac_desc *desc)
{
	int i;
	printk(KERN_DEBUG "%s: desc[%p]:", dev->name, desc);
	for (i = 0; i < sizeof(*desc) / 4; i++)
		printk(" %08x", ((u32 *)desc)[i]);
	printk("\n");
}

static void cpmac_dump_skb(struct net_device *dev, struct sk_buff *skb)
{
	int i;
	printk(KERN_DEBUG "%s: skb 0x%p, len=%d\n", dev->name, skb, skb->len);
	for (i = 0; i < skb->len; i++) {
		if (i % 16 == 0) {
			if (i)
				printk("\n");
			printk(KERN_DEBUG "%s: data[%p]:", dev->name,
			       skb->data + i);
		}
		printk(" %02x", ((u8 *)skb->data)[i]);
	}
	printk("\n");
}

static int cpmac_mdio_read(struct mii_bus *bus, int phy_id, int reg)
{
	u32 val;

	while (cpmac_read(bus->priv, CPMAC_MDIO_ACCESS(0)) & MDIO_BUSY)
		cpu_relax();
	cpmac_write(bus->priv, CPMAC_MDIO_ACCESS(0), MDIO_BUSY | MDIO_REG(reg) |
		    MDIO_PHY(phy_id));
	while ((val = cpmac_read(bus->priv, CPMAC_MDIO_ACCESS(0))) & MDIO_BUSY)
		cpu_relax();
	return MDIO_DATA(val);
}

static int cpmac_mdio_write(struct mii_bus *bus, int phy_id,
			    int reg, u16 val)
{
	while (cpmac_read(bus->priv, CPMAC_MDIO_ACCESS(0)) & MDIO_BUSY)
		cpu_relax();
	cpmac_write(bus->priv, CPMAC_MDIO_ACCESS(0), MDIO_BUSY | MDIO_WRITE |
		    MDIO_REG(reg) | MDIO_PHY(phy_id) | MDIO_DATA(val));
	return 0;
}

static int cpmac_mdio_reset(struct mii_bus *bus)
{
	ar7_device_reset(AR7_RESET_BIT_MDIO);
	cpmac_write(bus->priv, CPMAC_MDIO_CONTROL, MDIOC_ENABLE |
		    MDIOC_CLKDIV(ar7_cpmac_freq() / 2200000 - 1));
	return 0;
}

static int mii_irqs[PHY_MAX_ADDR] = { PHY_POLL, };

static struct mii_bus cpmac_mii = {
	.name = "cpmac-mii",
	.read = cpmac_mdio_read,
	.write = cpmac_mdio_write,
	.reset = cpmac_mdio_reset,
	.irq = mii_irqs,
};

static int cpmac_config(struct net_device *dev, struct ifmap *map)
{
	if (dev->flags & IFF_UP)
		return -EBUSY;

	/* Don't allow changing the I/O address */
	if (map->base_addr != dev->base_addr)
		return -EOPNOTSUPP;

	/* ignore other fields */
	return 0;
}

static int cpmac_set_mac_address(struct net_device *dev, void *addr)
{
	struct sockaddr *sa = addr;

	if (dev->flags & IFF_UP)
		return -EBUSY;

	memcpy(dev->dev_addr, sa->sa_data, dev->addr_len);

	return 0;
}

static void cpmac_set_multicast_list(struct net_device *dev)
{
	struct dev_mc_list *iter;
	int i;
	u8 tmp;
	u32 mbp, bit, hash[2] = { 0, };
	struct cpmac_priv *priv = netdev_priv(dev);

	mbp = cpmac_read(priv->regs, CPMAC_MBP);
	if (dev->flags & IFF_PROMISC) {
		cpmac_write(priv->regs, CPMAC_MBP, (mbp & ~MBP_PROMISCCHAN(0)) |
			    MBP_RXPROMISC);
	} else {
		cpmac_write(priv->regs, CPMAC_MBP, mbp & ~MBP_RXPROMISC);
		if (dev->flags & IFF_ALLMULTI) {
			/* enable all multicast mode */
			cpmac_write(priv->regs, CPMAC_MAC_HASH_LO, 0xffffffff);
			cpmac_write(priv->regs, CPMAC_MAC_HASH_HI, 0xffffffff);
		} else {
			/* 
			 * cpmac uses some strange mac address hashing
			 * (not crc32)
			 */
			for (i = 0, iter = dev->mc_list; i < dev->mc_count;
			     i++, iter = iter->next) {
				bit = 0;
				tmp = iter->dmi_addr[0];
				bit  ^= (tmp >> 2) ^ (tmp << 4);
				tmp = iter->dmi_addr[1];
				bit  ^= (tmp >> 4) ^ (tmp << 2);
				tmp = iter->dmi_addr[2];
				bit  ^= (tmp >> 6) ^ tmp;
				tmp = iter->dmi_addr[3];
				bit  ^= (tmp >> 2) ^ (tmp << 4);
				tmp = iter->dmi_addr[4];
				bit  ^= (tmp >> 4) ^ (tmp << 2);
				tmp = iter->dmi_addr[5];
				bit  ^= (tmp >> 6) ^ tmp;
				bit &= 0x3f;
				hash[bit / 32] |= 1 << (bit % 32);
			}

			cpmac_write(priv->regs, CPMAC_MAC_HASH_LO, hash[0]);
			cpmac_write(priv->regs, CPMAC_MAC_HASH_HI, hash[1]);
		}
	}
}

static struct sk_buff *cpmac_rx_one(struct net_device *dev,
				    struct cpmac_priv *priv,
				    struct cpmac_desc *desc)
{
	unsigned long flags;
	struct sk_buff *skb, *result = NULL;

	if (unlikely(netif_msg_hw(priv)))
		cpmac_dump_desc(dev, desc);
	cpmac_write(priv->regs, CPMAC_RX_ACK(0), (u32)desc->mapping);
	if (unlikely(!desc->datalen)) {
		if (netif_msg_rx_err(priv) && net_ratelimit())
			printk(KERN_WARNING "%s: rx: spurious interrupt\n",
			       dev->name);
		return NULL;
	}

	skb = netdev_alloc_skb(dev, CPMAC_SKB_SIZE);
	spin_lock_irqsave(&priv->lock, flags);
	if (likely(skb)) {
		skb_reserve(skb, 2);
		skb_put(desc->skb, desc->datalen);
		desc->skb->protocol = eth_type_trans(desc->skb, dev);
		desc->skb->ip_summed = CHECKSUM_NONE;
		priv->stats.rx_packets++;
		priv->stats.rx_bytes += desc->datalen;
		result = desc->skb;
		dma_unmap_single(&dev->dev, desc->data_mapping, CPMAC_SKB_SIZE,
				 DMA_FROM_DEVICE);
		desc->skb = skb;
		desc->data_mapping = dma_map_single(&dev->dev, skb->data,
						    CPMAC_SKB_SIZE,
						    DMA_FROM_DEVICE);
		desc->hw_data = (u32)desc->data_mapping;
		if (unlikely(netif_msg_pktdata(priv))) {
			printk(KERN_DEBUG "%s: received packet:\n", dev->name);
			cpmac_dump_skb(dev, result);
		}
	} else {
		if (netif_msg_rx_err(priv) && net_ratelimit())
			printk(KERN_WARNING 
			       "%s: low on skbs, dropping packet\n", dev->name);
		priv->stats.rx_dropped++;
	}
	spin_unlock_irqrestore(&priv->lock, flags);

	desc->buflen = CPMAC_SKB_SIZE;
	desc->dataflags = CPMAC_OWN;

	return result;
}

static void cpmac_rx(struct net_device *dev)
{
	struct sk_buff *skb;
	struct cpmac_desc *desc;
	struct cpmac_priv *priv = netdev_priv(dev);

	spin_lock(&priv->lock);
	if (unlikely(!priv->rx_head)) {
		spin_unlock(&priv->lock);
		return;
	}

	desc = priv->rx_head;

	while ((desc->dataflags & CPMAC_OWN) == 0) {
		skb = cpmac_rx_one(dev, priv, desc);
		if (likely(skb))
			netif_rx(skb);
		desc = desc->next;
	}

	priv->rx_head = desc;
	cpmac_write(priv->regs, CPMAC_RX_PTR(0), (u32)desc->mapping);
	spin_unlock(&priv->lock);
}

static int cpmac_poll(struct net_device *dev, int *budget)
{
	struct sk_buff *skb;
	struct cpmac_desc *desc;
	int received = 0, quota = min(dev->quota, *budget);
	struct cpmac_priv *priv = netdev_priv(dev);

	if (unlikely(!priv->rx_head)) {
		if (netif_msg_rx_err(priv) && net_ratelimit())
			printk(KERN_WARNING "%s: rx: polling, but no queue\n",
			       dev->name);
		netif_rx_complete(dev);
		return 0;
	}

	desc = priv->rx_head;

	while ((received < quota) && ((desc->dataflags & CPMAC_OWN) == 0)) {
		skb = cpmac_rx_one(dev, priv, desc);
		if (likely(skb)) {
			netif_receive_skb(skb);
			received++;
		}
		desc = desc->next;
	}

	priv->rx_head = desc;
	*budget -= received;
	dev->quota -= received;
	if (unlikely(netif_msg_rx_status(priv)))
		printk(KERN_DEBUG "%s: poll processed %d packets\n", dev->name,
		       received);
	if (desc->dataflags & CPMAC_OWN) {
		netif_rx_complete(dev);
		cpmac_write(priv->regs, CPMAC_RX_PTR(0), (u32)desc->mapping);
		cpmac_write(priv->regs, CPMAC_RX_INT_ENABLE, 1);
		return 0;
	}

	return 1;
}

static int cpmac_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	unsigned long flags;
	int channel, len;
	struct cpmac_desc *desc;
	struct cpmac_priv *priv = netdev_priv(dev);

	if (unlikely(skb_padto(skb, ETH_ZLEN))) {
		if (netif_msg_tx_err(priv) && net_ratelimit())
			printk(KERN_WARNING "%s: tx: padding failed, dropping\n",
			       dev->name);
		spin_lock_irqsave(&priv->lock, flags);
		priv->stats.tx_dropped++;
		spin_unlock_irqrestore(&priv->lock, flags);
		return -ENOMEM;
	}

	len = max(skb->len, ETH_ZLEN);
	spin_lock_irqsave(&priv->lock, flags);
	channel = priv->tx_tail++;
	priv->tx_tail %= CPMAC_TX_RING_SIZE;
	if (priv->tx_tail == priv->tx_head)
		netif_stop_queue(dev);

	desc = &priv->desc_ring[channel];
	if (desc->dataflags & CPMAC_OWN) {
		if (netif_msg_tx_err(priv) && net_ratelimit())
			printk(KERN_WARNING "%s: tx dma ring full, dropping\n",
			       dev->name);
		priv->stats.tx_dropped++;
		spin_unlock_irqrestore(&priv->lock, flags);
		dev_kfree_skb_any(skb);
		return -ENOMEM;
	}

	dev->trans_start = jiffies;
	spin_unlock_irqrestore(&priv->lock, flags);
	desc->dataflags = CPMAC_SOP | CPMAC_EOP | CPMAC_OWN;
	desc->skb = skb;
	desc->data_mapping = dma_map_single(&dev->dev, skb->data, len,
					    DMA_TO_DEVICE);
	desc->hw_data = (u32)desc->data_mapping;
	desc->datalen = len;
	desc->buflen = len;
	if (unlikely(netif_msg_tx_queued(priv)))
		printk(KERN_DEBUG "%s: sending 0x%p, len=%d\n", dev->name, skb,
		       skb->len);
	if (unlikely(netif_msg_hw(priv)))
		cpmac_dump_desc(dev, desc);
	if (unlikely(netif_msg_pktdata(priv)))
		cpmac_dump_skb(dev, skb);
	cpmac_write(priv->regs, CPMAC_TX_PTR(channel), (u32)desc->mapping);

	return 0;
}

static void cpmac_end_xmit(struct net_device *dev, int channel)
{
	struct cpmac_desc *desc;
	struct cpmac_priv *priv = netdev_priv(dev);

	spin_lock(&priv->lock);
	desc = &priv->desc_ring[channel];
	cpmac_write(priv->regs, CPMAC_TX_ACK(channel), (u32)desc->mapping);
	if (likely(desc->skb)) {
		priv->stats.tx_packets++;
		priv->stats.tx_bytes += desc->skb->len;
		dma_unmap_single(&dev->dev, desc->data_mapping, desc->skb->len,
				 DMA_TO_DEVICE);

		if (unlikely(netif_msg_tx_done(priv)))
			printk(KERN_DEBUG "%s: sent 0x%p, len=%d\n", dev->name,
			       desc->skb, desc->skb->len);

		dev_kfree_skb_irq(desc->skb);
		if (netif_queue_stopped(dev))
			netif_wake_queue(dev);
	} else
		if (netif_msg_tx_err(priv) && net_ratelimit())
			printk(KERN_WARNING
			       "%s: end_xmit: spurious interrupt\n", dev->name);
	spin_unlock(&priv->lock);
}

static void cpmac_reset(struct net_device *dev)
{
	int i;
	struct cpmac_priv *priv = netdev_priv(dev);

	ar7_device_reset(priv->config->reset_bit);
	cpmac_write(priv->regs, CPMAC_RX_CONTROL,
		    cpmac_read(priv->regs, CPMAC_RX_CONTROL) & ~1);
	cpmac_write(priv->regs, CPMAC_TX_CONTROL,
		    cpmac_read(priv->regs, CPMAC_TX_CONTROL) & ~1);
	for (i = 0; i < 8; i++) {
		cpmac_write(priv->regs, CPMAC_TX_PTR(i), 0);
		cpmac_write(priv->regs, CPMAC_RX_PTR(i), 0);
	}
	cpmac_write(priv->regs, CPMAC_MAC_CONTROL,
		    cpmac_read(priv->regs, CPMAC_MAC_CONTROL) & ~MAC_MII);
}

static inline void cpmac_free_rx_ring(struct net_device *dev)
{
	struct cpmac_desc *desc;
	int i;
	struct cpmac_priv *priv = netdev_priv(dev);

	if (unlikely(!priv->rx_head))
		return;

	desc = priv->rx_head;

	for (i = 0; i < rx_ring_size; i++) {
		desc->buflen = CPMAC_SKB_SIZE;
		if ((desc->dataflags & CPMAC_OWN) == 0) {
			if (netif_msg_rx_err(priv) && net_ratelimit())
				printk(KERN_WARNING "%s: packet dropped\n",
				       dev->name);
			if (unlikely(netif_msg_hw(priv)))
				cpmac_dump_desc(dev, desc);
			desc->dataflags = CPMAC_OWN;
			priv->stats.rx_dropped++;
		}
		desc = desc->next;
	}
}

static irqreturn_t cpmac_irq(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
 	struct cpmac_priv *priv;
	u32 status;

	if (!dev)
		return IRQ_NONE;

	priv = netdev_priv(dev);

	status = cpmac_read(priv->regs, CPMAC_MAC_INT_VECTOR);

	if (unlikely(netif_msg_intr(priv)))
		printk(KERN_DEBUG "%s: interrupt status: 0x%08x\n", dev->name,
		       status);

	if (status & MAC_INT_TX)
		cpmac_end_xmit(dev, (status & 7));

	if (status & MAC_INT_RX) {
		if (disable_napi)
			cpmac_rx(dev);
		else {
			cpmac_write(priv->regs, CPMAC_RX_INT_CLEAR, 1);
			netif_rx_schedule(dev);
		}
	}

	cpmac_write(priv->regs, CPMAC_MAC_EOI_VECTOR, 0);

	if (unlikely(status & (MAC_INT_HOST | MAC_INT_STATUS))) {
		if (netif_msg_drv(priv) && net_ratelimit())
			printk(KERN_ERR "%s: hw error, resetting...\n",
			       dev->name);
		if (unlikely(netif_msg_hw(priv)))
			cpmac_dump_regs(dev);
		spin_lock(&priv->lock);
		phy_stop(priv->phy);
		cpmac_reset(dev);
		cpmac_free_rx_ring(dev);
		cpmac_hw_init(dev);
		spin_unlock(&priv->lock);
	}

	return IRQ_HANDLED;
}

static void cpmac_tx_timeout(struct net_device *dev)
{
	struct cpmac_priv *priv = netdev_priv(dev);
	struct cpmac_desc *desc;

	priv->stats.tx_errors++;
	desc = &priv->desc_ring[priv->tx_head++];
	priv->tx_head %= 8;
	if (netif_msg_tx_err(priv) && net_ratelimit())
		printk(KERN_WARNING "%s: transmit timeout\n", dev->name);
	if (desc->skb)
		dev_kfree_skb_any(desc->skb);
	netif_wake_queue(dev);
}

static int cpmac_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct cpmac_priv *priv = netdev_priv(dev);
	if (!(netif_running(dev)))
		return -EINVAL;
	if (!priv->phy)
		return -EINVAL;
	if ((cmd == SIOCGMIIPHY) || (cmd == SIOCGMIIREG) ||
	    (cmd == SIOCSMIIREG))
		return phy_mii_ioctl(priv->phy, if_mii(ifr), cmd);

	return -EINVAL;
}

static int cpmac_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct cpmac_priv *priv = netdev_priv(dev);

	if (priv->phy)
		return phy_ethtool_gset(priv->phy, cmd);

	return -EINVAL;
}

static int cpmac_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct cpmac_priv *priv = netdev_priv(dev);

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (priv->phy)
		return phy_ethtool_sset(priv->phy, cmd);

	return -EINVAL;
}

static void cpmac_get_drvinfo(struct net_device *dev,
			      struct ethtool_drvinfo *info)
{
	strcpy(info->driver, "cpmac");
	strcpy(info->version, "0.0.3");
	info->fw_version[0] = '\0';
	sprintf(info->bus_info, "%s", "cpmac");
	info->regdump_len = 0;
}

static const struct ethtool_ops cpmac_ethtool_ops = {
	.get_settings = cpmac_get_settings,
	.set_settings = cpmac_set_settings,
	.get_drvinfo = cpmac_get_drvinfo,
	.get_link = ethtool_op_get_link,
};

static struct net_device_stats *cpmac_stats(struct net_device *dev)
{
	struct cpmac_priv *priv = netdev_priv(dev);

	if (netif_device_present(dev))
		return &priv->stats;

	return NULL;
}

static int cpmac_change_mtu(struct net_device *dev, int mtu)
{
	unsigned long flags;
	struct cpmac_priv *priv = netdev_priv(dev);
	spinlock_t *lock = &priv->lock;

	if ((mtu < 68) || (mtu > 1500))
		return -EINVAL;

	spin_lock_irqsave(lock, flags);
	dev->mtu = mtu;
	spin_unlock_irqrestore(lock, flags);

	return 0;
}

static void cpmac_adjust_link(struct net_device *dev)
{
	struct cpmac_priv *priv = netdev_priv(dev);
	unsigned long flags;
	int new_state = 0;

	spin_lock_irqsave(&priv->lock, flags);
	if (priv->phy->link) {
		if (priv->phy->duplex != priv->oldduplex) {
			new_state = 1;
			priv->oldduplex = priv->phy->duplex;
		}

		if (priv->phy->speed != priv->oldspeed) {
			new_state = 1;
			priv->oldspeed = priv->phy->speed;
		}

		if (!priv->oldlink) {
			new_state = 1;
			priv->oldlink = 1;
			netif_schedule(dev);
		}
	} else if (priv->oldlink) {
		new_state = 1;
		priv->oldlink = 0;
		priv->oldspeed = 0;
		priv->oldduplex = -1;
	}

	if (new_state && netif_msg_link(priv) && net_ratelimit())
		phy_print_status(priv->phy);

	spin_unlock_irqrestore(&priv->lock, flags);
}

static void cpmac_hw_init(struct net_device *dev)
{
	int i;
	struct cpmac_priv *priv = netdev_priv(dev);

	for (i = 0; i < 8; i++) {
		cpmac_write(priv->regs, CPMAC_TX_PTR(i), 0);
		cpmac_write(priv->regs, CPMAC_RX_PTR(i), 0);
	}
	cpmac_write(priv->regs, CPMAC_RX_PTR(0), priv->rx_head->mapping);

	cpmac_write(priv->regs, CPMAC_MBP, MBP_RXSHORT | MBP_RXBCAST |
		    MBP_RXMCAST);
	cpmac_write(priv->regs, CPMAC_UNICAST_ENABLE, 1);
	cpmac_write(priv->regs, CPMAC_UNICAST_CLEAR, 0xfe);
	cpmac_write(priv->regs, CPMAC_BUFFER_OFFSET, 0);
	for (i = 0; i < 8; i++)
		cpmac_write(priv->regs, CPMAC_MAC_ADDR_LO(i), dev->dev_addr[5]);
	cpmac_write(priv->regs, CPMAC_MAC_ADDR_MID, dev->dev_addr[4]);
	cpmac_write(priv->regs, CPMAC_MAC_ADDR_HI, dev->dev_addr[0] |
		    (dev->dev_addr[1] << 8) | (dev->dev_addr[2] << 16) |
		    (dev->dev_addr[3] << 24));
	cpmac_write(priv->regs, CPMAC_MAX_LENGTH, CPMAC_SKB_SIZE);
	cpmac_write(priv->regs, CPMAC_RX_INT_CLEAR, 0xff);
	cpmac_write(priv->regs, CPMAC_TX_INT_CLEAR, 0xff);
	cpmac_write(priv->regs, CPMAC_MAC_INT_CLEAR, 0xff);
	cpmac_write(priv->regs, CPMAC_RX_INT_ENABLE, 1);
	cpmac_write(priv->regs, CPMAC_TX_INT_ENABLE, 0xff);
	cpmac_write(priv->regs, CPMAC_MAC_INT_ENABLE, 3);

	cpmac_write(priv->regs, CPMAC_RX_CONTROL,
		    cpmac_read(priv->regs, CPMAC_RX_CONTROL) | 1);
	cpmac_write(priv->regs, CPMAC_TX_CONTROL,
		    cpmac_read(priv->regs, CPMAC_TX_CONTROL) | 1);
	cpmac_write(priv->regs, CPMAC_MAC_CONTROL,
		    cpmac_read(priv->regs, CPMAC_MAC_CONTROL) | MAC_MII |
		    MAC_FDX);

	priv->phy->state = PHY_CHANGELINK;
	phy_start(priv->phy);
}

static int cpmac_open(struct net_device *dev)
{
	int i, size, res;
	struct cpmac_priv *priv = netdev_priv(dev);
	struct cpmac_desc *desc;
	struct sk_buff *skb;

	priv->phy = phy_connect(dev, priv->phy_name, &cpmac_adjust_link,
				0, PHY_INTERFACE_MODE_MII);
	if (IS_ERR(priv->phy)) {
		if (netif_msg_drv(priv))
			printk(KERN_ERR "%s: Could not attach to PHY\n",
			       dev->name);
		return PTR_ERR(priv->phy);
	}

	if (!request_mem_region(dev->mem_start, dev->mem_end -
				dev->mem_start, dev->name)) {
		if (netif_msg_drv(priv))
			printk(KERN_ERR "%s: failed to request registers\n",
			       dev->name);
		res = -ENXIO;
		goto fail_reserve;
	}

	priv->regs = ioremap(dev->mem_start, dev->mem_end -
			     dev->mem_start);
	if (!priv->regs) {
		if (netif_msg_drv(priv))
			printk(KERN_ERR "%s: failed to remap registers\n",
			       dev->name);
		res = -ENXIO;
		goto fail_remap;
	}

	priv->rx_head = NULL;
	size = rx_ring_size + CPMAC_TX_RING_SIZE;
	priv->desc_ring = dma_alloc_coherent(&dev->dev,
					     sizeof(struct cpmac_desc) * size,
					     &priv->dma_ring,
					     GFP_KERNEL);
	if (!priv->desc_ring) {
		res = -ENOMEM;
		goto fail_alloc;
	}

	priv->rx_head = &priv->desc_ring[CPMAC_TX_RING_SIZE];
	for (i = 0; i < size; i++)
		priv->desc_ring[i].mapping = priv->dma_ring + sizeof(*desc) * i;

	for (i = 0, desc = &priv->rx_head[i]; i < rx_ring_size; i++, desc++) {
		skb = netdev_alloc_skb(dev, CPMAC_SKB_SIZE);
		if (unlikely(!skb)) {
			res = -ENOMEM;
			goto fail_desc;
		}
		skb_reserve(skb, 2);
		desc->skb = skb;
		desc->data_mapping = dma_map_single(&dev->dev, skb->data,
						    CPMAC_SKB_SIZE,
						    DMA_FROM_DEVICE);
		desc->hw_data = (u32)desc->data_mapping;
		desc->buflen = CPMAC_SKB_SIZE;
		desc->dataflags = CPMAC_OWN;
		desc->next = &priv->rx_head[(i + 1) % rx_ring_size];
		desc->hw_next = (u32)desc->next->mapping;
	}

	if ((res = request_irq(dev->irq, cpmac_irq, IRQF_SHARED,
			       dev->name, dev))) {
		if (netif_msg_drv(priv))
			printk(KERN_ERR "%s: failed to obtain irq\n",
			       dev->name);
		goto fail_irq;
	}

	cpmac_reset(dev);
	cpmac_hw_init(dev);

	netif_start_queue(dev);
	return 0;

fail_irq:
fail_desc:
	for (i = 0; i < rx_ring_size; i++) {
		if (priv->rx_head[i].skb) {
			kfree_skb(priv->rx_head[i].skb);
			dma_unmap_single(&dev->dev,
					 priv->rx_head[i].data_mapping,
					 CPMAC_SKB_SIZE,
					 DMA_FROM_DEVICE);
		}
	}
fail_alloc:
	kfree(priv->desc_ring);
	iounmap(priv->regs);

fail_remap:
	release_mem_region(dev->mem_start, dev->mem_end -
			   dev->mem_start);

fail_reserve:
	phy_disconnect(priv->phy);

	return res;
}

static int cpmac_stop(struct net_device *dev)
{
	int i;
	struct cpmac_priv *priv = netdev_priv(dev);

	netif_stop_queue(dev);

	phy_stop(priv->phy);
	phy_disconnect(priv->phy);
	priv->phy = NULL;

	cpmac_reset(dev);

	for (i = 0; i < 8; i++)
		cpmac_write(priv->regs, CPMAC_TX_PTR(i), 0);
	cpmac_write(priv->regs, CPMAC_RX_PTR(0), 0);
	cpmac_write(priv->regs, CPMAC_MBP, 0);

	free_irq(dev->irq, dev);
	release_mem_region(dev->mem_start, dev->mem_end -
			   dev->mem_start);
	priv->rx_head = &priv->desc_ring[CPMAC_TX_RING_SIZE];
	for (i = 0; i < rx_ring_size; i++) {
		if (priv->rx_head[i].skb) {
			kfree_skb(priv->rx_head[i].skb);
			dma_unmap_single(&dev->dev,
					 priv->rx_head[i].data_mapping,
					 CPMAC_SKB_SIZE,
					 DMA_FROM_DEVICE);
		}
	}

	dma_free_coherent(&dev->dev, sizeof(struct cpmac_desc) *
			  (CPMAC_TX_RING_SIZE + rx_ring_size),
			  priv->desc_ring, priv->dma_ring);
	return 0;
}

static int external_switch;

static int __devinit cpmac_probe(struct platform_device *pdev)
{
	int i, rc, phy_id;
	struct resource *res;
	struct cpmac_priv *priv;
	struct net_device *dev;
	struct plat_cpmac_data *pdata;

	pdata = pdev->dev.platform_data;

	for (phy_id = 0; phy_id < PHY_MAX_ADDR; phy_id++) {
		if (!(pdata->phy_mask & (1 << phy_id)))
			continue;
		if (!cpmac_mii.phy_map[phy_id])
			continue;
		break;
	}

	if (phy_id == PHY_MAX_ADDR) {
		if (external_switch || dumb_switch)
			phy_id = 0;
		else {
			printk(KERN_ERR "cpmac: no PHY present\n");
			return -ENODEV;
		}
	}

	dev = alloc_etherdev(sizeof(struct cpmac_priv));

	if (!dev) {
		printk(KERN_ERR "cpmac: Unable to allocate net_device\n");
		return -ENOMEM;
	}

	SET_MODULE_OWNER(dev);
	platform_set_drvdata(pdev, dev);
	priv = netdev_priv(dev);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "regs");
	if (!res) {
		rc = -ENODEV;
		goto fail;
	}

	dev->mem_start = res->start;
	dev->mem_end = res->end;
	dev->irq = platform_get_irq_byname(pdev, "irq");

	dev->open               = cpmac_open;
	dev->stop               = cpmac_stop;
	dev->set_config         = cpmac_config;
	dev->hard_start_xmit    = cpmac_start_xmit;
	dev->do_ioctl           = cpmac_ioctl;
	dev->get_stats          = cpmac_stats;
	dev->change_mtu         = cpmac_change_mtu;
	dev->set_mac_address    = cpmac_set_mac_address;
	dev->set_multicast_list = cpmac_set_multicast_list;
	dev->tx_timeout         = cpmac_tx_timeout;
	dev->ethtool_ops        = &cpmac_ethtool_ops;
	if (!disable_napi) {
		dev->poll = cpmac_poll;
		dev->weight = min(rx_ring_size, 64);
	}

	spin_lock_init(&priv->lock);
	priv->msg_enable = netif_msg_init(debug_level, 0xff);
	priv->config = pdata;
	priv->dev = dev;
	memcpy(dev->dev_addr, priv->config->dev_addr, sizeof(dev->dev_addr));
	if (phy_id == 31) {
		snprintf(priv->phy_name, BUS_ID_SIZE, PHY_ID_FMT,
			 cpmac_mii.id, phy_id);
/*		cpmac_write(cpmac_mii.priv, CPMAC_MDIO_PHYSEL(0), PHYSEL_LINKSEL
		| PHYSEL_LINKINT | phy_id);*/
	} else
		snprintf(priv->phy_name, BUS_ID_SIZE, "fixed@%d:%d", 100, 1);

	if ((rc = register_netdev(dev))) {
		printk(KERN_ERR "cpmac: error %i registering device %s\n", rc,
		       dev->name);
		goto fail;
	}

	if (netif_msg_probe(priv)) {
		printk(KERN_INFO
		       "cpmac: device %s (regs: %p, irq: %d, phy: %s, mac: ",
		       dev->name, (u32 *)dev->mem_start, dev->irq,
		       priv->phy_name);
		for (i = 0; i < 6; i++)
			printk("%02x%s", dev->dev_addr[i], i < 5 ? ":" : ")\n");
	}
	return 0;

fail:
	free_netdev(dev);
	return rc;
}

static int __devexit cpmac_remove(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	unregister_netdev(dev);
	free_netdev(dev);
	return 0;
}

static struct platform_driver cpmac_driver = {
	.driver.name = "cpmac",
	.probe = cpmac_probe,
	.remove = cpmac_remove,
};

int __devinit cpmac_init(void)
{
	u32 mask;
	int i, res;

	cpmac_mii.priv = ioremap(AR7_REGS_MDIO, 256);

	if (!cpmac_mii.priv) {
		printk(KERN_ERR "Can't ioremap mdio registers\n");
		return -ENXIO;
	}

#warning FIXME: unhardcode gpio&reset bits
	ar7_gpio_disable(26);
	ar7_gpio_disable(27);
	ar7_device_reset(AR7_RESET_BIT_CPMAC_LO);
	ar7_device_reset(AR7_RESET_BIT_CPMAC_HI);
	ar7_device_reset(AR7_RESET_BIT_EPHY);

	cpmac_mii.reset(&cpmac_mii);

	for (i = 0; i < 300000; i++)
		if ((mask = cpmac_read(cpmac_mii.priv, CPMAC_MDIO_ALIVE)))
			break;
		else
			cpu_relax();

	mask &= 0x7fffffff;
	if (mask & (mask - 1)) {
		external_switch = 1;
		mask = 0;
	}

	cpmac_mii.phy_mask = ~(mask | 0x80000000);

	res = mdiobus_register(&cpmac_mii);
	if (res)
		goto fail_mii;

	res = platform_driver_register(&cpmac_driver);
	if (res)
		goto fail_cpmac;

	return 0;

fail_cpmac:
	mdiobus_unregister(&cpmac_mii);

fail_mii:
	iounmap(cpmac_mii.priv);

	return res;
}

void __devexit cpmac_exit(void)
{
	platform_driver_unregister(&cpmac_driver);
	mdiobus_unregister(&cpmac_mii);
	iounmap(cpmac_mii.priv);
}

module_init(cpmac_init);
module_exit(cpmac_exit);
