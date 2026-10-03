// The module as Companion runs it (its own process, Companion's IPC protocol), against a
// stand-in for FeedView's API: definitions, every action's request, Take, feedbacks,
// variables, presets, and the connection states.
const { test, before, after } = require('node:test')
const assert = require('node:assert/strict')
const fs = require('node:fs')
const path = require('node:path')
const { FakeCompanion } = require('./helpers/fake-companion')
const { FakeFeedView } = require('./helpers/fake-feedview')
const { NONE } = require('../src/choices')

let fv, comp

before(async () => {
	fv = new FakeFeedView()
	const port = await fv.start()
	comp = new FakeCompanion()
	await comp.start({ host: '127.0.0.1', port, pin: '', poll: 100 })
	await comp.waitFor(() => comp.status && comp.status.status === 'ok', 5000, 'status ok')
})

after(async () => {
	await comp.stop()
	await fv.stop()
})

const lastCommand = () => fv.commands().at(-1)

test('[REQ-16][REQ-17] connects and offers the same controls as the web page', async () => {
	assert.equal(comp.status.status, 'ok')
	const actions = Object.keys(comp.actions).sort()
	assert.deepEqual(actions, [
		'allow_notification_control',
		'audio_output',
		'audio_pair',
		'cancel_selection',
		'controls',
		'display',
		'display_select',
		'extra_ips',
		'fade_time',
		'fullscreen',
		'identify',
		'mute',
		'reconnect',
		'setting',
		'source',
		'source_select',
		'take',
		'volume',
		'volume_step',
	])
	for (const id of [
		'source_on_output',
		'source_selected',
		'display_on_output',
		'muted',
		'controls_visible',
		'signal_lost',
		'disconnected',
	])
		assert.ok(comp.feedbacks[id], id)
	assert.ok(comp.variableDefinitions.length >= 30)
})

test('[REQ-15][REQ-16] source dropdown: None first, then the sources FeedView reports', () => {
	const choices = comp.actions.source.options.find((o) => o.id === 'source').choices
	assert.deepEqual(
		choices.map((c) => c.id),
		[NONE, 'CAM-PC (Camera 1)', 'GFX-PC (Program)'],
	)
	const displays = comp.actions.display.options.find((o) => o.id === 'display').choices
	assert.deepEqual(
		displays.map((c) => c.id),
		[1, 2],
	)
})

test('[REQ-16] variables follow FeedView', async () => {
	await comp.waitFor(() => comp.variables.source_name === 'Camera 1')
	assert.equal(comp.variables.connected, true)
	assert.equal(comp.variables.signal, 'Live')
	assert.equal(comp.variables.volume, 40)
	assert.equal(comp.variables.sound_output, 'Speakers (Realtek(R) Audio)')
})

// Each action, and the request FeedView's web page would send for the same control.
const CASES = [
	[
		'source',
		{ source: 'GFX-PC (Program)', transition: 'default', fade_ms: 500 },
		'source',
		{ name: 'GFX-PC (Program)' },
	],
	['source', { source: NONE, transition: 'cut', fade_ms: 500 }, 'source', { name: '', fade_ms: '0' }],
	[
		'source',
		{ source: 'CAM-PC (Camera 1)', transition: 'fade', fade_ms: 1500 },
		'source',
		{ name: 'CAM-PC (Camera 1)', fade_ms: '1500' },
	],
	['display', { display: 1, fullscreen: '1' }, 'display', { number: '1', fullscreen: '1' }],
	['display', { display: 2, fullscreen: '0' }, 'display', { number: '2', fullscreen: '0' }],
	['fullscreen', { on: 'toggle' }, 'fullscreen', { on: 'toggle' }],
	['identify', { on: '1' }, 'identify', { on: '1' }],
	['reconnect', { transition: 'default', fade_ms: 500 }, 'reconnect', {}],
	['volume', { value: 63 }, 'volume', { value: '63' }],
	['volume_step', { step: 5 }, 'volume', { value: '+5' }],
	['volume_step', { step: -10 }, 'volume', { value: '-10' }],
	['mute', { on: '0' }, 'mute', { on: '0' }],
	['audio_output', { output: '{out-b}' }, 'audio-output', { id: '{out-b}' }],
	['audio_pair', { first: 3 }, 'audio-pair', { first: '3' }],
	['controls', { on: '0' }, 'controls', { on: '0' }],
	['setting', { setting: 'clean_output', on: 'toggle' }, 'settings', { clean_output: 'toggle' }],
	['setting', { setting: 'silence_notifications', on: '1' }, 'settings', { silence_notifications: '1' }],
	['fade_time', { ms: 1000 }, 'settings', { fade_ms: '1000' }],
	['extra_ips', { ips: ' 10.0.1.20,10.0.2.15 ' }, 'settings', { extra_ips: '10.0.1.20,10.0.2.15' }],
	['allow_notification_control', {}, 'allow-notification-control', {}],
]

