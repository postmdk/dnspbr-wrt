# dnspbr (DNS Policy-Based Routing для OpenWrt)

`dnspbr` — это высокопроизводительный, легковесный сетевой демон на C++ и пакет для OpenWrt, предназначенный для маршрутизации на основе политик (Policy-Based Routing, PBR) с динамическим отслеживанием DNS-запросов.

Демон перехватывает DNS-запросы для целевых доменов, отображает (маппит) реальные IP-адреса в виртуальные пулы подсетей, маркирует соответствующий трафик метками межсетевого экрана `nftables` (`fwmark`) и направляет пакеты через выделенные сетевые интерфейсы (например, WireGuard, AmneziaWG, OpenVPN или WAN-шлюзы OpenWrt).

## Основные возможности

* **Мультиинстанс-архитектура:** одновременный запуск нескольких независимых экземпляров `dnspbr` под управлением `procd` — каждый со своим портом прослушивания, подсетью Fake DNS, таблицей/картой `nftables`, меткой `fwmark`, таблицей маршрутизации и целевым интерфейсом.
* **Высокая производительность и низкая задержка:** написан на нативном C++ с поддержкой многопоточности (`libpthread`).
* **Динамическое отображение IP (Fake DNS):** автоматически перехватывает DNS-ответы и сопоставляет реальные IP-адреса с заранее заданными внутренними подсетями (`range`).
* **Интеграция с `nftables`:** динамически создаёт отдельные таблицы DNAT и mangle для каждого экземпляра (`dnsmap_<instance>` и `mangle_dnsmap_<instance>`) для маркировки трафика с помощью `fwmark`.
* **Поддержка современных версий OpenWrt:** автоматическое определение chroot/jail-каталогов `dnsmasq` (`/tmp/dnsmasq*d/`) и генерация правил пересылки доменов для каждого экземпляра.
* **Веб-интерфейс LuCI (`luci-app-dnspbr`):** полноценный интерфейс управления несколькими экземплярами, мониторинга состояния каждого демона и редактирования списков доменов по экземплярам.
* **Сборки под разные архитектуры и версии OpenWrt:** готовые пакеты для `aarch64_cortex-a53` и `mipsel_24kc` с поддержкой OpenWrt `24.10.x` (`.ipk`) и `25.12.x` (`.apk`).

## Архитектура прохождения пакетов

Ниже приведена концептуальная схема обработки DNS-запросов и сетевого трафика через несколько экземпляров `dnspbr` и стек маршрутизации ядра Linux.

```
[ Клиентское устройство ]
       │
       │ 1. DNS-запрос (например, domain-a.com / domain-b.com)
       ▼
[ dnsmasq ]
       ├──(server=/domain-a.com/127.0.0.1#5353)──► [ dnspbr (awg0) ] (Порт 5353)
       └──(server=/domain-b.com/127.0.0.1#5453)──► [ dnspbr (awg1) ] (Порт 5453)
                                                           │
                                                           │ 2. Вышестоящий DNS (Порт 5354)
                                                           ▼
                                                   [ Upstream DNS / DoH ]
                                                           │
[ Клиентское устройство ] ◄───(3. Подменённый IP: 10.64.0.X / 10.32.0.X)
       │
       │ 4. HTTP/TLS-трафик на подменённый IP
       ▼
[ Ядро Linux (nftables) ]
       ├──(Совпадение 10.64.0.0/16 в dnsmap_awg0)──► Метка 0x101 ──► Таблица 101 ──► [ awg0 ]
       └──(Совпадение 10.32.0.0/16 в dnsmap_awg1)──► Метка 0x102 ──► Таблица 102 ──► [ awg1 ]
```

### Подробные этапы работы:

1. **Перехват DNS:** клиент отправляет запрос к целевому домену. `dnsmasq` перенаправляет совпадающие домены на соответствующий экземпляр `dnspbr` (например, порт `5353` для `awg0` или `5453` для `awg1`).
2. **Разрешение через вышестоящий DNS:** `dnspbr` пересылает запрос на указанный вышестоящий DNS-сервер (например, на порт `5354`).
3. **Отображение в подсеть:** получив реальный IP-адрес от вышестоящего сервера, `dnspbr` сопоставляет его с виртуальным внутренним пулом адресов экземпляра (например, `10.64.0.0/16` или `10.32.0.0/16`) и возвращает клиенту этот синтетический IP.
4. **Маркировка пакетов:** когда клиент отправляет IP-трафик в виртуальную подсеть, таблица `nftables` соответствующего экземпляра выполняет обратную трансляцию адреса назначения в реальный IP и помечает пакеты заданной меткой `fwmark` (например, `0x101` или `0x102`).
5. **Маршрутизация по политикам:** правила маршрутизации ядра Linux (`ip rule`) проверяют `fwmark` и направляют трафик в выделенную таблицу маршрутизации и целевой интерфейс экземпляра.

