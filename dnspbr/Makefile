include $(TOPDIR)/rules.mk

PKG_NAME:=dnspbr
PKG_VERSION:=1.2.2
PKG_RELEASE:=1

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/postmdk/dnspbr.git
PKG_SOURCE_VERSION:=85e5aa821efa4af984f924db2aab9ed43c58b1eb
PKG_MIRROR_HASH:=701bd0718ce4ff5961971b96a61bec3680cff2ab2ab300a344860bb52eb79d55

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

PKG_LICENSE:=MIT
PKG_MAINTAINER:=postmdk

include $(INCLUDE_DIR)/package.mk

define Package/dnspbr
	SECTION:=net
	CATEGORY:=Network
	TITLE:=DNS-based policy routing helper
	DEPENDS:=+libstdcpp +libpthread +libldns +nftables
endef

define Build/Compile
	$(MAKE) ENABLE_OUTPUT=1 \
		-C $(PKG_BUILD_DIR) \
		CXX="$(TARGET_CXX)" \
		CXXFLAGS="$(TARGET_CXXFLAGS) -std=c++17 -O2 -DVERSION=\\\"$(PKG_VERSION)\\\"" \
		LDFLAGS="$(TARGET_LDFLAGS) -lldns -lnftables -pthread"
endef

define Package/dnspbr/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/bin/dnspbr $(1)/usr/bin/dnspbr

	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/dnspbr.init $(1)/etc/init.d/dnspbr

	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/dnspbr.config $(1)/etc/config/dnspbr
endef

$(eval $(call BuildPackage,dnspbr))
