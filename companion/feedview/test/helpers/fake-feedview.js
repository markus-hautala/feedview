// A stand-in for FeedView's web remote API (same routes, auth rules and answers), recording
// every request, so the module can be tested without FeedView. Tests against the real
// FeedView are in feedview.test.js.
const http = require('node:http')

function baseState() {
	return {
		app: { name: 'FeedView', version: '1.0.0', host: 'STUDIO-PC', uptime: 12.5, frameGapMs: 17, runtimeLoaded: true },
		source: {
			name: 'CAM-PC (Camera 1)',
			listed: true,
			connected: true,
			hasPicture: true,
			signalLost: false,
			frame: 100,
			video: {
				width: 1920,
				height: 1080,
				rateN: 50,
				rateD: 1,
				fps: 49.96,
				secondsSinceFrame: 0.02,
				frames: 5000,
				dropped: 2,
			},
			audio: { sampleRate: 48000, channels: 4 },
		},
		sources: ['CAM-PC (Camera 1)', 'GFX-PC (Program)'],
		audio: {
			volume: 40,
			muted: false,
			firstChannel: 1,
			device: true,
			bufferMs: 61.4,
			system: true,
			output: '{out-a}',
			outputs: [
				{ id: '{out-a}', name: 'Speakers (Realtek(R) Audio)', default: true },
				{ id: '{out-b}', name: 'DELL U2720Q (HDMI)', default: false },
			],
		},
		displays: [
			{ number: 1, name: 'Laptop', x: 0, y: 0, w: 1920, h: 1080, refresh: 60, scale: 1.25, primary: true },
			{ number: 2, name: 'Projector', x: 1920, y: 0, w: 1920, h: 1080, refresh: 60, scale: 1, primary: false },
		],
		output: {
			fullscreen: true,
			wantFullscreen: true,
			waiting: false,
			identify: false,
			target: { number: 2, name: 'Projector', label: '2: Projector (1920x1080)', connected: true },
			window: { x: 1920, y: 0, w: 1920, h: 1080, display: 2 },
			onTop: true,
			picture: true,
			controls: { visible: false, panel: '', cursor: false },
			fade: { active: false, waiting: false, from: '' },
		},
		settings: {
			startFullscreen: true,
			showInfo: false,
			cleanOutput: true,
			alwaysOnTop: true,
			silenceNotifications: true,
			fadeMs: 500,
			extraIps: '',
		},
		notifications: {
			supported: true,
			permitted: true,
			silenced: true,
			permissionPending: false,
			message: 'Notifications are off while FeedView runs',
		},
		remote: { pinRequired: false, urls: ['http://192.168.1.20:8080'], clients: [] },
		notices: [{ id: 7, time: Date.UTC(2026, 9, 3, 12, 30, 15), text: 'Fullscreen on 2: Projector (1920x1080)' }],
	}
}

const SETTING_KEYS = {
	clean_output: 'cleanOutput',
	start_fullscreen: 'startFullscreen',
	show_info: 'showInfo',
	always_on_top: 'alwaysOnTop',
	silence_notifications: 'silenceNotifications',
}

class FakeFeedView {
	constructor() {
		this.state = baseState()
		this.pin = '4821'
		this.pinRequired = false
		this.requests = [] // {method, path, params, headers}
		this.refuse = null // {action, status, message}: refuse the next such command
	}

	async start(port = 0) {
		this.server = http.createServer((req, res) => this.#handle(req, res))
		await new Promise((r) => this.server.listen(port, '127.0.0.1', r))
		this.port = this.server.address().port
		return this.port
	}

	async stop() {
		this.server.closeAllConnections()
		await new Promise((r) => this.server.close(r))
	}

	commands(action) {
		return this.requests.filter((r) => r.method === 'POST' && (!action || r.path === `/api/${action}`))
	}

	#send(res, status, body) {
		res.writeHead(status, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' })
		res.end(JSON.stringify(body))
	}

	#handle(req, res) {
		let raw = ''
		req.on('data', (d) => (raw += d))
		req.on('end', () => {
			const url = new URL(req.url, 'http://x')
			const params = Object.fromEntries(new URLSearchParams(raw || url.search))
			this.requests.push({ method: req.method, path: url.pathname, params, headers: req.headers })
			if (url.pathname === '/api/ping')
				return this.#send(res, 200, {
					app: 'FeedView',
					version: '1.0.0',
					host: 'STUDIO-PC',
					pinRequired: this.pinRequired,
				})
			// Same rules as FeedView: with a PIN every call needs it; without, commands still need the header.
			const given = req.headers['x-feedview-pin']
			if (this.pinRequired && given !== this.pin)
				return this.#send(res, 401, { ok: false, error: 'pin', message: given ? 'Wrong PIN' : 'PIN required' })
			if (req.method === 'POST' && given === undefined)
				return this.#send(res, 403, {
					ok: false,
					error: 'header',
					message: 'Commands need the X-FeedView-Pin header (any value).',
				})
			if (req.method === 'GET' && url.pathname === '/api/state') return this.#send(res, 200, this.state)
			if (req.method !== 'POST') return this.#send(res, 404, { ok: false, message: 'Not found' })
			const action = url.pathname.replace('/api/', '')
			if (this.refuse && this.refuse.action === action) {
				const r = this.refuse
				this.refuse = null
				return this.#send(res, r.status, { ok: false, message: r.message, state: this.state })
			}
			const message = this.#apply(action, params)
			if (message === null) return this.#send(res, 404, { ok: false, message: 'Unknown command' })
			return this.#send(res, 200, { ok: true, message, state: this.state })
		})
	}

	// A few effects, so feedbacks and variables can be seen changing.
	#apply(action, p) {
		const s = this.state
		const sw = (v, cur) => (v === 'toggle' ? !cur : v === '1')
		switch (action) {
			case 'source':
				s.source.name = p.name
				return p.name ? `Showing ${p.name}` : 'None (black output)'
			case 'fullscreen':
				s.output.fullscreen = sw(p.on, s.output.fullscreen)
				return 'ok'
			case 'display':
				s.output.window.display = Number(p.number)
				s.output.fullscreen = p.fullscreen === '1'
				return 'ok'
			case 'identify':
				s.output.identify = sw(p.on, s.output.identify)
				return 'ok'
			case 'mute':
				s.audio.muted = sw(p.on, s.audio.muted)
				return 'ok'
			case 'volume':
				s.audio.volume = /^[+-]/.test(p.value) ? s.audio.volume + Number(p.value) : Number(p.value)
				return 'ok'
			case 'audio-output':
				s.audio.output = p.id
				return 'ok'
			case 'audio-pair':
				s.audio.firstChannel = Number(p.first)
				return 'ok'
			case 'controls':
				s.output.controls.visible = p.on === '1'
				return 'ok'
			case 'settings':
				for (const [k, key] of Object.entries(SETTING_KEYS)) if (k in p) s.settings[key] = sw(p[k], s.settings[key])
				if ('fade_ms' in p) s.settings.fadeMs = Number(p.fade_ms)
				if ('extra_ips' in p) s.settings.extraIps = p.extra_ips
				return 'Settings saved'
			case 'reconnect':
			case 'allow-notification-control':
				return 'ok'
			default:
				return null
		}
	}
}

module.exports = { FakeFeedView, baseState }
