#!/bin/sh
# Copyright (C) 2010-2013 OpenWrt.org

. /lib/functions/leds.sh
. /lib/ramips.sh

get_status_led() {
	case $(ramips_board_name) in
	3g-6200n)
		status_led="edimax:green:power"
		;;
	3g150b | 3g300m | w150m)
		status_led="tenda:blue:ap"
		;;
	a5-v11)
		status_led="a5-v11:red:power"
		;;
	ai-br100)
		status_led="aigale:blue:wlan"
		;;
	ar670w)
		status_led="ar670w:green:power"
		;;
	ar725w)
		status_led="ar725w:green:power"
		;;
	awapn2403)
		status_led="asiarf:green:wps"
		;;
	atp-52b)
		status_led="atp-52b:green:run"
		;;
	asl26555)
		status_led="asl26555:green:power"
		;;
	br6524n)
		status_led="edimax:blue:power"
		;;
	br-6425 | br-6475nd)
		status_led="edimax:green:power"
		;;
	cf-wr800n)
		status_led="comfast:white:wps"
		;;
	cy-swr1100)
		status_led="samsung:blue:wps"
		;;
	d105)
		status_led="d105:red:power"
		;;
	dcs-930 | dir-300-b1 | dir-600-b1 | dir-600-b2 | dir-610-a1 | dir-615-h1 | dir-615-d | dir-620-a1| dir-620-d1| dir-300-b7| dir-320-b1)
		status_led="d-link:green:status"
		;;
	dcs-930l-b1)
		status_led="d-link:red:power"
		;;
	dir-645)
		status_led="d-link:green:wps"
		;;
	dap-1350)
		status_led="d-link:blue:power"
		;;
	e1700)
		status_led="linksys:green:power"
		;;
	esr-9753)
		status_led="esr-9753:orange:power"
		;;
	f5d8235-v2)
		status_led="f5d8235v2:blue:router"
		;;
	fonera20n)
		status_led="fonera20n:green:power"
		;;
	ip2202)
		status_led="ip2202:green:run"
		;;
	rt-n13u)
		status_led="rt-n13u:power"
		;;
	hlk-rm04)
		status_led="hlk-rm04:red:power"
		;;
	ht-tm02)
		status_led="ht-tm02:blue:wlan"
		;;
	all0239-3g|\
	hw550-3g)
		status_led="hw550-3g:green:status"
		;;
	m2m)
		status_led="m2m:blue:wifi"
		;;
	m3)
		status_led="m3:blue:status"
		;;
	m4)
		status_led="m4:blue:status"
		;;
	mlw221|\
	mlwg2)
		status_led="kingston:blue:system"
		;;
	mofi3500-3gn)
		status_led="mofi3500-3gn:green:status"
		;;
	mpr-a1)
		status_led="hame:red:power"
		;;
	mpr-a2)
		status_led="hame:red:power"
		;;
	mr-102n)
		status_led="mr-102n:amber:status"
		;;
	nbg-419n)
		status_led="nbg-419n:green:power"
		;;
	nw718)
		status_led="nw718:amber:cpu"
		;;
	miniembwifi)
		status_led="miniembwifi:green:status"
		;;
	hpm)
		status_led="hpm:green:status"
		;;
	pbr-m1)
		status_led="pbr-m1:green:sys"
		;;
	psr-680w)
		status_led="psr-680w:red:wan"
		;;
	pwh2004)
		status_led="pwh2004:green:power"
		;;
	px-4885)
		status_led="px-4885:orange:wifi"
		;;
	re6500)
		status_led="linksys:orange:wifi"
		;;
	rt-n15)
		status_led="rt-n15:blue:power"
		;;
	rt-n10-plus)
		status_led="asus:green:wps"
		;;
	rt-n14u | rt-n56u | wl-330n | wl-330n3g)
		status_led="asus:blue:power"
		;;
	rut5xx)
		status_led="rut5xx:green:status"
		;;
	sap-g3200u3)
		status_led="storylink:green:usb"
		;;
	sl-r7205)
		status_led="sl-r7205:green:status"
		;;
	tew-691gr|\
	tew-692gr)
		status_led="trendnet:green:wps"
		;;
	v11st-fe)
		status_led="v11st-fe:green:status"
		;;
	v22rw-2x2)
		status_led="v22rw-2x2:green:security"
		;;
	vocore)
		status_led="vocore:green:status"
		;;
	w306r-v20)
		status_led="w306r-v20:green:sys"
		;;
	w502u)
		status_led="alfa:blue:wps"
		;;
	wcr-150gn)
		status_led="wcr150gn:amber:power"
		;;
	whr-g300n)
		status_led="whr-g300n:green:router"
		;;
	wmr-300)
		status_led="wmr-300:green:status"
		;;
	wli-tx4-ag300n)
		status_led="buffalo:blue:power"
		;;
	wzr-agl300nh)
		status_led="buffalo:green:router"
		;;
	wl-351)
		status_led="wl-351:amber:power"
		;;
	wr512-3gn)
		status_led="wr512:green:wps"
		;;
	zbt-wr8305rt)
		status_led="zbt-wr8305rt:sys"
		;;
	whr-300hp2 | \
	whr-600d | \
	whr-1166d | \
	wsr-600 | \
	wsr-1166)
		status_led="buffalo:orange:diag"
		;;
	wnce2001)
		status_led="netgear:green:power"
		;;
	nexx-wt1520)
		status_led="nexx-wt1520:white:power"
		;;
	wt3020)
		status_led="nexx:blue:power"
		;;
	mzk-w300nh2)
		status_led="mzkw300nh2:green:power"
		;;
	ur-326n4g)
		status_led="ur326:green:wps"
		;;
	ur-336un)
		status_led="ur336:green:wps"
		;;
	x5)
		status_led="x5:green:power"
		;;
	x8)
		status_led="x8:green:power"
		;;
	xdxrn502j)
		status_led="xdxrn502j:green:power"
		;;
	miwifi-mini)
		status_led="miwifi-mini:red:status"
		;;
	f7c027)
		status_led="belkin:orange:status"
		;;
	oy-0001)
		status_led="oy:green:wifi"
		;;
	na930)
		status_led="na930:blue:power"
		;;
	y1 | \
	y1s)
		status_led="lenovo:blue:power"
		;;
	zte-q7)
		status_led="zte:red:status"
		;;
	mzk-dp150n)
		status_led="mzkdp150n:green:power"
		;;
	esac
}

set_state() {
	get_status_led

	case "$1" in
	preinit)
		status_led_blink_preinit
		;;
	failsafe)
		status_led_blink_failsafe
		;;
	preinit_regular)
		status_led_blink_preinit_regular
		;;
	done)
		status_led_on
		;;
	esac
}
