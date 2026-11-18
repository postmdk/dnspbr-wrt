include $(TOPDIR)/rules.mk

PKG_NAME:=luci-i18n-dnspbr-ru
PKG_RELEASE:=1

PKGARCH:=all

include $(INCLUDE_DIR)/package.mk

define Package/luci-i18n-dnspbr-ru
  SECTION:=luci
  CATEGORY:=LuCI
  SUBMENU:=translations
  TITLE:=Russian translation for luci-app-dnspbr
  DEPENDS:=+luci-app-dnspbr
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./po $(PKG_BUILD_DIR)/
endef

define Build/Configure
endef

define Build/Compile
	for po in $(PKG_BUILD_DIR)/po/ru/*.po; do \
		[ -f "$$po" ] && po2lmo "$$po" "$$po.lmo" || true; \
	done
endef

define Package/luci-i18n-dnspbr-ru/install
	$(INSTALL_DIR) $(1)/usr/lib/lua/luci/i18n
	for po in $(PKG_BUILD_DIR)/po/ru/*.po; do \
		[ -f "$$po" ] && $(INSTALL_DATA) "$$po.lmo" $(1)/usr/lib/lua/luci/i18n/dnspbr.ru.lmo || true; \
	done
endef

$(eval $(call BuildPackage,luci-i18n-dnspbr-ru))
