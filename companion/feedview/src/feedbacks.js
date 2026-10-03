// Feedbacks: button colours from FeedView's state. Red = on the output (program), green =
// selected for Take (preview), as on FeedView's web page.
const { combineRgb } = require('@companion-module/base')
const { NONE, SETTINGS, AUDIO_PAIRS, sourceChoices, toSourceName, displayChoices, outputChoices } = require('./choices')

const WHITE = combineRgb(255, 255, 255)
const BLACK = combineRgb(0, 0, 0)
const RED = combineRgb(200, 0, 0)
const GREEN = combineRgb(0, 150, 50)
const AMBER = combineRgb(240, 168, 28)
const BLUE = combineRgb(0, 90, 180)

const STYLE = {
	program: { bgcolor: RED, color: WHITE },
	preview: { bgcolor: GREEN, color: WHITE },
	warning: { bgcolor: AMBER, color: BLACK },
	active: { bgcolor: BLUE, color: WHITE },
}

// The conditions, as plain functions of FeedView's state (null = not connected) and the
// selection waiting for Take. Exported for the tests.
const is = {
	disconnected: (state) => !state,
	sourceOnOutput: (state, id) => !!state && state.source.name === toSourceName(id),
	sourceSelected: (_state, sel, id) => !!sel && sel.kind === 'source' && sel.value === toSourceName(id),
	displayOnOutput: (state, n) => !!state && state.output.fullscreen && state.output.window.display === Number(n),
	displaySelected: (_state, sel, n) => !!sel && sel.kind === 'display' && sel.value === Number(n),
	selectionPending: (_state, sel) => !!sel,
	fullscreen: (state) => !!state && !!state.output.fullscreen,
	waitingForDisplay: (state) => !!state && !!state.output.waiting,
	identify: (state) => !!state && !!state.output.identify,
	controlsVisible: (state) => !!state && !!(state.output.controls && state.output.controls.visible),
	fading: (state) => !!state && !!(state.output.fade && state.output.fade.active),
	signalLost: (state) => !!state && !!state.source.name && !!state.source.signalLost,
	noPicture: (state) => !!state && !!state.source.name && !state.source.hasPicture,
	muted: (state) => !!state && !!state.audio.muted,
	audioOutput: (state, id) => !!state && state.audio.output === id,
	audioPair: (state, first) => !!state && state.audio.firstChannel === Number(first),
	setting: (state, id) => {
		const s = SETTINGS.find((x) => x.id === id)
		return !!state && !!s && !!state.settings[s.key]
	},
	fadeTime: (state, ms) => !!state && state.settings.fadeMs === Number(ms),
	notificationsSilenced: (state) => !!state && !!(state.notifications && state.notifications.silenced),
}

function buildFeedbacks(self) {
	const state = () => self.state
	const sel = () => self.selection
	const sourceOpt = {
		type: 'dropdown',
		id: 'source',
		label: 'Source',
		default: NONE,
		choices: sourceChoices(self.state),
		allowCustom: true,
	}
	const displayOpt = {
		type: 'dropdown',
		id: 'display',
		label: 'Display',
		default: 1,
		choices: displayChoices(self.state),
	}
	const outputs = outputChoices(self.state)
	const bool = (name, description, style, callback, options = []) => ({
		type: 'boolean',
		name,
		description,
		defaultStyle: style,
		showInvert: true,
		options,
		callback,
	})
	return {
		source_on_output: bool(
			'Source: on the output',
			'Red when this source (or None) is on the output',
			STYLE.program,
			(fb) => is.sourceOnOutput(state(), fb.options.source),
			[sourceOpt],
		),
		source_selected: bool(
			'Source: selected for Take',
			'Green while this source waits for Take',
			STYLE.preview,
			(fb) => is.sourceSelected(state(), sel(), fb.options.source),
			[sourceOpt],
		),
		display_on_output: bool(
			'Display: FeedView fullscreen on it',
			'Red when FeedView is fullscreen on this display',
			STYLE.program,
			(fb) => is.displayOnOutput(state(), fb.options.display),
			[displayOpt],
		),
		display_selected: bool(
			'Display: selected for Take',
			'Green while this display waits for Take',
			STYLE.preview,
			(fb) => is.displaySelected(state(), sel(), fb.options.display),
			[displayOpt],
		),
		selection_pending: bool(
			'Take: something is selected',
			'Green while a source or display waits for Take (for the Take button)',
			STYLE.preview,
			() => is.selectionPending(state(), sel()),
		),
		fullscreen: bool('Fullscreen', 'FeedView is fullscreen', STYLE.program, () => is.fullscreen(state())),
		waiting_for_display: bool(
			'Display: waiting for a disconnected display',
			'The chosen display is unplugged or off; FeedView goes back to it when it returns',
			STYLE.warning,
			() => is.waitingForDisplay(state()),
		),
		identify: bool('Display numbers showing', 'The big display numbers are on every screen', STYLE.active, () =>
			is.identify(state()),
		),
		controls_visible: bool(
			"FeedView's controls on its screen",
			"FeedView's own controls or a panel are showing on its screen (Hide them with the Controls action)",
			STYLE.warning,
			() => is.controlsVisible(state()),
		),
		fading: bool('Fade running', 'A fade between sources is running', STYLE.active, () => is.fading(state())),
		signal_lost: bool(
			'Signal lost',
			'The source on the output stopped sending video (its last picture is still up)',
			STYLE.program,
			() => is.signalLost(state()),
		),
		no_picture: bool(
			'No picture yet',
			'A source is chosen but sends no picture yet (connecting, or no video)',
			STYLE.warning,
			() => is.noPicture(state()),
		),
		muted: bool('Muted', 'Sound is muted', STYLE.warning, () => is.muted(state())),
		audio_output: bool(
			'Sound output: in use',
			"This is the computer's sound output",
			STYLE.active,
			(fb) => is.audioOutput(state(), fb.options.output),
			[
				{
					type: 'dropdown',
					id: 'output',
					label: 'Sound output',
					default: outputs.length ? outputs[0].id : '',
					choices: outputs,
					allowCustom: true,
				},
			],
		),
		audio_pair: bool(
			'Audio channels: playing',
			'These source channels are playing',
			STYLE.active,
			(fb) => is.audioPair(state(), fb.options.first),
			[{ type: 'dropdown', id: 'first', label: 'Channels', default: 1, choices: AUDIO_PAIRS }],
		),
		setting: bool(
			'Setting: on',
			'This FeedView setting is on',
			STYLE.active,
			(fb) => is.setting(state(), fb.options.setting),
			[{ type: 'dropdown', id: 'setting', label: 'Setting', default: SETTINGS[0].id, choices: SETTINGS }],
		),
		fade_time: bool(
			'Fade time: set to',
			'The fade between sources is set to this time',
			STYLE.active,
			(fb) => is.fadeTime(state(), fb.options.ms),
			[{ type: 'number', id: 'ms', label: 'Fade (ms, 0 = cut)', default: 500, min: 0, max: 10000 }],
		),
		notifications_silenced: bool(
			'Notifications silenced',
			'OS notifications are switched off (invert it to warn when they are not)',
			STYLE.active,
			() => is.notificationsSilenced(state()),
		),
		disconnected: bool('FeedView not reachable', "Companion can't reach FeedView", STYLE.program, () =>
			is.disconnected(state()),
		),
	}
}

module.exports = { buildFeedbacks, is, STYLE, WHITE, BLACK, RED, GREEN, AMBER, BLUE }
