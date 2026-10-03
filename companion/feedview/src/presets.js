// Ready-made buttons, built from what FeedView reports (its sources, displays, sound outputs);
// rebuilt when those change.
const { NONE, SETTINGS, AUDIO_PAIRS, sourceList, splitSourceName, displayChoices, outputChoices } = require('./choices')
const { STYLE, WHITE, BLACK } = require('./feedbacks')

const C = {
	select: 'Sources: select, then Take',
	direct: 'Sources: straight to the output',
	displays: 'Displays and fullscreen',
	audio: 'Audio',
	settings: 'Settings',
	status: 'Status',
}

function buildPresets(self) {
	const v = (id) => `$(${self.label}:${id})`
	const presets = {}
	const button = (category, name, text, down, feedbacks = [], style = {}) => ({
		type: 'button',
		category,
		name,
		style: { text, size: 'auto', color: WHITE, bgcolor: BLACK, ...style },
		steps: [{ down, up: [] }],
		feedbacks,
	})
	const fb = (feedbackId, options, style) => ({ feedbackId, options, style })

	// ---- Sources (None first: the safe black output)
	const sources = [
		{ id: NONE, text: 'NONE\nblack', name: 'None (black output)' },
		...sourceList(self.state).map((n) => ({ id: n, text: splitSourceName(n).name, name: n })),
	]
	sources.forEach((s, i) => {
		presets[`select_source_${i}`] = button(
			C.select,
			`Select ${s.name}`,
			s.text,
			[{ actionId: 'source_select', options: { source: s.id } }],
			[fb('source_on_output', { source: s.id }, STYLE.program), fb('source_selected', { source: s.id }, STYLE.preview)],
		)
		presets[`source_${i}`] = button(
			C.direct,
			`${s.name} on the output`,
			s.text,
			[{ actionId: 'source', options: { source: s.id, transition: 'default', fade_ms: 500 } }],
			[fb('source_on_output', { source: s.id }, STYLE.program)],
		)
	})
	presets.take = button(
		C.select,
		'Take (with the fade set in FeedView)',
		'TAKE',
		[{ actionId: 'take', options: { transition: 'default', fade_ms: 500 } }],
		[fb('selection_pending', {}, STYLE.preview)],
	)
	presets.cut = button(
		C.select,
		'Cut (take without a fade)',
		'CUT',
		[{ actionId: 'take', options: { transition: 'cut', fade_ms: 500 } }],
		[fb('selection_pending', {}, STYLE.preview)],
	)
	presets.cancel = button(C.select, 'Cancel the selection', 'CANCEL', [{ actionId: 'cancel_selection', options: {} }])

	// ---- Displays
	for (const d of displayChoices(self.state)) {
		presets[`display_${d.id}`] = button(
			C.displays,
			`Fullscreen on display ${d.id}`,
			`DISPLAY\n${d.id}`,
			[{ actionId: 'display', options: { display: d.id, fullscreen: '1' } }],
			[fb('display_on_output', { display: d.id }, STYLE.program), fb('waiting_for_display', {}, STYLE.warning)],
		)
		presets[`select_display_${d.id}`] = button(
			C.displays,
			`Select display ${d.id} for Take`,
			`SELECT\nDISPLAY ${d.id}`,
			[{ actionId: 'display_select', options: { display: d.id } }],
			[
				fb('display_on_output', { display: d.id }, STYLE.program),
				fb('display_selected', { display: d.id }, STYLE.preview),
			],
		)
	}
	presets.fullscreen = button(
		C.displays,
		'Fullscreen on / off',
		'FULL\nSCREEN',
		[{ actionId: 'fullscreen', options: { on: 'toggle' } }],
		[fb('fullscreen', {}, STYLE.program), fb('waiting_for_display', {}, STYLE.warning)],
	)
	presets.identify = button(
		C.displays,
		'Show display numbers',
		'DISPLAY\nNUMBERS',
		[{ actionId: 'identify', options: { on: 'toggle' } }],
		[fb('identify', {}, STYLE.active)],
	)
	presets.hide_controls = button(
		C.displays,
		"Hide FeedView's controls (amber while they show)",
		'HIDE\nCONTROLS',
		[{ actionId: 'controls', options: { on: '0' } }],
		[fb('controls_visible', {}, STYLE.warning)],
	)

	// ---- Audio
	presets.mute = button(
		C.audio,
		'Mute on / off',
		`MUTE\n${v('volume')}%`,
		[{ actionId: 'mute', options: { on: 'toggle' } }],
		[fb('muted', {}, STYLE.warning)],
	)
	presets.volume_up = button(C.audio, 'Volume up 5 %', `VOL +\n${v('volume')}%`, [
		{ actionId: 'volume_step', options: { step: 5 } },
	])
	presets.volume_down = button(C.audio, 'Volume down 5 %', `VOL -\n${v('volume')}%`, [
		{ actionId: 'volume_step', options: { step: -5 } },
	])
	outputChoices(self.state).forEach((o, i) => {
		presets[`output_${i}`] = button(
			C.audio,
			`Sound output: ${o.label}`,
			o.label,
			[{ actionId: 'audio_output', options: { output: o.id } }],
			[fb('audio_output', { output: o.id }, STYLE.active)],
			{ size: '7' },
		)
	})
	for (const p of AUDIO_PAIRS.slice(0, 4)) {
		presets[`pair_${p.id}`] = button(
			C.audio,
			`Play channels ${p.label}`,
			p.label.toUpperCase(),
			[{ actionId: 'audio_pair', options: { first: p.id } }],
			[fb('audio_pair', { first: p.id }, STYLE.active)],
		)
	}

	// ---- Settings
	for (const s of SETTINGS) {
		presets[`setting_${s.id}`] = button(
			C.settings,
			`${s.label}: on / off`,
			s.label.replace(/ \(.*\)$/, ''),
			[{ actionId: 'setting', options: { setting: s.id, on: 'toggle' } }],
			[fb('setting', { setting: s.id }, STYLE.active)],
			{ size: '7' },
		)
	}
	for (const ms of [0, 500, 1000, 2000]) {
		presets[`fade_${ms}`] = button(
			C.settings,
			ms ? `Fade between sources: ${ms / 1000} s` : 'No fade between sources (cut)',
			ms ? `FADE\n${ms / 1000} s` : 'CUT\n(no fade)',
			[{ actionId: 'fade_time', options: { ms } }],
			[fb('fade_time', { ms }, STYLE.active)],
		)
	}
	presets.allow_notifications = button(
		C.settings,
		'Ask for permission to silence notifications (Windows prompt on the FeedView computer)',
		'ALLOW\nSILENCE',
		[{ actionId: 'allow_notification_control', options: {} }],
		[fb('notifications_silenced', {}, STYLE.active)],
		{ size: '7' },
	)

	// ---- Status
	presets.status = button(
		C.status,
		'What is on the output, and its signal (red: lost, amber: no picture yet)',
		`${v('source_name')}\n${v('signal')}`,
		[],
		[
			fb('signal_lost', {}, STYLE.program),
			fb('no_picture', {}, STYLE.warning),
			fb('disconnected', {}, { bgcolor: STYLE.program.bgcolor, color: WHITE, text: 'FEEDVIEW\nOFFLINE' }),
		],
		{ size: '7' },
	)
	presets.signal = button(C.status, 'Video format and frame rate', `${v('resolution')}\n${v('fps')} fps`, [], [], {
		size: '7',
	})

	return presets
}

module.exports = { buildPresets }
