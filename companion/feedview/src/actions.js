// Actions: the same controls as FeedView's web page. Changes to what the audience sees can be
// done directly, or as on the web page: select (green), then Take.
const {
	NONE,
	SWITCH,
	SETTINGS,
	TRANSITIONS,
	AUDIO_PAIRS,
	sourceChoices,
	toSourceName,
	displayChoices,
	outputChoices,
} = require('./choices')

/** Transition options -> the fade_ms parameter of FeedView's source/reconnect commands. */
function fadeParam(options) {
	if (options.transition === 'cut') return { fade_ms: 0 }
	if (options.transition === 'fade') {
		const ms = Math.round(Number(options.fade_ms))
		return { fade_ms: Number.isFinite(ms) ? Math.min(10000, Math.max(0, ms)) : 500 }
	}
	return {}
}

function buildActions(self) {
	const sourceOpt = {
		type: 'dropdown',
		id: 'source',
		label: 'Source',
		default: NONE,
		choices: sourceChoices(self.state),
		allowCustom: true,
		tooltip: 'None is a black output. You can also type the full NDI name, e.g. STUDIO-PC (Program)',
	}
	const displayOpt = {
		type: 'dropdown',
		id: 'display',
		label: 'Display (numbered left to right)',
		default: 1,
		choices: displayChoices(self.state),
		allowCustom: true,
	}
	const switchOpt = { type: 'dropdown', id: 'on', label: 'Action', default: 'toggle', choices: SWITCH }
	const transitionOpts = [
		{ type: 'dropdown', id: 'transition', label: 'Transition', default: 'default', choices: TRANSITIONS },
		{
			type: 'number',
			id: 'fade_ms',
			label: 'Fade time (ms)',
			default: 500,
			min: 0,
			max: 10000,
			isVisibleExpression: "$(options:transition) == 'fade'",
		},
	]
	const outputs = outputChoices(self.state)

	return {
		source: {
			name: 'Source: put on the output',
			description:
				'Takes the source at once. With a picture up, it stays until the new source is ready, then fades (or cuts). None = black.',
			options: [sourceOpt, ...transitionOpts],
			callback: async (a) => self.run('source', { name: toSourceName(a.options.source), ...fadeParam(a.options) }),
		},
		source_select: {
			name: 'Source: select for Take (green)',
			description: 'Like tapping a source on the web page; Take puts it on the output',
			options: [sourceOpt],
			callback: async (a) => self.select('source', toSourceName(a.options.source)),
		},
		display_select: {
			name: 'Display: select for Take (green)',
			description: 'Like tapping a screen on the web page; Take puts FeedView fullscreen there',
			options: [displayOpt],
			callback: async (a) => self.select('display', Number(a.options.display)),
		},
		take: {
			name: 'Take: put the selection on the output',
			description: 'The selected source (with the transition below) or display',
			options: transitionOpts,
			callback: async (a) => self.take(fadeParam(a.options)),
		},
		cancel_selection: {
			name: 'Take: cancel the selection',
			options: [],
			callback: async () => self.select(null),
		},
		display: {
			name: 'Display: put FeedView on a display',
			options: [
				displayOpt,
				{
					type: 'dropdown',
					id: 'fullscreen',
					label: 'As',
					default: '1',
					choices: [
						{ id: '1', label: 'Fullscreen' },
						{ id: '0', label: 'A window (moved there)' },
					],
				},
			],
			callback: async (a) =>
				self.run('display', { number: Number(a.options.display), fullscreen: a.options.fullscreen }),
		},
		fullscreen: {
			name: 'Fullscreen',
			description: 'On the chosen display; also brings FeedView back on top',
			options: [switchOpt],
			callback: async (a) => self.run('fullscreen', { on: a.options.on }),
		},
		identify: {
			name: 'Show display numbers',
			description: 'Big numbers on every screen, projector included',
			options: [switchOpt],
			callback: async (a) => self.run('identify', { on: a.options.on }),
		},
		reconnect: {
			name: 'Source: reconnect',
			description: 'Reconnects the current source; its picture stays until the new connection is up',
			options: transitionOpts,
			callback: async (a) => self.run('reconnect', fadeParam(a.options)),
		},
		volume: {
			name: 'Volume: set',
			description: "The computer's system volume on Windows",
			options: [{ type: 'number', id: 'value', label: 'Volume (%)', default: 50, min: 0, max: 100, range: true }],
			callback: async (a) => self.run('volume', { value: Math.round(Number(a.options.value)) }),
		},
		volume_step: {
			name: 'Volume: up / down',
			options: [{ type: 'number', id: 'step', label: 'Step (%, negative = down)', default: 5, min: -100, max: 100 }],
			callback: async (a) => {
				const step = Math.round(Number(a.options.step)) || 0
				return self.run('volume', { value: step >= 0 ? `+${step}` : String(step) })
			},
		},
		mute: {
			name: 'Mute',
			options: [switchOpt],
			callback: async (a) => self.run('mute', { on: a.options.on }),
		},
		audio_output: {
			name: "Sound output: choose the computer's output",
			description: 'FeedView plays to it (Windows)',
			options: [
				{
					type: 'dropdown',
					id: 'output',
					label: 'Sound output',
					default: outputs.length ? outputs[0].id : '',
					choices: outputs,
					allowCustom: true,
				},
			],
			callback: async (a) => self.run('audio-output', { id: a.options.output }),
		},
		audio_pair: {
			name: 'Audio channels: choose the pair to play',
			options: [{ type: 'dropdown', id: 'first', label: 'Channels', default: 1, choices: AUDIO_PAIRS }],
			callback: async (a) => self.run('audio-pair', { first: Number(a.options.first) }),
		},
		controls: {
			name: "FeedView's controls: hide or show",
			description: "FeedView's own controls and panels (e.g. the Remote panel with its QR code) on its screen",
			options: [
				{
					type: 'dropdown',
					id: 'on',
					label: 'Action',
					default: '0',
					choices: [
						{ id: '0', label: 'Hide (closes panels too)' },
						{ id: '1', label: 'Show' },
					],
				},
			],
			callback: async (a) => self.run('controls', { on: a.options.on }),
		},
		setting: {
			name: 'Setting: on / off',
			options: [
				{ type: 'dropdown', id: 'setting', label: 'Setting', default: SETTINGS[0].id, choices: SETTINGS },
				switchOpt,
			],
			callback: async (a) => self.run('settings', { [a.options.setting]: a.options.on }),
		},
		fade_time: {
			name: 'Fade between sources: set the time',
			options: [{ type: 'number', id: 'ms', label: 'Fade (ms, 0 = cut)', default: 500, min: 0, max: 10000 }],
			callback: async (a) => self.run('settings', { fade_ms: Math.round(Number(a.options.ms)) }),
		},
		extra_ips: {
			name: 'Extra discovery IPs: set',
			description: 'For sources on other subnets or VLANs',
			options: [
				{
					type: 'textinput',
					id: 'ips',
					label: 'IPs (comma separated, empty = none)',
					default: '',
					useVariables: true,
				},
			],
			callback: async (a) => self.run('settings', { extra_ips: String(a.options.ips ?? '').trim() }),
		},
		allow_notification_control: {
			name: 'Notifications: ask for the one-time permission',
			description: 'Shows a Windows prompt on the FeedView computer, so FeedView can switch notifications off',
			options: [],
			callback: async () => self.run('allow-notification-control', {}),
		},
	}
}

module.exports = { buildActions, fadeParam }
