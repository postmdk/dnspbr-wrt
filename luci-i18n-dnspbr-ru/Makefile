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
	# Копируем все .po файлы из папки po/ru в корень сборки
	find ./po -name "*.po" -exec $(CP) {} $(PKG_BUILD_DIR)/ \;
endef

define Build/Configure
endef

define Build/Compile
	# Компилируем каждый .po в .lmo и падаем с ошибкой, если файла нет
	for po in $(PKG_BUILD_DIR)/*.po; do \
		[ -f "$$po" ] || exit 1; \
		po2lmo "$$po" "$$po.lmo"; \
	done
endef

define Package/luci-i18n-dnspbr-ru/install
	$(INSTALL_DIR) $(1)/usr/lib/lua/luci/i18n
	# Устанавливаем скомпилированный файл под именем dnspbr.ru.lmo
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/*.lmo $(1)/usr/lib/lua/luci/i18n/dnspbr.ru.lmo
endef

$(eval $(call BuildPackage,luci-i18n-dnspbr-ru))