## Конфигурация и интеграция с Dnsmasq

### Конфигурация UCI (`/etc/config/dnspbr`)

Основной конфигурационный файл расположен по пути `/etc/config/dnspbr`. Каждая секция `config instance` описывает изолированный экземпляр демона `dnspbr`, управляемый через `procd`.

> **Важно:** при запуске нескольких экземпляров убедитесь, что параметры `port`, `range`, `table`, `route_table` и `mark` уникальны для каждого включённого экземпляра, чтобы избежать конфликтов портов и пересечения правил маршрутизации.

```uci
config instance 'awg0'
    option enabled '1'
    option listen '127.0.0.1'
    option port '5353'
    option upstream '127.0.0.1'
    option upstream_port '5354'
    option range '10.64.0.0/16'
    option table 'dnsmap_awg0'
    option route_table '101'
    option mark '0x101'
    option interface 'awg0'
    option workers '2'
    option verbose '0'

config instance 'awg1'
    option enabled '1'
    option listen '127.0.0.1'
    option port '5453'
    option upstream '127.0.0.1'
    option upstream_port '5354'
    option range '10.32.0.0/16'
    option table 'dnsmap_awg1'
    option route_table '102'
    option mark '0x102'
    option interface 'awg1'
    option workers '2'
    option verbose '0'
```

### Справочник параметров

| Параметр | Тип | По умолчанию | Описание |
| ----- | ----- | ----- | ----- |
| `enabled` | `boolean` | `0` | Включает (`1`) или отключает (`0`) данный экземпляр `dnspbr`. |
| `listen` | `ipaddr` | `127.0.0.1` | Локальный IP-адрес, на котором экземпляр принимает входящие DNS-запросы. |
| `port` | `port` | `5353` | Локальный UDP-порт, на котором экземпляр принимает DNS-трафик от `dnsmasq`. |
| `upstream` | `ipaddr` | `9.9.9.10` | IP-адрес вышестоящего DNS-резолвера (например, `127.0.0.1` или SmartDNS). |
| `upstream_port` | `port` | `53` | Порт вышестоящего DNS-сервера для разрешения реальных публичных IP-адресов. |
| `range` | `cidr` | `10.30.0.0/16` | Виртуальный диапазон подсети, выделенный для отображения IP-адресов данного экземпляра. |
| `table` | `string` | `dnsmap_<name>` | Имя выделенной таблицы `nftables`, создаваемой для данного экземпляра. |
| `route_table` | `string` | `101` | Номер или имя таблицы маршрутизации по политикам для данного экземпляра. |
| `mark` | `hex` | `0x101` | Шестнадцатеричная метка межсетевого экрана (`fwmark`), назначаемая трафику. |
| `interface` | `string` | *(опционально)* | Целевой сетевой интерфейс для маршрутизации маркированного трафика (например, `awg0`, `wg0`). |
| `workers` | `integer` | `2` | Количество рабочих потоков для параллельной обработки DNS-запросов. |
| `verbose` | `boolean` | `0` | Включает (`1`) подробное отладочное журналирование для данного экземпляра. |

### Настройка Dnsmasq

`dnspbr` бесшовно интегрируется с `dnsmasq`, динамически добавляя правила пересылки доменов по экземплярам в активные каталоги конфигурации (включая chroot/jail-окружения вида `/tmp/dnsmasq*d/`).

Отключите защиту от DNS rebind, чтобы `dnsmasq` принимал синтетические приватные IP-адреса (`10.x.x.x`) от `dnspbr`:

```bash
uci set dhcp.@dnsmasq[0].rebind_protection='0'
uci commit dhcp
service dnsmasq restart
```

Каждый экземпляр использует собственный файл конфигурации в `/etc/dnspbr/dnsmasq/<instance>.conf` (например, `/etc/dnspbr/dnsmasq/awg0.conf` и `/etc/dnspbr/dnsmasq/awg1.conf`), направляя домены на соответствующие `listen` IP и `port` экземпляра:

```text
# /etc/dnspbr/dnsmasq/awg0.conf
server=/example.com/127.0.0.1#5353
```

