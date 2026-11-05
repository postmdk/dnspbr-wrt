include $(TOPDIR)/rules.mk

PKG_NAME:=dnspbr
PKG_VERSION:=1.1.3
PKG_RELEASE:=1

include $(INCLUDE_DIR)/package.mk

define Package/dnspbr
  SECTION:=net
  CATEGORY:=Network
  TITLE:=DNS-based network topology mapper and fake DNS daemon
  DEPENDS:=+libstdcpp +libpthread +libldns +nftables
endef

define Package/dnspbr/description
  A C++ daemon that maps fake IP addresses to real ones and manipulates nftables via JSON API for transparent redirection, with PBR
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src $(PKG_BUILD_DIR)/
	if [ -d ./files ]; then $(CP) ./files $(PKG_BUILD_DIR)/; fi
endef

# off autoconfig, Makefile in src/
define Build/Configure
endef

define Build/Compile
	$(MAKE) -C $(PKG_BUILD_DIR)/src \
		CC="$(TARGET_CC)" \
		CXX="$(TARGET_CXX)" \
		CFLAGS="$(TARGET_CFLAGS)" \
		CXXFLAGS="$(TARGET_CXXFLAGS) -std=c++17 -DVERSION='\"$(PKG_VERSION)\"'" \
		LDFLAGS="$(TARGET_LDFLAGS) -lldns -lnftables -lpthread"
endef

define Package/dnspbr/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/src/dnspbr $(1)/usr/bin/

	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/dnspbr.conf $(1)/etc/config/dnspbr
	$(INSTALL_DIR) $(1)/etc/dnspbr/dnsmasq
	$(INSTALL_CONF) ./files/dnspbr.conf.example $(1)/etc/dnspbr/dnsmasq/dnspbr.conf.example

	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/dnspbr.init $(1)/etc/init.d/dnspbr
endef

$(eval $(call BuildPackage,dnspbr))
