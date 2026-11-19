
include $(TOPDIR)/rules.mk

PKG_NAME:=luci-i18n-dnspbr-ru
PKG_VERSION:=1.1.4
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
	$(CP) "$(CURDIR)/po/ru/dnspbr.po" $(PKG_BUILD_DIR)/translation.po
endef

define Build/Compile
	@$(STAGING_DIR_HOST)/bin/po2lmo \
		$(PKG_BUILD_DIR)/translation.po \
		$(PKG_BUILD_DIR)/dnspbr.ru.lmo || { \
		echo "ERROR: Failed to compile Russian translation"; \
		exit 1; \
	}
	@test -s $(PKG_BUILD_DIR)/dnspbr.ru.lmo || { \
		echo "ERROR: Compiled translation is empty"; \
		exit 1; \
	}
endef

define Package/luci-i18n-dnspbr-ru/install
	$(INSTALL_DIR) $(1)/usr/lib/lua/luci/i18n
	$(INSTALL_DATA) \
		$(PKG_BUILD_DIR)/dnspbr.ru.lmo \
		$(1)/usr/lib/lua/luci/i18n/dnspbr.ru.lmo
endef

$(eval $(call BuildPackage,luci-i18n-dnspbr-ru))