```text
# /etc/dnspbr/dnsmasq/awg1.conf
server=/another-example.org/127.0.0.1#5453
```

1. При запуске или перезагрузке `/etc/init.d/dnspbr` автоматически копирует активные файлы `*.conf` из `/etc/dnspbr/dnsmasq/` в рабочий каталог `dnsmasq` (`/tmp/dnsmasq*d/dnspbr_<instance>.conf`) и перезапускает `dnsmasq`.
2. При отключении экземпляра (`enabled '0'`) его таблицы `nftables`, правила маршрутизации и активный файл пересылки `dnsmasq` автоматически удаляются.
3. Установите пакет `luci-app-dnspbr` для управления экземплярами и списками доменов каждого экземпляра напрямую через веб-интерфейс OpenWrt LuCI.

## Потребление ресурсов и аппаратные требования

* **Потребление ОЗУ:** ~12–13 МБ Resident Set Size (RSS) на один запущенный экземпляр при средней нагрузке.
* **Нагрузка на ЦП:** ~2% процессорного времени на роутерах начального и среднего уровня (ARM/MIPS) при активных запросах.
* **Рекомендации по памяти:** на устройствах с объёмом ОЗУ $\le 128 \text{ МБ}$ рекомендуется ограничивать количество одновременно запущенных экземпляров и задавать `workers '1'` для минимизации потребления памяти и предотвращения срабатывания Out-Of-Memory (OOM) в ядре.

## Установка

### Готовые бинарные пакеты

Скачайте актуальный релиз для вашей архитектуры и версии OpenWrt со страницы [GitHub Releases](https://github.com/postmdk/dnspbr-wrt/releases).

#### OpenWrt 24.10.x (пакеты `.ipk`):

```bash
opkg update
opkg install dnspbr-<pkg_ver>-<arc>_24.10.8.ipk
opkg install luci-app-dnspbr-<pkg_ver>_all.ipk
```

#### OpenWrt 25.12.x (пакеты `.apk`):

```bash
apk add --allow-untrusted dnspbr-<pkg_ver>-<arc>_25.12.6.apk
apk add --allow-untrusted luci-app-dnspbr-<pkg_ver>.apk
```

## Управление службой

Управление демоном и проверка запущенных экземпляров выполняются стандартными командами инициализации OpenWrt и `ubus`:

```bash
# Включить автозапуск службы при загрузке
/etc/init.d/dnspbr enable

# Запустить все включённые экземпляры
/etc/init.d/dnspbr start

# Проверить запущенные экземпляры через ubus
ubus call service list '{"name":"dnspbr"}'

# Перезапустить службу и обновить правила dnsmasq
/etc/init.d/dnspbr restart
```

## Опционально: принудительный перехват DNS (DNS hijacking) через межсетевой экран

### Перенаправление DNS-трафика:

```bash
uci -q del firewall.dns_int
uci set firewall.dns_int="redirect"
uci set firewall.dns_int.name="Intercept-DNS"
uci set firewall.dns_int.family="any"
uci set firewall.dns_int.proto="tcp udp"
uci set firewall.dns_int.src="lan"
uci set firewall.dns_int.src_dport="53"
uci set firewall.dns_int.target="DNAT"
uci commit firewall
service firewall restart
service dnspbr restart
```

### Примечание

Перезапуск межсетевого экрана (`firewall`) очищает правила DNAT и маркировки, поэтому после него необходимо также перезапустить службу `dnspbr`.

См. также: [dnsproxy](https://openwrt.org/docs/guide-user/services/dns/dot_dnsmasq_dnsproxy) — настройка DoH/DoH3, DoT, DoQ и DNSCrypt совместно с Dnsmasq и dnsproxy.

## Сборка из исходного кода

Для компиляции `dnspbr` с использованием официального OpenWrt SDK:

1. Добавьте репозиторий `dnspbr` в конфигурацию фидов SDK:
   ```bash
   echo "src-git dnspbr https://github.com/postmdk/dnspbr-wrt.git" >> feeds.conf.default
   ./scripts/feeds update -a
   ./scripts/feeds install -a
   ```

2. Сконфигурируйте и скомпилируйте пакеты:
   ```bash
   make defconfig
   make package/dnspbr/compile V=s
   make package/luci-app-dnspbr/compile V=s
   ```

3. Собранные пакеты будут находиться в каталоге:
   `bin/packages/<arch>/dnspbr/`

## См. также

* [dnspbr для Linux](https://github.com/postmdk/dnspbr.git)

## Лицензия

Проект распространяется под лицензией [MIT License](LICENSE).
