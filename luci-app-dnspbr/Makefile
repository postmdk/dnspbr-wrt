include $(TOPDIR)/rules.mk

PKG_NAME:=luci-app-dnspbr
PKG_VERSION:=1.1.5
PKG_RELEASE:=1

PKG_LICENSE:=MIT
PKG_MAINTAINER:=postmdk

# Пакет является чисто конфигурационным/скриптовым (не компилируется под конкретный CPU)
PKGARCH:=all

include $(TOPDIR)/feeds/luci/luci.mk

define Package/$(PKG_NAME)
  SECTION:=luci
  CATEGORY:=LuCI
  SUBMENU:=3. Applications
  TITLE:=LuCI support for dnspbr
  DEPENDS:=+dnspbr +luci-base
endef

define Package/$(PKG_NAME)/description
  Modern LuCI interface wrapper for dnspbr optimized for OpenWrt
endef

define Build/Compile
endef

define Package/$(PKG_NAME)/install
	$(INSTALL_DIR) $(1)/usr/share/luci/menu.d
	$(INSTALL_DATA) ./root/usr/share/luci/menu.d/luci-app-dnspbr.json $(1)/usr/share/luci/menu.d/

	$(INSTALL_DIR) $(1)/usr/share/rpcd/acl.d
	$(INSTALL_DATA) ./root/usr/share/rpcd/acl.d/luci-app-dnspbr.json $(1)/usr/share/rpcd/acl.d/

	$(INSTALL_DIR) $(1)/www/luci-static/resources/view/dnspbr
	$(INSTALL_DATA) ./htdocs/luci-static/resources/view/dnspbr/overview.js $(1)/www/luci-static/resources/view/dnspbr/
	$(INSTALL_DATA) ./htdocs/luci-static/resources/view/dnspbr/lists.js $(1)/www/luci-static/resources/view/dnspbr/

	$(INSTALL_DIR) $(1)/etc/uci-defaults
	$(INSTALL_BIN) ./root/etc/uci-defaults/40_luci-dnspbr $(1)/etc/uci-defaults/
endef

$(eval $(call BuildPackage,$(PKG_NAME)))
