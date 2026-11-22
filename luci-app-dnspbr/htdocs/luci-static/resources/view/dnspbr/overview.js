'use strict';
'require view';
'require form';
'require network';
'require uci';
'require rpc';
'require ui';

var callServiceList = rpc.declare({
	object: 'service',
	method: 'list',
	params: ['name'],
	expect: { '': {} }
});

var callInitAction = rpc.declare({
	object: 'luci',
	method: 'setInitAction',
	params: ['name', 'action'],
	expect: { result: false }
});

return view.extend({
	load: function() {
		return Promise.all([
			network.getDevices(),
			callServiceList('dnspbr').catch(function() { return {}; }),
			uci.load('dnspbr')
		]);
	},

	render: function(data) {
		var devices = data[0] || [];
		var serviceInfo = data[1] || {};
		var instances = (serviceInfo.dnspbr && serviceInfo.dnspbr.instances) ? serviceInfo.dnspbr.instances : {};

		var runningCount = 0;
		var totalCount = 0;
		for (var id in instances) {
			if (!Object.prototype.hasOwnProperty.call(instances, id)) continue;
			totalCount++;
			if (instances[id] && instances[id].running)
				runningCount++;
		}

		var isAnyRunning = runningCount > 0;

		var runAction = function(action) {
			var labels = {
				start: _('Starting dnspbr...'),
				stop: _('Stopping dnspbr...'),
				restart: _('Restarting dnspbr...')
			};
			ui.showModal(_('Service action'), [
				E('p', { class: 'spinning' }, labels[action] || _('Executing action...'))
			]);
			callInitAction('dnspbr', action).then(function() {
				window.setTimeout(function() {
					window.location.reload();
				}, 1000);
			}).catch(function(e) {
				ui.hideModal();
				ui.addNotification(null, E('p', _('Failed to execute action: ') + (e.message || e)), 'error');
			});
		};

		var statusBox = E('div', {
			style: 'display: flex; align-items: center; justify-content: space-between; flex-wrap: wrap; gap: 10px; margin-top: 15px; padding: 10px 12px; background: #f5f5f5; border-left: 4px solid ' + (isAnyRunning ? '#28a745' : '#dc3545') + ';'
		}, [
			E('div', {}, [
				E('strong', {}, _('Service Status')),
				E('span', {}, ': '),
				E('span', {
					style: 'color: ' + (isAnyRunning ? '#28a745' : '#dc3545') + '; font-weight: bold;'
				}, isAnyRunning ? _('Running (%d/%d)').format(runningCount, Math.max(runningCount, totalCount)) : _('Stopped'))
			]),
			E('div', { style: 'display: flex; gap: 6px;' }, [
				E('button', {
					class: 'btn cbi-button-action',
					disabled: isAnyRunning ? true : null,
					click: function() { runAction('start'); }
				}, _('Start')),
				E('button', {
					class: 'btn cbi-button-reset',
					disabled: !isAnyRunning ? true : null,
					click: function() { runAction('stop'); }
				}, _('Stop')),
				E('button', {
					class: 'btn',
					click: function() { runAction('restart'); }
				}, _('Restart'))
			])
		]);

		var m = new form.Map('dnspbr', _('DNSPBR Settings'),
			_('Multi-instance configuration for dnspbr daemon and policy routing.')
		);

		var headerSec = m.section(form.NamedSection, '_status', 'dnspbr');
		headerSec.render = function() {
			return statusBox;
		};

		var s = m.section(form.TypedSection, 'instance', _('Instances'),
			_('Each instance runs an independent DNS listener, nftables set/table, and routing table.')
		);
		s.anonymous = false;
		s.addremove = true;
		s.addbtntitle = _('Add instance');

		var o;

		o = s.option(form.Flag, 'enabled', _('Enabled'),
			_('Enable or disable this dnspbr instance.')
		);
		o.default = o.enabled;
		o.rmempty = false;

		o = s.option(form.Value, 'listen', _('Listen IP'),
			_('IP address for the daemon instance to listen on.')
		);
		o.datatype = 'ipaddr';
		o.default = '127.0.0.1';
		o.rmempty = false;

		o = s.option(form.Value, 'port', _('Listen Port'),
			_('UDP port for the daemon instance to listen on.')
		);
		o.datatype = 'port';
		o.default = '5353';
		o.rmempty = false;

		o = s.option(form.Value, 'upstream', _('Upstream IP'),
			_('Upstream DNS server IP address.')
		);
		o.datatype = 'ipaddr';
		o.default = '9.9.9.10';
		o.rmempty = false;

		o = s.option(form.Value, 'upstream_port', _('Upstream Port'),
			_('Upstream DNS server port.')
		);
		o.datatype = 'port';
		o.default = '53';
		o.rmempty = false;

		o = s.option(form.Value, 'range', _('Fake DNS Subnet'),
			_('Target subnet for Fake DNS policy routing (e.g. 10.64.0.0/16).')
		);
		o.datatype = 'cidr';
		o.rmempty = false;

		o = s.option(form.Value, 'table', _('nftables Table'),
			_('nftables table name (e.g. dnsmap_wg_ru).')
		);
		o.rmempty = false;

		o = s.option(form.Value, 'route_table', _('Routing Table'),
			_('Policy routing table ID or name (e.g. 101).')
		);
		o.rmempty = false;

		o = s.option(form.Value, 'mark', _('Firewall Mark'),
			_('Firewall mark value (e.g. 0x101).')
		);
		o.rmempty = false;

		o = s.option(form.Value, 'interface', _('Interface'),
			_('Optional network interface for policy routing (e.g. wg0). Leave empty if not bound to an interface.')
		);
		o.value('', _('-- none --'));
		for (var i = 0; i < devices.length; i++) {
			var devName = devices[i].getName();
			if (devName && devName !== 'lo')
				o.value(devName, devName);
		}
		o.rmempty = true;

		o = s.option(form.Value, 'workers', _('Workers'),
			_('Number of worker threads for this instance.')
		);
		o.datatype = 'uinteger';
		o.default = '2';
		o.rmempty = false;

		o = s.option(form.Flag, 'verbose', _('Verbose Logging'),
			_('Enable detailed debug output.')
		);
		o.default = o.disabled;
		o.rmempty = true;

		return m.render();
	}
});
