// Variables: what FeedView reports, for button texts and triggers.
const { splitSourceName } = require('./choices')

const VARIABLES = [
	{ variableId: 'connected', name: 'Connected to FeedView (true/false)' },
	{ variableId: 'host', name: 'Name of the FeedView computer' },
	{ variableId: 'version', name: 'FeedView version' },
	{ variableId: 'source', name: 'Source on the output (full NDI name, empty = None)' },
	{ variableId: 'source_name', name: 'Source on the output (short name, "None" for black)' },
	{ variableId: 'source_machine', name: 'Computer sending the source' },
	{ variableId: 'signal', name: 'Signal: Live, Connecting, Waiting for video, No video, Source offline, None' },
	{ variableId: 'resolution', name: 'Video resolution, e.g. 1920x1080' },
	{ variableId: 'frame_rate', name: 'Video frame rate, e.g. 50p' },
	{ variableId: 'fps', name: 'Measured frames per second' },
	{ variableId: 'audio_format', name: 'Audio format, e.g. 48 kHz, 2 ch' },
	{ variableId: 'audio_channels', name: 'Audio channels playing, e.g. Ch 1-2' },
	{ variableId: 'buffer_ms', name: 'Audio buffer (ms)' },
	{ variableId: 'dropped_frames', name: 'Dropped video frames' },
	{ variableId: 'volume', name: 'Volume 0-100 (the system volume on Windows)' },
	{ variableId: 'muted', name: 'Muted (true/false)' },
	{ variableId: 'sound_output', name: "The computer's sound output" },
	{ variableId: 'fullscreen', name: 'Fullscreen (true/false)' },
	{ variableId: 'display', name: 'Display number FeedView is on' },
	{ variableId: 'display_name', name: 'Name of the display FeedView is on' },
	{ variableId: 'target_display', name: 'Chosen display (e.g. "2: DELL U2720Q (2560x1440)")' },
	{ variableId: 'waiting_for_display', name: 'Disconnected display FeedView is waiting for (empty if none)' },
	{ variableId: 'identify', name: 'Display numbers showing (true/false)' },
	{ variableId: 'controls_visible', name: "FeedView's own controls on its screen (true/false)" },
	{ variableId: 'fading', name: 'A fade between sources is running (true/false)' },
	{ variableId: 'fade_ms', name: 'Fade between sources (ms, 0 = cut)' },
	{ variableId: 'clean_output', name: 'Setting: clean output (true/false)' },
	{ variableId: 'start_fullscreen', name: 'Setting: start in fullscreen (true/false)' },
	{ variableId: 'show_info', name: 'Setting: always show the info line (true/false)' },
	{ variableId: 'always_on_top', name: 'Setting: always on top (true/false)' },
	{ variableId: 'silence_notifications', name: 'Setting: silence notifications (true/false)' },
	{ variableId: 'notifications', name: 'OS notifications status' },
	{ variableId: 'extra_ips', name: 'Extra discovery IPs' },
	{ variableId: 'sources_count', name: 'NDI sources found' },
	{ variableId: 'selected', name: 'Selected for Take (empty if nothing)' },
	{ variableId: 'last_event', name: "FeedView's latest event" },
	{ variableId: 'last_event_time', name: "Time of FeedView's latest event (HH:MM:SS)" },
	{ variableId: 'remote_url', name: "FeedView's web remote address" },
]

function rate(n, d) {
	if (!n || !d) return ''
	const f = n / d
	return (Math.abs(f - Math.round(f)) < 0.005 ? String(Math.round(f)) : f.toFixed(2)) + 'p'
}

/** What the output shows, in words. */
function signalText(state) {
	const s = state.source
	if (!s.name) return 'None'
	if (s.signalLost) return s.connected ? 'No video' : 'Source offline'
	if (!s.hasPicture) return s.connected ? 'Waiting for video' : 'Connecting'
	return 'Live'
}

function time(ms) {
	const d = new Date(ms)
	const p = (v) => String(v).padStart(2, '0')
	return `${p(d.getHours())}:${p(d.getMinutes())}:${p(d.getSeconds())}`
}

/**
 * Variable values for a FeedView state (null = not connected).
 * @param {object|null} state
 * @param {{label: string}|null} selection what's selected for Take
 */
function variableValues(state, selection) {
	const selected = selection ? selection.label : ''
	if (!state) {
		const v = { connected: false, selected }
		for (const def of VARIABLES) if (!(def.variableId in v)) v[def.variableId] = ''
		v.signal = 'Not connected'
		return v
	}
	const src = state.source
	const out = state.output
	const { name, machine } = splitSourceName(src.name)
	const display = (state.displays || []).find((d) => d.number === out.window.display)
	const playing = src.audio.channels === 1 ? 'Mono' : `Ch ${state.audio.firstChannel}-${state.audio.firstChannel + 1}`
	const outputs = state.audio.outputs || []
	const output = outputs.find((o) => o.id === state.audio.output)
	const notices = state.notices || []
	const last = notices.length ? notices[notices.length - 1] : null
	return {
		connected: true,
		host: state.app.host,
		version: state.app.version,
		source: src.name,
		source_name: src.name ? name : 'None',
		source_machine: machine,
		signal: signalText(state),
		resolution: src.video.width ? `${src.video.width}x${src.video.height}` : '',
		frame_rate: rate(src.video.rateN, src.video.rateD),
		fps: src.video.width ? Math.round(src.video.fps * 10) / 10 : 0,
		audio_format: src.audio.sampleRate ? `${src.audio.sampleRate / 1000} kHz, ${src.audio.channels} ch` : '',
		audio_channels: playing,
		buffer_ms: Math.round(state.audio.bufferMs || 0),
		dropped_frames: src.video.dropped || 0,
		volume: state.audio.volume,
		muted: !!state.audio.muted,
		sound_output: output ? output.name : '',
		fullscreen: !!out.fullscreen,
		display: out.window.display || 0,
		display_name: display ? display.name : '',
		target_display: out.target.label || '',
		waiting_for_display: out.waiting ? out.target.name : '',
		identify: !!out.identify,
		controls_visible: !!(out.controls && out.controls.visible),
		fading: !!(out.fade && out.fade.active),
		fade_ms: state.settings.fadeMs ?? 0,
		clean_output: !!state.settings.cleanOutput,
		start_fullscreen: !!state.settings.startFullscreen,
		show_info: !!state.settings.showInfo,
		always_on_top: !!state.settings.alwaysOnTop,
		silence_notifications: !!state.settings.silenceNotifications,
		notifications: (state.notifications && state.notifications.message) || '',
		extra_ips: state.settings.extraIps || '',
		sources_count: (state.sources || []).length,
		selected,
		last_event: last ? last.text : '',
		last_event_time: last ? time(last.time) : '',
		remote_url: (state.remote && state.remote.urls && state.remote.urls[0]) || '',
	}
}

module.exports = { VARIABLES, variableValues, signalText }
