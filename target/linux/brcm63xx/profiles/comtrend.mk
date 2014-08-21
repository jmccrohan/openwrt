#
# Copyright (C) 2014 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

define Profile/CT536_CT5621
  NAME:=Comtrend CT-536+/CT-5621
  PACKAGES:=kmod-b43 wpad-mini
endef
define Profile/CT536_CT5621/Description
  Package set optimized for CT-536+/CT-5621.
endef
$(eval $(call Profile,CT536_CT5621))

define Profile/CT5365
  NAME:=Comtrend CT-5365
  PACKAGES:=kmod-b43 wpad-mini
endef
define Profile/CT5365/Description
  Package set optimized for CT-5365.
endef
$(eval $(call Profile,CT5365))

define Profile/CT6373
  NAME:=Comtrend CT-6373
  PACKAGES:=kmod-b43 wpad-mini \
	kmod-usb2 kmod-usb-ohci
endef
define Profile/CT6373/Description
  Package set optimized for CT-6373.
endef
$(eval $(call Profile,CT6373))
