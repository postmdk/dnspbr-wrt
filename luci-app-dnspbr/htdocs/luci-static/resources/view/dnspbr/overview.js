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
            callServiceList('dnspbr').catch(function() { return {}; })
        ]);
    },

    render: function(data) {
        var devices = data[0];
        var serviceInfo = data[1];
        
        var isRunning = false;
        try {
            var instances = serviceInfo.dnspbr.instances;
            for (var id in instances) {
                if (instances[id].running) {
                    isRunning = true;
                    break;
                }
            }
        } catch (e) {}

        var m, s, o;

        // Обработчики кнопок
        window.handleServiceAction = function(action) {
            ui.showModal(_('Action in progress...'), [
                E('p', { class: 'spinning' }, _('Please wait while the service is being ' + action + 'ed...'))
            ]);
            callInitAction('dnspbr', action).then(function() {
                window.setTimeout(function() {
                    window.location.reload();
                }, 1000);
            }).catch(function(e) {
                ui.addNotification(null, E('p', _('Failed to execute action: ' + e)));
                ui.hideModal();
            });
        };

        var statusHtml = '<div style="display: flex; align-items: center; justify-content: space-between; margin-top: 15px; padding: 10px; background: #f5f5f5; border-left: 4px solid ' + (isRunning ? '#28a745' : '#dc3545') + ';">' +
            '<div><strong>Service Status:</strong> ' + (isRunning ? '<span style="color: #28a745; font-weight: bold;">Running</span>' : '<span style="color: #dc3545; font-weight: bold;">Stopped</span>') + '</div>' +
            '<div>' +
            '<button class="btn cbi-button-action" style="margin-right: 5px; background-color: ' + (isRunning ? '#6c757d' : '#28a745') + '; color: white;" ' + (isRunning ? 'disabled' : 'onclick="handleServiceAction(\'start\')"') + '>Start</button>' +
            '<button class="btn cbi-button-reset" style="background-color: ' + (!isRunning ? '#6c757d' : '#dc3545') + '; color: white;" ' + (!isRunning ? 'disabled' : 'onclick="handleServiceAction(\'stop\')"') + '>Stop</button>' +
            '</div>' +
            '</div>';

        m = new form.Map('dnspbr', _('DNSPBR Settings'), 
            _('Configuration parameters for dnspbr daemon and policy routing.') + statusHtml
        );

        s = m.section(form.NamedSection, 'global', 'dnspbr', _('General Settings'));
        s.optional = false;

        o = s.option(form.Flag, 'enabled', _('Enabled'), _('Enable or disable dnspbr service.'));
        o.rmempty = false;

        o = s.option(form.Value, 'address', _('Listen IP'), _('IP address for the daemon to listen on.'));
        o.datatype = 'ipaddr';
        o.rmempty = false;

        o = s.option(form.Value, 'port', _('Listen Port'), _('Port for the daemon to listen on.'));
        o.datatype = 'port';
        o.rmempty = false;

        o = s.option(form.Value, 'upstream', _('Upstream IP'), _('Upstream DNS server IP address.'));
        o.datatype = 'ipaddr';
        o.rmempty = false;

        o = s.option(form.Value, 'upstream_port', _('Upstream Port'), _('Upstream DNS server port.'));
        o.datatype = 'port';
        o.rmempty = false;

        o = s.option(form.Value, 'range', _('Fake DNS Subnet'), _('Target subnet for Fake DNS policy routing (e.g. 10.30.0.0/16).'));
        o.datatype = 'cidr';
        o.rmempty = false;

        o = s.option(form.Value, 'mark', _('Firewall Mark'), _('Firewall mark hex value (e.g. 0x40).'));
        o.rmempty = false;

        o = s.option(form.Value, 'tunnel_iface', _('Interface'), _('Network interface name for policy routing (e.g. awg0).'));
        
        if (devices && devices.length > 0) {
            for (var i = 0; i < devices.length; i++) {
                var devName = devices[i].getName();
                if (devName && devName !== 'lo') {
                    o.value(devName, devName);
                }
            }
        }
        o.rmempty = false;

        o = s.option(form.Value, 'workers', _('Workers'), _('Number of worker threads.'));
        o.datatype = 'uinteger';
        o.rmempty = false;

        o = s.option(form.Flag, 'verbose', _('Verbose Logging'), _('Enable detailed debug output.'));
        o.rmempty = true;

        return m.render();
    }
});
