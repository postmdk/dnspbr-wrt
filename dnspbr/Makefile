include $(TOPDIR)/rules.mk

PKG_NAME:=dnspbr
PKG_VERSION:=1.2.2
PKG_RELEASE:=1

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/postmdk/dnspbr.git
PKG_SOURCE_VERSION:=a1f1d45b1e5d8d6b5bbadf2f1ff1d439065f3320
PKG_MIRROR_HASH:=4b6d4d6f48f83cb842c99b4d6aebe5754c1f1e49429684a8026be0c5b179a9db

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