test("[REQ-17] covers every command and setting FeedView's web page uses", () => {
	const page = fs.readFileSync(path.join(__dirname, '..', '..', '..', 'web', 'index.html'), 'utf8')
	const webCommands = new Set(
		[...page.matchAll(/cmd\("([a-z-]+)"/g), ...page.matchAll(/api\("\/api\/([a-z-]+)"/g)].map((m) => m[1]),
	)
	webCommands.delete('state')
	const webSettings = new Set([...page.matchAll(/cmd\("settings", \{ ([a-z_]+):/g)].map((m) => m[1]))
	assert.ok(webCommands.size >= 10 && webSettings.size >= 6, 'the page was parsed')
	const sent = new Set(CASES.map((c) => c[2]))
	const sentSettings = new Set(CASES.filter((c) => c[2] === 'settings').flatMap((c) => Object.keys(c[3])))
	sentSettings.add('show_info').add('start_fullscreen').add('always_on_top') // same 'setting' action, other choices
	for (const s of require('../src/choices').SETTINGS) assert.ok(sentSettings.has(s.id))
	for (const c of webCommands) assert.ok(sent.has(c), `no action sends "${c}"`)
	for (const s of webSettings) assert.ok(sentSettings.has(s), `no action sets "${s}"`)
})

test('[REQ-17] every action sends what the web page sends', async () => {
	for (const [actionId, options, command, params] of CASES) {
		const res = await comp.action(actionId, options)
		assert.equal(res.success, true, `${actionId}: ${res.errorMessage}`)
		const sent = lastCommand()
		assert.equal(sent.path, `/api/${command}`, actionId)
		assert.deepEqual(sent.params, params, actionId)
		assert.equal(sent.headers['x-feedview-pin'], '', 'commands carry the header (empty without a PIN)')
	}
})

test('[REQ-17][REQ-15] select, then Take - like the web page', async () => {
	await comp.action('source', { source: 'CAM-PC (Camera 1)', transition: 'cut', fade_ms: 0 })
	const onOut = await comp.subscribe('source_on_output', { source: 'GFX-PC (Program)' })
	const selected = await comp.subscribe('source_selected', { source: 'GFX-PC (Program)' })
	const pending = await comp.subscribe('selection_pending', {})
	await comp.waitFor(() => comp.feedbackValues[onOut] === false && comp.feedbackValues[selected] === false)
	const before = fv.commands('source').length

	assert.equal((await comp.action('source_select', { source: 'GFX-PC (Program)' })).success, true)
	await comp.waitFor(() => comp.feedbackValues[selected] === true && comp.feedbackValues[pending] === true)
	assert.equal(comp.variables.selected, 'Program')
	assert.equal(fv.commands('source').length, before, 'selecting changes nothing on the output')

	assert.equal((await comp.action('take', { transition: 'default', fade_ms: 500 })).success, true)
	assert.deepEqual(lastCommand().params, { name: 'GFX-PC (Program)' })
	await comp.waitFor(() => comp.feedbackValues[onOut] === true && comp.feedbackValues[selected] === false)
	assert.equal(comp.feedbackValues[pending], false)
	assert.equal(comp.variables.selected, '')

	// None, with a cut
	await comp.action('source_select', { source: NONE })
	assert.equal(comp.variables.selected, 'None (black)')
	await comp.action('take', { transition: 'cut', fade_ms: 500 })
	assert.deepEqual(lastCommand().params, { name: '', fade_ms: '0' })
	const none = await comp.subscribe('source_on_output', { source: NONE })
	await comp.waitFor(() => comp.feedbackValues[none] === true)

	// A display
	await comp.action('display_select', { display: 1 })
	await comp.action('take', { transition: 'default' })
	assert.deepEqual(lastCommand().params, { number: '1', fullscreen: '1' })

	// Cancel, and Take with nothing selected
	await comp.action('source_select', { source: 'CAM-PC (Camera 1)' })
	await comp.action('cancel_selection', {})
	assert.equal(comp.variables.selected, '')
	const res = await comp.action('take', {})
	assert.equal(res.success, false)
	assert.match(res.errorMessage, /Nothing is selected/)
})

test('[REQ-16] feedbacks follow FeedView: muted, settings, controls showing, signal lost', async () => {
	const muted = await comp.subscribe('muted', {})
	const clean = await comp.subscribe('setting', { setting: 'clean_output' })
	const controls = await comp.subscribe('controls_visible', {})
	const lost = await comp.subscribe('signal_lost', {})
	fv.state.audio.muted = true
	fv.state.settings.cleanOutput = true
	fv.state.output.controls.visible = true
	fv.state.source.name = 'CAM-PC (Camera 1)'
	fv.state.source.signalLost = true
	await comp.waitFor(
		() =>
			comp.feedbackValues[muted] &&
			comp.feedbackValues[clean] &&
			comp.feedbackValues[controls] &&
			comp.feedbackValues[lost],
		3000,
		'feedbacks on',
	)
	assert.equal(comp.variables.signal, 'No video')
	fv.state.source.signalLost = false
	fv.state.audio.muted = false
	await comp.waitFor(() => !comp.feedbackValues[muted] && !comp.feedbackValues[lost], 3000, 'feedbacks off')
})

test('[REQ-16][REQ-15] new sources show up in the dropdowns and presets (None stays first)', async () => {
	const updates = comp.definitionUpdates
	fv.state.sources.push('NEW-PC (Slides)')
	await comp.waitFor(() => comp.definitionUpdates > updates, 3000, 'definitions rebuilt')
	const ids = comp.actions.source.options.find((o) => o.id === 'source').choices.map((c) => c.id)
	assert.deepEqual(ids, [NONE, 'CAM-PC (Camera 1)', 'GFX-PC (Program)', 'NEW-PC (Slides)'])
	const names = comp.presets.map((p) => p.name)
	assert.ok(names.includes('Select NEW-PC (Slides)'))
	assert.ok(names.includes('NEW-PC (Slides) on the output'))
	const first = comp.presets.find((p) => p.category === 'Sources: select, then Take')
	assert.equal(first.name, 'Select None (black output)')
	// Unchanged choices don't rebuild anything.
	const again = comp.definitionUpdates
	await new Promise((r) => setTimeout(r, 400))
	assert.equal(comp.definitionUpdates, again)
})

test('[REQ-16][REQ-17] presets cover the controls, with feedback colours', () => {
	const byName = Object.fromEntries(comp.presets.map((p) => [p.name, p]))
	for (const n of [
		'Take (with the fade set in FeedView)',
		'Cut (take without a fade)',
		'Fullscreen on display 2',
		'Fullscreen on / off',
		'Show display numbers',
		"Hide FeedView's controls (amber while they show)",
		'Mute on / off',
		'Volume up 5 %',
		'Sound output: DELL U2720Q (HDMI)',
		'Play channels Ch 3-4',
		'No fade between sources (cut)',
	])
		assert.ok(byName[n], n)
	const take = byName['Take (with the fade set in FeedView)']
	assert.equal(take.steps[0].down[0].actionId, 'take')
	assert.equal(take.feedbacks[0].feedbackId, 'selection_pending')
	assert.match(byName['Mute on / off'].style.text, /\$\(feedview:volume\)/)
})

test("[REQ-16] a refused command fails the action with FeedView's message", async () => {
	fv.refuse = { action: 'identify', status: 409, message: 'Only one display is connected' }
	const res = await comp.action('identify', { on: '1' })
	assert.equal(res.success, false)
	assert.match(res.errorMessage, /Only one display is connected/)
	assert.equal(comp.status.status, 'ok', 'a refused command is not a connection problem')
})

test('[REQ-16] wrong PIN: status says so, and the PIN is not tried again (FeedView locks an address after 5)', async () => {
	fv.pinRequired = true
	await comp.waitFor(() => comp.status.status === 'authentication_failure', 3000, 'auth failure')
	assert.match(comp.status.message, /PIN/)
	await comp.waitFor(() => comp.variables.connected === false, 2000, 'offline variables')
	const off = await comp.subscribe('disconnected', {})
	await comp.waitFor(() => comp.feedbackValues[off] === true)
	const stateCalls = () => fv.requests.filter((r) => r.path === '/api/state').length
	const n = stateCalls()
	await new Promise((r) => setTimeout(r, 1200))
	assert.ok(stateCalls() <= n + 1, 'no repeated state requests while the PIN is wrong')

	await comp.updateConfig({ host: '127.0.0.1', port: fv.port, pin: '1234', poll: 100 })
	await comp.waitFor(
		() => comp.status.status === 'authentication_failure' && /Wrong PIN/.test(comp.status.message),
		3000,
	)
	const wrong = fv.requests.filter((r) => r.headers['x-feedview-pin'] === '1234').length
	await new Promise((r) => setTimeout(r, 1200))
	assert.equal(
		fv.requests.filter((r) => r.headers['x-feedview-pin'] === '1234').length,
		wrong,
		'the wrong PIN is sent once',
	)

	await comp.updateConfig({ host: '127.0.0.1', port: fv.port, pin: '4821', poll: 100 })
	await comp.waitFor(() => comp.status.status === 'ok', 3000, 'ok with the right PIN')
	await comp.waitFor(() => comp.feedbackValues[off] === false)
	assert.equal((await comp.action('mute', { on: '1' })).success, true)
	assert.equal(lastCommand().headers['x-feedview-pin'], '4821')

	fv.pinRequired = false // the operator turns the PIN off in FeedView: still fine
	await new Promise((r) => setTimeout(r, 300))
	assert.equal(comp.status.status, 'ok')
})

test('[REQ-16] FeedView not reachable: connection failure, then back by itself', async () => {
	const { port, state } = fv
	await fv.stop()
	await comp.waitFor(() => comp.status.status === 'connection_failure', 4000, 'connection failure')
	assert.match(comp.status.message, /reach FeedView/)
	assert.equal(comp.variables.connected, false)
	const res = await comp.action('mute', { on: '0' })
	assert.equal(res.success, false)
	fv = new FakeFeedView() // FeedView restarted, same port
	fv.state = state
	await fv.start(port)
	await comp.waitFor(() => comp.status.status === 'ok', 6000, 'reconnected')
	assert.equal(comp.variables.connected, true)
})

test('[REQ-16] the packaged module (what Companion imports) starts and works the same', async (t) => {
	const dir = path.join(__dirname, '..', 'pkg')
	if (!fs.existsSync(path.join(dir, 'main.js'))) return t.skip('not packaged (npm run package)')
	const packaged = new FakeCompanion()
	try {
		await packaged.start({ host: '127.0.0.1', port: fv.port, pin: '', poll: 100 }, { dir })
		await packaged.waitFor(() => packaged.status && packaged.status.status === 'ok', 5000, 'packaged module connected')
		assert.deepEqual(Object.keys(packaged.actions).sort(), Object.keys(comp.actions).sort())
		assert.equal((await packaged.action('mute', { on: '1' })).success, true)
		assert.deepEqual(lastCommand().params, { on: '1' })
	} finally {
		await packaged.stop()
	}
})
