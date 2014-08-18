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
