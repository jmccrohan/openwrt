# Use the default kernel version if the Makefile doesn't override it

ifeq ($(KERNEL),2.4)
  LINUX_VERSION?=2.4.37.1
else
  LINUX_VERSION?=2.6.21.7
endif
LINUX_RELEASE?=1

ifeq ($(LINUX_VERSION),2.4.37.1)
  LINUX_KERNEL_MD5SUM:=28bf1bcc94c78804ad67d2f06f950958
endif
ifeq ($(LINUX_VERSION),2.6.21.7)
  LINUX_KERNEL_MD5SUM:=bc15fad1487336d5dcb0945cd039d8ed
endif
ifeq ($(LINUX_VERSION),2.6.23.17)
  LINUX_KERNEL_MD5SUM:=a0300a393ac91ce9c64bf31522b45e2e
endif
ifeq ($(LINUX_VERSION),2.6.25.20)
  LINUX_KERNEL_MD5SUM:=0da698edccf03e2235abc2830a495114
endif
ifeq ($(LINUX_VERSION),2.6.27.24)
  LINUX_KERNEL_MD5SUM:=3e272117ceb50ad8ab3686ae00afa9d8
endif
ifeq ($(LINUX_VERSION),2.6.28.10)
  LINUX_KERNEL_MD5SUM:=c4efb2c494d749cb5de274f8ae41c3fa
endif
ifeq ($(LINUX_VERSION),2.6.29.2)
  LINUX_KERNEL_MD5SUM:=a6839571a9e70baf821d2fb752f9c4c6
endif
ifeq ($(LINUX_VERSION),2.6.30)
  LINUX_KERNEL_MD5SUM:=7a80058a6382e5108cdb5554d1609615
endif

# disable the md5sum check for unknown kernel versions
LINUX_KERNEL_MD5SUM?=x

split_version=$(subst ., ,$(1))
merge_version=$(subst $(space),.,$(1))
KERNEL_BASE=$(firstword $(subst -, ,$(LINUX_VERSION)))
KERNEL=$(call merge_version,$(wordlist 1,2,$(call split_version,$(KERNEL_BASE))))
KERNEL_PATCHVER=$(call merge_version,$(wordlist 1,3,$(call split_version,$(KERNEL_BASE))))

