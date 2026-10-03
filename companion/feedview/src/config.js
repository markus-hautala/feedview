// Connection settings.

function getConfigFields() {
	return [
		{
			type: 'static-text',
			id: 'info',
			width: 12,
			label: 'FeedView',
			value:
				"Enter the address shown in FeedView's Remote panel (press R in FeedView). Leave the PIN empty unless " +
				'"Require PIN" is ticked there.',
		},
		{
			type: 'textinput',
			id: 'host',
			label: 'FeedView computer (IP address or name, or the whole address from FeedView)',
			width: 8,
			default: '127.0.0.1',
		},
		{ type: 'number', id: 'port', label: 'Port', width: 4, min: 1, max: 65535, default: 8080 },
		{
			type: 'textinput',
			id: 'pin',
			label: 'PIN (only if FeedView requires one)',
			width: 4,
			default: '',
			regex: '/^(\\d{4,8})?$/',
		},
		{
			type: 'number',
			id: 'poll',
			label: 'Update interval (ms)',
			width: 4,
			min: 100,
			max: 5000,
			default: 500,
		},
	]
}

function int(v, min, max, fallback) {
	const n = Number.parseInt(v, 10)
	return Number.isFinite(n) ? Math.min(max, Math.max(min, n)) : fallback
}

// The host field also takes an address copied from FeedView, e.g. "http://192.168.1.20:8080/";
// its port then wins over the port field.
function normalizeConfig(c) {
	const cfg = c || {}
	let host = String(cfg.host || '')
		.trim()
		.replace(/^https?:\/\//i, '')
		.replace(/[/#?].*$/, '')
	let port = int(cfg.port, 1, 65535, 8080)
	const withPort = host.match(/^([^:[\]]+|\[[^\]]+\]):(\d{1,5})$/)
	if (withPort) {
		host = withPort[1]
		port = int(withPort[2], 1, 65535, port)
	}
	host = host.replace(/^\[(.*)\]$/, '$1')
	return {
		host: host || '127.0.0.1',
		port,
		pin: String(cfg.pin ?? '').trim(),
		poll: int(cfg.poll, 100, 5000, 500),
	}
}

module.exports = { getConfigFields, normalizeConfig }
